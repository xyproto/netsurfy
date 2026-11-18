/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "utils/log.h"
#include "utils/utils.h"
#include "html/box.h"
#include "html/html.h"
#include "html/private.h"
#include "html/box_inspect.h"
#include "html/layout_internal.h"

#define GRID_MAX_TRACKS 1000
#define GRID_NAME_LEN 64

enum grid_size {
	GS_FIXED,
	GS_PCT,
	GS_AUTO,
	GS_MIN_CONTENT,
	GS_MAX_CONTENT,
	GS_FR,
	GS_FIT
};

struct grid_track_def {
	enum grid_size min_type;
	enum grid_size max_type;
	float min_value;
	float max_value;
};

struct grid_line_name {
	char name[GRID_NAME_LEN];
	int line;
};

struct grid_area {
	char name[GRID_NAME_LEN];
	int r0, r1, c0, c1;
};

struct grid_axis {
	struct grid_track_def *defs;
	int count;
	int alloc;
	struct grid_line_name *names;
	int n_names;
	int names_alloc;
	struct grid_track_def auto_def;
	int explicit_count;
	int *size;
	int *pos;
	int gap;
};

struct grid_item {
	struct box *box;
	int r0, r1, c0, c1;
	bool rdef, cdef;
	int outer_height;
};

struct grid_placement {
	int kind;
	int n;
	char name[GRID_NAME_LEN];
};

enum {
	GP_AUTO,
	GP_LINE,
	GP_SPAN,
	GP_NAME
};

struct grid_ctx {
	html_content *content;
	const css_unit_ctx *unit_ctx;
	struct box *grid;
	struct grid_axis cols;
	struct grid_axis rows;
	struct grid_area *areas;
	int n_areas;
	int area_rows;
	int area_cols;
	struct grid_item *items;
	int n_items;
	bool column_flow;
};

static const char *skip_ws(const char *p)
{
	while (*p != '\0' && isspace((unsigned char)*p)) {
		p++;
	}
	return p;
}

static bool word_is(const char *p, const char *word)
{
	size_t len = strlen(word);
	return strncasecmp(p, word, len) == 0 &&
	       !(isalnum((unsigned char)p[len]) || p[len] == '-' ||
		 p[len] == '_');
}

static const char *skip_group(const char *p)
{
	int depth = 0;

	for (; *p != '\0'; p++) {
		if (*p == '(') {
			depth++;
		} else if (*p == ')') {
			if (depth == 0) {
				return p;
			}
			depth--;
		}
	}
	return p;
}

static const struct {
	const char *name;
	css_unit unit;
} grid_units[] = {
	{"px", CSS_UNIT_PX},
	{"rem", CSS_UNIT_REM},
	{"em", CSS_UNIT_EM},
	{"ex", CSS_UNIT_EX},
	{"ch", CSS_UNIT_CH},
	{"vw", CSS_UNIT_VW},
	{"vh", CSS_UNIT_VH},
	{"vmin", CSS_UNIT_VMIN},
	{"vmax", CSS_UNIT_VMAX},
	{"pt", CSS_UNIT_PT},
	{"pc", CSS_UNIT_PC},
	{"cm", CSS_UNIT_CM},
	{"mm", CSS_UNIT_MM},
	{"in", CSS_UNIT_IN},
	{"q", CSS_UNIT_Q},
};

static const char *parse_breadth(struct grid_ctx *ctx,
				 const char *p,
				 enum grid_size *type,
				 float *value)
{
	char *end;
	float v;

	p = skip_ws(p);
	if (word_is(p, "auto")) {
		*type = GS_AUTO;
		return p + 4;
	}
	if (word_is(p, "min-content")) {
		*type = GS_MIN_CONTENT;
		return p + 11;
	}
	if (word_is(p, "max-content")) {
		*type = GS_MAX_CONTENT;
		return p + 11;
	}
	v = strtof(p, &end);
	if (end == p) {
		const char *q = p;
		while (*q != '\0' && !isspace((unsigned char)*q) &&
		       *q != ',' && *q != ')' && *q != '[') {
			if (*q == '(') {
				q = skip_group(q + 1);
				if (*q == ')') {
					q++;
				}
				continue;
			}
			q++;
		}
		*type = GS_AUTO;
		return q;
	}
	p = end;
	if (*p == '%') {
		*type = GS_PCT;
		*value = v;
		return p + 1;
	}
	if (strncasecmp(p, "fr", 2) == 0) {
		*type = GS_FR;
		*value = v;
		return p + 2;
	}
	*type = GS_FIXED;
	*value = v;
	for (size_t i = 0; i < sizeof(grid_units) / sizeof(grid_units[0]);
	     i++) {
		size_t len = strlen(grid_units[i].name);
		if (strncasecmp(p, grid_units[i].name, len) == 0 &&
		    !isalpha((unsigned char)p[len])) {
			*value = FIXTOFLT(css_unit_len2device_px(
				ctx->grid->style,
				ctx->unit_ctx,
				FLTTOFIX(v),
				grid_units[i].unit));
			return p + len;
		}
	}
	while (isalpha((unsigned char)*p)) {
		p++;
	}
	return p;
}

