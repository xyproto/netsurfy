/*
 * Copyright 2019 Vincent Sanders <vince@netsurf-browser.org>
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
 * \file
 * implementation of toolbar to control browsing context
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <gtk/gtk.h>

#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/file.h"
#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "desktop/browser_history.h"
#include "desktop/searchweb.h"
#include "desktop/search.h"
#include "desktop/save_complete.h"
#include "desktop/save_text.h"
#include "desktop/print.h"
#include "desktop/hotlist.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/keypress.h"

#include "gtk/toolbar_items.h"
#include "gtk/completion.h"
#include "gtk/gui.h"
#include "gtk/warn.h"
#include "gtk/search.h"
#include "gtk/throbber.h"
#include "gtk/scaffolding.h"
#include "gtk/window.h"
#include "gtk/compat.h"
#include "gtk/resources.h"
#include "gtk/schedule.h"
#include "gtk/local_history.h"
#include "gtk/global_history.h"
#include "gtk/viewsource.h"
#include "gtk/download.h"
#include "gtk/viewdata.h"
#include "gtk/tabs.h"
#include "gtk/print.h"
#include "gtk/layout_pango.h"
#include "gtk/preferences.h"
#include "gtk/hotlist.h"
#include "gtk/cookies.h"
#include "gtk/about.h"
#include "gtk/gdk.h"
#include "gtk/bitmap.h"
#include "gtk/page_info.h"
#include "gtk/toolbar.h"

/**
 * button location indicating button is not to be shown
 */
#define INACTIVE_LOCATION (-1)

/**
 * time (in ms) between throbber animation frame updates
 */
#define THROBBER_FRAME_TIME (100)

/**
 * the minimum number of columns in the tool store
 */
#define NSGTK_MIN_STORE_COLUMNS 4

/**
 * the 'standard' width of a button that makes sufficient of its label visible
 */
#define NSGTK_BUTTON_WIDTH 120

/**
 * the 'standard' height of a button that fits as many toolbars as
 *   possible into the store
 */
#define NSGTK_BUTTON_HEIGHT 70

/**
 * the 'normal' width of the websearch bar
 */
#define NSGTK_WEBSEARCH_WIDTH 150

/**
 * toolbar item context
 */
struct nsgtk_toolbar_item {

	/**
	 * GTK widget in the toolbar
	 */
	GtkWidget *button;

	/**
	 * location index in toolbar
	 */
	int location;

	/**
	 * if the item is currently sensitive in the toolbar
	 */
	bool sensitivity;

	/**
	 * textural name used in serialising items
	 */
	const char *name;

	/**
	 * button clicked on toolbar handler
	 */
	gboolean (*clicked)(GtkWidget *widget, gpointer data);
};

/**
 * Location focus state machine
 *
 * 1. If we don't care, we're in LFS_IDLE
 * 2. When we create a new toolbar, we can put it into
 *    LFS_WANT which means that we want the url bar to focus
 * 3. When we start throbbing if we're in LFS_WANT we move to LFS_THROB
 * 4. When we stop throbbing, if we're in LFS_THROB we move to LFS_LAST
 *
 * While not in LFS_IDLE, if the url bar is updated and we previously had it
 * fully selected then we reselect it all.  If we're in LFS_LAST we move to
 * LFS_IDLE at that point.
 */
typedef enum {
	LFS_IDLE, /**< Nothing to do */
	LFS_WANT, /**< Want focus, will apply */
	LFS_THROB, /**< Want focus, we have started throbbing */
	LFS_LAST, /**< Last chance for a focus update */
} nsgtk_toolbar_location_focus_state;

/**
 * control toolbar context
 */
struct nsgtk_toolbar {
	/** gtk toolbar widget */
	GtkWidget *widget;

	/**
	 * Toolbar item contexts
	 */
	struct nsgtk_toolbar_item items[PLACEHOLDER_BUTTON];

	/**
	 * Current frame of throbber animation
	 */
	int throb_frame;

	/**
	 * Web search widget
	 */
	GtkWidget *webSearchEntry;

	/**
	 * callback to obtain a browser window for navigation
	 */
	struct browser_window *(*get_bw)(void *ctx);

	/**
	 * context passed to get_bw function
	 */
	void *get_ctx;

	/**
	 * Location focus state machine, current state
	 */
	nsgtk_toolbar_location_focus_state loc_focus;
};


/**
 * toolbar cusomisation context
 */
struct nsgtk_toolbar_customisation {
	/**
	 * first entry is a toolbar widget so a customisation widget
	 *   can be cast to toolbar and back.
	 */
	struct nsgtk_toolbar toolbar;

	/**
	 * The top level container (tabBox)
	 */
	GtkWidget *container;

	/**
	 * The vertical box into which the available tools are shown
	 */
	GtkBox *toolbox;

	/**
	 * widget handles for items in the customisation toolbox area
	 */
	GtkWidget *items[PLACEHOLDER_BUTTON];
};


/* forward declaration */
static nserror toolbar_item_create(nsgtk_toolbar_button id,
				   struct nsgtk_toolbar_item *item_out);


/**
 * returns a string without its underscores
 *
 * \param s The string to change.
 * \param replacespace true to insert a space where there was an underscore
 * \return The altered string
 */
static char *remove_underscores(const char *s, bool replacespace)
{
	size_t i, ii, len;
	char *ret;
	len = strlen(s);
	ret = malloc(len + 1);
	if (ret == NULL) {
		return NULL;
	}
	for (i = 0, ii = 0; i < len; i++) {
		if (s[i] != '_') {
			ret[ii++] = s[i];
		} else if (replacespace) {
			ret[ii++] = ' ';
		}
	}
	ret[ii] = '\0';
	return ret;
}


/**
 * set the icon and label of a toolbar button and show them according to
 *   the button type option.
 *
 * \param button The button to change
 * \param iconname The name of the icon
 * \param labelmsg The message key of the label
 * \param button_type The button type, see nsgtk_button_set_style()
 */
static void set_button_content(GtkWidget *button,
			       const char *iconname,
			       const char *labelmsg,
			       int button_type)
{
	char *label;

	g_object_set_data_full(
		G_OBJECT(button), "nsgtk-icon", g_strdup(iconname), g_free);
	g_object_set_data_full(
		G_OBJECT(button), "nsgtk-label", g_strdup(labelmsg), g_free);

	label = remove_underscores(messages_get(labelmsg), false);
	nsgtk_button_set_style(GTK_BUTTON(button),
			       iconname,
			       label != NULL ? label : labelmsg,
			       button_type);
	gtk_widget_set_tooltip_text(button, label);
	free(label);
}


/**
 * create a toolbar button widget
 *
 * \param iconname The name of the icon
 * \param labelmsg The message key of the label
 * \param sensitivity if the button should be sensitive to input
 * \param edit true if the button is for the customisation toolbox
 */
static GtkWidget *make_button(const char *iconname,
			      const char *labelmsg,
			      bool sensitivity,
			      bool edit)
{
	GtkWidget *button;

	button = gtk_button_new();
	gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
	gtk_widget_set_focus_on_click(button, FALSE);
	set_button_content(button,
			   iconname,
			   labelmsg,
			   edit ? 3 : nsoption_int(button_type));
	gtk_widget_set_sensitive(button, sensitivity);

	return button;
}


/**
 * create throbber toolbar item widget
 */
static GtkWidget *make_toolbar_item_throbber(bool sensitivity, bool edit)
{
	nserror res;
	GtkWidget *item;
	GdkPixbuf *pixbuf;

	res = nsgtk_throbber_get_frame(0, &pixbuf);
	if (res != NSERROR_OK) {
		return NULL;
	}

	if (edit) {
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

		item = gtk_button_new();
		gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
		gtk_box_append(GTK_BOX(box),
			       nsgtk_image_new_from_pixbuf(pixbuf));
		gtk_box_append(GTK_BOX(box),
			       gtk_label_new(messages_get("ToolThrob")));
		gtk_button_set_child(GTK_BUTTON(item), box);
	} else {
		item = nsgtk_image_new_from_pixbuf(pixbuf);
		gtk_image_set_pixel_size(GTK_IMAGE(item),
					 gdk_pixbuf_get_height(pixbuf));
		nsgtk_widget_set_alignment(item,
					   GTK_ALIGN_CENTER,
					   GTK_ALIGN_CENTER);
		nsgtk_widget_set_margins(item, 3, 0);
	}
	gtk_widget_set_sensitive(item, sensitivity);

	return item;
}


/**
 * create url bar toolbar item widget
 *
 * create a gtk entry widget with a completion attached
 *
 * \param sensitivity if the entry should be created sensitive to input
 * \param edit if the entry should be editable
 */
static GtkWidget *make_toolbar_item_url_bar(bool sensitivity, bool edit)
{
	GtkWidget *entry;
	GtkEntryCompletion *completion;

	if (edit) {
		return make_button("page-info-internal", "URL", true, true);
	}

	entry = gtk_entry_new();
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry),
					  GTK_ENTRY_ICON_PRIMARY,
					  "page-info-internal");

	completion = gtk_entry_completion_new();
	if (completion != NULL) {
		gtk_entry_set_completion(GTK_ENTRY(entry), completion);
		g_object_unref(completion);
	}

	gtk_widget_set_hexpand(entry, TRUE);
	gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
	gtk_widget_set_sensitive(entry, sensitivity);

	return entry;
}


/**
 * create web search toolbar item widget
 */
