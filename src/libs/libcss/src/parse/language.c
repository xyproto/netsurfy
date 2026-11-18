/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <parserutils/utils/stack.h>

#include "stylesheet.h"
#include "lex/lex.h"
#include "parse/font_face.h"
#include "parse/important.h"
#include "parse/language.h"
#include "parse/mq.h"
#include "parse/parse.h"
#include "parse/propstrings.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

#include "utils/parserutilserror.h"
#include "utils/utils.h"

typedef struct context_entry {
	css_parser_event type; /**< Type of entry */
	void *data; /**< Data for context */
	bool nested;
	bool pending;
} context_entry;

/* Event handlers */
static css_error language_handle_event(css_parser_event type,
				       const parserutils_vector *tokens,
				       void *pw);
static css_error
handleStartStylesheet(css_language *c, const parserutils_vector *vector);
static css_error
handleEndStylesheet(css_language *c, const parserutils_vector *vector);
static css_error
handleStartRuleset(css_language *c, const parserutils_vector *vector);
static css_error
handleEndRuleset(css_language *c, const parserutils_vector *vector);
static css_error
handleStartAtRule(css_language *c, const parserutils_vector *vector);
static css_error
handleEndAtRule(css_language *c, const parserutils_vector *vector);
static css_error
handleStartBlock(css_language *c, const parserutils_vector *vector);
static css_error
handleEndBlock(css_language *c, const parserutils_vector *vector);
static css_error
handleBlockContent(css_language *c, const parserutils_vector *vector);
static css_error
handleEndBlockContent(css_language *c, const parserutils_vector *vector);
static css_error
handleDeclaration(css_language *c, const parserutils_vector *vector);

/* At-rule parsing */
static css_error
addNamespace(css_language *c, lwc_string *prefix, lwc_string *uri);
static css_error
lookupNamespace(css_language *c, lwc_string *prefix, lwc_string **uri);

/* Selector list parsing */
static css_error parseClass(css_language *c,
			    const parserutils_vector *vector,
			    int32_t *ctx,
			    css_selector_detail *specific);
static css_error parseAttrib(css_language *c,
			     const parserutils_vector *vector,
			     int32_t *ctx,
			     css_selector_detail *specific);
static css_error parseNth(css_language *c,
			  const parserutils_vector *vector,
			  int32_t *ctx,
			  css_selector_detail_value *value);
static css_error parsePseudo(css_language *c,
			     const parserutils_vector *vector,
			     int32_t *ctx,
			     bool in_not,
			     css_selector_detail *specific);
static css_error parseSpecific(css_language *c,
			       const parserutils_vector *vector,
			       int32_t *ctx,
			       bool in_not,
			       css_selector_detail *specific);
static css_error parseAppendSpecific(css_language *c,
				     const parserutils_vector *vector,
				     int32_t *ctx,
				     css_selector **parent);
static css_error parseSelectorSpecifics(css_language *c,
					const parserutils_vector *vector,
					int32_t *ctx,
					css_selector **parent);
static css_error parseTypeSelector(css_language *c,
				   const parserutils_vector *vector,
				   int32_t *ctx,
				   css_qname *qname);
static css_error parseSimpleSelector(css_language *c,
				     const parserutils_vector *vector,
				     int32_t *ctx,
				     css_selector **result);
static css_error parseCombinator(css_language *c,
				 const parserutils_vector *vector,
				 int32_t *ctx,
				 css_combinator *result);
static css_error parseSelector(css_language *c,
			       const parserutils_vector *vector,
			       int32_t *ctx,
			       css_selector **result);
static css_error parseSelectorList(css_language *c,
				   const parserutils_vector *vector,
				   css_rule *rule);

static void
reportInvalid(const char *what, const parserutils_vector *vector, int32_t ctx);

/* Declaration parsing */
static bool vectorHasVar(const parserutils_vector *vector, int32_t ctx);
static css_error parseProperty(css_language *c,
			       const css_token *property,
			       const parserutils_vector *vector,
			       int32_t *ctx,
			       css_rule *rule);

/**
 * Create a CSS language parser
 *
 * \param sheet	    The stylesheet object to parse for
 * \param parser    The core parser object to use
 * \param language  Pointer to location to receive parser object
 * \return CSS_OK on success,
 *	   CSS_BADPARM on bad parameters,
 *	   CSS_NOMEM on memory exhaustion
 */
css_error
css__language_create(css_stylesheet *sheet, css_parser *parser, void **language)
{
	css_language *c;
	css_parser_optparams params;
	parserutils_error perror;
	css_error error;

	if (sheet == NULL || parser == NULL || language == NULL)
		return CSS_BADPARM;

	c = malloc(sizeof(css_language));
	if (c == NULL)
		return CSS_NOMEM;

	perror = parserutils_stack_create(sizeof(context_entry),
					  STACK_CHUNK,
					  &c->context);
	if (perror != PARSERUTILS_OK) {
		free(c);
		return css_error_from_parserutils_error(perror);
	}

	params.event_handler.handler = language_handle_event;
	params.event_handler.pw = c;
	error = css__parser_setopt(parser, CSS_PARSER_EVENT_HANDLER, &params);
	if (error != CSS_OK) {
		parserutils_stack_destroy(c->context);
		free(c);
		return error;
	}

	c->sheet = sheet;
	c->state = CHARSET_PERMITTED;
	c->default_namespace = NULL;
	c->namespaces = NULL;
	c->num_namespaces = 0;
	c->strings = sheet->propstrings;

	*language = c;

	return CSS_OK;
}

/**
 * Destroy a CSS language parser
 *
 * \param language  The parser to destroy
 * \return CSS_OK on success, appropriate error otherwise
 */
css_error css__language_destroy(css_language *language)
{
	uint32_t i;

	if (language == NULL)
		return CSS_BADPARM;

	lwc_string_unref(language->default_namespace);

	if (language->namespaces != NULL) {
		for (i = 0; i < language->num_namespaces; i++) {
			lwc_string_unref(language->namespaces[i].prefix);
			lwc_string_unref(language->namespaces[i].uri);
		}

		free(language->namespaces);
	}

	parserutils_stack_destroy(language->context);

	free(language);

	return CSS_OK;
}

/**
 * Handler for core parser events
 *
 * \param type	  The event type
 * \param tokens  Vector of tokens read since last event, or NULL
 * \param pw	  Pointer to handler context
 * \return CSS_OK on success, CSS_INVALID to indicate parse error,
 *	   appropriate error otherwise.
 */
css_error language_handle_event(css_parser_event type,
				const parserutils_vector *tokens,
				void *pw)
{
	css_language *language = (css_language *)pw;

	switch (type) {
	case CSS_PARSER_START_STYLESHEET:
		return handleStartStylesheet(language, tokens);
	case CSS_PARSER_END_STYLESHEET:
		return handleEndStylesheet(language, tokens);
	case CSS_PARSER_START_RULESET:
		return handleStartRuleset(language, tokens);
	case CSS_PARSER_END_RULESET:
		return handleEndRuleset(language, tokens);
	case CSS_PARSER_START_ATRULE:
		return handleStartAtRule(language, tokens);
	case CSS_PARSER_END_ATRULE:
		return handleEndAtRule(language, tokens);
	case CSS_PARSER_START_BLOCK:
		return handleStartBlock(language, tokens);
	case CSS_PARSER_END_BLOCK:
		return handleEndBlock(language, tokens);
	case CSS_PARSER_BLOCK_CONTENT:
		return handleBlockContent(language, tokens);
	case CSS_PARSER_END_BLOCK_CONTENT:
		return handleEndBlockContent(language, tokens);
	case CSS_PARSER_DECLARATION:
		return handleDeclaration(language, tokens);
	}

	return CSS_OK;
}

/******************************************************************************
 * Parser stages							      *
 ******************************************************************************/

static css_error dropPending(css_language *c)
{
	context_entry *cur = parserutils_stack_get_current(c->context);

	if (cur != NULL && cur->pending) {
		parserutils_error perror = parserutils_stack_pop(c->context,
								 NULL);
		if (perror != PARSERUTILS_OK) {
			return css_error_from_parserutils_error(perror);
		}
	}

	return CSS_OK;
}

static css_error collectParens(const parserutils_vector *vector,
			       int32_t *ctx,
			       parserutils_vector **result)
{
	const css_token *token;
	parserutils_vector *sub;
	parserutils_error perror;
	int depth = 0;

	perror = parserutils_vector_create(sizeof(css_token), 16, &sub);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	while ((token = parserutils_vector_iterate(vector, ctx)) != NULL) {
		if (token->type == CSS_TOKEN_FUNCTION ||
		    tokenIsChar(token, '(')) {
			depth++;
		} else if (tokenIsChar(token, ')')) {
			if (depth == 0) {
				*result = sub;
				return CSS_OK;
			}
			depth--;
		}
		perror = parserutils_vector_append(sub, (void *)token);
		if (perror != PARSERUTILS_OK) {
			parserutils_vector_destroy(sub);
			return css_error_from_parserutils_error(perror);
		}
	}

	parserutils_vector_destroy(sub);
	return CSS_INVALID;
}

static bool supportsCondition(css_language *c,
			      const parserutils_vector *vector,
			      int32_t *ctx,
			      bool *valid);

