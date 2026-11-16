/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef dom_internal_core_range_h_
#define dom_internal_core_range_h_

#include <stdint.h>

#include <dom/core/range.h>

struct dom_node_internal;

void _dom_range_pre_remove(struct dom_node_internal *node);
void _dom_range_inserted(struct dom_node_internal *first,
			 struct dom_node_internal *last);
void _dom_range_replace_data(struct dom_node_internal *node,
			     uint32_t offset,
			     uint32_t count,
			     uint32_t added);
void _dom_range_split_text(struct dom_node_internal *node,
			   struct dom_node_internal *new_node,
			   uint32_t offset);
void _dom_range_merge_text(struct dom_node_internal *node,
			   struct dom_node_internal *sibling,
			   uint32_t length);

#endif
