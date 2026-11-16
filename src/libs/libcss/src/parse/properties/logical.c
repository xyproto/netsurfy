/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

static css_error parse_sides(css_language *c,
			     const parserutils_vector *vector,
			     int32_t *ctx,
			     css_style *result,
			     const css_prop_handler *handlers,
			     int sides)
{
	static const int map2[2][2] = {{0, 0}, {0, 1}};
	static const int map4[4][4] = {
		{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
	int32_t orig_ctx = *ctx;
	int32_t start[4];
	int count = 0;
	css_error error;

	while (count < sides) {
		const css_token *token;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || tokenIsChar(token, '!')) {
			break;
		}
		start[count] = *ctx;
		error = handlers[count](c, vector, ctx, result);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
		count++;
	}
	if (count == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	for (int side = count; side < sides; side++) {
		int32_t end_ctx = *ctx;
		int from = sides == 2 ? map2[count - 1][side]
				      : map4[count - 1][side];

		*ctx = start[from];
		error = handlers[side](c, vector, ctx, result);
		*ctx = end_ctx;
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
	}

	return CSS_OK;
}

css_error css__parse_border_block(css_language *c,
				  const parserutils_vector *vector,
				  int32_t *ctx,
				  css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_border_top,
						    css__parse_border_bottom};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_block_color(css_language *c,
					const parserutils_vector *vector,
					int32_t *ctx,
					css_style *result)
{
	static const css_prop_handler handlers[] = {
		css__parse_border_top_color, css__parse_border_bottom_color};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_block_style(css_language *c,
					const parserutils_vector *vector,
					int32_t *ctx,
					css_style *result)
{
	static const css_prop_handler handlers[] = {
		css__parse_border_top_style, css__parse_border_bottom_style};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_block_width(css_language *c,
					const parserutils_vector *vector,
					int32_t *ctx,
					css_style *result)
{
	static const css_prop_handler handlers[] = {
		css__parse_border_top_width, css__parse_border_bottom_width};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_inline(css_language *c,
				   const parserutils_vector *vector,
				   int32_t *ctx,
				   css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_border_left,
						    css__parse_border_right};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_inline_color(css_language *c,
					 const parserutils_vector *vector,
					 int32_t *ctx,
					 css_style *result)
{
	static const css_prop_handler handlers[] = {
		css__parse_border_left_color, css__parse_border_right_color};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_inline_style(css_language *c,
					 const parserutils_vector *vector,
					 int32_t *ctx,
					 css_style *result)
{
	static const css_prop_handler handlers[] = {
		css__parse_border_left_style, css__parse_border_right_style};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_border_inline_width(css_language *c,
					 const parserutils_vector *vector,
					 int32_t *ctx,
					 css_style *result)
{
	static const css_prop_handler handlers[] = {
		css__parse_border_left_width, css__parse_border_right_width};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_inset(css_language *c,
			   const parserutils_vector *vector,
			   int32_t *ctx,
			   css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_top,
						    css__parse_right,
						    css__parse_bottom,
						    css__parse_left};

	return parse_sides(c, vector, ctx, result, handlers, 4);
}

css_error css__parse_inset_block(css_language *c,
				 const parserutils_vector *vector,
				 int32_t *ctx,
				 css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_top,
						    css__parse_bottom};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_inset_inline(css_language *c,
				  const parserutils_vector *vector,
				  int32_t *ctx,
				  css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_left,
						    css__parse_right};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_margin_block(css_language *c,
				  const parserutils_vector *vector,
				  int32_t *ctx,
				  css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_margin_top,
						    css__parse_margin_bottom};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_margin_inline(css_language *c,
				   const parserutils_vector *vector,
				   int32_t *ctx,
				   css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_margin_left,
						    css__parse_margin_right};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_padding_block(css_language *c,
				   const parserutils_vector *vector,
				   int32_t *ctx,
				   css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_padding_top,
						    css__parse_padding_bottom};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}

css_error css__parse_padding_inline(css_language *c,
				    const parserutils_vector *vector,
				    int32_t *ctx,
				    css_style *result)
{
	static const css_prop_handler handlers[] = {css__parse_padding_left,
						    css__parse_padding_right};

	return parse_sides(c, vector, ctx, result, handlers, 2);
}
