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

#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsurl.h"
#include "utils/nsoption.h"
#include "netsurf/browser_window.h"
#include "desktop/browser_history.h"
#include "desktop/hotlist.h"

#include "gtk/compat.h"
#include "gtk/toolbar_items.h"
#include "gtk/menu.h"
#include "gtk/local_history.h"
#include "gtk/gui.h"
#include "gtk/download.h"
#include "gtk/window.h"
#include "gtk/warn.h"
#include "gtk/tabs.h"
#include "gtk/resources.h"
#include "gtk/scaffolding.h"


/**
 * menu entry context
 */
struct nsgtk_menu {
	/** action activated by all menus and the keyboard shortcut */
	GSimpleAction *action;
	/**
	 * menu item handler
	 */
	gboolean (*mhandler)(GtkWidget *widget, gpointer data);
	bool sensitivity; /* menu item is sensitive */
};

/**
 * Core scaffolding structure.
 */
struct nsgtk_scaffolding {
	/** global linked list of scaffolding for gui interface adjustments */
	struct nsgtk_scaffolding *next, *prev;

	/** currently active gui browsing context */
	struct gui_window *top_level;

	/** Builder object scaffold was created from */
	GtkBuilder *builder;

	/** scaffold container window */
	GtkWindow *window;

	/** tab widget holding displayed pages */
	GtkNotebook *notebook;

	/** handler id for tabs remove callback */
	gulong tabs_remove_handler_id;

	/** menu bar */
	GtkWidget *menu_bar;

	/** actions activated by the menus */
	GSimpleActionGroup *actions;

	/** menu entries for sensitivity adjustment */
	struct nsgtk_menu menus[PLACEHOLDER_BUTTON];
};

/**
 * current scaffold for model dialogue use
 */
static struct nsgtk_scaffolding *scaf_current;

/**
 * global list for interface changes
 */
static struct nsgtk_scaffolding *scaf_list = NULL;

/**
 * holds the context data for what's under the pointer, when the
 *  contextual menu is opened.
 */
static struct browser_window_features current_menu_features;


/**
 * resource cleanup function for window destruction.
 *
 * gtk event called when window is being destroyed. Need to free any
 * resources associated with this scaffold,
 *
 * \param widget the widget being destroyed
 * \param data The context pointer passed when the connection was made.
 */
static void scaffolding_window_destroy(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *gs = data;

	NSLOG(netsurf, INFO, "scaffold:%p", gs);

	nsgtk_local_history_hide();

	/* ensure scaffolding being destroyed is not current */
	if (scaf_current == gs) {
		scaf_current = NULL;
		/* attempt to select nearest scaffold instead of just selecting
		 * the first */
		if (gs->prev != NULL) {
			scaf_current = gs->prev;
		} else if (gs->next != NULL) {
			scaf_current = gs->next;
		}
	}

	/* remove scaffolding from list */
	if (gs->prev != NULL) {
		gs->prev->next = gs->next;
	} else {
		scaf_list = gs->next;
	}
	if (gs->next != NULL) {
		gs->next->prev = gs->prev;
	}

	NSLOG(netsurf, INFO, "scaffold list head: %p", scaf_list);

	g_signal_handler_disconnect(gs->notebook, gs->tabs_remove_handler_id);
	g_object_unref(gs->notebook);

	g_object_unref(gs->actions);

	free(gs);

	if (scaf_list == NULL) {
		/* no more open windows - stop the browser */
		nsgtk_complete = true;
	}
}


/**
 * gtk event callback on window close request.
 *
 * prevent window close if download is in progress
 *
 * \param window The window being closed
 * \param data The context pointer passed when the connection was made.
 * \return TRUE to indicate the close request is handled.
 */
static gboolean
scaffolding_window_close_request(GtkWindow *window, gpointer data)
{
	struct nsgtk_scaffolding *g = data;

	if (nsgtk_check_for_downloads(window) == false) {
		gtk_window_destroy(g->window);
	}
	return TRUE;
}


