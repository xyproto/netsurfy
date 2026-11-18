/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <stdlib.h>
#include <dom/dom.h>

#include "utils/corestrings.h"
#include "html/private.h"
#include "html/shadow.h"
#include "html/css.h"

struct html_shadow_scope {
	struct html_shadow_scope *next;
	dom_node *scope;
	dom_node *host_scope;
	css_select_ctx *ctx;
};

static dom_node_type node_type(dom_node *node)
{
	dom_node_type type = DOM_NODE_TYPE_COUNT;

	(void)dom_node_get_node_type(node, &type);
	return type;
}

static dom_node *shadow_root(dom_node *node)
{
	dom_document_fragment *root = NULL;

	if (node_type(node) == DOM_ELEMENT_NODE) {
		(void)dom_element_get_shadow_root((dom_element *)node, &root);
	}
	return (dom_node *)root;
}

static bool is_slot(dom_node *node)
{
	dom_string *name = NULL;
	bool slot = false;

	if (node_type(node) != DOM_ELEMENT_NODE ||
	    dom_node_get_node_name(node, &name) != DOM_NO_ERR || name == NULL) {
		return false;
	}
	slot = dom_string_caseless_isequal(name, corestring_dom_slot);
	dom_string_unref(name);
	return slot;
}

static bool name_matches(dom_node *node, dom_string *attr, dom_string *name)
{
	dom_string *value = NULL;
	bool match;

	(void)dom_element_get_attribute(node, attr, &value);
	if (value == NULL || dom_string_byte_length(value) == 0) {
		match = name == NULL;
	} else {
		match = name != NULL && dom_string_isequal(value, name);
	}
	if (value != NULL) {
		dom_string_unref(value);
	}
	return match;
}

static dom_node *next_in_tree(dom_node *node, dom_node *root)
{
	dom_node *next = NULL;

	(void)dom_node_get_first_child(node, &next);
	if (next != NULL) {
		return next;
	}
	node = dom_node_ref(node);
	while (node != root) {
		dom_node *parent = NULL;

		(void)dom_node_get_next_sibling(node, &next);
		if (next != NULL) {
			dom_node_unref(node);
			return next;
		}
		(void)dom_node_get_parent_node(node, &parent);
		dom_node_unref(node);
		if (parent == NULL) {
			return NULL;
		}
		node = parent;
	}
	dom_node_unref(node);
	return NULL;
}

static dom_node *find_slot(dom_node *host, dom_string *name)
{
	dom_node *root = shadow_root(host);
	dom_node *n;

	if (root == NULL) {
		return NULL;
	}
	for (n = next_in_tree(root, root); n != NULL;) {
		dom_node *next;

		if (is_slot(n) && name_matches(n, corestring_dom_name, name)) {
			break;
		}
		next = next_in_tree(n, root);
		dom_node_unref(n);
		n = next;
	}
	dom_node_unref(root);
	return n;
}

static dom_node *assigned_slot(dom_node *host, dom_node *node)
{
	dom_string *name = NULL;
	dom_node *slot;

	switch (node_type(node)) {
	case DOM_ELEMENT_NODE:
		(void)dom_element_get_attribute(node, corestring_dom_slot, &name);
		if (name != NULL && dom_string_byte_length(name) == 0) {
			dom_string_unref(name);
			name = NULL;
		}
		break;
	case DOM_TEXT_NODE:
		break;
	default:
		return NULL;
	}
	slot = find_slot(host, name);
	if (name != NULL) {
		dom_string_unref(name);
	}
	return slot;
}

static bool is_shadow_host(dom_node *node)
{
	dom_node *root = shadow_root(node);

	if (root == NULL) {
		return false;
	}
	dom_node_unref(root);
	return true;
}

static dom_node *next_assigned(dom_node *host, dom_node *node, dom_node *slot)
{
	dom_node *next = NULL;

	(void)dom_node_get_next_sibling(node, &next);
	while (next != NULL) {
		dom_node *s = assigned_slot(host, next);
		dom_node *after = NULL;

		if (s != NULL) {
			dom_node_unref(s);
			if (s == slot) {
				return next;
			}
		}
		(void)dom_node_get_next_sibling(next, &after);
		dom_node_unref(next);
		next = after;
	}
	return NULL;
}

/* exported interface documented in html/shadow.h */
dom_node *html_shadow_root_of(dom_node *node)
{
	dom_node *n = dom_node_ref(node);
	dom_element *host = NULL;

	for (;;) {
		dom_node *parent = NULL;

		(void)dom_node_get_parent_node(n, &parent);
		if (parent == NULL) {
			break;
		}
		dom_node_unref(n);
		n = parent;
	}
	if (node_type(n) == DOM_DOCUMENT_FRAGMENT_NODE) {
		(void)dom_document_fragment_get_host((dom_document_fragment *)n,
						     &host);
	}
	if (host == NULL) {
		dom_node_unref(n);
		return NULL;
	}
	dom_node_unref(host);
	return n;
}

