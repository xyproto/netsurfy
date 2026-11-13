/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "javascript/quickjs/geometry.h"

void qjsky_matrix_identity(qjsky_matrix *mat)
{
	memset(mat, 0, sizeof(*mat));
	for (int i = 0; i < 4; i++) {
		mat->m[i][i] = 1;
	}
	mat->is2d = true;
}

double *qjsky_matrix_entry(qjsky_matrix *mat, int i, int j)
{
	return &mat->m[j - 1][i - 1];
}

void qjsky_matrix_multiply(qjsky_matrix *out,
			   const qjsky_matrix *a,
			   const qjsky_matrix *b)
{
	qjsky_matrix r;

	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			double sum = 0;
			for (int k = 0; k < 4; k++) {
				sum += a->m[row][k] * b->m[k][col];
			}
			r.m[row][col] = sum;
		}
	}
	r.is2d = a->is2d && b->is2d;
	*out = r;
}

bool qjsky_matrix_invert(qjsky_matrix *out, const qjsky_matrix *in)
{
	double a[4][8];
	qjsky_matrix r;

	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			a[row][col] = in->m[row][col];
			a[row][col + 4] = row == col ? 1 : 0;
		}
	}
	for (int col = 0; col < 4; col++) {
		int pivot = col;
		for (int row = col + 1; row < 4; row++) {
			if (fabs(a[row][col]) > fabs(a[pivot][col])) {
				pivot = row;
			}
		}
		if (fabs(a[pivot][col]) < 1e-12) {
			for (int row = 0; row < 4; row++) {
				for (int c = 0; c < 4; c++) {
					out->m[row][c] = NAN;
				}
			}
			out->is2d = false;
			return false;
		}
		if (pivot != col) {
			for (int c = 0; c < 8; c++) {
				double t = a[col][c];
				a[col][c] = a[pivot][c];
				a[pivot][c] = t;
			}
		}
		for (int c = 7; c >= col; c--) {
			a[col][c] /= a[col][col];
		}
		for (int row = 0; row < 4; row++) {
			if (row != col && a[row][col] != 0) {
				double f = a[row][col];
				for (int c = col; c < 8; c++) {
					a[row][c] -= f * a[col][c];
				}
			}
		}
	}
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			r.m[row][col] = a[row][col + 4];
		}
	}
	r.is2d = in->is2d;
	*out = r;
	return true;
}

void qjsky_matrix_transform(const qjsky_matrix *mat,
			    const double in[4],
			    double out[4])
{
	for (int row = 0; row < 4; row++) {
		out[row] = mat->m[row][0] * in[0] + mat->m[row][1] * in[1] +
			   mat->m[row][2] * in[2] + mat->m[row][3] * in[3];
	}
}

void qjsky_matrix_translate(qjsky_matrix *mat, double tx, double ty, double tz)
{
	qjsky_matrix t;

	qjsky_matrix_identity(&t);
	t.m[0][3] = tx;
	t.m[1][3] = ty;
	t.m[2][3] = tz;
	t.is2d = tz == 0;
	qjsky_matrix_multiply(mat, mat, &t);
}

void qjsky_matrix_scale(qjsky_matrix *mat,
			double sx,
			double sy,
			double sz,
			double ox,
			double oy,
			double oz)
{
	qjsky_matrix s;

	qjsky_matrix_translate(mat, ox, oy, oz);
	qjsky_matrix_identity(&s);
	s.m[0][0] = sx;
	s.m[1][1] = sy;
	s.m[2][2] = sz;
	s.is2d = sz == 1;
	qjsky_matrix_multiply(mat, mat, &s);
	qjsky_matrix_translate(mat, -ox, -oy, -oz);
}

bool qjsky_matrix_is_identity(const qjsky_matrix *mat)
{
	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			if (mat->m[row][col] != (row == col ? 1 : 0)) {
				return false;
			}
		}
	}
	return true;
}

static int matrix_field(JSContext *ctx,
			JSValueConst val,
			const char *name,
			double *out,
			bool *present)
{
	JSValue v = JS_GetPropertyStr(ctx, val, name);
	int r = 0;

	if (JS_IsException(v)) {
		return -1;
	}
	*present = !JS_IsUndefined(v);
	if (*present) {
		r = JS_ToFloat64(ctx, out, v);
	}
	JS_FreeValue(ctx, v);
	return r;
}

