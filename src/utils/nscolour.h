/*
 * Copyright 2020 Michael Drake <tlsa@netsurf-browser.org>
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

/** \file
 * TinySurf UI colours (interface).
 *
 * Interface to acquire common colours used throughout TinySurf's interface.
 */

#ifndef _NETSURFY_UTILS_NSCOLOUR_H_
#define _NETSURFY_UTILS_NSCOLOUR_H_

#include "tinysurf/types.h"

/**
 * TinySurf UI colour key.
 */
enum nscolour {
	NSCOLOUR_WIN_ODD_BG,
	NSCOLOUR_WIN_ODD_BG_HOVER,
	NSCOLOUR_WIN_ODD_FG,
	NSCOLOUR_WIN_ODD_FG_SUBTLE,
	NSCOLOUR_WIN_ODD_FG_FADED,
	NSCOLOUR_WIN_ODD_FG_GOOD,
	NSCOLOUR_WIN_ODD_FG_BAD,
	NSCOLOUR_WIN_ODD_BORDER,
	NSCOLOUR_WIN_EVEN_BG,
	NSCOLOUR_WIN_EVEN_BG_HOVER,
	NSCOLOUR_WIN_EVEN_FG,
	NSCOLOUR_WIN_EVEN_FG_SUBTLE,
	NSCOLOUR_WIN_EVEN_FG_FADED,
	NSCOLOUR_WIN_EVEN_FG_GOOD,
	NSCOLOUR_WIN_EVEN_FG_BAD,
	NSCOLOUR_WIN_EVEN_BORDER,
	NSCOLOUR_TEXT_INPUT_BG,
	NSCOLOUR_TEXT_INPUT_FG,
	NSCOLOUR_TEXT_INPUT_FG_SUBTLE,
	NSCOLOUR_SEL_BG,
	NSCOLOUR_SEL_FG,
	NSCOLOUR_SEL_FG_SUBTLE,
	NSCOLOUR_SCROLL_WELL,
	NSCOLOUR_BUTTON_BG,
	NSCOLOUR_BUTTON_FG,
	NSCOLOUR__COUNT,
};

/**
 * TinySurf UI colour table.
 */
extern colour nscolours[];

/**
 * Update the nscolour table from the current nsoptions.
 *
 * \return NSERROR_OK on success, or appropriate error otherwise.
 */
nserror nscolour_update(void);

/**
 * Get a pointer to a stylesheet for nscolours.
 *
 * \return NSERROR_OK on success, or appropriate error otherwise.
 */
nserror nscolour_get_stylesheet(const char **stylesheet_out);

#endif