static const char *parse_track_size(struct grid_ctx *ctx,
				    const char *p,
				    struct grid_track_def *def)
{
	p = skip_ws(p);
	memset(def, 0, sizeof(*def));
	if (strncasecmp(p, "minmax(", 7) == 0) {
		p = parse_breadth(ctx, p + 7, &def->min_type, &def->min_value);
		p = skip_ws(p);
		if (*p == ',') {
			p++;
		}
		p = parse_breadth(ctx, p, &def->max_type, &def->max_value);
		if (def->min_type == GS_FR) {
			def->min_type = GS_AUTO;
		}
		p = skip_group(p);
		return *p == ')' ? p + 1 : p;
	}
	if (strncasecmp(p, "fit-content(", 12) == 0) {
		enum grid_size type;
		float value = 0;
		p = parse_breadth(ctx, p + 12, &type, &value);
		def->min_type = GS_AUTO;
		def->max_type = type == GS_FIXED ? GS_FIT : GS_MAX_CONTENT;
		def->max_value = value;
		p = skip_group(p);
		return *p == ')' ? p + 1 : p;
	}
	p = parse_breadth(ctx, p, &def->max_type, &def->max_value);
	if (def->max_type == GS_FR) {
		def->min_type = GS_AUTO;
	} else {
		def->min_type = def->max_type;
		def->min_value = def->max_value;
	}
	return p;
}

static bool axis_add_track(struct grid_axis *axis,
			   const struct grid_track_def *def)
{
	if (axis->count >= GRID_MAX_TRACKS) {
		return true;
	}
	struct grid_track_def copy = *def;

	if (axis->count == axis->alloc) {
		int alloc = axis->alloc == 0 ? 16 : axis->alloc * 2;
		struct grid_track_def *defs = realloc(axis->defs,
						      alloc * sizeof(*defs));
		if (defs == NULL) {
			return false;
		}
		axis->defs = defs;
		axis->alloc = alloc;
	}
	axis->defs[axis->count++] = copy;
	return true;
}

static bool axis_add_name(struct grid_axis *axis, const char *name, size_t len)
{
	char copy[GRID_NAME_LEN];

	if (len == 0 || len >= GRID_NAME_LEN) {
		return true;
	}
	memcpy(copy, name, len);
	if (axis->n_names == axis->names_alloc) {
		int alloc = axis->names_alloc == 0 ? 16
						   : axis->names_alloc * 2;
		struct grid_line_name *names = realloc(axis->names,
						       alloc * sizeof(*names));
		if (names == NULL) {
			return false;
		}
		axis->names = names;
		axis->names_alloc = alloc;
	}
	memcpy(axis->names[axis->n_names].name, copy, len);
	axis->names[axis->n_names].name[len] = '\0';
	axis->names[axis->n_names].line = axis->count;
	axis->n_names++;
	return true;
}

static const char *parse_names(struct grid_axis *axis, const char *p)
{
	p++;
	while (*p != '\0' && *p != ']') {
		const char *start;
		p = skip_ws(p);
		start = p;
		while (*p != '\0' && *p != ']' && !isspace((unsigned char)*p)) {
			p++;
		}
		if (!axis_add_name(axis, start, p - start)) {
			return NULL;
		}
	}
	return *p == ']' ? p + 1 : p;
}

static int fixed_track_size(const struct grid_track_def *def, int avail)
{
	enum grid_size type = def->max_type;
	float value = def->max_value;

	if (type != GS_FIXED && type != GS_PCT) {
		type = def->min_type;
		value = def->min_value;
	}
	if (type == GS_FIXED) {
		return (int)value;
	}
	if (type == GS_PCT && avail >= 0) {
		return (int)(value * avail / 100);
	}
	return -1;
}

static bool parse_track_list(struct grid_ctx *ctx,
			     struct grid_axis *axis,
			     const char *p,
			     int avail)
{
	while (p != NULL && *(p = skip_ws(p)) != '\0') {
		if (*p == '[') {
			p = parse_names(axis, p);
		} else if (strncasecmp(p, "repeat(", 7) == 0) {
			const char *end;
			int repeat = 1;
			bool auto_repeat = false;
			int first = axis->count;
			int n_inner;

			p = skip_ws(p + 7);
			if (word_is(p, "auto-fill") || word_is(p, "auto-fit")) {
				auto_repeat = true;
				p += word_is(p, "auto-fill") ? 9 : 8;
			} else {
				repeat = (int)strtol(p, (char **)&p, 10);
			}
			p = skip_ws(p);
			if (*p == ',') {
				p++;
			}
			end = skip_group(p);
			while (*(p = skip_ws(p)) != '\0' && p < end) {
				if (*p == '[') {
					p = parse_names(axis, p);
					if (p == NULL) {
						return false;
					}
				} else {
					struct grid_track_def def;
					const char *next = parse_track_size(
						ctx, p, &def);
					if (next == p) {
						break;
					}
					p = next;
					if (!axis_add_track(axis, &def)) {
						return false;
					}
				}
			}
			n_inner = axis->count - first;
			if (auto_repeat && n_inner > 0) {
				int sum = 0;
				for (int i = first; i < axis->count; i++) {
					int s = fixed_track_size(&axis->defs[i],
								 avail);
					sum += s > 0 ? s : 0;
				}
				repeat = 1;
				if (sum > 0 && avail > 0) {
					repeat = (avail + axis->gap) /
						 (sum + axis->gap * n_inner);
					if (repeat < 1) {
						repeat = 1;
					}
				}
			}
			if (repeat > GRID_MAX_TRACKS) {
				repeat = GRID_MAX_TRACKS;
			}
			for (int r = 1; r < repeat && n_inner > 0; r++) {
				int names = axis->n_names;
				for (int i = 0; i < names; i++) {
					if (axis->names[i].line >= first &&
					    axis->names[i].line <
						    first + n_inner) {
						int line = axis->names[i].line;
						axis->count = first + r * n_inner +
							      (line - first);
						if (!axis_add_name(
							    axis,
							    axis->names[i].name,
							    strlen(axis->names[i]
									   .name))) {
							return false;
						}
					}
				}
				axis->count = first + r * n_inner;
				for (int i = 0; i < n_inner; i++) {
					if (!axis_add_track(
						    axis,
						    &axis->defs[first + i])) {
						return false;
					}
				}
			}
			p = *end == ')' ? end + 1 : end;
		} else if (word_is(p, "none") || word_is(p, "subgrid") ||
			   word_is(p, "masonry")) {
			p += strlen(word_is(p, "none")      ? "none"
				    : word_is(p, "subgrid") ? "subgrid"
							    : "masonry");
		} else {
			struct grid_track_def def;
			const char *next = parse_track_size(ctx, p, &def);
			if (next == p) {
				p++;
				continue;
			}
			p = next;
			if (!axis_add_track(axis, &def)) {
				return false;
			}
		}
	}
	return p != NULL;
}

