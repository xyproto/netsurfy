/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse padding shorthand
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
css_error css__parse_padding(css_language *c,
			     const parserutils_vector *vector,
			     int32_t *ctx,
			     css_style *result)
{
	const css_token *token;
	enum flag_value flag_value;
	css_error error;

	/* Firstly, handle inherit */
	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);

	if (flag_value != FLAG_VALUE__NONE) {
		error = css_stylesheet_style_flag_value(result,
							flag_value,
							CSS_PROP_PADDING_TOP);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result,
							flag_value,
							CSS_PROP_PADDING_RIGHT);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(
			result, flag_value, CSS_PROP_PADDING_BOTTOM);
		if (error != CSS_OK)
			return error;

		error = css_stylesheet_style_flag_value(result,
							flag_value,
							CSS_PROP_PADDING_LEFT);
		if (error == CSS_OK)
			parserutils_vector_iterate(vector, ctx);

		return error;
	}

	static const css_prop_handler handlers[] = {
		css__parse_padding_top,
		css__parse_padding_right,
		css__parse_padding_bottom,
		css__parse_padding_left,
	};

	return css__parse_sides(c, vector, ctx, result, handlers, 4);
}