/**
 * Update the scaffolding controls
 *
 * The button sensitivity, url bar and local history visibility are updated
 *
 * \param g The scaffolding context to update
 */
static void scaffolding_update_context(struct nsgtk_scaffolding *g)
{
	struct browser_window *bw = nsgtk_get_browser_window(g->top_level);

	g->menus[BACK_BUTTON].sensitivity =
		browser_window_history_back_available(bw);
	g->menus[FORWARD_BUTTON].sensitivity =
		browser_window_history_forward_available(bw);

	nsgtk_scaffolding_set_sensitivity(g);

	nsgtk_local_history_hide();
}


/**
 * edit the sensitivity of focused widget
 *
 * \todo this needs to update toolbar sensitivity
 *
 * \param g The scaffolding context.
 */
static guint
nsgtk_scaffolding_update_edit_actions_sensitivity(struct nsgtk_scaffolding *g)
{
	GtkWidget *widget = gtk_window_get_focus(g->window);

	if (GTK_IS_EDITABLE(widget)) {
		gboolean has_selection;
		has_selection = gtk_editable_get_selection_bounds(
			GTK_EDITABLE(widget), NULL, NULL);
		g->menus[COPY_BUTTON].sensitivity = has_selection;
		g->menus[CUT_BUTTON].sensitivity = has_selection;
		g->menus[PASTE_BUTTON].sensitivity = true;
	} else {
		struct browser_window *bw = nsgtk_get_browser_window(
			g->top_level);
		browser_editor_flags edit_f = browser_window_get_editor_flags(
			bw);

		g->menus[COPY_BUTTON].sensitivity = edit_f & BW_EDITOR_CAN_COPY;
		g->menus[CUT_BUTTON].sensitivity = edit_f & BW_EDITOR_CAN_CUT;
		g->menus[PASTE_BUTTON].sensitivity = edit_f &
						     BW_EDITOR_CAN_PASTE;
	}

	nsgtk_scaffolding_set_sensitivity(g);

	return ((g->menus[COPY_BUTTON].sensitivity) |
		(g->menus[CUT_BUTTON].sensitivity) |
		(g->menus[PASTE_BUTTON].sensitivity));
}


/**
 * make edit actions sensitive
 *
 * \todo toolbar sensitivity
 *
 * \param g The scaffolding context.
 */
static void
nsgtk_scaffolding_enable_edit_actions_sensitivity(struct nsgtk_scaffolding *g)
{
	g->menus[PASTE_BUTTON].sensitivity = true;
	g->menus[COPY_BUTTON].sensitivity = true;
	g->menus[CUT_BUTTON].sensitivity = true;
	nsgtk_scaffolding_set_sensitivity(g);
}

/* signal handling functions for the toolbar, URL bar, and menu bar */

static gboolean unparent_popover(gpointer data)
{
	gtk_widget_unparent(GTK_WIDGET(data));
	g_object_unref(data);
	return G_SOURCE_REMOVE;
}

/**
 * gtk event handler for a popup menu being closed.
 *
 * \param popover The popup menu
 * \param g scaffolding handle
 */
static void
nsgtk_window_popup_menu_closed(GtkPopover *popover, struct nsgtk_scaffolding *g)
{
	nsgtk_scaffolding_enable_edit_actions_sensitivity(g);
	g_object_ref(popover);
	g_idle_add(unparent_popover, popover);
}

/**
 * Open a popup menu on a widget.
 *
 * \param g scaffolding handle
 * \param model The menu to show, the reference is taken
 * \param widget The widget the menu belongs to
 * \param point true to open the menu at x, y in the widget
 * \param x The horizontal position in the widget
 * \param y The vertical position in the widget
 */