static GtkWidget *make_toolbar_item_websearch(bool sensitivity, bool edit)
{
	nserror res;
	GtkWidget *entry;
	struct bitmap *bitmap;
	GdkPixbuf *pixbuf = NULL;

	if (edit) {
		return make_button(NSGTK_STOCK_INFO, "Web Search", true, true);
	}

	res = search_web_get_provider_bitmap(&bitmap);
	if ((res == NSERROR_OK) && (bitmap != NULL)) {
		pixbuf = nsgdk_pixbuf_get_from_surface(bitmap->surface, 32, 32);
	}

	entry = gtk_entry_new();

	if (pixbuf != NULL) {
		nsgtk_entry_set_icon_from_pixbuf(entry,
						 GTK_ENTRY_ICON_PRIMARY,
						 pixbuf);
		g_object_unref(pixbuf);
	} else {
		gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry),
						  GTK_ENTRY_ICON_PRIMARY,
						  NSGTK_STOCK_INFO);
	}

	gtk_widget_set_size_request(entry, NSGTK_WEBSEARCH_WIDTH, -1);
	gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
	gtk_widget_set_sensitive(entry, sensitivity);

	return entry;
}


/**
 * create local history toolbar item widget
 */
static GtkWidget *make_toolbar_item_history(bool sensitivity, bool edit)
{
	GtkWidget *item;

	if (edit) {
		return make_button(
			"local-history", "gtkLocalHistory", sensitivity, true);
	}

	item = gtk_button_new_from_icon_name("local-history");
	gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
	gtk_widget_set_focus_on_click(item, FALSE);
	gtk_widget_set_tooltip_text(item, messages_get("gtkLocalHistory"));
	gtk_widget_set_sensitive(item, sensitivity);

	return item;
}


/**
 * widget factory for creation of toolbar item widgets
 *
 * \param i the id of the widget
 * \param theme the theme to make the widgets from
 * \return gtk widget
 */
