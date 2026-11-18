/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Michael Drake <tlsa@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse overflow shorthand
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_overflow(css_language *c,
			      const parserutils_vector *vector,
			      int32_t *ctx,
			      css_style *result)
{
	int32_t orig_ctx = *ctx;
	css_error error1, error2 = CSS_OK;
	const css_token *token;
	enum flag_value flag_value;

	token = parserutils_vector_iterate(vector, ctx);
	if ((token == NULL) || ((token->type != CSS_TOKEN_IDENT))) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	flag_value = get_css_flag_value(c, token);

	if (flag_value != FLAG_VALUE__NONE) {
		error1 = css_stylesheet_style_flag_value(result,
							 flag_value,
							 CSS_PROP_OVERFLOW_X);
		error2 = css_stylesheet_style_flag_value(result,
							 flag_value,
							 CSS_PROP_OVERFLOW_Y);

	} else {
		static const css_prop_handler handlers[] = {
			css__parse_overflow_x,
			css__parse_overflow_y,
		};

		*ctx = orig_ctx;
		return css__parse_sides(c, vector, ctx, result, handlers, 2);
	}

	if (error2 != CSS_OK)
		error1 = error2;

	if (error1 != CSS_OK)
		*ctx = orig_ctx;

	return error1;
}
