/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdlib.h>

#include <dom/core/node.h>
#include <dom/core/string.h>

#include "core/node.h"
#include "core/range.h"

struct dom_range {
	struct dom_range *prev;
	struct dom_range *next;
	dom_node_internal *start;
	dom_node_internal *end;
	uint32_t start_offset;
	uint32_t end_offset;
};

static struct dom_range *live_ranges;

static uint32_t node_index(const dom_node_internal *node)
{
	uint32_t index = 0;

	for (node = node->previous; node != NULL; node = node->previous) {
		index++;
	}
	return index;
}

static bool is_inclusive_ancestor(const dom_node_internal *ancestor,
				  const dom_node_internal *node)
{
	for (; node != NULL; node = node->parent) {
		if (node == ancestor) {
			return true;
		}
	}
	return false;
}

static void set_point(dom_node_internal **slot,
		      uint32_t *slot_offset,
		      dom_node_internal *node,
		      uint32_t offset)
{
	dom_node_internal *old = *slot;

	*slot_offset = offset;
	if (old == node) {
		return;
	}
	dom_node_ref(node);
	*slot = node;
	dom_node_unref(old);
}

uint32_t dom_node_length(struct dom_node *node)
{
	dom_node_internal *n = (dom_node_internal *)node;
	dom_node_internal *c;
	uint32_t length = 0;

	switch (n->type) {
	case DOM_DOCUMENT_TYPE_NODE:
		return 0;
	case DOM_TEXT_NODE:
	case DOM_CDATA_SECTION_NODE:
	case DOM_COMMENT_NODE:
	case DOM_PROCESSING_INSTRUCTION_NODE:
		return n->value != NULL ? dom_string_length(n->value) : 0;
	default:
		break;
	}
	for (c = n->first_child; c != NULL; c = c->next) {
		length++;
	}
	return length;
}

struct dom_node *dom_node_root(struct dom_node *node)
{
	dom_node_internal *n = (dom_node_internal *)node;

	while (n->parent != NULL) {
		n = n->parent;
	}
	return (struct dom_node *)n;
}

static uint32_t node_depth(const dom_node_internal *node)
{
	uint32_t depth = 0;

	for (node = node->parent; node != NULL; node = node->parent) {
		depth++;
	}
	return depth;
}

static bool node_precedes(dom_node_internal *a, dom_node_internal *b)
{
	uint32_t da = node_depth(a);
	uint32_t db = node_depth(b);
	bool a_shallower = da < db;
	dom_node_internal *n;

	if (a == b) {
		return false;
	}
	while (da > db) {
		a = a->parent;
		da--;
	}
	while (db > da) {
		b = b->parent;
		db--;
	}
	if (a == b) {
		return a_shallower;
	}
	while (a->parent != b->parent) {
		a = a->parent;
		b = b->parent;
	}
	for (n = a->next; n != NULL; n = n->next) {
		if (n == b) {
			return true;
		}
	}
	return false;
}

int dom_boundary_point_compare(struct dom_node *a_node,
			       uint32_t a_offset,
			       struct dom_node *b_node,
			       uint32_t b_offset)
{
	dom_node_internal *a = (dom_node_internal *)a_node;
	dom_node_internal *b = (dom_node_internal *)b_node;
	dom_node_internal *child;

	if (a == b) {
		return a_offset < b_offset ? -1 : a_offset > b_offset ? 1 : 0;
	}
	if (node_precedes(b, a)) {
		return -dom_boundary_point_compare(
			b_node, b_offset, a_node, a_offset);
	}
	if (is_inclusive_ancestor(a, b)) {
		child = b;
		while (child->parent != a) {
			child = child->parent;
		}
		if (node_index(child) < a_offset) {
			return 1;
		}
	}
	return -1;
}

dom_exception dom_range_create(struct dom_node *container, dom_range **result)
{
	struct dom_range *range = calloc(1, sizeof(*range));

	if (range == NULL) {
		return DOM_NO_MEM_ERR;
	}
	range->start = (dom_node_internal *)dom_node_ref(container);
	range->end = (dom_node_internal *)dom_node_ref(container);
	range->next = live_ranges;
	if (live_ranges != NULL) {
		live_ranges->prev = range;
	}
	live_ranges = range;
	*result = range;
	return DOM_NO_ERR;
}

void dom_range_destroy(dom_range *range)
{
	if (range == NULL) {
		return;
	}
	if (range->prev != NULL) {
		range->prev->next = range->next;
	} else {
		live_ranges = range->next;
	}
	if (range->next != NULL) {
		range->next->prev = range->prev;
	}
	dom_node_unref(range->start);
	dom_node_unref(range->end);
	free(range);
}

void dom_range_get_start(dom_range *range,
			 struct dom_node **container,
			 uint32_t *offset)
{
	*container = (struct dom_node *)dom_node_ref(range->start);
	*offset = range->start_offset;
}

void dom_range_get_end(dom_range *range,
		       struct dom_node **container,
		       uint32_t *offset)
{
	*container = (struct dom_node *)dom_node_ref(range->end);
	*offset = range->end_offset;
}

static dom_exception check_point(dom_node_internal *node, uint32_t offset)
{
	if (node->type == DOM_DOCUMENT_TYPE_NODE) {
		return DOM_INVALID_NODE_TYPE_ERR;
	}
	if (offset > dom_node_length((struct dom_node *)node)) {
		return DOM_INDEX_SIZE_ERR;
	}
	return DOM_NO_ERR;
}

