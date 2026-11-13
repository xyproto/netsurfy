/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "javascript/quickjs/urlutils.h"

static char *url_component(nsurl *url, nsurl_component part)
{
	char *s = NULL;
	size_t len = 0;

	if (nsurl_get(url, part, &s, &len) != NSERROR_OK || s == NULL) {
		free(s);
		return strdup("");
	}
	return s;
}

static char *url_separated(nsurl *url, nsurl_component part)
{
	char *s = url_component(url, part);

	if (s != NULL && s[0] == ':') {
		memmove(s, s + 1, strlen(s));
	}
	return s;
}

static const char *default_port(const char *scheme)
{
	static const char *const ports[][2] = {
		{"http", "80"},
		{"https", "443"},
		{"ftp", "21"},
		{"ws", "80"},
		{"wss", "443"},
	};

	for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); i++) {
		if (strcasecmp(scheme, ports[i][0]) == 0) {
			return ports[i][1];
		}
	}
	return NULL;
}

static bool special_scheme(const char *scheme)
{
	return default_port(scheme) != NULL;
}

static JSValue url_string(JSContext *ctx,
			  const char *prefix,
			  char *value,
			  const char *suffix,
			  bool empty_if_blank)
{
	JSValue ret;
	size_t len;
	char *buf;

	if (value == NULL) {
		return JS_ThrowOutOfMemory(ctx);
	}
	if (empty_if_blank && value[0] == '\0') {
		free(value);
		return JS_NewString(ctx, "");
	}
	len = strlen(prefix) + strlen(value) + strlen(suffix) + 1;
	buf = malloc(len);
	if (buf == NULL) {
		free(value);
		return JS_ThrowOutOfMemory(ctx);
	}
	snprintf(buf, len, "%s%s%s", prefix, value, suffix);
	ret = JS_NewString(ctx, buf);
	free(buf);
	free(value);
	return ret;
}

JSValue qjsky_url_get(JSContext *ctx, nsurl *url, enum qjsky_url_part part)
{
	switch (part) {
	case QJSKY_URL_HREF:
		return url_string(
			ctx, "", url_component(url, NSURL_WITH_FRAGMENT), "", false);
	case QJSKY_URL_ORIGIN: {
		char *scheme = url_component(url, NSURL_SCHEME);
		JSValue ret;
		if (scheme == NULL) {
			return JS_ThrowOutOfMemory(ctx);
		}
		if (special_scheme(scheme)) {
			ret = url_string(ctx,
					 "",
					 url_component(url,
						       NSURL_SCHEME |
							       NSURL_HOST |
							       NSURL_PORT),
					 "",
					 false);
		} else if (strcasecmp(scheme, "file") == 0) {
			ret = JS_NewString(ctx, "file://");
		} else {
			ret = JS_NewString(ctx, "null");
		}
		free(scheme);
		return ret;
	}
	case QJSKY_URL_PROTOCOL:
		return url_string(
			ctx, "", url_component(url, NSURL_SCHEME), ":", false);
	case QJSKY_URL_USERNAME:
		return url_string(
			ctx, "", url_component(url, NSURL_USERNAME), "", false);
	case QJSKY_URL_PASSWORD:
		return url_string(
			ctx, "", url_separated(url, NSURL_PASSWORD), "", false);
	case QJSKY_URL_HOST:
		return url_string(ctx,
				  "",
				  url_component(url, NSURL_HOST | NSURL_PORT),
				  "",
				  false);
	case QJSKY_URL_HOSTNAME:
		return url_string(
			ctx, "", url_component(url, NSURL_HOST), "", false);
	case QJSKY_URL_PORT:
		return url_string(
			ctx, "", url_separated(url, NSURL_PORT), "", false);
	case QJSKY_URL_PATHNAME:
		return url_string(
			ctx, "", url_component(url, NSURL_PATH), "", false);
	case QJSKY_URL_SEARCH:
		return url_string(
			ctx, "?", url_component(url, NSURL_QUERY), "", true);
	case QJSKY_URL_HASH:
		return url_string(
			ctx, "#", url_component(url, NSURL_FRAGMENT), "", true);
	}
	return JS_NewString(ctx, "");
}

