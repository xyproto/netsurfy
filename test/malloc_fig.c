/*
 * Copyright 2017 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
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

/**
 * \file
 *
 * heap fault injection generation.
 *
 * This library inject allocation faults into NetSurfy tests
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

#include "test/malloc_fig.h"

static unsigned int count = UINT_MAX;

void malloc_limit(unsigned int newcount)
{
	count = newcount;
	// fprintf(stderr, "malloc_limit %d\n", count);
}

#ifdef _WIN32
void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
#else
void *malloc(size_t size)
#endif
{
#ifdef _WIN32
	void *(*real_malloc)(size_t) = __real_malloc;
#else
	static void *(*real_malloc)(size_t) = NULL;
#endif
	void *p = NULL;

#ifndef _WIN32
	if (real_malloc == NULL) {
		real_malloc = dlsym(RTLD_NEXT, "malloc");
	}
#endif

	if (count > 0) {
		p = real_malloc(size);
		count--;
	}
	// fprintf(stderr, "malloc(%d) = %p remian:%d\n", size, p, count);
	return p;
}
