/*
 * Copyright 2009 Mark Benjamin <netsurf-browser.org.MarkBenjamin@dfgh.net>
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

#include <stdlib.h>
#include <stdbool.h>

#include <gtk/gtk.h>

#include "utils/messages.h"

#include "gtk/menu.h"
#include "gtk/accelerator.h"

/**
 * Menu entries with a keyboard shortcut: the message key of the label,
 * which is also the key of the accelerator, and the action to activate.
 */
static const struct {
	const char *key;
	const char *action;
} shortcuts[] = {
	{"gtkNewWindow", "win.newwindow"},
	{"gtkNewTab", "win.newtab"},
	{"gtkOpenFile", "win.openfile"},
	{"gtkCloseWindow", "win.closewindow"},
	{"gtkSavePage", "win.savepage"},
	{"gtkPlainText", "win.plaintext"},
	{"gtkPDF", "win.pdf"},
	{"gtkPrintPreview", "win.printpreview"},
	{"gtkPrint", "win.print"},
	{"gtkQuitMenu", "win.quit"},
	{"gtkCut", "win.cut"},
	{"gtkCopy", "win.copy"},
	{"gtkPaste", "win.paste"},
	{"gtkDelete", "win.delete"},
	{"gtkSelectAll", "win.selectall"},
	{"gtkFind", "win.find"},
	{"gtkPreferences", "win.preferences"},
	{"gtkZoomPlus", "win.zoomplus"},
	{"gtkZoomNormal", "win.zoomnormal"},
	{"gtkZoomMinus", "win.zoomminus"},
	{"gtkFullScreen", "win.fullscreen"},
	{"gtkCustomize", "win.cutomize"},
	{"gtkNextTab", "win.nexttab"},
	{"gtkPrevTab", "win.prevtab"},
	{"gtkCloseTab", "win.closetab"},
	{"gtkSaveWindowSize", "win.savewindowsize"},
	{"gtkBack", "win.back"},
	{"gtkForward", "win.forward"},
	{"gtkStop", "win.stop"},
	{"gtkReload", "win.reload"},
	{"gtkHome", "win.home"},
	{"gtkLocalHistory", "win.localhistory"},
	{"gtkGlobalHistory", "win.globalhistory"},
	{"gtkAddBookMarks", "win.addbookmarks"},
	{"gtkShowBookMarks", "win.showbookmarks"},
	{"gtkOpenLocation", "win.openlocation"},
	{"gtkDownloads", "win.downloads"},
	{"gtkShowCookies", "win.showcookies"},
	{"gtkPageSource", "win.viewsource"},
	{"gtkToggleDebugging", "win.toggledebugging"},
	{"gtkDebugBoxTree", "win.debugboxtree"},
	{"gtkDebugDomTree", "win.debugdomtree"},
	{"gtkContents", "win.contents"},
	{"gtkGuide", "win.guide"},
	{"gtkUserInformation", "win.info"},
	{"gtkAbout", "win.about"},
};

static void add_item(GMenu *menu, const char *key, const char *action)
{
	GMenuItem *item;
	const char *accel;

	item = g_menu_item_new(messages_get(key), action);
	accel = nsgtk_accelerator_get_desc(key);
	if (accel != NULL) {
		g_menu_item_set_attribute(item, "accel", "s", accel);
	}
	g_menu_append_item(menu, item);
	g_object_unref(item);
}

static void add_section(GMenu *menu, GMenu *section)
{
	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);
}

static void add_submenu(GMenu *menu, const char *key, GMenu *submenu)
{
	g_menu_append_submenu(menu, messages_get(key), G_MENU_MODEL(submenu));
	g_object_unref(submenu);
}

static GMenu *export_menu(void)
{
	GMenu *menu = g_menu_new();
	add_item(menu, "gtkSavePage", "win.savepage");
	add_item(menu, "gtkPlainText", "win.plaintext");
	add_item(menu, "gtkPDF", "win.pdf");
	return menu;
}

static GMenu *scaleview_menu(void)
{
	GMenu *menu = g_menu_new();
	add_item(menu, "gtkZoomPlus", "win.zoomplus");
	add_item(menu, "gtkZoomNormal", "win.zoomnormal");
	add_item(menu, "gtkZoomMinus", "win.zoomminus");
	return menu;
}

static GMenu *tabs_menu(void)
{
	GMenu *menu = g_menu_new();
	add_item(menu, "gtkNextTab", "win.nexttab");
	add_item(menu, "gtkPrevTab", "win.prevtab");
	add_item(menu, "gtkCloseTab", "win.closetab");
	return menu;
}

static GMenu *toolbars_menu(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_item(section, "gtkMenuBar", "win.showmenubar");
	add_item(section, "gtkToolBar", "win.showtoolbar");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkCustomize", "win.cutomize");
	add_section(menu, section);
	return menu;
}

static GMenu *developer_menu(void)
{
	GMenu *menu = g_menu_new();
	add_item(menu, "gtkPageSource", "win.viewsource");
	add_item(menu, "gtkToggleDebugging", "win.toggledebugging");
	add_item(menu, "gtkDebugBoxTree", "win.debugboxtree");
	add_item(menu, "gtkDebugDomTree", "win.debugdomtree");
	return menu;
}

static GMenu *file_menu(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_item(section, "gtkNewWindow", "win.newwindow");
	add_item(section, "gtkNewTab", "win.newtab");
	add_item(section, "gtkOpenFile", "win.openfile");
	add_item(section, "gtkCloseWindow", "win.closewindow");
	add_section(menu, section);
	section = g_menu_new();
	add_submenu(section, "gtkExport", export_menu());
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkPrintPreview", "win.printpreview");
	add_item(section, "gtkPrint", "win.print");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkQuitMenu", "win.quit");
	add_section(menu, section);
	return menu;
}

