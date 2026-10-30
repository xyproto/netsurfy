/*
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
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
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/file.h"
#include "utils/log.h"
#include "utils/nsurl.h"
#include "netsurfy/browser_window.h"
#include "desktop/searchweb.h"

#include "gtk/compat.h"
#include "gtk/toolbar_items.h"
#include "gtk/window.h"
#include "gtk/gui.h"
#include "gtk/scaffolding.h"
#include "gtk/resources.h"
#include "gtk/preferences.h"

/* private prefs */
struct ppref {
	/** dialog handle created when window first accessed */
	GObject *dialog;

	struct browser_window *bw;

	/* widgets which are accessed from outside their own signal handlers */
	GtkEntry *entryHomePageURL;
	GtkEntry *entryProxyHost;
	GtkEntry *entryProxyUser;
	GtkEntry *entryProxyPassword;
	GtkEntry *entryProxyNoproxy;
	GtkSpinButton *spinProxyPort;

	/* dynamic list stores */
	GtkListStore *content_language;
	GtkListStore *search_providers;
};

static struct ppref ppref;


/* Set netsurfy option based on toggle button state
 *
 * This works for any widget which subclasses togglebutton (checkbox,
 * radiobutton etc.)
 */
#define TOGGLEBUTTON_SIGNALS(WIDGET, OPTION)                                   \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_toggled(             \
		GtkCheckButton *togglebutton, gpointer user_data);             \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_toggled(             \
		GtkCheckButton *togglebutton, gpointer user_data)              \
	{                                                                      \
		nsoption_set_bool(OPTION,                                      \
				  gtk_check_button_get_active(togglebutton));  \
	}                                                                      \
                                                                               \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data);                        \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data)                         \
	{                                                                      \
		gtk_check_button_set_active(GTK_CHECK_BUTTON(widget),          \
					    nsoption_bool(OPTION));            \
	}

#define SPINBUTTON_SIGNALS(WIDGET, OPTION, MULTIPLIER)                         \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_valuechanged(        \
		GtkSpinButton *spinbutton, gpointer user_data);                \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_valuechanged(        \
		GtkSpinButton *spinbutton, gpointer user_data)                 \
	{                                                                      \
		nsoption_set_int(OPTION,                                       \
				 round(gtk_spin_button_get_value(spinbutton) * \
				       MULTIPLIER));                           \
	}                                                                      \
                                                                               \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data);                        \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data)                         \
	{                                                                      \
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget),             \
					  ((gdouble)nsoption_int(OPTION)) /    \
						  MULTIPLIER);                 \
	}

#define SPINBUTTON_UINT_SIGNALS(WIDGET, OPTION, MULTIPLIER)                    \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_valuechanged(        \
		GtkSpinButton *spinbutton, gpointer user_data);                \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_valuechanged(        \
		GtkSpinButton *spinbutton, gpointer user_data)                 \
	{                                                                      \
		nsoption_set_uint(                                             \
			OPTION,                                                \
			round(gtk_spin_button_get_value(spinbutton) *          \
			      MULTIPLIER));                                    \
	}                                                                      \
                                                                               \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data);                        \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data)                         \
	{                                                                      \
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget),             \
					  ((gdouble)nsoption_uint(OPTION)) /   \
						  MULTIPLIER);                 \
	}

#define ENTRY_SIGNALS(WIDGET, OPTION)                                          \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_changed(             \
		GtkEditable *editable, gpointer user_data);                    \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_changed(             \
		GtkEditable *editable, gpointer user_data)                     \
	{                                                                      \
		nsoption_set_charp(OPTION,                                     \
				   strdup(gtk_editable_get_text(               \
					   GTK_EDITABLE(editable))));          \
	}                                                                      \
                                                                               \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data);                        \
	G_MODULE_EXPORT void nsgtk_preferences_##WIDGET##_realize(             \
		GtkWidget *widget, gpointer user_data)                         \
	{                                                                      \
		const char *OPTION;                                            \
		OPTION = nsoption_charp(OPTION);                               \
		if (OPTION != NULL) {                                          \
			gtk_editable_set_text(GTK_EDITABLE(widget), OPTION);   \
		}                                                              \
	}