static void nsgtk_scaffolding_popup(struct nsgtk_scaffolding *g,
				    GMenuModel *model,
				    GtkWidget *widget,
				    bool point,
				    gdouble x,
				    gdouble y)
{
	GtkWidget *popover;

	popover = gtk_popover_menu_new_from_model(model);
	g_object_unref(model);
	gtk_widget_set_parent(popover, widget);
	if (point) {
		GdkRectangle rect = {(int)x, (int)y, 1, 1};
		gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
		gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
		gtk_widget_set_halign(popover, GTK_ALIGN_START);
	}
	g_signal_connect(popover,
			 "closed",
			 G_CALLBACK(nsgtk_window_popup_menu_closed),
			 g);
	gtk_popover_popup(GTK_POPOVER(popover));
}


/**
 * Update the menus when the number of tabs changes.
 *
 * \todo toolbar sensitivity
 * \todo next/previous tab ought to only be visible if there is such a tab
 *
 * \param notebook The notebook all the tabs are in
 * \param page The newly added page container widget
 * \param page_num The index of the newly added page
 * \param g The scaffolding context containing the notebook
 */
static void nsgtk_window_tabs_add(GtkNotebook *notebook,
				  GtkWidget *page,
				  guint page_num,
				  struct nsgtk_scaffolding *g)
{
	gboolean visible = gtk_notebook_get_show_tabs(g->notebook);

	g->menus[NEXTTAB_BUTTON].sensitivity = visible;
	g->menus[PREVTAB_BUTTON].sensitivity = visible;
	g->menus[CLOSETAB_BUTTON].sensitivity = visible;

	nsgtk_scaffolding_set_sensitivity(g);
}


/**
 * Update the menus when the number of tabs changes.
 *
 * \todo toolbar sensitivity
 *
 * \param notebook The notebook all the tabs are in
 * \param page The page container widget being removed
 * \param page_num The index of the removed page
 * \param gs The scaffolding context containing the notebook
 */
static void nsgtk_window_tabs_remove(GtkNotebook *notebook,
				     GtkWidget *page,
				     guint page_num,
				     struct nsgtk_scaffolding *gs)
{
	gboolean visible;

	/* if the scaffold is being destroyed it is not useful to
	 * update the state, further many of the widgets may have
	 * already been destroyed.
	 */
	if (gtk_widget_in_destruction(GTK_WIDGET(gs->window)) == TRUE) {
		return;
	}

	/* if this is the last tab destroy the scaffold in addition */
	if (gtk_notebook_get_n_pages(notebook) == 1) {
		gtk_window_destroy(gs->window);
		return;
	}

	visible = gtk_notebook_get_show_tabs(gs->notebook);

	gs->menus[NEXTTAB_BUTTON].sensitivity = visible;
	gs->menus[PREVTAB_BUTTON].sensitivity = visible;
	gs->menus[CLOSETAB_BUTTON].sensitivity = visible;

	nsgtk_scaffolding_set_sensitivity(gs);
}

/* signal handlers for menu entries */

/**
 * handle menu activate signals by calling toolbar item activation
 */
#define TOOLBAR_ITEM_p(identifier, name)                                       \
	static gboolean nsgtk_on_##name##_activate_menu(GtkWidget *widget,     \
							gpointer data)         \
	{                                                                      \
		struct nsgtk_scaffolding *gs = (struct nsgtk_scaffolding *)    \
			data;                                                  \
		nsgtk_window_item_activate(gs->top_level, identifier);         \
		return TRUE;                                                   \
	}
#define TOOLBAR_ITEM_y(identifier, name)
#define TOOLBAR_ITEM_n(identifier, name)
#define TOOLBAR_ITEM(                                                          \
	identifier, name, sensitivity, clicked, activate, label, iconame)      \
	TOOLBAR_ITEM_##activate(identifier, name)
#include "gtk/toolbar_items.h"
#undef TOOLBAR_ITEM_y
#undef TOOLBAR_ITEM_n
#undef TOOLBAR_ITEM_p
#undef TOOLBAR_ITEM


static gboolean
nsgtk_on_savelink_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;
	struct gui_window *gui = g->top_level;
	struct browser_window *bw = nsgtk_get_browser_window(gui);
	nserror err;

	if (current_menu_features.link == NULL)
		return FALSE;

	err = browser_window_navigate(bw,
				      current_menu_features.link,
				      NULL,
				      BW_NAVIGATE_DOWNLOAD,
				      NULL,
				      NULL,
				      NULL);
	if (err != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(err), 0);
	}

	return TRUE;
}


