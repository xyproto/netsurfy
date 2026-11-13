/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <dom/dom.h>

#include "utils/corestrings.h"
#include "javascript/quickjs/geometry.h"
#include "javascript/quickjs/mutation.h"
#include "quickjs/binding.h"
#include "quickjs/private.h"
#include "quickjs/prototype.h"
#include "qjsky.h"

struct qjsky_registration {
	struct qjsky_registration *next;
	struct qjsky_observer *mo;
	dom_node *target;
	dom_event_listener *listener;
	bool child_list;
	bool attributes;
	bool character_data;
	bool subtree;
	bool attribute_old_value;
	bool character_data_old_value;
	dom_string **filter;
	size_t nfilter;
};

struct qjsky_observer {
	struct qjsky_observer *next;
	JSContext *ctx;
	JSValue self;
	JSValue callback;
	JSValue records;
	uint32_t nrecords;
	struct qjsky_registration *regs;
	dom_event *last_event;
	bool queued;
};

static void registration_free(struct qjsky_registration *reg)
{
	dom_string *const types[] = {
		corestring_dom_DOMNodeInserted,
		corestring_dom_DOMNodeRemoved,
		corestring_dom_DOMAttrModified,
		corestring_dom_DOMCharacterDataModified,
	};

	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		(void)dom_event_target_remove_event_listener(
			reg->target, types[i], reg->listener, true);
	}
	dom_event_listener_unref(reg->listener);
	dom_node_unref(reg->target);
	for (size_t i = 0; i < reg->nfilter; i++) {
		dom_string_unref(reg->filter[i]);
	}
	free(reg->filter);
	free(reg);
}

static bool node_is_within(dom_node *node, dom_node *ancestor)
{
	dom_node *n = node != NULL ? dom_node_ref(node) : NULL;

	while (n != NULL) {
		dom_node *parent = NULL;
		if (n == ancestor) {
			dom_node_unref(n);
			return true;
		}
		if (dom_node_get_parent_node(n, &parent) != DOM_NO_ERR) {
			parent = NULL;
		}
		dom_node_unref(n);
		n = parent;
	}
	return false;
}

static JSValue deliver_job(JSContext *ctx, int argc, JSValueConst *argv);

static bool node_is_attribute(dom_node *node)
{
	dom_node_type type;

	return dom_node_get_node_type(node, &type) == DOM_NO_ERR && type == DOM_ATTRIBUTE_NODE;
}

static void queue_record(struct qjsky_observer *mo, JSValue record)
{
	JSContext *ctx = mo->ctx;

	if (JS_IsException(record)) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return;
	}
	if (JS_SetPropertyUint32(ctx, mo->records, mo->nrecords, record) < 0) {
		JS_FreeValue(ctx, JS_GetException(ctx));
		return;
	}
	mo->nrecords++;
	if (!mo->queued && !JS_IsUndefined(mo->self)) {
		mo->queued = true;
		if (JS_EnqueueJob(
			    ctx, deliver_job, 1, (JSValueConst *)&mo->self) <
		    0) {
			mo->queued = false;
		}
	}
}

