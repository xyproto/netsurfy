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
 * GTK helper functions (implementation)
 */

#include <stdint.h>
#include <stdbool.h>

#include "gtk/compat.h"

GdkTexture *nsgtk_texture_new_for_pixbuf(GdkPixbuf *pixbuf)
{
	GBytes *bytes;
	GdkTexture *texture;
	int height = gdk_pixbuf_get_height(pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	int width = gdk_pixbuf_get_width(pixbuf);
	gboolean alpha = gdk_pixbuf_get_has_alpha(pixbuf);
	gsize size;

	/* the last row of a pixbuf may be shorter than the rowstride */
	size = (gsize)(height - 1) * rowstride +
	       width * ((gdk_pixbuf_get_n_channels(pixbuf) *
				 gdk_pixbuf_get_bits_per_sample(pixbuf) +
			 7) /
			8);
	bytes = g_bytes_new_with_free_func(gdk_pixbuf_read_pixels(pixbuf),
					   size,
					   g_object_unref,
					   g_object_ref(pixbuf));
	texture = gdk_memory_texture_new(width,
					 height,
					 alpha ? GDK_MEMORY_R8G8B8A8
					       : GDK_MEMORY_R8G8B8,
					 bytes,
					 rowstride);
	g_bytes_unref(bytes);
	return texture;
}

void nsgtk_widget_set_alignment(GtkWidget *widget,
				GtkAlign halign,
				GtkAlign valign)
{
	gtk_widget_set_halign(widget, halign);
	gtk_widget_set_valign(widget, valign);
}

void nsgtk_widget_set_margins(GtkWidget *widget, gint hmargin, gint vmargin)
{
	gtk_widget_set_margin_start(widget, hmargin);
	gtk_widget_set_margin_end(widget, hmargin);
	gtk_widget_set_margin_top(widget, vmargin);
	gtk_widget_set_margin_bottom(widget, vmargin);
}

void nsgtk_widget_get_allocation(GtkWidget *widget, GtkAllocation *allocation)
{
	allocation->x = 0;
	allocation->y = 0;
	allocation->width = gtk_widget_get_width(widget);
	allocation->height = gtk_widget_get_height(widget);
}

void nsgtk_widget_override_background_color(GtkWidget *widget,
					    uint16_t a,
					    uint16_t r,
					    uint16_t g,
					    uint16_t b)
{
	GtkCssProvider *provider;
	gchar *css;

	provider = g_object_get_data(G_OBJECT(widget), "nsgtk-background");
	if (provider == NULL) {
		provider = gtk_css_provider_new();
		gtk_widget_add_css_class(widget, "nsgtk-background");
		gtk_style_context_add_provider_for_display(
			gtk_widget_get_display(widget),
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data_full(G_OBJECT(widget),
				       "nsgtk-background",
				       provider,
				       g_object_unref);
	}
	css = g_strdup_printf(
		".nsgtk-background { background-color: rgba(%d, %d, %d, %f); }",
		r >> 8,
		g >> 8,
		b >> 8,
		a / 65535.0);
	gtk_css_provider_load_from_string(provider, css);
	g_free(css);
}

GtkWidget *nsgtk_image_new_from_icon_name(const gchar *icon_name)
{
	return gtk_image_new_from_icon_name(icon_name);
}

GtkWidget *nsgtk_image_new_from_pixbuf(GdkPixbuf *pixbuf)
{
	GdkTexture *texture;
	GtkWidget *image;

	texture = nsgtk_texture_new_for_pixbuf(pixbuf);
	image = gtk_image_new_from_paintable(GDK_PAINTABLE(texture));
	g_object_unref(texture);
	return image;
}

void nsgtk_image_set_from_pixbuf(GtkImage *image, GdkPixbuf *pixbuf)
{
	GdkTexture *texture;

	if (pixbuf == NULL) {
		gtk_image_clear(image);
		return;
	}
	texture = nsgtk_texture_new_for_pixbuf(pixbuf);
	gtk_image_set_from_paintable(image, GDK_PAINTABLE(texture));
	g_object_unref(texture);
}

void nsgtk_entry_set_icon_from_pixbuf(GtkWidget *entry,
				      GtkEntryIconPosition icon_pos,
				      GdkPixbuf *pixbuf)
{
	GdkTexture *texture;

	if (pixbuf == NULL) {
		gtk_entry_set_icon_from_paintable(GTK_ENTRY(entry),
						  icon_pos,
						  NULL);
		return;
	}
	texture = nsgtk_texture_new_for_pixbuf(pixbuf);
	gtk_entry_set_icon_from_paintable(GTK_ENTRY(entry),
					  icon_pos,
					  GDK_PAINTABLE(texture));
	g_object_unref(texture);
}

void nsgtk_button_set_style(GtkButton *button,
			    const char *icon_name,
			    const char *label,
			    int button_type)
{
	GtkWidget *box;
	GtkWidget *image;

	switch (button_type) {
	case 1: /* Small icons */
		image = gtk_image_new_from_icon_name(icon_name);
		gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_NORMAL);
		gtk_button_set_child(button, image);
		break;

	case 3: /* Large icons with text */
		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		image = gtk_image_new_from_icon_name(icon_name);
		gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_LARGE);
		gtk_box_append(GTK_BOX(box), image);
		gtk_box_append(GTK_BOX(box),
			       gtk_label_new_with_mnemonic(label));
		gtk_button_set_child(button, box);
		break;

	case 4: /* Text icons only */
		gtk_button_set_child(button,
				     gtk_label_new_with_mnemonic(label));
		break;

	case 2: /* Large icons */
	default:
		image = gtk_image_new_from_icon_name(icon_name);
		gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_LARGE);
		gtk_button_set_child(button, image);
		break;
	}
}

