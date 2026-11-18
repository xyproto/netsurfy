/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#ifndef dom_core_documentfragment_h_
#define dom_core_documentfragment_h_

#include <dom/core/exceptions.h>

struct dom_element;

typedef struct dom_document_fragment dom_document_fragment;

dom_exception dom_document_fragment_get_host(dom_document_fragment *frag,
					     struct dom_element **host);

#endif