/* GTK module requires these to be exported symbols so these all need
 * forward declaring to avoid warnings
 */
G_MODULE_EXPORT void
nsgtk_preferences_comboProxyType_changed(GtkComboBox *combo,
					 gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboProxyType_realize(GtkWidget *widget, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLoadImages_changed(GtkComboBox *combo,
					     gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLoadImages_realize(GtkWidget *widget,
					     gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboDefault_changed(GtkComboBox *combo, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboDefault_realize(GtkWidget *widget, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_fontPreview_clicked(GtkButton *button, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLanguage_changed(GtkComboBox *combo,
					   gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLanguage_realize(GtkWidget *widget,
					   gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_checkShowSingleTab_toggled(GtkCheckButton *togglebutton,
					     gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_checkShowSingleTab_realize(GtkWidget *widget,
					     gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboTabPosition_changed(GtkComboBox *widget,
					   gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboTabPosition_realize(GtkWidget *widget,
					   gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboDeveloperView_changed(GtkComboBox *widget,
					     gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboDeveloperView_realize(GtkWidget *widget,
					     gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboButtonType_changed(GtkComboBox *widget,
					  gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboButtonType_realize(GtkWidget *widget,
					  gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_setCurrentPage_clicked(GtkButton *button, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_setDefaultPage_clicked(GtkButton *button, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboSearch_changed(GtkComboBox *widget, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_comboSearch_realize(GtkWidget *widget, gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_fileChooserDownloads_clicked(GtkButton *button,
					       gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_fileChooserDownloads_realize(GtkWidget *widget,
					       gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_close_clicked(GtkButton *button, gpointer user_data);
G_MODULE_EXPORT gboolean
nsgtk_preferences_dialogPreferences_closerequest(GtkWindow *dlg,
						 gpointer user_data);
G_MODULE_EXPORT void
nsgtk_preferences_dialogPreferences_destroy(GtkWindow *dlg, gpointer user_data);


/********* PDF **********/

/* Appearance */

/* no images in output */
TOGGLEBUTTON_SIGNALS(checkSuppressImages, suppress_images)

/* no background images */
TOGGLEBUTTON_SIGNALS(checkRemoveBackgrounds, remove_backgrounds)

/* scale to fit page */
TOGGLEBUTTON_SIGNALS(checkFitPage, enable_loosening)

/* port */
SPINBUTTON_SIGNALS(spinExportScale, export_scale, 1.0)

/* Margins */
SPINBUTTON_SIGNALS(spinMarginTop, margin_top, 1.0)
SPINBUTTON_SIGNALS(spinMarginBottom, margin_bottom, 1.0)
SPINBUTTON_SIGNALS(spinMarginLeft, margin_left, 1.0)
SPINBUTTON_SIGNALS(spinMarginRight, margin_right, 1.0)


/* Generation */

/* output is compressed */
TOGGLEBUTTON_SIGNALS(checkCompressPDF, enable_PDF_compression)

/* output has a password */
TOGGLEBUTTON_SIGNALS(checkPasswordPDF, enable_PDF_password)

/********* Network **********/

/* HTTP proxy */
static void set_proxy_widgets_sensitivity(int proxyval, struct ppref *priv)
{
	gboolean host;
	gboolean port;
	gboolean user;
	gboolean pass;
	gboolean noproxy;

	switch (proxyval) {
	case 0: /* no proxy */
		host = FALSE;
		port = FALSE;
		user = FALSE;
		pass = FALSE;
		noproxy = FALSE;
		break;

	case 1: /* proxy with no auth */
		host = TRUE;
		port = TRUE;
		user = FALSE;
		pass = FALSE;
		noproxy = TRUE;
		break;

	case 2: /* proxy with basic auth */
		host = TRUE;
		port = TRUE;
		user = TRUE;
		pass = TRUE;
		noproxy = TRUE;
		break;

	case 3: /* proxy with ntlm auth */
		host = TRUE;
		port = TRUE;
		user = TRUE;
		pass = TRUE;
		noproxy = TRUE;
		break;

	case 4: /* system proxy */
		host = FALSE;
		port = FALSE;
		user = FALSE;
		pass = FALSE;
		noproxy = FALSE;
		break;

	default:
		return;
	}

	gtk_widget_set_sensitive(GTK_WIDGET(priv->entryProxyHost), host);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->spinProxyPort), port);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->entryProxyUser), user);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->entryProxyPassword), pass);
	gtk_widget_set_sensitive(GTK_WIDGET(priv->entryProxyNoproxy), noproxy);
}

G_MODULE_EXPORT void
nsgtk_preferences_comboProxyType_changed(GtkComboBox *combo, gpointer user_data)
{
	struct ppref *priv = &ppref;
	int proxy_sel;
	proxy_sel = gtk_combo_box_get_active(combo);

	switch (proxy_sel) {
	case 0: /* no proxy */
		nsoption_set_bool(http_proxy, false);
		break;

	case 1: /* proxy with no auth */
		nsoption_set_bool(http_proxy, true);
		nsoption_set_int(http_proxy_auth, OPTION_HTTP_PROXY_AUTH_NONE);
		break;

	case 2: /* proxy with basic auth */
		nsoption_set_bool(http_proxy, true);
		nsoption_set_int(http_proxy_auth, OPTION_HTTP_PROXY_AUTH_BASIC);
		break;

	case 3: /* proxy with ntlm auth */
		nsoption_set_bool(http_proxy, true);
		nsoption_set_int(http_proxy_auth, OPTION_HTTP_PROXY_AUTH_NTLM);
		break;

	case 4: /* system proxy */
		nsoption_set_bool(http_proxy, true);
		nsoption_set_int(http_proxy_auth, OPTION_HTTP_PROXY_AUTH_NONE);
		break;
	}

	set_proxy_widgets_sensitivity(proxy_sel, priv);
}

G_MODULE_EXPORT void
nsgtk_preferences_comboProxyType_realize(GtkWidget *widget, gpointer user_data)
{
	struct ppref *priv = &ppref;
	int proxytype = 0; /* no proxy by default */

	if (nsoption_bool(http_proxy) == true) {
		/* proxy type combo box starts with disabled, to allow
		 * for this the http_proxy option needs combining with
		 * the http_proxy_auth option
		 */
		proxytype = nsoption_int(http_proxy_auth) + 1;
		if (nsoption_charp(http_proxy_host) == NULL) {
			/* set to use a proxy without a host, turn proxy off */
			proxytype = 0;
		} else if (((proxytype == 2) || (proxytype == 3)) &&
			   ((nsoption_charp(http_proxy_auth_user) == NULL) ||
			    (nsoption_charp(http_proxy_auth_pass) == NULL))) {
			/* authentication selected with empty credentials, turn
			 * proxy off */
			proxytype = 0;
		}
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), proxytype);

	set_proxy_widgets_sensitivity(proxytype, priv);
}

/* host */
ENTRY_SIGNALS(entryProxyHost, http_proxy_host)

/* port */
SPINBUTTON_SIGNALS(spinProxyPort, http_proxy_port, 1.0)

/* user */
ENTRY_SIGNALS(entryProxyUser, http_proxy_auth_user)

/* password */
ENTRY_SIGNALS(entryProxyPassword, http_proxy_auth_pass)

/* no proxy */
ENTRY_SIGNALS(entryProxyNoproxy, http_proxy_noproxy)

/* Fetching */

/* maximum fetchers */
SPINBUTTON_SIGNALS(spinMaxFetchers, max_fetchers, 1.0)

/* fetches per host */
SPINBUTTON_SIGNALS(spinFetchesPerHost, max_fetchers_per_host, 1.0)

/* cached connections */
SPINBUTTON_SIGNALS(spinCachedConnections, max_cached_fetch_handles, 1.0)


/********* Privacy **********/

/* General */

/* enable referral submission */
TOGGLEBUTTON_SIGNALS(checkSendReferer, send_referer)

/* send do not track */
TOGGLEBUTTON_SIGNALS(checkSendDNT, do_not_track)

/* History */

/* local history shows url tooltips */
TOGGLEBUTTON_SIGNALS(checkHoverURLs, hover_urls)

/* remember browsing history */
SPINBUTTON_SIGNALS(spinHistoryAge, history_age, 1.0)

/* Cache */

/* memory cache size */
SPINBUTTON_SIGNALS(spinMemoryCacheSize, memory_cache_size, (1024 * 1024))

/* disc cache size */
SPINBUTTON_UINT_SIGNALS(spinDiscCacheSize, disc_cache_size, (1024 * 1024))


/* disc cache age */
SPINBUTTON_SIGNALS(spinDiscCacheAge, disc_cache_age, 1.0)


/********* Content **********/

/* Control */


/* prevent popups */
TOGGLEBUTTON_SIGNALS(checkDisablePopups, disable_popups)

/* hide adverts */
TOGGLEBUTTON_SIGNALS(checkHideAdverts, block_advertisements)

/* enable javascript */
TOGGLEBUTTON_SIGNALS(checkEnableJavascript, enable_javascript)

/* load and display of images */
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLoadImages_changed(GtkComboBox *combo,
					     gpointer user_data)
{
	int img_sel;
	/* get the row number for the selection */
	img_sel = gtk_combo_box_get_active(combo);
	switch (img_sel) {
	case 0:
		/* background and foreground */
		nsoption_set_bool(foreground_images, true);
		nsoption_set_bool(background_images, true);
		break;

	case 1:
		/* foreground only */
		nsoption_set_bool(foreground_images, true);
		nsoption_set_bool(background_images, false);
		break;

	case 2:
		/* background only */
		nsoption_set_bool(foreground_images, false);
		nsoption_set_bool(background_images, true);
		break;

	case 3:
		/* no images */
		nsoption_set_bool(foreground_images, false);
		nsoption_set_bool(background_images, false);
		break;
	}
}

G_MODULE_EXPORT void
nsgtk_preferences_comboboxLoadImages_realize(GtkWidget *widget,
					     gpointer user_data)
{
	if (nsoption_bool(foreground_images)) {
		if (nsoption_bool(background_images)) {
			/* background and foreground */
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
		} else {
			/* foreground only */
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 1);
		}
	} else {
		if (nsoption_bool(background_images)) {
			/* background only */
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 2);
		} else {
			/* no images */
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 3);
		}
	}
}

/* Animation */

/* enable animation */
TOGGLEBUTTON_SIGNALS(checkEnableAnimations, animate_images)

/* Fonts */

/* default font */
G_MODULE_EXPORT void
nsgtk_preferences_comboDefault_changed(GtkComboBox *combo, gpointer user_data)
{
	int font_sel;
	/* get the row number for the selection */
	font_sel = gtk_combo_box_get_active(combo);
	if ((font_sel >= 0) && (font_sel <= 4)) {
		nsoption_set_int(font_default, font_sel);
	}
}

G_MODULE_EXPORT void
nsgtk_preferences_comboDefault_realize(GtkWidget *widget, gpointer user_data)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget),
				 nsoption_int(font_default));
}

/* default font size */
SPINBUTTON_SIGNALS(spinDefaultSize, font_size, 10.0)

/* preview - actually reflow all views */
G_MODULE_EXPORT void
nsgtk_preferences_fontPreview_clicked(GtkButton *button, gpointer user_data)
{
	nsgtk_window_update_all();
}


/* Language */

/* accept language */
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLanguage_changed(GtkComboBox *combo,
					   gpointer user_data)
{
	gchar *lang = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* Obtain currently selected item from combo box.
	 * If nothing is selected, do nothing.
	 */
	if (gtk_combo_box_get_active_iter(combo, &iter)) {
		/* Obtain data model from combo box. */
		model = gtk_combo_box_get_model(combo);

		/* Obtain string from model. */
		gtk_tree_model_get(model, &iter, 0, &lang, -1);
	}

	if (lang != NULL) {
		nsoption_set_charp(accept_language, strdup(lang));
		g_free(lang);
	}
}

/**
 * populate language combo from data
 */
static nserror comboboxLanguage_add_from_data(GtkListStore *liststore,
					      GtkComboBox *combobox,
					      const char *accept_language,
					      const uint8_t *data,
					      size_t data_size)
{
	int active_language = -1;
	GtkTreeIter iter;
	int combo_row_count = 0;
	const uint8_t *s;
	const uint8_t *nl;
	char buf[50];
	int bufi = 0;

	gtk_list_store_clear(liststore);
	s = data;

	while (s < (data + data_size)) {
		/* find nl and copy buffer */
		for (nl = s; nl < data + data_size; nl++) {
			if ((*nl == '\n') || (bufi == (sizeof(buf) - 2))) {
				buf[bufi] = 0; /* null terminate */
				break;
			}
			buf[bufi++] = *nl;
		}
		if (bufi > 0) {
			gtk_list_store_append(liststore, &iter);
			gtk_list_store_set(liststore, &iter, 0, buf, -1);

			if (strcmp(buf, accept_language) == 0) {
				active_language = combo_row_count;
			}

			combo_row_count++;
		}
		bufi = 0;
		s = nl + 1; /* skip newline */
	}

	/* if configured language was not in list, add it */
	if (active_language == -1) {
		gtk_list_store_append(liststore, &iter);
		gtk_list_store_set(liststore, &iter, 0, accept_language, -1);
		active_language = combo_row_count;
	}

	gtk_combo_box_set_active(combobox, active_language);

	return NSERROR_OK;
}

/**
 * populate language combo from file
 */
static nserror comboboxLanguage_add_from_file(GtkListStore *liststore,
					      GtkComboBox *combobox,
					      const char *accept_language,
					      const char *file_location)
{
	int active_language = -1;
	GtkTreeIter iter;
	int combo_row_count = 0;
	FILE *fp;
	char buf[50];

	fp = fopen(file_location, "r");
	if (fp == NULL) {
		return NSERROR_NOT_FOUND;
	}

	gtk_list_store_clear(liststore);

	NSLOG(netsurfy, INFO, "Used %s for languages", file_location);
	while (fgets(buf, sizeof(buf), fp)) {
		/* Ignore blank lines */
		if (buf[0] == '\0')
			continue;

		/* Remove trailing \n */
		buf[strlen(buf) - 1] = '\0';

		gtk_list_store_append(liststore, &iter);
		gtk_list_store_set(liststore, &iter, 0, buf, -1);

		if (strcmp(buf, accept_language) == 0) {
			active_language = combo_row_count;
		}

		combo_row_count++;
	}

	/* if configured language was not in list, add it */
	if (active_language == -1) {
		gtk_list_store_append(liststore, &iter);
		gtk_list_store_set(liststore, &iter, 0, accept_language, -1);
		active_language = combo_row_count;
	}

	fclose(fp);

	gtk_combo_box_set_active(combobox, active_language);

	return NSERROR_OK;
}

/**
 * Fill content language list store.
 */
G_MODULE_EXPORT void
nsgtk_preferences_comboboxLanguage_realize(GtkWidget *widget,
					   gpointer user_data)
{
	struct ppref *priv = &ppref;
	nserror res;
	const uint8_t *data;
	size_t data_size;
	const char *languages_file;
	const char *accept_language;

	if (priv->content_language == NULL) {
		NSLOG(netsurfy, INFO, "content language list store unavailable");
		return;
	}

	/* get current accept language */
	accept_language = nsoption_charp(accept_language);
	if (accept_language == NULL) {
		accept_language = "en";
	}

	/* attempt to read languages from inline resource */
	res = nsgtk_data_from_resname("languages", &data, &data_size);
	if (res == NSERROR_OK) {
		res = comboboxLanguage_add_from_data(priv->content_language,
						     GTK_COMBO_BOX(widget),
						     accept_language,
						     data,
						     data_size);
	} else {
		/* attempt to read languages from file */
		res = nsgtk_path_from_resname("languages", &languages_file);
		if (res == NSERROR_OK) {
			res = comboboxLanguage_add_from_file(
				priv->content_language,
				GTK_COMBO_BOX(widget),
				accept_language,
				languages_file);
		}
	}
	if (res != NSERROR_OK) {
		NSLOG(netsurfy, INFO, "error populatiung languages combo");
	}
}


/********* Apperance **********/

/* Tabs */

/* always show tab bar */
G_MODULE_EXPORT void
nsgtk_preferences_checkShowSingleTab_toggled(GtkCheckButton *togglebutton,
					     gpointer user_data)
{
	nsoption_set_bool(show_single_tab,
			  gtk_check_button_get_active(togglebutton));
	nsgtk_window_update_all();
}

G_MODULE_EXPORT void
nsgtk_preferences_checkShowSingleTab_realize(GtkWidget *widget,
					     gpointer user_data)
{
	gtk_check_button_set_active(GTK_CHECK_BUTTON(widget),
				    nsoption_bool(show_single_tab));
}

/* switch to newly opened tabs immediately */
TOGGLEBUTTON_SIGNALS(checkForegroundNew, foreground_new)

/* newly opened tabs are blank */
TOGGLEBUTTON_SIGNALS(checkNewBlank, new_blank)

/* tab position */
G_MODULE_EXPORT void
nsgtk_preferences_comboTabPosition_changed(GtkComboBox *widget,
					   gpointer user_data)
{
	/* set the option */
	nsoption_set_int(position_tab, gtk_combo_box_get_active(widget));

	/* update all windows */
	nsgtk_window_update_all();
}

G_MODULE_EXPORT void
nsgtk_preferences_comboTabPosition_realize(GtkWidget *widget,
					   gpointer user_data)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget),
				 nsoption_int(position_tab));
}

/* Tools */

/* developer view opening */
G_MODULE_EXPORT void
nsgtk_preferences_comboDeveloperView_changed(GtkComboBox *widget,
					     gpointer user_data)
{
	/* set the option */
	nsoption_set_int(developer_view, gtk_combo_box_get_active(widget));
}

G_MODULE_EXPORT void
nsgtk_preferences_comboDeveloperView_realize(GtkWidget *widget,
					     gpointer user_data)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget),
				 nsoption_int(developer_view));
}


/* URLbar */

/* show recently visited urls as you type */
TOGGLEBUTTON_SIGNALS(checkDisplayRecentURLs, url_suggestion)

/* Toolbar */

/* button position */
G_MODULE_EXPORT void
nsgtk_preferences_comboButtonType_changed(GtkComboBox *widget,
					  gpointer user_data)
{
	nsoption_set_int(button_type, gtk_combo_box_get_active(widget) + 1);

	/* update all windows to adopt change */
	nsgtk_window_update_all();
}

G_MODULE_EXPORT void
nsgtk_preferences_comboButtonType_realize(GtkWidget *widget, gpointer user_data)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(widget),
				 nsoption_int(button_type) - 1);
}


/************ Main ************/

/* Startup */

/* entry HomePageURL widget */
ENTRY_SIGNALS(entryHomePageURL, homepage_url)

/* put current page into homepage url */
G_MODULE_EXPORT void
nsgtk_preferences_setCurrentPage_clicked(GtkButton *button, gpointer user_data)
{
	struct ppref *priv = &ppref;
	const gchar *url = nsurl_access(browser_window_access_url(priv->bw));

	if (priv->entryHomePageURL != NULL) {
		gtk_editable_set_text(GTK_EDITABLE(priv->entryHomePageURL),
				      url);
		nsoption_set_charp(homepage_url, strdup(url));
	}
}

/* put default page into homepage */
G_MODULE_EXPORT void
nsgtk_preferences_setDefaultPage_clicked(GtkButton *button, gpointer user_data)
{
	struct ppref *priv = &ppref;
	const gchar *url = NETSURFY_HOMEPAGE;

	if (priv->entryHomePageURL != NULL) {
		gtk_editable_set_text(GTK_EDITABLE(priv->entryHomePageURL),
				      url);
		nsoption_set_charp(homepage_url, strdup(url));
	}
}

/* Search */

/* Url Search widget */
TOGGLEBUTTON_SIGNALS(checkUrlSearch, search_url_bar)

/* provider combo */
G_MODULE_EXPORT void
nsgtk_preferences_comboSearch_changed(GtkComboBox *widget, gpointer user_data)
{
	gboolean set;
	GtkTreeIter iter;
	GtkTreeModel *model;
	gchar *provider;
	const char *defprovider;

	set = gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &iter);
	if (!set) {
		return;
	}

	model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
	gtk_tree_model_get(model, &iter, 0, &provider, -1);

	/* set search provider */
	search_web_select_provider(provider);

	/* set to default option if the default provider is selected */
	if ((search_web_iterate_providers(-1, &defprovider) != -1) &&
	    (strcmp(provider, defprovider) == 0)) {
		free(provider);
		/* use default option */
		provider = NULL;
	}

	/* set the option which takes owership of the provider allocation */
	nsoption_set_charp(search_web_provider, provider);
}

G_MODULE_EXPORT void
nsgtk_preferences_comboSearch_realize(GtkWidget *widget, gpointer user_data)
{
	struct ppref *priv = &ppref;
	int iter;
	const char *name;
	const char *provider;
	int provider_idx = 0;

	if (priv->search_providers == NULL) {
		return;
	}
	gtk_list_store_clear(priv->search_providers);

	provider = nsoption_charp(search_web_provider);

	iter = search_web_iterate_providers(-1, &name);
	while (iter != -1) {
		gtk_list_store_insert_with_values(
			priv->search_providers, NULL, -1, 0, name, -1);
		if ((provider != NULL) && (strcmp(name, provider) == 0)) {
			provider_idx = iter;
		}
		iter = search_web_iterate_providers(iter, &name);
	}


	gtk_combo_box_set_active(GTK_COMBO_BOX(widget), provider_idx);
}


/* Downloads */

/* clear downloads */
TOGGLEBUTTON_SIGNALS(checkClearDownloads, downloads_clear)

/* request overwite */
TOGGLEBUTTON_SIGNALS(checkRequestOverwrite, request_overwrite)

/* download location */
static void fileChooserDownloads_set_label(GtkButton *button)
{
	const char *dir = nsoption_charp(downloads_directory);
	char *name;

	if (dir == NULL) {
		gtk_button_set_label(button, "");
		return;
	}
	name = g_filename_display_name(dir);
	gtk_button_set_label(button, name);
	g_free(name);
}

static void fileChooserDownloads_chosen(GObject *source,
					GAsyncResult *result,
					gpointer data)
{
	GtkButton *button = GTK_BUTTON(data);
	GFile *folder;
	char *dir;

	folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),
						      result,
						      NULL);
	if (folder != NULL) {
		dir = g_file_get_path(folder);
		if (dir != NULL) {
			nsoption_set_charp(downloads_directory, strdup(dir));
			g_free(dir);
		}
		g_object_unref(folder);
		fileChooserDownloads_set_label(button);
	}
	g_object_unref(button);
}