int qjsky_matrix_from_js(JSContext *ctx, JSValueConst val, qjsky_matrix *out)
{
	static const struct {
		const char *alias;
		const char *name;
		int i, j;
	} fields[] = {
		{"a", "m11", 1, 1},
		{"b", "m12", 1, 2},
		{"c", "m21", 2, 1},
		{"d", "m22", 2, 2},
		{"e", "m41", 4, 1},
		{"f", "m42", 4, 2},
		{NULL, "m13", 1, 3},
		{NULL, "m14", 1, 4},
		{NULL, "m23", 2, 3},
		{NULL, "m24", 2, 4},
		{NULL, "m31", 3, 1},
		{NULL, "m32", 3, 2},
		{NULL, "m33", 3, 3},
		{NULL, "m34", 3, 4},
		{NULL, "m43", 4, 3},
		{NULL, "m44", 4, 4},
	};
	bool three_d = false;

	qjsky_matrix_identity(out);
	if (JS_IsUndefined(val) || JS_IsNull(val)) {
		return 0;
	}
	if (!JS_IsObject(val)) {
		JS_ThrowTypeError(ctx, "the matrix is not an object");
		return -1;
	}
	for (size_t k = 0; k < sizeof(fields) / sizeof(fields[0]); k++) {
		double v;
		bool present = false;

		if (matrix_field(ctx, val, fields[k].name, &v, &present) < 0) {
			return -1;
		}
		if (!present && fields[k].alias != NULL &&
		    matrix_field(ctx, val, fields[k].alias, &v, &present) < 0) {
			return -1;
		}
		if (present) {
			*qjsky_matrix_entry(out, fields[k].i, fields[k].j) = v;
			if (fields[k].alias == NULL &&
			    v != ((fields[k].i == fields[k].j) ? 1 : 0)) {
				three_d = true;
			}
		}
	}
	{
		JSValue is2d = JS_GetPropertyStr(ctx, val, "is2D");
		if (JS_IsException(is2d)) {
			return -1;
		}
		if (JS_IsBool(is2d) && !JS_ToBool(ctx, is2d)) {
			three_d = true;
		}
		JS_FreeValue(ctx, is2d);
	}
	out->is2d = !three_d;
	return 0;
}

JSValue qjsky_matrix_to_string(JSContext *ctx, const qjsky_matrix *mat)
{
	char buf[512];
	qjsky_matrix copy = *mat;

	if (mat->is2d) {
		snprintf(buf,
			 sizeof(buf),
			 "matrix(%g, %g, %g, %g, %g, %g)",
			 *qjsky_matrix_entry(&copy, 1, 1),
			 *qjsky_matrix_entry(&copy, 1, 2),
			 *qjsky_matrix_entry(&copy, 2, 1),
			 *qjsky_matrix_entry(&copy, 2, 2),
			 *qjsky_matrix_entry(&copy, 4, 1),
			 *qjsky_matrix_entry(&copy, 4, 2));
	} else {
		size_t o = (size_t)snprintf(buf, sizeof(buf), "matrix3d(");
		for (int i = 1; i <= 4; i++) {
			for (int j = 1; j <= 4; j++) {
				o += (size_t)snprintf(
					buf + o,
					sizeof(buf) - o,
					"%g%s",
					*qjsky_matrix_entry(&copy, i, j),
					(i == 4 && j == 4) ? ")" : ", ");
			}
		}
	}
	return JS_NewString(ctx, buf);
}