static bool
supportsDeclaration(css_language *c, const parserutils_vector *vector)
{
	const css_token *ident, *token;
	css_style *style = NULL;
	css_prop_handler handler;
	uint8_t flags = 0;
	int32_t ctx = 0;
	css_error error;
	int i;

	consumeWhitespace(vector, &ctx);
	ident = parserutils_vector_iterate(vector, &ctx);
	if (ident == NULL || ident->type != CSS_TOKEN_IDENT) {
		return false;
	}
	consumeWhitespace(vector, &ctx);
	token = parserutils_vector_iterate(vector, &ctx);
	if (token == NULL || !tokenIsChar(token, ':')) {
		return false;
	}
	consumeWhitespace(vector, &ctx);

	if (lwc_string_length(ident->idata) > 2 &&
	    lwc_string_data(ident->idata)[0] == '-' &&
	    lwc_string_data(ident->idata)[1] == '-') {
		return true;
	}

	for (i = FIRST_PROP; i <= LAST_PROP; i++) {
		bool match = false;

		if (lwc_string_caseless_isequal(ident->idata,
						c->strings[i],
						&match) == lwc_error_ok &&
		    match)
			break;
	}
	if (i == LAST_PROP + 1) {
		return false;
	}
	if (vectorHasVar(vector, ctx)) {
		return true;
	}

	handler = property_handlers[i - FIRST_PROP];
	if (css__stylesheet_style_create(c->sheet, &style) != CSS_OK) {
		return false;
	}
	error = handler(c, vector, &ctx, style);
	if (error == CSS_OK) {
		error = css__parse_important(c, vector, &ctx, &flags);
	}
	css__stylesheet_style_destroy(style);
	if (error != CSS_OK) {
		return false;
	}
	consumeWhitespace(vector, &ctx);
	return parserutils_vector_iterate(vector, &ctx) == NULL;
}

static bool supportsSelector(css_language *c, const parserutils_vector *vector)
{
	css_selector *selector = NULL;
	int32_t ctx = 0;
	bool ok;

	consumeWhitespace(vector, &ctx);
	if (parseSelector(c, vector, &ctx, &selector) != CSS_OK) {
		return false;
	}
	consumeWhitespace(vector, &ctx);
	ok = parserutils_vector_iterate(vector, &ctx) == NULL;
	css__stylesheet_selector_destroy(c->sheet, selector);
	return ok;
}

static bool supportsInParens(css_language *c,
			     const parserutils_vector *vector,
			     int32_t *ctx,
			     bool *valid)
{
	const css_token *token = parserutils_vector_iterate(vector, ctx);
	parserutils_vector *sub = NULL;
	bool result = false;
	bool match = false;

	if (token == NULL ||
	    (token->type != CSS_TOKEN_FUNCTION && !tokenIsChar(token, '('))) {
		*valid = false;
		return false;
	}
	if (collectParens(vector, ctx, &sub) != CSS_OK) {
		*valid = false;
		return false;
	}

	if (token->type == CSS_TOKEN_FUNCTION) {
		if (lwc_string_caseless_isequal(token->idata,
						c->strings[SUPPORTS_SELECTOR],
						&match) == lwc_error_ok &&
		    match) {
			result = supportsSelector(c, sub);
		}
	} else {
		int32_t sub_ctx = 0;
		const css_token *first;

		consumeWhitespace(sub, &sub_ctx);
		first = parserutils_vector_peek(sub, sub_ctx);
		if (first != NULL && first->type == CSS_TOKEN_IDENT) {
			int32_t look = sub_ctx + 1;
			const css_token *next;

			consumeWhitespace(sub, &look);
			next = parserutils_vector_peek(sub, look);
			if (next != NULL && tokenIsChar(next, ':')) {
				result = supportsDeclaration(c, sub);
				parserutils_vector_destroy(sub);
				return result;
			}
		}
		{
			bool sub_valid = true;

			result = supportsCondition(
				c, sub, &sub_ctx, &sub_valid);
			if (!sub_valid ||
			    parserutils_vector_peek(sub, sub_ctx) != NULL) {
				result = false;
			}
		}
	}

	parserutils_vector_destroy(sub);
	return result;
}

static bool supportsCondition(css_language *c,
			      const parserutils_vector *vector,
			      int32_t *ctx,
			      bool *valid)
{
	const css_token *token;
	bool result;
	int op = 0;
	bool match = false;

	consumeWhitespace(vector, ctx);
	token = parserutils_vector_peek(vector, *ctx);
	if (token != NULL && token->type == CSS_TOKEN_IDENT &&
	    lwc_string_caseless_isequal(
		    token->idata, c->strings[NOT], &match) == lwc_error_ok &&
	    match) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		result = !supportsInParens(c, vector, ctx, valid);
		consumeWhitespace(vector, ctx);
		return result;
	}

	result = supportsInParens(c, vector, ctx, valid);
	while (*valid) {
		int this_op = 0;
		bool next;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || token->type != CSS_TOKEN_IDENT) {
			break;
		}
		if (lwc_string_caseless_isequal(token->idata,
						c->strings[AND],
						&match) == lwc_error_ok &&
		    match) {
			this_op = 1;
		} else if (lwc_string_caseless_isequal(
				   token->idata, c->strings[OR], &match) ==
				   lwc_error_ok &&
			   match) {
			this_op = 2;
		}
		if (this_op == 0 || (op != 0 && op != this_op)) {
			*valid = false;
			break;
		}
		op = this_op;
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		next = supportsInParens(c, vector, ctx, valid);
		result = op == 1 ? (result && next) : (result || next);
	}

	return result;
}

static css_error
createConditionalRule(css_language *c, bool applies, css_rule **result)
{
	css_mq_query *media;
	css_rule *rule;
	css_error error;

	media = calloc(1, sizeof(*media));
	if (media == NULL) {
		return CSS_NOMEM;
	}
	media->type = CSS_MEDIA_ALL;
	media->negate_type = applies ? 0 : 1;

	error = css__stylesheet_rule_create(c->sheet, CSS_RULE_MEDIA, &rule);
	if (error != CSS_OK) {
		css__mq_query_destroy(media);
		return error;
	}
	error = css__stylesheet_rule_set_media(c->sheet, rule, media);
	if (error != CSS_OK) {
		css__stylesheet_rule_destroy(c->sheet, rule);
		css__mq_query_destroy(media);
		return error;
	}
	*result = rule;
	return CSS_OK;
}

css_error
handleStartStylesheet(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry entry = {CSS_PARSER_START_STYLESHEET, NULL};

	UNUSED(vector);

	assert(c != NULL);

	perror = parserutils_stack_push(c->context, (void *)&entry);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	return CSS_OK;
}

css_error handleEndStylesheet(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry *entry;

	UNUSED(vector);

	assert(c != NULL);

	entry = parserutils_stack_get_current(c->context);
	if (entry == NULL || entry->type != CSS_PARSER_START_STYLESHEET)
		return CSS_INVALID;

	perror = parserutils_stack_pop(c->context, NULL);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	return CSS_OK;
}

css_error handleStartRuleset(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	css_error error;
	context_entry entry = {CSS_PARSER_START_RULESET, NULL};
	context_entry *cur;
	css_rule *parent_rule = NULL;
	css_rule *rule = NULL;

	assert(c != NULL);

	/* Retrieve parent rule from stack, if any */
	cur = parserutils_stack_get_current(c->context);
	if (cur != NULL && cur->type != CSS_PARSER_START_STYLESHEET)
		parent_rule = cur->data;
	if (cur != NULL && cur->type == CSS_PARSER_START_BLOCK) {
		entry.nested = true;
		entry.pending = true;
	}

	error = css__stylesheet_rule_create(c->sheet, CSS_RULE_SELECTOR, &rule);
	if (error != CSS_OK)
		return error;

	if (vector != NULL) {
		/* Parse selectors, if there are any */
		error = parseSelectorList(c, vector, rule);
		if (error != CSS_OK) {
			if (error == CSS_INVALID) {
				reportInvalid("SELECTOR", vector, 0);
			}
			css__stylesheet_rule_destroy(c->sheet, rule);
			return error;
		}
	}

	entry.data = rule;

	perror = parserutils_stack_push(c->context, (void *)&entry);
	if (perror != PARSERUTILS_OK) {
		css__stylesheet_rule_destroy(c->sheet, rule);
		return css_error_from_parserutils_error(perror);
	}

	error = css__stylesheet_add_rule(c->sheet, rule, parent_rule);
	if (error != CSS_OK) {
		parserutils_stack_pop(c->context, NULL);
		css__stylesheet_rule_destroy(c->sheet, rule);
		return error;
	}

	/* Flag that we've had a valid rule, so @import/@namespace/@charset
	 * have no effect. */
	c->state = HAD_RULE;

	/* Rule is now owned by the sheet, so no need to destroy it */

	return CSS_OK;
}

css_error handleEndRuleset(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry *entry;

	UNUSED(vector);

	assert(c != NULL);

	entry = parserutils_stack_get_current(c->context);
	if (entry == NULL || entry->type != CSS_PARSER_START_RULESET)
		return CSS_INVALID;

	perror = parserutils_stack_pop(c->context, NULL);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	return CSS_OK;
}

