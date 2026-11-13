/*
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 * Copyright 2015 Daniel Dilverstone <dsilvers@netsurf-browser.org>
 * Copyright 2016 Michael Drake <tlsa@netsurf-browser.org>
 * Copyright 2016 John-Mark Bell <jmb@netsurf-browser.org>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <nsutils/time.h>

#include "netsurfy/inttypes.h"
#include "utils/utils.h"
#include "utils/nsoption.h"
#include "utils/log.h"
#include "utils/corestrings.h"
#include "content/content.h"
#include "content/fetch.h"
#include "utils/nsurl.h"
#include "html/private.h"

#include "javascript/js.h"
#include "javascript/content.h"

#include "quickjs/binding.h"
#include "quickjs/private.h"
#include "quickjs/prototype.h"
#include "quickjs/prelude.js.inc"

#include "qjsky.h"

#include <dom/dom.h>

#define QJSKY_MEMORY_LIMIT (256u * 1024u * 1024u)
#define QJSKY_MAX_JOBS 100000
#define QJSKY_MAX_FETCH_BODY (64u * 1024u * 1024u)

struct qjsky_fetch {
	struct qjsky_fetch *next;
	jsthread *thread;
	struct fetch *fetch;
	uint32_t id;
	JSValue callback;
	uint8_t *body;
	size_t body_len;
	size_t body_alloc;
	char **headers;
	size_t n_headers;
};

struct qjsky_map_slot {
	void *key;
	JSValue val;
};

struct qjsky_map {
	size_t count;
	size_t cap;
	struct qjsky_map_slot *slots;
};

struct jsheap {
	JSRuntime *rt;
	uint64_t timeout_ms;
	bool pending_destroy;
	unsigned int live_threads;
	uint64_t exec_start_time;
};

struct jsthread {
	bool pending_destroy;
	unsigned int in_use;
	unsigned int call_depth;
	jsheap *heap;
	JSContext *ctx;
	JSValue global;
	JSValue generics;
	JSValue protos[QJSKY_IFACE__COUNT];
	JSAtom hidden[QJSKY_HIDDEN__COUNT];
	struct qjsky_map nodes;
	struct qjsky_map events;
	window_private_t *window_priv;
	struct dom_node *current_script;
	struct qjsky_fetch *fetches;
	uint32_t next_fetch_id;
};

static JSClassID qjsky_class_id;

static const char *const qjsky_hidden_names[QJSKY_HIDDEN__COUNT] = {
	[QJSKY_HIDDEN_HANDLERS] = "handlers",
	[QJSKY_HIDDEN_HANDLER_LISTENERS] = "handler listeners",
	[QJSKY_HIDDEN_EVENT_LISTENERS] = "event listeners",
	[QJSKY_HIDDEN_WINDOW_CALLBACKS] = "window callbacks",
	[QJSKY_HIDDEN_CONSOLE_TIMERS] = "console timers",
	[QJSKY_HIDDEN_ATTRIBUTES] = "attributes",
	[QJSKY_HIDDEN_CHILD_NODES] = "child nodes",
	[QJSKY_HIDDEN_CLASS_LIST] = "class list",
	[QJSKY_HIDDEN_CONSOLE] = "console",
	[QJSKY_HIDDEN_DATA] = "data",
	[QJSKY_HIDDEN_DOM_IMPLEMENTATION] = "dom implementation",
	[QJSKY_HIDDEN_LOCATION] = "location",
	[QJSKY_HIDDEN_NAVIGATOR] = "navigator",
	[QJSKY_HIDDEN_CONTEXT2D] = "context2d",
};

static size_t qjsky_map_hash(void *key, size_t cap)
{
	uintptr_t x = (uintptr_t)key >> 3;
	x ^= x >> 15;
	x *= (uintptr_t)2654435761u;
	x ^= x >> 13;
	return (size_t)x & (cap - 1);
}

static JSValue qjsky_map_get(struct qjsky_map *map, void *key)
{
	size_t idx;

	if (map->cap == 0) {
		return JS_UNDEFINED;
	}
	idx = qjsky_map_hash(key, map->cap);
	while (map->slots[idx].key != NULL) {
		if (map->slots[idx].key == key) {
			return map->slots[idx].val;
		}
		idx = (idx + 1) & (map->cap - 1);
	}
	return JS_UNDEFINED;
}

static bool qjsky_map_grow(struct qjsky_map *map)
{
	size_t cap = map->cap == 0 ? 64 : map->cap * 2;
	struct qjsky_map_slot *slots = calloc(cap, sizeof(*slots));

	if (slots == NULL) {
		return false;
	}
	for (size_t i = 0; i < map->cap; i++) {
		if (map->slots[i].key != NULL) {
			size_t idx = qjsky_map_hash(map->slots[i].key, cap);
			while (slots[idx].key != NULL) {
				idx = (idx + 1) & (cap - 1);
			}
			slots[idx] = map->slots[i];
		}
	}
	free(map->slots);
	map->slots = slots;
	map->cap = cap;
	return true;
}

static bool qjsky_map_put(struct qjsky_map *map, void *key, JSValue val)
{
	size_t idx;

	if ((map->count + 1) * 4 > map->cap * 3 && !qjsky_map_grow(map)) {
		return false;
	}
	idx = qjsky_map_hash(key, map->cap);
	while (map->slots[idx].key != NULL) {
		idx = (idx + 1) & (map->cap - 1);
	}
	map->slots[idx].key = key;
	map->slots[idx].val = val;
	map->count++;
	return true;
}

static JSValue qjsky_map_take(struct qjsky_map *map, void *key)
{
	size_t idx, next;
	JSValue val;

	if (map->cap == 0) {
		return JS_UNDEFINED;
	}
	idx = qjsky_map_hash(key, map->cap);
	while (map->slots[idx].key != key) {
		if (map->slots[idx].key == NULL) {
			return JS_UNDEFINED;
		}
		idx = (idx + 1) & (map->cap - 1);
	}
	val = map->slots[idx].val;
	map->slots[idx].key = NULL;
	map->count--;
	next = (idx + 1) & (map->cap - 1);
	while (map->slots[next].key != NULL) {
		size_t home = qjsky_map_hash(map->slots[next].key, map->cap);
		if (((next - home) & (map->cap - 1)) >=
		    ((next - idx) & (map->cap - 1))) {
			map->slots[idx] = map->slots[next];
			map->slots[next].key = NULL;
			idx = next;
		}
		next = (next + 1) & (map->cap - 1);
	}
	return val;
}

static void qjsky_map_clear(JSContext *ctx, struct qjsky_map *map)
{
	for (size_t i = 0; i < map->cap; i++) {
		if (map->slots[i].key != NULL) {
			JS_FreeValue(ctx, map->slots[i].val);
		}
	}
	free(map->slots);
	map->slots = NULL;
	map->cap = 0;
	map->count = 0;
}

static jsthread *qjsky_thread(JSContext *ctx)
{
	return JS_GetContextOpaque(ctx);
}

static void qjsky_finalizer(JSRuntime *rt, JSValueConst val)
{
	qjsky_object_t *priv = JS_GetOpaque(val, qjsky_class_id);

	if (priv == NULL) {
		return;
	}
	if (qjsky_iface_fini[priv->iface] != NULL) {
		qjsky_iface_fini[priv->iface](rt, priv);
	}
	free(priv);
}

static const JSClassDef qjsky_class = {
	.class_name = "NetSurfyObject",
	.finalizer = qjsky_finalizer,
};

static int qjsky_interrupt(JSContext *ctx, void *opaque)
{
	jsheap *heap = opaque;
	uint64_t now;

	if (heap->exec_start_time == 0) {
		return 0;
	}
	(void)nsu_getmonotonic_ms(&now);
	return now > heap->exec_start_time + heap->timeout_ms;
}

void qjsky_reset_timeout(JSContext *ctx)
{
	jsheap *heap = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));

	(void)nsu_getmonotonic_ms(&heap->exec_start_time);
}

void qjsky_dump_error(JSContext *ctx)
{
	JSValue exc = JS_GetException(ctx);
	const char *msg = NULL;
	const char *stack = NULL;
	JSValue stackval = JS_UNDEFINED;

	if (JS_IsUncatchableError(exc)) {
		NSLOG(jserrors,
		      WARNING,
		      "JS execution interrupted after timeout");
		JS_FreeValue(ctx, exc);
		return;
	}
	msg = JS_ToCString(ctx, exc);
	if (msg == NULL) {
		JS_FreeValue(ctx, JS_GetException(ctx));
	}
	if (JS_IsError(exc)) {
		stackval = JS_GetPropertyStr(ctx, exc, "stack");
		if (JS_IsString(stackval)) {
			stack = JS_ToCString(ctx, stackval);
		}
	}
	NSLOG(jserrors,
	      WARNING,
	      "Uncaught error in JS: %s%s%s",
	      msg != NULL ? msg : "(unprintable)",
	      stack != NULL ? "\n" : "",
	      stack != NULL ? stack : "");
	if (stack != NULL) {
		JS_FreeCString(ctx, stack);
	}
	if (msg != NULL) {
		JS_FreeCString(ctx, msg);
	}
	JS_FreeValue(ctx, stackval);
	JS_FreeValue(ctx, exc);
}

static void qjsky_run_jobs(jsthread *thread)
{
	JSRuntime *rt = thread->heap->rt;
	JSContext *job_ctx;

	if (thread->call_depth != 0) {
		return;
	}
	thread->call_depth++;
	for (int count = 0; count < QJSKY_MAX_JOBS; count++) {
		int res = JS_ExecutePendingJob(rt, &job_ctx);
		if (res == 0) {
			break;
		}
		if (res < 0) {
			qjsky_dump_error(job_ctx);
		}
	}
	thread->call_depth--;
}

JSValue qjsky_call(JSContext *ctx,
		   JSValueConst func,
		   JSValueConst this_val,
		   int argc,
		   JSValueConst *argv,
		   bool reset_timeout)
{
	jsthread *thread = qjsky_thread(ctx);
	JSValue ret;

	if (reset_timeout) {
		qjsky_reset_timeout(ctx);
	}
	thread->call_depth++;
	ret = JS_Call(ctx, func, this_val, argc, argv);
	thread->call_depth--;
	if (JS_IsException(ret)) {
		qjsky_dump_error(ctx);
	}
	qjsky_run_jobs(thread);
	return ret;
}

static JSValue
qjsky_eval(JSContext *ctx, const char *src, size_t len, const char *name)
{
	char *buf = malloc(len + 1);
	JSValue ret;

	if (buf == NULL) {
		return JS_ThrowOutOfMemory(ctx);
	}
	memcpy(buf, src, len);
	buf[len] = '\0';
	ret = JS_Eval(ctx, buf, len, name, JS_EVAL_TYPE_GLOBAL);
	free(buf);
	return ret;
}

void *qjsky_global_private(JSContext *ctx)
{
	return qjsky_thread(ctx)->window_priv;
}

void *qjsky_private(JSContext *ctx, JSValueConst val, int iface)
{
	jsthread *thread = qjsky_thread(ctx);
	qjsky_object_t *priv;

	if (JS_IsUndefined(val) || JS_IsNull(val) ||
	    (JS_IsObject(val) &&
	     JS_VALUE_GET_PTR(val) == JS_VALUE_GET_PTR(thread->global))) {
		if (qjsky_iface_is(QJSKY_IFACE_WINDOW, iface)) {
			return thread->window_priv;
		}
		return NULL;
	}
	if (!JS_IsObject(val)) {
		return NULL;
	}
	if (JS_IsProxy(val)) {
		JSValue target = JS_GetProxyTarget(ctx, val);
		if (JS_IsException(target)) {
			JS_FreeValue(ctx, JS_GetException(ctx));
			return NULL;
		}
		priv = qjsky_private(ctx, target, iface);
		JS_FreeValue(ctx, target);
		return priv;
	}
	priv = JS_GetOpaque(val, qjsky_class_id);
	if (priv == NULL || !qjsky_iface_is(priv->iface, iface)) {
		return NULL;
	}
	return priv;
}

JSValue qjsky_new_object(JSContext *ctx, int iface, void *priv)
{
	jsthread *thread = qjsky_thread(ctx);
	JSValue obj;

	obj = JS_NewObjectProtoClass(ctx,
				     thread->protos[iface],
				     qjsky_class_id);
	if (JS_IsException(obj)) {
		if (qjsky_iface_fini[iface] != NULL) {
			qjsky_iface_fini[iface](JS_GetRuntime(ctx), priv);
		}
		free(priv);
		return obj;
	}
	JS_SetOpaque(obj, priv);
	return obj;
}

JSValue qjsky_string(JSContext *ctx, dom_string *str)
{
	if (str == NULL) {
		return JS_NULL;
	}
	return JS_NewStringLen(ctx,
			       dom_string_data(str),
			       dom_string_length(str));
}

dom_string *qjsky_to_dom_string(JSContext *ctx, JSValueConst val)
{
	size_t len;
	const char *s = JS_ToCStringLen(ctx, &len, val);
	dom_string *str = NULL;

	if (s == NULL) {
		return NULL;
	}
	if (dom_string_create((const uint8_t *)s, len, &str) != DOM_NO_ERR) {
		str = NULL;
		JS_ThrowOutOfMemory(ctx);
	}
	JS_FreeCString(ctx, s);
	return str;
}

JSValue qjsky_hidden_get(JSContext *ctx,
			 JSValueConst obj,
			 qjsky_hidden key,
			 bool create)
{
	jsthread *thread = qjsky_thread(ctx);
	JSValue val = JS_GetProperty(ctx, obj, thread->hidden[key]);

	if (JS_IsUndefined(val) && create) {
		val = JS_NewObject(ctx);
		if (JS_IsException(val)) {
			return val;
		}
		if (JS_DefinePropertyValue(ctx,
					   obj,
					   thread->hidden[key],
					   JS_DupValue(ctx, val),
					   JS_PROP_CONFIGURABLE |
						   JS_PROP_WRITABLE) < 0) {
			JS_FreeValue(ctx, val);
			return JS_EXCEPTION;
		}
	}
	return val;
}

int qjsky_hidden_set(JSContext *ctx,
		     JSValueConst obj,
		     qjsky_hidden key,
		     JSValue val)
{
	jsthread *thread = qjsky_thread(ctx);

	return JS_DefinePropertyValue(ctx,
				      obj,
				      thread->hidden[key],
				      val,
				      JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
}

JSValue qjsky_generic(JSContext *ctx, const char *name)
{
	return JS_GetPropertyStr(ctx, qjsky_thread(ctx)->generics, name);
}

JSValue qjsky_list_proxy(JSContext *ctx, const char *generic, JSValue inner)
{
	JSValue fn, ret;

	if (JS_IsException(inner)) {
		return inner;
	}
	fn = qjsky_generic(ctx, generic);
	ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, (JSValueConst *)&inner);
	JS_FreeValue(ctx, fn);
	JS_FreeValue(ctx, inner);
	return ret;
}

int qjsky_define_method(JSContext *ctx,
			JSValueConst proto,
			const char *name,
			JSCFunction *fn,
			int length)
{
	JSValue func = JS_NewCFunction2(
		ctx, fn, name, length, JS_CFUNC_generic, 0);

	if (JS_IsException(func)) {
		return -1;
	}
	return JS_DefinePropertyValueStr(ctx, proto, name, func, JS_PROP_C_W_E);
}

int qjsky_define_accessor(JSContext *ctx,
			  JSValueConst proto,
			  const char *name,
			  JSCFunction *getter,
			  JSCFunction *setter)
{
	JSValue get = JS_UNDEFINED, set = JS_UNDEFINED;
	JSAtom atom;
	int ret;

	if (getter != NULL) {
		get = JS_NewCFunction2(
			ctx, getter, name, 0, JS_CFUNC_generic, 0);
		if (JS_IsException(get)) {
			return -1;
		}
	}
	if (setter != NULL) {
		set = JS_NewCFunction2(
			ctx, setter, name, 1, JS_CFUNC_generic, 0);
		if (JS_IsException(set)) {
			JS_FreeValue(ctx, get);
			return -1;
		}
	}
	atom = JS_NewAtom(ctx, name);
	ret = JS_DefinePropertyGetSet(ctx,
				      proto,
				      atom,
				      get,
				      set,
				      JS_PROP_CONFIGURABLE |
					      JS_PROP_ENUMERABLE);
	JS_FreeAtom(ctx, atom);
	return ret;
}

int qjsky_define_constant(JSContext *ctx,
			  JSValueConst proto,
			  JSValueConst ctor,
			  const char *name,
			  int value)
{
	if (JS_DefinePropertyValueStr(ctx,
				      proto,
				      name,
				      JS_NewInt32(ctx, value),
				      JS_PROP_ENUMERABLE) < 0) {
		return -1;
	}
	if (JS_IsObject(ctor) &&
	    JS_DefinePropertyValueStr(ctx,
				      ctor,
				      name,
				      JS_NewInt32(ctx, value),
				      JS_PROP_ENUMERABLE) < 0) {
		return -1;
	}
	return 0;
}

static int qjsky_node_iface_html(dom_html_element_type type)
{
	switch (type) {
	case DOM_HTML_ELEMENT_TYPE_HTML:
		return QJSKY_IFACE_HTMLHTMLELEMENT;
	case DOM_HTML_ELEMENT_TYPE_HEAD:
		return QJSKY_IFACE_HTMLHEADELEMENT;
	case DOM_HTML_ELEMENT_TYPE_META:
		return QJSKY_IFACE_HTMLMETAELEMENT;
	case DOM_HTML_ELEMENT_TYPE_BASE:
		return QJSKY_IFACE_HTMLBASEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_TITLE:
		return QJSKY_IFACE_HTMLTITLEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_BODY:
		return QJSKY_IFACE_HTMLBODYELEMENT;
	case DOM_HTML_ELEMENT_TYPE_DIV:
		return QJSKY_IFACE_HTMLDIVELEMENT;
	case DOM_HTML_ELEMENT_TYPE_FORM:
		return QJSKY_IFACE_HTMLFORMELEMENT;
	case DOM_HTML_ELEMENT_TYPE_LINK:
		return QJSKY_IFACE_HTMLLINKELEMENT;
	case DOM_HTML_ELEMENT_TYPE_BUTTON:
		return QJSKY_IFACE_HTMLBUTTONELEMENT;
	case DOM_HTML_ELEMENT_TYPE_INPUT:
		return QJSKY_IFACE_HTMLINPUTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_TEXTAREA:
		return QJSKY_IFACE_HTMLTEXTAREAELEMENT;
	case DOM_HTML_ELEMENT_TYPE_OPTGROUP:
		return QJSKY_IFACE_HTMLOPTGROUPELEMENT;
	case DOM_HTML_ELEMENT_TYPE_OPTION:
		return QJSKY_IFACE_HTMLOPTIONELEMENT;
	case DOM_HTML_ELEMENT_TYPE_SELECT:
		return QJSKY_IFACE_HTMLSELECTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_HR:
		return QJSKY_IFACE_HTMLHRELEMENT;
	case DOM_HTML_ELEMENT_TYPE_DL:
		return QJSKY_IFACE_HTMLDLISTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_DIR:
		return QJSKY_IFACE_HTMLDIRECTORYELEMENT;
	case DOM_HTML_ELEMENT_TYPE_MENU:
		return QJSKY_IFACE_HTMLMENUELEMENT;
	case DOM_HTML_ELEMENT_TYPE_FIELDSET:
		return QJSKY_IFACE_HTMLFIELDSETELEMENT;
	case DOM_HTML_ELEMENT_TYPE_LEGEND:
		return QJSKY_IFACE_HTMLLEGENDELEMENT;
	case DOM_HTML_ELEMENT_TYPE_P:
		return QJSKY_IFACE_HTMLPARAGRAPHELEMENT;
	case DOM_HTML_ELEMENT_TYPE_H1:
	case DOM_HTML_ELEMENT_TYPE_H2:
	case DOM_HTML_ELEMENT_TYPE_H3:
	case DOM_HTML_ELEMENT_TYPE_H4:
	case DOM_HTML_ELEMENT_TYPE_H5:
	case DOM_HTML_ELEMENT_TYPE_H6:
		return QJSKY_IFACE_HTMLHEADINGELEMENT;
	case DOM_HTML_ELEMENT_TYPE_BLOCKQUOTE:
	case DOM_HTML_ELEMENT_TYPE_Q:
		return QJSKY_IFACE_HTMLQUOTEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_PRE:
		return QJSKY_IFACE_HTMLPREELEMENT;
	case DOM_HTML_ELEMENT_TYPE_BR:
		return QJSKY_IFACE_HTMLBRELEMENT;
	case DOM_HTML_ELEMENT_TYPE_LABEL:
		return QJSKY_IFACE_HTMLLABELELEMENT;
	case DOM_HTML_ELEMENT_TYPE_UL:
		return QJSKY_IFACE_HTMLULISTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_OL:
		return QJSKY_IFACE_HTMLOLISTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_LI:
		return QJSKY_IFACE_HTMLLIELEMENT;
	case DOM_HTML_ELEMENT_TYPE_FONT:
		return QJSKY_IFACE_HTMLFONTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_DEL:
	case DOM_HTML_ELEMENT_TYPE_INS:
		return QJSKY_IFACE_HTMLMODELEMENT;
	case DOM_HTML_ELEMENT_TYPE_A:
		return QJSKY_IFACE_HTMLANCHORELEMENT;
	case DOM_HTML_ELEMENT_TYPE_IMG:
		return QJSKY_IFACE_HTMLIMAGEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_OBJECT:
		return QJSKY_IFACE_HTMLOBJECTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_PARAM:
		return QJSKY_IFACE_HTMLPARAMELEMENT;
	case DOM_HTML_ELEMENT_TYPE_APPLET:
		return QJSKY_IFACE_HTMLAPPLETELEMENT;
	case DOM_HTML_ELEMENT_TYPE_MAP:
		return QJSKY_IFACE_HTMLMAPELEMENT;
	case DOM_HTML_ELEMENT_TYPE_AREA:
		return QJSKY_IFACE_HTMLAREAELEMENT;
	case DOM_HTML_ELEMENT_TYPE_SCRIPT:
		return QJSKY_IFACE_HTMLSCRIPTELEMENT;
	case DOM_HTML_ELEMENT_TYPE_CAPTION:
		return QJSKY_IFACE_HTMLTABLECAPTIONELEMENT;
	case DOM_HTML_ELEMENT_TYPE_TD:
	case DOM_HTML_ELEMENT_TYPE_TH:
		return QJSKY_IFACE_HTMLTABLECELLELEMENT;
	case DOM_HTML_ELEMENT_TYPE_COL:
	case DOM_HTML_ELEMENT_TYPE_COLGROUP:
		return QJSKY_IFACE_HTMLTABLECOLELEMENT;
	case DOM_HTML_ELEMENT_TYPE_THEAD:
	case DOM_HTML_ELEMENT_TYPE_TBODY:
	case DOM_HTML_ELEMENT_TYPE_TFOOT:
		return QJSKY_IFACE_HTMLTABLESECTIONELEMENT;
	case DOM_HTML_ELEMENT_TYPE_TABLE:
		return QJSKY_IFACE_HTMLTABLEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_TR:
		return QJSKY_IFACE_HTMLTABLEROWELEMENT;
	case DOM_HTML_ELEMENT_TYPE_STYLE:
		return QJSKY_IFACE_HTMLSTYLEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_FRAMESET:
		return QJSKY_IFACE_HTMLFRAMESETELEMENT;
	case DOM_HTML_ELEMENT_TYPE_FRAME:
		return QJSKY_IFACE_HTMLFRAMEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_IFRAME:
		return QJSKY_IFACE_HTMLIFRAMEELEMENT;
	case DOM_HTML_ELEMENT_TYPE_CANVAS:
		return QJSKY_IFACE_HTMLCANVASELEMENT;
	case DOM_HTML_ELEMENT_TYPE_ISINDEX:
	case DOM_HTML_ELEMENT_TYPE__COUNT:
	case DOM_HTML_ELEMENT_TYPE__UNKNOWN:
		return QJSKY_IFACE_HTMLUNKNOWNELEMENT;
	default:
		return QJSKY_IFACE_HTMLELEMENT;
	}
}

static int qjsky_node_iface(struct dom_node *node)
{
	dom_node_type nodetype;
	dom_string *namespace;
	dom_html_element_type type;
	bool is_html;

	if (dom_node_get_node_type(node, &nodetype) != DOM_NO_ERR) {
		return QJSKY_IFACE_NODE;
	}
	switch (nodetype) {
	case DOM_ELEMENT_NODE:
		if (dom_node_get_namespace(node, &namespace) != DOM_NO_ERR ||
		    namespace == NULL) {
			return QJSKY_IFACE_ELEMENT;
		}
		is_html = dom_string_isequal(namespace,
					     corestring_dom_html_namespace);
		dom_string_unref(namespace);
		if (!is_html) {
			return QJSKY_IFACE_ELEMENT;
		}
		if (dom_html_element_get_tag_type(node, &type) != DOM_NO_ERR) {
			type = DOM_HTML_ELEMENT_TYPE__UNKNOWN;
		}
		if (type == DOM_HTML_ELEMENT_TYPE__UNKNOWN) {
			dom_string *name = NULL;
			bool custom = false;
			if (dom_node_get_node_name(node, &name) == DOM_NO_ERR &&
			    name != NULL) {
				custom = memchr(dom_string_data(name),
						'-',
						dom_string_byte_length(name)) !=
					 NULL;
				dom_string_unref(name);
			}
			if (custom) {
				return QJSKY_IFACE_HTMLELEMENT;
			}
		}
		return qjsky_node_iface_html(type);
	case DOM_TEXT_NODE:
		return QJSKY_IFACE_TEXT;
	case DOM_COMMENT_NODE:
		return QJSKY_IFACE_COMMENT;
	case DOM_DOCUMENT_NODE:
		return QJSKY_IFACE_DOCUMENT;
	case DOM_ATTRIBUTE_NODE:
		return QJSKY_IFACE_ATTR;
	default:
		return QJSKY_IFACE_NODE;
	}
}

JSValue qjsky_node(JSContext *ctx, struct dom_node *node)
{
	jsthread *thread = qjsky_thread(ctx);
	JSValue obj;
	int iface;

	if (node == NULL) {
		return JS_NULL;
	}
	obj = qjsky_map_get(&thread->nodes, node);
	if (!JS_IsUndefined(obj)) {
		return JS_DupValue(ctx, obj);
	}
	iface = qjsky_node_iface(node);
	if (qjsky_iface_create[iface] == NULL) {
		iface = QJSKY_IFACE_NODE;
	}
	obj = qjsky_iface_create[iface](ctx, node);
	if (JS_IsException(obj)) {
		return obj;
	}
	if (!qjsky_map_put(&thread->nodes, node, JS_DupValue(ctx, obj))) {
		JS_FreeValue(ctx, obj);
		return JS_ThrowOutOfMemory(ctx);
	}
	return obj;
}

static int qjsky_event_iface(dom_event *evt)
{
	dom_event_kind kind;

	if (dom_event_get_kind(evt, &kind) != DOM_NO_ERR) {
		return QJSKY_IFACE_EVENT;
	}
	switch (kind) {
	case DOM_EVENT_KIND_KEYBOARD:
		return QJSKY_IFACE_KEYBOARDEVENT;
	case DOM_EVENT_KIND_MUTATION:
	case DOM_EVENT_KIND_MUTATION_NAME:
		return QJSKY_IFACE_MUTATIONEVENT;
	default:
		return QJSKY_IFACE_EVENT;
	}
}

JSValue qjsky_event(JSContext *ctx, struct dom_event *evt)
{
	jsthread *thread = qjsky_thread(ctx);
	JSValue obj;

	if (evt == NULL) {
		return JS_NULL;
	}
	obj = qjsky_map_get(&thread->events, evt);
	if (!JS_IsUndefined(obj)) {
		return JS_DupValue(ctx, obj);
	}
	obj = qjsky_iface_create[qjsky_event_iface(evt)](ctx, evt);
	if (JS_IsException(obj)) {
		return obj;
	}
	if (!qjsky_map_put(&thread->events, evt, JS_DupValue(ctx, obj))) {
		JS_FreeValue(ctx, obj);
		return JS_ThrowOutOfMemory(ctx);
	}
	return obj;
}

static JSValue
qjsky_compile_handler(JSContext *ctx, dom_string *name, dom_event_target *et)
{
	dom_string *onname, *val = NULL;
	dom_node_type ntype;
	JSValue ret;
	size_t len;
	char *src;
	static const char prefix[] = "(function (event) {\n";
	static const char suffix[] = "\n})";

	if (et == NULL) {
		return JS_NULL;
	}
	if (dom_node_get_node_type(et, &ntype) != DOM_NO_ERR ||
	    ntype != DOM_ELEMENT_NODE) {
		return JS_NULL;
	}
	if (dom_string_concat(corestring_dom_on, name, &onname) != DOM_NO_ERR) {
		return JS_NULL;
	}
	if (dom_element_get_attribute((dom_element *)et, onname, &val) !=
		    DOM_NO_ERR ||
	    val == NULL) {
		dom_string_unref(onname);
		return JS_NULL;
	}
	dom_string_unref(onname);

	len = SLEN(prefix) + dom_string_length(val) + SLEN(suffix);
	src = malloc(len + 1);
	if (src == NULL) {
		dom_string_unref(val);
		return JS_NULL;
	}
	memcpy(src, prefix, SLEN(prefix));
	memcpy(src + SLEN(prefix),
	       dom_string_data(val),
	       dom_string_length(val));
	memcpy(src + SLEN(prefix) + dom_string_length(val),
	       suffix,
	       SLEN(suffix));
	src[len] = '\0';
	dom_string_unref(val);

	ret = JS_Eval(
		ctx, src, len, "event handler attribute", JS_EVAL_TYPE_GLOBAL);
	free(src);
	if (JS_IsException(ret)) {
		qjsky_dump_error(ctx);
		return JS_NULL;
	}
	return ret;
}

JSValue qjsky_get_event_handler(JSContext *ctx,
				JSValueConst obj,
				dom_string *name,
				dom_event_target *et)
{
	JSValue handlers = qjsky_hidden_get(
		ctx, obj, QJSKY_HIDDEN_HANDLERS, false);

	if (JS_IsObject(handlers)) {
		JSAtom atom = JS_NewAtomLen(ctx,
					    dom_string_data(name),
					    dom_string_length(name));
		int has = JS_HasProperty(ctx, handlers, atom);
		if (has > 0) {
			JSValue handler = JS_GetProperty(ctx, handlers, atom);
			JS_FreeAtom(ctx, atom);
			JS_FreeValue(ctx, handlers);
			return handler;
		}
		JS_FreeAtom(ctx, atom);
	}
	JS_FreeValue(ctx, handlers);
	return qjsky_compile_handler(ctx, name, et);
}

int qjsky_set_event_handler(JSContext *ctx,
			    JSValueConst obj,
			    const char *name,
			    JSValueConst handler)
{
	JSValue handlers = qjsky_hidden_get(
		ctx, obj, QJSKY_HIDDEN_HANDLERS, true);
	JSValue val;
	int ret;

	if (JS_IsException(handlers)) {
		return -1;
	}
	val = JS_IsObject(handler) ? JS_DupValue(ctx, handler) : JS_NULL;
	ret = JS_SetPropertyStr(ctx, handlers, name, val);
	JS_FreeValue(ctx, handlers);
	return ret < 0 ? -1 : 0;
}

JSValue qjsky_event_target_listeners(JSContext *ctx,
				     JSValueConst obj,
				     JSValueConst type,
				     bool create,
				     bool *created)
{
	JSValue map, arr;
	JSAtom atom;

	if (created != NULL) {
		*created = false;
	}
	map = qjsky_hidden_get(ctx, obj, QJSKY_HIDDEN_EVENT_LISTENERS, create);
	if (!JS_IsObject(map)) {
		return map;
	}
	atom = JS_ValueToAtom(ctx, type);
	if (atom == JS_ATOM_NULL) {
		JS_FreeValue(ctx, map);
		return JS_EXCEPTION;
	}
	arr = JS_GetProperty(ctx, map, atom);
	if (JS_IsUndefined(arr) && create) {
		arr = JS_NewArray(ctx);
		if (!JS_IsException(arr)) {
			if (JS_SetProperty(
				    ctx, map, atom, JS_DupValue(ctx, arr)) <
			    0) {
				JS_FreeValue(ctx, arr);
				arr = JS_EXCEPTION;
			} else if (created != NULL) {
				*created = true;
			}
		}
	}
	JS_FreeAtom(ctx, atom);
	JS_FreeValue(ctx, map);
	return arr;
}

static void qjsky_listener_remove(JSContext *ctx,
				  JSValueConst listeners,
				  JSValueConst entry)
{
	int64_t len = 0;

	JS_SetPropertyUint32(ctx, entry, 2, JS_TRUE);
	if (JS_GetLength(ctx, listeners, &len) < 0) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return;
	}
	for (int64_t idx = 0; idx < len; idx++) {
		JSValue cur = JS_GetPropertyInt64(ctx, listeners, idx);
		bool same = JS_IsStrictEqual(ctx, cur, entry);
		JS_FreeValue(ctx, cur);
		if (same) {
			JSValue splice = JS_GetPropertyStr(ctx,
							   listeners,
							   "splice");
			JSValue args[2] = {JS_NewInt64(ctx, idx),
					   JS_NewInt32(ctx, 1)};
			JSValue ret = JS_Call(ctx, splice, listeners, 2, args);
			JS_FreeValue(ctx, ret);
			JS_FreeValue(ctx, splice);
			return;
		}
	}
}

static void qjsky_run_listeners(JSContext *ctx,
				JSValueConst target,
				JSValueConst type,
				JSValueConst event,
				dom_event_flow_phase phase)
{
	JSValue listeners = qjsky_event_target_listeners(
		ctx, target, type, false, NULL);
	JSValue *entries = NULL;
	int64_t len = 0;

	if (!JS_IsObject(listeners)) {
		if (JS_IsException(listeners)) {
			JS_FreeValue(ctx, JS_GetException(ctx));
		}
		return;
	}
	if (JS_GetLength(ctx, listeners, &len) < 0 || len <= 0) {
		JS_FreeValue(ctx, listeners);
		return;
	}
	entries = calloc((size_t)len, sizeof(JSValue));
	if (entries == NULL) {
		JS_FreeValue(ctx, listeners);
		return;
	}
	for (int64_t idx = 0; idx < len; idx++) {
		entries[idx] = JS_GetPropertyInt64(ctx, listeners, idx);
	}

	for (int64_t idx = 0; idx < len; idx++) {
		JSValue entry = entries[idx];
		JSValue removed = JS_GetPropertyUint32(ctx, entry, 2);
		JSValue fl = JS_GetPropertyUint32(ctx, entry, 1);
		JSValue cb, fn, thisv, ret;
		int32_t flags = 0;
		bool skip = JS_ToBool(ctx, removed) > 0;

		JS_FreeValue(ctx, removed);
		(void)JS_ToInt32(ctx, &flags, fl);
		JS_FreeValue(ctx, fl);
		if (skip) {
			continue;
		}
		if ((phase == DOM_CAPTURING_PHASE && !(flags & ELF_CAPTURE)) ||
		    (phase == DOM_BUBBLING_PHASE && (flags & ELF_CAPTURE))) {
			continue;
		}
		if (flags & ELF_ONCE) {
			qjsky_listener_remove(ctx, listeners, entry);
		}
		cb = JS_GetPropertyUint32(ctx, entry, 0);
		if (JS_IsFunction(ctx, cb)) {
			fn = JS_DupValue(ctx, cb);
			thisv = JS_DupValue(ctx, target);
		} else {
			fn = JS_GetPropertyStr(ctx, cb, "handleEvent");
			thisv = JS_DupValue(ctx, cb);
		}
		if (JS_IsFunction(ctx, fn)) {
			ret = qjsky_call(ctx, fn, thisv, 1, &event, true);
			JS_FreeValue(ctx, ret);
		} else if (JS_IsException(fn)) {
			qjsky_dump_error(ctx);
		}
		JS_FreeValue(ctx, thisv);
		JS_FreeValue(ctx, fn);
		JS_FreeValue(ctx, cb);
	}

	for (int64_t idx = 0; idx < len; idx++) {
		JS_FreeValue(ctx, entries[idx]);
	}
	free(entries);
	JS_FreeValue(ctx, listeners);
}

static void qjsky_run_handler(JSContext *ctx,
			      JSValueConst target,
			      dom_string *name,
			      dom_event_target *et,
			      JSValueConst event,
			      dom_event *evt)
{
	JSValue handler = qjsky_get_event_handler(ctx, target, name, et);
	JSValue ret;

	if (JS_IsFunction(ctx, handler)) {
		ret = qjsky_call(ctx, handler, target, 1, &event, true);
		if (JS_IsBool(ret) && !JS_ToBool(ctx, ret) && evt != NULL) {
			(void)dom_event_prevent_default(evt);
		}
		JS_FreeValue(ctx, ret);
	}
	JS_FreeValue(ctx, handler);
}

static void qjsky_enter_thread(jsthread *thread);
static void qjsky_leave_thread(jsthread *thread);

static void
qjsky_event_dispatch(dom_event *evt, jsthread *thread, bool capture_listener)
{
	JSContext *ctx = thread->ctx;
	dom_string *name = NULL;
	dom_event_target *targ = NULL;
	dom_event_flow_phase phase;
	JSValue node, event, type;

	if (thread->pending_destroy) {
		return;
	}
	if (dom_event_get_event_phase(evt, &phase) != DOM_NO_ERR) {
		return;
	}
	if (capture_listener != (phase == DOM_CAPTURING_PHASE)) {
		return;
	}
	if (dom_event_get_type(evt, &name) != DOM_NO_ERR || name == NULL) {
		return;
	}
	if (dom_event_get_current_target(evt, &targ) != DOM_NO_ERR ||
	    targ == NULL) {
		dom_string_unref(name);
		return;
	}

	qjsky_enter_thread(thread);
	node = qjsky_node(ctx, (dom_node *)targ);
	event = qjsky_event(ctx, evt);
	type = qjsky_string(ctx, name);
	if (!JS_IsException(node) && !JS_IsException(event) &&
	    !JS_IsException(type)) {
		if (!capture_listener) {
			qjsky_run_handler(ctx, node, name, targ, event, evt);
		}
		qjsky_run_listeners(ctx, node, type, event, phase);
	} else {
		qjsky_dump_error(ctx);
	}
	JS_FreeValue(ctx, type);
	JS_FreeValue(ctx, event);
	JS_FreeValue(ctx, node);
	qjsky_leave_thread(thread);

	dom_node_unref(targ);
	dom_string_unref(name);
}

static void qjsky_bubble_listener(dom_event *evt, void *pw)
{
	qjsky_event_dispatch(evt, pw, false);
}

static void qjsky_capture_listener(dom_event *evt, void *pw)
{
	qjsky_event_dispatch(evt, pw, true);
}

static void qjsky_add_dom_listener(jsthread *thread,
				   struct dom_element *ele,
				   dom_string *name,
				   bool capture)
{
	dom_event_listener *listen = NULL;

	if (dom_event_listener_create(capture ? qjsky_capture_listener
					      : qjsky_bubble_listener,
				      thread,
				      &listen) != DOM_NO_ERR) {
		return;
	}
	if (dom_event_target_add_event_listener(ele, name, listen, capture) !=
	    DOM_NO_ERR) {
		NSLOG(qjsky,
		      DEBUG,
		      "Unable to register listener for %p.%.*s",
		      ele,
		      (int)dom_string_length(name),
		      dom_string_data(name));
	}
	dom_event_listener_unref(listen);
}

void qjsky_register_event_listener_for(JSContext *ctx,
				       struct dom_element *ele,
				       dom_string *name,
				       bool capture)
{
	jsthread *thread = qjsky_thread(ctx);
	JSValue obj, registered;
	JSAtom atom;
	int has;

	obj = ele == NULL ? JS_DupValue(ctx, thread->global)
			  : qjsky_node(ctx, (struct dom_node *)ele);
	if (JS_IsException(obj)) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return;
	}
	registered = qjsky_hidden_get(
		ctx, obj, QJSKY_HIDDEN_HANDLER_LISTENERS, true);
	JS_FreeValue(ctx, obj);
	if (JS_IsException(registered)) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return;
	}
	atom = JS_NewAtomLen(ctx,
			     dom_string_data(name),
			     dom_string_length(name));
	has = JS_HasProperty(ctx, registered, atom);
	if (has == 0) {
		JS_SetProperty(ctx, registered, atom, JS_TRUE);
	}
	JS_FreeAtom(ctx, atom);
	JS_FreeValue(ctx, registered);
	if (has != 0 || ele == NULL) {
		return;
	}
	qjsky_add_dom_listener(thread, ele, name, false);
	qjsky_add_dom_listener(thread, ele, name, true);
}

static JSValue qjsky_illegal_constructor(JSContext *ctx,
					 JSValueConst this_val,
					 int argc,
					 JSValueConst *argv)
{
	return JS_ThrowTypeError(ctx, "Illegal constructor");
}

static const int qjsky_unexposed[] = {
	QJSKY_IFACE_MESSAGECHANNEL,
	QJSKY_IFACE_MESSAGEPORT,
	QJSKY_IFACE_BROADCASTCHANNEL,
	QJSKY_IFACE_WORKER,
	QJSKY_IFACE_SHAREDWORKER,
	QJSKY_IFACE_WORKERGLOBALSCOPE,
	QJSKY_IFACE_DEDICATEDWORKERGLOBALSCOPE,
	QJSKY_IFACE_SHAREDWORKERGLOBALSCOPE,
	QJSKY_IFACE_WORKERNAVIGATOR,
	QJSKY_IFACE_WORKERLOCATION,
	QJSKY_IFACE_WEBSOCKET,
	QJSKY_IFACE_EVENTSOURCE,
	QJSKY_IFACE_APPLICATIONCACHE,
};

static bool qjsky_iface_exposed(int iface)
{
	for (size_t i = 0;
	     i < sizeof(qjsky_unexposed) / sizeof(qjsky_unexposed[0]);
	     i++) {
		if (qjsky_unexposed[i] == iface) {
			return false;
		}
	}
	return true;
}

static int qjsky_create_proto(JSContext *ctx,
			      jsthread *thread,
			      JSValue *ctors,
			      JSAtom tag,
			      int iface)
{
	int parent = qjsky_iface_parent[iface];
	JSValue global = thread->global;
	JSAtom name;
	int has;

	if (!JS_IsUndefined(thread->protos[iface])) {
		return 0;
	}
	if (parent >= 0) {
		if (qjsky_create_proto(ctx, thread, ctors, tag, parent) < 0) {
			return -1;
		}
		thread->protos[iface] = JS_NewObjectProto(
			ctx, thread->protos[parent]);
	} else {
		thread->protos[iface] = JS_NewObject(ctx);
	}
	if (JS_IsException(thread->protos[iface])) {
		thread->protos[iface] = JS_UNDEFINED;
		return -1;
	}
	ctors[iface] = JS_NewCFunction2(ctx,
					qjsky_illegal_constructor,
					qjsky_iface_name[iface],
					0,
					JS_CFUNC_constructor_or_func,
					0);
	if (JS_IsException(ctors[iface])) {
		ctors[iface] = JS_UNDEFINED;
		return -1;
	}
	if (JS_SetConstructor(ctx, ctors[iface], thread->protos[iface]) < 0) {
		return -1;
	}
	if (parent >= 0 &&
	    JS_SetPrototype(ctx, ctors[iface], ctors[parent]) < 0) {
		return -1;
	}
	if (JS_DefinePropertyValue(ctx,
				   thread->protos[iface],
				   tag,
				   JS_NewString(ctx, qjsky_iface_name[iface]),
				   JS_PROP_CONFIGURABLE) < 0) {
		return -1;
	}
	if (!qjsky_iface_exposed(iface)) {
		return 0;
	}
	name = JS_NewAtom(ctx, qjsky_iface_name[iface]);
	has = JS_HasProperty(ctx, global, name);
	if (has == 0) {
		has = JS_DefinePropertyValue(ctx,
					     global,
					     name,
					     JS_DupValue(ctx, ctors[iface]),
					     JS_PROP_CONFIGURABLE |
						     JS_PROP_WRITABLE);
	}
	JS_FreeAtom(ctx, name);
	return has < 0 ? -1 : 0;
}

static int qjsky_create_protos(JSContext *ctx, jsthread *thread)
{
	JSValue ctors[QJSKY_IFACE__COUNT];
	JSValue symbol, tagval;
	JSAtom tag;
	int ret = 0;

	symbol = JS_GetPropertyStr(ctx, thread->global, "Symbol");
	tagval = JS_GetPropertyStr(ctx, symbol, "toStringTag");
	tag = JS_ValueToAtom(ctx, tagval);
	JS_FreeValue(ctx, tagval);
	JS_FreeValue(ctx, symbol);
	if (tag == JS_ATOM_NULL) {
		return -1;
	}

	for (int i = 0; i < QJSKY_IFACE__COUNT; i++) {
		ctors[i] = JS_UNDEFINED;
	}
	for (int i = 0; i < QJSKY_IFACE__COUNT && ret == 0; i++) {
		ret = qjsky_create_proto(ctx, thread, ctors, tag, i);
	}
	for (int i = 0; i < QJSKY_IFACE__COUNT && ret == 0; i++) {
		if (qjsky_iface_proto[i] != NULL &&
		    qjsky_iface_proto[i](ctx, thread->protos[i], ctors[i]) <
			    0) {
			NSLOG(qjsky,
			      CRITICAL,
			      "Unable to populate the %s prototype",
			      qjsky_iface_name[i]);
			ret = -1;
		}
	}
	for (int i = 0; i < QJSKY_IFACE__COUNT; i++) {
		JS_FreeValue(ctx, ctors[i]);
	}
	JS_FreeAtom(ctx, tag);
	return ret;
}

void js_initialise(void)
{
	javascript_init();
}

void js_finalise(void)
{
}

nserror js_newheap(int timeout, jsheap **heap)
{
	jsheap *ret = calloc(1, sizeof(*ret));

	*heap = NULL;
	NSLOG(qjsky, DEBUG, "Creating new QuickJS javascript heap");
	if (ret == NULL) {
		return NSERROR_NOMEM;
	}
	ret->rt = JS_NewRuntime();
	if (ret->rt == NULL) {
		free(ret);
		return NSERROR_NOMEM;
	}
	ret->timeout_ms = (uint64_t)(timeout > 0 ? timeout : 10) * 1000;
	JS_SetRuntimeOpaque(ret->rt, ret);
	JS_SetMemoryLimit(ret->rt, QJSKY_MEMORY_LIMIT);
	JS_SetInterruptHandler(ret->rt, qjsky_interrupt, ret);
	JS_NewClassID(ret->rt, &qjsky_class_id);
	if (JS_NewClass(ret->rt, qjsky_class_id, &qjsky_class) < 0) {
		JS_FreeRuntime(ret->rt);
		free(ret);
		return NSERROR_NOMEM;
	}
	*heap = ret;
	return NSERROR_OK;
}

static void qjsky_destroyheap(jsheap *heap)
{
	JSMemoryUsage usage;

	assert(heap->pending_destroy == true);
	assert(heap->live_threads == 0);
	NSLOG(qjsky, DEBUG, "Destroying QuickJS javascript heap");
	JS_RunGC(heap->rt);
	JS_ComputeMemoryUsage(heap->rt, &usage);
	if (usage.obj_count > 0) {
		NSLOG(qjsky,
		      WARNING,
		      "%" PRId64 " JavaScript objects leaked",
		      usage.obj_count);
	}
	JS_FreeRuntime(heap->rt);
	free(heap);
}

void js_destroyheap(jsheap *heap)
{
	heap->pending_destroy = true;
	if (heap->live_threads == 0) {
		qjsky_destroyheap(heap);
	}
}

JSValue qjsky_current_script(JSContext *ctx)
{
	return qjsky_node(ctx, qjsky_thread(ctx)->current_script);
}

void js_set_current_script(jsthread *thread, struct dom_node *node)
{
	if (thread == NULL) {
		return;
	}
	if (thread->current_script != NULL) {
		dom_node_unref(thread->current_script);
	}
	thread->current_script = node != NULL ? dom_node_ref(node) : NULL;
}

static void qjsky_fetch_free(JSContext *ctx, struct qjsky_fetch *f)
{
	JS_FreeValue(ctx, f->callback);
	for (size_t i = 0; i < f->n_headers; i++) {
		free(f->headers[i]);
	}
	free(f->headers);
	free(f->body);
	free(f);
}

static void qjsky_fetch_unlink(jsthread *thread, struct qjsky_fetch *f)
{
	struct qjsky_fetch **pp;

	for (pp = &thread->fetches; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == f) {
			*pp = f->next;
			return;
		}
	}
}

static void qjsky_fetch_abort_all(jsthread *thread)
{
	while (thread->fetches != NULL) {
		struct qjsky_fetch *f = thread->fetches;
		thread->fetches = f->next;
		if (f->fetch != NULL) {
			fetch_abort(f->fetch);
		}
		qjsky_fetch_free(thread->ctx, f);
	}
}

static void qjsky_fetch_deliver(struct qjsky_fetch *f,
				const char *error,
				long status,
				const char *location)
{
	jsthread *thread = f->thread;
	JSContext *ctx = thread->ctx;
	JSValue args[5], ret;

	qjsky_fetch_unlink(thread, f);
	if (thread->pending_destroy) {
		qjsky_fetch_free(ctx, f);
		return;
	}

	qjsky_enter_thread(thread);
	args[0] = error != NULL ? JS_NewString(ctx, error) : JS_NULL;
	args[1] = JS_NewInt32(ctx, (int32_t)status);
	args[2] = location != NULL ? JS_NewString(ctx, location) : JS_NULL;
	args[3] = JS_NewArray(ctx);
	for (size_t i = 0; i < f->n_headers; i++) {
		JS_SetPropertyUint32(ctx,
				     args[3],
				     (uint32_t)i,
				     JS_NewString(ctx, f->headers[i]));
	}
	args[4] = JS_NewArrayBufferCopy(ctx,
					f->body != NULL ? f->body
							: (const uint8_t *)"",
					f->body_len);
	ret = qjsky_call(ctx, f->callback, JS_UNDEFINED, 5, args, true);
	JS_FreeValue(ctx, ret);
	for (int i = 0; i < 5; i++) {
		JS_FreeValue(ctx, args[i]);
	}
	qjsky_fetch_free(ctx, f);
	qjsky_leave_thread(thread);
}

static void
qjsky_fetch_add_header(struct qjsky_fetch *f, const uint8_t *buf, size_t len)
{
	char **headers;
	char *line;

	while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
		len--;
	}
	if (len == 0) {
		return;
	}
	if (len > 5 && memcmp(buf, "HTTP/", 5) == 0) {
		for (size_t i = 0; i < f->n_headers; i++) {
			free(f->headers[i]);
		}
		f->n_headers = 0;
		return;
	}
	headers = realloc(f->headers, (f->n_headers + 1) * sizeof(*headers));
	if (headers == NULL) {
		return;
	}
	f->headers = headers;
	line = malloc(len + 1);
	if (line == NULL) {
		return;
	}
	memcpy(line, buf, len);
	line[len] = '\0';
	f->headers[f->n_headers++] = line;
}

static bool
qjsky_fetch_add_data(struct qjsky_fetch *f, const uint8_t *buf, size_t len)
{
	if (f->body_len + len > QJSKY_MAX_FETCH_BODY) {
		return false;
	}
	if (f->body_len + len > f->body_alloc) {
		size_t alloc = f->body_alloc == 0 ? 16384 : f->body_alloc;
		uint8_t *body;
		while (alloc < f->body_len + len) {
			alloc *= 2;
		}
		body = realloc(f->body, alloc);
		if (body == NULL) {
			return false;
		}
		f->body = body;
		f->body_alloc = alloc;
	}
	memcpy(f->body + f->body_len, buf, len);
	f->body_len += len;
	return true;
}

static void qjsky_fetch_cb(const fetch_msg *msg, void *p)
{
	struct qjsky_fetch *f = p;
	long status;

	switch (msg->type) {
	case FETCH_HEADER:
		qjsky_fetch_add_header(f,
				       msg->data.header_or_data.buf,
				       msg->data.header_or_data.len);
		break;
	case FETCH_DATA:
		if (!qjsky_fetch_add_data(f,
					  msg->data.header_or_data.buf,
					  msg->data.header_or_data.len)) {
			fetch_abort(f->fetch);
			f->fetch = NULL;
			qjsky_fetch_deliver(f, "Response too large", 0, NULL);
		}
		break;
	case FETCH_FINISHED:
		status = fetch_http_code(f->fetch);
		f->fetch = NULL;
		qjsky_fetch_deliver(f, NULL, status != 0 ? status : 200, NULL);
		break;
	case FETCH_REDIRECT:
		status = fetch_http_code(f->fetch);
		f->fetch = NULL;
		qjsky_fetch_deliver(f, NULL, status, msg->data.redirect);
		break;
	case FETCH_NOTMODIFIED:
		f->fetch = NULL;
		qjsky_fetch_deliver(f, NULL, 304, NULL);
		break;
	case FETCH_AUTH:
		f->fetch = NULL;
		qjsky_fetch_deliver(f, NULL, 401, NULL);
		break;
	case FETCH_TIMEDOUT:
	case FETCH_ERROR:
	case FETCH_CERT_ERR:
	case FETCH_SSL_ERR:
		f->fetch = NULL;
		qjsky_fetch_deliver(f, "Failed to fetch", 0, NULL);
		break;
	default:
		break;
	}
}

static nsurl *qjsky_document_url(jsthread *thread)
{
	if (thread->window_priv == NULL || thread->window_priv->htmlc == NULL) {
		return NULL;
	}
	return thread->window_priv->htmlc->base_url;
}

static JSValue qjsky_native_fetch(JSContext *ctx,
				  JSValueConst this_val,
				  int argc,
				  JSValueConst *argv)
{
	jsthread *thread = qjsky_thread(ctx);
	struct qjsky_fetch *f = NULL;
	const char *url_s = NULL, *method = NULL, *body = NULL;
	char **headers = NULL;
	int64_t n_headers = 0;
	nsurl *url = NULL;
	nserror err;
	int credentials;
	bool post;
	JSValue ret = JS_EXCEPTION;

	if (argc < 6 || !JS_IsFunction(ctx, argv[5])) {
		return JS_ThrowTypeError(ctx, "bad native fetch call");
	}
	if (thread->window_priv == NULL || thread->window_priv->closed_down) {
		return JS_ThrowTypeError(ctx, "Failed to fetch");
	}
	url_s = JS_ToCString(ctx, argv[0]);
	method = JS_ToCString(ctx, argv[1]);
	if (url_s == NULL || method == NULL) {
		goto out;
	}
	post = strcmp(method, "POST") == 0;
	if (!post && strcmp(method, "GET") != 0) {
		ret = JS_ThrowTypeError(ctx,
					"Method %s is not supported",
					method);
		goto out;
	}
	if (post && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
		body = JS_ToCString(ctx, argv[3]);
		if (body == NULL) {
			goto out;
		}
	}
	credentials = JS_ToBool(ctx, argv[4]);
	if (credentials < 0) {
		goto out;
	}
	if (nsurl_create(url_s, &url) != NSERROR_OK) {
		ret = JS_ThrowTypeError(ctx, "Invalid URL");
		goto out;
	}
	if (!fetch_can_fetch(url)) {
		ret = JS_ThrowTypeError(ctx, "Failed to fetch");
		goto out;
	}
	if (JS_GetLength(ctx, argv[2], &n_headers) < 0) {
		goto out;
	}
	headers = calloc((size_t)n_headers + 1, sizeof(*headers));
	if (headers == NULL) {
		ret = JS_ThrowOutOfMemory(ctx);
		goto out;
	}
	for (int64_t i = 0; i < n_headers; i++) {
		JSValue v = JS_GetPropertyInt64(ctx, argv[2], i);
		const char *h = JS_ToCString(ctx, v);
		JS_FreeValue(ctx, v);
		if (h == NULL) {
			goto out;
		}
		headers[i] = strdup(h);
		JS_FreeCString(ctx, h);
		if (headers[i] == NULL) {
			ret = JS_ThrowOutOfMemory(ctx);
			goto out;
		}
	}

	f = calloc(1, sizeof(*f));
	if (f == NULL) {
		ret = JS_ThrowOutOfMemory(ctx);
		goto out;
	}
	f->thread = thread;
	f->id = ++thread->next_fetch_id;
	f->callback = JS_DupValue(ctx, argv[5]);
	f->next = thread->fetches;
	thread->fetches = f;

	err = fetch_start(url,
			  qjsky_document_url(thread),
			  qjsky_fetch_cb,
			  f,
			  false,
			  post ? (body != NULL ? body : "") : NULL,
			  NULL,
			  true,
			  false,
			  credentials != 0,
			  (const char **)headers,
			  &f->fetch);
	if (err != NSERROR_OK) {
		qjsky_fetch_unlink(thread, f);
		qjsky_fetch_free(ctx, f);
		ret = JS_ThrowTypeError(ctx, "Failed to fetch");
		goto out;
	}
	ret = JS_NewUint32(ctx, f->id);

out:
	if (headers != NULL) {
		for (int64_t i = 0; i < n_headers; i++) {
			free(headers[i]);
		}
		free(headers);
	}
	if (url != NULL) {
		nsurl_unref(url);
	}
	if (body != NULL) {
		JS_FreeCString(ctx, body);
	}
	if (method != NULL) {
		JS_FreeCString(ctx, method);
	}
	if (url_s != NULL) {
		JS_FreeCString(ctx, url_s);
	}
	return ret;
}

static JSValue qjsky_native_fetch_abort(JSContext *ctx,
					JSValueConst this_val,
					int argc,
					JSValueConst *argv)
{
	jsthread *thread = qjsky_thread(ctx);
	uint32_t id;

	if (JS_ToUint32(ctx, &id, argv[0]) < 0) {
		return JS_EXCEPTION;
	}
	for (struct qjsky_fetch *f = thread->fetches; f != NULL; f = f->next) {
		if (f->id == id) {
			qjsky_fetch_unlink(thread, f);
			if (f->fetch != NULL) {
				fetch_abort(f->fetch);
			}
			qjsky_fetch_free(ctx, f);
			break;
		}
	}
	return JS_UNDEFINED;
}

static JSValue qjsky_native_resolve(JSContext *ctx,
				    JSValueConst this_val,
				    int argc,
				    JSValueConst *argv)
{
	nsurl *base = qjsky_document_url(qjsky_thread(ctx));
	nsurl *joined = NULL;
	const char *rel;
	JSValue ret = JS_NULL;

	nsurl *own_base = NULL;

	if (argc > 1 && JS_IsString(argv[1])) {
		const char *base_s = JS_ToCString(ctx, argv[1]);
		if (base_s == NULL) {
			return JS_EXCEPTION;
		}
		if (nsurl_create(base_s, &own_base) == NSERROR_OK) {
			base = own_base;
		}
		JS_FreeCString(ctx, base_s);
	}
	rel = JS_ToCString(ctx, argv[0]);
	if (rel == NULL) {
		if (own_base != NULL) {
			nsurl_unref(own_base);
		}
		return JS_EXCEPTION;
	}
	if (base != NULL) {
		if (nsurl_join(base, rel, &joined) != NSERROR_OK) {
			joined = NULL;
		}
	} else if (nsurl_create(rel, &joined) != NSERROR_OK) {
		joined = NULL;
	}
	JS_FreeCString(ctx, rel);
	if (own_base != NULL) {
		nsurl_unref(own_base);
	}
	if (joined != NULL) {
		ret = JS_NewString(ctx, nsurl_access(joined));
		nsurl_unref(joined);
	}
	return ret;
}

static JSValue qjsky_native_origin(JSContext *ctx,
				   JSValueConst this_val,
				   int argc,
				   JSValueConst *argv)
{
	const char *url_s = JS_ToCString(ctx, argv[0]);
	nsurl *url = NULL;
	lwc_string *scheme;
	bool web;
	char *origin = NULL;
	size_t origin_len = 0;
	JSValue ret;

	if (url_s == NULL) {
		return JS_EXCEPTION;
	}
	if (nsurl_create(url_s, &url) != NSERROR_OK) {
		JS_FreeCString(ctx, url_s);
		return JS_NewString(ctx, "null");
	}
	JS_FreeCString(ctx, url_s);
	scheme = nsurl_get_component(url, NSURL_SCHEME);
	web = scheme != NULL &&
	      (scheme == corestring_lwc_http || scheme == corestring_lwc_https);
	if (scheme != NULL) {
		lwc_string_unref(scheme);
	}
	if (!web || nsurl_get(url,
			      NSURL_SCHEME | NSURL_HOST | NSURL_PORT,
			      &origin,
			      &origin_len) != NSERROR_OK) {
		nsurl_unref(url);
		return JS_NewString(ctx, "null");
	}
	nsurl_unref(url);
	ret = JS_NewStringLen(ctx, origin, origin_len);
	free(origin);
	return ret;
}

static JSValue qjsky_native_utf8_encode(JSContext *ctx,
					JSValueConst this_val,
					int argc,
					JSValueConst *argv)
{
	size_t len;
	const char *str = JS_ToCStringLen(ctx, &len, argv[0]);
	JSValue ret;

	if (str == NULL) {
		return JS_EXCEPTION;
	}
	ret = JS_NewArrayBufferCopy(ctx, (const uint8_t *)str, len);
	JS_FreeCString(ctx, str);
	return ret;
}

static JSValue qjsky_native_utf8_decode(JSContext *ctx,
					JSValueConst this_val,
					int argc,
					JSValueConst *argv)
{
	size_t size = 0, offset = 0, length = 0, per = 0;
	uint8_t *data;
	JSValue buffer;

	data = JS_GetArrayBuffer(ctx, &size, argv[0]);
	if (data != NULL) {
		return JS_NewStringLen(ctx, (const char *)data, size);
	}
	JS_FreeValue(ctx, JS_GetException(ctx));
	buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &length, &per);
	if (JS_IsException(buffer)) {
		return JS_ThrowTypeError(ctx,
					 "expected an ArrayBuffer or view");
	}
	data = JS_GetArrayBuffer(ctx, &size, buffer);
	JS_FreeValue(ctx, buffer);
	if (data == NULL || offset + length > size) {
		return JS_ThrowTypeError(ctx, "detached buffer");
	}
	return JS_NewStringLen(ctx, (const char *)data + offset, length);
}

static JSValue qjsky_native_object(JSContext *ctx)
{
	static const struct {
		const char *name;
		JSCFunction *fn;
		int length;
	} natives[] = {
		{"fetch", qjsky_native_fetch, 6},
		{"fetchAbort", qjsky_native_fetch_abort, 1},
		{"resolve", qjsky_native_resolve, 2},
		{"utf8Encode", qjsky_native_utf8_encode, 1},
		{"utf8Decode", qjsky_native_utf8_decode, 1},
		{"origin", qjsky_native_origin, 1},
	};
	JSValue obj = JS_NewObject(ctx);

	if (JS_IsException(obj)) {
		return obj;
	}
	for (size_t i = 0; i < sizeof(natives) / sizeof(natives[0]); i++) {
		if (qjsky_define_method(ctx,
					obj,
					natives[i].name,
					natives[i].fn,
					natives[i].length) < 0) {
			JS_FreeValue(ctx, obj);
			return JS_EXCEPTION;
		}
	}
	return obj;
}

static void qjsky_destroythread(jsthread *thread)
{
	jsheap *heap = thread->heap;
	JSContext *ctx = thread->ctx;

	assert(thread->in_use == 0);
	assert(thread->pending_destroy == true);

	NSLOG(qjsky, DEBUG, "Destroying thread %p in heap %p", thread, heap);

	if (ctx != NULL) {
		qjsky_window_closedown(ctx, thread->window_priv);
		qjsky_fetch_abort_all(thread);
		qjsky_map_clear(ctx, &thread->nodes);
		qjsky_map_clear(ctx, &thread->events);
		for (int i = 0; i < QJSKY_IFACE__COUNT; i++) {
			JS_FreeValue(ctx, thread->protos[i]);
		}
		for (int i = 0; i < QJSKY_HIDDEN__COUNT; i++) {
			if (thread->hidden[i] != JS_ATOM_NULL) {
				JS_FreeAtom(ctx, thread->hidden[i]);
			}
		}
		JS_FreeValue(ctx, thread->generics);
		JS_FreeValue(ctx, thread->global);
		JS_FreeContext(ctx);
		JS_RunGC(heap->rt);
	}
	if (thread->current_script != NULL) {
		dom_node_unref(thread->current_script);
	}
	if (thread->window_priv != NULL) {
		qjsky_iface_fini[QJSKY_IFACE_WINDOW](heap->rt,
						     thread->window_priv);
		free(thread->window_priv);
	}
	free(thread);

	heap->live_threads--;
	if (heap->pending_destroy == true && heap->live_threads == 0) {
		qjsky_destroyheap(heap);
	}
}

void js_destroythread(jsthread *thread)
{
	thread->pending_destroy = true;
	if (thread->in_use == 0) {
		qjsky_destroythread(thread);
	}
}

static void qjsky_enter_thread(jsthread *thread)
{
	assert(thread != NULL);
	thread->in_use++;
}

static void qjsky_leave_thread(jsthread *thread)
{
	assert(thread != NULL);
	assert(thread->in_use > 0);

	thread->in_use--;
	if (thread->in_use == 0 && thread->pending_destroy == true) {
		qjsky_destroythread(thread);
	}
}

nserror
js_newthread(jsheap *heap, void *win_priv, void *doc_priv, jsthread **thread)
{
	jsthread *ret;
	JSContext *ctx;
	JSValue prelude, native;
	static const char *const shadowed[] = {"navigator", "console"};

	assert(heap != NULL);
	assert(heap->pending_destroy == false);

	*thread = NULL;
	ret = calloc(1, sizeof(*ret));
	if (ret == NULL) {
		NSLOG(qjsky,
		      ERROR,
		      "Unable to allocate new JS thread structure");
		return NSERROR_NOMEM;
	}
	ret->heap = heap;
	ret->global = JS_UNDEFINED;
	ret->generics = JS_UNDEFINED;
	for (int i = 0; i < QJSKY_IFACE__COUNT; i++) {
		ret->protos[i] = JS_UNDEFINED;
	}
	heap->live_threads++;

	NSLOG(qjsky,
	      DEBUG,
	      "New javascript/QuickJS thread, win_priv=%p, doc_priv=%p",
	      win_priv,
	      doc_priv);

	ctx = ret->ctx = JS_NewContext(heap->rt);
	if (ctx == NULL) {
		goto fail;
	}
	JS_SetContextOpaque(ctx, ret);
	ret->global = JS_GetGlobalObject(ctx);

	for (int i = 0; i < QJSKY_HIDDEN__COUNT; i++) {
		JSValue sym = JS_NewSymbol(ctx, qjsky_hidden_names[i], false);
		if (JS_IsException(sym)) {
			goto fail;
		}
		ret->hidden[i] = JS_ValueToAtom(ctx, sym);
		JS_FreeValue(ctx, sym);
	}

	for (size_t i = 0; i < sizeof(shadowed) / sizeof(shadowed[0]); i++) {
		JSAtom atom = JS_NewAtom(ctx, shadowed[i]);
		JS_DeleteProperty(ctx, ret->global, atom, 0);
		JS_FreeAtom(ctx, atom);
	}

	if (qjsky_create_protos(ctx, ret) < 0) {
		goto fail;
	}
	if (JS_SetPrototype(ctx, ret->global, ret->protos[QJSKY_IFACE_WINDOW]) <
	    0) {
		goto fail;
	}

	ret->window_priv = calloc(1, sizeof(*ret->window_priv));
	if (ret->window_priv == NULL) {
		goto fail;
	}
	((qjsky_object_t *)ret->window_priv)->iface = QJSKY_IFACE_WINDOW;
	qjsky_window___init(ctx, ret->window_priv, win_priv, doc_priv);

	qjsky_reset_timeout(ctx);
	prelude = qjsky_eval(
		ctx, (const char *)prelude_js, prelude_js_len, "prelude.js");
	if (JS_IsException(prelude)) {
		qjsky_dump_error(ctx);
		NSLOG(qjsky,
		      CRITICAL,
		      "Unable to run prelude.js, thread aborted");
		goto fail;
	}
	native = qjsky_native_object(ctx);
	if (JS_IsException(native)) {
		JS_FreeValue(ctx, prelude);
		goto fail;
	}
	ret->generics = qjsky_call(
		ctx, prelude, JS_UNDEFINED, 1, &native, true);
	JS_FreeValue(ctx, native);
	JS_FreeValue(ctx, prelude);
	if (JS_IsException(ret->generics)) {
		ret->generics = JS_UNDEFINED;
		NSLOG(qjsky,
		      CRITICAL,
		      "Unable to run prelude.js, thread aborted");
		goto fail;
	}

	NSLOG(qjsky, DEBUG, "New thread is %p in heap %p", ret, heap);
	*thread = ret;
	return NSERROR_OK;

fail:
	if (ctx != NULL && JS_HasException(ctx)) {
		qjsky_dump_error(ctx);
	}
	js_destroythread(ret);
	return NSERROR_INIT_FAILED;
}

nserror js_closethread(jsthread *thread)
{
	NSLOG(qjsky,
	      DEBUG,
	      "Closing down thread %p in heap %p",
	      thread,
	      thread->heap);
	qjsky_window_closedown(thread->ctx, thread->window_priv);
	qjsky_fetch_abort_all(thread);
	return NSERROR_OK;
}

bool js_exec(jsthread *thread,
	     const uint8_t *txt,
	     size_t txtlen,
	     const char *name)
{
	JSValue res;
	bool ret = false;

	assert(thread);

	if (txt == NULL || txtlen == 0) {
		return false;
	}
	if (thread->pending_destroy) {
		NSLOG(qjsky,
		      DEEPDEBUG,
		      "Skipping exec call because thread is dead");
		return false;
	}

	qjsky_enter_thread(thread);
	NSLOG(qjsky,
	      DEEPDEBUG,
	      "Running %" PRIsizet " bytes from %s",
	      txtlen,
	      name);

	qjsky_reset_timeout(thread->ctx);
	thread->call_depth++;
	res = qjsky_eval(thread->ctx,
			 (const char *)txt,
			 txtlen,
			 name != NULL ? name : "?unknown source?");
	thread->call_depth--;
	if (JS_IsException(res)) {
		NSLOG(qjsky, DEBUG, "Failed to execute JavaScript");
		qjsky_dump_error(thread->ctx);
	} else {
		ret = JS_IsBool(res) && JS_ToBool(thread->ctx, res) > 0;
		JS_FreeValue(thread->ctx, res);
	}
	qjsky_run_jobs(thread);

	qjsky_leave_thread(thread);
	return ret;
}

void js_handle_new_element(jsthread *thread, struct dom_element *node)
{
	dom_namednodemap *map;
	dom_exception exc;
	dom_ulong idx;
	dom_ulong siz;
	dom_attr *attr = NULL;
	dom_string *key = NULL;
	dom_string *nodename;
	bool is_body = false;

	assert(thread);
	assert(node);

	exc = dom_node_get_node_name(node, &nodename);
	if (exc != DOM_NO_ERR) {
		return;
	}
	if (nodename == corestring_dom_BODY) {
		is_body = true;
	}
	dom_string_unref(nodename);

	exc = dom_node_get_attributes(node, &map);
	if (exc != DOM_NO_ERR || map == NULL) {
		return;
	}

	qjsky_enter_thread(thread);

	exc = dom_namednodemap_get_length(map, &siz);
	if (exc != DOM_NO_ERR) {
		goto out;
	}

	for (idx = 0; idx < siz; idx++) {
		exc = dom_namednodemap_item(map, idx, &attr);
		if (exc != DOM_NO_ERR) {
			goto out;
		}
		exc = dom_attr_get_name(attr, &key);
		if (exc != DOM_NO_ERR) {
			goto out;
		}
		if (is_body && (key == corestring_dom_onblur ||
				key == corestring_dom_onerror ||
				key == corestring_dom_onfocus ||
				key == corestring_dom_onload ||
				key == corestring_dom_onresize ||
				key == corestring_dom_onscroll)) {
			goto skip_register;
		}
		if (dom_string_length(key) > 2) {
			const uint8_t *data = (const uint8_t *)dom_string_data(
				key);
			if (data[0] == 'o' && data[1] == 'n') {
				dom_string *sub = NULL;
				exc = dom_string_substr(
					key, 2, dom_string_length(key), &sub);
				if (exc == DOM_NO_ERR) {
					qjsky_register_event_listener_for(
						thread->ctx, node, sub, false);
					dom_string_unref(sub);
				}
			}
		}
	skip_register:
		dom_string_unref(key);
		key = NULL;
		dom_node_unref(attr);
		attr = NULL;
	}

out:
	if (key != NULL) {
		dom_string_unref(key);
	}
	if (attr != NULL) {
		dom_node_unref(attr);
	}
	dom_namednodemap_unref(map);

	qjsky_leave_thread(thread);
}

void js_event_cleanup(jsthread *thread, struct dom_event *evt)
{
	JSValue obj;

	assert(thread);
	if (thread->ctx == NULL) {
		return;
	}
	obj = qjsky_map_take(&thread->events, evt);
	JS_FreeValue(thread->ctx, obj);
}

static void qjsky_fire_window_event(jsthread *thread,
				    dom_event *evt,
				    dom_string *name,
				    struct dom_document *doc)
{
	JSContext *ctx = thread->ctx;
	JSValue event, type;
	dom_event_target *body = NULL;

	event = qjsky_event(ctx, evt);
	type = qjsky_string(ctx, name);
	if (JS_IsException(event) || JS_IsException(type)) {
		qjsky_dump_error(ctx);
		JS_FreeValue(ctx, event);
		JS_FreeValue(ctx, type);
		return;
	}

	if (dom_string_isequal(name, corestring_dom_load)) {
		JSValue handler = qjsky_get_event_handler(
			ctx, thread->global, name, NULL);
		if (JS_IsNull(handler) && doc != NULL &&
		    dom_html_document_get_body(doc, &body) == DOM_NO_ERR &&
		    body != NULL) {
			JSValue bnode = qjsky_node(ctx, (dom_node *)body);
			if (!JS_IsException(bnode)) {
				handler = qjsky_get_event_handler(
					ctx, bnode, name, body);
			} else {
				JS_FreeValue(ctx, JS_GetException(ctx));
			}
			JS_FreeValue(ctx, bnode);
			dom_node_unref(body);
		}
		if (JS_IsFunction(ctx, handler)) {
			JSValue ret = qjsky_call(ctx,
						 handler,
						 thread->global,
						 1,
						 (JSValueConst *)&event,
						 true);
			JS_FreeValue(ctx, ret);
		}
		JS_FreeValue(ctx, handler);
	}
	qjsky_run_listeners(ctx, thread->global, type, event, DOM_AT_TARGET);

	JS_FreeValue(ctx, type);
	JS_FreeValue(ctx, event);
	js_event_cleanup(thread, evt);
}

static void
qjsky_fire_content_loaded(jsthread *thread, struct dom_document *doc)
{
	dom_string *name = NULL;
	dom_event *evt = NULL;
	bool success;

	if (dom_string_create((const uint8_t *)"DOMContentLoaded",
			      SLEN("DOMContentLoaded"),
			      &name) != DOM_NO_ERR) {
		return;
	}
	if (dom_event_create(&evt) != DOM_NO_ERR) {
		dom_string_unref(name);
		return;
	}
	if (dom_event_init(evt, name, true, false) == DOM_NO_ERR) {
		if (doc != NULL) {
			(void)dom_event_target_dispatch_event(doc,
							      evt,
							      &success);
		}
		if (!thread->pending_destroy) {
			qjsky_fire_window_event(thread, evt, name, NULL);
		}
	}
	js_event_cleanup(thread, evt);
	dom_event_unref(evt);
	dom_string_unref(name);
}

bool js_fire_event(jsthread *thread,
		   const char *type,
		   struct dom_document *doc,
		   struct dom_node *target)
{
	dom_event *evt;

	NSLOG(qjsky, DEBUG, "Event: %s (doc=%p, target=%p)", type, doc, target);

	if (target != NULL || strcmp(type, "load") != 0 ||
	    thread->pending_destroy) {
		return true;
	}

	qjsky_enter_thread(thread);

	qjsky_fire_content_loaded(thread, doc);

	if (!thread->pending_destroy && dom_event_create(&evt) == DOM_NO_ERR) {
		if (dom_event_init(evt, corestring_dom_load, false, false) ==
		    DOM_NO_ERR) {
			qjsky_fire_window_event(
				thread, evt, corestring_dom_load, doc);
		}
		dom_event_unref(evt);
	}

	qjsky_leave_thread(thread);
	return true;
}
