/*
 * This file is part of TinySurf, https://github.com/xyproto/tinysurf
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

/**
 * \file
 * Shadow trees: flat tree traversal and per-scope style selection.
 */

#ifndef NETSURFY_HTML_SHADOW_H
#define NETSURFY_HTML_SHADOW_H

#include <libcss/libcss.h>

struct dom_node;
struct html_content;

struct dom_node *html_shadow_root_of(struct dom_node *node);
struct dom_node *html_shadow_host_of(struct dom_node *node);

struct dom_node *html_flat_parent(struct dom_node *node);
struct dom_node *html_flat_first_child(struct dom_node *node);
struct dom_node *html_flat_next_sibling(struct dom_node *node);

css_select_ctx *html_shadow_select_ctx(struct html_content *c,
				       struct dom_node *node,
				       struct dom_node **scope_host);
void html_shadow_flush(struct html_content *c);

#endif
