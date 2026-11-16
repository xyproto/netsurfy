/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef dom_core_range_h_
#define dom_core_range_h_

#include <stdbool.h>
#include <stdint.h>

#include <dom/core/exceptions.h>

struct dom_node;

typedef struct dom_range dom_range;

dom_exception dom_range_create(struct dom_node *container, dom_range **result);
void dom_range_destroy(dom_range *range);

void dom_range_get_start(dom_range *range,
			 struct dom_node **container,
			 uint32_t *offset);
void dom_range_get_end(dom_range *range,
		       struct dom_node **container,
		       uint32_t *offset);

dom_exception
dom_range_set_start(dom_range *range, struct dom_node *node, uint32_t offset);
dom_exception
dom_range_set_end(dom_range *range, struct dom_node *node, uint32_t offset);
void dom_range_collapse(dom_range *range, bool to_start);

uint32_t dom_node_length(struct dom_node *node);
struct dom_node *dom_node_root(struct dom_node *node);
int dom_boundary_point_compare(struct dom_node *a_node,
			       uint32_t a_offset,
			       struct dom_node *b_node,
			       uint32_t b_offset);

#endif