static GtkWidget *
make_toolbar_item(nsgtk_toolbar_button itemid, bool sensitivity)
{
	GtkWidget *toolitem = NULL;

	switch (itemid) {
#define TOOLBAR_ITEM_y(identifier, label, iconame)
#define TOOLBAR_ITEM_n(identifier, label, iconame)
#define TOOLBAR_ITEM_t(identifier, label, iconame)                             \
	case identifier:                                                       \
		toolitem = make_button(iconame, #label, sensitivity, false);   \
		break;
#define TOOLBAR_ITEM_b(identifier, label, iconame)                             \
	case identifier:                                                       \
		toolitem = make_button(iconame, #label, sensitivity, false);   \
		break;
#define TOOLBAR_ITEM(                                                          \
	identifier, name, snstvty, clicked, activate, label, iconame)          \
	TOOLBAR_ITEM_##clicked(identifier, label, iconame)

#include "gtk/toolbar_items.h"

#undef TOOLBAR_ITEM_t
#undef TOOLBAR_ITEM_b
#undef TOOLBAR_ITEM_n
#undef TOOLBAR_ITEM_y
#undef TOOLBAR_ITEM

	case HISTORY_BUTTON:
		toolitem = make_toolbar_item_history(sensitivity, false);
		break;

	case URL_BAR_ITEM:
		toolitem = make_toolbar_item_url_bar(sensitivity, false);
		break;

	case THROBBER_ITEM:
		toolitem = make_toolbar_item_throbber(sensitivity, false);
		break;

	case WEBSEARCH_ITEM:
		toolitem = make_toolbar_item_websearch(sensitivity, false);
		break;

	default:
		break;
	}
	return toolitem;
}


/**
 * widget factory for creation of toolbar item widgets for the toolbox
 *
 * \param itemid the id of the widget
 * \return gtk widget
 */
static GtkWidget *make_toolbox_item(nsgtk_toolbar_button itemid, bool bar)
{
	GtkWidget *toolitem = NULL;

	switch (itemid) {
#define TOOLBAR_ITEM_y(identifier, label, iconame)
#define TOOLBAR_ITEM_n(identifier, label, iconame)
#define TOOLBAR_ITEM_t(identifier, label, iconame)                             \
	case identifier:                                                       \
		if (bar) {                                                     \
			toolitem = make_button(iconame, #label, true, true);   \
		}                                                              \
		break;
#define TOOLBAR_ITEM_b(identifier, label, iconame)                             \
	case identifier:                                                       \
		toolitem = make_button(iconame, #label, true, true);           \
		break;
#define TOOLBAR_ITEM(                                                          \
	identifier, name, snstvty, clicked, activate, label, iconame)          \
	TOOLBAR_ITEM_##clicked(identifier, label, iconame)

#include "gtk/toolbar_items.h"

#undef TOOLBAR_ITEM_t
#undef TOOLBAR_ITEM_b
#undef TOOLBAR_ITEM_n
#undef TOOLBAR_ITEM_y
#undef TOOLBAR_ITEM

	case HISTORY_BUTTON:
		toolitem = make_toolbar_item_history(true, true);
		break;

	case URL_BAR_ITEM:
		toolitem = make_toolbar_item_url_bar(false, true);
		break;

	case THROBBER_ITEM:
		toolitem = make_toolbar_item_throbber(true, true);
		break;

	case WEBSEARCH_ITEM:
		toolitem = make_toolbar_item_websearch(false, true);
		break;

	default:
		break;
	}
	return toolitem;
}


/**
 * find the toolbar item with a given location.
 *
 * \param tb the toolbar instance
 * \param locaction the location to search for
 * \return the item id for a location
 */
static nsgtk_toolbar_button
itemid_from_location(struct nsgtk_toolbar *tb, int location)
{
	int iidx;
	for (iidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
		if (tb->items[iidx].location == location) {
			break;
		}
	}
	return iidx;
}


/**
 * save toolbar settings to file
 */
static nserror nsgtk_toolbar_customisation_save(struct nsgtk_toolbar *tb)
{
	int iidx; /* item index */
	char *order; /* item ordering */
	char *start; /* start of next item name to be output */
	int orderlen = 0; /* length of item ordering */
	nsgtk_toolbar_button itemid;
	int location;
	char *choices = NULL;

	for (iidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
		if (tb->items[iidx].location != INACTIVE_LOCATION) {
			orderlen += strlen(tb->items[iidx].name);
			orderlen++; /* allow for separator */
		}
	}

	/* ensure there are some items to store */
	if (orderlen == 0) {
		return NSERROR_INVALID;
	}

	order = malloc(orderlen + 1);
	if (order == NULL) {
		return NSERROR_NOMEM;
	}

	start = order;

	for (location = BACK_BUTTON; location < PLACEHOLDER_BUTTON;
	     location++) {
		int written;
		itemid = itemid_from_location(tb, location);
		if (itemid == PLACEHOLDER_BUTTON) {
			/* no more filled locations */
			break;
		}
		written = snprintf(start,
				   orderlen + 1 - (start - order),
				   "%s/",
				   tb->items[itemid].name);
		if ((written < 0) || (written > orderlen - (start - order))) {
			free(order);
			return NSERROR_UNKNOWN;
		}
		start += written;

		if ((start - order) >= orderlen) {
			break;
		}
	}

	order[orderlen - 1] = 0;

	nsoption_set_charp(toolbar_items, order);

	/* ensure choices are saved */
	netsurf_mkpath(&choices, NULL, 2, nsgtk_config_home, "Choices");
	if (choices != NULL) {
		nsoption_write(choices, NULL, NULL);
		free(choices);
	}

	return NSERROR_OK;
}


/**
 * remove an item from a customisation toolbar
 *
 * \param tb The customisation toolbar
 * \param itemid The item to remove
 */
static void
customisation_toolbar_remove_item(struct nsgtk_toolbar *tb, int itemid)
{
	int location;
	int other;

	/* update the locations for all the subsequent toolbar items */
	for (location = tb->items[itemid].location;
	     location < PLACEHOLDER_BUTTON;
	     location++) {
		other = itemid_from_location(tb, location);
		if (other == PLACEHOLDER_BUTTON) {
			break;
		}
		tb->items[other].location--;
	}

	/* remove existing item */
	tb->items[itemid].location = INACTIVE_LOCATION;
	gtk_box_remove(GTK_BOX(tb->widget), tb->items[itemid].button);
}


/**
 * drag source prepare handler providing the dragged item
 *
 * The item id is offset by PLACEHOLDER_BUTTON for items dragged from the
 *   customisation toolbar instead of the toolbox.
 */
static GdkContentProvider *toolbar_item_drag_prepare_cb(GtkDragSource *source,
							double x,
							double y,
							gpointer data)
{
	return gdk_content_provider_new_typed(G_TYPE_INT,
					      GPOINTER_TO_INT(data));
}


/**
 * drag source begin handler setting the drag icon to the dragged widget
 */
static void
toolbar_item_drag_begin_cb(GtkDragSource *source, GdkDrag *drag, gpointer data)
{
	GtkWidget *widget;
	GdkPaintable *paintable;

	widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
	paintable = gtk_widget_paintable_new(widget);
	gtk_drag_source_set_icon(source, paintable, 0, 0);
	g_object_unref(paintable);
}


/**
 * make a widget a drag source for a toolbar item
 *
 * \param widget The widget to drag
 * \param value The value providing the item, see toolbar_item_drag_prepare_cb()
 */
static void toolbar_item_drag_source(GtkWidget *widget, int value)
{
	GtkDragSource *source;

	source = gtk_drag_source_new();
	gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
	g_signal_connect(source,
			 "prepare",
			 G_CALLBACK(toolbar_item_drag_prepare_cb),
			 GINT_TO_POINTER(value));
	g_signal_connect(source,
			 "drag-begin",
			 G_CALLBACK(toolbar_item_drag_begin_cb),
			 NULL);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(source));
}


/**
 * connect signals to a toolbar item in a customisation toolbar
 *
 * \param tb The toolbar
 * \param itemid The item id within to toolbar to connect
 * \param NSERROR_OK on success
 */
static nserror
toolbar_item_connect_signals(struct nsgtk_toolbar *tb, int itemid)
{
	/* set toolbar items to be a drag source */
	toolbar_item_drag_source(tb->items[itemid].button,
				 itemid + PLACEHOLDER_BUTTON);
	return NSERROR_OK;
}


/**
 * customisation container handler for drop signal
 *
 * called when a widget is dropped onto the store window
 */
static gboolean customisation_container_drop_cb(GtkDropTarget *target,
						const GValue *value,
						double x,
						double y,
						gpointer data)
{
	struct nsgtk_toolbar_customisation *tbc;
	int itemid;

	tbc = (struct nsgtk_toolbar_customisation *)data;
	itemid = g_value_get_int(value);

	/* only items dragged from the toolbar are removed from it */
	if (itemid < PLACEHOLDER_BUTTON) {
		return FALSE;
	}
	itemid -= PLACEHOLDER_BUTTON;

	if (tbc->toolbar.items[itemid].location != INACTIVE_LOCATION) {
		customisation_toolbar_remove_item(&tbc->toolbar, itemid);
	}

	return TRUE;
}


/**
 * customisation toolbar handler for drop signal
 *
 * called when a widget is dropped onto the toolbar
 */
static gboolean customisation_toolbar_drop_cb(GtkDropTarget *target,
					      const GValue *value,
					      double x,
					      double y,
					      gpointer data)
{
	struct nsgtk_toolbar_customisation *tbc;
	tbc = (struct nsgtk_toolbar_customisation *)data;
	gint position = 0; /* drop position in toolbar */
	int location;
	int itemid;
	int dragid;
	GtkWidget *child;
	GtkWidget *sibling = NULL;
	struct nsgtk_toolbar_item *dragitem; /* toolbar item being dragged */

	dragid = g_value_get_int(value);
	if (dragid >= PLACEHOLDER_BUTTON) {
		dragid -= PLACEHOLDER_BUTTON;
	}
	if ((dragid < BACK_BUTTON) || (dragid >= PLACEHOLDER_BUTTON)) {
		return FALSE;
	}

	/* the drop position is the number of items left of the pointer */
	for (child = gtk_widget_get_first_child(tbc->toolbar.widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling(child)) {
		graphene_rect_t bounds;

		if (gtk_widget_compute_bounds(
			    child, tbc->toolbar.widget, &bounds) &&
		    (x > bounds.origin.x + bounds.size.width / 2)) {
			position++;
		}
	}

	/* pure conveiance variable */
	dragitem = &tbc->toolbar.items[dragid];

	/* deal with replacing existing item in toolbar */
	if (dragitem->location != INACTIVE_LOCATION) {
		if (dragitem->location < position) {
			position--;
		}
		customisation_toolbar_remove_item(&tbc->toolbar, dragid);
	}

	dragitem->button = make_toolbox_item(dragid, true);

	if (dragitem->button == NULL) {
		nsgtk_warning("NoMemory", 0);
		return TRUE;
	}

	/* update locations */
	for (location = PLACEHOLDER_BUTTON; location >= position; location--) {
		itemid = itemid_from_location(&tbc->toolbar, location);
		if (itemid != PLACEHOLDER_BUTTON) {
			tbc->toolbar.items[itemid].location++;
		}
	}
	dragitem->location = position;

	/* insert after the widget before the drop position */
	for (child = gtk_widget_get_first_child(tbc->toolbar.widget);
	     (child != NULL) && (position > 0);
	     child = gtk_widget_get_next_sibling(child)) {
		sibling = child;
		position--;
	}
	gtk_box_insert_child_after(GTK_BOX(tbc->toolbar.widget),
				   dragitem->button,
				   sibling);

	toolbar_item_connect_signals(&tbc->toolbar, dragid);
	return TRUE;
}


/**
 * create a new browser window
 *
 * creates a browser window with default url depending on user choices.
 *
 * \param bw The browser window to pass for existing window/
 * \param intab true if the new window should be in a tab else false
 *                for new window.
 * \return NSERROR_OK on success else error code.
 */
static nserror
nsgtk_browser_window_create(struct browser_window *bw, bool intab)
{
	nserror res = NSERROR_OK;
	nsurl *url = NULL;
	int flags = BW_CREATE_HISTORY | BW_CREATE_FOREGROUND |
		    BW_CREATE_FOCUS_LOCATION;

	if (intab) {
		flags |= BW_CREATE_TAB;
	}

	if (!nsoption_bool(new_blank)) {
		const char *addr;
		if (nsoption_charp(homepage_url) != NULL) {
			addr = nsoption_charp(homepage_url);
		} else {
			addr = NETSURF_HOMEPAGE;
		}
		res = nsurl_create(addr, &url);
	}

	if (res == NSERROR_OK) {
		res = browser_window_create(flags, url, NULL, bw, NULL);
	}

	if (url != NULL) {
		nsurl_unref(url);
	}

	return res;
}


/**
 * Apply the user toolbar button settings from configuration
 *
 * GTK specific user option string is a set of fields arranged as
 * [itemreference];[itemlocation]|[itemreference];[itemlocation]| etc
 *
 * \param tb The toolbar to apply customisation to
 * \param NSERROR_OK on success else error code.
 */
static nserror apply_user_button_customisation(struct nsgtk_toolbar *tb)
{
	const char *tbitems; /* item order user config */
	const char *start;
	const char *end;
	int iidx; /* item index */
	int location = 0; /* location index */

	/* set all button locations to inactive */
	for (iidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
		tb->items[iidx].location = INACTIVE_LOCATION;
	}

	tbitems = nsoption_charp(toolbar_items);
	if (tbitems == NULL) {
		tbitems = "";
	}

	end = tbitems;
	while (*end != 0) {
		start = end;
		while ((*end != 0) && (*end != '/')) {
			end++;
		}

		for (iidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
			if (((ssize_t)strlen(tb->items[iidx].name) ==
			     (end - start)) &&
			    (strncmp(tb->items[iidx].name,
				     start,
				     end - start) == 0)) {
				tb->items[iidx].location = location++;
				break;
			}
		}

		if (*end == '/') {
			end++;
		}
	}

	if (location == 0) {
		/* completely failed to create any buttons so use defaults */
		tb->items[BACK_BUTTON].location = location++;
		tb->items[HISTORY_BUTTON].location = location++;
		tb->items[FORWARD_BUTTON].location = location++;
		tb->items[RELOADSTOP_BUTTON].location = location++;
		tb->items[URL_BAR_ITEM].location = location++;
		tb->items[WEBSEARCH_ITEM].location = location++;
		tb->items[OPENMENU_BUTTON].location = location++;
		tb->items[THROBBER_ITEM].location = location++;
	}


	return NSERROR_OK;
}


/**
 * remove all widgets from a toolbar
 */
static void toolbar_remove_all(GtkWidget *box)
{
	GtkWidget *child;

	while ((child = gtk_widget_get_first_child(box)) != NULL) {
		gtk_box_remove(GTK_BOX(box), child);
	}
}


/**
 * populates a toolbar with widgets in correct order
 *
 * \param tb toolbar
 * \return NSERROR_OK on success else error code.
 */
static nserror populate_gtk_toolbar_widget(struct nsgtk_toolbar *tb)
{
	int location; /* location index */
	int itemid;

	/* clear the toolbar container of all widgets */
	toolbar_remove_all(tb->widget);

	/* add widgets to toolbar */
	for (location = 0; location < PLACEHOLDER_BUTTON; location++) {
		itemid = itemid_from_location(tb, location);
		if (itemid == PLACEHOLDER_BUTTON) {
			break;
		}
		tb->items[itemid].button = make_toolbar_item(
			itemid, tb->items[itemid].sensitivity);
		if (tb->items[itemid].button != NULL) {
			gtk_box_append(GTK_BOX(tb->widget),
				       tb->items[itemid].button);
		}
	}

	gtk_widget_set_visible(tb->widget, TRUE);

	return NSERROR_OK;
}


/**
 * populates the customization toolbar with widgets in correct order
 *
 * \param tb toolbar
 * \return NSERROR_OK on success else error code.
 */
static nserror customisation_toolbar_populate(struct nsgtk_toolbar *tb)
{
	int location; /* location index */
	int itemid;

	/* clear the toolbar container of all widgets */
	toolbar_remove_all(tb->widget);

	/* add widgets to toolbar */
	for (location = 0; location < PLACEHOLDER_BUTTON; location++) {
		itemid = itemid_from_location(tb, location);
		if (itemid == PLACEHOLDER_BUTTON) {
			break;
		}
		tb->items[itemid].button = make_toolbox_item(itemid, true);
		if (tb->items[itemid].button != NULL) {
			gtk_box_append(GTK_BOX(tb->widget),
				       tb->items[itemid].button);
		}
	}

	gtk_widget_set_visible(tb->widget, TRUE);

	return NSERROR_OK;
}


/**
 * set a toolbar items sensitivity
 *
 * note this does not set menu items sensitivity
 */
static nserror
set_item_sensitivity(struct nsgtk_toolbar_item *item, bool sensitivity)
{
	if (item->sensitivity != sensitivity) {
		/* item requires sensitivity changing */
		item->sensitivity = sensitivity;

		if ((item->location != -1) && (item->button != NULL)) {
			gtk_widget_set_sensitive(GTK_WIDGET(item->button),
						 item->sensitivity);
		}
	}

	return NSERROR_OK;
}


/**
 * set an item to its alternative action
 *
 * this is currently only used for the stop/reload button where we
 *   also reuse the item sensitivity for the state indicator.
 *
 * \param tb the toolbar instance
 */
static nserror set_item_action(struct nsgtk_toolbar *tb, int itemid, bool alt)
{
	if (itemid != RELOADSTOP_BUTTON) {
		return NSERROR_INVALID;
	}
	if (tb->items[itemid].location == -1) {
		return NSERROR_OK;
	}
	tb->items[itemid].sensitivity = alt;

	if (tb->items[itemid].button == NULL) {
		return NSERROR_INVALID;
	}

	if (tb->items[itemid].sensitivity) {
		set_button_content(tb->items[itemid].button,
				   NSGTK_STOCK_REFRESH,
				   "Reload",
				   nsoption_int(button_type));
	} else {
		set_button_content(tb->items[itemid].button,
				   NSGTK_STOCK_STOP,
				   "gtkStop",
				   nsoption_int(button_type));
	}

	gtk_widget_set_sensitive(tb->items[itemid].button, TRUE);

	return NSERROR_OK;
}


/**
 * cause the toolbar browsing context to navigate to a new url.
 *
 * \param tb the toolbar context.
 * \param urltxt The url string.
 * \return NSERROR_OK on success else appropriate error code.
 */
static nserror
toolbar_navigate_to_url(struct nsgtk_toolbar *tb, const char *urltxt)
{
	struct browser_window *bw;
	nsurl *url;
	nserror res;

	res = nsurl_create(urltxt, &url);
	if (res != NSERROR_OK) {
		return res;
	}

	bw = tb->get_bw(tb->get_ctx);

	res = browser_window_navigate(
		bw, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
	nsurl_unref(url);

	return res;
}


/**
 * run a gtk file chooser as a save dialog to obtain a path
 */
static nserror nsgtk_saveas_dialog(struct browser_window *bw,
				   const char *title,
				   GtkWindow *parent,
				   bool folder,
				   gchar **path_out)
{
	nserror res;
	char *path; /* proposed path */
	char *chosen;

	if (!browser_window_has_content(bw)) {
		/* cannot save a page with no content */
		return NSERROR_INVALID;
	}

	/* set a default file name */
	res = nsurl_nice(browser_window_access_url(bw), &path, false);
	if (res != NSERROR_OK) {
		path = strdup(messages_get("SaveText"));
		if (path == NULL) {
			return NSERROR_NOMEM;
		}
	}

	/* run the dialog to let user select path */
	chosen = nsgtk_save_file_choose(parent,
					title,
					NULL,
					((!folder) || (access(path, F_OK) != 0))
						? path
						: NULL);
	free(path);
	if (chosen == NULL) {
		return NSERROR_NOT_FOUND;
	}

	/* a complete save goes into a new folder of the chosen name */
	if (folder) {
		g_mkdir_with_parents(chosen, 0755);
	}

	*path_out = chosen;

	return NSERROR_OK;
}


/**
 * connect all signals to widgets in a customisation
 */
static nserror toolbar_customisation_connect_signals(struct nsgtk_toolbar *tb)
{
	int iidx;

	for (iidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
		/* skip inactive items in toolbar */
		if ((tb->items[iidx].location != INACTIVE_LOCATION) &&
		    (tb->items[iidx].button != NULL)) {
			toolbar_item_connect_signals(tb, iidx);
		}
	}

	return NSERROR_OK;
}


/**
 * add a row to a toolbar customisation toolbox
 *
 * \param tbc The toolbar customisation context
 * \param startitem The item index of the beginning of the row
 * \param enditem The item index of the beginning of the next row
 * \return NSERROR_OK on successs else error
 */
static nserror add_toolbox_row(struct nsgtk_toolbar_customisation *tbc,
			       int startitem,
			       int enditem)
{
	GtkWidget *rowbar;
	int iidx;

	rowbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class(rowbar, "toolbar");
	gtk_box_append(tbc->toolbox, rowbar);

	for (iidx = startitem; iidx < enditem; iidx++) {
		if (tbc->items[iidx] == NULL) {
			/* skip any widgets that failed to initialise */
			continue;
		}
		gtk_widget_set_size_request(tbc->items[iidx],
					    NSGTK_BUTTON_WIDTH,
					    NSGTK_BUTTON_HEIGHT);
		toolbar_item_drag_source(tbc->items[iidx], iidx);
		gtk_box_append(GTK_BOX(rowbar), tbc->items[iidx]);
	}
	return NSERROR_OK;
}


/**
 * creates widgets in customisation toolbox
 *
 * \param tbc The toolbar customisation context
 * \param width The width to layout the toolbox to
 * \return NSERROR_OK on success else error code.
 */
static nserror
toolbar_customisation_create_toolbox(struct nsgtk_toolbar_customisation *tbc,
				     int width)
{
	int columns; /* number of items in a single row */
	int curcol; /* current column in creation */
	int iidx; /* item index */
	int startidx; /* index of item at start of row */

	/* ensure there are a minimum number of items per row */
	columns = width / NSGTK_BUTTON_WIDTH;
	if (columns < NSGTK_MIN_STORE_COLUMNS) {
		columns = NSGTK_MIN_STORE_COLUMNS;
	}

	curcol = 0;
	for (iidx = startidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
		if (curcol >= columns) {
			add_toolbox_row(tbc, startidx, iidx);
			curcol = 0;
			startidx = iidx;
		}
		tbc->items[iidx] = make_toolbox_item(iidx, false);
		if (tbc->items[iidx] != NULL) {
			curcol++;
		}
	}
	if (curcol > 0) {
		add_toolbox_row(tbc, startidx, iidx);
	}

	return NSERROR_OK;
}


/**
 * update toolbar in customisation to user settings
 */
static nserror
customisation_toolbar_update(struct nsgtk_toolbar_customisation *tbc)
{
	nserror res;

	res = apply_user_button_customisation(&tbc->toolbar);
	if (res != NSERROR_OK) {
		return res;
	}

	/* populate toolbar widget */
	res = customisation_toolbar_populate(&tbc->toolbar);
	if (res != NSERROR_OK) {
		return res;
	}

	/* ensure icon sizes and text labels on toolbar are set */
	res = nsgtk_toolbar_restyle(&tbc->toolbar);
	if (res != NSERROR_OK) {
		return res;
	}

	/* attach handlers to toolbar widgets */
	res = toolbar_customisation_connect_signals(&tbc->toolbar);
	if (res != NSERROR_OK) {
		return res;
	}

	return NSERROR_OK;
}


/**
 * close a toolbar customisation tab
 */
static void customisation_close(struct nsgtk_toolbar_customisation *tbc)
{
	GtkNotebook *notebook;
	gint pagenum;

	notebook = GTK_NOTEBOOK(
		gtk_widget_get_ancestor(tbc->container, GTK_TYPE_NOTEBOOK));
	if (notebook != NULL) {
		pagenum = gtk_notebook_page_num(notebook, tbc->container);
		if (pagenum > 0) {
			gtk_notebook_set_current_page(notebook, pagenum - 1);
		}
		gtk_notebook_remove_page(notebook, pagenum);
	}
}


/**
 * customisation apply handler for clicked signal
 *
 * when 'save settings' button is clicked
 */
static gboolean customisation_apply_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar_customisation *tbc;
	tbc = (struct nsgtk_toolbar_customisation *)data;

	/* save state to file, update toolbars for all windows */
	nsgtk_toolbar_customisation_save(&tbc->toolbar);
	nsgtk_window_toolbar_update();
	customisation_close(tbc);

	return TRUE;
}


/**
 * customisation discard handler for clicked signal
 */
static void customisation_discard_clicked_cb(GtkWidget *widget, gpointer data)
{
	customisation_close((struct nsgtk_toolbar_customisation *)data);
}


/**
 * customisation reset handler for clicked signal
 *
 * when 'reload defaults' button is clicked
 */
static gboolean customisation_reset_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar_customisation *tbc;
	tbc = (struct nsgtk_toolbar_customisation *)data;

	customisation_toolbar_update(tbc);

	return TRUE;
}


/**
 * customisation container destroy handler
 */
static void customisation_container_destroy_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar_customisation *tbc;
	tbc = (struct nsgtk_toolbar_customisation *)data;

	free(tbc);
}

