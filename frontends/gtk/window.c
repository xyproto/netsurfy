/*
 * Copyright 2006 Daniel Silverstone <dsilvers@digital-scurf.org>
 * Copyright 2006 Rob Kendrick <rjek@rjek.com>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Implementation of gtk windowing.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <math.h>
#include <gtk/gtk.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/utf8.h"
#include "utils/nsoption.h"
#include "utils/messages.h"
#include "utils/nsurl.h"
#include "netsurf/inttypes.h"
#include "netsurf/content.h"
#include "netsurf/browser_window.h"
#include "netsurf/mouse.h"
#include "netsurf/window.h"
#include "netsurf/plotters.h"
#include "netsurf/form.h"
#include "netsurf/keypress.h"
#include "desktop/searchweb.h"
#include "desktop/textinput.h"

#include "gtk/selection.h"
#include "gtk/warn.h"
#include "gtk/compat.h"
#include "gtk/gui.h"
#include "gtk/scaffolding.h"
#include "gtk/toolbar_items.h"
#include "gtk/toolbar.h"
#include "gtk/local_history.h"
#include "gtk/plotters.h"
#include "gtk/schedule.h"
#include "gtk/tabs.h"
#include "gtk/bitmap.h"
#include "gtk/gdk.h"
#include "gtk/resources.h"
#include "gtk/search.h"
#include "gtk/throbber.h"
#include "gtk/window.h"

/**
 * time (in ms) between throbber animation frame updates
 */
#define THROBBER_FRAME_TIME (100)

static GtkWidget *select_menu;
static struct form_control *select_menu_control;

struct gui_window {
	/**
	 * The gtk scaffold object containing menu, buttons, url bar, [tabs],
	 * drawing area, etc that may contain one or more gui_windows.
	 */
	struct nsgtk_scaffolding *scaffold;

	/** The 'content' window that is rendered in the gui_window */
	struct browser_window *bw;

	/** mouse state and events. */
	struct {
		struct gui_window *gui;

		gdouble pressed_x;
		gdouble pressed_y;
		gboolean waiting;
		browser_mouse_state state;
	} mouse;

	/** last pointer position in the drawing area */
	gdouble pointer_x, pointer_y;

	/** caret dimension and location for rendering */
	int caretx, carety, careth;

	/** caret shape for rendering */
	gui_pointer_shape current_pointer;

	/** previous event location */
	int last_x, last_y;

	/** controls toolbar context */
	struct nsgtk_toolbar *toolbar;

	/** search toolbar context */
	struct gtk_search *search;

	/** The top level container (tabBox) */
	GtkWidget *container;

	/** display widget for this page or frame */
	GtkWidget *layout;

	/** scroll position of the content */
	GtkAdjustment *hadj, *vadj;

	/** size of the content */
	int extent_width, extent_height;

	/** The container for the layout etc */
	GtkWidget *grid;

	/** handle to the the visible tab */
	GtkWidget *tab;

	/** statusbar */
	GtkLabel *status_bar;

	/** status pane */
	GtkPaned *paned;

	/** The icon this window should have */
	GdkPixbuf *icon;

	/** The input method to use with this window */
	GtkIMContext *input_method;

	/** current frame of throbber */
	int throb_frame;

	/** list for cleanup */
	struct gui_window *next, *prev;
};

/**< first entry in window list */
struct gui_window *window_list = NULL;

static void
nsgtk_select_menu_clicked(GtkCheckButton *button, gpointer user_data)
{
	form_select_process_selection(select_menu_control, (intptr_t)user_data);
	gtk_popover_popdown(GTK_POPOVER(select_menu));
}

static void nsgtk_window_draw(GtkDrawingArea *area,
			      cairo_t *cr,
			      int width,
			      int height,
			      gpointer data)
{
	struct gui_window *gw = data;
	struct gui_window *z;
	struct rect clip;
	struct redraw_context ctx = {.interactive = true,
				     .background_images = true,
				     .plot = &nsgtk_plotters};

	double x1;
	double y1;
	double x2;
	double y2;

	assert(gw);
	assert(gw->bw);

	for (z = window_list; z && z != gw; z = z->next)
		continue;
	assert(z);
	assert(gw->layout == GTK_WIDGET(area));

	/* the default background colour of the drawing area is white */
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	current_cr = cr;

	cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

	clip.x0 = x1;
	clip.y0 = y1;
	clip.x1 = x2;
	clip.y1 = y2;

	browser_window_redraw(gw->bw,
			      -gtk_adjustment_get_value(gw->hadj),
			      -gtk_adjustment_get_value(gw->vadj),
			      &clip,
			      &ctx);

	if (gw->careth != 0) {
		nsgtk_plot_caret(
			gw->caretx - gtk_adjustment_get_value(gw->hadj),
			gw->carety - gtk_adjustment_get_value(gw->vadj),
			gw->careth);
	}
}

/**
 * Update the scroll adjustments from the size of the drawing area and the
 * size of the content.
 */
static void nsgtk_window_update_adjustments(struct gui_window *g)
{
	int width = gtk_widget_get_width(g->layout);
	int height = gtk_widget_get_height(g->layout);

	gtk_adjustment_configure(g->hadj,
				 gtk_adjustment_get_value(g->hadj),
				 0,
				 MAX(g->extent_width, width),
				 width * 0.1,
				 width * 0.9,
				 width);
	gtk_adjustment_configure(g->vadj,
				 gtk_adjustment_get_value(g->vadj),
				 0,
				 MAX(g->extent_height, height),
				 height * 0.1,
				 height * 0.9,
				 height);
}

static void
nsgtk_window_adjustment_changed(GtkAdjustment *adjustment, gpointer data)
{
	struct gui_window *g = data;

	gtk_widget_queue_draw(g->layout);
}

