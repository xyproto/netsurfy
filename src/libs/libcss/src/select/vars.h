/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef css_select_vars_h_
#define css_select_vars_h_

#include <stdbool.h>
#include <stdint.h>

#include <libwapcaplet/libwapcaplet.h>
#include <libcss/computed.h>

#include "stylesheet.h"

struct css_select_state;

typedef struct css_var_custom {
	lwc_string *name;
	lwc_string *value;
} css_var_custom;

typedef struct css_var_params {
	lwc_string *url;
	css_language_level level;
	bool quirks;
	css_url_resolution_fn resolve;
	void *resolve_pw;
	css_color_resolution_fn color;
	void *color_pw;
	css_font_resolution_fn font;
	void *font_pw;
} css_var_params;

typedef struct css_var_pending {
	uint16_t prop;
	bool important;
	lwc_string *value;
	css_var_params params;
	uint16_t n_ops;
	uint16_t *ops;
} css_var_pending;

typedef struct css_computed_vars {
	uint32_t refcnt;
	uint32_t n_custom;
	css_var_custom *custom;
	uint32_t n_pending;
	css_var_pending *pending;
} css_computed_vars;

typedef struct css_select_var_decl {
	lwc_string *name;
	lwc_string *value;
	uint32_t specificity;
	uint8_t rank;
	uint8_t pseudo;
} css_select_var_decl;

typedef struct css_select_var_pend {
	uint16_t prop;
	bool important;
	uint8_t pseudo;
	lwc_string *value;
	const css_stylesheet *sheet;
} css_select_var_pend;

typedef struct css_select_vars {
	css_select_var_decl *decls;
	uint32_t n_decls;
	uint32_t alloc_decls;
	css_select_var_pend *pends;
	uint32_t n_pends;
	uint32_t alloc_pends;
} css_select_vars;

css_computed_vars *css__vars_ref(css_computed_vars *vars);
void css__vars_unref(css_computed_vars *vars);

css_error css__vars_cascade_custom(struct css_select_state *state,
				   lwc_string *name,
				   lwc_string *value,
				   bool important);
css_error css__vars_cascade_pending(struct css_select_state *state,
				    uint16_t prop,
				    lwc_string *value,
				    bool important,
				    const css_stylesheet *sheet);
css_error css__vars_finish_select(struct css_select_state *state);
void css__vars_select_cleanup(struct css_select_state *state);

css_error css__vars_resolve(const css_computed_style *parent,
			    const css_computed_vars *own,
			    css_computed_style *target);

#endif