/*
 * Toolbar button clicked handlers
 */

/**
 * create a toolbar customisation tab
 *
 * this is completely different approach to previous implementation. it
 *  is not modal and the toolbar configuration is performed completely
 *  within the tab. once the user is happy they can apply the change or
 *  cancel as they see fit while continuing to use the browser as usual.
 */
static gboolean cutomize_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar_customisation *tbc;
	nserror res;
	GtkBuilder *builder;
	GtkNotebook *notebook; /* notebook containing widget */
	GtkDropTarget *target;
	int iidx; /* item index */

	/* obtain the notebook being added to */
	notebook = GTK_NOTEBOOK(
		gtk_widget_get_ancestor(widget, GTK_TYPE_NOTEBOOK));
	if (notebook == NULL) {
		return TRUE;
	}

	/* create builder */
	res = nsgtk_builder_new_from_resname("toolbar", &builder);
	if (res != NSERROR_OK) {
		NSLOG(netsurf, INFO, "Toolbar UI builder init failed");
		return TRUE;
	}

	/* create nsgtk_toolbar_customisation which has nsgtk_toolbar
	 * at the front so we can reuse functions that take
	 * nsgtk_toolbar
	 */
	tbc = calloc(1, sizeof(struct nsgtk_toolbar_customisation));
	if (tbc == NULL) {
		g_object_unref(builder);
		return TRUE;
	}

	/* get container box widget which forms a page of the tabs */
	tbc->container = GTK_WIDGET(
		gtk_builder_get_object(builder, "customisation"));
	if (tbc->container == NULL) {
		goto cutomize_button_clicked_cb_error;
	}

	/* vertical box for the toolbox to drag items into and out of */
	tbc->toolbox = GTK_BOX(gtk_builder_get_object(builder, "toolbox"));
	if (tbc->toolbox == NULL) {
		goto cutomize_button_clicked_cb_error;
	}

	/* customisation toolbar container */
	tbc->toolbar.widget = GTK_WIDGET(
		gtk_builder_get_object(builder, "toolbar"));
	if (tbc->toolbar.widget == NULL) {
		goto cutomize_button_clicked_cb_error;
	}

	/* build customisation toolbar */
	for (iidx = BACK_BUTTON; iidx < PLACEHOLDER_BUTTON; iidx++) {
		res = toolbar_item_create(iidx, &tbc->toolbar.items[iidx]);
		if (res != NSERROR_OK) {
			goto cutomize_button_clicked_cb_error;
		}
	}

	res = customisation_toolbar_update(tbc);
	if (res != NSERROR_OK) {
		goto cutomize_button_clicked_cb_error;
	}

	/* use toolbox for widgets to drag to/from */
	res = toolbar_customisation_create_toolbox(
		tbc, gtk_widget_get_width(GTK_WIDGET(notebook)));
	if (res != NSERROR_OK) {
		goto cutomize_button_clicked_cb_error;
	}

	/* items dropped on the toolbar are added to it */
	target = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);
	g_signal_connect(
		target, "drop", G_CALLBACK(customisation_toolbar_drop_cb), tbc);
	gtk_widget_add_controller(tbc->toolbar.widget,
				  GTK_EVENT_CONTROLLER(target));

	/* items dropped elsewhere are removed from the toolbar */
	target = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);
	g_signal_connect(target,
			 "drop",
			 G_CALLBACK(customisation_container_drop_cb),
			 tbc);
	gtk_widget_add_controller(tbc->container, GTK_EVENT_CONTROLLER(target));

	/* discard button closes the customisation */
	g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "discard")),
			 "clicked",
			 G_CALLBACK(customisation_discard_clicked_cb),
			 tbc);

	/* save and update on apply button  */
	g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "apply")),
			 "clicked",
			 G_CALLBACK(customisation_apply_clicked_cb),
			 tbc);

	g_signal_connect(GTK_WIDGET(gtk_builder_get_object(builder, "reset")),
			 "clicked",
			 G_CALLBACK(customisation_reset_clicked_cb),
			 tbc);

	/* close and cleanup on delete signal */
	g_signal_connect(tbc->container,
			 "destroy",
			 G_CALLBACK(customisation_container_destroy_cb),
			 tbc);

	nsgtk_tab_add_page(notebook,
			   tbc->container,
			   false,
			   messages_get("gtkCustomizeToolbarTitle"),
			   favicon_pixbuf);


	/* safe to drop the reference to the builder as the container is
	 * referenced by the notebook now.
	 */
	g_object_unref(builder);

	return TRUE;