int qjsky_matrix_from_init(JSContext *ctx, JSValueConst init, qjsky_matrix *out)
{
	qjsky_matrix_identity(out);
	if (JS_IsUndefined(init)) {
		return 0;
	}
	if (JS_IsString(init)) {
		const char *str = JS_ToCString(ctx, init);
		double v[6];
		int r = 0;

		if (str == NULL) {
			return -1;
		}
		while (*str == ' ') {
			str++;
		}
		if (*str == '\0' || strcmp(str, "none") == 0) {
			r = 0;
		} else if (sscanf(str,
				  "matrix(%lf ,%lf ,%lf ,%lf ,%lf ,%lf )",
				  &v[0],
				  &v[1],
				  &v[2],
				  &v[3],
				  &v[4],
				  &v[5]) == 6) {
			*qjsky_matrix_entry(out, 1, 1) = v[0];
			*qjsky_matrix_entry(out, 1, 2) = v[1];
			*qjsky_matrix_entry(out, 2, 1) = v[2];
			*qjsky_matrix_entry(out, 2, 2) = v[3];
			*qjsky_matrix_entry(out, 4, 1) = v[4];
			*qjsky_matrix_entry(out, 4, 2) = v[5];
		} else {
			JS_ThrowSyntaxError(
				ctx,
				"Failed to parse '%s' as a transform list",
				str);
			r = -1;
		}
		JS_FreeCString(ctx, str);
		return r;
	}
	if (JS_IsArray(init)) {
		int64_t len = 0;
		double v[16];

		if (JS_GetLength(ctx, init, &len) < 0) {
			return -1;
		}
		if (len != 6 && len != 16) {
			JS_ThrowTypeError(
				ctx,
				"the matrix sequence must have 6 or 16 elements");
			return -1;
		}
		for (int64_t k = 0; k < len; k++) {
			JSValue e = JS_GetPropertyInt64(ctx, init, k);
			int r = JS_IsException(e) ? -1
						  : JS_ToFloat64(ctx, &v[k], e);
			JS_FreeValue(ctx, e);
			if (r < 0) {
				return -1;
			}
		}
		if (len == 6) {
			*qjsky_matrix_entry(out, 1, 1) = v[0];
			*qjsky_matrix_entry(out, 1, 2) = v[1];
			*qjsky_matrix_entry(out, 2, 1) = v[2];
			*qjsky_matrix_entry(out, 2, 2) = v[3];
			*qjsky_matrix_entry(out, 4, 1) = v[4];
			*qjsky_matrix_entry(out, 4, 2) = v[5];
		} else {
			for (int i = 1; i <= 4; i++) {
				for (int j = 1; j <= 4; j++) {
					*qjsky_matrix_entry(out, i, j) =
						v[(i - 1) * 4 + (j - 1)];
				}
			}
			out->is2d = false;
		}
		return 0;
	}
	JS_ThrowTypeError(
		ctx,
		"the matrix init must be a string or a sequence of numbers");
	return -1;
}

int qjsky_geometry_args(JSContext *ctx,
			int argc,
			JSValueConst *argv,
			double *v,
			int n)
{
	for (int i = 0; i < n && i < argc; i++) {
		if (!JS_IsUndefined(argv[i]) &&
		    JS_ToFloat64(ctx, &v[i], argv[i]) < 0) {
			return -1;
		}
	}
	return 0;
}

int qjsky_geometry_point_from_js(JSContext *ctx, JSValueConst val, double v[4])
{
	static const char *const names[4] = {"x", "y", "z", "w"};

	v[0] = v[1] = v[2] = 0;
	v[3] = 1;
	if (JS_IsUndefined(val) || JS_IsNull(val)) {
		return 0;
	}
	if (!JS_IsObject(val)) {
		JS_ThrowTypeError(ctx, "the point is not an object");
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		JSValue e = JS_GetPropertyStr(ctx, val, names[i]);
		int r = 0;
		if (JS_IsException(e)) {
			return -1;
		}
		if (!JS_IsUndefined(e)) {
			r = JS_ToFloat64(ctx, &v[i], e);
		}
		JS_FreeValue(ctx, e);
		if (r < 0) {
			return -1;
		}
	}
	return 0;
}

JSValue qjsky_geometry_json(JSContext *ctx,
			    const char *const *names,
			    const double *v,
			    int n)
{
	JSValue obj = JS_NewObject(ctx);

	if (JS_IsException(obj)) {
		return obj;
	}
	for (int i = 0; i < n; i++) {
		if (JS_SetPropertyStr(
			    ctx, obj, names[i], JS_NewFloat64(ctx, v[i])) < 0) {
			JS_FreeValue(ctx, obj);
			return JS_EXCEPTION;
		}
	}
	return obj;
}

JSValue qjsky_matrix_scale_args(JSContext *ctx,
				int argc,
				JSValueConst *argv,
				qjsky_matrix *mat)
{
	double v[6] = {1, NAN, 1, 0, 0, 0};

	if (qjsky_geometry_args(ctx, argc, argv, v, 6) < 0) {
		return JS_EXCEPTION;
	}
	if (isnan(v[1])) {
		v[1] = v[0];
	}
	qjsky_matrix_scale(mat, v[0], v[1], v[2], v[3], v[4], v[5]);
	return JS_UNDEFINED;
}

JSValue qjsky_matrix_translate_args(JSContext *ctx,
				    int argc,
				    JSValueConst *argv,
				    qjsky_matrix *mat)
{
	double v[3] = {0, 0, 0};

	if (qjsky_geometry_args(ctx, argc, argv, v, 3) < 0) {
		return JS_EXCEPTION;
	}
	qjsky_matrix_translate(mat, v[0], v[1], v[2]);
	return JS_UNDEFINED;
}
