/*
 * Copyright 2014 Vincent Sanders <vince@netsurf-browser.org>
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

/**
 * \file
 *
 * TinySurf core interface registration, construction and destruction.
 */

#ifndef _NETSURFY_NETSURFY_H_
#define _NETSURFY_NETSURFY_H_

#include "utils/errors.h"
#include "desktop/gui_table.h"

/**
 * Register operation table.
 *
 * @param table TinySurf operations table.
 * @return NSERROR_OK on success or error code on faliure. On faliure
 *                    global table will not be initialised.
 */
nserror tinysurf_register(struct tinysurf_table *table);

/**
 * Initialise tinysurf core.
 *
 * @param store_path path to persistant storage.
 * @return NSERROR_OK on success or error code on faliure.
 */
nserror tinysurf_init(const char *store_path);

/**
 * Finalise TinySurf core
 */
extern void tinysurf_exit(void);

#endif