css_error handleStartAtRule(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry entry = {CSS_PARSER_START_ATRULE, NULL};
	const css_token *token = NULL;
	const css_token *atkeyword = NULL;
	int32_t ctx = 0;
	bool match = false;
	css_rule *rule;
	css_rule *parent = NULL;
	context_entry *cur;
	css_error error;

	/* vector contains: ATKEYWORD ws any0 */

	assert(c != NULL);

	cur = parserutils_stack_get_current(c->context);
	if (cur != NULL && cur->type == CSS_PARSER_START_BLOCK) {
		parent = cur->data;
		if (parent == NULL || parent->type != CSS_RULE_MEDIA) {
			return CSS_INVALID;
		}
		entry.nested = true;
		entry.pending = true;
	}

	consumeWhitespace(vector, &ctx);
	atkeyword = parserutils_vector_iterate(vector, &ctx);
	if (atkeyword == NULL || atkeyword->type != CSS_TOKEN_ATKEYWORD) {
		return CSS_INVALID;
	}

	consumeWhitespace(vector, &ctx);

	/* We now have an ATKEYWORD and the context for the start of any0, if
	 * there is one */
	assert(atkeyword != NULL && atkeyword->type == CSS_TOKEN_ATKEYWORD);

	if (lwc_string_caseless_isequal(atkeyword->idata,
					c->strings[CHARSET],
					&match) == lwc_error_ok &&
	    match) {
		if (c->state == CHARSET_PERMITTED) {
			const css_token *charset;

			/* any0 = STRING */
			if (ctx == 0)
				return CSS_INVALID;

			charset = parserutils_vector_iterate(vector, &ctx);
			if (charset == NULL ||
			    charset->type != CSS_TOKEN_STRING)
				return CSS_INVALID;

			token = parserutils_vector_iterate(vector, &ctx);
			if (token != NULL)
				return CSS_INVALID;

			error = css__stylesheet_rule_create(c->sheet,
							    CSS_RULE_CHARSET,
							    &rule);
			if (error != CSS_OK)
				return error;

			error = css__stylesheet_rule_set_charset(
				c->sheet, rule, charset->idata);
			if (error != CSS_OK) {
				css__stylesheet_rule_destroy(c->sheet, rule);
				return error;
			}

			error = css__stylesheet_add_rule(c->sheet, rule, NULL);
			if (error != CSS_OK) {
				css__stylesheet_rule_destroy(c->sheet, rule);
				return error;
			}

			/* Rule is now owned by the sheet,
			 * so no need to destroy it */

			c->state = IMPORT_PERMITTED;
		} else {
			return CSS_INVALID;
		}
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[LIBCSS_IMPORT],
					       &match) == lwc_error_ok &&
		   match) {
		if (c->state <= IMPORT_PERMITTED) {
			lwc_string *url;
			css_mq_query *media = NULL;

			/* any0 = (STRING | URI) ws (media query)? */
			const css_token *uri = parserutils_vector_iterate(
				vector, &ctx);
			if (uri == NULL || (uri->type != CSS_TOKEN_STRING &&
					    uri->type != CSS_TOKEN_URI))
				return CSS_INVALID;

			consumeWhitespace(vector, &ctx);

			/* Parse media list */
			error = css__mq_parse_media_list(
				c->strings, vector, &ctx, &media);
			if (error == CSS_NOMEM) {
				return error;
			} else if (media == NULL) {
				/* Fall back to default media: "all". */
				media = calloc(1, sizeof(*media));
				if (media == NULL) {
					return CSS_NOMEM;
				}
				media->type = CSS_MEDIA_ALL;
			}

			/* Create rule */
			error = css__stylesheet_rule_create(c->sheet,
							    CSS_RULE_IMPORT,
							    &rule);
			if (error != CSS_OK) {
				css__mq_query_destroy(media);
				return error;
			}

			/* Resolve import URI */
			error = c->sheet->resolve(c->sheet->resolve_pw,
						  c->sheet->url,
						  uri->idata,
						  &url);
			if (error != CSS_OK) {
				css__stylesheet_rule_destroy(c->sheet, rule);
				css__mq_query_destroy(media);
				return error;
			}

			/* Inform rule of it */
			error = css__stylesheet_rule_set_nascent_import(
				c->sheet, rule, url, media);
			if (error != CSS_OK) {
				lwc_string_unref(url);
				css__stylesheet_rule_destroy(c->sheet, rule);
				css__mq_query_destroy(media);
				return error;
			}

			/* Inform client of need for import */
			if (c->sheet->import != NULL) {
				error = c->sheet->import(c->sheet->import_pw,
							 c->sheet,
							 url);
				if (error != CSS_OK) {
					lwc_string_unref(url);
					css__stylesheet_rule_destroy(c->sheet,
								     rule);
					return error;
				}
			}

			/* No longer care about url */
			lwc_string_unref(url);

			/* Add rule to sheet */
			error = css__stylesheet_add_rule(c->sheet, rule, NULL);
			if (error != CSS_OK) {
				css__stylesheet_rule_destroy(c->sheet, rule);
				return error;
			}

			/* Rule is now owned by the sheet,
			 * so no need to destroy it */

			c->state = IMPORT_PERMITTED;
		} else {
			return CSS_INVALID;
		}
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[NAMESPACE],
					       &match) == lwc_error_ok &&
		   match) {
		if (c->state <= NAMESPACE_PERMITTED) {
			lwc_string *prefix = NULL;

			/* any0 = (IDENT ws)? (STRING | URI) ws */

			token = parserutils_vector_iterate(vector, &ctx);
			if (token == NULL)
				return CSS_INVALID;

			if (token->type == CSS_TOKEN_IDENT) {
				prefix = token->idata;

				consumeWhitespace(vector, &ctx);

				token = parserutils_vector_iterate(vector,
								   &ctx);
			}

			if (token == NULL || (token->type != CSS_TOKEN_STRING &&
					      token->type != CSS_TOKEN_URI)) {
				return CSS_INVALID;
			}

			consumeWhitespace(vector, &ctx);

			error = addNamespace(c, prefix, token->idata);
			if (error != CSS_OK)
				return error;

			c->state = NAMESPACE_PERMITTED;

			/* Namespaces are special, and do not generate rules */
			return CSS_OK;
		} else {
			return CSS_INVALID;
		}
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[MEDIA],
					       &match) == lwc_error_ok &&
		   match) {
		css_mq_query *media = NULL;

		/* any0 = media query */
		error = css__mq_parse_media_list(
			c->strings, vector, &ctx, &media);
		if (error != CSS_OK)
			return error;

		error = css__stylesheet_rule_create(c->sheet,
						    CSS_RULE_MEDIA,
						    &rule);
		if (error != CSS_OK) {
			css__mq_query_destroy(media);
			return error;
		}

		error = css__stylesheet_rule_set_media(c->sheet, rule, media);
		if (error != CSS_OK) {
			css__stylesheet_rule_destroy(c->sheet, rule);
			css__mq_query_destroy(media);
			return error;
		}

		error = css__stylesheet_add_rule(c->sheet, rule, parent);
		if (error != CSS_OK) {
			css__stylesheet_rule_destroy(c->sheet, rule);
			return error;
		}

		/* Rule is now owned by the sheet,
		 * so no need to destroy it */

		c->state = HAD_RULE;
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[FONT_FACE],
					       &match) == lwc_error_ok &&
		   match) {
		error = css__stylesheet_rule_create(c->sheet,
						    CSS_RULE_FONT_FACE,
						    &rule);
		if (error != CSS_OK)
			return error;

		consumeWhitespace(vector, &ctx);

		error = css__stylesheet_add_rule(c->sheet, rule, parent);
		if (error != CSS_OK) {
			css__stylesheet_rule_destroy(c->sheet, rule);
			return error;
		}

		/* Rule is now owned by the sheet,
		 * so no need to destroy it */

		c->state = HAD_RULE;
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[PAGE],
					       &match) == lwc_error_ok &&
		   match) {
		const css_token *token;

		/* any0 = (':' IDENT)? ws */

		error = css__stylesheet_rule_create(c->sheet,
						    CSS_RULE_PAGE,
						    &rule);
		if (error != CSS_OK)
			return error;

		consumeWhitespace(vector, &ctx);

		token = parserutils_vector_peek(vector, ctx);
		if (token != NULL) {
			css_selector *sel = NULL;

			error = parseSelector(c, vector, &ctx, &sel);
			if (error != CSS_OK) {
				css__stylesheet_rule_destroy(c->sheet, rule);
				return error;
			}

			error = css__stylesheet_rule_set_page_selector(c->sheet,
								       rule,
								       sel);
			if (error != CSS_OK) {
				css__stylesheet_selector_destroy(c->sheet, sel);
				css__stylesheet_rule_destroy(c->sheet, rule);
				return error;
			}
		}

		error = css__stylesheet_add_rule(c->sheet, rule, parent);
		if (error != CSS_OK) {
			css__stylesheet_rule_destroy(c->sheet, rule);
			return error;
		}

		/* Rule is now owned by the sheet,
		 * so no need to destroy it */

		c->state = HAD_RULE;
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[LAYER],
					       &match) == lwc_error_ok &&
		   match) {
		error = createConditionalRule(c, true, &rule);
		if (error != CSS_OK)
			return error;

		error = css__stylesheet_add_rule(c->sheet, rule, parent);
		if (error != CSS_OK) {
			css__stylesheet_rule_destroy(c->sheet, rule);
			return error;
		}

		c->state = HAD_RULE;
	} else if (lwc_string_caseless_isequal(atkeyword->idata,
					       c->strings[SUPPORTS],
					       &match) == lwc_error_ok &&
		   match) {
		bool valid = true;
		bool applies = supportsCondition(c, vector, &ctx, &valid);

		consumeWhitespace(vector, &ctx);
		if (parserutils_vector_peek(vector, ctx) != NULL) {
			valid = false;
		}

		error = createConditionalRule(c, valid && applies, &rule);
		if (error != CSS_OK)
			return error;

		error = css__stylesheet_add_rule(c->sheet, rule, parent);
		if (error != CSS_OK) {
			css__stylesheet_rule_destroy(c->sheet, rule);
			return error;
		}

		c->state = HAD_RULE;
	} else {
		return CSS_INVALID;
	}

	entry.data = rule;

	perror = parserutils_stack_push(c->context, (void *)&entry);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	return CSS_OK;
}

css_error handleEndAtRule(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry *entry;

	UNUSED(vector);

	assert(c != NULL);

	entry = parserutils_stack_get_current(c->context);
	if (entry == NULL || entry->type != CSS_PARSER_START_ATRULE)
		return CSS_INVALID;

	perror = parserutils_stack_pop(c->context, NULL);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	return CSS_OK;
}

css_error handleStartBlock(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry entry = {CSS_PARSER_START_BLOCK, NULL};
	context_entry *cur;

	UNUSED(vector);

	/* If the current item on the stack isn't a block,
	 * then clone its data field. This ensures that the relevant rule
	 * is available when parsing the block contents. */
	cur = parserutils_stack_get_current(c->context);
	if (cur != NULL && cur->type != CSS_PARSER_START_BLOCK)
		entry.data = cur->data;
	if (cur != NULL)
		cur->pending = false;

	perror = parserutils_stack_push(c->context, (void *)&entry);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	return CSS_OK;
}

