/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#ifndef NETSURFY_JAVASCRIPT_QUICKJS_MUTATION_H
#define NETSURFY_JAVASCRIPT_QUICKJS_MUTATION_H

#include <quickjs.h>

struct dom_node;
struct qjsky_observer;

struct qjsky_observer *
qjsky_observer_create(JSContext *ctx, JSValueConst callback);
JSValue qjsky_observer_observe(JSContext *ctx,
			       struct qjsky_observer *mo,
			       JSValueConst self,
			       struct dom_node *target,
			       JSValueConst options);
void qjsky_observer_disconnect(struct qjsky_observer *mo);
JSValue qjsky_observer_take_records(JSContext *ctx, struct qjsky_observer *mo);
void qjsky_observer_free(JSRuntime *rt, struct qjsky_observer *mo);
void qjsky_observer_release_all(JSContext *ctx);

#endif