static bool parse_areas(struct grid_ctx *ctx, const char *p)
{
	int row = 0;

	while (*(p = skip_ws(p)) != '\0') {
		int col = 0;
		if (*p != '"' && *p != '\'') {
			p++;
			continue;
		}
		p++;
		while (*p != '\0' && *p != '"' && *p != '\'') {
			const char *start;
			size_t len;
			p = skip_ws(p);
			if (*p == '"' || *p == '\'' || *p == '\0') {
				break;
			}
			start = p;
			while (*p != '\0' && *p != '"' && *p != '\'' &&
			       !isspace((unsigned char)*p)) {
				p++;
			}
			len = p - start;
			if (start[0] != '.' && len < GRID_NAME_LEN) {
				int i;
				for (i = 0; i < ctx->n_areas; i++) {
					if (strlen(ctx->areas[i].name) == len &&
					    strncmp(ctx->areas[i].name,
						    start,
						    len) == 0) {
						break;
					}
				}
				if (i == ctx->n_areas) {
					struct grid_area *areas = realloc(
						ctx->areas,
						(ctx->n_areas + 1) *
							sizeof(*areas));
					if (areas == NULL) {
						return false;
					}
					ctx->areas = areas;
					memcpy(areas[i].name, start, len);
					areas[i].name[len] = '\0';
					areas[i].r0 = row;
					areas[i].r1 = row + 1;
					areas[i].c0 = col;
					areas[i].c1 = col + 1;
					ctx->n_areas++;
				} else {
					struct grid_area *a = &ctx->areas[i];
					a->r0 = min(a->r0, row);
					a->r1 = max(a->r1, row + 1);
					a->c0 = min(a->c0, col);
					a->c1 = max(a->c1, col + 1);
				}
			}
			col++;
		}
		if (*p != '\0') {
			p++;
		}
		ctx->area_cols = max(ctx->area_cols, col);
		row++;
	}
	ctx->area_rows = row;
	return true;
}

static void parse_placement(const char *p, struct grid_placement *gp)
{
	memset(gp, 0, sizeof(*gp));
	gp->kind = GP_AUTO;
	if (p == NULL) {
		return;
	}
	while (*(p = skip_ws(p)) != '\0' && *p != '/') {
		if (word_is(p, "span")) {
			gp->kind = GP_SPAN;
			if (gp->n == 0) {
				gp->n = 1;
			}
			p += 4;
		} else if (word_is(p, "auto")) {
			p += 4;
		} else if (isdigit((unsigned char)*p) || *p == '-' ||
			   *p == '+') {
			gp->n = (int)strtol(p, (char **)&p, 10);
			if (gp->kind == GP_AUTO) {
				gp->kind = GP_LINE;
			}
		} else {
			const char *start = p;
			size_t len;
			while (*p != '\0' && *p != '/' &&
			       !isspace((unsigned char)*p)) {
				p++;
			}
			len = p - start;
			if (len >= GRID_NAME_LEN) {
				len = GRID_NAME_LEN - 1;
			}
			memcpy(gp->name, start, len);
			gp->name[len] = '\0';
			if (gp->kind != GP_SPAN) {
				gp->kind = GP_NAME;
			}
		}
	}
	if (gp->kind == GP_SPAN && gp->n < 1) {
		gp->n = 1;
	}
}

static const char *split_part(const char *s, int index)
{
	while (s != NULL && index > 0) {
		s = strchr(s, '/');
		if (s != NULL) {
			s++;
		}
		index--;
	}
	return s;
}

static int resolve_name(struct grid_ctx *ctx,
			struct grid_axis *axis,
			bool columns,
			const char *name,
			int nth,
			bool start)
{
	size_t len = strlen(name);
	int seen = 0;

	if (nth == 0) {
		nth = 1;
	}
	for (int i = 0; i < ctx->n_areas; i++) {
		const struct grid_area *a = &ctx->areas[i];
		if (strcmp(a->name, name) == 0) {
			return columns ? (start ? a->c0 : a->c1)
				       : (start ? a->r0 : a->r1);
		}
		if (len > 6 && strcmp(name + len - 6, "-start") == 0 &&
		    strncmp(a->name, name, len - 6) == 0 &&
		    a->name[len - 6] == '\0') {
			return columns ? a->c0 : a->r0;
		}
		if (len > 4 && strcmp(name + len - 4, "-end") == 0 &&
		    strncmp(a->name, name, len - 4) == 0 &&
		    a->name[len - 4] == '\0') {
			return columns ? a->c1 : a->r1;
		}
	}
	for (int i = 0; i < axis->n_names; i++) {
		if (strcmp(axis->names[i].name, name) == 0 && ++seen == nth) {
			return axis->names[i].line;
		}
	}
	for (int i = 0; i < axis->n_names; i++) {
		size_t nl = strlen(axis->names[i].name);
		const char *suffix = start ? "-start" : "-end";
		size_t sl = strlen(suffix);
		if (nl == len + sl && strncmp(axis->names[i].name, name, len) == 0 &&
		    strcmp(axis->names[i].name + len, suffix) == 0) {
			return axis->names[i].line;
		}
	}
	return -1;
}