nserror qjsky_url_set(nsurl *url,
		      enum qjsky_url_part part,
		      const char *value,
		      nsurl **out)
{
	char *c[8];
	static const nsurl_component parts[8] = {
		NSURL_SCHEME,
		NSURL_USERNAME,
		NSURL_PASSWORD,
		NSURL_HOST,
		NSURL_PORT,
		NSURL_PATH,
		NSURL_QUERY,
		NSURL_FRAGMENT,
	};
	char *buf;
	size_t len = 32;
	nserror err;
	int slot = -1;

	if (part == QJSKY_URL_HREF) {
		return nsurl_create(value, out);
	}
	for (int i = 0; i < 8; i++) {
		c[i] = (parts[i] == NSURL_PORT || parts[i] == NSURL_PASSWORD)
			       ? url_separated(url, parts[i])
			       : url_component(url, parts[i]);
		if (c[i] == NULL) {
			for (int k = 0; k < i; k++)
				free(c[k]);
			return NSERROR_NOMEM;
		}
	}
	switch (part) {
	case QJSKY_URL_PROTOCOL:
		slot = 0;
		break;
	case QJSKY_URL_USERNAME:
		slot = 1;
		break;
	case QJSKY_URL_PASSWORD:
		slot = 2;
		break;
	case QJSKY_URL_HOSTNAME:
		slot = 3;
		break;
	case QJSKY_URL_PORT:
		slot = 4;
		break;
	case QJSKY_URL_PATHNAME:
		slot = 5;
		break;
	case QJSKY_URL_SEARCH:
		slot = 6;
		break;
	case QJSKY_URL_HASH:
		slot = 7;
		break;
	case QJSKY_URL_HOST: {
		const char *colon = strrchr(value, ':');
		free(c[3]);
		free(c[4]);
		if (colon != NULL && strchr(colon, ']') == NULL) {
			c[3] = strndup(value, (size_t)(colon - value));
			c[4] = strdup(colon + 1);
		} else {
			c[3] = strdup(value);
			c[4] = strdup("");
		}
		break;
	}
	default:
		break;
	}
	if (slot >= 0) {
		const char *v = value;
		size_t vl;
		if ((slot == 6 && v[0] == '?') || (slot == 7 && v[0] == '#')) {
			v++;
		}
		vl = strlen(v);
		if (slot == 0 && vl > 0 && v[vl - 1] == ':') {
			vl--;
		}
		free(c[slot]);
		c[slot] = strndup(v, vl);
	}
	for (int i = 0; i < 8; i++) {
		if (c[i] == NULL) {
			for (int k = 0; k < 8; k++)
				free(c[k]);
			return NSERROR_NOMEM;
		}
		len += strlen(c[i]);
	}
	buf = malloc(len);
	if (buf == NULL) {
		for (int k = 0; k < 8; k++)
			free(c[k]);
		return NSERROR_NOMEM;
	}
	snprintf(buf,
		 len,
		 "%s:%s%s%s%s%s%s%s%s%s%s%s%s%s",
		 c[0],
		 c[3][0] != '\0' ? "//" : "",
		 c[1],
		 c[2][0] != '\0' ? ":" : "",
		 c[2],
		 (c[1][0] != '\0' || c[2][0] != '\0') ? "@" : "",
		 c[3],
		 c[4][0] != '\0' ? ":" : "",
		 c[4],
		 (c[3][0] != '\0' && c[5][0] != '/') ? "/" : "",
		 c[5],
		 c[6][0] != '\0' ? "?" : "",
		 c[6],
		 c[7][0] != '\0' ? "#" : "");
	strncat(buf, c[7], len - strlen(buf) - 1);
	for (int k = 0; k < 8; k++)
		free(c[k]);
	err = nsurl_create(buf, out);
	free(buf);
	return err;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static char *form_decode(const char *s, size_t len)
{
	char *out = malloc(len + 1), *o = out;

	if (out == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < len; i++) {
		if (s[i] == '+') {
			*o++ = ' ';
		} else if (s[i] == '%' && i + 2 < len &&
			   hexval((unsigned char)s[i + 1]) >= 0 &&
			   hexval((unsigned char)s[i + 2]) >= 0) {
			*o++ = (char)(hexval((unsigned char)s[i + 1]) * 16 +
				      hexval((unsigned char)s[i + 2]));
			i += 2;
		} else {
			*o++ = s[i];
		}
	}
	*o = '\0';
	return out;
}

void qjsky_query_clear(struct qjsky_query *q)
{
	for (size_t i = 0; i < q->n; i++) {
		free(q->names[i]);
		free(q->values[i]);
	}
	free(q->names);
	free(q->values);
	q->names = q->values = NULL;
	q->n = 0;
}

bool qjsky_query_append(struct qjsky_query *q,
			const char *name,
			const char *value)
{
	char **names = realloc(q->names, (q->n + 1) * sizeof(*names));
	char **values;

	if (names == NULL) {
		return false;
	}
	q->names = names;
	values = realloc(q->values, (q->n + 1) * sizeof(*values));
	if (values == NULL) {
		return false;
	}
	q->values = values;
	q->names[q->n] = strdup(name);
	q->values[q->n] = strdup(value);
	if (q->names[q->n] == NULL || q->values[q->n] == NULL) {
		free(q->names[q->n]);
		free(q->values[q->n]);
		return false;
	}
	q->n++;
	return true;
}

void qjsky_query_remove(struct qjsky_query *q, size_t index)
{
	free(q->names[index]);
	free(q->values[index]);
	memmove(&q->names[index],
		&q->names[index + 1],
		(q->n - index - 1) * sizeof(char *));
	memmove(&q->values[index],
		&q->values[index + 1],
		(q->n - index - 1) * sizeof(char *));
	q->n--;
}

bool qjsky_query_parse(struct qjsky_query *q, const char *text)
{
	const char *p = text;

	qjsky_query_clear(q);
	if (p[0] == '?') {
		p++;
	}
	while (*p != '\0') {
		const char *amp = strchr(p, '&');
		size_t len = amp != NULL ? (size_t)(amp - p) : strlen(p);
		if (len > 0) {
			const char *eq = memchr(p, '=', len);
			char *name = form_decode(
				p, eq != NULL ? (size_t)(eq - p) : len);
			char *value = eq != NULL
					      ? form_decode(
							eq + 1,
							len - (size_t)(eq - p) -
								1)
					      : strdup("");
			bool ok = name != NULL && value != NULL &&
				  qjsky_query_append(q, name, value);
			free(name);
			free(value);
			if (!ok) {
				return false;
			}
		}
		p += len;
		if (*p == '&') {
			p++;
		}
	}
	return true;
}

static void form_encode(char **o, const char *s)
{
	static const char hex[] = "0123456789ABCDEF";

	for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
		if (isalnum(*c) || *c == '*' || *c == '-' || *c == '.' ||
		    *c == '_') {
			*(*o)++ = (char)*c;
		} else if (*c == ' ') {
			*(*o)++ = '+';
		} else {
			*(*o)++ = '%';
			*(*o)++ = hex[*c >> 4];
			*(*o)++ = hex[*c & 15];
		}
	}
}

char *qjsky_query_serialise(const struct qjsky_query *q)
{
	size_t len = 1;
	char *out, *o;

	for (size_t i = 0; i < q->n; i++) {
		len += 3 * (strlen(q->names[i]) + strlen(q->values[i])) + 2;
	}
	out = malloc(len);
	if (out == NULL) {
		return NULL;
	}
	o = out;
	for (size_t i = 0; i < q->n; i++) {
		if (i > 0) {
			*o++ = '&';
		}
		form_encode(&o, q->names[i]);
		*o++ = '=';
		form_encode(&o, q->values[i]);
	}
	*o = '\0';
	return out;
}