css_error handleEndBlock(css_language *c, const parserutils_vector *vector)
{
	parserutils_error perror;
	context_entry *entry;
	css_rule *rule;

	entry = parserutils_stack_get_current(c->context);
	if (entry == NULL || entry->type != CSS_PARSER_START_BLOCK)
		return CSS_INVALID;

	rule = entry->data;

	perror = parserutils_stack_pop(c->context, NULL);
	if (perror != PARSERUTILS_OK) {
		return css_error_from_parserutils_error(perror);
	}

	/* If the block we just popped off the stack was associated with a
	 * stack entry that was opened inside another block, then report the
	 * end of that entry, too. */
	entry = parserutils_stack_get_current(c->context);
	if (rule != NULL && entry != NULL && entry->nested &&
	    entry->data == rule) {
		perror = parserutils_stack_pop(c->context, NULL);
		if (perror != PARSERUTILS_OK) {
			return css_error_from_parserutils_error(perror);
		}
	}

	return CSS_OK;
}

css_error handleBlockContent(css_language *c, const parserutils_vector *vector)
{
	context_entry *entry;
	css_rule *rule;
	css_error error;

	/* Block content comprises either declarations (if the current block is
	 * associated with @page, @font-face or a selector), or rulesets (if the
	 * current block is associated with @media). */

	error = dropPending(c);
	if (error != CSS_OK)
		return error;

	entry = parserutils_stack_get_current(c->context);
	if (entry == NULL || entry->data == NULL)
		return CSS_INVALID;

	rule = entry->data;
	if (rule == NULL ||
	    (rule->type != CSS_RULE_SELECTOR && rule->type != CSS_RULE_PAGE &&
	     rule->type != CSS_RULE_MEDIA && rule->type != CSS_RULE_FONT_FACE))
		return CSS_INVALID;

	if (rule->type == CSS_RULE_MEDIA) {
		int32_t ctx = 0;
		const css_token *token;

		consumeWhitespace(vector, &ctx);
		token = parserutils_vector_peek(vector, ctx);
		if (token != NULL && token->type == CSS_TOKEN_ATKEYWORD) {
			return handleStartAtRule(c, vector);
		}
		/* Expect rulesets */
		return handleStartRuleset(c, vector);
	} else {
		/* Expect declarations */
		return handleDeclaration(c, vector);
	}

	return CSS_OK;
}

css_error
handleEndBlockContent(css_language *c, const parserutils_vector *vector)
{
	context_entry *entry;
	parserutils_error perror;
	css_error ret;

	/* First we call handleBlockContent() to deal with any intermediate
	 * tokens we have left
	 */
	ret = handleBlockContent(c, vector);

	/* Our goal here is to ensure that the language parse stack is in the
	 * right state.  We've encountered the end of a BlockContent such as
	 * @media ... { ... }
	 * and we need to ensure that the language stack reflects the end of
	 * that block, not any unfinished business within it such as
	 * @media ... { d }
	 */

	entry = parserutils_stack_get_current(c->context);
	while (entry != NULL && entry->type != CSS_PARSER_START_BLOCK) {
		perror = parserutils_stack_pop(c->context, NULL);
		if (perror != PARSERUTILS_OK) {
			return css_error_from_parserutils_error(perror);
		}
		entry = parserutils_stack_get_current(c->context);
	}

	return ret;
}

static void
reportInvalid(const char *what, const parserutils_vector *vector, int32_t ctx)
{
	static int enabled = -1;
	const css_token *token;

	if (enabled < 0) {
		enabled = getenv("NETSURFY_CSS_REPORT") != NULL;
	}
	if (!enabled || vector == NULL ||
	    parserutils_vector_peek(vector, ctx) == NULL) {
		return;
	}
	fprintf(stderr, "CSS_REJECT %s ", what);
	while ((token = parserutils_vector_iterate(vector, &ctx)) != NULL) {
		const uint8_t *data = token->data.data;
		int len = (int)token->data.len;
		if (token->idata != NULL) {
			data = (const uint8_t *)lwc_string_data(token->idata);
			len = (int)lwc_string_length(token->idata);
		}
		switch (token->type) {
		case CSS_TOKEN_S:
			fputc(' ', stderr);
			break;
		case CSS_TOKEN_FUNCTION:
			fprintf(stderr, "%.*s(", len, data);
			break;
		case CSS_TOKEN_STRING:
			fprintf(stderr, "\"%.*s\"", len, data);
			break;
		case CSS_TOKEN_HASH:
			fprintf(stderr, "#%.*s", len, data);
			break;
		case CSS_TOKEN_ATKEYWORD:
			fprintf(stderr, "@%.*s", len, data);
			break;
		case CSS_TOKEN_PERCENTAGE:
			fprintf(stderr, "%.*s%%", len, data);
			break;
		default:
			fprintf(stderr, "%.*s", len, data);
			break;
		}
	}
	fputc('\n', stderr);
}

css_error handleDeclaration(css_language *c, const parserutils_vector *vector)
{
	css_error error;
	const css_token *token, *ident;
	int32_t ctx = 0;
	context_entry *entry;
	css_rule *rule;

	/* Locations where declarations are permitted:
	 *
	 * + In @page
	 * + In @font-face
	 * + In ruleset
	 */
	entry = parserutils_stack_get_current(c->context);
	if (entry == NULL || entry->data == NULL)
		return CSS_INVALID;

	rule = entry->data;
	if (rule == NULL ||
	    (rule->type != CSS_RULE_SELECTOR && rule->type != CSS_RULE_PAGE &&
	     rule->type != CSS_RULE_FONT_FACE))
		return CSS_INVALID;

	/* Strip any leading whitespace (can happen if in nested block) */
	consumeWhitespace(vector, &ctx);

	/* IDENT ws ':' ws value
	 *
	 * In CSS 2.1, value is any1, so '{' or ATKEYWORD => parse error
	 */
	ident = parserutils_vector_iterate(vector, &ctx);
	if (ident == NULL || ident->type != CSS_TOKEN_IDENT)
		return CSS_INVALID;

	consumeWhitespace(vector, &ctx);

	token = parserutils_vector_iterate(vector, &ctx);
	if (token == NULL || tokenIsChar(token, ':') == false)
		return CSS_INVALID;

	consumeWhitespace(vector, &ctx);

	if (rule->type == CSS_RULE_FONT_FACE) {
		css_rule_font_face *ff_rule = (css_rule_font_face *)rule;
		error = css__parse_font_descriptor(
			c, ident, vector, &ctx, ff_rule);
	} else {
		error = parseProperty(c, ident, vector, &ctx, rule);
	}
	if (error == CSS_INVALID) {
		reportInvalid("DECL", vector, 0);
	}
	if (error != CSS_OK)
		return error;

	return CSS_OK;
}

/******************************************************************************
 * At-rule parsing functions						      *
 ******************************************************************************/

/**
 * Add a namespace mapping
 *
 * \param c       Parsing context to add to
 * \param prefix  Namespace prefix, or NULL for default namespace
 * \param uri     Namespace URI
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion.
 */
css_error addNamespace(css_language *c, lwc_string *prefix, lwc_string *uri)
{
	if (prefix == NULL) {
		/* Replace default namespace */
		lwc_string_unref(c->default_namespace);

		/* Special case: if new namespace uri is "", use NULL */
		if (lwc_string_length(uri) == 0)
			c->default_namespace = NULL;
		else
			c->default_namespace = lwc_string_ref(uri);
	} else {
		/* Replace, or add mapping */
		bool match;
		uint32_t idx;

		for (idx = 0; idx < c->num_namespaces; idx++) {
			if (lwc_string_isequal(c->namespaces[idx].prefix,
					       prefix,
					       &match) == lwc_error_ok &&
			    match)
				break;
		}

		if (idx == c->num_namespaces) {
			/* Not found, create a new mapping */
			css_namespace *ns = realloc(c->namespaces,
						    sizeof(css_namespace) *
							    (c->num_namespaces +
							     1));

			if (ns == NULL)
				return CSS_NOMEM;

			ns[idx].prefix = lwc_string_ref(prefix);
			ns[idx].uri = NULL;

			c->namespaces = ns;
			c->num_namespaces++;
		}

		/* Replace namespace URI */
		lwc_string_unref(c->namespaces[idx].uri);

		/* Special case: if new namespace uri is "", use NULL */
		if (lwc_string_length(uri) == 0)
			c->namespaces[idx].uri = NULL;
		else
			c->namespaces[idx].uri = lwc_string_ref(uri);
	}

	return CSS_OK;
}

/**
 * Look up a namespace prefix
 *
 * \param c       Language parser context
 * \param prefix  Namespace prefix to find, or NULL for none
 * \param uri     Pointer to location to receive namespace URI
 * \return CSS_OK on success, CSS_INVALID if prefix is not found
 */
css_error lookupNamespace(css_language *c, lwc_string *prefix, lwc_string **uri)
{
	uint32_t idx;
	bool match;

	if (prefix == NULL) {
		*uri = NULL;
	} else {
		for (idx = 0; idx < c->num_namespaces; idx++) {
			if (lwc_string_isequal(c->namespaces[idx].prefix,
					       prefix,
					       &match) == lwc_error_ok &&
			    match)
				break;
		}

		if (idx == c->num_namespaces)
			return CSS_INVALID;

		*uri = c->namespaces[idx].uri;
	}

	return CSS_OK;
}

/******************************************************************************
 * Selector list parsing functions					      *
 ******************************************************************************/

css_error parseClass(css_language *c,
		     const parserutils_vector *vector,
		     int32_t *ctx,
		     css_selector_detail *specific)
{
	css_qname qname;
	css_selector_detail_value detail_value;
	const css_token *token;

	/* class     -> '.' IDENT */
	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || tokenIsChar(token, '.') == false)
		return CSS_INVALID;

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || token->type != CSS_TOKEN_IDENT)
		return CSS_INVALID;

	detail_value.string = NULL;

	qname.ns = NULL;
	qname.name = token->idata;

	/* Ensure lwc insensitive string is available for class names */
	if (qname.name->insensitive == NULL &&
	    lwc__intern_caseless_string(qname.name) != lwc_error_ok)
		return CSS_NOMEM;

	return css__stylesheet_selector_detail_init(
		c->sheet,
		CSS_SELECTOR_CLASS,
		&qname,
		detail_value,
		CSS_SELECTOR_DETAIL_VALUE_STRING,
		false,
		specific);
}