G_MODULE_EXPORT void
nsgtk_preferences_fileChooserDownloads_clicked(GtkButton *button,
					       gpointer user_data)
{
	GtkFileDialog *dialog;
	GtkWidget *toplevel;
	const char *dir = nsoption_charp(downloads_directory);

	toplevel = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);

	dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(
		dialog, messages_get("preferencesDownloadsLocationTooltip"));
	if (dir != NULL) {
		GFile *folder = g_file_new_for_path(dir);
		gtk_file_dialog_set_initial_folder(dialog, folder);
		g_object_unref(folder);
	}
	gtk_file_dialog_select_folder(dialog,
				      GTK_WINDOW(toplevel),
				      NULL,
				      fileChooserDownloads_chosen,
				      g_object_ref(button));
	g_object_unref(dialog);
}

G_MODULE_EXPORT void
nsgtk_preferences_fileChooserDownloads_realize(GtkWidget *widget,
					       gpointer user_data)
{
	fileChooserDownloads_set_label(GTK_BUTTON(widget));
}


/************* Dialog window ***********/

/* dialog close and destroy events */
G_MODULE_EXPORT void
nsgtk_preferences_close_clicked(GtkButton *button, gpointer user_data)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
	gtk_window_close(GTK_WINDOW(toplevel));
}

