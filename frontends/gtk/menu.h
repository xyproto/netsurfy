/*
 * Copyright 2009 Mark Benjamin <netsurf-browser.org.MarkBenjamin@dfgh.net>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETSURF_GTK_MENU_H_
#define NETSURF_GTK_MENU_H_

#include <stdbool.h>
#include <gtk/gtk.h>

/**
 * Create the model for the menu bar.
 *
 * The menu items activate actions in the "win" action group.
 */
GMenuModel *nsgtk_menu_bar_model(void);

/**
 * Create the model for the burger menu.
 */
GMenuModel *nsgtk_burger_menu_model(void);

/**
 * Create the model for the context menu of a browser window.
 *
 * \param nav include the navigation entries
 * \param cnp include the cut, copy and paste entries
 */
GMenuModel *nsgtk_popup_menu_model(bool nav, bool cnp);

/**
 * Create the model for the context menu of a link.
 */
GMenuModel *nsgtk_link_menu_model(void);

/**
 * Add the keyboard shortcuts of the menu entries to a window.
 */
void nsgtk_menu_add_shortcuts(GtkWidget *window);

#endif