dom_exception
dom_range_set_start(dom_range *range, struct dom_node *node, uint32_t offset)
{
	dom_node_internal *n = (dom_node_internal *)node;
	dom_exception err = check_point(n, offset);

	if (err != DOM_NO_ERR) {
		return err;
	}
	if (dom_node_root((struct dom_node *)range->start) !=
		    dom_node_root(node) ||
	    dom_boundary_point_compare(node,
				       offset,
				       (struct dom_node *)range->end,
				       range->end_offset) > 0) {
		set_point(&range->end, &range->end_offset, n, offset);
	}
	set_point(&range->start, &range->start_offset, n, offset);
	return DOM_NO_ERR;
}

dom_exception
dom_range_set_end(dom_range *range, struct dom_node *node, uint32_t offset)
{
	dom_node_internal *n = (dom_node_internal *)node;
	dom_exception err = check_point(n, offset);

	if (err != DOM_NO_ERR) {
		return err;
	}
	if (dom_node_root((struct dom_node *)range->start) !=
		    dom_node_root(node) ||
	    dom_boundary_point_compare(node,
				       offset,
				       (struct dom_node *)range->start,
				       range->start_offset) < 0) {
		set_point(&range->start, &range->start_offset, n, offset);
	}
	set_point(&range->end, &range->end_offset, n, offset);
	return DOM_NO_ERR;
}

void dom_range_collapse(dom_range *range, bool to_start)
{
	if (to_start) {
		set_point(&range->end,
			  &range->end_offset,
			  range->start,
			  range->start_offset);
	} else {
		set_point(&range->start,
			  &range->start_offset,
			  range->end,
			  range->end_offset);
	}
}

void _dom_range_pre_remove(dom_node_internal *node)
{
	dom_node_internal *parent = node->parent;
	struct dom_range *r;
	uint32_t index;

	if (live_ranges == NULL || parent == NULL) {
		return;
	}
	index = node_index(node);
	for (r = live_ranges; r != NULL; r = r->next) {
		if (is_inclusive_ancestor(node, r->start)) {
			set_point(&r->start, &r->start_offset, parent, index);
		}
		if (is_inclusive_ancestor(node, r->end)) {
			set_point(&r->end, &r->end_offset, parent, index);
		}
		if (r->start == parent && r->start_offset > index) {
			r->start_offset--;
		}
		if (r->end == parent && r->end_offset > index) {
			r->end_offset--;
		}
	}
}

void _dom_range_inserted(dom_node_internal *first, dom_node_internal *last)
{
	dom_node_internal *parent = first->parent;
	struct dom_range *r;
	uint32_t index, count = 1;
	dom_node_internal *n;

	if (live_ranges == NULL || parent == NULL) {
		return;
	}
	for (n = first; n != last; n = n->next) {
		count++;
	}
	index = node_index(first);
	for (r = live_ranges; r != NULL; r = r->next) {
		if (r->start == parent && r->start_offset > index) {
			r->start_offset += count;
		}
		if (r->end == parent && r->end_offset > index) {
			r->end_offset += count;
		}
	}
}

static void replace_point(dom_node_internal *node,
			  dom_node_internal *container,
			  uint32_t *point,
			  uint32_t offset,
			  uint32_t count,
			  uint32_t added)
{
	if (container != node) {
		return;
	}
	if (*point > offset && *point <= offset + count) {
		*point = offset;
	} else if (*point > offset + count) {
		*point = *point + added - count;
	}
}

void _dom_range_replace_data(dom_node_internal *node,
			     uint32_t offset,
			     uint32_t count,
			     uint32_t added)
{
	struct dom_range *r;

	for (r = live_ranges; r != NULL; r = r->next) {
		replace_point(
			node, r->start, &r->start_offset, offset, count, added);
		replace_point(
			node, r->end, &r->end_offset, offset, count, added);
	}
}

void _dom_range_split_text(dom_node_internal *node,
			   dom_node_internal *new_node,
			   uint32_t offset)
{
	dom_node_internal *parent = node->parent;
	struct dom_range *r;
	uint32_t index = 0;

	if (live_ranges == NULL) {
		return;
	}
	if (parent != NULL) {
		index = node_index(node) + 1;
	}
	for (r = live_ranges; r != NULL; r = r->next) {
		if (parent == NULL) {
			continue;
		}
		if (r->start == node && r->start_offset > offset) {
			set_point(&r->start,
				  &r->start_offset,
				  new_node,
				  r->start_offset - offset);
		}
		if (r->end == node && r->end_offset > offset) {
			set_point(&r->end,
				  &r->end_offset,
				  new_node,
				  r->end_offset - offset);
		}
		if (r->start == parent && r->start_offset == index) {
			r->start_offset++;
		}
		if (r->end == parent && r->end_offset == index) {
			r->end_offset++;
		}
	}
}

void _dom_range_merge_text(dom_node_internal *node,
			   dom_node_internal *sibling,
			   uint32_t length)
{
	dom_node_internal *parent = sibling->parent;
	struct dom_range *r;
	uint32_t index;

	if (live_ranges == NULL) {
		return;
	}
	index = node_index(sibling);
	for (r = live_ranges; r != NULL; r = r->next) {
		if (r->start == sibling) {
			set_point(&r->start,
				  &r->start_offset,
				  node,
				  r->start_offset + length);
		} else if (r->start == parent && r->start_offset == index) {
			set_point(&r->start, &r->start_offset, node, length);
		}
		if (r->end == sibling) {
			set_point(&r->end,
				  &r->end_offset,
				  node,
				  r->end_offset + length);
		} else if (r->end == parent && r->end_offset == index) {
			set_point(&r->end, &r->end_offset, node, length);
		}
	}
}