static void mutation_listener(dom_event *evt, void *pw)
{
	struct qjsky_registration *reg = pw;
	struct qjsky_observer *mo = reg->mo;
	dom_string *type = NULL;
	dom_event_target *et = NULL;
	dom_node *node, *record_target = NULL, *added = NULL, *removed = NULL;
	dom_node *prev = NULL, *next = NULL;
	dom_string *record_type = NULL, *attr_name = NULL, *old_value = NULL;
	bool wanted = false;

	if (mo->last_event == evt) {
		return;
	}
	if (dom_event_get_type(evt, &type) != DOM_NO_ERR || type == NULL) {
		return;
	}
	if (dom_event_get_target(evt, &et) != DOM_NO_ERR || et == NULL) {
		dom_string_unref(type);
		return;
	}
	node = (dom_node *)et;

	if ((dom_string_isequal(type, corestring_dom_DOMNodeInserted) ||
	     dom_string_isequal(type, corestring_dom_DOMNodeRemoved)) &&
	    !node_is_attribute(node)) {
		dom_node *parent = NULL;
		bool inserted = dom_string_isequal(
			type, corestring_dom_DOMNodeInserted);
		(void)dom_mutation_event_get_related_node(evt, &parent);
		if (parent == NULL) {
			(void)dom_node_get_parent_node(node, &parent);
		}
		if (reg->child_list && parent != NULL &&
		    (parent == reg->target ||
		     (reg->subtree && node_is_within(parent, reg->target)))) {
			wanted = true;
			record_target = dom_node_ref(parent);
			if (inserted) {
				added = dom_node_ref(node);
			} else {
				removed = dom_node_ref(node);
			}
			(void)dom_node_get_previous_sibling(node, &prev);
			(void)dom_node_get_next_sibling(node, &next);
			dom_string_create((const uint8_t *)"childList",
					  9,
					  &record_type);
		}
		if (parent != NULL) {
			dom_node_unref(parent);
		}
	} else if (dom_string_isequal(type, corestring_dom_DOMAttrModified)) {
		if (reg->attributes &&
		    (node == reg->target ||
		     (reg->subtree && node_is_within(node, reg->target)))) {
			(void)dom_mutation_event_get_attr_name(evt, &attr_name);
			wanted = attr_name != NULL;
			if (wanted && reg->nfilter > 0) {
				wanted = false;
				for (size_t i = 0; i < reg->nfilter; i++) {
					if (dom_string_isequal(reg->filter[i],
							       attr_name)) {
						wanted = true;
					}
				}
			}
			if (wanted) {
				dom_mutation_type change =
					DOM_MUTATION_MODIFICATION;
				record_target = dom_node_ref(node);
				(void)dom_mutation_event_get_attr_change(
					evt, &change);
				if (reg->attribute_old_value &&
				    change != DOM_MUTATION_ADDITION) {
					(void)dom_mutation_event_get_prev_value(
						evt, &old_value);
				}
				dom_string_create((const uint8_t *)"attributes",
						  10,
						  &record_type);
			}
		}
	} else if (dom_string_isequal(
			   type, corestring_dom_DOMCharacterDataModified)) {
		if (reg->character_data &&
		    (node == reg->target ||
		     (reg->subtree && node_is_within(node, reg->target)))) {
			wanted = true;
			record_target = dom_node_ref(node);
			if (reg->character_data_old_value) {
				(void)dom_mutation_event_get_prev_value(
					evt, &old_value);
			}
			dom_string_create((const uint8_t *)"characterData",
					  13,
					  &record_type);
		}
	}

	if (wanted && record_type != NULL) {
		if (mo->last_event != NULL) {
			dom_event_unref(mo->last_event);
		}
		mo->last_event = evt;
		dom_event_ref(evt);
		queue_record(mo,
			     qjsky_mutation_record___create(mo->ctx,
							    record_type,
							    record_target,
							    added,
							    removed,
							    prev,
							    next,
							    attr_name,
							    old_value));
	}

	if (record_type != NULL)
		dom_string_unref(record_type);
	if (record_target != NULL)
		dom_node_unref(record_target);
	if (added != NULL)
		dom_node_unref(added);
	if (removed != NULL)
		dom_node_unref(removed);
	if (prev != NULL)
		dom_node_unref(prev);
	if (next != NULL)
		dom_node_unref(next);
	if (attr_name != NULL)
		dom_string_unref(attr_name);
	if (old_value != NULL)
		dom_string_unref(old_value);
	dom_node_unref(node);
	dom_string_unref(type);
}