static int resolve_line(struct grid_ctx *ctx,
			struct grid_axis *axis,
			bool columns,
			const struct grid_placement *gp,
			bool start)
{
	if (gp->kind == GP_LINE) {
		if (gp->n > 0) {
			return gp->n - 1;
		}
		if (gp->n < 0) {
			int line = axis->explicit_count + 1 + gp->n;
			return line < 0 ? 0 : line;
		}
		return -1;
	}
	if (gp->kind == GP_NAME) {
		return resolve_name(ctx, axis, columns, gp->name, gp->n, start);
	}
	return -1;
}

static void resolve_axis(struct grid_ctx *ctx,
			 struct grid_axis *axis,
			 bool columns,
			 const struct grid_placement *start,
			 const struct grid_placement *end,
			 int *s,
			 int *e,
			 bool *definite)
{
	int a = resolve_line(ctx, axis, columns, start, true);
	int b = resolve_line(ctx, axis, columns, end, false);
	int span = 1;

	if (end->kind == GP_AUTO && start->kind == GP_NAME && a >= 0) {
		struct grid_placement same = *start;
		b = resolve_line(ctx, axis, columns, &same, false);
		if (b <= a) {
			b = -1;
		}
	}
	if (start->kind == GP_SPAN) {
		span = start->n;
	} else if (end->kind == GP_SPAN) {
		span = end->n;
	}
	if (a >= 0 && b >= 0) {
		if (b < a) {
			int t = a;
			a = b;
			b = t;
		}
		if (b == a) {
			b = a + 1;
		}
		*definite = true;
	} else if (a >= 0) {
		b = a + span;
		*definite = true;
	} else if (b >= 0) {
		a = b - span;
		if (a < 0) {
			a = 0;
			b = span;
		}
		*definite = true;
	} else {
		a = 0;
		b = span;
		*definite = false;
	}
	*s = a;
	*e = b;
}

static const char *style_string(uint8_t (*get)(const css_computed_style *,
					       lwc_string **),
				const css_computed_style *style)
{
	lwc_string *value = NULL;

	get(style, &value);
	return value != NULL ? lwc_string_data(value) : NULL;
}

static void item_placement(struct grid_ctx *ctx, struct grid_item *item)
{
	const css_computed_style *style = item->box->style;
	const char *area = style_string(css_computed_grid_area, style);
	const char *row = style_string(css_computed_grid_row, style);
	const char *col = style_string(css_computed_grid_column, style);
	const char *rs = style_string(css_computed_grid_row_start, style);
	const char *re = style_string(css_computed_grid_row_end, style);
	const char *cs = style_string(css_computed_grid_column_start, style);
	const char *ce = style_string(css_computed_grid_column_end, style);
	struct grid_placement p_rs, p_re, p_cs, p_ce;

	parse_placement(rs != NULL	  ? rs
			: row != NULL	  ? row
					  : split_part(area, 0),
			&p_rs);
	parse_placement(cs != NULL	  ? cs
			: col != NULL	  ? col
					  : split_part(area, 1),
			&p_cs);
	parse_placement(re != NULL	  ? re
			: row != NULL	  ? split_part(row, 1)
					  : split_part(area, 2),
			&p_re);
	parse_placement(ce != NULL	  ? ce
			: col != NULL	  ? split_part(col, 1)
					  : split_part(area, 3),
			&p_ce);
	if (area != NULL && row == NULL && rs == NULL && re == NULL &&
	    strchr(area, '/') == NULL && p_rs.kind == GP_NAME) {
		p_cs = p_rs;
		p_re = p_rs;
		p_ce = p_rs;
	} else if (area != NULL && split_part(area, 1) == NULL &&
		   p_rs.kind == GP_NAME) {
		p_cs = p_rs;
	}
	resolve_axis(ctx,
		     &ctx->rows,
		     false,
		     &p_rs,
		     &p_re,
		     &item->r0,
		     &item->r1,
		     &item->rdef);
	resolve_axis(ctx,
		     &ctx->cols,
		     true,
		     &p_cs,
		     &p_ce,
		     &item->c0,
		     &item->c1,
		     &item->cdef);
}

struct grid_occupancy {
	bool *cells;
	int rows;
	int cols;
};

static bool occ_ensure(struct grid_occupancy *occ, int rows)
{
	bool *cells;

	if (rows <= occ->rows) {
		return true;
	}
	cells = realloc(occ->cells, (size_t)rows * occ->cols * sizeof(bool));
	if (cells == NULL) {
		return false;
	}
	memset(cells + (size_t)occ->rows * occ->cols,
	       0,
	       (size_t)(rows - occ->rows) * occ->cols * sizeof(bool));
	occ->cells = cells;
	occ->rows = rows;
	return true;
}

static bool occ_free(struct grid_occupancy *occ, int r0, int r1, int c0, int c1)
{
	if (c1 > occ->cols) {
		return false;
	}
	for (int r = r0; r < r1 && r < occ->rows; r++) {
		for (int c = c0; c < c1; c++) {
			if (occ->cells[(size_t)r * occ->cols + c]) {
				return false;
			}
		}
	}
	return true;
}