void nsgtk_show_uri(GtkWindow *parent, const gchar *uri)
{
	GtkUriLauncher *launcher;

	launcher = gtk_uri_launcher_new(uri);
	gtk_uri_launcher_launch(launcher, parent, NULL, NULL, NULL);
	g_object_unref(launcher);
}

/**
 * Context of an action calling a menu entry handler.
 */
struct nsgtk_menu_action {
	gboolean (*handler)(GtkWidget *widget, gpointer data);
	gpointer data;
};

static void nsgtk_menu_action_activate(GSimpleAction *action,
				       GVariant *parameter,
				       gpointer data)
{
	struct nsgtk_menu_action *ma = data;

	ma->handler(NULL, ma->data);
}

static void nsgtk_menu_action_free(gpointer data, GClosure *closure)
{
	g_free(data);
}

void nsgtk_action_map_add_menu_action(GActionMap *map,
				      const char *name,
				      GCallback handler,
				      gpointer data)
{
	GSimpleAction *action;
	struct nsgtk_menu_action *ma;

	ma = g_new(struct nsgtk_menu_action, 1);
	ma->handler = (gboolean (*)(GtkWidget *, gpointer))(
		void (*)(void))handler;
	ma->data = data;

	action = g_simple_action_new(name, NULL);
	g_signal_connect_data(action,
			      "activate",
			      G_CALLBACK(nsgtk_menu_action_activate),
			      ma,
			      nsgtk_menu_action_free,
			      0);
	g_action_map_add_action(map, G_ACTION(action));
	g_object_unref(action);
}

/**
 * Context of a file dialog.
 */
struct nsgtk_file_dialog {
	void (*callback)(const char *filename, void *pw);
	void *pw;
	bool save;
};

static void
nsgtk_file_dialog_done(GObject *source, GAsyncResult *result, gpointer data)
{
	struct nsgtk_file_dialog *fd = data;
	GFile *file;

	if (fd->save) {
		file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source),
						   result,
						   NULL);
	} else {
		file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),
						   result,
						   NULL);
	}
	if (file != NULL) {
		char *filename = g_file_get_path(file);

		if (filename != NULL) {
			fd->callback(filename, fd->pw);
			g_free(filename);
		}
		g_object_unref(file);
	}
	g_free(fd);
}