static void nsgtk_window_motion(GtkEventControllerMotion *controller,
				gdouble x,
				gdouble y,
				gpointer data)
{
	struct gui_window *g = data;
	GdkModifierType state;
	bool shift;
	bool ctrl;

	state = gtk_event_controller_get_current_event_state(
		GTK_EVENT_CONTROLLER(controller));
	shift = state & GDK_SHIFT_MASK;
	ctrl = state & GDK_CONTROL_MASK;

	g->pointer_x = x;
	g->pointer_y = y;
	x += gtk_adjustment_get_value(g->hadj);
	y += gtk_adjustment_get_value(g->vadj);

	if ((fabs(x - g->last_x) < 5.0) && (fabs(y - g->last_y) < 5.0)) {
		/* Mouse hasn't moved far enough from press coordinate
		 * for this to be considered a drag.
		 */
		return;
	} else {
		/* This is a drag, ensure it's always treated as such,
		 * even if we drag back over the press location.
		 */
		g->last_x = INT_MIN;
		g->last_y = INT_MIN;
	}

	if (g->mouse.state & BROWSER_MOUSE_PRESS_1) {
		/* Start button 1 drag */
		browser_window_mouse_click(g->bw,
					   BROWSER_MOUSE_DRAG_1,
					   g->mouse.pressed_x,
					   g->mouse.pressed_y);

		/* Replace PRESS with HOLDING and declare drag in progress */
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_1 |
				   BROWSER_MOUSE_HOLDING_1);
		g->mouse.state |= BROWSER_MOUSE_DRAG_ON;
	} else if (g->mouse.state & BROWSER_MOUSE_PRESS_2) {
		/* Start button 2 drag */
		browser_window_mouse_click(g->bw,
					   BROWSER_MOUSE_DRAG_2,
					   g->mouse.pressed_x,
					   g->mouse.pressed_y);

		/* Replace PRESS with HOLDING and declare drag in progress */
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_2 |
				   BROWSER_MOUSE_HOLDING_2);
		g->mouse.state |= BROWSER_MOUSE_DRAG_ON;
	}

	/* Handle modifiers being removed */
	if (g->mouse.state & BROWSER_MOUSE_MOD_1 && !shift)
		g->mouse.state ^= BROWSER_MOUSE_MOD_1;
	if (g->mouse.state & BROWSER_MOUSE_MOD_2 && !ctrl)
		g->mouse.state ^= BROWSER_MOUSE_MOD_2;

	browser_window_mouse_track(g->bw, g->mouse.state, x, y);
}

/**
 * GTK signal handler for the focus leaving the layout
 *
 * when focus leaves the layout widget ensure the caret is cleared
 */
static void
nsgtk_window_focus_leave(GtkEventControllerFocus *controller, gpointer data)
{
	struct gui_window *g = data;

	gtk_im_context_focus_out(g->input_method);
	browser_window_remove_caret(g->bw, true);
}

static void
nsgtk_window_focus_enter(GtkEventControllerFocus *controller, gpointer data)
{
	struct gui_window *g = data;

	gtk_im_context_focus_in(g->input_method);
}

/**
 * GTK signal handler for a mouse button press on the layout
 */
static void nsgtk_window_button_pressed(GtkGestureClick *gesture,
					gint n_press,
					gdouble x,
					gdouble y,
					gpointer data)
{
	struct gui_window *g = data;
	GdkModifierType state;
	guint button;

	button = gtk_gesture_single_get_current_button(
		GTK_GESTURE_SINGLE(gesture));
	state = gtk_event_controller_get_current_event_state(
		GTK_EVENT_CONTROLLER(gesture));

	gtk_im_context_reset(g->input_method);
	gtk_widget_grab_focus(g->layout);
	nsgtk_local_history_hide();

	g->pointer_x = x;
	g->pointer_y = y;
	g->mouse.pressed_x = x + gtk_adjustment_get_value(g->hadj);
	g->mouse.pressed_y = y + gtk_adjustment_get_value(g->vadj);

	switch (button) {
	case 1: /* Left button, usually. Pass to core as BUTTON 1. */
		g->mouse.state = BROWSER_MOUSE_PRESS_1;
		break;

	case 2: /* Middle button, usually. Pass to core as BUTTON 2 */
		g->mouse.state = BROWSER_MOUSE_PRESS_2;
		break;

	case 3: /* Right button, usually. Action button, context menu. */
		/** \todo determine if hiding the caret here is necessary */
		browser_window_remove_caret(g->bw, true);
		nsgtk_scaffolding_context_menu(g->scaffold,
					       g->layout,
					       x,
					       y,
					       g->mouse.pressed_x,
					       g->mouse.pressed_y);
		gtk_gesture_set_state(GTK_GESTURE(gesture),
				      GTK_EVENT_SEQUENCE_CLAIMED);
		return;

	case 8: /* Back button */
		g->mouse.state = BROWSER_MOUSE_PRESS_4;
		break;

	case 9: /* Forward button */
		g->mouse.state = BROWSER_MOUSE_PRESS_5;
		break;

	default:
		return;
	}

	/* Modify for double & triple clicks */
	if (n_press == 3)
		g->mouse.state |= BROWSER_MOUSE_TRIPLE_CLICK;
	else if (n_press == 2)
		g->mouse.state |= BROWSER_MOUSE_DOUBLE_CLICK;

	/* Handle the modifiers too */
	if (state & GDK_SHIFT_MASK)
		g->mouse.state |= BROWSER_MOUSE_MOD_1;
	if (state & GDK_CONTROL_MASK)
		g->mouse.state |= BROWSER_MOUSE_MOD_2;
	if (state & GDK_ALT_MASK)
		g->mouse.state |= BROWSER_MOUSE_MOD_3;

	/* Record where we pressed, for use when determining whether to start
	 * a drag in motion notify events. */
	g->last_x = g->mouse.pressed_x;
	g->last_y = g->mouse.pressed_y;

	browser_window_mouse_click(
		g->bw, g->mouse.state, g->mouse.pressed_x, g->mouse.pressed_y);

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}