static bool occ_mark(struct grid_occupancy *occ, int r0, int r1, int c0, int c1)
{
	if (!occ_ensure(occ, r1)) {
		return false;
	}
	for (int r = r0; r < r1; r++) {
		for (int c = c0; c < c1 && c < occ->cols; c++) {
			occ->cells[(size_t)r * occ->cols + c] = true;
		}
	}
	return true;
}

static void swap_item_axes(struct grid_ctx *ctx)
{
	for (int i = 0; i < ctx->n_items; i++) {
		struct grid_item *it = &ctx->items[i];
		int t0 = it->r0, t1 = it->r1;
		bool td = it->rdef;
		it->r0 = it->c0;
		it->r1 = it->c1;
		it->rdef = it->cdef;
		it->c0 = t0;
		it->c1 = t1;
		it->cdef = td;
	}
}

static bool place_items(struct grid_ctx *ctx)
{
	struct grid_occupancy occ = {NULL, 0, 0};
	int cols;
	int cursor_r = 0, cursor_c = 0;
	bool ok = true;

	if (ctx->column_flow) {
		swap_item_axes(ctx);
	}
	cols = ctx->column_flow ? ctx->rows.explicit_count
				: ctx->cols.explicit_count;
	for (int i = 0; i < ctx->n_items; i++) {
		struct grid_item *it = &ctx->items[i];
		if (it->cdef) {
			cols = max(cols, it->c1);
		} else {
			cols = max(cols, it->c1 - it->c0);
		}
	}
	if (cols < 1) {
		cols = 1;
	}
	occ.cols = cols;

	for (int i = 0; i < ctx->n_items && ok; i++) {
		struct grid_item *it = &ctx->items[i];
		if (it->rdef && it->cdef) {
			ok = occ_mark(&occ, it->r0, it->r1, it->c0, it->c1);
		}
	}
	for (int i = 0; i < ctx->n_items && ok; i++) {
		struct grid_item *it = &ctx->items[i];
		if (it->rdef && !it->cdef) {
			int span = it->c1 - it->c0;
			int c = 0;
			while (c + span <= cols &&
			       !occ_free(&occ, it->r0, it->r1, c, c + span)) {
				c++;
			}
			if (c + span > cols) {
				c = 0;
			}
			it->c0 = c;
			it->c1 = c + span;
			ok = occ_mark(&occ, it->r0, it->r1, it->c0, it->c1);
		}
	}
	for (int i = 0; i < ctx->n_items && ok; i++) {
		struct grid_item *it = &ctx->items[i];
		int rspan = it->r1 - it->r0;
		int cspan = it->c1 - it->c0;

		if (it->rdef) {
			continue;
		}
		if (it->cdef) {
			if (it->c0 < cursor_c) {
				cursor_r++;
			}
			cursor_c = it->c0;
			while (!occ_free(&occ,
					 cursor_r,
					 cursor_r + rspan,
					 it->c0,
					 it->c1)) {
				cursor_r++;
			}
		} else {
			for (;;) {
				if (cursor_c + cspan > cols) {
					cursor_c = 0;
					cursor_r++;
					continue;
				}
				if (occ_free(&occ,
					     cursor_r,
					     cursor_r + rspan,
					     cursor_c,
					     cursor_c + cspan)) {
					break;
				}
				cursor_c++;
			}
			it->c0 = cursor_c;
			it->c1 = cursor_c + cspan;
		}
		it->r0 = cursor_r;
		it->r1 = cursor_r + rspan;
		ok = occ_mark(&occ, it->r0, it->r1, it->c0, it->c1);
		cursor_c = it->c1;
	}
	free(occ.cells);
	if (ctx->column_flow) {
		swap_item_axes(ctx);
	}
	return ok;
}

static bool axis_resolve_count(struct grid_axis *axis, int needed)
{
	while (axis->count < needed) {
		if (!axis_add_track(axis, &axis->auto_def)) {
			return false;
		}
		if (axis->count >= GRID_MAX_TRACKS) {
			break;
		}
	}
	free(axis->size);
	free(axis->pos);
	axis->size = calloc(axis->count + 1, sizeof(int));
	axis->pos = calloc(axis->count + 1, sizeof(int));
	return axis->size != NULL && axis->pos != NULL;
}

static int clamp_index(int v, int count)
{
	return v < 0 ? 0 : v > count ? count : v;
}

static int item_min_contribution(const struct box *b)
{
	return lh__automatic_min_width(b);
}