/* exported interface documented in html/shadow.h */
dom_node *html_shadow_host_of(dom_node *node)
{
	dom_node *root = html_shadow_root_of(node);
	dom_element *host = NULL;

	if (root != NULL) {
		(void)dom_document_fragment_get_host(
			(dom_document_fragment *)root, &host);
		dom_node_unref(root);
	}
	return (dom_node *)host;
}

/* exported interface documented in html/shadow.h */
dom_node *html_flat_parent(dom_node *node)
{
	dom_node *parent = NULL;
	dom_element *host = NULL;

	(void)dom_node_get_parent_node(node, &parent);
	if (parent == NULL) {
		return NULL;
	}
	if (is_shadow_host(parent)) {
		dom_node *slot = assigned_slot(parent, node);

		if (slot != NULL) {
			dom_node_unref(parent);
			return slot;
		}
		return parent;
	}
	if (node_type(parent) == DOM_DOCUMENT_FRAGMENT_NODE) {
		(void)dom_document_fragment_get_host(
			(dom_document_fragment *)parent, &host);
		if (host != NULL) {
			dom_node_unref(parent);
			return (dom_node *)host;
		}
	}
	return parent;
}

/* exported interface documented in html/shadow.h */
dom_node *html_flat_first_child(dom_node *node)
{
	dom_node *root = shadow_root(node);
	dom_node *child = NULL;

	if (root != NULL) {
		(void)dom_node_get_first_child(root, &child);
		dom_node_unref(root);
		return child;
	}
	if (is_slot(node)) {
		dom_node *host = html_shadow_host_of(node);

		if (host != NULL) {
			(void)dom_node_get_first_child(host, &child);
			while (child != NULL) {
				dom_node *s = assigned_slot(host, child);
				dom_node *next = NULL;

				if (s != NULL) {
					dom_node_unref(s);
					if (s == node) {
						break;
					}
				}
				(void)dom_node_get_next_sibling(child, &next);
				dom_node_unref(child);
				child = next;
			}
			dom_node_unref(host);
			if (child != NULL) {
				return child;
			}
		}
	}
	(void)dom_node_get_first_child(node, &child);
	return child;
}

/* exported interface documented in html/shadow.h */
dom_node *html_flat_next_sibling(dom_node *node)
{
	dom_node *parent = NULL;
	dom_node *next = NULL;

	(void)dom_node_get_parent_node(node, &parent);
	if (parent != NULL && is_shadow_host(parent)) {
		dom_node *slot = assigned_slot(parent, node);

		if (slot != NULL) {
			next = next_assigned(parent, node, slot);
			dom_node_unref(slot);
		}
		dom_node_unref(parent);
		return next;
	}
	if (parent != NULL) {
		dom_node_unref(parent);
	}
	(void)dom_node_get_next_sibling(node, &next);
	return next;
}

/* exported interface documented in html/shadow.h */
css_select_ctx *html_shadow_select_ctx(html_content *c,
				       dom_node *node,
				       dom_node **scope_host)
{
	dom_node *scope = NULL;
	dom_node *host_scope = shadow_root(node);
	struct html_shadow_scope *s;
	css_select_ctx *ctx = c->select_ctx;

	*scope_host = NULL;
	if (host_scope != NULL) {
		c->shadow_seen = true;
	}
	if (c->shadow_seen) {
		scope = html_shadow_root_of(node);
	}
	if (scope != NULL) {
		dom_node *host = html_shadow_host_of(node);

		*scope_host = host;
		dom_node_unref(host);
	}
	if (scope == NULL && host_scope == NULL) {
		return ctx;
	}

	for (s = c->shadow_scopes; s != NULL; s = s->next) {
		if (s->scope == scope && s->host_scope == host_scope) {
			ctx = s->ctx;
			break;
		}
	}
	if (s == NULL) {
		s = calloc(1, sizeof(*s));
		if (s != NULL && html_css_new_scope_context(c,
							    scope,
							    host_scope,
							    &s->ctx) ==
					 NSERROR_OK) {
			s->scope = scope != NULL ? dom_node_ref(scope) : NULL;
			s->host_scope = host_scope != NULL
						? dom_node_ref(host_scope)
						: NULL;
			s->next = c->shadow_scopes;
			c->shadow_scopes = s;
			ctx = s->ctx;
		} else {
			free(s);
		}
	}
	if (scope != NULL) {
		dom_node_unref(scope);
	}
	if (host_scope != NULL) {
		dom_node_unref(host_scope);
	}
	return ctx;
}

/* exported interface documented in html/shadow.h */
void html_shadow_flush(html_content *c)
{
	while (c->shadow_scopes != NULL) {
		struct html_shadow_scope *s = c->shadow_scopes;

		c->shadow_scopes = s->next;
		css_select_ctx_destroy(s->ctx);
		if (s->scope != NULL) {
			dom_node_unref(s->scope);
		}
		if (s->host_scope != NULL) {
			dom_node_unref(s->host_scope);
		}
		free(s);
	}
}
