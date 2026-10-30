/*
 * Copyright 2010 John-Mark Bell <jmb@netsurf-browser.org>
 *
 * This file is part of NetSurfy, http://www.netsurf-browser.org/
 *
 * NetSurfy is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurfy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETSURFY_UTILS_HTTP_PRIMITIVES_H_
#define NETSURFY_UTILS_HTTP_PRIMITIVES_H_

#include <libwapcaplet/libwapcaplet.h>

#include "utils/errors.h"

void http__skip_LWS(const char **input);

nserror http__parse_token(const char **input, lwc_string **value);

nserror http__parse_quoted_string(const char **input, lwc_string **value);

#endif
