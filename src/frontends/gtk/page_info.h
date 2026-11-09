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

#ifndef NETSURFY_GTK_PAGE_INFO_H
#define NETSURFY_GTK_PAGE_INFO_H 1

/**
 * Page information window
 *
 * \param anchor The widget the page information is shown below
 * \param bw the browser window to get page information for
 * \return NSERROR_OK or error code if prompt creation failed.
 */
nserror nsgtk_page_info(GtkWidget *anchor, struct browser_window *bw);


#endif
