/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "parse/propstrings.h"
#include "select/computed.h"
#include "select/dispatch.h"
#include "select/select.h"
#include "select/vars.h"

#define VARS_MAX_DEPTH 16
#define VARS_CACHE_SIZE 2048
#define N_PROP_HANDLERS (LAST_PROP - FIRST_PROP + 1)

typedef struct vars_buf {
	char *data;
	size_t len;
	size_t alloc;
} vars_buf;

typedef struct vars_cache_entry {
	uint16_t prop;
	lwc_string *text;
	lwc_string *url;
	css_stylesheet *sheet;
} vars_cache_entry;

typedef struct vars_scope {
	const css_computed_vars *own;
	lwc_string **resolved;
	uint8_t *state;
	const css_computed_vars *parent;
} vars_scope;

static vars_cache_entry vars_cache[VARS_CACHE_SIZE];
static uint16_t *prop_ops[N_PROP_HANDLERS];
static uint16_t prop_n_ops[N_PROP_HANDLERS];
static bool prop_ops_done[N_PROP_HANDLERS];
static lwc_string **propstrings;

static bool vars_scope_lookup(vars_scope *scope,
			      const char *name,
			      size_t len,
			      int depth,
			      const char **value,
			      size_t *value_len);

static bool buf_append(vars_buf *b, const char *data, size_t n)
{
	if (n > SIZE_MAX / 2) {
		return false;
	}
	if (b->len + n + 1 > b->alloc) {
		size_t want = b->alloc == 0 ? 128 : b->alloc * 2;
		char *grown;
		while (want < b->len + n + 1) {
			want *= 2;
		}
		grown = realloc(b->data, want);
		if (grown == NULL) {
			return false;
		}
		b->data = grown;
		b->alloc = want;
	}
	memcpy(b->data + b->len, data, n);
	b->len += n;
	b->data[b->len] = '\0';
	return true;
}

static inline uint8_t vars_rank(css_origin origin, bool important)
{
	return important ? 5 - origin : origin;
}

css_computed_vars *css__vars_ref(css_computed_vars *vars)
{
	if (vars != NULL) {
		vars->refcnt++;
	}
	return vars;
}

void css__vars_unref(css_computed_vars *vars)
{
	if (vars == NULL || --vars->refcnt > 0) {
		return;
	}
	for (uint32_t i = 0; i < vars->n_custom; i++) {
		lwc_string_unref(vars->custom[i].name);
		lwc_string_unref(vars->custom[i].value);
	}
	for (uint32_t i = 0; i < vars->n_pending; i++) {
		lwc_string_unref(vars->pending[i].value);
		if (vars->pending[i].params.url != NULL) {
			lwc_string_unref(vars->pending[i].params.url);
		}
		free(vars->pending[i].ops);
	}
	free(vars->custom);
	free(vars->pending);
	free(vars);
}

static bool vars_is_name_char(char c)
{
	return isalnum((unsigned char)c) || c == '-' || c == '_' ||
	       (unsigned char)c >= 0x80;
}

static size_t vars_match_paren(const char *s, size_t len, size_t i)
{
	int depth = 1;
	char quote = 0;

	for (; i < len; i++) {
		char c = s[i];
		if (quote != 0) {
			if (c == '\\') {
				i++;
			} else if (c == quote) {
				quote = 0;
			}
		} else if (c == '"' || c == '\'') {
			quote = c;
		} else if (c == '(') {
			depth++;
		} else if (c == ')') {
			if (--depth == 0) {
				return i;
			}
		}
	}
	return len;
}

