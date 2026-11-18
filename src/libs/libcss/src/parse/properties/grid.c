/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 */

#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"
#include "utils/parserutilserror.h"

static bool append_token(parserutils_buffer *buf, const css_token *token)
{
	const uint8_t *data = token->data.data;
	size_t len = token->data.len;
	parserutils_error err = PARSERUTILS_OK;

	if (token->idata != NULL) {
		data = (const uint8_t *)lwc_string_data(token->idata);
		len = lwc_string_length(token->idata);
	}
	switch (token->type) {
	case CSS_TOKEN_S:
		if (buf->length > 0 && buf->data[buf->length - 1] != ' ') {
			err = parserutils_buffer_append(buf,
							(const uint8_t *)" ",
							1);
		}
		break;
	case CSS_TOKEN_FUNCTION:
		err = parserutils_buffer_append(buf, data, len);
		if (err == PARSERUTILS_OK) {
			err = parserutils_buffer_append(
				buf, (const uint8_t *)"(", 1);
		}
		break;
	case CSS_TOKEN_STRING:
		err = parserutils_buffer_append(buf, (const uint8_t *)"\"", 1);
		if (err == PARSERUTILS_OK) {
			err = parserutils_buffer_append(buf, data, len);
		}
		if (err == PARSERUTILS_OK) {
			err = parserutils_buffer_append(
				buf, (const uint8_t *)"\"", 1);
		}
		break;
	case CSS_TOKEN_PERCENTAGE:
		err = parserutils_buffer_append(buf, data, len);
		if (err == PARSERUTILS_OK) {
			err = parserutils_buffer_append(
				buf, (const uint8_t *)"%", 1);
		}
		break;
	case CSS_TOKEN_HASH:
		err = parserutils_buffer_append(buf, (const uint8_t *)"#", 1);
		if (err == PARSERUTILS_OK) {
			err = parserutils_buffer_append(buf, data, len);
		}
		break;
	default:
		err = parserutils_buffer_append(buf, data, len);
		break;
	}
	return err == PARSERUTILS_OK;
}

static css_error parse_raw(css_language *c,
			   const parserutils_vector *vector,
			   int32_t *ctx,
			   css_style *result,
			   enum css_properties_e prop)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	parserutils_buffer *buf;
	lwc_string *str = NULL;
	uint32_t snum;
	css_error error;
	enum flag_value flag_value;
	size_t len;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL) {
		return CSS_INVALID;
	}
	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value, prop);
	}

	if (parserutils_buffer_create(&buf) != PARSERUTILS_OK) {
		return CSS_NOMEM;
	}
	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL &&
	       !tokenIsChar(token, '!')) {
		parserutils_vector_iterate(vector, ctx);
		if (!append_token(buf, token)) {
			parserutils_buffer_destroy(buf);
			*ctx = orig_ctx;
			return CSS_NOMEM;
		}
	}
	len = buf->length;
	while (len > 0 && buf->data[len - 1] == ' ') {
		len--;
	}
	if (len == 0) {
		parserutils_buffer_destroy(buf);
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (len == 4 && strncasecmp((const char *)buf->data, "none", 4) == 0) {
		parserutils_buffer_destroy(buf);
		return css__stylesheet_style_appendOPV(result, prop, 0, 0);
	}
	if (lwc_intern_string((const char *)buf->data, len, &str) !=
	    lwc_error_ok) {
		parserutils_buffer_destroy(buf);
		*ctx = orig_ctx;
		return CSS_NOMEM;
	}
	parserutils_buffer_destroy(buf);

	error = css__stylesheet_string_add(c->sheet, str, &snum);
	if (error == CSS_OK) {
		error = css__stylesheet_style_appendOPV(result, prop, 0, 0x0080);
	}
	if (error == CSS_OK) {
		error = css__stylesheet_style_append(result, snum);
	}
	if (error != CSS_OK) {
		*ctx = orig_ctx;
	}
	return error;
}

#define GRID_PARSER(name, prop)                                                \
	css_error css__parse_##name(css_language *c,                           \
				    const parserutils_vector *vector,          \
				    int32_t *ctx,                              \
				    css_style *result)                         \
	{                                                                      \
		return parse_raw(c, vector, ctx, result, prop);                \
	}

GRID_PARSER(grid_template_columns, CSS_PROP_GRID_TEMPLATE_COLUMNS)
GRID_PARSER(grid_template_rows, CSS_PROP_GRID_TEMPLATE_ROWS)
GRID_PARSER(grid_template_areas, CSS_PROP_GRID_TEMPLATE_AREAS)
GRID_PARSER(grid_auto_columns, CSS_PROP_GRID_AUTO_COLUMNS)
GRID_PARSER(grid_auto_rows, CSS_PROP_GRID_AUTO_ROWS)
GRID_PARSER(grid_auto_flow, CSS_PROP_GRID_AUTO_FLOW)
GRID_PARSER(grid_column, CSS_PROP_GRID_COLUMN)
GRID_PARSER(grid_row, CSS_PROP_GRID_ROW)
GRID_PARSER(grid_area, CSS_PROP_GRID_AREA)
GRID_PARSER(grid_column_start, CSS_PROP_GRID_COLUMN_START)
GRID_PARSER(grid_column_end, CSS_PROP_GRID_COLUMN_END)
GRID_PARSER(grid_row_start, CSS_PROP_GRID_ROW_START)
GRID_PARSER(grid_row_end, CSS_PROP_GRID_ROW_END)