static JSValue deliver_job(JSContext *ctx, int argc, JSValueConst *argv)
{
	mutation_observer_private_t *priv;
	struct qjsky_observer *mo;
	JSValue records, args[2], ret;

	priv = qjsky_private(ctx, argv[0], QJSKY_IFACE_MUTATIONOBSERVER);
	if (priv == NULL || (mo = priv->mo) == NULL) {
		return JS_UNDEFINED;
	}
	mo->queued = false;
	if (mo->last_event != NULL) {
		dom_event_unref(mo->last_event);
		mo->last_event = NULL;
	}
	if (mo->nrecords == 0) {
		return JS_UNDEFINED;
	}
	records = qjsky_observer_take_records(ctx, mo);
	if (JS_IsException(records)) {
		return records;
	}
	args[0] = records;
	args[1] = argv[0];
	ret = qjsky_call(ctx, mo->callback, argv[0], 2, args, true);
	JS_FreeValue(ctx, records);
	if (JS_IsException(ret)) {
		qjsky_dump_error(ctx);
	}
	JS_FreeValue(ctx, ret);
	return JS_UNDEFINED;
}

struct qjsky_observer *
qjsky_observer_create(JSContext *ctx, JSValueConst callback)
{
	struct qjsky_observer *mo = calloc(1, sizeof(*mo));

	if (mo == NULL) {
		return NULL;
	}
	mo->ctx = ctx;
	mo->self = JS_UNDEFINED;
	mo->callback = JS_DupValue(ctx, callback);
	mo->records = JS_NewArray(ctx);
	return mo;
}

static int option_bool(JSContext *ctx,
		       JSValueConst options,
		       const char *name,
		       bool *value,
		       bool *present)
{
	JSValue v;
	int b;

	*present = false;
	if (JS_IsUndefined(options) || JS_IsNull(options)) {
		return 0;
	}
	v = JS_GetPropertyStr(ctx, options, name);
	if (JS_IsException(v)) {
		return -1;
	}
	if (!JS_IsUndefined(v)) {
		b = JS_ToBool(ctx, v);
		JS_FreeValue(ctx, v);
		if (b < 0) {
			return -1;
		}
		*value = b != 0;
		*present = true;
	}
	return 0;
}

JSValue qjsky_observer_observe(JSContext *ctx,
			       struct qjsky_observer *mo,
			       JSValueConst self,
			       dom_node *target,
			       JSValueConst options)
{
	dom_string *const types[] = {
		corestring_dom_DOMNodeInserted,
		corestring_dom_DOMNodeRemoved,
		corestring_dom_DOMAttrModified,
		corestring_dom_DOMCharacterDataModified,
	};
	struct qjsky_registration *reg, **link;
	bool child_list = false, attributes = false, character_data = false,
	     subtree = false;
	bool attr_old = false, cdata_old = false;
	bool has_attributes, has_cdata, present, has_filter = false;
	dom_string **filter = NULL;
	size_t nfilter = 0;
	JSValue fv;

	if (!JS_IsUndefined(options) && !JS_IsNull(options) &&
	    !JS_IsObject(options)) {
		return JS_ThrowTypeError(ctx, "The options must be an object");
	}
	if (option_bool(ctx, options, "childList", &child_list, &present) < 0 ||
	    option_bool(
		    ctx, options, "attributes", &attributes, &has_attributes) <
		    0 ||
	    option_bool(ctx,
			options,
			"characterData",
			&character_data,
			&has_cdata) < 0 ||
	    option_bool(ctx, options, "subtree", &subtree, &present) < 0 ||
	    option_bool(
		    ctx, options, "attributeOldValue", &attr_old, &present) <
		    0 ||
	    option_bool(ctx,
			options,
			"characterDataOldValue",
			&cdata_old,
			&present) < 0) {
		return JS_EXCEPTION;
	}
	if (JS_IsObject(options)) {
		fv = JS_GetPropertyStr(ctx, options, "attributeFilter");
		if (JS_IsException(fv)) {
			return fv;
		}
		if (!JS_IsUndefined(fv)) {
			int64_t len = 0;
			has_filter = true;
			if (JS_GetLength(ctx, fv, &len) < 0) {
				JS_FreeValue(ctx, fv);
				return JS_EXCEPTION;
			}
			filter = calloc(len > 0 ? (size_t)len : 1,
					sizeof(*filter));
			for (int64_t i = 0; filter != NULL && i < len; i++) {
				JSValue e = JS_GetPropertyInt64(ctx, fv, i);
				dom_string *ds = JS_IsException(e)
							 ? NULL
							 : qjsky_to_dom_string(
								   ctx, e);
				JS_FreeValue(ctx, e);
				if (ds == NULL) {
					for (size_t k = 0; k < nfilter; k++)
						dom_string_unref(filter[k]);
					free(filter);
					JS_FreeValue(ctx, fv);
					return JS_EXCEPTION;
				}
				filter[nfilter++] = ds;
			}
		}
		JS_FreeValue(ctx, fv);
	}
	if (!has_attributes && (attr_old || has_filter)) {
		attributes = true;
	}
	if (!has_cdata && cdata_old) {
		character_data = true;
	}
	if (!child_list && !attributes && !character_data) {
		for (size_t k = 0; k < nfilter; k++)
			dom_string_unref(filter[k]);
		free(filter);
		return JS_ThrowTypeError(
			ctx,
			"The options object must set at least one of 'attributes', 'characterData', or 'childList' to true.");
	}

	for (link = &mo->regs; *link != NULL; link = &(*link)->next) {
		if ((*link)->target == target) {
			reg = *link;
			*link = reg->next;
			registration_free(reg);
			break;
		}
	}
	reg = calloc(1, sizeof(*reg));
	if (reg == NULL ||
	    dom_event_listener_create(mutation_listener, reg, &reg->listener) !=
		    DOM_NO_ERR) {
		free(reg);
		for (size_t k = 0; k < nfilter; k++)
			dom_string_unref(filter[k]);
		free(filter);
		return JS_ThrowOutOfMemory(ctx);
	}
	reg->mo = mo;
	reg->target = dom_node_ref(target);
	reg->child_list = child_list;
	reg->attributes = attributes;
	reg->character_data = character_data;
	reg->subtree = subtree;
	reg->attribute_old_value = attr_old;
	reg->character_data_old_value = cdata_old;
	reg->filter = filter;
	reg->nfilter = nfilter;
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		(void)dom_event_target_add_event_listener(
			target, types[i], reg->listener, true);
	}
	reg->next = mo->regs;
	mo->regs = reg;

	if (JS_IsUndefined(mo->self)) {
		struct qjsky_observer **list = qjsky_observers(ctx);
		mo->self = JS_DupValue(ctx, self);
		mo->next = *list;
		*list = mo;
	}
	return JS_UNDEFINED;
}