css_error parseAttrib(css_language *c,
		      const parserutils_vector *vector,
		      int32_t *ctx,
		      css_selector_detail *specific)
{
	css_qname qname;
	css_selector_detail_value detail_value;
	const css_token *token, *value = NULL;
	css_selector_type type = CSS_SELECTOR_ATTRIBUTE;
	css_error error;
	lwc_string *prefix = NULL;

	/* attrib    -> '[' ws namespace_prefix? IDENT ws [
	 *		       [ '=' |
	 *		         INCLUDES |
	 *		         DASHMATCH |
	 *		         PREFIXMATCH |
	 *		         SUFFIXMATCH |
	 *		         SUBSTRINGMATCH
	 *		       ] ws
	 *		       [ IDENT | STRING ] ws ]? ']'
	 * namespace_prefix -> [ IDENT | '*' ]? '|'
	 */
	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || tokenIsChar(token, '[') == false)
		return CSS_INVALID;

	consumeWhitespace(vector, ctx);

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
			      tokenIsChar(token, '*') == false &&
			      tokenIsChar(token, '|') == false))
		return CSS_INVALID;

	if (tokenIsChar(token, '|')) {
		token = parserutils_vector_iterate(vector, ctx);
	} else {
		const css_token *temp;

		temp = parserutils_vector_peek(vector, *ctx);
		if (temp != NULL && tokenIsChar(temp, '|')) {
			prefix = token->idata;

			parserutils_vector_iterate(vector, ctx);

			token = parserutils_vector_iterate(vector, ctx);
		}
	}

	if (token == NULL || token->type != CSS_TOKEN_IDENT)
		return CSS_INVALID;

	error = lookupNamespace(c, prefix, &qname.ns);
	if (error != CSS_OK)
		return error;

	qname.name = token->idata;

	consumeWhitespace(vector, ctx);

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (tokenIsChar(token, ']') == false) {
		if (tokenIsChar(token, '='))
			type = CSS_SELECTOR_ATTRIBUTE_EQUAL;
		else if (token->type == CSS_TOKEN_INCLUDES)
			type = CSS_SELECTOR_ATTRIBUTE_INCLUDES;
		else if (token->type == CSS_TOKEN_DASHMATCH)
			type = CSS_SELECTOR_ATTRIBUTE_DASHMATCH;
		else if (token->type == CSS_TOKEN_PREFIXMATCH)
			type = CSS_SELECTOR_ATTRIBUTE_PREFIX;
		else if (token->type == CSS_TOKEN_SUFFIXMATCH)
			type = CSS_SELECTOR_ATTRIBUTE_SUFFIX;
		else if (token->type == CSS_TOKEN_SUBSTRINGMATCH)
			type = CSS_SELECTOR_ATTRIBUTE_SUBSTRING;
		else
			return CSS_INVALID;

		consumeWhitespace(vector, ctx);

		token = parserutils_vector_iterate(vector, ctx);
		if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
				      token->type != CSS_TOKEN_STRING))
			return CSS_INVALID;

		value = token;

		consumeWhitespace(vector, ctx);

		token = parserutils_vector_iterate(vector, ctx);
		if (token == NULL || tokenIsChar(token, ']') == false)
			return CSS_INVALID;
	}

	detail_value.string = value != NULL ? value->idata : NULL;

	return css__stylesheet_selector_detail_init(
		c->sheet,
		type,
		&qname,
		detail_value,
		CSS_SELECTOR_DETAIL_VALUE_STRING,
		false,
		specific);
}

css_error parseNth(css_language *c,
		   const parserutils_vector *vector,
		   int32_t *ctx,
		   css_selector_detail_value *value)
{
	const css_token *token;
	bool match;

	/* nth -> [ DIMENSION | IDENT ] ws [ [ CHAR ws ]? NUMBER ws ]?
	 *        (e.g. DIMENSION: 2n-1, 2n- 1, 2n -1, 2n - 1)
	 *        (e.g. IDENT: -n-1, -n- 1, -n -1, -n - 1)
	 *     -> NUMBER ws
	 *     -> IDENT(odd) ws
	 *     -> IDENT(even) ws
	 */

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
			      token->type != CSS_TOKEN_NUMBER &&
			      token->type != CSS_TOKEN_DIMENSION))
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_IDENT &&
	    lwc_string_caseless_isequal(
		    token->idata, c->strings[ODD], &match) == lwc_error_ok &&
	    match) {
		/* Odd */
		value->nth.a = 2;
		value->nth.b = 1;
	} else if (token->type == CSS_TOKEN_IDENT &&
		   lwc_string_caseless_isequal(token->idata,
					       c->strings[EVEN],
					       &match) == lwc_error_ok &&
		   match) {
		/* Even */
		value->nth.a = 2;
		value->nth.b = 0;
	} else if (token->type == CSS_TOKEN_NUMBER) {
		size_t consumed = 0;
		css_fixed val = 0;

		val = css__number_from_lwc_string(token->idata,
						  true,
						  &consumed);
		if (consumed != lwc_string_length(token->idata))
			return CSS_INVALID;

		value->nth.a = 0;
		value->nth.b = FIXTOINT(val);
	} else {
		/* [ DIMENSION | IDENT ] ws [ [ CHAR ws ]? NUMBER ws ]?
		 *
		 * (e.g. DIMENSION: 2n-1, 2n- 1, 2n -1, 2n - 1)
		 * (e.g. IDENT: n, -n-1, -n- 1, -n -1, -n - 1)
		 */
		size_t consumed = 0, len;
		const char *data;
		css_fixed a = 0, b = 0;
		int sign = 1;
		bool had_sign = false, had_b = false;

		len = lwc_string_length(token->idata);
		data = lwc_string_data(token->idata);

		/* Compute a */
		if (token->type == CSS_TOKEN_IDENT) {
			if (len < 2) {
				if (data[0] != 'n' && data[0] != 'N')
					return CSS_INVALID;

				/* n */
				a = INTTOFIX(1);

				data += 1;
				len -= 1;
			} else {
				if (data[0] != '-' ||
				    (data[1] != 'n' && data[1] != 'N'))
					return CSS_INVALID;

				/* -n */
				a = INTTOFIX(-1);

				data += 2;
				len -= 2;
			}

			if (len > 0) {
				if (data[0] != '-')
					return CSS_INVALID;

				/* -n- */
				sign = -1;
				had_sign = true;

				if (len > 1) {
					/* Reject additional sign */
					if (data[1] == '-' || data[1] == '+')
						return CSS_INVALID;

					/* -n-b */
					b = css__number_from_string(
						(const uint8_t *)data + 1,
						len - 1,
						true,
						&consumed);
					if (consumed != len - 1)
						return CSS_INVALID;

					had_b = true;
				}
			}
		} else {
			/* 2n */
			a = css__number_from_lwc_string(token->idata,
							true,
							&consumed);
			if (consumed == 0 ||
			    (data[consumed] != 'n' && data[consumed] != 'N'))
				return CSS_INVALID;

			if (++consumed < len) {
				if (data[consumed] != '-')
					return CSS_INVALID;

				/* 2n- */
				sign = -1;
				had_sign = true;

				if (++consumed < len) {
					size_t bstart;

					/* Reject additional sign */
					if (data[consumed] == '-' ||
					    data[consumed] == '+')
						return CSS_INVALID;

					/* 2n-b */
					bstart = consumed;

					b = css__number_from_string(
						(const uint8_t *)data + bstart,
						len - bstart,
						true,
						&consumed);
					if (consumed != len - bstart)
						return CSS_INVALID;

					had_b = true;
				}
			}
		}

		if (had_b == false) {
			consumeWhitespace(vector, ctx);

			/* Look for optional b : [ [ CHAR ws ]? NUMBER ws ]? */
			token = parserutils_vector_peek(vector, *ctx);

			if (had_sign == false && token != NULL &&
			    (tokenIsChar(token, '-') ||
			     tokenIsChar(token, '+'))) {
				parserutils_vector_iterate(vector, ctx);

				had_sign = true;

				if (tokenIsChar(token, '-'))
					sign = -1;

				consumeWhitespace(vector, ctx);

				token = parserutils_vector_peek(vector, *ctx);
			}

			/* Expect NUMBER */
			if (token != NULL && token->type == CSS_TOKEN_NUMBER) {
				parserutils_vector_iterate(vector, ctx);

				/* If we've already seen a sign, ensure one
				 * does not occur at the start of this token
				 */
				if (had_sign &&
				    lwc_string_length(token->idata) > 0) {
					data = lwc_string_data(token->idata);

					if (data[0] == '-' || data[0] == '+')
						return CSS_INVALID;
				}

				b = css__number_from_lwc_string(token->idata,
								true,
								&consumed);
				if (consumed != lwc_string_length(token->idata))
					return CSS_INVALID;
			}
		}

		value->nth.a = FIXTOINT(a);
		value->nth.b = FIXTOINT(b) * sign;
	}

	consumeWhitespace(vector, ctx);

	return CSS_OK;
}

static void skipToComma(const parserutils_vector *vector, int32_t *ctx)
{
	const css_token *token;
	int depth = 0;

	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL) {
		if (depth == 0 && tokenIsChar(token, ',')) {
			return;
		}
		if (token->type == CSS_TOKEN_FUNCTION ||
		    tokenIsChar(token, '(')) {
			depth++;
		} else if (tokenIsChar(token, ')') && depth > 0) {
			depth--;
		}
		parserutils_vector_iterate(vector, ctx);
	}
}

