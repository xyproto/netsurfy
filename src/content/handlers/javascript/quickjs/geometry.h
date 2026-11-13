/*
 * This file is part of NetSurfy, https://github.com/xyproto/netsurfy
 *
 * Released under the terms of the MIT License,
 *         http://www.opensource.org/licenses/mit-license
 */

#ifndef NETSURFY_JAVASCRIPT_QUICKJS_GEOMETRY_H
#define NETSURFY_JAVASCRIPT_QUICKJS_GEOMETRY_H

#include <stdbool.h>
#include <quickjs.h>

typedef struct qjsky_matrix {
	double m[4][4];
	bool is2d;
} qjsky_matrix;

void qjsky_matrix_identity(qjsky_matrix *mat);
void qjsky_matrix_multiply(qjsky_matrix *out,
			   const qjsky_matrix *a,
			   const qjsky_matrix *b);
bool qjsky_matrix_invert(qjsky_matrix *out, const qjsky_matrix *in);
void qjsky_matrix_transform(const qjsky_matrix *mat,
			    const double in[4],
			    double out[4]);
void qjsky_matrix_translate(qjsky_matrix *mat, double tx, double ty, double tz);
void qjsky_matrix_scale(qjsky_matrix *mat,
			double sx,
			double sy,
			double sz,
			double ox,
			double oy,
			double oz);
double *qjsky_matrix_entry(qjsky_matrix *mat, int i, int j);
bool qjsky_matrix_is_identity(const qjsky_matrix *mat);
int qjsky_matrix_from_js(JSContext *ctx, JSValueConst val, qjsky_matrix *out);
int qjsky_matrix_from_init(JSContext *ctx,
			   JSValueConst init,
			   qjsky_matrix *out);
JSValue qjsky_matrix_to_string(JSContext *ctx, const qjsky_matrix *mat);
int qjsky_geometry_args(JSContext *ctx,
			int argc,
			JSValueConst *argv,
			double *v,
			int n);
int qjsky_geometry_point_from_js(JSContext *ctx, JSValueConst val, double v[4]);
JSValue qjsky_geometry_json(JSContext *ctx,
			    const char *const *names,
			    const double *v,
			    int n);
JSValue qjsky_matrix_scale_args(JSContext *ctx,
				int argc,
				JSValueConst *argv,
				qjsky_matrix *mat);
JSValue qjsky_matrix_translate_args(JSContext *ctx,
				    int argc,
				    JSValueConst *argv,
				    qjsky_matrix *mat);

#endif
