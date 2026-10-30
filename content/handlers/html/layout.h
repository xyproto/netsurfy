/*
 * Copyright 2003 James Bursa <bursa@users.sourceforge.net>
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
 * interface to HTML layout.
 *
 * The main interface to the layout code is layout_document(), which takes a
 * normalized box tree and assigns coordinates and dimensions to the boxes, and
 * also adds boxes to the tree (eg. when formatting lines of text).
 */

#ifndef NETSURFY_HTML_LAYOUT_H
#define NETSURFY_HTML_LAYOUT_H

struct box;
struct html_content;
struct gui_layout_table;

/**
 * Calculate positions of boxes in a document.
 *
 * \param content content of type CONTENT_HTML
 * \param width available width
 * \param height available height
 * \return true on success, false on memory exhaustion
 */
bool layout_document(struct html_content *content, int width, int height);

#endif
