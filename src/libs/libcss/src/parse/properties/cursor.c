/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse cursor
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *         CSS_NOMEM on memory exhaustion,
 *         CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *                 If the input is invalid, then \a *ctx remains unchanged.
 */
static css_error cursor_fallback(css_language *c,
				 const css_token *token,
				 bool first,
				 css_style *result)
{
	static const struct {
		const char *name;
		uint16_t value;
	} map[] = {
		{"none", CURSOR_DEFAULT},
		{"context-menu", CURSOR_DEFAULT},
		{"not-allowed", CURSOR_DEFAULT},
		{"no-drop", CURSOR_DEFAULT},
		{"alias", CURSOR_POINTER},
		{"copy", CURSOR_POINTER},
		{"zoom-in", CURSOR_POINTER},
		{"zoom-out", CURSOR_POINTER},
		{"cell", CURSOR_CROSSHAIR},
		{"vertical-text", CURSOR_TEXT},
		{"grab", CURSOR_MOVE},
		{"grabbing", CURSOR_MOVE},
		{"all-scroll", CURSOR_MOVE},
		{"col-resize", CURSOR_E_RESIZE},
		{"ew-resize", CURSOR_E_RESIZE},
		{"row-resize", CURSOR_N_RESIZE},
		{"ns-resize", CURSOR_N_RESIZE},
		{"nesw-resize", CURSOR_NE_RESIZE},
		{"nwse-resize", CURSOR_NW_RESIZE},
	};
	const char *name = lwc_string_data(token->idata);
	size_t len = lwc_string_length(token->idata);

	UNUSED(c);
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strlen(map[i].name) == len &&
		    strncasecmp(map[i].name, name, len) == 0) {
			return css__stylesheet_style_append(
				result,
				first ? buildOPV(CSS_PROP_CURSOR,
						 0,
						 map[i].value)
				      : map[i].value);
		}
	}
	return CSS_INVALID;
}

css_error css__parse_cursor(css_language *c,
			    const parserutils_vector *vector,
			    int32_t *ctx,
			    css_style *result)
{
	int32_t orig_ctx = *ctx;
	css_error error = CSS_OK;
	const css_token *token;
	enum flag_value flag_value;
	bool match;

	/* [ (URI ',')* IDENT(auto, crosshair, default, pointer, move, e-resize,
	 *              ne-resize, nw-resize, n-resize, se-resize, sw-resize,
	 *              s-resize, w-resize, text, wait, help, progress) ]
	 * | IDENT(inherit)
	 */
	token = parserutils_vector_iterate(vector, ctx);
	if ((token == NULL) ||
	    (token->type != CSS_TOKEN_IDENT && token->type != CSS_TOKEN_URI)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	flag_value = get_css_flag_value(c, token);

	if (flag_value != FLAG_VALUE__NONE) {
		error = css_stylesheet_style_flag_value(result,
							flag_value,
							CSS_PROP_CURSOR);
	} else {
		bool first = true;

/* Macro to output the value marker, awkward because we need to check
 * first to determine how the value is constructed.
 */
#define CSS_APPEND(CSSVAL)                                                     \
	css__stylesheet_style_append(                                          \
		result, first ? buildOPV(CSS_PROP_CURSOR, 0, CSSVAL) : CSSVAL)


		/* URI* */
		while (token != NULL && token->type == CSS_TOKEN_URI) {
			lwc_string *uri;
			uint32_t uri_snumber;

			error = c->sheet->resolve(c->sheet->resolve_pw,
						  c->sheet->url,
						  token->idata,
						  &uri);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}

			error = css__stylesheet_string_add(c->sheet,
							   uri,
							   &uri_snumber);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}

			error = CSS_APPEND(CURSOR_URI);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}

			error = css__stylesheet_style_append(result,
							     uri_snumber);
			if (error != CSS_OK) {
				*ctx = orig_ctx;
				return error;
			}

			consumeWhitespace(vector, ctx);

			/* Expect ',' */
			token = parserutils_vector_iterate(vector, ctx);
			if (token == NULL || tokenIsChar(token, ',') == false) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}

			consumeWhitespace(vector, ctx);

			/* Expect either URI or IDENT */
			token = parserutils_vector_iterate(vector, ctx);
			if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
					      token->type != CSS_TOKEN_URI)) {
				*ctx = orig_ctx;
				return CSS_INVALID;
			}

			first = false;
		}

		/* IDENT */
		if (token != NULL && token->type == CSS_TOKEN_IDENT) {
			if ((lwc_string_caseless_isequal(
				     token->idata, c->strings[AUTO], &match) ==
				     lwc_error_ok &&
			     match)) {
				error = CSS_APPEND(CURSOR_AUTO);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[CROSSHAIR],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_CROSSHAIR);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[DEFAULT],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_DEFAULT);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[POINTER],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_POINTER);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[MOVE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_MOVE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[E_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_E_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[NE_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_NE_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[NW_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_NW_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[N_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_N_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[SE_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_SE_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[SW_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_SW_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[S_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_S_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[W_RESIZE],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_W_RESIZE);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[LIBCSS_TEXT],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_TEXT);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[WAIT],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_WAIT);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[HELP],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_HELP);
			} else if ((lwc_string_caseless_isequal(
					    token->idata,
					    c->strings[PROGRESS],
					    &match) == lwc_error_ok &&
				    match)) {
				error = CSS_APPEND(CURSOR_PROGRESS);
			} else {
				error = cursor_fallback(
					c, token, first, result);
			}
		}
	}

	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}