/**
 * Handler for opening new window from a link. attached to the popup menu.
 */
static gboolean
nsgtk_on_link_openwin_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;
	struct gui_window *gui = g->top_level;
	struct browser_window *bw = nsgtk_get_browser_window(gui);
	nserror err;

	if (current_menu_features.link == NULL)
		return FALSE;

	err = browser_window_create(BW_CREATE_CLONE | BW_CREATE_HISTORY,
				    current_menu_features.link,
				    NULL,
				    bw,
				    NULL);
	if (err != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(err), 0);
	}

	return TRUE;
}


/**
 * Handler for opening new tab from a link. attached to the popup menu.
 */
static gboolean
nsgtk_on_link_opentab_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;
	struct gui_window *gui = g->top_level;
	struct browser_window *bw = nsgtk_get_browser_window(gui);
	nserror err;

	if (current_menu_features.link == NULL)
		return FALSE;

	err = browser_window_create(BW_CREATE_CLONE | BW_CREATE_HISTORY |
					    BW_CREATE_TAB,
				    current_menu_features.link,
				    NULL,
				    bw,
				    NULL);
	if (err != NSERROR_OK) {
		nsgtk_warning(messages_get_errorcode(err), 0);
	}

	return TRUE;
}


/**
 * Handler for bookmarking a link. attached to the popup menu.
 */
static gboolean
nsgtk_on_link_bookmark_activate_menu(GtkWidget *widget, gpointer data)
{
	if (current_menu_features.link == NULL)
		return FALSE;

	hotlist_add_url(current_menu_features.link);

	return TRUE;
}


/**
 * Handler for copying a link. attached to the popup menu.
 */
static gboolean
nsgtk_on_link_copy_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;

	if (current_menu_features.link == NULL)
		return FALSE;

	gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(g->window)),
			       nsurl_access(current_menu_features.link));

	return TRUE;
}


static gboolean nsgtk_on_find_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;

	nsgtk_window_search_toggle(g->top_level);

	return TRUE;
}

static nserror get_bar_show(bool *menu, bool *tool)
{
	const char *cur_bar_show;

	*menu = false;
	*tool = false;

	cur_bar_show = nsoption_charp(bar_show);
	if (cur_bar_show != NULL) {
		if (strcmp(cur_bar_show, "menu/tool") == 0) {
			*menu = true;
			*tool = true;
		} else if (strcmp(cur_bar_show, "menu") == 0) {
			*menu = true;
		} else if (strcmp(cur_bar_show, "tool") == 0) {
			*tool = true;
		}
	}

	return NSERROR_OK;
}

static nserror set_bar_show(const char *bar, bool show)
{
	bool menu;
	bool tool;
	const char *new_bar_show;

	get_bar_show(&menu, &tool);

	if (strcmp(bar, "menu") == 0) {
		menu = show;
	} else if (strcmp(bar, "tool") == 0) {
		tool = show;
	}

	if ((menu == true) && (tool == true)) {
		new_bar_show = "menu/tool";
	} else if (menu == true) {
		new_bar_show = "menu";
	} else if (tool == true) {
		new_bar_show = "tool";
	} else {
		new_bar_show = "none";
	}
	nsoption_set_charp(bar_show, strdup(new_bar_show));

	return NSERROR_OK;
}

/**
 * Show or hide the menu bar when the state of its toggle action changes.
 */
static void nsgtk_on_menubar_change_state(GSimpleAction *action,
					  GVariant *state,
					  gpointer data)
{
	struct nsgtk_scaffolding *gs = (struct nsgtk_scaffolding *)data;
	bool show = g_variant_get_boolean(state);

	g_simple_action_set_state(action, state);
	gtk_widget_set_visible(gs->menu_bar, show);
	set_bar_show("menu", show);
}


/**
 * Show or hide the toolbar when the state of its toggle action changes.
 */