static bool vars_substitute(vars_scope *scope,
			    const char *s,
			    size_t len,
			    int depth,
			    vars_buf *out)
{
	size_t i = 0, start = 0;

	if (depth > VARS_MAX_DEPTH) {
		return false;
	}
	while (i + 4 <= len) {
		if (strncasecmp(s + i, "var(", 4) != 0 ||
		    (i > 0 && vars_is_name_char(s[i - 1]))) {
			i++;
			continue;
		}
		size_t close = vars_match_paren(s, len, i + 4);
		size_t p = i + 4, name_start, name_end;
		const char *value;
		size_t value_len;

		if (!buf_append(out, s + start, i - start)) {
			return false;
		}
		while (p < close && isspace((unsigned char)s[p])) {
			p++;
		}
		name_start = p;
		while (p < close && s[p] != ',' &&
		       !isspace((unsigned char)s[p])) {
			p++;
		}
		name_end = p;
		while (p < close && isspace((unsigned char)s[p])) {
			p++;
		}
		if (vars_scope_lookup(scope,
				      s + name_start,
				      name_end - name_start,
				      depth,
				      &value,
				      &value_len)) {
			if (!buf_append(out, value, value_len)) {
				return false;
			}
		} else if (p < close && s[p] == ',') {
			if (!vars_substitute(scope,
					     s + p + 1,
					     close - p - 1,
					     depth + 1,
					     out)) {
				return false;
			}
		} else {
			return false;
		}
		i = close < len ? close + 1 : len;
		start = i;
	}
	return buf_append(out, s + start, len - start);
}

static bool vars_scope_lookup(vars_scope *scope,
			      const char *name,
			      size_t len,
			      int depth,
			      const char **value,
			      size_t *value_len)
{
	if (scope->own != NULL) {
		for (uint32_t k = scope->own->n_custom; k-- > 0;) {
			lwc_string *n = scope->own->custom[k].name;
			if (lwc_string_length(n) != len ||
			    memcmp(lwc_string_data(n), name, len) != 0) {
				continue;
			}
			if (scope->state[k] == 0) {
				lwc_string *raw = scope->own->custom[k].value;
				vars_buf b = {NULL, 0, 0};
				scope->state[k] = 1;
				if (vars_substitute(scope,
						    lwc_string_data(raw),
						    lwc_string_length(raw),
						    depth + 1,
						    &b) &&
				    lwc_intern_string(b.data != NULL ? b.data
								     : "",
						      b.len,
						      &scope->resolved[k]) ==
					    lwc_error_ok) {
					scope->state[k] = 2;
				} else {
					scope->state[k] = 3;
				}
				free(b.data);
			}
			if (scope->state[k] != 2) {
				return false;
			}
			*value = lwc_string_data(scope->resolved[k]);
			*value_len = lwc_string_length(scope->resolved[k]);
			return true;
		}
	}
	if (scope->parent != NULL) {
		for (uint32_t k = 0; k < scope->parent->n_custom; k++) {
			lwc_string *n = scope->parent->custom[k].name;
			if (lwc_string_length(n) == len &&
			    memcmp(lwc_string_data(n), name, len) == 0) {
				*value = lwc_string_data(
					scope->parent->custom[k].value);
				*value_len = lwc_string_length(
					scope->parent->custom[k].value);
				return true;
			}
		}
	}
	return false;
}

static css_stylesheet *
vars_parse(uint16_t prop, const char *text, size_t len, const css_var_params *p)
{
	css_stylesheet_params params;
	css_stylesheet *sheet = NULL;
	vars_buf b = {NULL, 0, 0};
	lwc_string *name;
	css_error error;

	if (propstrings == NULL &&
	    css__propstrings_get(&propstrings) != CSS_OK) {
		return NULL;
	}
	name = propstrings[FIRST_PROP + prop];
	if (!buf_append(&b, lwc_string_data(name), lwc_string_length(name)) ||
	    !buf_append(&b, ":", 1) || !buf_append(&b, text, len)) {
		free(b.data);
		return NULL;
	}
	memset(&params, 0, sizeof(params));
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = p->level;
	params.charset = "UTF-8";
	params.url = p->url != NULL ? lwc_string_data(p->url) : "";
	params.allow_quirks = p->quirks;
	params.inline_style = true;
	params.resolve = p->resolve;
	params.resolve_pw = p->resolve_pw;
	params.color = p->color;
	params.color_pw = p->color_pw;
	params.font = p->font;
	params.font_pw = p->font_pw;
	if (css_stylesheet_create(&params, &sheet) != CSS_OK) {
		free(b.data);
		return NULL;
	}
	error = css_stylesheet_append_data(sheet,
					   (const uint8_t *)b.data,
					   b.len);
	free(b.data);
	if (error == CSS_OK || error == CSS_NEEDDATA) {
		error = css_stylesheet_data_done(sheet);
	}
	if (error != CSS_OK) {
		css_stylesheet_destroy(sheet);
		return NULL;
	}
	return sheet;
}