static css_error parseSelectorListArgument(css_language *c,
					   const parserutils_vector *vector,
					   int32_t *ctx,
					   css_selector_list_kind kind,
					   css_selector_list **result)
{
	bool forgiving = kind == CSS_SELECTOR_LIST_IS ||
			 kind == CSS_SELECTOR_LIST_WHERE;
	parserutils_vector *sub = NULL;
	css_selector_list *list;
	const css_token *token;
	int32_t sub_ctx = 0;
	uint32_t capacity = 1;
	css_error error;

	error = collectParens(vector, ctx, &sub);
	if (error != CSS_OK)
		return error;

	while ((token = parserutils_vector_iterate(sub, &sub_ctx)) != NULL) {
		if (tokenIsChar(token, ','))
			capacity++;
	}
	sub_ctx = 0;

	list = calloc(1, sizeof(*list) + capacity * sizeof(list->items[0]));
	if (list == NULL) {
		parserutils_vector_destroy(sub);
		return CSS_NOMEM;
	}
	list->kind = kind;

	for (;;) {
		css_combinator comb = CSS_COMBINATOR_ANCESTOR;
		css_selector *selector = NULL;

		consumeWhitespace(sub, &sub_ctx);
		token = parserutils_vector_peek(sub, sub_ctx);
		if (kind == CSS_SELECTOR_LIST_HAS && token != NULL) {
			if (tokenIsChar(token, '>')) {
				comb = CSS_COMBINATOR_PARENT;
			} else if (tokenIsChar(token, '+')) {
				comb = CSS_COMBINATOR_SIBLING;
			} else if (tokenIsChar(token, '~')) {
				comb = CSS_COMBINATOR_GENERIC_SIBLING;
			}
			if (comb != CSS_COMBINATOR_ANCESTOR) {
				parserutils_vector_iterate(sub, &sub_ctx);
				consumeWhitespace(sub, &sub_ctx);
			}
		}

		error = parseSelector(c, sub, &sub_ctx, &selector);
		if (error == CSS_OK) {
			consumeWhitespace(sub, &sub_ctx);
			token = parserutils_vector_peek(sub, sub_ctx);
			if (token != NULL && !tokenIsChar(token, ',')) {
				css__stylesheet_selector_destroy(c->sheet,
								 selector);
				selector = NULL;
				error = CSS_INVALID;
			}
		}
		if (error == CSS_OK) {
			if (kind != CSS_SELECTOR_LIST_WHERE &&
			    selector->specificity > list->specificity) {
				list->specificity = selector->specificity;
			}
			list->items[list->count].selector = selector;
			list->items[list->count].comb = comb;
			list->count++;
		} else if (error == CSS_NOMEM || !forgiving) {
			css__stylesheet_selector_list_destroy(c->sheet, list);
			parserutils_vector_destroy(sub);
			return error;
		} else {
			skipToComma(sub, &sub_ctx);
		}

		token = parserutils_vector_iterate(sub, &sub_ctx);
		if (token == NULL)
			break;
	}

	parserutils_vector_destroy(sub);

	if (list->count == 0 && !forgiving) {
		css__stylesheet_selector_list_destroy(c->sheet, list);
		return CSS_INVALID;
	}

	*result = list;
	return CSS_OK;
}

css_error parsePseudo(css_language *c,
		      const parserutils_vector *vector,
		      int32_t *ctx,
		      bool in_not,
		      css_selector_detail *specific)
{
	static const struct {
		int index;
		css_selector_type type;
	} pseudo_lut[] = {{FIRST_CHILD, CSS_SELECTOR_PSEUDO_CLASS},
			  {LINK, CSS_SELECTOR_PSEUDO_CLASS},
			  {VISITED, CSS_SELECTOR_PSEUDO_CLASS},
			  {HOVER, CSS_SELECTOR_PSEUDO_CLASS},
			  {ACTIVE, CSS_SELECTOR_PSEUDO_CLASS},
			  {FOCUS, CSS_SELECTOR_PSEUDO_CLASS},
			  {LANG, CSS_SELECTOR_PSEUDO_CLASS},
			  {LEFT, CSS_SELECTOR_PSEUDO_CLASS},
			  {RIGHT, CSS_SELECTOR_PSEUDO_CLASS},
			  {FIRST, CSS_SELECTOR_PSEUDO_CLASS},
			  {ROOT, CSS_SELECTOR_PSEUDO_CLASS},
			  {NTH_CHILD, CSS_SELECTOR_PSEUDO_CLASS},
			  {NTH_LAST_CHILD, CSS_SELECTOR_PSEUDO_CLASS},
			  {NTH_OF_TYPE, CSS_SELECTOR_PSEUDO_CLASS},
			  {NTH_LAST_OF_TYPE, CSS_SELECTOR_PSEUDO_CLASS},
			  {LAST_CHILD, CSS_SELECTOR_PSEUDO_CLASS},
			  {FIRST_OF_TYPE, CSS_SELECTOR_PSEUDO_CLASS},
			  {LAST_OF_TYPE, CSS_SELECTOR_PSEUDO_CLASS},
			  {ONLY_CHILD, CSS_SELECTOR_PSEUDO_CLASS},
			  {ONLY_OF_TYPE, CSS_SELECTOR_PSEUDO_CLASS},
			  {EMPTY, CSS_SELECTOR_PSEUDO_CLASS},
			  {TARGET, CSS_SELECTOR_PSEUDO_CLASS},
			  {ENABLED, CSS_SELECTOR_PSEUDO_CLASS},
			  {DISABLED, CSS_SELECTOR_PSEUDO_CLASS},
			  {CHECKED, CSS_SELECTOR_PSEUDO_CLASS},
			  {NOT, CSS_SELECTOR_PSEUDO_CLASS},
			  {IS, CSS_SELECTOR_PSEUDO_CLASS},
			  {WHERE, CSS_SELECTOR_PSEUDO_CLASS},
			  {HAS, CSS_SELECTOR_PSEUDO_CLASS},
			  {HOST, CSS_SELECTOR_PSEUDO_CLASS},

			  {FIRST_LINE, CSS_SELECTOR_PSEUDO_ELEMENT},
			  {FIRST_LETTER, CSS_SELECTOR_PSEUDO_ELEMENT},
			  {BEFORE, CSS_SELECTOR_PSEUDO_ELEMENT},
			  {AFTER, CSS_SELECTOR_PSEUDO_ELEMENT}};
	css_selector_detail_value detail_value;
	css_selector_detail_value_type value_type =
		CSS_SELECTOR_DETAIL_VALUE_STRING;
	css_qname qname;
	const css_token *token;
	bool match = false, require_element = false, negate = false;
	uint32_t lut_idx;
	css_selector_type type =
		CSS_SELECTOR_PSEUDO_CLASS; /* GCC's braindead */
	css_error error;

	/* pseudo    -> ':' ':'? [ IDENT | FUNCTION ws any1 ws ')' ] */

	detail_value.string = NULL;

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || tokenIsChar(token, ':') == false)
		return CSS_INVALID;

	/* Optional second colon before pseudo element names */
	token = parserutils_vector_iterate(vector, ctx);
	if (token != NULL && tokenIsChar(token, ':')) {
		/* If present, we require a pseudo element */
		require_element = true;

		/* Consume subsequent token */
		token = parserutils_vector_iterate(vector, ctx);
	}

	/* Expect IDENT or FUNCTION */
	if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
			      token->type != CSS_TOKEN_FUNCTION))
		return CSS_INVALID;

	qname.ns = NULL;
	qname.name = token->idata;

	/* Search lut for selector type */
	for (lut_idx = 0; lut_idx < N_ELEMENTS(pseudo_lut); lut_idx++) {
		if ((lwc_string_caseless_isequal(
			     qname.name,
			     c->strings[pseudo_lut[lut_idx].index],
			     &match) == lwc_error_ok) &&
		    match) {
			type = pseudo_lut[lut_idx].type;
			break;
		}
	}

	/* Not found: invalid */
	if (lut_idx == N_ELEMENTS(pseudo_lut))
		return CSS_INVALID;

	/* Required a pseudo element, but didn't find one: invalid */
	if (require_element && type != CSS_SELECTOR_PSEUDO_ELEMENT)
		return CSS_INVALID;

	/* :not() and pseudo elements are not permitted in :not() */
	if (in_not && (type == CSS_SELECTOR_PSEUDO_ELEMENT ||
		       pseudo_lut[lut_idx].index == NOT))
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_FUNCTION) {
		int fun_type = pseudo_lut[lut_idx].index;

		consumeWhitespace(vector, ctx);

		if (fun_type == LANG) {
			/* IDENT */
			token = parserutils_vector_iterate(vector, ctx);
			if (token == NULL || token->type != CSS_TOKEN_IDENT)
				return CSS_INVALID;

			detail_value.string = token->idata;
			value_type = CSS_SELECTOR_DETAIL_VALUE_STRING;

			consumeWhitespace(vector, ctx);
		} else if (fun_type == NTH_CHILD ||
			   fun_type == NTH_LAST_CHILD ||
			   fun_type == NTH_OF_TYPE ||
			   fun_type == NTH_LAST_OF_TYPE) {
			/* an + b */
			error = parseNth(c, vector, ctx, &detail_value);
			if (error != CSS_OK)
				return error;

			value_type = CSS_SELECTOR_DETAIL_VALUE_NTH;
		} else if (fun_type == NOT || fun_type == IS ||
			   fun_type == WHERE || fun_type == HAS ||
			   fun_type == HOST) {
			css_selector_list *list = NULL;

			error = parseSelectorListArgument(
				c,
				vector,
				ctx,
				fun_type == NOT	    ? CSS_SELECTOR_LIST_NOT
				: fun_type == IS    ? CSS_SELECTOR_LIST_IS
				: fun_type == WHERE ? CSS_SELECTOR_LIST_WHERE
				: fun_type == HAS   ? CSS_SELECTOR_LIST_HAS
						    : CSS_SELECTOR_LIST_HOST,
				&list);
			if (error != CSS_OK)
				return error;

			detail_value.list = list;
			return css__stylesheet_selector_detail_init(
				c->sheet,
				CSS_SELECTOR_PSEUDO_CLASS,
				&qname,
				detail_value,
				CSS_SELECTOR_DETAIL_VALUE_LIST,
				false,
				specific);
		}

		token = parserutils_vector_iterate(vector, ctx);
		if (token == NULL || tokenIsChar(token, ')') == false)
			return CSS_INVALID;
	}

	return css__stylesheet_selector_detail_init(c->sheet,
						    type,
						    &qname,
						    detail_value,
						    value_type,
						    negate,
						    specific);
}