cutomize_button_clicked_cb_error:
	free(tbc);
	g_object_unref(builder);
	return TRUE;
}


/**
 * handler for back tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean back_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	if ((bw != NULL) && browser_window_history_back_available(bw)) {
		/* clear potential search effects */
		browser_window_search_clear(bw);

		browser_window_history_back(bw, false);

		set_item_sensitivity(&tb->items[BACK_BUTTON],
				     browser_window_history_back_available(bw));
		set_item_sensitivity(&tb->items[FORWARD_BUTTON],
				     browser_window_history_forward_available(
					     bw));

		nsgtk_local_history_hide();
	}
	return TRUE;
}


/**
 * handler for forward tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean forward_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	if ((bw != NULL) && browser_window_history_forward_available(bw)) {
		/* clear potential search effects */
		browser_window_search_clear(bw);

		browser_window_history_forward(bw, false);

		set_item_sensitivity(&tb->items[BACK_BUTTON],
				     browser_window_history_back_available(bw));
		set_item_sensitivity(&tb->items[FORWARD_BUTTON],
				     browser_window_history_forward_available(
					     bw));
		nsgtk_local_history_hide();
	}
	return TRUE;
}


/**
 * handler for stop tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean stop_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;

	browser_window_stop(tb->get_bw(tb->get_ctx));

	return TRUE;
}


/**
 * handler for reload tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean reload_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	/* clear potential search effects */
	browser_window_search_clear(bw);

	browser_window_reload(bw, true);

	return TRUE;
}


/**
 * handler for reload/stop tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean reloadstop_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	/* clear potential search effects */
	browser_window_search_clear(bw);

	if (tb->items[RELOADSTOP_BUTTON].sensitivity) {
		browser_window_reload(bw, true);
	} else {
		browser_window_stop(tb->get_bw(tb->get_ctx));
	}

	return TRUE;
}


/**
 * handler for home tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean home_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	nserror res;
	const char *addr;

	if (nsoption_charp(homepage_url) != NULL) {
		addr = nsoption_charp(homepage_url);
	} else {
		addr = NETSURF_HOMEPAGE;
	}

	res = toolbar_navigate_to_url(tb, addr);
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}


/**
 * callback for url entry widget activation
 *
 * handler connected to url entry widget for the activate signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE to allow activation.
 */
static gboolean url_entry_activate_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	nsurl *url;

	res = search_web_omni(gtk_editable_get_text(GTK_EDITABLE(widget)),
			      SEARCH_WEB_OMNI_NONE,
			      &url);
	if (res == NSERROR_OK) {
		bw = tb->get_bw(tb->get_ctx);
		res = browser_window_navigate(
			bw, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
		nsurl_unref(url);
	}
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}


/**
 * callback for url entry widget changing
 *
 * handler connected to url entry widget for the change signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param event The key change event that changed the entry.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE to allow activation.
 */
static gboolean url_entry_changed_cb(GtkEditable *editable, gpointer data)
{
	return nsgtk_completion_update(GTK_ENTRY(editable));
}


/**
 * callback for url entry widget icon button release
 *
 * handler connected to url entry widget for the icon release signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param event The key change event that changed the entry.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE to allow activation.
 */
static void url_entry_icon_release_cb(GtkEntry *entry,
				      GtkEntryIconPosition icon_pos,
				      gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	nsgtk_page_info(GTK_WIDGET(entry), bw);
}


/**
 * handler for web search tool bar entry item activate signal
 *
 * handler connected to web search entry widget for the activate signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean websearch_entry_activate_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	nsurl *url;

	res = search_web_omni(gtk_editable_get_text(GTK_EDITABLE(widget)),
			      SEARCH_WEB_OMNI_SEARCHONLY,
			      &url);
	if (res == NSERROR_OK) {
		bw = tb->get_bw(tb->get_ctx);

		res = browser_window_navigate(
			bw, url, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
		nsurl_unref(url);
	}
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}

/**
 * handler for web search tool bar item focus in
 *
 * selects the contents of the websearch entry field so typing replaces
 * the name of the provider.
 *
 * \param controller The focus controller of the entry.
 * \param data The toolbar context passed when the signal was connected
 */
static void websearch_entry_button_press_cb(GtkEventControllerFocus *controller,
					    gpointer data)
{
	GtkWidget *widget;

	widget = gtk_event_controller_get_widget(
		GTK_EVENT_CONTROLLER(controller));
	gtk_editable_select_region(GTK_EDITABLE(widget), 0, -1);
}


/**
 * handler for new window tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean newwindow_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;

	res = nsgtk_browser_window_create(tb->get_bw(tb->get_ctx), false);
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}


/**
 * handler for new tab tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean newtab_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;

	res = nsgtk_browser_window_create(tb->get_bw(tb->get_ctx), true);
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}
	return TRUE;
}


/**
 * open a file chosen in the open file dialog
 */
static void openfile_chosen(const char *filename, void *pw)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)pw;
	size_t urltxt_size;
	char *urltxt;
	nserror res;
	nsurl *url;

	urltxt_size = strlen(filename) + FILE_SCHEME_PREFIX_LEN + 1;
	urltxt = malloc(urltxt_size);
	if (urltxt == NULL) {
		return;
	}
	snprintf(urltxt, urltxt_size, FILE_SCHEME_PREFIX "%s", filename);

	res = nsurl_create(urltxt, &url);
	if (res == NSERROR_OK) {
		res = browser_window_navigate(tb->get_bw(tb->get_ctx),
					      url,
					      NULL,
					      BW_NAVIGATE_HISTORY,
					      NULL,
					      NULL,
					      NULL);
		nsurl_unref(url);
	}
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}
	free(urltxt);
}


/**
 * handler for open file tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean openfile_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	nsgtk_open_file_dialog(
		GTK_WINDOW(toplevel), "Open File", NULL, openfile_chosen, data);

	return TRUE;
}


/**
 * handler for close window tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean closewindow_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *toplevel;
	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
	gtk_window_destroy(GTK_WINDOW(toplevel));
	return TRUE;
}


/**
 * handler for full save export tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean savepage_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	DIR *d;
	gchar *path;
	nserror res;
	GtkWidget *toplevel;

	bw = tb->get_bw(tb->get_ctx);
	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	res = nsgtk_saveas_dialog(bw,
				  messages_get("gtkcompleteSave"),
				  GTK_WINDOW(toplevel),
				  true,
				  &path);
	if (res != NSERROR_OK) {
		return FALSE;
	}

	d = opendir(path);
	if (d == NULL) {
		NSLOG(netsurf,
		      INFO,
		      "Unable to open directory %s for complete save: %s",
		      path,
		      strerror(errno));
		if (errno == ENOTDIR) {
			nsgtk_warning("NoDirError", path);
		} else {
			nsgtk_warning("gtkFileError", path);
		}
		g_free(path);
		return TRUE;
	}
	closedir(d);

	save_complete(browser_window_get_content(bw), path, NULL);
	g_free(path);

	return TRUE;
}


/**
 * handler for pdf export tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean pdf_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *toplevel;
	gchar *filename;
	nserror res;

	bw = tb->get_bw(tb->get_ctx);

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	res = nsgtk_saveas_dialog(
		bw, "Export to PDF", GTK_WINDOW(toplevel), false, &filename);
	if (res != NSERROR_OK) {
		return FALSE;
	}

#ifdef WITH_PDF_EXPORT
	struct print_settings *settings;

	/* this way the scale used by PDF functions is synchronised with that
	 * used by the all-purpose print interface
	 */
	haru_nsfont_set_scale((float)option_export_scale / 100);

	settings = print_make_settings(PRINT_OPTIONS,
				       (const char *)filename,
				       &haru_nsfont);
	g_free(filename);
	if (settings == NULL) {
		return TRUE;
	}
	/* This will clean up the print_settings object for us */
	print_basic_run(browser_window_get_content(bw), &pdf_printer, settings);
#endif
	return TRUE;
}