static void nsgtk_window_button_released(GtkGestureClick *gesture,
					 gint n_press,
					 gdouble x,
					 gdouble y,
					 gpointer data)
{
	struct gui_window *g = data;
	GdkModifierType state;

	state = gtk_event_controller_get_current_event_state(
		GTK_EVENT_CONTROLLER(gesture));
	x += gtk_adjustment_get_value(g->hadj);
	y += gtk_adjustment_get_value(g->vadj);

	/* If the mouse state is PRESS then we are waiting for a release to emit
	 * a click event, otherwise just reset the state to nothing */
	if (g->mouse.state & BROWSER_MOUSE_PRESS_1) {
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_1 |
				   BROWSER_MOUSE_CLICK_1);
	} else if (g->mouse.state & BROWSER_MOUSE_PRESS_2) {
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_2 |
				   BROWSER_MOUSE_CLICK_2);
	} else if (g->mouse.state & BROWSER_MOUSE_PRESS_3) {
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_3 |
				   BROWSER_MOUSE_CLICK_3);
	} else if (g->mouse.state & BROWSER_MOUSE_PRESS_4) {
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_4 |
				   BROWSER_MOUSE_CLICK_4);
	} else if (g->mouse.state & BROWSER_MOUSE_PRESS_5) {
		g->mouse.state ^= (BROWSER_MOUSE_PRESS_5 |
				   BROWSER_MOUSE_CLICK_5);
	}

	/* Handle modifiers being removed */
	if (g->mouse.state & BROWSER_MOUSE_MOD_1 && !(state & GDK_SHIFT_MASK)) {
		g->mouse.state ^= BROWSER_MOUSE_MOD_1;
	}
	if (g->mouse.state & BROWSER_MOUSE_MOD_2 &&
	    !(state & GDK_CONTROL_MASK)) {
		g->mouse.state ^= BROWSER_MOUSE_MOD_2;
	}
	if (g->mouse.state & BROWSER_MOUSE_MOD_3 && !(state & GDK_ALT_MASK)) {
		g->mouse.state ^= BROWSER_MOUSE_MOD_3;
	}

	if (g->mouse.state & (BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_CLICK_2 |
			      BROWSER_MOUSE_CLICK_3 | BROWSER_MOUSE_CLICK_4 |
			      BROWSER_MOUSE_CLICK_5)) {
		browser_window_mouse_click(g->bw, g->mouse.state, x, y);
	} else {
		browser_window_mouse_track(g->bw, 0, x, y);
	}

	g->mouse.state = 0;
}


/**
 * Move a scroll adjustment by a delta, keeping it within its range.
 */
static void nsgtk_adjustment_scroll(GtkAdjustment *adj, double delta)
{
	double value = gtk_adjustment_get_value(adj) + delta;
	double upper = gtk_adjustment_get_upper(adj) -
		       gtk_adjustment_get_page_size(adj);

	if (value > upper) {
		value = upper;
	}
	if (value < gtk_adjustment_get_lower(adj)) {
		value = gtk_adjustment_get_lower(adj);
	}
	gtk_adjustment_set_value(adj, value);
}


static gboolean nsgtk_window_scroll(GtkEventControllerScroll *controller,
				    gdouble deltax,
				    gdouble deltay,
				    gpointer data)
{
	struct gui_window *g = data;

	if (gtk_event_controller_scroll_get_unit(controller) ==
	    GDK_SCROLL_UNIT_WHEEL) {
		deltax *= gtk_adjustment_get_step_increment(g->hadj);
		deltay *= gtk_adjustment_get_step_increment(g->vadj);
	}

	if (browser_window_scroll_at_point(
		    g->bw,
		    g->pointer_x + gtk_adjustment_get_value(g->hadj),
		    g->pointer_y + gtk_adjustment_get_value(g->vadj),
		    deltax,
		    deltay) != true) {

		/* core did not handle event so change adjustments */
		if (deltax != 0) {
			nsgtk_adjustment_scroll(g->hadj, deltax);
		}
		if (deltay != 0) {
			nsgtk_adjustment_scroll(g->vadj, deltay);
		}
	}

	return TRUE;
}


static gboolean nsgtk_window_key_pressed(GtkEventControllerKey *controller,
					 guint keyval,
					 guint keycode,
					 GdkModifierType state,
					 gpointer data)
{
	struct gui_window *g = data;
	uint32_t nskey;
	double value;

	nskey = gtk_gui_gdkkey_to_nskey(keyval, state);

	if (browser_window_key_press(g->bw, nskey))
		return TRUE;

	if ((state & (GDK_SHIFT_MASK | GDK_LOCK_MASK | GDK_CONTROL_MASK)) != 0)
		return FALSE;

	switch (keyval) {

	case GDK_KEY(Home):
	case GDK_KEY(KP_Home):
		gtk_adjustment_set_value(g->vadj,
					 gtk_adjustment_get_lower(g->vadj));
		break;

	case GDK_KEY(End):
	case GDK_KEY(KP_End):
		value = gtk_adjustment_get_upper(g->vadj) -
			gtk_adjustment_get_page_size(g->vadj);
		if (value < gtk_adjustment_get_lower(g->vadj))
			value = gtk_adjustment_get_lower(g->vadj);
		gtk_adjustment_set_value(g->vadj, value);
		break;

	case GDK_KEY(Left):
	case GDK_KEY(KP_Left):
		nsgtk_adjustment_scroll(
			g->hadj, -gtk_adjustment_get_step_increment(g->hadj));
		break;

	case GDK_KEY(Up):
	case GDK_KEY(KP_Up):
		nsgtk_adjustment_scroll(
			g->vadj, -gtk_adjustment_get_step_increment(g->vadj));
		break;

	case GDK_KEY(Right):
	case GDK_KEY(KP_Right):
		nsgtk_adjustment_scroll(
			g->hadj, gtk_adjustment_get_step_increment(g->hadj));
		break;

	case GDK_KEY(Down):
	case GDK_KEY(KP_Down):
		nsgtk_adjustment_scroll(
			g->vadj, gtk_adjustment_get_step_increment(g->vadj));
		break;

	case GDK_KEY(Page_Up):
	case GDK_KEY(KP_Page_Up):
		nsgtk_adjustment_scroll(
			g->vadj, -gtk_adjustment_get_page_increment(g->vadj));
		break;

	case GDK_KEY(Page_Down):
	case GDK_KEY(KP_Page_Down):
		nsgtk_adjustment_scroll(
			g->vadj, gtk_adjustment_get_page_increment(g->vadj));
		break;

	default:
		return FALSE;
	}

	return TRUE;
}