static GMenu *edit_menu(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_item(section, "gtkCut", "win.cut");
	add_item(section, "gtkCopy", "win.copy");
	add_item(section, "gtkPaste", "win.paste");
	add_item(section, "gtkDelete", "win.delete");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkSelectAll", "win.selectall");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkFind", "win.find");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkPreferences", "win.preferences");
	add_section(menu, section);
	return menu;
}

static GMenu *view_menu(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_submenu(section, "gtkScaleView", scaleview_menu());
	add_item(section, "gtkFullScreen", "win.fullscreen");
	add_section(menu, section);
	section = g_menu_new();
	add_submenu(section, "gtkToolbars", toolbars_menu());
	add_submenu(section, "gtkTabs", tabs_menu());
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkSaveWindowSize", "win.savewindowsize");
	add_section(menu, section);
	return menu;
}

static GMenu *nav_menu(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_item(section, "gtkBack", "win.back");
	add_item(section, "gtkForward", "win.forward");
	add_item(section, "gtkStop", "win.stop");
	add_item(section, "gtkReload", "win.reload");
	add_item(section, "gtkHome", "win.home");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkLocalHistory", "win.localhistory");
	add_item(section, "gtkGlobalHistory", "win.globalhistory");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkAddBookMarks", "win.addbookmarks");
	add_item(section, "gtkShowBookMarks", "win.showbookmarks");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkOpenLocation", "win.openlocation");
	add_section(menu, section);
	return menu;
}

static GMenu *tools_menu(void)
{
	GMenu *menu = g_menu_new();
	add_item(menu, "gtkDownloads", "win.downloads");
	add_item(menu, "gtkShowCookies", "win.showcookies");
	add_submenu(menu, "gtkDeveloper", developer_menu());
	return menu;
}

static GMenu *help_menu(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_item(section, "gtkContents", "win.contents");
	add_item(section, "gtkGuide", "win.guide");
	add_item(section, "gtkUserInformation", "win.info");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkAbout", "win.about");
	add_section(menu, section);
	return menu;
}

static GMenu *main_menu(void)
{
	GMenu *menu = g_menu_new();
	add_submenu(menu, "gtkFile", file_menu());
	add_submenu(menu, "gtkEdit", edit_menu());
	add_submenu(menu, "gtkView", view_menu());
	add_submenu(menu, "gtkNavigate", nav_menu());
	add_submenu(menu, "gtkTools", tools_menu());
	add_submenu(menu, "gtkHelp", help_menu());
	return menu;
}

/* exported interface documented in gtk/menu.h */
GMenuModel *nsgtk_menu_bar_model(void)
{
	return G_MENU_MODEL(main_menu());
}

/* exported interface documented in gtk/menu.h */
GMenuModel *nsgtk_burger_menu_model(void)
{
	return G_MENU_MODEL(main_menu());
}

/* exported interface documented in gtk/menu.h */
GMenuModel *nsgtk_popup_menu_model(bool nav, bool cnp)
{
	GMenu *menu = g_menu_new();
	GMenu *section;

	if (nav) {
		section = g_menu_new();
		add_item(section, "gtkBack", "win.back");
		add_item(section, "gtkForward", "win.forward");
		add_item(section, "gtkStop", "win.stop");
		add_item(section, "gtkReload", "win.reload");
		add_section(menu, section);
	}
	if (cnp) {
		section = g_menu_new();
		add_item(section, "gtkCut", "win.cut");
		add_item(section, "gtkCopy", "win.copy");
		add_item(section, "gtkPaste", "win.paste");
		add_section(menu, section);
	}
	section = g_menu_new();
	add_submenu(section, "gtkToolbars", toolbars_menu());
	add_submenu(section, "gtkTools", tools_menu());
	add_section(menu, section);
	return G_MENU_MODEL(menu);
}

/* exported interface documented in gtk/menu.h */
GMenuModel *nsgtk_link_menu_model(void)
{
	GMenu *menu = g_menu_new();
	GMenu *section = g_menu_new();
	add_item(section, "gtkOpentab", "win.link-opentab");
	add_item(section, "gtkOpenwin", "win.link-openwin");
	add_section(menu, section);
	section = g_menu_new();
	add_item(section, "gtkSavelink", "win.link-save");
	add_item(section, "gtkBookmarklink", "win.link-bookmark");
	add_item(section, "gtkCopylink", "win.link-copy");
	add_section(menu, section);
	return G_MENU_MODEL(menu);
}

/* exported interface documented in gtk/menu.h */
void nsgtk_menu_add_shortcuts(GtkWidget *window)
{
	GtkEventController *controller;
	size_t idx;

	controller = gtk_shortcut_controller_new();
	gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller),
					  GTK_SHORTCUT_SCOPE_LOCAL);
	for (idx = 0; idx < G_N_ELEMENTS(shortcuts); idx++) {
		const char *accel = nsgtk_accelerator_get_desc(
			shortcuts[idx].key);
		GtkShortcutTrigger *trigger;

		if (accel == NULL) {
			continue;
		}
		trigger = gtk_shortcut_trigger_parse_string(accel);
		if (trigger == NULL) {
			continue;
		}
		gtk_shortcut_controller_add_shortcut(
			GTK_SHORTCUT_CONTROLLER(controller),
			gtk_shortcut_new(trigger,
					 gtk_named_action_new(
						 shortcuts[idx].action)));
	}
	gtk_widget_add_controller(window, controller);
}