/**
 * handler for plain text export tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean plaintext_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *toplevel;
	gchar *filename;
	nserror res;

	bw = tb->get_bw(tb->get_ctx);

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	res = nsgtk_saveas_dialog(bw,
				  messages_get("gtkplainSave"),
				  GTK_WINDOW(toplevel),
				  false,
				  &filename);
	if (res != NSERROR_OK) {
		return FALSE;
	}


	save_as_text(browser_window_get_content(bw), filename);
	g_free(filename);

	return TRUE;
}


/**
 * handler for print tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean print_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkPrintOperation *print_op;
	GtkPageSetup *page_setup;
	GtkPrintSettings *print_settings;
	GtkPrintOperationResult res = GTK_PRINT_OPERATION_RESULT_ERROR;
	struct print_settings *nssettings;
	char *settings_fname = NULL;
	GtkWidget *toplevel;

	bw = tb->get_bw(tb->get_ctx);

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	print_op = gtk_print_operation_new();
	if (print_op == NULL) {
		nsgtk_warning(messages_get("NoMemory"), 0);
		return TRUE;
	}

	/* use previously saved settings if any */
	netsurf_mkpath(&settings_fname, NULL, 2, nsgtk_config_home, "Print");
	if (settings_fname != NULL) {
		print_settings = gtk_print_settings_new_from_file(
			settings_fname, NULL);
		if (print_settings != NULL) {
			gtk_print_operation_set_print_settings(print_op,
							       print_settings);

			/* We're not interested in the settings any more */
			g_object_unref(print_settings);
		}
	}

	content_to_print = browser_window_get_content(bw);

	page_setup = gtk_print_run_page_setup_dialog(GTK_WINDOW(toplevel),
						     NULL,
						     NULL);
	if (page_setup == NULL) {
		nsgtk_warning(messages_get("NoMemory"), 0);
		free(settings_fname);
		g_object_unref(print_op);
		return TRUE;
	}
	gtk_print_operation_set_default_page_setup(print_op, page_setup);

	nssettings = print_make_settings(PRINT_DEFAULT,
					 NULL,
					 nsgtk_layout_table);

	g_signal_connect(print_op,
			 "begin_print",
			 G_CALLBACK(gtk_print_signal_begin_print),
			 nssettings);
	g_signal_connect(print_op,
			 "draw_page",
			 G_CALLBACK(gtk_print_signal_draw_page),
			 NULL);
	g_signal_connect(print_op,
			 "end_print",
			 G_CALLBACK(gtk_print_signal_end_print),
			 nssettings);

	if (content_get_type(browser_window_get_content(bw)) !=
	    CONTENT_TEXTPLAIN) {
		res = gtk_print_operation_run(
			print_op,
			GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
			GTK_WINDOW(toplevel),
			NULL);
	}

	/* if the settings were used save them for future use */
	if (settings_fname != NULL) {
		if (res == GTK_PRINT_OPERATION_RESULT_APPLY) {
			/* Do not increment the settings reference */
			print_settings = gtk_print_operation_get_print_settings(
				print_op);

			gtk_print_settings_to_file(print_settings,
						   settings_fname,
						   NULL);
		}
		free(settings_fname);
	}

	/* Our print_settings object is destroyed by the end print handler */
	g_object_unref(page_setup);
	g_object_unref(print_op);

	return TRUE;
}

/**
 * handler for quit tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean quit_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nsgtk_scaffolding_destroy_all();
	return TRUE;
}


/**
 * handler for cut tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean cut_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *focused;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	focused = gtk_window_get_focus(GTK_WINDOW(toplevel));

	/* let gtk handle it if focused widget is an editable */
	if (GTK_IS_EDITABLE(focused)) {
		gtk_widget_activate_action(focused, "clipboard.cut", NULL);
	} else {
		bw = tb->get_bw(tb->get_ctx);
		browser_window_key_press(bw, NS_KEY_CUT_SELECTION);
	}

	return TRUE;
}


/**
 * handler for copy tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean copy_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *focused;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	focused = gtk_window_get_focus(GTK_WINDOW(toplevel));

	/* let gtk handle it if focused widget is an editable */
	if (GTK_IS_EDITABLE(focused)) {
		gtk_widget_activate_action(focused, "clipboard.copy", NULL);
	} else {
		bw = tb->get_bw(tb->get_ctx);
		browser_window_key_press(bw, NS_KEY_COPY_SELECTION);
	}

	return TRUE;
}


/**
 * handler for paste tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean paste_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *focused;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	focused = gtk_window_get_focus(GTK_WINDOW(toplevel));

	/* let gtk handle it if focused widget is an editable */
	if (GTK_IS_EDITABLE(focused)) {
		gtk_widget_activate_action(focused, "clipboard.paste", NULL);
	} else {
		bw = tb->get_bw(tb->get_ctx);
		browser_window_key_press(bw, NS_KEY_PASTE);
	}

	return TRUE;
}


/**
 * handler for delete tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean delete_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *focused;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	focused = gtk_window_get_focus(GTK_WINDOW(toplevel));

	/* let gtk handle it if focused widget is an editable */
	if (GTK_IS_EDITABLE(focused)) {
		gtk_editable_delete_selection(GTK_EDITABLE(focused));
	} else {
		bw = tb->get_bw(tb->get_ctx);
		browser_window_key_press(bw, NS_KEY_CLEAR_SELECTION);
	}

	return TRUE;
}


/**
 * handler for select all tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean selectall_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *focused;
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	focused = gtk_window_get_focus(GTK_WINDOW(toplevel));

	/* let gtk handle it if focused widget is an editable */
	if (GTK_IS_EDITABLE(focused)) {
		gtk_editable_select_region(GTK_EDITABLE(focused), 0, -1);
	} else {
		bw = tb->get_bw(tb->get_ctx);
		browser_window_key_press(bw, NS_KEY_SELECT_ALL);
	}

	return TRUE;
}


/**
 * handler for preferences tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean preferences_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *toplevel;
	GtkWidget *wndpreferences;

	bw = tb->get_bw(tb->get_ctx);

	toplevel = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	wndpreferences = nsgtk_preferences(bw, GTK_WINDOW(toplevel));
	if (wndpreferences != NULL) {
		gtk_widget_set_visible(wndpreferences, TRUE);
	}

	return TRUE;
}


/**
 * handler for zoom plus tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean zoomplus_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	browser_window_set_scale(bw, 0.05, false);

	return TRUE;
}


/**
 * handler for zoom minus tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean zoomminus_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	browser_window_set_scale(bw, -0.05, false);

	return TRUE;
}


/**
 * handler for zoom normal tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean zoomnormal_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	browser_window_set_scale(bw, 1.0, true);

	return TRUE;
}


/**
 * handler for full screen tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean fullscreen_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	GtkWindow *gtkwindow; /* gtk window widget is in */

	gtkwindow = GTK_WINDOW(
		gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW));

	if (gtk_window_is_fullscreen(gtkwindow)) {
		gtk_window_unfullscreen(gtkwindow);
	} else {
		gtk_window_fullscreen(gtkwindow);
	}
	return TRUE;
}


/**
 * handler for view source tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean viewsource_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWindow *gtkwindow; /* gtk window widget is in */

	bw = tb->get_bw(tb->get_ctx);

	gtkwindow = GTK_WINDOW(
		gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW));

	res = nsgtk_viewsource(gtkwindow, bw);
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}


/**
 * handler for show downloads tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean downloads_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	GtkWindow *gtkwindow; /* gtk window widget is in */
	gtkwindow = GTK_WINDOW(
		gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW));
	nsgtk_download_show(gtkwindow);
	return TRUE;
}


/**
 * handler for show downloads tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean
savewindowsize_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *gtkwindow; /* gtk window widget is in */
	char *choices = NULL;

	gtkwindow = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);

	/* the window position is not available on all platforms */
	nsoption_set_int(window_width, gtk_widget_get_width(gtkwindow));
	nsoption_set_int(window_height, gtk_widget_get_height(gtkwindow));

	netsurf_mkpath(&choices, NULL, 2, nsgtk_config_home, "Choices");
	if (choices != NULL) {
		nsoption_write(choices, NULL, NULL);
		free(choices);
	}

	return TRUE;
}


/**
 * handler for show downloads tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean
toggledebugging_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);

	browser_window_debug(bw, CONTENT_DEBUG_REDRAW);

	nsgtk_window_update_all();

	return TRUE;
}


/**
 * handler for debug box tree tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean debugboxtree_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	gchar *fname;
	gint handle;
	FILE *f;

	handle = g_file_open_tmp("nsgtkboxtreeXXXXXX", &fname, NULL);
	if ((handle == -1) || (fname == NULL)) {
		return TRUE;
	}
	close(handle); /* in case it was binary mode */

	/* save data to temporary file */
	f = fopen(fname, "w");
	if (f == NULL) {
		nsgtk_warning("Error saving box tree dump.",
			      "Unable to open file for writing.");
		unlink(fname);

	} else {
		bw = tb->get_bw(tb->get_ctx);

		browser_window_debug_dump(bw, f, CONTENT_DEBUG_RENDER);

		fclose(f);

		nsgtk_viewfile("Box Tree Debug", "boxtree", fname);
	}
	g_free(fname);

	return TRUE;
}


/**
 * handler for debug dom tree tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean debugdomtree_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	gchar *fname;
	gint handle;
	FILE *f;

	handle = g_file_open_tmp("nsgtkdomtreeXXXXXX", &fname, NULL);
	if ((handle == -1) || (fname == NULL)) {
		return TRUE;
	}
	close(handle); /* in case it was binary mode */

	/* save data to temporary file */
	f = fopen(fname, "w");
	if (f == NULL) {
		nsgtk_warning("Error saving box tree dump.",
			      "Unable to open file for writing.");
		unlink(fname);

	} else {
		bw = tb->get_bw(tb->get_ctx);

		browser_window_debug_dump(bw, f, CONTENT_DEBUG_DOM);

		fclose(f);

		nsgtk_viewfile("DOM Tree Debug", "domtree", fname);
	}
	g_free(fname);

	return TRUE;
}