static void nsgtk_window_input_method_commit(GtkIMContext *ctx,
					     const gchar *str,
					     gpointer data)
{
	struct gui_window *g = data;
	size_t len = strlen(str), offset = 0;

	while (offset < len) {
		uint32_t nskey = utf8_to_ucs4(str + offset, len - offset);

		browser_window_key_press(g->bw, nskey);

		offset = utf8_next(str, len, offset);
	}
}


static void nsgtk_window_resize(GtkDrawingArea *area,
				gint width,
				gint height,
				gpointer data)
{
	struct gui_window *g = data;

	nsgtk_window_update_adjustments(g);
	browser_window_schedule_reformat(g->bw);
}


/**
 * when the pane position is changed update the user option
 *
 * The user position is a percentage of the pane total width not an
 * absolute value.
 */
static void
nsgtk_paned_notify__position(GObject *gobject, GParamSpec *pspec, gpointer data)
{
	struct gui_window *g = data;
	int max_position;

	g_object_get(g->paned, "max-position", &max_position, NULL);
	if (max_position <= 0) {
		return;
	}

	nsoption_set_int(toolbar_status_size,
			 ((gtk_paned_get_position(g->paned) * 10000) /
			  max_position));
}


/**
 * Set status bar / scroll bar proportion according to user option
 *   when pane is resized.
 */
static void nsgtk_paned_notify__max_position(GObject *gobject,
					     GParamSpec *pspec,
					     gpointer data)
{
	struct gui_window *g = data;
	int max_position;

	g_object_get(g->paned, "max-position", &max_position, NULL);
	gtk_paned_set_position(
		g->paned,
		(nsoption_int(toolbar_status_size) * max_position) / 10000);
}


/**
 * handler for gtk destroy signal on window container
 *
 * destroy the browsing context as there is will be nothing to display it now
 */
static void window_destroy(GtkWidget *widget, gpointer data)
{
	struct gui_window *gw = data;

	browser_window_destroy(gw->bw);

	g_object_unref(gw->input_method);
	g_object_unref(gw->hadj);
	g_object_unref(gw->vadj);

	/* free any existing icon */
	if (gw->icon != NULL) {
		g_object_unref(gw->icon);
		gw->icon = NULL;
	}

	free(gw);
}


static struct browser_window *bw_from_gw(void *data)
{
	struct gui_window *gw = data;
	return gw->bw;
}


static bool get_tool_bar_show(void)
{
	const char *cur_bar_show;

	cur_bar_show = nsoption_charp(bar_show);
	if (cur_bar_show != NULL) {
		if (strcmp(cur_bar_show, "menu/tool") == 0) {
			return true;
		} else if (strcmp(cur_bar_show, "tool") == 0) {
			return true;
		}
	}
	return false;
}


/**
 * Make the throbber advance to next frame.
 *
 * scheduled callback to update the throbber
 *
 * \param p The context passed when scheduled.
 */
static void next_throbber_frame(void *p)
{
	struct gui_window *gw = p;
	nserror res;
	GdkPixbuf *pixbuf;

	gw->throb_frame++; /* advance to next frame */

	res = nsgtk_throbber_get_frame(gw->throb_frame, &pixbuf);
	if (res == NSERROR_BAD_SIZE) {
		gw->throb_frame = 1;
		res = nsgtk_throbber_get_frame(gw->throb_frame, &pixbuf);
	}

	if (res == NSERROR_OK) {
		nsgtk_tab_set_icon(gw->container, pixbuf);
		/* only schedule next frame if there are no errors */
		nsgtk_schedule(THROBBER_FRAME_TIME, next_throbber_frame, p);
	}
}


/**
 * Create and open a gtk container (window or tab) for a browsing context.
 *
 * \param bw The browsing context to create gui_window for.
 * \param existing An existing gui_window, may be NULL
 * \param flags	flags to control the container creation
 * \return gui window, or NULL on error
 *
 * If GW_CREATE_CLONE flag is set existing is non-NULL.
 *
 * Front end's gui_window must include a reference to the
 * browser window passed in the bw param.
 */