static void size_tracks(struct grid_axis *axis,
			const struct grid_item *items,
			int n_items,
			bool columns,
			int avail)
{
	int n = axis->count;
	int *base = axis->size;
	int *limit;
	int total, free_space;
	float fr_total = 0;

	if (n == 0) {
		return;
	}
	limit = calloc(n, sizeof(int));
	if (limit == NULL) {
		return;
	}
	for (int i = 0; i < n; i++) {
		const struct grid_track_def *d = &axis->defs[i];
		int mn = 0, mx = 0;
		bool content_min = d->min_type == GS_AUTO ||
				   d->min_type == GS_MIN_CONTENT ||
				   d->min_type == GS_MAX_CONTENT ||
				   (d->min_type == GS_PCT && avail < 0);

		for (int k = 0; k < n_items; k++) {
			const struct grid_item *it = &items[k];
			int a = columns ? it->c0 : it->r0;
			int b = columns ? it->c1 : it->r1;
			int cmin, cmax;
			if (a != i || b != i + 1) {
				continue;
			}
			if (columns) {
				cmin = item_min_contribution(it->box);
				cmax = it->box->max_width;
			} else {
				cmin = cmax = it->outer_height;
			}
			mn = max(mn, cmin);
			mx = max(mx, cmax);
		}
		mx = max(mx, mn);
		if (d->min_type == GS_FIXED) {
			base[i] = (int)d->min_value;
		} else if (d->min_type == GS_PCT && avail >= 0) {
			base[i] = (int)(d->min_value * avail / 100);
		} else if (d->min_type == GS_MAX_CONTENT) {
			base[i] = mx;
		} else if (content_min) {
			base[i] = mn;
		}
		switch (d->max_type) {
		case GS_FIXED:
			limit[i] = (int)d->max_value;
			break;
		case GS_PCT:
			limit[i] = avail >= 0 ? (int)(d->max_value * avail / 100)
					      : mx;
			break;
		case GS_MIN_CONTENT:
			limit[i] = mn;
			break;
		case GS_FIT:
			limit[i] = min(mx, (int)d->max_value);
			break;
		case GS_FR:
			fr_total += d->max_value;
			limit[i] = base[i];
			break;
		default:
			limit[i] = mx;
			break;
		}
		if (limit[i] < base[i]) {
			limit[i] = base[i];
		}
	}

	for (int k = 0; k < n_items; k++) {
		const struct grid_item *it = &items[k];
		int a = clamp_index(columns ? it->c0 : it->r0, n);
		int b = clamp_index(columns ? it->c1 : it->r1, n);
		int need = columns ? item_min_contribution(it->box)
				   : it->outer_height;
		int have = axis->gap * (b - a - 1);
		int flexible = 0;
		if (b - a < 2) {
			continue;
		}
		for (int i = a; i < b; i++) {
			have += base[i];
			if (axis->defs[i].min_type != GS_FIXED) {
				flexible++;
			}
		}
		if (need > have && flexible > 0) {
			int extra = (need - have + flexible - 1) / flexible;
			for (int i = a; i < b; i++) {
				if (axis->defs[i].min_type != GS_FIXED) {
					base[i] += extra;
					limit[i] = max(limit[i], base[i]);
				}
			}
		}
	}

	total = axis->gap * (n - 1);
	for (int i = 0; i < n; i++) {
		total += base[i];
	}

	if (avail >= 0) {
		free_space = avail - total;
		while (free_space > 0) {
			int growable = 0, share;
			for (int i = 0; i < n; i++) {
				if (axis->defs[i].max_type != GS_FR &&
				    base[i] < limit[i]) {
					growable++;
				}
			}
			if (growable == 0) {
				break;
			}
			share = max(free_space / growable, 1);
			for (int i = 0; i < n && free_space > 0; i++) {
				if (axis->defs[i].max_type != GS_FR &&
				    base[i] < limit[i]) {
					int g = min(share, limit[i] - base[i]);
					g = min(g, free_space);
					base[i] += g;
					free_space -= g;
				}
			}
		}
		if (fr_total > 0) {
			int leftover = avail - axis->gap * (n - 1);
			float denom = fr_total;
			bool changed = true;
			bool *frozen = calloc(n, sizeof(bool));
			for (int i = 0; i < n; i++) {
				if (axis->defs[i].max_type != GS_FR) {
					leftover -= base[i];
				}
			}
			while (changed && frozen != NULL) {
				int rest = leftover;
				changed = false;
				denom = 0;
				for (int i = 0; i < n; i++) {
					if (axis->defs[i].max_type == GS_FR) {
						if (frozen[i]) {
							rest -= base[i];
						} else {
							denom += axis->defs[i]
									 .max_value;
						}
					}
				}
				if (denom <= 0) {
					break;
				}
				for (int i = 0; i < n; i++) {
					float want;
					if (axis->defs[i].max_type != GS_FR ||
					    frozen[i]) {
						continue;
					}
					want = rest * axis->defs[i].max_value /
					       denom;
					if (want < base[i]) {
						frozen[i] = true;
						changed = true;
					}
				}
				if (!changed && rest > 0) {
					float acc = 0;
					int used = 0;
					for (int i = 0; i < n; i++) {
						if (axis->defs[i].max_type ==
							    GS_FR &&
						    !frozen[i]) {
							acc += rest *
							       axis->defs[i]
								       .max_value /
							       denom;
							base[i] = (int)(acc + 0.5f) -
								  used;
							used += base[i];
						}
					}
				}
			}
			free(frozen);
		} else {
			int stretchable = 0;
			total = axis->gap * (n - 1);
			for (int i = 0; i < n; i++) {
				total += base[i];
				if (axis->defs[i].max_type == GS_AUTO) {
					stretchable++;
				}
			}
			if (columns && stretchable > 0 && avail > total) {
				int extra = (avail - total) / stretchable;
				for (int i = 0; i < n; i++) {
					if (axis->defs[i].max_type == GS_AUTO) {
						base[i] += extra;
					}
				}
			}
		}
	} else {
		for (int i = 0; i < n; i++) {
			if (axis->defs[i].max_type != GS_FR) {
				base[i] = max(base[i], limit[i]);
			}
		}
	}

	axis->pos[0] = 0;
	for (int i = 0; i < n; i++) {
		axis->pos[i + 1] = axis->pos[i] + base[i] +
				   (i + 1 < n ? axis->gap : 0);
	}
	free(limit);
}

static int area_size(const struct grid_axis *axis, int a, int b)
{
	a = clamp_index(a, axis->count);
	b = clamp_index(b, axis->count);
	if (b <= a) {
		return 0;
	}
	return axis->pos[b] - axis->pos[a] - (b < axis->count ? axis->gap : 0);
}

