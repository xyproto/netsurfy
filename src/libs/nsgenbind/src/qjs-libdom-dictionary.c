/* QuickJS binding generation implementation
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>

#include "options.h"
#include "utils.h"
#include "nsgenbind-ast.h"
#include "webidl-ast.h"
#include "ir.h"
#include "output.h"
#include "qjs-libdom.h"

#define DLPFX "qjsky"


static int get_member_default_str(struct ir_entry *dictionarye,
				  struct ir_operation_argument_entry *membere,
				  enum webidl_type member_type,
				  char **defl_out)
{
	struct webidl_node *lit_node;
	enum webidl_node_type lit_type;
	int *lit_int;
	float *lit_flt;

	lit_node = webidl_node_getnode(
		webidl_node_find_type(webidl_node_getnode(membere->node),
				      NULL,
				      WEBIDL_NODE_TYPE_OPTIONAL));
	if (lit_node == NULL) {
		*defl_out = NULL;
		return 0;
	}

	lit_type = webidl_node_gettype(lit_node);

	switch (lit_type) {

	case WEBIDL_NODE_TYPE_LITERAL_BOOL:
		if (member_type != WEBIDL_TYPE_BOOL) {
			fprintf(stderr,
				"Dictionary %s:%s literal boolean type mismatch\n",
				dictionarye->name,
				membere->name);
			return -1;
		}
		lit_int = webidl_node_getint(lit_node);
		if (*lit_int == 0) {
			*defl_out = strdup("false");
		} else {
			*defl_out = strdup("true");
		}
		break;

	case WEBIDL_NODE_TYPE_LITERAL_NULL:
		*defl_out = strdup("NULL");
		break;

	case WEBIDL_NODE_TYPE_LITERAL_STRING:
		*defl_out = strdup(webidl_node_gettext(lit_node));
		break;

	case WEBIDL_NODE_TYPE_LITERAL_INT:
		lit_int = webidl_node_getint(lit_node);
		*defl_out = malloc(128);
		snprintf(*defl_out, 128, "%d", *lit_int);
		break;

	case WEBIDL_NODE_TYPE_LITERAL_FLOAT:
		lit_flt = webidl_node_getfloat(lit_node);
		*defl_out = malloc(128);
		snprintf(*defl_out, 128, "%f", *lit_flt);
		break;

	default:
		*defl_out = NULL;
		break;
	}

	return 0;
}


/**
 * generate a single class method for an interface operation
 */

static int output_member_acessor(struct opctx *outc,
				 struct ir_entry *dictionarye,
				 struct ir_operation_argument_entry *membere)
{
	struct webidl_node *type_node;
	enum webidl_type *argument_type;
	char *defl;
	int res;

	type_node = webidl_node_find_type(webidl_node_getnode(membere->node),
					  NULL,
					  WEBIDL_NODE_TYPE_TYPE);

	if (type_node == NULL) {
		fprintf(stderr,
			"%s:%s has no type\n",
			dictionarye->name,
			membere->name);
		return -1;
	}

	argument_type = (enum webidl_type *)webidl_node_getint(
		webidl_node_find_type(webidl_node_getnode(type_node),
				      NULL,
				      WEBIDL_NODE_TYPE_TYPE_BASE));

	if (argument_type == NULL) {
		fprintf(stderr,
			"%s:%s has no type base\n",
			dictionarye->name,
			membere->name);
		return -1;
	}

	res = get_member_default_str(
		dictionarye, membere, *argument_type, &defl);
	if (res != 0) {
		return res;
	}