static struct gui_window *gui_window_create(struct browser_window *bw,
					    struct gui_window *existing,
					    gui_window_create_flags flags)
{
	struct gui_window *g; /* what is being created to return */
	bool open_in_background = !(flags & GW_CREATE_FOREGROUND);
	GtkBuilder *tab_builder;
	GtkEventController *controller;
	GtkGesture *gesture;
	nserror res;

	res = nsgtk_builder_new_from_resname("tabcontents", &tab_builder);
	if (res != NSERROR_OK) {
		NSLOG(netsurf, INFO, "Tab contents UI builder init failed");
		return NULL;
	}

	g = calloc(1, sizeof(*g));
	if (!g) {
		nsgtk_warning("NoMemory", 0);
		g_object_unref(tab_builder);
		return NULL;
	}

	NSLOG(netsurf,
	      INFO,
	      "Creating gui window %p for browser window %p",
	      g,
	      bw);

	g->bw = bw;
	g->mouse.state = 0;
	g->current_pointer = GUI_POINTER_DEFAULT;

	/* attach scaffold */
	if (flags & GW_CREATE_TAB) {
		/* open in new tab, attach to existing scaffold */
		if (existing != NULL) {
			g->scaffold = existing->scaffold;
		} else {
			g->scaffold = nsgtk_current_scaffolding();
		}
	} else {
		/* open in new window, create and attach to scaffold */
		g->scaffold = nsgtk_new_scaffolding(g);
	}
	if (g->scaffold == NULL) {
		nsgtk_warning("NoMemory", 0);
		free(g);
		g_object_unref(tab_builder);
		return NULL;
	}

	/* Construct our primary elements */
	g->container = GTK_WIDGET(
		gtk_builder_get_object(tab_builder, "tabBox"));
	g->layout = GTK_WIDGET(gtk_builder_get_object(tab_builder, "layout"));
	g->grid = GTK_WIDGET(
		gtk_builder_get_object(tab_builder, "tabContents"));
	g->status_bar = GTK_LABEL(
		gtk_builder_get_object(tab_builder, "status_bar"));
	g->paned = GTK_PANED(gtk_builder_get_object(tab_builder, "hpaned1"));
	g->hadj = g_object_ref(GTK_ADJUSTMENT(
		gtk_builder_get_object(tab_builder, "layouthadjustment")));
	g->vadj = g_object_ref(GTK_ADJUSTMENT(
		gtk_builder_get_object(tab_builder, "layoutvadjustment")));
	g->input_method = gtk_im_multicontext_new();


	/* create toolbar */
	res = nsgtk_toolbar_create(tab_builder,
				   bw_from_gw,
				   g,
				   !!(flags & GW_CREATE_FOCUS_LOCATION),
				   &g->toolbar);
	if (res != NSERROR_OK) {
		free(g);
		g_object_unref(tab_builder);
		return NULL;
	}

	/* local page text search toolbar */
	res = nsgtk_search_create(tab_builder, g->bw, &g->search);
	if (res != NSERROR_OK) {
		free(g);
		g_object_unref(tab_builder);
		return NULL;
	}

	/* set a default favicon */
	g_object_ref(favicon_pixbuf);
	g->icon = favicon_pixbuf;

	/* add new gui window to global list (push_top) */
	if (window_list) {
		window_list->prev = g;
	}
	g->next = window_list;
	g->prev = NULL;
	window_list = g;

	gtk_drawing_area_set_draw_func(
		GTK_DRAWING_AREA(g->layout), nsgtk_window_draw, g, NULL);

	/* helper macro to conect signals to callbacks */
#define CONNECT(obj, sig, callback, ptr)                                       \
	g_signal_connect(G_OBJECT(obj), (sig), G_CALLBACK(callback), (ptr))

	/* layout input */
	controller = gtk_event_controller_motion_new();
	CONNECT(controller, "motion", nsgtk_window_motion, g);
	gtk_widget_add_controller(g->layout, controller);

	gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	CONNECT(gesture, "pressed", nsgtk_window_button_pressed, g);
	CONNECT(gesture, "released", nsgtk_window_button_released, g);
	gtk_widget_add_controller(g->layout, GTK_EVENT_CONTROLLER(gesture));

	controller = gtk_event_controller_scroll_new(
		GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
	CONNECT(controller, "scroll", nsgtk_window_scroll, g);
	gtk_widget_add_controller(g->layout, controller);

	controller = gtk_event_controller_key_new();
	gtk_event_controller_key_set_im_context(
		GTK_EVENT_CONTROLLER_KEY(controller), g->input_method);
	CONNECT(controller, "key-pressed", nsgtk_window_key_pressed, g);
	gtk_widget_add_controller(g->layout, controller);

	controller = gtk_event_controller_focus_new();
	CONNECT(controller, "enter", nsgtk_window_focus_enter, g);
	CONNECT(controller, "leave", nsgtk_window_focus_leave, g);
	gtk_widget_add_controller(g->layout, controller);

	CONNECT(g->layout, "resize", nsgtk_window_resize, g);

	/* scrolling */
	CONNECT(g->hadj, "value-changed", nsgtk_window_adjustment_changed, g);
	CONNECT(g->vadj, "value-changed", nsgtk_window_adjustment_changed, g);

	/* status pane signals */
	CONNECT(g->paned,
		"notify::max-position",
		nsgtk_paned_notify__max_position,
		g);

	CONNECT(g->paned, "notify::position", nsgtk_paned_notify__position, g);

	/* gtk container destructor */
	CONNECT(g->container, "destroy", window_destroy, g);

	/* input method */
	gtk_im_context_set_client_widget(g->input_method, g->layout);
	gtk_im_context_set_use_preedit(g->input_method, FALSE);

	/* input method signals */
	CONNECT(g->input_method, "commit", nsgtk_window_input_method_commit, g);

	/* add the tab container to the scaffold notebook */
	nsgtk_tab_add(g,
		      g->container,
		      open_in_background,
		      messages_get("NewTab"),
		      g->icon);

	/* initialy should not be visible */
	nsgtk_search_toggle_visibility(g->search);

	/* set toolbar visibility from user option */
	nsgtk_toolbar_show(g->toolbar, get_tool_bar_show());

	/* safe to drop the reference to the tab_builder as the container is
	 * referenced by the notebook now.
	 */
	g_object_unref(tab_builder);

	/* Finally we need to focus the location bar if requested */
	if (flags & GW_CREATE_FOCUS_LOCATION) {
		if (nsgtk_window_item_activate(g, OPENLOCATION_BUTTON) !=
		    NSERROR_OK) {
			NSLOG(netsurf,
			      WARNING,
			      "Unable to focus location input");
		}
	}

	return g;
}


static void gui_window_destroy(struct gui_window *gw)
{
	NSLOG(netsurf, INFO, "gui_window: %p", gw);
	assert(gw != NULL);
	assert(gw->bw != NULL);
	NSLOG(netsurf, INFO, "scaffolding: %p", gw->scaffold);

	/* kill off any throbber that might be running */
	nsgtk_schedule(-1, next_throbber_frame, gw);

	/* remove from window list */
	if (gw->prev) {
		gw->prev->next = gw->next;
	} else {
		window_list = gw->next;
	}

	if (gw->next) {
		gw->next->prev = gw->prev;
	}

	NSLOG(netsurf, INFO, "window list head: %p", window_list);
}


/**
 * favicon setting for gtk gui window.
 *
 * \param gw gtk gui window to set favicon on.
 * \param icon A handle to the new favicon content.
 */
static void
gui_window_set_icon(struct gui_window *gw, struct hlcache_handle *icon)
{
	struct bitmap *icon_bitmap = NULL;

	/* free any existing icon */
	if (gw->icon != NULL) {
		g_object_unref(gw->icon);
		gw->icon = NULL;
	}

	if (icon != NULL) {
		icon_bitmap = content_get_bitmap(icon);
		if (icon_bitmap != NULL) {
			NSLOG(netsurf, INFO, "Using %p bitmap", icon_bitmap);
			gw->icon = nsgdk_pixbuf_get_from_surface(
				icon_bitmap->surface, 16, 16);
		}
	}

	if (gw->icon == NULL) {
		NSLOG(netsurf, INFO, "Using default favicon");
		g_object_ref(favicon_pixbuf);
		gw->icon = favicon_pixbuf;
	}

	/* only set icon if throbber not running */
	if (gw->throb_frame == 0) {
		nsgtk_tab_set_icon(gw->container, gw->icon);
	}
}


static bool gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	*sy = (int)(gtk_adjustment_get_value(g->vadj));
	*sx = (int)(gtk_adjustment_get_value(g->hadj));

	return true;
}


