/*
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 * Copyright 2015 Daniel Dilverstone <dsilvers@netsurf-browser.org>
 * Copyright 2016 Michael Drake <tlsa@netsurf-browser.org>
 *
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
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

#ifndef QJSKY_H
#define QJSKY_H

#include <stdbool.h>
#include <quickjs.h>
#include <dom/dom.h>

typedef enum {
	ELF_CAPTURE = 1 << 0,
	ELF_PASSIVE = 1 << 1,
	ELF_ONCE = 1 << 2,
	ELF_NONE = 0
} event_listener_flags;

typedef enum {
	QJSKY_HIDDEN_HANDLERS,
	QJSKY_HIDDEN_HANDLER_LISTENERS,
	QJSKY_HIDDEN_EVENT_LISTENERS,
	QJSKY_HIDDEN_WINDOW_CALLBACKS,
	QJSKY_HIDDEN_CONSOLE_TIMERS,
	QJSKY_HIDDEN_ATTRIBUTES,
	QJSKY_HIDDEN_CHILD_NODES,
	QJSKY_HIDDEN_CLASS_LIST,
	QJSKY_HIDDEN_CONSOLE,
	QJSKY_HIDDEN_DATA,
	QJSKY_HIDDEN_DOM_IMPLEMENTATION,
	QJSKY_HIDDEN_LOCATION,
	QJSKY_HIDDEN_NAVIGATOR,
	QJSKY_HIDDEN_CONTEXT2D,
	QJSKY_HIDDEN_DETAIL,
	QJSKY_HIDDEN_STYLE,
	QJSKY_HIDDEN_ADDED_NODES,
	QJSKY_HIDDEN_REMOVED_NODES,
	QJSKY_HIDDEN_SEARCH_PARAMS,
	QJSKY_HIDDEN__COUNT
} qjsky_hidden;

JSValue qjsky_node(JSContext *ctx, struct dom_node *node);
JSValue qjsky_event(JSContext *ctx, struct dom_event *evt);
JSValue qjsky_constructed(JSContext *ctx, JSValue obj, JSValueConst new_target);
bool qjsky_node_boxes(struct dom_node *node, double border[4], double content[4]);
void qjsky_viewport_scroll(JSContext *ctx, double *sx, double *sy);
JSValue qjsky_computed_style(JSContext *ctx, struct dom_node *node);
JSValue qjsky_element_style(JSContext *ctx, JSValueConst obj, struct dom_element *element);
struct qjsky_matrix;
struct qjsky_observer;
struct qjsky_observer **qjsky_observers(JSContext *ctx);
JSAtom qjsky_symbol_iterator(JSContext *ctx);
JSValue qjsky_reflected_url(JSContext *ctx, struct dom_string *value);
JSValue qjsky_element_url_get(JSContext *ctx, struct dom_node *element, int part);
JSValue qjsky_element_url_set(JSContext *ctx, struct dom_node *element, int part, JSValueConst value);
struct dom_node *qjsky_svg_root(struct dom_node *node, bool include_self);
bool qjsky_svg_ctm(JSContext *ctx, struct dom_node *node, bool screen, struct qjsky_matrix *out);
void qjsky_event_bind(JSContext *ctx, struct dom_event *evt, JSValueConst obj);
JSValue qjsky_create_event(JSContext *ctx, const char *type_name, struct dom_event **evt);
int qjsky_dict_bool(JSContext *ctx, JSValueConst dict, const char *name, bool *out);
int qjsky_dict_int(JSContext *ctx, JSValueConst dict, const char *name, int32_t *out);
struct dom_string *qjsky_dict_string(JSContext *ctx, JSValueConst dict, const char *name);
JSValue qjsky_current_script(JSContext *ctx);
JSValue qjsky_generic(JSContext *ctx, const char *name);
JSValue qjsky_list_proxy(JSContext *ctx, const char *generic, JSValue inner);

JSValue qjsky_hidden_get(JSContext *ctx,
			 JSValueConst obj,
			 qjsky_hidden key,
			 bool create);
int qjsky_hidden_set(JSContext *ctx,
		     JSValueConst obj,
		     qjsky_hidden key,
		     JSValue val);

JSValue qjsky_call(JSContext *ctx,
		   JSValueConst func,
		   JSValueConst this_val,
		   int argc,
		   JSValueConst *argv,
		   bool reset_timeout);
void qjsky_dump_error(JSContext *ctx);
void qjsky_reset_timeout(JSContext *ctx);

JSValue qjsky_get_event_handler(JSContext *ctx,
				JSValueConst obj,
				dom_string *name,
				dom_event_target *et);
int qjsky_set_event_handler(JSContext *ctx,
			    JSValueConst obj,
			    const char *name,
			    JSValueConst handler);
void qjsky_register_event_listener_for(JSContext *ctx,
				       struct dom_element *ele,
				       dom_string *name,
				       bool capture);
JSValue qjsky_event_target_listeners(JSContext *ctx,
				     JSValueConst obj,
				     JSValueConst type,
				     bool create,
				     bool *created);

int qjsky_define_method(JSContext *ctx,
			JSValueConst proto,
			const char *name,
			JSCFunction *fn,
			int length);
int qjsky_define_accessor(JSContext *ctx,
			  JSValueConst proto,
			  const char *name,
			  JSCFunction *getter,
			  JSCFunction *setter);
int qjsky_define_constant(JSContext *ctx,
			  JSValueConst proto,
			  JSValueConst ctor,
			  const char *name,
			  int value);

void qjsky_window_closedown(JSContext *ctx, void *window_priv);

JSValue qjsky_string(JSContext *ctx, dom_string *str);
dom_string *qjsky_to_dom_string(JSContext *ctx, JSValueConst val);

#endif
