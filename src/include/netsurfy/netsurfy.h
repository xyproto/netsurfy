/*
 * Copyright 2014 Vincent Sanders <vince@netsurf-browser.org>
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

/**
 * \file
 *
 * NetSurfy core interface registration, construction and destruction.
 */

#ifndef _NETSURFY_NETSURFY_H_
#define _NETSURFY_NETSURFY_H_

#include "utils/errors.h"
#include "desktop/gui_table.h"

/**
 * Register operation table.
 *
 * @param table NetSurfy operations table.
 * @return NSERROR_OK on success or error code on faliure. On faliure
 *                    global table will not be initialised.
 */
nserror netsurfy_register(struct netsurfy_table *table);

/**
 * Initialise netsurfy core.
 *
 * @param store_path path to persistant storage.
 * @return NSERROR_OK on success or error code on faliure.
 */
nserror netsurfy_init(const char *store_path);

/**
 * Finalise NetSurfy core
 */
extern void netsurfy_exit(void);

#endif
