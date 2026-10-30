/*
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurfy, http://www.netsurf-browser.org/
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

/**
 * \file
 * HTML content DOM event handling interface
 */

#ifndef NETSURFY_HTML_DOM_EVENT_H
#define NETSURFY_HTML_DOM_EVENT_H

/**
 * html content DOM action callback function selector
 *
 * selects a callback function for libdom to call based on the type and phase.
 * dom_default_action_phase from events/document_event.h
 *
 * The principle events are:
 *   DOMSubtreeModified
 *   DOMAttrModified
 *   DOMNodeInserted
 *   DOMNodeInsertedIntoDocument
 *
 * @return callback function pointer or NULL for none
 */
dom_default_action_callback
html_dom_event_fetcher(dom_string *type,
		       dom_default_action_phase phase,
		       void **pw);

#endif
