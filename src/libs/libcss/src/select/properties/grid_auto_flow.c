/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_grid_auto_flow(uint32_t opv,
					css_style *style,
					css_select_state *state)
{
	return css__cascade_uri_none(opv, style, state, set_grid_auto_flow);
}

css_error css__set_grid_auto_flow_from_hint(const css_hint *hint,
					      css_computed_style *style)
{
	css_error error;

	error = set_grid_auto_flow(style, hint->status, hint->data.string);

	lwc_string_unref(hint->data.string);

	return error;
}

css_error css__initial_grid_auto_flow(css_select_state *state)
{
	return set_grid_auto_flow(state->computed,
				    CSS_GRID_AUTO_FLOW_NONE,
				    NULL);
}

css_error css__copy_grid_auto_flow(const css_computed_style *from,
				     css_computed_style *to)
{
	lwc_string *url;
	uint8_t type = get_grid_auto_flow(from, &url);

	if (from == to) {
		return CSS_OK;
	}

	return set_grid_auto_flow(to, type, url);
}

css_error css__compose_grid_auto_flow(const css_computed_style *parent,
					const css_computed_style *child,
					css_computed_style *result)
{
	lwc_string *url;
	uint8_t type = get_grid_auto_flow(child, &url);

	return css__copy_grid_auto_flow(
		type == CSS_GRID_AUTO_FLOW_INHERIT ? parent : child, result);
}