static const css_style *vars_sheet_style(const css_stylesheet *sheet)
{
	const css_rule_selector *rule;

	if (sheet == NULL || sheet->rule_count != 1 ||
	    sheet->rule_list == NULL ||
	    sheet->rule_list->type != CSS_RULE_SELECTOR) {
		return NULL;
	}
	rule = (const css_rule_selector *)sheet->rule_list;
	return rule->style;
}

static const css_style *vars_parse_cached(uint16_t prop,
					  const char *text,
					  size_t len,
					  const css_var_params *p)
{
	lwc_string *key;
	vars_cache_entry *e;
	uintptr_t h;

	if (lwc_intern_string(text, len, &key) != lwc_error_ok) {
		return NULL;
	}
	h = ((uintptr_t)key >> 4) ^ ((uintptr_t)p->url >> 4) ^
	    (prop * 2654435761u);
	e = &vars_cache[h % VARS_CACHE_SIZE];
	if (e->text == key && e->prop == prop && e->url == p->url) {
		lwc_string_unref(key);
		return vars_sheet_style(e->sheet);
	}
	if (e->text != NULL) {
		lwc_string_unref(e->text);
		if (e->url != NULL) {
			lwc_string_unref(e->url);
		}
		if (e->sheet != NULL) {
			css_stylesheet_destroy(e->sheet);
		}
	}
	e->prop = prop;
	e->text = key;
	e->url = p->url != NULL ? lwc_string_ref(p->url) : NULL;
	e->sheet = vars_parse(prop, text, len, p);
	return vars_sheet_style(e->sheet);
}

void css__vars_flush(void)
{
	for (size_t i = 0; i < VARS_CACHE_SIZE; i++) {
		vars_cache_entry *e = &vars_cache[i];
		if (e->text != NULL) {
			lwc_string_unref(e->text);
		}
		if (e->url != NULL) {
			lwc_string_unref(e->url);
		}
		if (e->sheet != NULL) {
			css_stylesheet_destroy(e->sheet);
		}
		memset(e, 0, sizeof(*e));
	}
	if (propstrings != NULL) {
		css__propstrings_unref();
		propstrings = NULL;
	}
}

static css_error vars_probe_resolve(void *pw,
				    const char *base,
				    lwc_string *rel,
				    lwc_string **abs)
{
	*abs = lwc_string_ref(rel);
	return CSS_OK;
}

static void vars_prop_ops(uint16_t prop)
{
	css_var_params params;
	css_stylesheet *sheet;
	const css_style *style;

	if (prop_ops_done[prop]) {
		return;
	}
	prop_ops_done[prop] = true;
	memset(&params, 0, sizeof(params));
	params.level = CSS_LEVEL_DEFAULT;
	params.resolve = vars_probe_resolve;
	sheet = vars_parse(prop, "inherit", 7, &params);
	style = vars_sheet_style(sheet);
	if (style != NULL && style->used > 0) {
		prop_ops[prop] = malloc(style->used * sizeof(uint16_t));
		if (prop_ops[prop] != NULL) {
			for (uint32_t i = 0; i < style->used; i++) {
				opcode_t op = getOpcode(style->bytecode[i]);
				if (op < CSS_N_PROPERTIES) {
					prop_ops[prop][prop_n_ops[prop]++] = op;
				}
			}
		}
	}
	if (sheet != NULL) {
		css_stylesheet_destroy(sheet);
	}
}