	switch (*argument_type) {

	case WEBIDL_TYPE_STRING:
		outputf(outc,
			"char *\n"
			"%s_%s_get_%s(JSContext *ctx, JSValueConst obj)\n"
			"{\n"
			"\tJSValue val = JS_GetPropertyStr(ctx, obj, \"%s\");\n"
			"\tconst char *s;\n"
			"\tchar *ret;\n\n"
			"\tif (JS_IsUndefined(val) || JS_IsException(val)) {\n"
			"\t\tJS_FreeValue(ctx, val);\n",
			DLPFX,
			dictionarye->class_name,
			membere->name,
			membere->name);
		if ((defl == NULL) || (strcmp(defl, "NULL") == 0)) {
			outputf(outc, "\t\treturn NULL;\n");
		} else {
			outputf(outc, "\t\treturn strdup(\"%s\");\n", defl);
		}
		outputf(outc,
			"\t}\n"
			"\ts = JS_ToCString(ctx, val);\n"
			"\tJS_FreeValue(ctx, val);\n"
			"\tif (s == NULL) {\n"
			"\t\treturn NULL;\n"
			"\t}\n"
			"\tret = strdup(s);\n"
			"\tJS_FreeCString(ctx, s);\n"
			"\treturn ret;\n"
			"}\n\n");
		break;

	case WEBIDL_TYPE_BOOL:
		outputf(outc,
			"bool\n"
			"%s_%s_get_%s(JSContext *ctx, JSValueConst obj)\n"
			"{\n"
			"\tJSValue val = JS_GetPropertyStr(ctx, obj, \"%s\");\n"
			"\tbool ret = %s;\n\n"
			"\tif (!JS_IsUndefined(val) && !JS_IsException(val)) {\n"
			"\t\tret = JS_ToBool(ctx, val) > 0;\n"
			"\t}\n"
			"\tJS_FreeValue(ctx, val);\n"
			"\treturn ret;\n"
			"}\n\n",
			DLPFX,
			dictionarye->class_name,
			membere->name,
			membere->name,
			defl == NULL ? "false" : defl);
		break;

	case WEBIDL_TYPE_SHORT:
	case WEBIDL_TYPE_LONG:
	case WEBIDL_TYPE_LONGLONG:
		outputf(outc,
			"int32_t\n"
			"%s_%s_get_%s(JSContext *ctx, JSValueConst obj)\n"
			"{\n"
			"\tJSValue val = JS_GetPropertyStr(ctx, obj, \"%s\");\n"
			"\tint32_t ret = %s;\n\n"
			"\tif (!JS_IsUndefined(val) && !JS_IsException(val)) {\n"
			"\t\t(void)JS_ToInt32(ctx, &ret, val);\n"
			"\t}\n"
			"\tJS_FreeValue(ctx, val);\n"
			"\treturn ret;\n"
			"}\n\n",
			DLPFX,
			dictionarye->class_name,
			membere->name,
			membere->name,
			defl == NULL ? "0" : defl);
		break;

	case WEBIDL_TYPE_FLOAT:
	case WEBIDL_TYPE_DOUBLE:
		outputf(outc,
			"double\n"
			"%s_%s_get_%s(JSContext *ctx, JSValueConst obj)\n"
			"{\n"
			"\tJSValue val = JS_GetPropertyStr(ctx, obj, \"%s\");\n"
			"\tdouble ret = %s;\n\n"
			"\tif (!JS_IsUndefined(val) && !JS_IsException(val)) {\n"
			"\t\t(void)JS_ToFloat64(ctx, &ret, val);\n"
			"\t}\n"
			"\tJS_FreeValue(ctx, val);\n"
			"\treturn ret;\n"
			"}\n\n",
			DLPFX,
			dictionarye->class_name,
			membere->name,
			membere->name,
			defl == NULL ? "0" : defl);
		break;

	default:
		WARN(WARNING_UNIMPLEMENTED,
		     "Dictionary %s:%s unhandled type (%d)",
		     dictionarye->name,
		     membere->name,
		     *argument_type);
		outputf(outc,
			"/* Dictionary %s:%s unhandled type (%d) */\n\n",
			dictionarye->name,
			membere->name,
			*argument_type);
	}

	if (defl != NULL) {
		free(defl);
	}

	return 0;
}

static int
output_member_acessors(struct opctx *outc, struct ir_entry *dictionarye)
{
	int memberc;
	int res = 0;

	for (memberc = 0; memberc < dictionarye->u.dictionary.memberc;
	     memberc++) {
		res = output_member_acessor(outc,
					    dictionarye,
					    dictionarye->u.dictionary.memberv +
						    memberc);
		if (res != 0) {
			break;
		}
	}

	return res;
}