static GtkFileDialog *
nsgtk_file_dialog_new(const char *title, const char *folder)
{
	GtkFileDialog *dialog;

	dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, title);
	if (folder != NULL) {
		GFile *dir = g_file_new_for_path(folder);
		gtk_file_dialog_set_initial_folder(dialog, dir);
		g_object_unref(dir);
	}
	return dialog;
}

void nsgtk_save_file_dialog(GtkWindow *parent,
			    const char *title,
			    const char *folder,
			    const char *name,
			    void (*callback)(const char *filename, void *pw),
			    void *pw)
{
	GtkFileDialog *dialog;
	struct nsgtk_file_dialog *fd;

	fd = g_new(struct nsgtk_file_dialog, 1);
	fd->callback = callback;
	fd->pw = pw;
	fd->save = true;

	dialog = nsgtk_file_dialog_new(title, folder);
	if (name != NULL) {
		gtk_file_dialog_set_initial_name(dialog, name);
	}
	gtk_file_dialog_save(dialog, parent, NULL, nsgtk_file_dialog_done, fd);
	g_object_unref(dialog);
}

void nsgtk_open_file_dialog(GtkWindow *parent,
			    const char *title,
			    const char *folder,
			    void (*callback)(const char *filename, void *pw),
			    void *pw)
{
	GtkFileDialog *dialog;
	struct nsgtk_file_dialog *fd;

	fd = g_new(struct nsgtk_file_dialog, 1);
	fd->callback = callback;
	fd->pw = pw;
	fd->save = false;

	dialog = nsgtk_file_dialog_new(title, folder);
	gtk_file_dialog_open(dialog, parent, NULL, nsgtk_file_dialog_done, fd);
	g_object_unref(dialog);
}

/**
 * Result of a dialog waited for.
 */
struct nsgtk_wait {
	bool done;
	int button;
	char *filename;
};

static void
nsgtk_alert_done(GObject *source, GAsyncResult *result, gpointer data)
{
	struct nsgtk_wait *wait = data;

	wait->button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),
						      result,
						      NULL);
	wait->done = true;
}

int nsgtk_alert_choose(GtkWindow *parent,
		       const char *message,
		       const char *detail,
		       const char *const *buttons,
		       int cancel_button,
		       int default_button)
{
	GtkAlertDialog *dialog;
	struct nsgtk_wait wait = {false, cancel_button, NULL};

	dialog = gtk_alert_dialog_new("%s", message);
	if (detail != NULL) {
		gtk_alert_dialog_set_detail(dialog, detail);
	}
	gtk_alert_dialog_set_buttons(dialog, buttons);
	gtk_alert_dialog_set_cancel_button(dialog, cancel_button);
	gtk_alert_dialog_set_default_button(dialog, default_button);
	gtk_alert_dialog_choose(dialog, parent, NULL, nsgtk_alert_done, &wait);
	while (!wait.done) {
		g_main_context_iteration(NULL, TRUE);
	}
	g_object_unref(dialog);

	if (wait.button < 0) {
		return cancel_button;
	}
	return wait.button;
}

static void
nsgtk_save_done(GObject *source, GAsyncResult *result, gpointer data)
{
	struct nsgtk_wait *wait = data;
	GFile *file;

	file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source),
					   result,
					   NULL);
	if (file != NULL) {
		wait->filename = g_file_get_path(file);
		g_object_unref(file);
	}
	wait->done = true;
}

char *nsgtk_save_file_choose(GtkWindow *parent,
			     const char *title,
			     const char *folder,
			     const char *name)
{
	GtkFileDialog *dialog;
	struct nsgtk_wait wait = {false, 0, NULL};

	dialog = nsgtk_file_dialog_new(title, folder);
	if (name != NULL) {
		gtk_file_dialog_set_initial_name(dialog, name);
	}
	gtk_file_dialog_save(dialog, parent, NULL, nsgtk_save_done, &wait);
	while (!wait.done) {
		g_main_context_iteration(NULL, TRUE);
	}
	g_object_unref(dialog);

	return wait.filename;
}

guint nsgtk_builder_add_from_resource(GtkBuilder *builder,
				      const gchar *resource_path,
				      GError **error)
{
	return gtk_builder_add_from_resource(builder, resource_path, error);
}
