/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "netsurfy/browser_window.h"

#include "headless/browser.h"
#include "headless/schedule.h"
#include "headless/duck.h"

#define DUCK_TIMEOUT 60000
#define DUCK_BLOCKED 2
#define DUCK_MARK "\x01netsurfy-duck\x01"

static const char *const duck_bases[] = {
	"https://html.duckduckgo.com/html/",
	"https://lite.duckduckgo.com/lite/",
};

static const char duck_script[] =
	"(function () {"
	"var out = [];"
	"function text(e) {"
	" return e ? e.textContent.replace(/\\s+/g, ' ').trim() : '';"
	"}"
	"function target(a) {"
	" var h = a.getAttribute('href') || '';"
	" var m = /[?&]uddg=([^&]*)/.exec(h);"
	" if (m) h = decodeURIComponent(m[1]);"
	" if (h.indexOf('//') === 0) h = 'https:' + h;"
	" return h;"
	"}"
	"function up(e, re) {"
	" while (e && !(typeof e.className === 'string' && re.test(e.className)))"
	"  e = e.parentNode;"
	" return e;"
	"}"
	"function add(a, snippet) {"
	" var url = target(a);"
	" if (/^https?:/.test(url) && !/duckduckgo\\.com\\/y\\.js/.test(url))"
	"  out.push({ title: text(a), url: url, snippet: text(snippet) });"
	"}"
	"var links = document.querySelectorAll('a.result__a');"
	"for (var i = 0; i < links.length; i++) {"
	" var r = up(links[i], /\\bresult\\b/);"
	" if (r && /result--ad/.test(r.className)) continue;"
	" add(links[i], r && r.querySelector('.result__snippet'));"
	"}"
	"links = document.querySelectorAll('a.result-link');"
	"for (i = 0; i < links.length && out.length === 0; i++) {"
	" var row = links[i].parentNode;"
	" while (row && row.nodeName !== 'TR') row = row.parentNode;"
	" var next = row ? row.nextElementSibling : null;"
	" add(links[i], next && next.querySelector('.result-snippet'));"
	"}"
	"var blocked = out.length === 0 &&"
	" /anomaly/.test(document.documentElement.innerHTML);"
	"console.log('" DUCK_MARK "' + (blocked ? 'B' : 'R') +"
	" JSON.stringify(out, null, 2));"
	"})();";

static char *duck_query;
static size_t duck_attempt;
static bool duck_active;
static bool duck_loading;
static bool duck_finished;
static int duck_status;
static bool duck_blocked;
static char *duck_json;

static void duck_finish(int status, const char *error)
{
	if (duck_finished) {
		return;
	}
	duck_finished = true;
	duck_status = status;
	if (error != NULL) {
		fprintf(stderr, "netsurfy-headless: %s\n", error);
	}
}

static void duck_timeout(void *p)
{
	duck_finish(EXIT_FAILURE, "timed out waiting for DuckDuckGo");
}

static nserror duck_open(void)
{
	static const char hex[] = "0123456789ABCDEF";
	const char *base = duck_bases[duck_attempt];
	size_t len = strlen(duck_query);
	char *text = malloc(strlen(base) + len * 3 + 16);
	char *p;
	nsurl *url;
	nserror res;

	if (text == NULL) {
		return NSERROR_NOMEM;
	}
	p = text + sprintf(text, "%s?q=", base);
	for (size_t i = 0; i < len; i++) {
		unsigned char c = duck_query[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || strchr("-_.~", c) != NULL) {
			*p++ = c;
		} else {
			*p++ = '%';
			*p++ = hex[c >> 4];
			*p++ = hex[c & 15];
		}
	}
	strcpy(p, "&kl=wt-wt");

	res = nsurl_create(text, &url);
	free(text);
	if (res != NSERROR_OK) {
		return res;
	}
	res = browser_window_create(BW_CREATE_HISTORY, url, NULL, NULL, NULL);
	nsurl_unref(url);
	return res;
}

nserror headless_duck_start(const char *query)
{
	duck_query = strdup(query);
	if (duck_query == NULL) {
		return NSERROR_NOMEM;
	}
	duck_active = true;
	headless_schedule(DUCK_TIMEOUT, duck_timeout, NULL);
	return duck_open();
}

bool headless_duck_active(void)
{
	return duck_active;
}

bool headless_duck_done(void)
{
	return duck_finished;
}

int headless_duck_status(void)
{
	if (duck_json != NULL) {
		fputs(duck_json, stdout);
		fputc('\n', stdout);
	}
	free(duck_json);
	free(duck_query);
	duck_json = NULL;
	duck_query = NULL;
	return duck_status;
}

void headless_duck_loading(void)
{
	duck_loading = true;
}

void headless_duck_loaded(struct gui_window *g)
{
	if (!duck_active || duck_finished || !duck_loading) {
		return;
	}
	duck_loading = false;
	free(duck_json);
	duck_json = NULL;
	if (!browser_window_exec(g->bw, duck_script, strlen(duck_script))) {
		duck_json = strdup("[]");
	}
	if (duck_json == NULL) {
		duck_finish(EXIT_FAILURE,
			    "could not read the DuckDuckGo results");
	} else if (strcmp(duck_json, "[]") != 0) {
		duck_finish(EXIT_SUCCESS, NULL);
	} else if (duck_attempt + 1 ==
		   sizeof(duck_bases) / sizeof(duck_bases[0])) {
		if (duck_blocked) {
			duck_finish(DUCK_BLOCKED,
				    "DuckDuckGo blocked the search as unusual "
				    "traffic");
		} else {
			duck_finish(EXIT_SUCCESS, NULL);
		}
	} else {
		duck_attempt++;
		if (duck_open() != NSERROR_OK) {
			duck_finish(EXIT_FAILURE, "could not open DuckDuckGo");
		}
	}
}

bool headless_duck_console(const char *msg, size_t len)
{
	size_t mark = strlen(DUCK_MARK);

	if (!duck_active) {
		return false;
	}
	if (len >= mark && memcmp(msg, DUCK_MARK, mark) == 0) {
		free(duck_json);
		duck_json = NULL;
		if (len > mark) {
			duck_blocked = duck_blocked || msg[mark] == 'B';
			duck_json = strndup(msg + mark + 1, len - mark - 1);
		}
	}
	return true;
}