/**
 * handler for local history tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean localhistory_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;
	GtkWidget *anchor = widget;

	if ((tb->items[HISTORY_BUTTON].location != INACTIVE_LOCATION) &&
	    (tb->items[HISTORY_BUTTON].button != NULL)) {
		anchor = tb->items[HISTORY_BUTTON].button;
	}
	if (anchor != NULL) {
		bw = tb->get_bw(tb->get_ctx);

		res = nsgtk_local_history_present(anchor, bw);
		if (res != NSERROR_OK) {
			NSLOG(netsurf,
			      INFO,
			      "Unable to present local history window.");
		}
	}
	return TRUE;
}

/**
 * handler for history tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean history_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	return localhistory_button_clicked_cb(widget, data);
}


/**
 * handler for global history tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean
globalhistory_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	res = nsgtk_global_history_present();
	if (res != NSERROR_OK) {
		NSLOG(netsurf,
		      INFO,
		      "Unable to initialise global history window.");
	}
	return TRUE;
}


/**
 * handler for add bookmark tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean addbookmarks_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct browser_window *bw;

	bw = tb->get_bw(tb->get_ctx);
	if (browser_window_has_content(bw)) {
		hotlist_add_url(browser_window_access_url(bw));
	}
	return TRUE;
}


/**
 * handler for show bookmark tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean
showbookmarks_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	res = nsgtk_hotlist_present();
	if (res != NSERROR_OK) {
		NSLOG(netsurf, INFO, "Unable to initialise bookmark window.");
	}
	return TRUE;
}


/**
 * handler for show cookies tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean showcookies_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	nserror res;
	res = nsgtk_cookies_present(NULL);
	if (res != NSERROR_OK) {
		NSLOG(netsurf, INFO, "Unable to initialise cookies window.");
	}
	return TRUE;
}


/**
 * handler for open location tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean openlocation_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	GtkWidget *entry;

	entry = tb->items[URL_BAR_ITEM].button;
	if (entry != NULL) {
		gtk_widget_grab_focus(entry);
	}
	return TRUE;
}


/**
 * handler for contents tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean contents_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	nserror res;

	res = toolbar_navigate_to_url(
		tb, "https://www.netsurf-browser.org/documentation/");
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}

/**
 * handler for contents tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean guide_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	nserror res;

	res = toolbar_navigate_to_url(
		tb, "https://www.netsurf-browser.org/documentation/guide");
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}


/**
 * handler for contents tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean info_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	nserror res;

	res = toolbar_navigate_to_url(
		tb, "https://www.netsurf-browser.org/documentation/info");
	if (res != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(res), 0);
	}

	return TRUE;
}


/**
 * handler for contents tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE
 */
static gboolean about_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	GtkWindow *parent; /* gtk window widget is in */

	parent = GTK_WINDOW(gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW));

	nsgtk_about_dialog_init(parent);
	return TRUE;
}

/**
 * handler for openmenu tool bar item clicked signal
 *
 * \param widget The widget the signal is being delivered to.
 * \param data The toolbar context passed when the signal was connected
 * \return TRUE to indicate signal handled.
 */
static gboolean openmenu_button_clicked_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct gui_window *gw;
	struct nsgtk_scaffolding *gs;

	gw = tb->get_ctx; /** \todo stop assuming the context is a gui window */

	gs = nsgtk_get_scaffold(gw);

	nsgtk_scaffolding_burger_menu(gs, widget);

	return TRUE;
}


/**
 * create a toolbar item
 *
 * create a toolbar item and set up its default handlers
 */
static nserror
toolbar_item_create(nsgtk_toolbar_button id, struct nsgtk_toolbar_item *item)
{
	item->location = INACTIVE_LOCATION;

	/* set item defaults from macro */
	switch (id) {
#define TOOLBAR_ITEM_t(name) item->clicked = name##_button_clicked_cb;
#define TOOLBAR_ITEM_b(name) item->clicked = name##_button_clicked_cb;
#define TOOLBAR_ITEM_y(name) item->clicked = name##_button_clicked_cb;
#define TOOLBAR_ITEM_n(name) item->clicked = NULL;
#define TOOLBAR_ITEM(                                                          \
	identifier, iname, snstvty, clicked, activate, label, iconame)         \
	case identifier:                                                       \
		item->name = #iname;                                           \
		item->sensitivity = snstvty;                                   \
		TOOLBAR_ITEM_##clicked(iname) break;

#include "gtk/toolbar_items.h"

#undef TOOLBAR_ITEM_t
#undef TOOLBAR_ITEM_y
#undef TOOLBAR_ITEM_n
#undef TOOLBAR_ITEM

	case PLACEHOLDER_BUTTON:
		return NSERROR_INVALID;
	}

	return NSERROR_OK;
}


/**
 * set a toolbar item to a throbber frame number
 *
 * \param toolbar_item The toolbar item to update
 * \param frame The animation frame number to update to
 * \return NSERROR_OK on success,
 *         NSERROR_INVALID if the toolbar item does not contain an image,
 *         NSERROR_BAD_SIZE if the frame is out of range.
 */
static nserror set_throbber_frame(GtkWidget *toolbar_item, int frame)
{
	nserror res;
	GdkPixbuf *pixbuf;

	if ((toolbar_item == NULL) || !GTK_IS_IMAGE(toolbar_item)) {
		/* no toolbar item */
		return NSERROR_INVALID;
	}

	res = nsgtk_throbber_get_frame(frame, &pixbuf);
	if (res != NSERROR_OK) {
		return res;
	}

	nsgtk_image_set_from_pixbuf(GTK_IMAGE(toolbar_item), pixbuf);

	return NSERROR_OK;
}


/**
 * Make the throbber run.
 *
 * scheduled callback to update the throbber
 *
 * \param p The context passed when scheduled.
 */
static void next_throbber_frame(void *p)
{
	struct nsgtk_toolbar *tb = p;
	nserror res;

	tb->throb_frame++; /* advance to next frame */

	res = set_throbber_frame(tb->items[THROBBER_ITEM].button,
				 tb->throb_frame);
	if (res == NSERROR_BAD_SIZE) {
		tb->throb_frame = 1;
		res = set_throbber_frame(tb->items[THROBBER_ITEM].button,
					 tb->throb_frame);
	}

	/* only schedule next frame if there are no errors */
	if (res == NSERROR_OK) {
		nsgtk_schedule(THROBBER_FRAME_TIME, next_throbber_frame, p);
	}
}


/**
 * connect signal handlers to a gtk toolbar item
 */
static nserror
toolbar_connect_signal(struct nsgtk_toolbar *tb, nsgtk_toolbar_button itemid)
{
	struct nsgtk_toolbar_item *item;
	GtkEntry *entry;
	GtkEventController *controller;

	item = &tb->items[itemid];

	if (item->button == NULL) {
		return NSERROR_OK;
	}

	switch (itemid) {
	case URL_BAR_ITEM:
		entry = GTK_ENTRY(item->button);

		g_signal_connect(GTK_WIDGET(entry),
				 "activate",
				 G_CALLBACK(url_entry_activate_cb),
				 tb);
		g_signal_connect(GTK_WIDGET(entry),
				 "changed",
				 G_CALLBACK(url_entry_changed_cb),
				 tb);
		g_signal_connect(GTK_WIDGET(entry),
				 "icon-release",
				 G_CALLBACK(url_entry_icon_release_cb),
				 tb);

		nsgtk_completion_connect_signals(entry,
						 tb->get_bw,
						 tb->get_ctx);
		break;


	case WEBSEARCH_ITEM:
		entry = GTK_ENTRY(item->button);

		g_signal_connect(GTK_WIDGET(entry),
				 "activate",
				 G_CALLBACK(websearch_entry_activate_cb),
				 tb);
		controller = gtk_event_controller_focus_new();
		g_signal_connect(controller,
				 "enter",
				 G_CALLBACK(websearch_entry_button_press_cb),
				 tb);
		gtk_widget_add_controller(GTK_WIDGET(entry), controller);
		break;

	default:
		if ((item->clicked != NULL) && (item->button != NULL)) {
			g_signal_connect(item->button,
					 "clicked",
					 G_CALLBACK(item->clicked),
					 tb);
		}
		break;
	}

	return NSERROR_OK;
}

/**
 * connect all signals to widgets in a toolbar
 */
static nserror toolbar_connect_signals(struct nsgtk_toolbar *tb)
{
	int location; /* location index */
	nsgtk_toolbar_button itemid; /* item id */

	for (location = BACK_BUTTON; location < PLACEHOLDER_BUTTON;
	     location++) {
		itemid = itemid_from_location(tb, location);
		if (itemid == PLACEHOLDER_BUTTON) {
			/* no more filled locations */
			break;
		}
		toolbar_connect_signal(tb, itemid);
	}

	return NSERROR_OK;
}


/**
 * signal handler for toolbar context menu
 *
 * \param toolbar The toolbar event is being delivered to
 * \param x The x coordinate where the click happened
 * \param y The x coordinate where the click happened
 * \param button the buttons being pressed
 * \param data The context pointer passed when the connection was made.
 * \return TRUE to indicate signal handled.
 */
static void toolbar_popup_context_menu_cb(GtkGestureClick *gesture,
					  gint n_press,
					  gdouble x,
					  gdouble y,
					  gpointer data)
{
	struct nsgtk_toolbar *tb = (struct nsgtk_toolbar *)data;
	struct gui_window *gw;
	struct nsgtk_scaffolding *gs;

	gw = tb->get_ctx; /** \todo stop assuming the context is a gui window */

	gs = nsgtk_get_scaffold(gw);

	nsgtk_scaffolding_toolbar_context_menu(gs, tb->widget, x, y);
}


/**
 * toolbar delete signal handler
 */
