/*
 * This file is part of TinySurf, https://github.com/xyproto/tinysurf
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tinysurf/types.h"
#include "tinysurf/url_db.h"
#include "utils/errors.h"
#include "utils/nsurl.h"
#include "content/fetch.h"
#include "content/urldb.h"

#include "private.h"
#include "start.h"

#define START_TILES 8

struct start_tile {
	nsurl *url;
	lwc_string *host;
	const char *title;
	unsigned int visits;
	time_t last_visit;
};

struct start_tiles {
	struct start_tile tile[START_TILES];
	size_t count;
};

static const char start_page_head[] =
	"<!DOCTYPE html>\n"
	"<html><head><meta charset=\"utf-8\">\n"
	"<title>New Tab</title>\n"
	"<style>\n"
	"body { margin: 0; background: #f4f5f7; color: #1f2328; font-family: sans-serif; }\n"
	".page { max-width: 760px; margin: 0 auto; padding: 88px 24px 48px; }\n"
	".brand { text-align: center; font-size: 30px; font-weight: bold; margin-bottom: 30px; }\n"
	".brand img { height: 64px; }\n"
	"form { margin: 0 0 44px; }\n"
	"input { width: 100%; box-sizing: border-box; font-size: 18px; padding: 14px 18px;"
	" border: 1px solid #c4cad3; background: #fff; color: #1f2328; }\n"
	".tiles { display: flex; flex-wrap: wrap; justify-content: center; }\n"
	".tile { display: block; width: 136px; margin: 8px; padding: 16px 8px 14px; text-align: center;"
	" text-decoration: none; color: #1f2328; background: #fff; border: 1px solid #e1e4e8; }\n"
	".mono { display: block; width: 44px; height: 44px; line-height: 44px; margin: 0 auto 12px;"
	" color: #fff; font-size: 20px; font-weight: bold; text-align: center; }\n"
	".label { display: block; font-size: 13px; height: 18px; overflow: hidden; white-space: nowrap; }\n"
	".hint { text-align: center; color: #6b7280; font-size: 14px; }\n"
	"@media (prefers-color-scheme: dark) {\n"
	" body { background: #1b1d21; color: #e6e7ea; }\n"
	" input { background: #26292f; color: #e6e7ea; border-color: #3a3f47; }\n"
	" .tile { background: #24272d; color: #e6e7ea; border-color: #33373f; }\n"
	" .hint { color: #9aa1ab; }\n"
	" .brand img { background: #e6e7ea; padding: 10px 18px; }\n"
	"}\n"
	"</style>\n"
	"</head>\n"
	"<body><div class=\"page\">\n"
	"<div class=\"brand\"><img src=\"resource:tinysurf.png\" alt=\"TinySurf\"></div>\n"
	"<form action=\"about:websearch\" method=\"get\">"
	"<input name=\"q\" type=\"search\" autofocus "
	"placeholder=\"Search the web or type an address\"></form>\n";

static const char *const start_colours[] = {
	"#1a73e8",
	"#d93025",
	"#188038",
	"#e37400",
	"#9334e6",
	"#007b83",
	"#c5221f",
	"#3c4043",
};

static bool start_is_web(nsurl *url)
{
	lwc_string *scheme = nsurl_get_component(url, NSURL_SCHEME);
	bool web = false;

	if (scheme != NULL) {
		const char *s = lwc_string_data(scheme);
		web = strcmp(s, "http") == 0 || strcmp(s, "https") == 0;
		lwc_string_unref(scheme);
	}
	return web;
}

static bool start_better(const struct start_tile *a, const struct start_tile *b)
{
	if (a->visits != b->visits) {
		return a->visits > b->visits;
	}
	return a->last_visit > b->last_visit;
}

static void start_tile_free(struct start_tile *tile)
{
	nsurl_unref(tile->url);
	lwc_string_unref(tile->host);
}

static struct start_tiles *start_current;

static bool start_collect(nsurl *url, const struct url_data *data)
{
	struct start_tiles *tiles = start_current;
	struct start_tile candidate;
	size_t i, worst;

	if (data->visits == 0 || !start_is_web(url)) {
		return true;
	}
	candidate.host = nsurl_get_component(url, NSURL_HOST);
	if (candidate.host == NULL) {
		return true;
	}
	candidate.url = url;
	candidate.title = data->title;
	candidate.visits = data->visits;
	candidate.last_visit = data->last_visit;

	for (i = 0; i < tiles->count; i++) {
		bool same = false;
		if (lwc_string_isequal(tiles->tile[i].host,
				       candidate.host,
				       &same) == lwc_error_ok &&
		    same) {
			if (start_better(&candidate, &tiles->tile[i])) {
				start_tile_free(&tiles->tile[i]);
				tiles->tile[i] = candidate;
				tiles->tile[i].url = nsurl_ref(url);
			} else {
				lwc_string_unref(candidate.host);
			}
			return true;
		}
	}
	if (tiles->count < START_TILES) {
		tiles->tile[tiles->count] = candidate;
		tiles->tile[tiles->count].url = nsurl_ref(url);
		tiles->count++;
		return true;
	}
	worst = 0;
	for (i = 1; i < tiles->count; i++) {
		if (start_better(&tiles->tile[worst], &tiles->tile[i])) {
			worst = i;
		}
	}
	if (start_better(&candidate, &tiles->tile[worst])) {
		start_tile_free(&tiles->tile[worst]);
		tiles->tile[worst] = candidate;
		tiles->tile[worst].url = nsurl_ref(url);
	} else {
		lwc_string_unref(candidate.host);
	}
	return true;
}

static int start_compare(const void *a, const void *b)
{
	const struct start_tile *ta = a;
	const struct start_tile *tb = b;

	if (start_better(ta, tb)) {
		return -1;
	}
	return start_better(tb, ta) ? 1 : 0;
}

static nserror start_send_escaped(struct fetch_about_context *ctx,
				  const char *text,
				  size_t len)
{
	size_t start = 0;
	nserror res = NSERROR_OK;

	for (size_t i = 0; i < len && res == NSERROR_OK; i++) {
		const char *entity = NULL;
		switch (text[i]) {
		case '&':
			entity = "&amp;";
			break;
		case '<':
			entity = "&lt;";
			break;
		case '>':
			entity = "&gt;";
			break;
		case '"':
			entity = "&quot;";
			break;
		default:
			continue;
		}
		res = fetch_about_senddata(ctx,
					   (const uint8_t *)text + start,
					   i - start);
		if (res == NSERROR_OK) {
			res = fetch_about_senddata(ctx,
						   (const uint8_t *)entity,
						   strlen(entity));
		}
		start = i + 1;
	}
	if (res == NSERROR_OK && start < len) {
		res = fetch_about_senddata(ctx,
					   (const uint8_t *)text + start,
					   len - start);
	}
	return res;
}

static nserror
start_send_tile(struct fetch_about_context *ctx, const struct start_tile *tile)
{
	const char *host = lwc_string_data(tile->host);
	size_t host_len = lwc_string_length(tile->host);
	const char *label;
	size_t label_len;
	unsigned int hash = 0;
	char initial;
	nserror res;

	if (host_len > 4 && strncmp(host, "www.", 4) == 0) {
		host += 4;
		host_len -= 4;
	}
	for (size_t i = 0; i < host_len; i++) {
		hash = hash * 31 + (unsigned char)host[i];
	}
	initial = host_len > 0 ? host[0] : '?';
	if (initial >= 'a' && initial <= 'z') {
		initial = initial - 'a' + 'A';
	}
	if (tile->title != NULL && tile->title[0] != '\0') {
		label = tile->title;
		label_len = strlen(label);
	} else {
		label = host;
		label_len = host_len;
	}

	res = fetch_about_ssenddataf(ctx, "<a class=\"tile\" href=\"");
	if (res == NSERROR_OK) {
		res = start_send_escaped(ctx,
					 nsurl_access(tile->url),
					 nsurl_length(tile->url));
	}
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(
			ctx,
			"\"><span class=\"mono\" style=\"background: %s\">",
			start_colours[hash % (sizeof(start_colours) /
					      sizeof(start_colours[0]))]);
	}
	if (res == NSERROR_OK) {
		res = start_send_escaped(ctx, &initial, 1);
	}
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx,
					     "</span><span class=\"label\">");
	}
	if (res == NSERROR_OK) {
		res = start_send_escaped(ctx, label, label_len);
	}
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "</span></a>\n");
	}
	return res;
}

bool fetch_about_start_handler(struct fetch_about_context *ctx)
{
	struct start_tiles tiles;
	nserror res;
	size_t i;

	memset(&tiles, 0, sizeof(tiles));
	start_current = &tiles;
	urldb_iterate_entries(start_collect);
	start_current = NULL;
	qsort(tiles.tile, tiles.count, sizeof(tiles.tile[0]), start_compare);

	fetch_about_set_http_code(ctx, 200);
	if (fetch_about_send_header(ctx,
				    "Content-Type: text/html; charset=utf-8")) {
		goto aborted;
	}
	res = fetch_about_ssenddataf(ctx, "%s", start_page_head);
	if (res == NSERROR_OK && tiles.count > 0) {
		res = fetch_about_ssenddataf(ctx, "<div class=\"tiles\">\n");
		for (i = 0; i < tiles.count && res == NSERROR_OK; i++) {
			res = start_send_tile(ctx, &tiles.tile[i]);
		}
		if (res == NSERROR_OK) {
			res = fetch_about_ssenddataf(ctx, "</div>\n");
		}
	} else if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(
			ctx,
			"<p class=\"hint\">Sites you visit often will appear here.</p>\n");
	}
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "</div></body></html>\n");
	}
	if (res != NSERROR_OK) {
		goto aborted;
	}
	for (i = 0; i < tiles.count; i++) {
		start_tile_free(&tiles.tile[i]);
	}
	fetch_about_send_finished(ctx);
	return true;

aborted:
	for (i = 0; i < tiles.count; i++) {
		start_tile_free(&tiles.tile[i]);
	}
	return false;
}