static void nsgtk_on_toolbar_change_state(GSimpleAction *action,
					  GVariant *state,
					  gpointer data)
{
	struct nsgtk_scaffolding *gs = (struct nsgtk_scaffolding *)data;
	bool show = g_variant_get_boolean(state);

	g_simple_action_set_state(action, state);
	nsgtk_window_toolbar_show(gs, show);
	set_bar_show("tool", show);
}


static gboolean nsgtk_on_nexttab_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;

	nsgtk_tab_next(g->notebook);

	return TRUE;
}


static gboolean nsgtk_on_prevtab_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;

	nsgtk_tab_prev(g->notebook);

	return TRUE;
}


/**
 * menu signal handler for activation on close tab item
 */
static gboolean
nsgtk_on_closetab_activate_menu(GtkWidget *widget, gpointer data)
{
	struct nsgtk_scaffolding *g = (struct nsgtk_scaffolding *)data;

	nsgtk_tab_close_current(g->notebook);

	return TRUE;
}

/* end of menu callback handlers */


/**
 * Context for an action that calls a menu entry handler.
 */
struct nsgtk_menu_action {
	struct nsgtk_scaffolding *gs;
	gboolean (*mhandler)(GtkWidget *widget, gpointer data);
};

static void nsgtk_menu_action_free(gpointer data, GClosure *closure)
{
	g_free(data);
}

static void nsgtk_menu_action_activate(GSimpleAction *action,
				       GVariant *parameter,
				       gpointer data)
{
	struct nsgtk_menu_action *ma = data;

	ma->mhandler(GTK_WIDGET(ma->gs->window), ma->gs);
}

/**
 * Add an action to the scaffolding which calls a menu entry handler.
 */
static GSimpleAction *
nsgtk_menu_add_action(struct nsgtk_scaffolding *gs,
		      const char *name,
		      gboolean (*mhandler)(GtkWidget *widget, gpointer data))
{
	GSimpleAction *action;
	struct nsgtk_menu_action *ma;

	ma = g_new(struct nsgtk_menu_action, 1);
	ma->gs = gs;
	ma->mhandler = mhandler;

	action = g_simple_action_new(name, NULL);
	g_signal_connect_data(action,
			      "activate",
			      G_CALLBACK(nsgtk_menu_action_activate),
			      ma,
			      nsgtk_menu_action_free,
			      0);
	g_action_map_add_action(G_ACTION_MAP(gs->actions), G_ACTION(action));
	g_object_unref(action);
	return action;
}


/**
 * initialiase the menu handlers and create their actions
 */
static nserror nsgtk_menu_initialise(struct nsgtk_scaffolding *g)
{
#define TOOLBAR_ITEM_p(identifier, name)                                       \
	g->menus[identifier].mhandler = nsgtk_on_##name##_activate_menu;
#define TOOLBAR_ITEM_y(identifier, name)                                       \
	g->menus[identifier].mhandler = nsgtk_on_##name##_activate_menu;
#define TOOLBAR_ITEM_n(identifier, name) g->menus[identifier].mhandler = NULL;
#define TOOLBAR_ITEM(                                                          \
	identifier, name, snstvty, clicked, activate, label, iconame)          \
	g->menus[identifier].sensitivity = snstvty;                            \
	TOOLBAR_ITEM_##activate(                                               \
		identifier, name) if (g->menus[identifier].mhandler != NULL)   \
	{                                                                      \
		g->menus[identifier].action = nsgtk_menu_add_action(           \
			g, #name, g->menus[identifier].mhandler);              \
	}
#include "gtk/toolbar_items.h"
#undef TOOLBAR_ITEM_y
#undef TOOLBAR_ITEM_n
#undef TOOLBAR_ITEM_p
#undef TOOLBAR_ITEM

	nsgtk_menu_add_action(g,
			      "link-opentab",
			      nsgtk_on_link_opentab_activate_menu);
	nsgtk_menu_add_action(g,
			      "link-openwin",
			      nsgtk_on_link_openwin_activate_menu);
	nsgtk_menu_add_action(g, "link-save", nsgtk_on_savelink_activate_menu);
	nsgtk_menu_add_action(g,
			      "link-bookmark",
			      nsgtk_on_link_bookmark_activate_menu);
	nsgtk_menu_add_action(g, "link-copy", nsgtk_on_link_copy_activate_menu);

	return NSERROR_OK;
}