static int grid_gap(struct grid_ctx *ctx, bool columns, int avail)
{
	const css_computed_style *style = ctx->grid->style;
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type;
	int px;

	if (lh__calc_px(ctx->unit_ctx,
			style,
			columns ? CSS_PROP_COLUMN_GAP : CSS_PROP_ROW_GAP,
			avail,
			&px)) {
		return px;
	}
	type = columns ? css_computed_column_gap(style, &length, &unit)
		       : css_computed_row_gap(style, &length, &unit);
	if (type != CSS_COLUMN_GAP_SET) {
		return 0;
	}
	if (unit == CSS_UNIT_PCT) {
		return avail > 0 ? FPCT_OF_INT_TOINT(length, avail) : 0;
	}
	return FIXTOINT(
		css_unit_len2device_px(style, ctx->unit_ctx, length, unit));
}

static void axis_init_auto(struct grid_ctx *ctx,
			   struct grid_axis *axis,
			   const char *text)
{
	axis->auto_def.min_type = GS_AUTO;
	axis->auto_def.max_type = GS_AUTO;
	if (text != NULL) {
		parse_track_size(ctx, text, &axis->auto_def);
	}
}

static void grid_ctx_free(struct grid_ctx *ctx)
{
	free(ctx->cols.defs);
	free(ctx->cols.names);
	free(ctx->cols.size);
	free(ctx->cols.pos);
	free(ctx->rows.defs);
	free(ctx->rows.names);
	free(ctx->rows.size);
	free(ctx->rows.pos);
	free(ctx->areas);
	free(ctx->items);
}

static bool box_is_out_of_flow(const struct box *b)
{
	return b->style != NULL &&
	       (css_computed_position(b->style) == CSS_POSITION_ABSOLUTE ||
		css_computed_position(b->style) == CSS_POSITION_FIXED);
}

static bool grid_ctx_setup(struct grid_ctx *ctx,
			   struct box *grid,
			   const html_content *content,
			   int avail)
{
	const css_computed_style *style = grid->style;
	const char *flow;
	int n = 0;

	memset(ctx, 0, sizeof(*ctx));
	ctx->unit_ctx = &content->unit_len_ctx;
	ctx->grid = grid;
	ctx->cols.gap = grid_gap(ctx, true, avail);
	ctx->rows.gap = grid_gap(ctx, false, -1);

	flow = style_string(css_computed_grid_auto_flow, style);
	ctx->column_flow = flow != NULL && strstr(flow, "column") != NULL;
	axis_init_auto(ctx,
		       &ctx->cols,
		       style_string(css_computed_grid_auto_columns, style));
	axis_init_auto(ctx,
		       &ctx->rows,
		       style_string(css_computed_grid_auto_rows, style));

	if (style_string(css_computed_grid_template_areas, style) != NULL &&
	    !parse_areas(ctx,
			 style_string(css_computed_grid_template_areas,
				      style))) {
		return false;
	}
	if (style_string(css_computed_grid_template_columns, style) != NULL &&
	    !parse_track_list(
		    ctx,
		    &ctx->cols,
		    style_string(css_computed_grid_template_columns, style),
		    avail)) {
		return false;
	}
	if (style_string(css_computed_grid_template_rows, style) != NULL &&
	    !parse_track_list(
		    ctx,
		    &ctx->rows,
		    style_string(css_computed_grid_template_rows, style),
		    -1)) {
		return false;
	}
	while (ctx->cols.count < ctx->area_cols) {
		if (!axis_add_track(&ctx->cols, &ctx->cols.auto_def)) {
			return false;
		}
	}
	while (ctx->rows.count < ctx->area_rows) {
		if (!axis_add_track(&ctx->rows, &ctx->rows.auto_def)) {
			return false;
		}
	}
	ctx->cols.explicit_count = ctx->cols.count;
	ctx->rows.explicit_count = ctx->rows.count;

	for (struct box *b = grid->children; b != NULL; b = b->next) {
		n++;
	}
	ctx->items = calloc(n > 0 ? n : 1, sizeof(*ctx->items));
	if (ctx->items == NULL) {
		return false;
	}
	for (struct box *b = grid->children; b != NULL; b = b->next) {
		struct grid_item *it;
		if (box_is_out_of_flow(b) || b->style == NULL) {
			continue;
		}
		it = &ctx->items[ctx->n_items++];
		it->box = b;
		item_placement(ctx, it);
	}
	if (!place_items(ctx)) {
		return false;
	}
	{
		int ncols = ctx->cols.count, nrows = ctx->rows.count;
		for (int i = 0; i < ctx->n_items; i++) {
			ncols = max(ncols, ctx->items[i].c1);
			nrows = max(nrows, ctx->items[i].r1);
		}
		if (ncols < 1) {
			ncols = 1;
		}
		return axis_resolve_count(&ctx->cols, ncols) &&
		       axis_resolve_count(&ctx->rows, nrows);
	}
}

bool layout_grid_is_grid(const struct box *box)
{
	uint8_t display;

	if (box->style == NULL ||
	    (box->type != BOX_FLEX && box->type != BOX_INLINE_FLEX)) {
		return false;
	}
	display = css_computed_display(box->style, false);
	return display == CSS_DISPLAY_GRID || display == CSS_DISPLAY_INLINE_GRID;
}