G_MODULE_EXPORT gboolean
nsgtk_preferences_dialogPreferences_closerequest(GtkWindow *dlg,
						 gpointer user_data)
{
	char *choices = NULL;

	netsurfy_mkpath(&choices, NULL, 2, nsgtk_config_home, "Choices");
	if (choices != NULL) {
		nsoption_write(choices, NULL, NULL);
		free(choices);
	}

	return FALSE;
}

G_MODULE_EXPORT void
nsgtk_preferences_dialogPreferences_destroy(GtkWindow *dlg, gpointer user_data)
{
	char *choices = NULL;

	netsurfy_mkpath(&choices, NULL, 2, nsgtk_config_home, "Choices");
	if (choices != NULL) {
		nsoption_write(choices, NULL, NULL);
		free(choices);
	}
}


/* exported interface documented in gtk/preferences.h */
GtkWidget *nsgtk_preferences(struct browser_window *bw, GtkWindow *parent)
{
	GtkBuilder *preferences_builder;
	struct ppref *priv = &ppref;
	nserror res;

	priv->bw = bw; /* for setting "current" page */

	/* memoised dialog creation */
	if (priv->dialog != NULL) {
		gtk_window_set_transient_for(GTK_WINDOW(priv->dialog), parent);
		return GTK_WIDGET(priv->dialog);
	}

	res = nsgtk_builder_new_from_resname("options", &preferences_builder);
	if (res != NSERROR_OK) {
		NSLOG(netsurfy, INFO, "Preferences UI builder init failed");
		return NULL;
	}

	priv->dialog = gtk_builder_get_object(preferences_builder,
					      "dialogPreferences");
	if (priv->dialog == NULL) {
		NSLOG(netsurfy,
		      INFO,
		      "Unable to get object for preferences dialog");
		/* release builder as were done with it */
		g_object_unref(G_OBJECT(preferences_builder));
		return NULL;
	}

	/* need to explicitly obtain handles for some widgets enabling
	 * updates by other widget events
	 */
#define GB(TYPE, NAME)                                                         \
	GTK_##TYPE(gtk_builder_get_object(preferences_builder, #NAME))
	priv->entryHomePageURL = GB(ENTRY, entryHomePageURL);
	priv->content_language = GB(LIST_STORE, liststore_content_language);
	priv->search_providers = GB(LIST_STORE, liststore_search_provider);
	priv->entryProxyHost = GB(ENTRY, entryProxyHost);
	priv->spinProxyPort = GB(SPIN_BUTTON, spinProxyPort);
	priv->entryProxyUser = GB(ENTRY, entryProxyUser);
	priv->entryProxyPassword = GB(ENTRY, entryProxyPassword);
	priv->entryProxyNoproxy = GB(ENTRY, entryProxyNoproxy);
#undef GB


	/* release builder as were done with it */
	g_object_unref(G_OBJECT(preferences_builder));

	/* mark dialog as transient on parent */
	gtk_window_set_transient_for(GTK_WINDOW(priv->dialog), parent);

	return GTK_WIDGET(priv->dialog);
}