css_error css__vars_cascade_custom(css_select_state *state,
				   lwc_string *name,
				   lwc_string *value,
				   bool important)
{
	css_select_vars *v = &state->vars;
	uint8_t rank = vars_rank(state->current_origin, important);
	css_select_var_decl *d;

	for (uint32_t i = 0; i < v->n_decls; i++) {
		d = &v->decls[i];
		if (d->pseudo != state->current_pseudo || d->name != name) {
			continue;
		}
		if (rank > d->rank ||
		    (rank == d->rank &&
		     state->current_specificity >= d->specificity)) {
			lwc_string_unref(d->value);
			d->value = lwc_string_ref(value);
			d->rank = rank;
			d->specificity = state->current_specificity;
		}
		return CSS_OK;
	}
	if (v->n_decls == v->alloc_decls) {
		uint32_t n = v->alloc_decls == 0 ? 8 : v->alloc_decls * 2;
		css_select_var_decl *grown = realloc(v->decls,
						     n * sizeof(*grown));
		if (grown == NULL) {
			return CSS_NOMEM;
		}
		v->decls = grown;
		v->alloc_decls = n;
	}
	d = &v->decls[v->n_decls++];
	d->name = lwc_string_ref(name);
	d->value = lwc_string_ref(value);
	d->rank = rank;
	d->specificity = state->current_specificity;
	d->pseudo = state->current_pseudo;
	return CSS_OK;
}

css_error css__vars_cascade_pending(css_select_state *state,
				    uint16_t prop,
				    lwc_string *value,
				    bool important,
				    const css_stylesheet *sheet)
{
	css_select_vars *v = &state->vars;
	css_select_var_pend *e;
	uint32_t index;
	bool claimed = false;

	if (prop >= N_PROP_HANDLERS) {
		return CSS_OK;
	}
	vars_prop_ops(prop);
	if (v->n_pends == v->alloc_pends) {
		uint32_t n = v->alloc_pends == 0 ? 8 : v->alloc_pends * 2;
		css_select_var_pend *grown = realloc(v->pends,
						     n * sizeof(*grown));
		if (grown == NULL) {
			return CSS_NOMEM;
		}
		v->pends = grown;
		v->alloc_pends = n;
	}
	index = v->n_pends;
	for (uint16_t i = 0; i < prop_n_ops[prop]; i++) {
		uint16_t op = prop_ops[prop][i];
		if (css__outranks_existing(
			    op, important, state, FLAG_VALUE__NONE)) {
			state->var_owner[op][state->current_pseudo] = index + 1;
			claimed = true;
		}
	}
	if (!claimed) {
		return CSS_OK;
	}
	e = &v->pends[v->n_pends++];
	e->prop = prop;
	e->important = important;
	e->pseudo = state->current_pseudo;
	e->value = lwc_string_ref(value);
	e->sheet = sheet;
	return CSS_OK;
}

static css_error
vars_build(css_select_state *state, uint8_t pseudo, css_computed_vars **out)
{
	css_select_vars *v = &state->vars;
	css_computed_vars *cv;
	uint32_t n_custom = 0, n_pending = 0;

	*out = NULL;
	for (uint32_t i = 0; i < v->n_decls; i++) {
		n_custom += v->decls[i].pseudo == pseudo;
	}
	for (uint32_t i = 0; i < v->n_pends; i++) {
		n_pending += v->pends[i].pseudo == pseudo;
	}
	if (n_custom == 0 && n_pending == 0) {
		return CSS_OK;
	}
	cv = calloc(1, sizeof(*cv));
	if (cv == NULL) {
		return CSS_NOMEM;
	}
	cv->refcnt = 1;
	cv->custom = calloc(n_custom + 1, sizeof(*cv->custom));
	cv->pending = calloc(n_pending + 1, sizeof(*cv->pending));
	if (cv->custom == NULL || cv->pending == NULL) {
		css__vars_unref(cv);
		return CSS_NOMEM;
	}
	for (uint32_t i = 0; i < v->n_decls; i++) {
		if (v->decls[i].pseudo == pseudo) {
			cv->custom[cv->n_custom].name = lwc_string_ref(
				v->decls[i].name);
			cv->custom[cv->n_custom].value = lwc_string_ref(
				v->decls[i].value);
			cv->n_custom++;
		}
	}
	for (uint32_t i = 0; i < v->n_pends; i++) {
		css_select_var_pend *e = &v->pends[i];
		css_var_pending *p;
		uint16_t prop = e->prop;

		if (e->pseudo != pseudo) {
			continue;
		}
		p = &cv->pending[cv->n_pending];
		p->ops = malloc((prop_n_ops[prop] + 1) * sizeof(uint16_t));
		if (p->ops == NULL) {
			css__vars_unref(cv);
			return CSS_NOMEM;
		}
		for (uint16_t k = 0; k < prop_n_ops[prop]; k++) {
			uint16_t op = prop_ops[prop][k];
			if (state->var_owner[op][pseudo] == i + 1) {
				p->ops[p->n_ops++] = op;
			}
		}
		if (p->n_ops == 0) {
			free(p->ops);
			p->ops = NULL;
			continue;
		}
		p->prop = prop;
		p->important = e->important;
		p->value = lwc_string_ref(e->value);
		if (e->sheet->url != NULL &&
		    lwc_intern_string(e->sheet->url,
				      strlen(e->sheet->url),
				      &p->params.url) != lwc_error_ok) {
			p->params.url = NULL;
		}
		p->params.level = e->sheet->level;
		p->params.quirks = e->sheet->quirks_allowed;
		p->params.resolve = e->sheet->resolve;
		p->params.resolve_pw = e->sheet->resolve_pw;
		p->params.color = e->sheet->color;
		p->params.color_pw = e->sheet->color_pw;
		p->params.font = e->sheet->font;
		p->params.font_pw = e->sheet->font_pw;
		cv->n_pending++;
	}
	if (cv->n_custom == 0 && cv->n_pending == 0) {
		css__vars_unref(cv);
		return CSS_OK;
	}
	*out = cv;
	return CSS_OK;
}

