/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#ifndef NETSURFY_JAVASCRIPT_QUICKJS_URLUTILS_H
#define NETSURFY_JAVASCRIPT_QUICKJS_URLUTILS_H

#include <stddef.h>
#include <quickjs.h>

#include "utils/errors.h"
#include "utils/nsurl.h"

enum qjsky_url_part {
	QJSKY_URL_HREF,
	QJSKY_URL_ORIGIN,
	QJSKY_URL_PROTOCOL,
	QJSKY_URL_USERNAME,
	QJSKY_URL_PASSWORD,
	QJSKY_URL_HOST,
	QJSKY_URL_HOSTNAME,
	QJSKY_URL_PORT,
	QJSKY_URL_PATHNAME,
	QJSKY_URL_SEARCH,
	QJSKY_URL_HASH,
};

struct qjsky_query {
	char **names;
	char **values;
	size_t n;
};

JSValue qjsky_url_get(JSContext *ctx, nsurl *url, enum qjsky_url_part part);
nserror qjsky_url_set(nsurl *url,
		      enum qjsky_url_part part,
		      const char *value,
		      nsurl **out);

void qjsky_query_clear(struct qjsky_query *q);
bool qjsky_query_parse(struct qjsky_query *q, const char *text);
bool qjsky_query_append(struct qjsky_query *q,
			const char *name,
			const char *value);
void qjsky_query_remove(struct qjsky_query *q, size_t index);
char *qjsky_query_serialise(const struct qjsky_query *q);

#endif