static void nsgtk_redraw_caret(struct gui_window *g)
{
	if (g->careth == 0)
		return;

	gtk_widget_queue_draw(g->layout);
}


static void gui_window_remove_caret(struct gui_window *g)
{
	if (g->careth == 0)
		return;

	g->careth = 0;

	gtk_widget_queue_draw(g->layout);
}


/**
 * Invalidates an area of a GTK browser window
 *
 * \param g gui_window
 * \param rect area to redraw or NULL for the entire window area
 * \return NSERROR_OK on success or appropriate error code
 */
static nserror
nsgtk_window_invalidate_area(struct gui_window *g, const struct rect *rect)
{
	if (rect != NULL && !browser_window_has_content(g->bw)) {
		return NSERROR_OK;
	}

	gtk_widget_queue_draw(g->layout);

	return NSERROR_OK;
}


static void gui_window_set_status(struct gui_window *g, const char *text)
{
	assert(g);
	assert(g->status_bar);
	gtk_label_set_text(g->status_bar, text);
}


/**
 * Set the scroll position of a gtk browser window.
 *
 * Scrolls the viewport to ensure the specified rectangle of the
 *   content is shown. The GTK implementation scrolls the contents so
 *   the specified point in the content is at the top of the viewport.
 *
 * \param g gui window to scroll
 * \param rect The rectangle to ensure is shown.
 * \return NSERROR_OK on success or apropriate error code.
 */
static nserror
gui_window_set_scroll(struct gui_window *g, const struct rect *rect)
{
	GtkAdjustment *vadj = g->vadj;
	GtkAdjustment *hadj = g->hadj;
	gdouble vlower, vpage, vupper, hlower, hpage, hupper;
	gdouble x = (gdouble)rect->x0;
	gdouble y = (gdouble)rect->y0;

	assert(vadj);
	assert(hadj);

	g_object_get(vadj,
		     "page-size",
		     &vpage,
		     "lower",
		     &vlower,
		     "upper",
		     &vupper,
		     NULL);
	g_object_get(hadj,
		     "page-size",
		     &hpage,
		     "lower",
		     &hlower,
		     "upper",
		     &hupper,
		     NULL);

	if (x < hlower) {
		x = hlower;
	}
	if (x > (hupper - hpage)) {
		x = hupper - hpage;
	}
	if (y < vlower) {
		y = vlower;
	}
	if (y > (vupper - vpage)) {
		y = vupper - vpage;
	}

	gtk_adjustment_set_value(vadj, y);
	gtk_adjustment_set_value(hadj, x);

	return NSERROR_OK;
}


static void gui_window_update_extent(struct gui_window *g)
{
	int w, h;

	if (browser_window_get_extents(g->bw, true, &w, &h) == NSERROR_OK) {
		g->extent_width = w;
		g->extent_height = h;
		nsgtk_window_update_adjustments(g);
		gtk_widget_queue_draw(g->layout);
	}
}


static void
gui_window_set_pointer(struct gui_window *g, gui_pointer_shape shape)
{
	const char *name;
	GdkCursor *cursor;

	if (g->current_pointer == shape)
		return;

	g->current_pointer = shape;

	switch (shape) {
	case GUI_POINTER_POINT:
		name = "pointer";
		break;
	case GUI_POINTER_CARET:
		name = "text";
		break;
	case GUI_POINTER_UP:
		name = "n-resize";
		break;
	case GUI_POINTER_DOWN:
		name = "s-resize";
		break;
	case GUI_POINTER_LEFT:
		name = "w-resize";
		break;
	case GUI_POINTER_RIGHT:
		name = "e-resize";
		break;
	case GUI_POINTER_LD:
		name = "sw-resize";
		break;
	case GUI_POINTER_RD:
		name = "se-resize";
		break;
	case GUI_POINTER_LU:
		name = "nw-resize";
		break;
	case GUI_POINTER_RU:
		name = "ne-resize";
		break;
	case GUI_POINTER_CROSS:
		name = "crosshair";
		break;
	case GUI_POINTER_MOVE:
		name = "move";
		break;
	case GUI_POINTER_WAIT:
		name = "wait";
		break;
	case GUI_POINTER_HELP:
		name = "help";
		break;
	case GUI_POINTER_MENU:
		cursor = nsgtk_create_menu_cursor();
		if (cursor != NULL) {
			gtk_widget_set_cursor(g->layout, cursor);
			g_object_unref(cursor);
			return;
		}
		name = "context-menu";
		break;
	case GUI_POINTER_PROGRESS:
		name = "progress";
		break;
	case GUI_POINTER_NO_DROP:
		name = "no-drop";
		break;
	case GUI_POINTER_NOT_ALLOWED:
		name = "not-allowed";
		break;
	case GUI_POINTER_DEFAULT:
	default:
		name = NULL;
	}

	gtk_widget_set_cursor_from_name(g->layout, name);
}