css_error parseSpecific(css_language *c,
			const parserutils_vector *vector,
			int32_t *ctx,
			bool in_not,
			css_selector_detail *specific)
{
	css_error error;
	const css_token *token;

	/* specific  -> [ HASH | class | attrib | pseudo ] */

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_HASH) {
		css_qname qname;
		css_selector_detail_value detail_value;

		detail_value.string = NULL;

		qname.ns = NULL;
		qname.name = token->idata;

		/* Ensure lwc insensitive string is available for id names */
		if (qname.name->insensitive == NULL &&
		    lwc__intern_caseless_string(qname.name) != lwc_error_ok)
			return CSS_NOMEM;

		error = css__stylesheet_selector_detail_init(
			c->sheet,
			CSS_SELECTOR_ID,
			&qname,
			detail_value,
			CSS_SELECTOR_DETAIL_VALUE_STRING,
			false,
			specific);
		if (error != CSS_OK)
			return error;

		parserutils_vector_iterate(vector, ctx);
	} else if (tokenIsChar(token, '.')) {
		error = parseClass(c, vector, ctx, specific);
		if (error != CSS_OK)
			return error;
	} else if (tokenIsChar(token, '[')) {
		error = parseAttrib(c, vector, ctx, specific);
		if (error != CSS_OK)
			return error;
	} else if (tokenIsChar(token, ':')) {
		error = parsePseudo(c, vector, ctx, in_not, specific);
		if (error != CSS_OK)
			return error;
	} else {
		return CSS_INVALID;
	}

	return CSS_OK;
}

css_error parseAppendSpecific(css_language *c,
			      const parserutils_vector *vector,
			      int32_t *ctx,
			      css_selector **parent)
{
	css_error error;
	css_selector_detail specific;

	error = parseSpecific(c, vector, ctx, false, &specific);
	if (error != CSS_OK)
		return error;

	error = css__stylesheet_selector_append_specific(c->sheet,
							 parent,
							 &specific);
	if (error != CSS_OK &&
	    specific.value_type == CSS_SELECTOR_DETAIL_VALUE_LIST) {
		css__stylesheet_selector_list_destroy(c->sheet,
						      specific.value.list);
	}
	return error;
}

css_error parseSelectorSpecifics(css_language *c,
				 const parserutils_vector *vector,
				 int32_t *ctx,
				 css_selector **parent)
{
	css_error error;
	const css_token *token;

	/* specifics -> specific* */
	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL &&
	       token->type != CSS_TOKEN_S && tokenIsChar(token, '+') == false &&
	       tokenIsChar(token, '>') == false &&
	       tokenIsChar(token, '~') == false &&
	       tokenIsChar(token, ',') == false) {
		error = parseAppendSpecific(c, vector, ctx, parent);
		if (error != CSS_OK)
			return error;
	}

	return CSS_OK;
}

css_error parseTypeSelector(css_language *c,
			    const parserutils_vector *vector,
			    int32_t *ctx,
			    css_qname *qname)
{
	const css_token *token;
	css_error error;
	lwc_string *prefix = NULL;

	/* type_selector    -> namespace_prefix? element_name
	 * namespace_prefix -> [ IDENT | '*' ]? '|'
	 * element_name	    -> IDENT | '*'
	 */

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (tokenIsChar(token, '|') == false) {
		prefix = token->idata;

		parserutils_vector_iterate(vector, ctx);

		token = parserutils_vector_peek(vector, *ctx);
	}

	if (token != NULL && tokenIsChar(token, '|')) {
		/* Have namespace prefix */
		parserutils_vector_iterate(vector, ctx);

		/* Expect element_name */
		token = parserutils_vector_iterate(vector, ctx);

		if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
				      tokenIsChar(token, '*') == false)) {
			return CSS_INVALID;
		}

		error = lookupNamespace(c, prefix, &qname->ns);
		if (error != CSS_OK)
			return error;

		qname->name = token->idata;
	} else {
		/* No namespace prefix */
		if (c->default_namespace == NULL) {
			qname->ns = c->strings[UNIVERSAL];
		} else {
			qname->ns = c->default_namespace;
		}

		qname->name = prefix;
	}

	/* Ensure lwc insensitive string is available for element names */
	if (qname->name->insensitive == NULL &&
	    lwc__intern_caseless_string(qname->name) != lwc_error_ok)
		return CSS_NOMEM;

	return CSS_OK;
}

css_error parseSimpleSelector(css_language *c,
			      const parserutils_vector *vector,
			      int32_t *ctx,
			      css_selector **result)
{
	int32_t orig_ctx = *ctx;
	css_error error;
	const css_token *token;
	css_selector *selector;
	css_qname qname;

	/* simple_selector  -> type_selector specifics
	 *		    -> specific specifics
	 */

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_IDENT || tokenIsChar(token, '*') ||
	    tokenIsChar(token, '|')) {
		/* Have type selector */
		error = parseTypeSelector(c, vector, ctx, &qname);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}

		error = css__stylesheet_selector_create(c->sheet,
							&qname,
							&selector);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
	} else {
		/* Universal selector */
		if (c->default_namespace == NULL)
			qname.ns = c->strings[UNIVERSAL];
		else
			qname.ns = c->default_namespace;

		qname.name = c->strings[UNIVERSAL];

		error = css__stylesheet_selector_create(c->sheet,
							&qname,
							&selector);
		if (error != CSS_OK)
			return error;

		/* Ensure we have at least one specific selector */
		error = parseAppendSpecific(c, vector, ctx, &selector);
		if (error != CSS_OK) {
			css__stylesheet_selector_destroy(c->sheet, selector);
			return error;
		}
	}

	error = parseSelectorSpecifics(c, vector, ctx, &selector);
	if (error != CSS_OK) {
		css__stylesheet_selector_destroy(c->sheet, selector);
		return error;
	}

	*result = selector;

	return CSS_OK;
}

css_error parseCombinator(css_language *c,
			  const parserutils_vector *vector,
			  int32_t *ctx,
			  css_combinator *result)
{
	const css_token *token;
	css_combinator comb = CSS_COMBINATOR_NONE;

	/* combinator	   -> ws '+' ws | ws '>' ws | ws '~' ws | ws1 */

	UNUSED(c);

	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL) {
		if (tokenIsChar(token, '+'))
			comb = CSS_COMBINATOR_SIBLING;
		else if (tokenIsChar(token, '>'))
			comb = CSS_COMBINATOR_PARENT;
		else if (tokenIsChar(token, '~'))
			comb = CSS_COMBINATOR_GENERIC_SIBLING;
		else if (token->type == CSS_TOKEN_S)
			comb = CSS_COMBINATOR_ANCESTOR;
		else
			break;

		parserutils_vector_iterate(vector, ctx);

		/* If we've seen a '+', '>', or '~', we're done. */
		if (comb != CSS_COMBINATOR_ANCESTOR)
			break;
	}

	/* No valid combinator found */
	if (comb == CSS_COMBINATOR_NONE)
		return CSS_INVALID;

	/* Consume any trailing whitespace */
	consumeWhitespace(vector, ctx);

	*result = comb;

	return CSS_OK;
}

css_error parseSelector(css_language *c,
			const parserutils_vector *vector,
			int32_t *ctx,
			css_selector **result)
{
	css_error error;
	const css_token *token = NULL;
	css_selector *selector = NULL;

	/* selector -> simple_selector [ combinator simple_selector ]* ws
	 *
	 * Note, however, that, as combinator can be wholly whitespace,
	 * there's an ambiguity as to whether "ws" has been reached. We
	 * resolve this by attempting to extract a combinator, then
	 * recovering when we detect that we've reached the end of the
	 * selector.
	 */

	error = parseSimpleSelector(c, vector, ctx, &selector);
	if (error != CSS_OK)
		return error;

	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL &&
	       tokenIsChar(token, ',') == false) {
		css_combinator comb = CSS_COMBINATOR_NONE;
		css_selector *other = NULL;

		error = parseCombinator(c, vector, ctx, &comb);
		if (error != CSS_OK) {
			css__stylesheet_selector_destroy(c->sheet, selector);
			return error;
		}

		/* In the case of "html , body { ... }", the whitespace after
		 * "html" and "body" will be considered an ancestor combinator.
		 * This clearly is not the case, however. Therefore, as a
		 * special case, if we've got an ancestor combinator and there
		 * are no further tokens, or if the next token is a comma,
		 * we ignore the supposed combinator and continue. */
		if (comb == CSS_COMBINATOR_ANCESTOR &&
		    ((token = parserutils_vector_peek(vector, *ctx)) == NULL ||
		     tokenIsChar(token, ',')))
			continue;

		error = parseSimpleSelector(c, vector, ctx, &other);
		if (error != CSS_OK) {
			css__stylesheet_selector_destroy(c->sheet, selector);
			return error;
		}

		error = css__stylesheet_selector_combine(
			c->sheet, comb, selector, other);
		if (error != CSS_OK) {
			css__stylesheet_selector_destroy(c->sheet, selector);
			css__stylesheet_selector_destroy(c->sheet, other);
			return error;
		}

		selector = other;
	}

	*result = selector;

	return CSS_OK;
}