/**
 * Add a toggle action to the scaffolding.
 */
static void nsgtk_menu_add_toggle(struct nsgtk_scaffolding *gs,
				  const char *name,
				  bool state,
				  void (*change_state)(GSimpleAction *action,
						       GVariant *state,
						       gpointer data))
{
	GSimpleAction *action;

	action = g_simple_action_new_stateful(name,
					      NULL,
					      g_variant_new_boolean(state));
	g_signal_connect(action, "change-state", G_CALLBACK(change_state), gs);
	g_action_map_add_action(G_ACTION_MAP(gs->actions), G_ACTION(action));
	g_object_unref(action);
}


/**
 * create and initialise menus
 *
 * There are four menus held by the scaffolding:
 *
 *  1. Main menubar menu.
 *     This can be hidden which causes the right click popup context menu
 *       to use the burger menu.
 *  2. Burger menu.
 *     This can be opened from a burger icon on the toolbar.
 *  3. popup context menu.
 *     This is opened by right mouse clicking on the toolbar or browser area
 *  4. link context menu
 *     Opened like the other popup menu when the mouse is over a link in the
 *        browser area
 *
 * All menu entries activate actions in the "win" group of the window, so the
 * sensitivity of an entry is the enabled state of its action.
 */
static nserror nsgtk_menus_create(struct nsgtk_scaffolding *gs)
{
	GMenuModel *model;
	bool showmenu; /* show menubar */
	bool showtool; /* show toolbar */

	get_bar_show(&showmenu, &showtool);

	gs->actions = g_simple_action_group_new();
	nsgtk_menu_initialise(gs);
	nsgtk_menu_add_toggle(
		gs, "showmenubar", showmenu, nsgtk_on_menubar_change_state);
	nsgtk_menu_add_toggle(
		gs, "showtoolbar", showtool, nsgtk_on_toolbar_change_state);
	gtk_widget_insert_action_group(GTK_WIDGET(gs->window),
				       "win",
				       G_ACTION_GROUP(gs->actions));

	gs->menu_bar = GTK_WIDGET(
		gtk_builder_get_object(gs->builder, "menubar"));
	model = nsgtk_menu_bar_model();
	gtk_popover_menu_bar_set_menu_model(GTK_POPOVER_MENU_BAR(gs->menu_bar),
					    model);
	g_object_unref(model);
	gtk_widget_set_visible(gs->menu_bar, showmenu);

	nsgtk_menu_add_shortcuts(GTK_WIDGET(gs->window));

	nsgtk_scaffolding_set_sensitivity(gs);

	return NSERROR_OK;
}


/* exported function documented in gtk/scaffolding.h */
void nsgtk_scaffolding_set_title(struct gui_window *gw, const char *title)
{
	struct nsgtk_scaffolding *gs = nsgtk_get_scaffold(gw);
	int title_len;
	char *newtitle;

	/* only set window title if top level window */
	if (gs->top_level != gw) {
		return;
	}

	if (title == NULL || title[0] == '\0') {
		gtk_window_set_title(gs->window, "NetSurf");
		return;
	}

	title_len = strlen(title) + SLEN(" - NetSurf") + 1;
	newtitle = malloc(title_len);
	if (newtitle == NULL) {
		return;
	}

	snprintf(newtitle, title_len, "%s - NetSurf", title);

	gtk_window_set_title(gs->window, newtitle);

	free(newtitle);
}