void qjsky_observer_disconnect(struct qjsky_observer *mo)
{
	JSContext *ctx = mo->ctx;

	while (mo->regs != NULL) {
		struct qjsky_registration *reg = mo->regs;
		mo->regs = reg->next;
		registration_free(reg);
	}
	if (!JS_IsUndefined(mo->self)) {
		struct qjsky_observer **list = qjsky_observers(ctx);
		while (*list != NULL && *list != mo) {
			list = &(*list)->next;
		}
		if (*list == mo) {
			*list = mo->next;
		}
		mo->next = NULL;
		JS_FreeValue(ctx, mo->self);
		mo->self = JS_UNDEFINED;
	}
	JS_FreeValue(ctx, mo->records);
	mo->records = JS_NewArray(ctx);
	mo->nrecords = 0;
}

JSValue qjsky_observer_take_records(JSContext *ctx, struct qjsky_observer *mo)
{
	JSValue records = mo->records;

	mo->records = JS_NewArray(ctx);
	mo->nrecords = 0;
	return records;
}

void qjsky_observer_free(JSRuntime *rt, struct qjsky_observer *mo)
{
	if (mo == NULL) {
		return;
	}
	while (mo->regs != NULL) {
		struct qjsky_registration *reg = mo->regs;
		mo->regs = reg->next;
		registration_free(reg);
	}
	if (mo->last_event != NULL) {
		dom_event_unref(mo->last_event);
	}
	JS_FreeValueRT(rt, mo->callback);
	JS_FreeValueRT(rt, mo->records);
	free(mo);
}

void qjsky_observer_release_all(JSContext *ctx)
{
	struct qjsky_observer **list = qjsky_observers(ctx);

	while (*list != NULL) {
		qjsky_observer_disconnect(*list);
	}
}