int output_dictionary(struct ir *ir, struct ir_entry *dictionarye)
{
	struct opctx *dyop;
	int res = 0;

	/* open the output */
	res = output_open(dictionarye->filename, &dyop);
	if (res != 0) {
		return res;
	}

	/* tool preface */
	output_tool_preface(dyop);

	/* binding preface */
	output_method_cdata(dyop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PREFACE);

	/* class preface */
	output_method_cdata(dyop,
			    dictionarye->class,
			    GENBIND_METHOD_TYPE_PREFACE);

	/* tool prologue */
	output_tool_prologue(dyop);

	/* binding prologue */
	output_method_cdata(dyop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PROLOGUE);

	/* class prologue */
	output_method_cdata(dyop,
			    dictionarye->class,
			    GENBIND_METHOD_TYPE_PROLOGUE);

	outputf(dyop, "\n");

	res = output_member_acessors(dyop, dictionarye);
	if (res != 0) {
		goto op_error;
	}

	outputf(dyop, "\n");

	/* class epilogue */
	output_method_cdata(dyop,
			    dictionarye->class,
			    GENBIND_METHOD_TYPE_EPILOGUE);

	/* binding epilogue */
	output_method_cdata(dyop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_EPILOGUE);

	/* class postface */
	output_method_cdata(dyop,
			    dictionarye->class,
			    GENBIND_METHOD_TYPE_POSTFACE);

	/* binding postface */
	output_method_cdata(dyop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_POSTFACE);

op_error:
	output_close(dyop);

	return res;
}

/**
 * generate a single class method declaration for an interface operation
 */
static int
output_member_declaration(struct opctx *outc,
			  struct ir_entry *dictionarye,
			  struct ir_operation_argument_entry *membere)
{
	struct webidl_node *type_node;
	enum webidl_type *argument_type;

	type_node = webidl_node_find_type(webidl_node_getnode(membere->node),
					  NULL,
					  WEBIDL_NODE_TYPE_TYPE);

	if (type_node == NULL) {
		fprintf(stderr,
			"%s:%s has no type\n",
			dictionarye->name,
			membere->name);
		return -1;
	}

	argument_type = (enum webidl_type *)webidl_node_getint(
		webidl_node_find_type(webidl_node_getnode(type_node),
				      NULL,
				      WEBIDL_NODE_TYPE_TYPE_BASE));

	if (argument_type == NULL) {
		fprintf(stderr,
			"%s:%s has no type base\n",
			dictionarye->name,
			membere->name);
		return -1;
	}


	switch (*argument_type) {

	case WEBIDL_TYPE_STRING:
		outputf(outc,
			"char *%s_%s_get_%s(JSContext *ctx, JSValueConst obj);\n",
			DLPFX,
			dictionarye->class_name,
			membere->name);
		break;

	case WEBIDL_TYPE_BOOL:
		outputf(outc,
			"bool %s_%s_get_%s(JSContext *ctx, JSValueConst obj);\n",
			DLPFX,
			dictionarye->class_name,
			membere->name);
		break;

	case WEBIDL_TYPE_SHORT:
	case WEBIDL_TYPE_LONG:
	case WEBIDL_TYPE_LONGLONG:
		outputf(outc,
			"int32_t %s_%s_get_%s(JSContext *ctx, JSValueConst obj);\n",
			DLPFX,
			dictionarye->class_name,
			membere->name);
		break;

	case WEBIDL_TYPE_FLOAT:
	case WEBIDL_TYPE_DOUBLE:
		outputf(outc,
			"double %s_%s_get_%s(JSContext *ctx, JSValueConst obj);\n",
			DLPFX,
			dictionarye->class_name,
			membere->name);
		break;

	default:
		outputf(outc,
			"/* Dictionary %s:%s unhandled type (%d) */\n",
			dictionarye->name,
			membere->name,
			*argument_type);
	}

	return 0;
}

int output_dictionary_declaration(struct opctx *outc,
				  struct ir_entry *dictionarye)
{
	int memberc;
	int res = 0;

	for (memberc = 0; memberc < dictionarye->u.dictionary.memberc;
	     memberc++) {
		res = output_member_declaration(
			outc,
			dictionarye,
			dictionarye->u.dictionary.memberv + memberc);
		if (res != 0) {
			break;
		}
	}

	return res;
}