css_error css__vars_finish_select(css_select_state *state)
{
	for (int j = 0; j < CSS_PSEUDO_ELEMENT_COUNT; j++) {
		css_computed_style *style = state->results->styles[j];
		css_computed_vars *cv;
		css_error error;

		if (style == NULL) {
			continue;
		}
		error = vars_build(state, j, &cv);
		if (error != CSS_OK) {
			return error;
		}
		css__vars_unref(style->vars);
		style->vars = cv;
	}
	return CSS_OK;
}

void css__vars_select_cleanup(css_select_state *state)
{
	css_select_vars *v = &state->vars;

	for (uint32_t i = 0; i < v->n_decls; i++) {
		lwc_string_unref(v->decls[i].name);
		lwc_string_unref(v->decls[i].value);
	}
	for (uint32_t i = 0; i < v->n_pends; i++) {
		lwc_string_unref(v->pends[i].value);
	}
	free(v->decls);
	free(v->pends);
	memset(v, 0, sizeof(*v));
}

static void vars_initial(css_select_state *st, uint16_t op)
{
	if (st->handler == NULL && prop_dispatch[op].inherited) {
		return;
	}
	prop_dispatch[op].initial(st);
}

static void vars_apply(const css_computed_style *parent,
		       css_computed_style *target,
		       const css_var_pending *p,
		       const css_style *style,
		       css_select_handler *handler,
		       void *pw)
{
	css_select_state *st = calloc(1, sizeof(*st));
	css_computed_style *scratch = NULL;
	css_style s;

	if (st == NULL) {
		return;
	}
	st->handler = handler;
	st->pw = pw;
	if (css__computed_style_create(&scratch, target->calc) != CSS_OK) {
		free(st);
		return;
	}
	st->current_origin = CSS_ORIGIN_AUTHOR;
	s = *style;
	while (s.used > 0) {
		css_code_t opv = *s.bytecode;
		opcode_t op = getOpcode(opv);
		bool keep = false;

		if (op >= CSS_N_PROPERTIES) {
			break;
		}
		for (uint16_t k = 0; k < p->n_ops && !keep; k++) {
			keep = p->ops[k] == op;
		}
		advance_bytecode(&s, sizeof(opv));
		st->computed = keep ? target : scratch;
		if (prop_dispatch[op].cascade(opv, &s, st) != CSS_OK) {
			break;
		}
		if (!keep) {
			continue;
		}
		enum flag_value d = st->props[op][0].explicit_default;
		if (d == FLAG_VALUE_INHERIT ||
		    ((d == FLAG_VALUE_UNSET || d == FLAG_VALUE_REVERT) &&
		     prop_dispatch[op].inherited)) {
			if (parent != NULL) {
				prop_dispatch[op].copy(parent, target);
			} else {
				vars_initial(st, op);
			}
		} else if (d != FLAG_VALUE__NONE) {
			vars_initial(st, op);
		}
	}
	css_computed_style_destroy(scratch);
	free(st);
}

