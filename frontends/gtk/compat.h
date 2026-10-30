/*
 * Copyright 2010 Rob Kendrick <rjek@netsurf-browser.org>
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
 * GTK helper functions (interface)
 */

#ifndef NETSURFY_GTK_COMPAT_H_
#define NETSURFY_GTK_COMPAT_H_

#include <stdint.h>

#include <gtk/gtk.h>

#define NSGTK_STOCK_ADD "list-add"
#define NSGTK_STOCK_CANCEL "_Cancel"
#define NSGTK_STOCK_CLEAR "edit-clear"
#define NSGTK_STOCK_CLOSE "window-close"
#define NSGTK_STOCK_HOME "go-home"
#define NSGTK_STOCK_INFO "dialog-information"
#define NSGTK_STOCK_REFRESH "view-refresh"
#define NSGTK_STOCK_SAVE "document-save"
#define NSGTK_STOCK_SAVE_AS "document-save-as"
#define NSGTK_STOCK_STOP "process-stop"
#define NSGTK_STOCK_OK "_OK"
#define NSGTK_STOCK_OPEN "_Open"
#define NSGTK_STOCK_OPEN_MENU "open-menu"

#define GDK_KEY(symbol) GDK_KEY_##symbol

/**
 * Create a texture from the pixels of a pixbuf.
 */
GdkTexture *nsgtk_texture_new_for_pixbuf(GdkPixbuf *pixbuf);

void nsgtk_widget_set_alignment(GtkWidget *widget,
				GtkAlign halign,
				GtkAlign valign);

void nsgtk_widget_set_margins(GtkWidget *widget, gint hmargin, gint vmargin);

/**
 * Get the size of a widget as an allocation with a zero origin.
 */
void nsgtk_widget_get_allocation(GtkWidget *widget, GtkAllocation *allocation);

/**
 * Set the background colour of a widget with a CSS provider.
 *
 * The colour components are 16 bit values as in GdkRGBA times 65535.
 */
void nsgtk_widget_override_background_color(GtkWidget *widget,
					    uint16_t a,
					    uint16_t r,
					    uint16_t g,
					    uint16_t b);

/**
 * Create an image widget showing a named icon.
 */
GtkWidget *nsgtk_image_new_from_icon_name(const gchar *icon_name);

/**
 * Create an image widget showing a pixbuf.
 */
GtkWidget *nsgtk_image_new_from_pixbuf(GdkPixbuf *pixbuf);

/**
 * Set the image shown by an image widget to a pixbuf.
 */
void nsgtk_image_set_from_pixbuf(GtkImage *image, GdkPixbuf *pixbuf);

/**
 * Set an entry icon from a pixbuf.
 */
void nsgtk_entry_set_icon_from_pixbuf(GtkWidget *entry,
				      GtkEntryIconPosition icon_pos,
				      GdkPixbuf *pixbuf);

/**
 * Set the content of a button according to a toolbar button type option.
 *
 * \param button The button to change
 * \param icon_name The name of the icon of the button
 * \param label The label of the button
 * \param button_type 1 for small icons, 2 for large icons, 3 for large
 *                    icons with text and 4 for text only
 */
void nsgtk_button_set_style(GtkButton *button,
			    const char *icon_name,
			    const char *label,
			    int button_type);

/**
 * Open a URI with the default handler for its scheme.
 */
void nsgtk_show_uri(GtkWindow *parent, const gchar *uri);

/**
 * Add an action which calls a menu entry handler to an action map.
 *
 * \param map The action map to add the action to
 * \param name The name of the action
 * \param handler The handler, a function taking a widget and the data
 * \param data The data passed to the handler
 */
void nsgtk_action_map_add_menu_action(GActionMap *map,
				      const char *name,
				      GCallback handler,
				      gpointer data);

/**
 * Ask the user for a file name to save to.
 *
 * The dialog is not modal for the application; the callback is called
 * with the chosen file name if the user accepts the dialog.
 *
 * \param parent The window the dialog belongs to
 * \param title The title of the dialog
 * \param folder The initial folder or NULL
 * \param name The initial file name or NULL
 * \param callback The function called with the chosen file name
 * \param pw The context passed to the callback
 */
void nsgtk_save_file_dialog(GtkWindow *parent,
			    const char *title,
			    const char *folder,
			    const char *name,
			    void (*callback)(const char *filename, void *pw),
			    void *pw);

/**
 * Ask the user for a file to open.
 *
 * \param parent The window the dialog belongs to
 * \param title The title of the dialog
 * \param folder The initial folder or NULL
 * \param callback The function called with the chosen file name
 * \param pw The context passed to the callback
 */
void nsgtk_open_file_dialog(GtkWindow *parent,
			    const char *title,
			    const char *folder,
			    void (*callback)(const char *filename, void *pw),
			    void *pw);

/**
 * Ask the user a question and wait for the answer.
 *
 * \param parent The window the question belongs to
 * \param message The question
 * \param detail Further information or NULL
 * \param buttons NULL terminated array of button labels
 * \param cancel_button The button chosen if the dialog is dismissed
 * \param default_button The button activated by the return key
 * \return The index of the chosen button
 */
int nsgtk_alert_choose(GtkWindow *parent,
		       const char *message,
		       const char *detail,
		       const char *const *buttons,
		       int cancel_button,
		       int default_button);

/**
 * Ask the user for a file name to save to and wait for the answer.
 *
 * \param parent The window the dialog belongs to
 * \param title The title of the dialog
 * \param folder The initial folder or NULL
 * \param name The initial file name or NULL
 * \return The chosen file name to be freed with g_free or NULL
 */
char *nsgtk_save_file_choose(GtkWindow *parent,
			     const char *title,
			     const char *folder,
			     const char *name);

guint nsgtk_builder_add_from_resource(GtkBuilder *builder,
				      const gchar *resource_path,
				      GError **error);

#endif /* NETSURFY_GTK_COMPAT_H */
