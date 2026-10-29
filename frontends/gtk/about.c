/*
 * Copyright 2014 Vincent Sanders <vince@netsurf-browser.org>
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

/**
 * \file gtk/about.c
 *
 * Implementation of gtk about dialog.
 */

#include <stdlib.h>
#include <stdint.h>

#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "netsurf/browser_window.h"
#include "desktop/version.h"

#include "gtk/warn.h"
#include "gtk/compat.h"
#include "gtk/resources.h"
#include "gtk/about.h"

/**
 * Open a url and a browser window/tab
 *
 * \param url_text The text of the url to open
 */
static void about_open(const char *url_text)
{
	nsurl *url;
	nserror ret;
	enum browser_window_create_flags flags = BW_CREATE_HISTORY;

	if (nsoption_bool(show_single_tab) == true) {
		flags |= BW_CREATE_TAB;
	}

	ret = nsurl_create(url_text, &url);
	if (ret == NSERROR_OK) {
		ret = browser_window_create(flags, url, NULL, NULL, NULL);
		nsurl_unref(url);
	}

	if (ret != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(ret), 0);
	}
}

static void nsgtk_about_licence_clicked(GtkButton *button, gpointer data)
{
	about_open("about:licence");
	gtk_window_destroy(GTK_WINDOW(data));
}

static void nsgtk_about_credits_clicked(GtkButton *button, gpointer data)
{
	about_open("about:credits");
	gtk_window_destroy(GTK_WINDOW(data));
}

static void nsgtk_about_close_clicked(GtkButton *button, gpointer data)
{
	gtk_window_destroy(GTK_WINDOW(data));
}

static GtkWidget *about_add_button(GtkWidget *box,
				   GtkWidget *dialog,
				   const char *label,
				   GCallback callback)
{
	GtkWidget *button = gtk_button_new_with_label(label);

	g_signal_connect(button, "clicked", callback, dialog);
	gtk_box_append(GTK_BOX(box), button);

	return button;
}

void nsgtk_about_dialog_init(GtkWindow *parent)
{
	GtkWidget *dialog, *vbox, *buttons, *label, *close;
	gchar *name_string;
	GdkPixbuf *pixbuf;

	/* Create the dialog */
	dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "About NetSurf");
	gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	nsgtk_widget_set_margins(vbox, 12, 12);
	gtk_window_set_child(GTK_WINDOW(dialog), vbox);

	/* NetSurf icon */
	if (nsgdk_pixbuf_new_from_resname("netsurf.xpm", &pixbuf) ==
	    NSERROR_OK) {
		GtkWidget *image;

		image = nsgtk_image_new_from_pixbuf(pixbuf);
		gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
		g_object_unref(pixbuf);

		gtk_box_append(GTK_BOX(vbox), image);
	}

	/* version string */
	label = gtk_label_new(NULL);
	name_string = g_markup_printf_escaped(
		"<span size=\"xx-large\" weight=\"bold\">NetSurf %s</span>",
		netsurf_version);
	gtk_label_set_markup(GTK_LABEL(label), name_string);
	g_free(name_string);
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_box_append(GTK_BOX(vbox), label);

	label = gtk_label_new(messages_get("AboutDesc"));
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_box_append(GTK_BOX(vbox), label);

	label = gtk_label_new(messages_get("NetSurfCopyright"));
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_box_append(GTK_BOX(vbox), label);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_widget_set_margin_top(buttons, 6);
	about_add_button(buttons,
			 dialog,
			 "Licence",
			 G_CALLBACK(nsgtk_about_licence_clicked));
	about_add_button(buttons,
			 dialog,
			 "Credits",
			 G_CALLBACK(nsgtk_about_credits_clicked));
	close = about_add_button(buttons,
				 dialog,
				 "Close",
				 G_CALLBACK(nsgtk_about_close_clicked));
	gtk_box_append(GTK_BOX(vbox), buttons);

	gtk_window_set_default_widget(GTK_WINDOW(dialog), close);
	gtk_window_set_focus(GTK_WINDOW(dialog), close);

	gtk_window_present(GTK_WINDOW(dialog));
}
