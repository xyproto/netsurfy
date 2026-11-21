/*
 * Copyright 2018 Vincent Sanders <vince@nexturf-browser.org>
 *
 * This file is part of TinySurf, https://github.com/xyproto/tinysurf
 *
 * TinySurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * TinySurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include "headless/output.h"

/**
 * output type prefixes
 */
static const char *type_text[] = {
	"DIE",
	"ERROR",
	"WARN",
	"GENERIC",
	"WINDOW",
	"LOGIN",
	"DOWNLOAD",
	"PLOT",
};

/* exported interface documented in headless/output.h */
bool headless_quiet = false;

int moutf(enum headless_output_type mout_type, const char *fmt, ...)
{
	FILE *out = stdout;
	va_list ap;
	int res;

	if (headless_quiet) {
		if (mout_type != MOUT_DIE) {
			return 0;
		}
		out = stderr;
	}

	res = fprintf(out, "%s ", type_text[mout_type]);

	va_start(ap, fmt);
	res += vfprintf(out, fmt, ap);
	va_end(ap);

	fputc('\n', out);

	return res + 1;
}
