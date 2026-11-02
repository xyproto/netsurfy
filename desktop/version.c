/*
 * Copyright 2019 Vincent Sanders <vince@netsurf-browser.org>
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

#include "desktop/version.h"
#include "testament.h"

const char *const netsurfy_version = "1.0.2"
#if defined(CI_BUILD)
				     " (CI #" CI_BUILD ")"
#endif
	;
const int netsurfy_version_major = 1;
const int netsurfy_version_minor = 0;