/* exported interface documented in scaffolding.h */
nserror nsgtk_scaffolding_throbber(struct gui_window *gw, bool active)
{
	struct nsgtk_scaffolding *gs = nsgtk_get_scaffold(gw);
	if (active) {
		gs->menus[STOP_BUTTON].sensitivity = true;
		gs->menus[RELOAD_BUTTON].sensitivity = false;
	} else {
		gs->menus[STOP_BUTTON].sensitivity = false;
		gs->menus[RELOAD_BUTTON].sensitivity = true;
	}
	scaffolding_update_context(gs);

	return NSERROR_OK;
}


/* exported interface documented in gtk/scaffolding.h */
nserror nsgtk_scaffolding_destroy_all(void)
{
	struct nsgtk_scaffolding *gs;

	gs = scaf_list;
	assert(gs != NULL);

	if (nsgtk_check_for_downloads(gs->window) == true) {
		return NSERROR_INVALID;
	}

	/* iterate all scaffolding windows and destroy them */
	while (gs != NULL) {
		struct nsgtk_scaffolding *next = gs->next;
		gtk_window_destroy(gs->window);
		gs = next;
	}
	return NSERROR_OK;
}


/* exported interface documented in gtk/scaffolding.h */
GtkWindow *nsgtk_scaffolding_window(struct nsgtk_scaffolding *g)
{
	return g->window;
}

/* exported interface documented in gtk/scaffolding.h */
GtkNotebook *nsgtk_scaffolding_notebook(struct nsgtk_scaffolding *g)
{
	return g->notebook;
}


/* exported interface documented in gtk/scaffolding.h */
GtkWidget *nsgtk_scaffolding_menu_bar(struct nsgtk_scaffolding *gs)
{
	if (gs == NULL) {
		return NULL;
	}
	return gs->menu_bar;
}

/* exported interface documented in gtk/scaffolding.h */
struct nsgtk_scaffolding *nsgtk_scaffolding_iterate(struct nsgtk_scaffolding *g)
{
	if (g == NULL) {
		return scaf_list;
	}
	return g->next;
}


/* exported interface documented in gtk/scaffolding.h */
struct gui_window *nsgtk_scaffolding_top_level(struct nsgtk_scaffolding *g)
{
	return g->top_level;
}


/* exported interface documented in gtk/scaffolding.h */
void nsgtk_scaffolding_set_top_level(struct gui_window *gw)
{
	struct browser_window *bw;
	struct nsgtk_scaffolding *sc;

	assert(gw != NULL);

	bw = nsgtk_get_browser_window(gw);

	assert(bw != NULL);

	sc = nsgtk_get_scaffold(gw);
	assert(sc != NULL);

	scaf_current = sc;

	sc->top_level = gw;

	/* Synchronise the history */
	scaffolding_update_context(sc);

	/* Ensure the window's title bar is updated */
	nsgtk_scaffolding_set_title(gw, browser_window_get_title(bw));
}


/* exported interface documented in scaffolding.h */
void nsgtk_scaffolding_set_sensitivity(struct nsgtk_scaffolding *g)
{
	int i;

	for (i = BACK_BUTTON; i < PLACEHOLDER_BUTTON; i++) {
		if (g->menus[i].action != NULL) {
			g_simple_action_set_enabled(g->menus[i].action,
						    g->menus[i].sensitivity);
		}
	}
}


/* exported interface documented in gtk/scaffolding.h */
nserror nsgtk_scaffolding_toolbar_context_menu(struct nsgtk_scaffolding *gs,
					       GtkWidget *widget,
					       gdouble x,
					       gdouble y)
{
	nsgtk_scaffolding_popup(
		gs, nsgtk_popup_menu_model(false, false), widget, true, x, y);

	return NSERROR_OK;
}


/* exported interface documented in gtk/scaffolding.h */
nserror
nsgtk_scaffolding_burger_menu(struct nsgtk_scaffolding *gs, GtkWidget *widget)
{
	nsgtk_scaffolding_popup(
		gs, nsgtk_burger_menu_model(), widget, false, 0, 0);

	return NSERROR_OK;
}