css_error parseSelectorList(css_language *c,
			    const parserutils_vector *vector,
			    css_rule *rule)
{
	css_error error;
	const css_token *token = NULL;
	css_selector *selector = NULL;
	int32_t ctx = 0;

	/* Strip any leading whitespace (can happen if in nested block) */
	consumeWhitespace(vector, &ctx);

	/* selector_list   -> selector [ ',' ws selector ]* */

	error = parseSelector(c, vector, &ctx, &selector);
	if (error != CSS_OK) {
		if (selector != NULL)
			css__stylesheet_selector_destroy(c->sheet, selector);
		return error;
	}

	assert(selector != NULL);

	error = css__stylesheet_rule_add_selector(c->sheet, rule, selector);
	if (error != CSS_OK) {
		css__stylesheet_selector_destroy(c->sheet, selector);
		return error;
	}

	while (parserutils_vector_peek(vector, ctx) != NULL) {
		token = parserutils_vector_iterate(vector, &ctx);
		if (tokenIsChar(token, ',') == false)
			return CSS_INVALID;

		consumeWhitespace(vector, &ctx);

		selector = NULL;

		error = parseSelector(c, vector, &ctx, &selector);
		if (error != CSS_OK) {
			if (selector != NULL) {
				css__stylesheet_selector_destroy(c->sheet,
								 selector);
			}
			return error;
		}

		assert(selector != NULL);

		error = css__stylesheet_rule_add_selector(c->sheet,
							  rule,
							  selector);
		if (error != CSS_OK) {
			css__stylesheet_selector_destroy(c->sheet, selector);
			return error;
		}
	}

	return CSS_OK;
}

/******************************************************************************
 * Property parsing functions						      *
 ******************************************************************************/

static bool vars_buf_append(char **buf,
			    size_t *len,
			    size_t *alloc,
			    const char *data,
			    size_t n)
{
	if (*len + n + 1 > *alloc) {
		size_t want = (*alloc == 0 ? 64 : *alloc * 2);
		char *grown;
		while (want < *len + n + 1) {
			want *= 2;
		}
		grown = realloc(*buf, want);
		if (grown == NULL) {
			return false;
		}
		*buf = grown;
		*alloc = want;
	}
	memcpy(*buf + *len, data, n);
	*len += n;
	(*buf)[*len] = '\0';
	return true;
}

static bool vars_token_append(char **buf,
			      size_t *len,
			      size_t *alloc,
			      const css_token *token)
{
	const char *pre = "", *post = "";
	const char *data = NULL;
	size_t n = 0;

	if (token->idata != NULL) {
		data = lwc_string_data(token->idata);
		n = lwc_string_length(token->idata);
	}
	switch (token->type) {
	case CSS_TOKEN_ATKEYWORD:
		pre = "@";
		break;
	case CSS_TOKEN_HASH:
		pre = "#";
		break;
	case CSS_TOKEN_FUNCTION:
		post = "(";
		break;
	case CSS_TOKEN_STRING:
	case CSS_TOKEN_INVALID_STRING:
		pre = "\"";
		post = "\"";
		break;
	case CSS_TOKEN_URI:
		pre = "url(\"";
		post = "\")";
		break;
	case CSS_TOKEN_UNICODE_RANGE:
		pre = "U+";
		break;
	case CSS_TOKEN_PERCENTAGE:
		post = "%";
		break;
	case CSS_TOKEN_S:
		data = " ";
		n = 1;
		break;
	case CSS_TOKEN_INCLUDES:
		data = "~=";
		n = 2;
		break;
	case CSS_TOKEN_DASHMATCH:
		data = "|=";
		n = 2;
		break;
	case CSS_TOKEN_PREFIXMATCH:
		data = "^=";
		n = 2;
		break;
	case CSS_TOKEN_SUFFIXMATCH:
		data = "$=";
		n = 2;
		break;
	case CSS_TOKEN_SUBSTRINGMATCH:
		data = "*=";
		n = 2;
		break;
	case CSS_TOKEN_COMMENT:
	case CSS_TOKEN_CDO:
	case CSS_TOKEN_CDC:
	case CSS_TOKEN_EOF:
		return true;
	default:
		break;
	}
	return vars_buf_append(buf, len, alloc, pre, strlen(pre)) &&
	       (data == NULL || vars_buf_append(buf, len, alloc, data, n)) &&
	       vars_buf_append(buf, len, alloc, post, strlen(post));
}

static bool vars_strip_important(char *buf, size_t *len)
{
	size_t n = *len;
	static const char important[] = "important";
	size_t ilen = sizeof(important) - 1;

	while (n > 0 && isspace((unsigned char)buf[n - 1])) {
		n--;
	}
	if (n < ilen + 1 || strncasecmp(buf + n - ilen, important, ilen) != 0) {
		*len = n;
		buf[n] = '\0';
		return false;
	}
	n -= ilen;
	while (n > 0 && isspace((unsigned char)buf[n - 1])) {
		n--;
	}
	if (n == 0 || buf[n - 1] != '!') {
		*len = n + ilen;
		buf[*len] = '\0';
		return false;
	}
	n--;
	while (n > 0 && isspace((unsigned char)buf[n - 1])) {
		n--;
	}
	*len = n;
	buf[n] = '\0';
	return true;
}

static css_error parseVarDeclaration(css_language *c,
				     const css_token *property,
				     const parserutils_vector *vector,
				     int32_t *ctx,
				     css_rule *rule,
				     int prop)
{
	const css_token *token;
	char *buf = NULL;
	size_t len = 0, alloc = 0, start = 0;
	bool important;
	lwc_string *value = NULL;
	uint32_t name_idx = 0, value_idx = 0;
	css_style *style = NULL;
	css_error error;

	vars_buf_append(&buf, &len, &alloc, "", 0);
	while ((token = parserutils_vector_iterate(vector, ctx)) != NULL) {
		if (!vars_token_append(&buf, &len, &alloc, token)) {
			free(buf);
			return CSS_NOMEM;
		}
	}
	if (buf == NULL) {
		return CSS_NOMEM;
	}
	important = vars_strip_important(buf, &len);
	while (start < len && isspace((unsigned char)buf[start])) {
		start++;
	}
	if (start == len && prop >= 0) {
		free(buf);
		return CSS_INVALID;
	}
	if (lwc_intern_string(buf + start, len - start, &value) !=
	    lwc_error_ok) {
		free(buf);
		return CSS_NOMEM;
	}
	free(buf);

	error = css__stylesheet_string_add(c->sheet, value, &value_idx);
	if (error != CSS_OK) {
		return error;
	}
	if (prop < 0) {
		error = css__stylesheet_string_add(
			c->sheet, lwc_string_ref(property->idata), &name_idx);
		if (error != CSS_OK) {
			return error;
		}
	}

	error = css__stylesheet_style_create(c->sheet, &style);
	if (error != CSS_OK) {
		return error;
	}
	if (prop < 0) {
		error = css__stylesheet_style_append(
			style,
			buildOPV(CSS_OP_CUSTOM,
				 important ? FLAG_IMPORTANT : 0,
				 0));
		if (error == CSS_OK) {
			error = css__stylesheet_style_append(style, name_idx);
		}
	} else {
		error = css__stylesheet_style_append(
			style,
			buildOPV(CSS_OP_VAR,
				 important ? FLAG_IMPORTANT : 0,
				 (uint16_t)prop));
	}
	if (error == CSS_OK) {
		error = css__stylesheet_style_append(style, value_idx);
	}
	if (error == CSS_OK) {
		error = css__stylesheet_rule_append_style(c->sheet,
							  rule,
							  style);
	}
	if (error != CSS_OK) {
		css__stylesheet_style_destroy(style);
	}
	return error;
}

static bool vectorHasVar(const parserutils_vector *vector, int32_t ctx)
{
	const css_token *token;

	while ((token = parserutils_vector_iterate(vector, &ctx)) != NULL) {
		if (token->type == CSS_TOKEN_FUNCTION && token->idata != NULL &&
		    lwc_string_length(token->idata) == 3 &&
		    strncasecmp(lwc_string_data(token->idata), "var", 3) == 0) {
			return true;
		}
	}
	return false;
}

css_error parseProperty(css_language *c,
			const css_token *property,
			const parserutils_vector *vector,
			int32_t *ctx,
			css_rule *rule)
{
	css_error error;
	css_prop_handler handler = NULL;
	int i = 0;
	uint8_t flags = 0;
	css_style *style = NULL;
	const css_token *token;

	if (lwc_string_length(property->idata) > 2 &&
	    lwc_string_data(property->idata)[0] == '-' &&
	    lwc_string_data(property->idata)[1] == '-') {
		return parseVarDeclaration(c, property, vector, ctx, rule, -1);
	}

	/* Find property index */
	/** \todo improve on this linear search */
	for (i = FIRST_PROP; i <= LAST_PROP; i++) {
		bool match = false;

		if (lwc_string_caseless_isequal(property->idata,
						c->strings[i],
						&match) == lwc_error_ok &&
		    match)
			break;
	}
	if (i == LAST_PROP + 1)
		return CSS_INVALID;

	if (vectorHasVar(vector, *ctx)) {
		return parseVarDeclaration(
			c, property, vector, ctx, rule, i - FIRST_PROP);
	}

	/* Get handler */
	handler = property_handlers[i - FIRST_PROP];
	assert(handler != NULL);

	/* allocate style */
	error = css__stylesheet_style_create(c->sheet, &style);
	if (error != CSS_OK)
		return error;

	assert(style != NULL);

	/* Call the handler */
	error = handler(c, vector, ctx, style);
	if (error != CSS_OK) {
		css__stylesheet_style_destroy(style);
		return error;
	}

	/* Determine if this declaration is important or not */
	error = css__parse_important(c, vector, ctx, &flags);
	if (error != CSS_OK) {
		css__stylesheet_style_destroy(style);
		return error;
	}

	/* Ensure that we've exhausted all the input */
	consumeWhitespace(vector, ctx);
	token = parserutils_vector_iterate(vector, ctx);
	if (token != NULL) {
		/* Trailing junk, so discard declaration */
		css__stylesheet_style_destroy(style);
		return CSS_INVALID;
	}

	/* If it's important, then mark the style appropriately */
	if (flags != 0)
		css__make_style_important(style);

	/* Append style to rule */
	error = css__stylesheet_rule_append_style(c->sheet, rule, style);
	if (error != CSS_OK) {
		css__stylesheet_style_destroy(style);
		return error;
	}

	/* Style owned or destroyed by stylesheet, so forget about it */

	return CSS_OK;
}