static void toolbar_destroy_cb(GtkWidget *widget, gpointer data)
{
	struct nsgtk_toolbar *tb;
	tb = (struct nsgtk_toolbar *)data;

	/* ensure any throbber scheduled is stopped */
	nsgtk_schedule(-1, next_throbber_frame, tb);

	free(tb);
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_create(GtkBuilder *builder,
			     struct browser_window *(*get_bw)(void *ctx),
			     void *get_ctx,
			     bool want_location_focus,
			     struct nsgtk_toolbar **tb_out)
{
	nserror res;
	struct nsgtk_toolbar *tb;
	int bidx; /* button index */
	GtkGesture *gesture;

	tb = calloc(1, sizeof(struct nsgtk_toolbar));
	if (tb == NULL) {
		return NSERROR_NOMEM;
	}

	tb->get_bw = get_bw;
	tb->get_ctx = get_ctx;
	/* set the throbber start frame. */
	tb->throb_frame = 0;
	if (want_location_focus) {
		tb->loc_focus = LFS_WANT;
	} else {
		tb->loc_focus = LFS_IDLE;
	}

	tb->widget = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar"));

	gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 3);
	g_signal_connect(gesture,
			 "pressed",
			 G_CALLBACK(toolbar_popup_context_menu_cb),
			 tb);
	gtk_widget_add_controller(tb->widget, GTK_EVENT_CONTROLLER(gesture));

	/* close and cleanup on delete signal */
	g_signal_connect(
		tb->widget, "destroy", G_CALLBACK(toolbar_destroy_cb), tb);

	/* allocate button contexts */
	for (bidx = BACK_BUTTON; bidx < PLACEHOLDER_BUTTON; bidx++) {
		res = toolbar_item_create(bidx, &tb->items[bidx]);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	res = nsgtk_toolbar_update(tb);
	if (res != NSERROR_OK) {
		return res;
	}

	*tb_out = tb;
	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_restyle(struct nsgtk_toolbar *tb)
{
	int itemid;

	for (itemid = BACK_BUTTON; itemid < PLACEHOLDER_BUTTON; itemid++) {
		GtkWidget *button = tb->items[itemid].button;
		const char *iconname;
		const char *labelmsg;

		if ((tb->items[itemid].location == INACTIVE_LOCATION) ||
		    (button == NULL)) {
			continue;
		}
		iconname = g_object_get_data(G_OBJECT(button), "nsgtk-icon");
		labelmsg = g_object_get_data(G_OBJECT(button), "nsgtk-label");
		if ((iconname != NULL) && (labelmsg != NULL)) {
			char *icon = g_strdup(iconname);
			char *label = g_strdup(labelmsg);

			set_button_content(
				button, icon, label, nsoption_int(button_type));
			g_free(icon);
			g_free(label);
		}
	}

	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_throbber(struct nsgtk_toolbar *tb, bool active)
{
	nserror res;
	struct browser_window *bw;

	/* Manage the location focus state */
	switch (tb->loc_focus) {
	case LFS_IDLE:
		break;
	case LFS_WANT:
		if (active) {
			tb->loc_focus = LFS_THROB;
		}
		break;
	case LFS_THROB:
		if (!active) {
			tb->loc_focus = LFS_LAST;
		}
		break;
	case LFS_LAST:
		break;
	}

	/* when activating the throbber simply schedule the next frame update */
	if (active) {
		nsgtk_schedule(THROBBER_FRAME_TIME, next_throbber_frame, tb);

		set_item_sensitivity(&tb->items[STOP_BUTTON], true);
		set_item_sensitivity(&tb->items[RELOAD_BUTTON], false);
		set_item_action(tb, RELOADSTOP_BUTTON, false);

		return NSERROR_OK;
	}

	/* stopping the throbber */
	nsgtk_schedule(-1, next_throbber_frame, tb);
	tb->throb_frame = 0;
	res = set_throbber_frame(tb->items[THROBBER_ITEM].button,
				 tb->throb_frame);

	bw = tb->get_bw(tb->get_ctx);

	/* adjust sensitivity of other items */
	set_item_sensitivity(&tb->items[STOP_BUTTON], false);
	set_item_sensitivity(&tb->items[RELOAD_BUTTON], true);
	set_item_action(tb, RELOADSTOP_BUTTON, true);
	set_item_sensitivity(&tb->items[BACK_BUTTON],
			     browser_window_history_back_available(bw));
	set_item_sensitivity(&tb->items[FORWARD_BUTTON],
			     browser_window_history_forward_available(bw));
	nsgtk_local_history_hide();

	return res;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_page_info_change(struct nsgtk_toolbar *tb)
{
	GtkEntry *url_entry;
	browser_window_page_info_state pistate;
	struct browser_window *bw;
	const char *icon_name;

	if (tb->items[URL_BAR_ITEM].button == NULL) {
		/* no toolbar item */
		return NSERROR_INVALID;
	}
	url_entry = GTK_ENTRY(tb->items[URL_BAR_ITEM].button);

	bw = tb->get_bw(tb->get_ctx);

	pistate = browser_window_get_page_info_state(bw);

	switch (pistate) {
	case PAGE_STATE_INTERNAL:
		icon_name = "page-info-internal";
		break;

	case PAGE_STATE_LOCAL:
		icon_name = "page-info-local";
		break;

	case PAGE_STATE_INSECURE:
		icon_name = "page-info-insecure";
		break;

	case PAGE_STATE_SECURE_OVERRIDE:
		icon_name = "page-info-warning";
		break;

	case PAGE_STATE_SECURE_ISSUES:
		icon_name = "page-info-warning";
		break;

	case PAGE_STATE_SECURE:
		icon_name = "page-info-secure";
		break;

	default:
		icon_name = "page-info-internal";
		break;
	}

	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(url_entry),
					  GTK_ENTRY_ICON_PRIMARY,
					  icon_name);
	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_set_url(struct nsgtk_toolbar *tb, nsurl *url)
{
	size_t idn_url_l;
	char *idn_url_s = NULL;
	const char *url_text = NULL;
	GtkEntry *url_entry;

	if (tb->items[URL_BAR_ITEM].button == NULL) {
		/* no toolbar item */
		return NSERROR_INVALID;
	}
	url_entry = GTK_ENTRY(tb->items[URL_BAR_ITEM].button);

	if (nsoption_bool(display_decoded_idn) == true) {
		if (nsurl_get_utf8(url, &idn_url_s, &idn_url_l) != NSERROR_OK) {
			idn_url_s = NULL;
		}
		url_text = idn_url_s;
	}
	if (url_text == NULL) {
		url_text = nsurl_access(url);
	}

	if (strcmp(url_text, gtk_editable_get_text(GTK_EDITABLE(url_entry))) !=
	    0) {
		/* The URL bar content has changed, we need to update it */
		gint startpos, endpos;
		bool was_selected;
		gtk_editable_get_selection_bounds(GTK_EDITABLE(url_entry),
						  &startpos,
						  &endpos);
		was_selected = gtk_widget_is_focus(GTK_WIDGET(url_entry)) &&
			       startpos == 0 &&
			       endpos == gtk_entry_get_text_length(url_entry);
		gtk_editable_set_text(GTK_EDITABLE(url_entry), url_text);
		if (was_selected && tb->loc_focus != LFS_IDLE) {
			gtk_widget_grab_focus(GTK_WIDGET(url_entry));
			if (tb->loc_focus == LFS_LAST) {
				tb->loc_focus = LFS_IDLE;
			}
		}
	}

	if (idn_url_s != NULL) {
		free(idn_url_s);
	}

	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror
nsgtk_toolbar_set_websearch_image(struct nsgtk_toolbar *tb, GdkPixbuf *pixbuf)
{
	GtkWidget *entry;

	if (tb->items[WEBSEARCH_ITEM].button == NULL) {
		/* no toolbar item */
		return NSERROR_INVALID;
	}

	entry = tb->items[WEBSEARCH_ITEM].button;

	if (pixbuf != NULL) {
		nsgtk_entry_set_icon_from_pixbuf(entry,
						 GTK_ENTRY_ICON_PRIMARY,
						 pixbuf);
	} else {
		gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry),
						  GTK_ENTRY_ICON_PRIMARY,
						  NSGTK_STOCK_INFO);
	}

	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_item_activate(struct nsgtk_toolbar *tb,
				    nsgtk_toolbar_button itemid)
{
	GtkWidget *widget;

	/* ensure item id in range */
	if ((itemid < BACK_BUTTON) || (itemid >= PLACEHOLDER_BUTTON)) {
		return NSERROR_BAD_PARAMETER;
	}

	if (tb->items[itemid].clicked == NULL) {
		return NSERROR_INVALID;
	}

	/*
	 * if item has a widget in the current toolbar use that as the
	 *   signal source otherwise use the toolbar widget itself.
	 */
	if (tb->items[itemid].button != NULL) {
		widget = GTK_WIDGET(tb->items[itemid].button);
	} else {
		widget = GTK_WIDGET(tb->widget);
	}

	tb->items[itemid].clicked(widget, tb);

	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_show(struct nsgtk_toolbar *tb, bool show)
{
	if (show) {
		gtk_widget_set_visible(GTK_WIDGET(tb->widget), TRUE);
	} else {
		gtk_widget_set_visible(GTK_WIDGET(tb->widget), FALSE);
	}
	return NSERROR_OK;
}


/* exported interface documented in toolbar.h */
nserror nsgtk_toolbar_update(struct nsgtk_toolbar *tb)
{
	nserror res;

	/* setup item locations based on user config */
	res = apply_user_button_customisation(tb);
	if (res != NSERROR_OK) {
		return res;
	}

	/* populate toolbar widget */
	res = populate_gtk_toolbar_widget(tb);
	if (res != NSERROR_OK) {
		return res;
	}

	/* ensure icon sizes and text labels on toolbar are set */
	res = nsgtk_toolbar_restyle(tb);
	if (res != NSERROR_OK) {
		return res;
	}

	res = toolbar_connect_signals(tb);

	return res;
}