void layout_grid_minmax(struct box *grid,
			const html_content *content,
			int *min,
			int *max)
{
	struct grid_ctx ctx;
	int lo = 0, hi = 0;

	if (!grid_ctx_setup(&ctx, grid, content, -1)) {
		grid_ctx_free(&ctx);
		return;
	}
	size_tracks(&ctx.cols, ctx.items, ctx.n_items, true, -1);
	for (int i = 0; i < ctx.cols.count; i++) {
		const struct grid_track_def *d = &ctx.cols.defs[i];
		int track_min = d->min_type == GS_FIXED ? (int)d->min_value : 0;
		for (int k = 0; k < ctx.n_items; k++) {
			if (ctx.items[k].c0 == i && ctx.items[k].c1 == i + 1) {
				track_min = max(track_min,
						ctx.items[k].box->min_width);
			}
		}
		lo += track_min;
		hi += ctx.cols.size[i];
	}
	if (ctx.cols.count > 1) {
		lo += ctx.cols.gap * (ctx.cols.count - 1);
		hi += ctx.cols.gap * (ctx.cols.count - 1);
	}
	*min = lo;
	*max = max(hi, lo);
	grid_ctx_free(&ctx);
}

static bool layout_grid_item(struct grid_ctx *ctx, struct box *b, int width)
{
	switch (b->type) {
	case BOX_BLOCK:
		return layout_block_context(b, -1, ctx->content);
	case BOX_TABLE:
		{
			bool ok;
			b->float_container = b->parent;
			ok = layout_table(b, width, ctx->content);
			b->float_container = NULL;
			return ok;
		}
	case BOX_FLEX:
		{
			bool ok;
			b->float_container = b->parent;
			ok = layout_flex(b, width, ctx->content);
			b->float_container = NULL;
			return ok;
		}
	default:
		return true;
	}
}

bool layout_grid(struct box *grid, int available_width, html_content *content)
{
	struct grid_ctx ctx;
	int max_height, min_height;
	bool ok = false;

	layout_find_dimensions(&content->unit_len_ctx,
			       available_width,
			       -1,
			       grid,
			       grid->style,
			       NULL,
			       &grid->height,
			       NULL,
			       NULL,
			       &max_height,
			       &min_height,
			       grid->margin,
			       grid->padding,
			       grid->border);
	available_width = min(available_width, grid->width);

	if (!grid_ctx_setup(&ctx, grid, content, available_width)) {
		goto done;
	}
	ctx.content = content;
	size_tracks(&ctx.cols, ctx.items, ctx.n_items, true, available_width);

	for (int i = 0; i < ctx.n_items; i++) {
		struct grid_item *it = &ctx.items[i];
		struct box *b = it->box;
		int area_w = area_size(&ctx.cols, it->c0, it->c1);
		int max_w, min_w, max_h, min_h;

		b->float_container = grid;
		layout_find_dimensions(&content->unit_len_ctx,
				       area_w,
				       -1,
				       b,
				       b->style,
				       &b->width,
				       &b->height,
				       &max_w,
				       &min_w,
				       &max_h,
				       &min_h,
				       b->margin,
				       b->padding,
				       b->border);
		b->float_container = NULL;
		if (b->width == AUTO) {
			b->width = area_w - lh__delta_outer_width(b);
		}
		if (max_w >= 0 && b->width > max_w) {
			b->width = max_w;
		}
		if (min_w > 0 && b->width < min_w) {
			b->width = min_w;
		}
		if (b->width < 0) {
			b->width = 0;
		}
		if (!layout_grid_item(&ctx, b, area_w)) {
			goto done;
		}
		if (b->height == AUTO) {
			b->height = 0;
		}
		if (max_h >= 0 && b->height > max_h) {
			b->height = max_h;
		}
		if (min_h > 0 && b->height < min_h) {
			b->height = min_h;
		}
		it->outer_height = b->height + lh__delta_outer_height(b);
	}

	size_tracks(&ctx.rows,
		    ctx.items,
		    ctx.n_items,
		    false,
		    grid->height != AUTO ? grid->height : -1);

	for (int i = 0; i < ctx.n_items; i++) {
		struct grid_item *it = &ctx.items[i];
		struct box *b = it->box;
		int area_h = area_size(&ctx.rows, it->r0, it->r1);
		int free_h = area_h - it->outer_height;
		int offset = 0;
		css_fixed hval = 0;
		css_unit hunit = CSS_UNIT_PX;

		switch (lh__box_align_self(grid, b)) {
		case CSS_ALIGN_SELF_FLEX_END:
			offset = free_h;
			break;
		case CSS_ALIGN_SELF_CENTER:
			offset = free_h / 2;
			break;
		case CSS_ALIGN_SELF_FLEX_START:
		case CSS_ALIGN_SELF_BASELINE:
			break;
		default:
			if (free_h > 0 &&
			    css_computed_height(b->style, &hval, &hunit) ==
				    CSS_HEIGHT_AUTO) {
				b->height += free_h;
			}
			break;
		}
		if (offset < 0) {
			offset = 0;
		}
		b->x = grid->padding[LEFT] +
		       ctx.cols.pos[clamp_index(it->c0, ctx.cols.count)] +
		       lh__non_auto_margin(b, LEFT) + b->border[LEFT].width;
		b->y = grid->padding[TOP] +
		       ctx.rows.pos[clamp_index(it->r0, ctx.rows.count)] +
		       offset + lh__non_auto_margin(b, TOP) +
		       b->border[TOP].width;
	}

	if (grid->height == AUTO) {
		grid->height = ctx.rows.count > 0
				       ? ctx.rows.pos[ctx.rows.count]
				       : 0;
	}
	if (max_height >= 0 && grid->height > max_height) {
		grid->height = max_height;
	}
	if (min_height > 0 && grid->height < min_height) {
		grid->height = min_height;
	}
	ok = true;

done:
	grid_ctx_free(&ctx);
	return ok;
}