static void gui_window_place_caret(struct gui_window *g,
				   int x,
				   int y,
				   int height,
				   const struct rect *clip)
{
	nsgtk_redraw_caret(g);

	y += 1;
	height -= 1;

	if (y < clip->y0) {
		height -= clip->y0 - y;
		y = clip->y0;
	}

	if (y + height > clip->y1) {
		height = clip->y1 - y + 1;
	}

	g->caretx = x;
	g->carety = y;
	g->careth = height;

	nsgtk_redraw_caret(g);

	gtk_widget_grab_focus(g->layout);
}


/**
 * Find the current dimensions of a GTK browser window content area.
 *
 * \param gw The gui window to measure content area of.
 * \param width receives width of window
 * \param height receives height of window
 * \return NSERROR_OK on sucess and width and height updated
 *          else error code.
 */
static nserror
gui_window_get_dimensions(struct gui_window *gw, int *width, int *height)
{
	*width = gtk_widget_get_width(gw->layout);
	*height = gtk_widget_get_height(gw->layout);

	return NSERROR_OK;
}


static void gui_window_start_selection(struct gui_window *g)
{
	gtk_widget_grab_focus(g->layout);
}


static void gui_window_create_form_select_menu(struct gui_window *g,
					       struct form_control *control)
{
	intptr_t item;
	struct form_option *option;
	GtkWidget *box;
	GtkWidget *scrolled;
	GtkWidget *check;
	GdkRectangle rect;

	/* control->data.select.multiple is true if multiple selections
	 * are allowable.  We ignore this, as the core handles it for us.
	 * Yay. \o/
	 */

	if (select_menu != NULL) {
		gtk_widget_unparent(select_menu);
	}

	select_menu = gtk_popover_new();
	select_menu_control = control;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	item = 0;
	option = form_select_get_option(control, item);
	while (option != NULL) {
		NSLOG(netsurf,
		      INFO,
		      "Item %" PRIdPTR " option %p text %s",
		      item,
		      option,
		      option->text);
		check = gtk_check_button_new_with_label(option->text);
		if (option->selected) {
			gtk_check_button_set_active(GTK_CHECK_BUTTON(check),
						    TRUE);
		}

		/*
		 * This casts the item index integer into an integer
		 * the size of a pointer. This allows the callback
		 * parameter to be passed avoiding allocating memory
		 * for a context with a single integer in it.
		 */
		g_signal_connect(check,
				 "toggled",
				 G_CALLBACK(nsgtk_select_menu_clicked),
				 (gpointer)item);

		gtk_box_append(GTK_BOX(box), check);

		item++;
		option = form_select_get_option(control, item);
	}

	scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(
		GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_max_content_height(
		GTK_SCROLLED_WINDOW(scrolled), 400);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
	gtk_popover_set_child(GTK_POPOVER(select_menu), scrolled);

	gtk_widget_set_parent(select_menu, g->layout);
	rect.x = g->pointer_x;
	rect.y = g->pointer_y;
	rect.width = 1;
	rect.height = 1;
	gtk_popover_set_pointing_to(GTK_POPOVER(select_menu), &rect);
	gtk_popover_set_has_arrow(GTK_POPOVER(select_menu), FALSE);
	gtk_popover_popup(GTK_POPOVER(select_menu));
}


/**
 * context for a file gadget dialog
 */
struct nsgtk_file_gadget {
	struct gui_window *g;
	struct form_control *gadget;
};

static void gui_window_file_gadget_chosen(GObject *source,
					  GAsyncResult *result,
					  gpointer data)
{
	struct nsgtk_file_gadget *fg = data;
	GFile *file;

	file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),
					   result,
					   NULL);
	if (file != NULL) {
		char *filename = g_file_get_path(file);
		struct gui_window *gw;

		/* the window may have been closed while the dialog was open */
		for (gw = window_list; gw != NULL && gw != fg->g;
		     gw = gw->next) {
			continue;
		}
		if (gw != NULL && filename != NULL) {
			browser_window_set_gadget_filename(gw->bw,
							   fg->gadget,
							   filename);
		}

		g_free(filename);
		g_object_unref(file);
	}
	free(fg);
}

/**
 * GTK window UI callback when core needs a file selection gadget
 *
 * \param g The gui window on which the gadget has been requested
 */
static void gui_window_file_gadget_open(struct gui_window *g,
					struct hlcache_handle *hl,
					struct form_control *gadget)
{
	GtkFileDialog *dialog;
	struct nsgtk_file_gadget *fg;

	fg = malloc(sizeof(*fg));
	if (fg == NULL) {
		return;
	}
	fg->g = g;
	fg->gadget = gadget;

	dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, "Select File");
	gtk_file_dialog_open(dialog,
			     nsgtk_scaffolding_window(g->scaffold),
			     NULL,
			     gui_window_file_gadget_chosen,
			     fg);
	g_object_unref(dialog);
}


/**
 * handle throbber changing state
 */
static nserror throbber(struct gui_window *gw, bool active)
{
	nsgtk_toolbar_throbber(gw->toolbar, active);
	nsgtk_scaffolding_throbber(gw, active);
	if (active) {
		nsgtk_schedule(THROBBER_FRAME_TIME, next_throbber_frame, gw);
	} else {
		nsgtk_schedule(-1, next_throbber_frame, gw);
		gw->throb_frame = 0;
		/* set tab back to favicon */
		nsgtk_tab_set_icon(gw->container, gw->icon);
	}
	return NSERROR_OK;
}


/**
 * handle page info changing
 */
static nserror page_info_change(struct gui_window *gw)
{
	nsgtk_toolbar_page_info_change(gw->toolbar);
	return NSERROR_OK;
}