static void vars_unset(const css_computed_style *parent,
		       css_computed_style *target,
		       const css_var_pending *p,
		       css_select_handler *handler,
		       void *pw)
{
	css_select_state *st = calloc(1, sizeof(*st));

	if (st == NULL) {
		return;
	}
	st->handler = handler;
	st->pw = pw;
	st->computed = target;
	for (uint16_t k = 0; k < p->n_ops; k++) {
		uint16_t op = p->ops[k];
		if (prop_dispatch[op].inherited && parent != NULL) {
			prop_dispatch[op].copy(parent, target);
		} else {
			vars_initial(st, op);
		}
	}
	free(st);
}

css_error css__vars_resolve(const css_computed_style *parent,
			    const css_computed_vars *own,
			    css_computed_style *target,
			    css_select_handler *handler,
			    void *pw)
{
	const css_computed_vars *inherited = parent != NULL ? parent->vars
							    : NULL;
	css_computed_vars *table = NULL;
	css_computed_vars *old = target->vars;
	vars_scope scope;
	uint32_t n_own = own != NULL ? own->n_custom : 0;

	memset(&scope, 0, sizeof(scope));
	scope.own = own;
	scope.parent = inherited;
	if (n_own > 0) {
		scope.resolved = calloc(n_own, sizeof(lwc_string *));
		scope.state = calloc(n_own, 1);
		if (scope.resolved == NULL || scope.state == NULL) {
			free(scope.resolved);
			free(scope.state);
			return CSS_NOMEM;
		}
	}

	if (own != NULL) {
		for (uint32_t i = 0; i < own->n_pending; i++) {
			const css_var_pending *p = &own->pending[i];
			vars_buf b = {NULL, 0, 0};
			const css_style *style = NULL;

			if (vars_substitute(&scope,
					    lwc_string_data(p->value),
					    lwc_string_length(p->value),
					    0,
					    &b)) {
				style = vars_parse_cached(p->prop,
							  b.data != NULL
								  ? b.data
								  : "",
							  b.len,
							  &p->params);
			}
			free(b.data);
			if (style != NULL) {
				vars_apply(
					parent, target, p, style, handler, pw);
			} else {
				vars_unset(parent, target, p, handler, pw);
			}
		}
	}

	if (n_own == 0) {
		table = (css_computed_vars *)inherited;
		css__vars_ref(table);
	} else {
		uint32_t n_parent = inherited != NULL ? inherited->n_custom : 0;
		table = calloc(1, sizeof(*table));
		if (table != NULL) {
			table->refcnt = 1;
			table->custom = calloc(n_parent + n_own,
					       sizeof(*table->custom));
			if (table->custom == NULL) {
				free(table);
				table = NULL;
			}
		}
		if (table != NULL) {
			for (uint32_t k = 0; k < n_own; k++) {
				const char *v;
				size_t vl;
				lwc_string *name = own->custom[k].name;
				if (!vars_scope_lookup(&scope,
						       lwc_string_data(name),
						       lwc_string_length(name),
						       0,
						       &v,
						       &vl)) {
					continue;
				}
				table->custom[table->n_custom].name =
					lwc_string_ref(name);
				table->custom[table->n_custom].value =
					lwc_string_ref(scope.resolved[k]);
				table->n_custom++;
			}
			for (uint32_t k = 0; k < n_parent; k++) {
				lwc_string *name = inherited->custom[k].name;
				bool shadowed = false;
				for (uint32_t m = 0; m < n_own && !shadowed;
				     m++) {
					shadowed = own->custom[m].name == name;
				}
				if (!shadowed) {
					table->custom[table->n_custom].name =
						lwc_string_ref(name);
					table->custom[table->n_custom]
						.value = lwc_string_ref(
						inherited->custom[k].value);
					table->n_custom++;
				}
			}
		}
	}

	for (uint32_t k = 0; k < n_own; k++) {
		if (scope.resolved[k] != NULL) {
			lwc_string_unref(scope.resolved[k]);
		}
	}
	free(scope.resolved);
	free(scope.state);

	target->vars = table;
	css__vars_unref(old);
	return CSS_OK;
}