/* exported interface documented in gtk/scaffolding.h */
void nsgtk_scaffolding_context_menu(struct nsgtk_scaffolding *g,
				    GtkWidget *widget,
				    gdouble wx,
				    gdouble wy,
				    gdouble x,
				    gdouble y)
{
	GMenuModel *model;
	struct browser_window *bw;

	bw = nsgtk_get_browser_window(g->top_level);

	/* update the global context menu features */
	browser_window_get_features(bw, x, y, &current_menu_features);

	if (current_menu_features.link != NULL) {
		/* menu is opening over a link */
		model = nsgtk_link_menu_model();
	} else {
		nsgtk_scaffolding_update_edit_actions_sensitivity(g);
		model = nsgtk_popup_menu_model(true, true);
	}

	nsgtk_scaffolding_popup(g, model, widget, true, wx, wy);
}

/* exported interface documented in gtk/scaffolding.h */
struct nsgtk_scaffolding *nsgtk_current_scaffolding(void)
{
	if (scaf_current == NULL) {
		scaf_current = scaf_list;
	}
	return scaf_current;
}

/* exported interface documented in gtk/scaffolding.h */
struct nsgtk_scaffolding *nsgtk_scaffolding_from_notebook(GtkNotebook *notebook)
{
	struct nsgtk_scaffolding *gs;
	gs = scaf_list;
	while (gs != NULL) {
		if (gs->notebook == notebook) {
			break;
		}
		gs = gs->next;
	}
	return gs;
}

/* exported interface documented in gtk/scaffolding.h */
struct nsgtk_scaffolding *nsgtk_new_scaffolding(struct gui_window *toplevel)
{
	nserror res;
	struct nsgtk_scaffolding *gs;

	gs = calloc(1, sizeof(*gs));
	if (gs == NULL) {
		return NULL;
	}

	NSLOG(netsurf,
	      INFO,
	      "Constructing a scaffold of %p for gui_window %p",
	      gs,
	      toplevel);

	gs->top_level = toplevel;

	/* Construct UI widgets */
	if (nsgtk_builder_new_from_resname("netsurf", &gs->builder) !=
	    NSERROR_OK) {
		free(gs);
		return NULL;
	}

	/* containing window setup */
	gs->window = GTK_WINDOW(
		gtk_builder_get_object(gs->builder, "wndBrowser"));

	/**
	 * set this window's size to what's in the options, or
	 *   some sensible default if they are not set yet.
	 */
	if (nsoption_int(window_width) > 0) {
		gtk_window_set_default_size(gs->window,
					    nsoption_int(window_width),
					    nsoption_int(window_height));
	} else {
		/* Set to 1000x700, so we're very likely to fit even on
		 * 1024x768 displays, not being able to take into account
		 * window furniture or panels.
		 */
		gtk_window_set_default_size(gs->window, 1000, 700);
	}

	g_signal_connect(gs->window,
			 "close-request",
			 G_CALLBACK(scaffolding_window_close_request),
			 gs);

	g_signal_connect(gs->window,
			 "destroy",
			 G_CALLBACK(scaffolding_window_destroy),
			 gs);


	/* notebook */
	res = nsgtk_notebook_create(gs->builder, &gs->notebook);
	if (res != NSERROR_OK) {
		free(gs);
		return NULL;
	}

	g_signal_connect_after(gs->notebook,
			       "page-added",
			       G_CALLBACK(nsgtk_window_tabs_add),
			       gs);
	gs->tabs_remove_handler_id = g_signal_connect_after(
		gs->notebook,
		"page-removed",
		G_CALLBACK(nsgtk_window_tabs_remove),
		gs);
	g_object_ref(gs->notebook);


	res = nsgtk_menus_create(gs);
	if (res != NSERROR_OK) {
		free(gs);
		return NULL;
	}

	/* attach to the list */
	if (scaf_list) {
		scaf_list->prev = gs;
	}
	gs->next = scaf_list;
	gs->prev = NULL;
	scaf_list = gs;

	g_object_unref(gs->builder);
	gs->builder = NULL;

	/* finally, show the window. */
	gtk_window_present(gs->window);

	NSLOG(netsurf, INFO, "creation complete");

	return gs;
}