/**
 * GTK window UI callback to process miscellaneous events
 *
 * \param gw The window receiving the event.
 * \param event The event code.
 * \return NSERROR_OK when processed ok
 */
static nserror
gui_window_event(struct gui_window *gw, enum gui_window_event event)
{
	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		gui_window_update_extent(gw);
		break;

	case GW_EVENT_REMOVE_CARET:
		gui_window_remove_caret(gw);
		break;

	case GW_EVENT_START_SELECTION:
		gui_window_start_selection(gw);
		break;

	case GW_EVENT_START_THROBBER:
		throbber(gw, true);
		break;

	case GW_EVENT_STOP_THROBBER:
		throbber(gw, false);
		break;

	case GW_EVENT_PAGE_INFO_CHANGE:
		page_info_change(gw);
		break;

	default:
		break;
	}
	return NSERROR_OK;
}


/**
 * GTK window UI callback when core changes the current url
 *
 * \param gw The gui window on which the url has been set.
 * \param url The new url.
 */
static nserror gui_window_set_url(struct gui_window *gw, nsurl *url)
{
	return nsgtk_toolbar_set_url(gw->toolbar, url);
}


/**
 * GTK window UI callback when core changes the current title
 *
 * \param gw The gui window on which the url has been set.
 * \param url The new url.
 */
static void gui_window_set_title(struct gui_window *gw, const char *title)
{

	if ((title != NULL) && (title[0] != '\0')) {
		nsgtk_tab_set_title(gw->container, title);
	}
	nsgtk_scaffolding_set_title(gw, title);
}


/**
 * GTK UI callback when search provider details are updated.
 *
 * \param name The providers name.
 * \param bitmap The bitmap representing the provider.
 * \return NSERROR_OK on success else error code.
 */
static nserror
gui_search_web_provider_update(const char *name, struct bitmap *bitmap)
{
	struct gui_window *gw;
	GdkPixbuf *pixbuf = NULL;

	if (bitmap != NULL) {
		pixbuf = nsgdk_pixbuf_get_from_surface(bitmap->surface, 32, 32);
	}

	for (gw = window_list; gw != NULL; gw = gw->next) {
		nsgtk_toolbar_set_websearch_image(gw->toolbar, pixbuf);
	}

	if (pixbuf != NULL) {
		g_object_unref(pixbuf);
	}

	return NSERROR_OK;
}


/**
 * GTK frontend web search operation table
 */
static struct gui_search_web_table search_web_table = {
	.provider_update = gui_search_web_provider_update,
};

struct gui_search_web_table *nsgtk_search_web_table = &search_web_table;


/**
 * GTK frontend browser window operation table
 */
static struct gui_window_table window_table = {
	.create = gui_window_create,
	.destroy = gui_window_destroy,
	.invalidate = nsgtk_window_invalidate_area,
	.get_scroll = gui_window_get_scroll,
	.set_scroll = gui_window_set_scroll,
	.get_dimensions = gui_window_get_dimensions,
	.event = gui_window_event,

	.set_icon = gui_window_set_icon,
	.set_title = gui_window_set_title,
	.set_status = gui_window_set_status,
	.set_pointer = gui_window_set_pointer,
	.place_caret = gui_window_place_caret,
	.create_form_select_menu = gui_window_create_form_select_menu,
	.file_gadget_open = gui_window_file_gadget_open,
	.set_url = gui_window_set_url,


};

struct gui_window_table *nsgtk_window_table = &window_table;


/* exported interface documented in window.h */
struct nsgtk_scaffolding *nsgtk_get_scaffold(struct gui_window *g)
{
	return g->scaffold;
}


/* exported interface documented in window.h */
struct browser_window *nsgtk_get_browser_window(struct gui_window *g)
{
	return g->bw;
}


/* exported interface documented in window.h */
GtkWidget *nsgtk_window_get_layout(struct gui_window *g)
{
	return g->layout;
}


/* exported interface documented in window.h */
nserror nsgtk_window_search_toggle(struct gui_window *gw)
{
	return nsgtk_search_toggle_visibility(gw->search);
}


/* exported interface documented in window.h */
nserror
nsgtk_window_item_activate(struct gui_window *gw, nsgtk_toolbar_button itemid)
{
	return nsgtk_toolbar_item_activate(gw->toolbar, itemid);
}


/* exported interface documented in window.h */
void nsgtk_window_destroy_browser(struct gui_window *gw)
{
	GtkNotebook *notebook = nsgtk_scaffolding_notebook(gw->scaffold);

	/* remove tab */
	gtk_notebook_remove_page(
		notebook, gtk_notebook_page_num(notebook, gw->container));
}


/* exported interface documented in window.h */
nserror nsgtk_window_update_all(void)
{
	struct gui_window *gw;
	for (gw = window_list; gw != NULL; gw = gw->next) {
		nsgtk_tab_options_changed(
			nsgtk_scaffolding_notebook(gw->scaffold));
		nsgtk_toolbar_restyle(gw->toolbar);
		nsgtk_search_restyle(gw->search);
		browser_window_schedule_reformat(gw->bw);
	}
	return NSERROR_OK;
}


/* exported interface documented in window.h */
nserror nsgtk_window_toolbar_show(struct nsgtk_scaffolding *gs, bool show)
{
	struct gui_window *gw;
	for (gw = window_list; gw != NULL; gw = gw->next) {
		if (gw->scaffold == gs) {
			nsgtk_toolbar_show(gw->toolbar, show);
		}
	}
	return NSERROR_OK;
}


/* exported interface documented in window.h */
nserror nsgtk_window_toolbar_update(void)
{
	struct gui_window *gw;
	for (gw = window_list; gw != NULL; gw = gw->next) {
		nsgtk_toolbar_update(gw->toolbar);
		nsgtk_toolbar_set_url(gw->toolbar,
				      browser_window_access_url(gw->bw));
		nsgtk_toolbar_page_info_change(gw->toolbar);
	}
	return NSERROR_OK;
}
