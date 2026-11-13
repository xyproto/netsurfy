/* QuickJS and libdom binding generation implementation
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2015 Vincent Sanders <vince@netsurf-browser.org>
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

static bool is_event_handler(struct ir_attribute_entry *atributee)
{
	return (atributee->typev[0].name != NULL) &&
	       strlen(atributee->typev[0].name) >= 12 &&
	       strcmp(atributee->typev[0].name +
			      strlen(atributee->typev[0].name) - 12,
		      "EventHandler") == 0 &&
	       atributee->name[0] == 'o' && atributee->name[1] == 'n';
}

static int
output_generated_attribute_user_getter(struct opctx *outc,
				       struct ir_entry *interfacee,
				       struct ir_attribute_entry *atributee)
{
	if (!is_event_handler(atributee)) {
		return -1;
	}

	if (interfacee->u.interface.primary_global) {
		outputf(outc, "\tdom_event_target *et = NULL;\n");
	} else {
		outputf(outc,
			"\tdom_event_target *et = (dom_event_target *)(((node_private_t *)priv)->node);\n");
	}
	outputf(outc,
		"\tdom_string *name;\n"
		"\tdom_exception exc;\n"
		"\tJSValue handler;\n\n"
		"\texc = dom_string_create((const uint8_t *)\"%s\", %ld, &name);\n"
		"\tif (exc != DOM_NO_ERR) {\n"
		"\t\treturn JS_UNDEFINED;\n"
		"\t}\n"
		"\thandler = qjsky_get_event_handler(ctx, this_val, name, et);\n"
		"\tdom_string_unref(name);\n"
		"\treturn handler;\n",
		atributee->name + 2,
		strlen(atributee->name + 2));
	return 0;
}

static int output_node_cast(struct opctx *outc, struct ir_entry *interfacee)
{
	outputf(outc,
		"(struct dom_%s *)((node_private_t *)priv)->node",
		interfacee->class_name);
	return 0;
}

int output_generated_attribute_getter(struct opctx *outc,
				      struct ir_entry *interfacee,
				      struct ir_attribute_entry *atributee)
{
	int res = 0;

	if (atributee->typec != 1) {
		return -1;
	}

	switch (atributee->typev[0].base) {
	case WEBIDL_TYPE_STRING:
		outputf(outc,
			"\tdom_exception exc;\n"
			"\tdom_string *str;\n"
			"\tJSValue ret;\n"
			"\n"
			"\texc = dom_%s_get_%s(",
			interfacee->class_name,
			atributee->property_name);
		output_node_cast(outc, interfacee);
		outputf(outc,
			", &str);\n"
			"\tif (exc != DOM_NO_ERR) {\n"
			"\t\treturn JS_UNDEFINED;\n"
			"\t}\n"
			"\tif (str == NULL) {\n"
			"\t\treturn %s;\n"
			"\t}\n"
			"\tret = JS_NewStringLen(ctx, dom_string_data(str), dom_string_length(str));\n"
			"\tdom_string_unref(str);\n"
			"\treturn ret;\n",
			atributee->typev[0].nullable
				? "JS_NULL"
				: "JS_NewString(ctx, \"\")");
		break;

	case WEBIDL_TYPE_LONG:
	case WEBIDL_TYPE_SHORT:
		if (atributee->typev[0].base == WEBIDL_TYPE_LONG) {
			outputf(outc,
				atributee->typev[0].modifier ==
						WEBIDL_TYPE_MODIFIER_UNSIGNED
					? "\tdom_ulong n;\n"
					: "\tdom_long n;\n");
		} else {
			outputf(outc,
				atributee->typev[0].modifier ==
						WEBIDL_TYPE_MODIFIER_UNSIGNED
					? "\tdom_ushort n;\n"
					: "\tdom_short n;\n");
		}
		outputf(outc,
			"\tdom_exception exc;\n"
			"\n"
			"\texc = dom_%s_get_%s(",
			interfacee->class_name,
			atributee->property_name);
		output_node_cast(outc, interfacee);
		outputf(outc,
			", &n);\n"
			"\tif (exc != DOM_NO_ERR) {\n"
			"\t\treturn JS_UNDEFINED;\n"
			"\t}\n"
			"\treturn JS_NewInt64(ctx, (int64_t)n);\n");
		break;

	case WEBIDL_TYPE_BOOL:
		outputf(outc,
			"\tdom_exception exc;\n"
			"\tbool b;\n"
			"\n"
			"\texc = dom_%s_get_%s(",
			interfacee->class_name,
			atributee->property_name);
		output_node_cast(outc, interfacee);
		outputf(outc,
			", &b);\n"
			"\tif (exc != DOM_NO_ERR) {\n"
			"\t\treturn JS_UNDEFINED;\n"
			"\t}\n"
			"\treturn JS_NewBool(ctx, b);\n");
		break;

	case WEBIDL_TYPE_USER:
		res = output_generated_attribute_user_getter(outc,
							     interfacee,
							     atributee);
		break;

	default:
		res = -1;
		break;
	}

	if (res >= 0) {
		WARN(WARNING_GENERATED,
		     "Generated: getter %s::%s();",
		     interfacee->name,
		     atributee->name);
	}

	return res;
}

static int
output_generated_attribute_user_setter(struct opctx *outc,
				       struct ir_entry *interfacee,
				       struct ir_attribute_entry *atributee)
{
	if (!is_event_handler(atributee)) {
		return -1;
	}

	if (interfacee->u.interface.primary_global) {
		outputf(outc, "\tdom_element *et = NULL;\n");
	} else {
		outputf(outc,
			"\tdom_element *et = (dom_element *)(((node_private_t *)priv)->node);\n");
	}

	outputf(outc,
		"\tif (qjsky_set_event_handler(ctx, this_val, \"%s\", argv[0]) < 0) {\n"
		"\t\treturn JS_EXCEPTION;\n"
		"\t}\n"
		"\tqjsky_register_event_listener_for(ctx, et, corestring_dom_%s, false);\n"
		"\treturn JS_UNDEFINED;\n",
		atributee->name + 2,
		atributee->name + 2);
	return 0;
}

int output_generated_attribute_setter(struct opctx *outc,
				      struct ir_entry *interfacee,
				      struct ir_attribute_entry *atributee)
{
	int res = 0;

	if (atributee->typec != 1) {
		return -1;
	}

	switch (atributee->typev[0].base) {
	case WEBIDL_TYPE_STRING:
		outputf(outc,
			"\tdom_exception exc;\n"
			"\tdom_string *str;\n"
			"\tsize_t slen = 0;\n"
			"\tconst char *s = NULL;\n\n");
		if ((atributee->treatnullas != NULL) &&
		    (strcmp(atributee->treatnullas, "EmptyString") == 0)) {
			outputf(outc,
				"\tif (!JS_IsNull(argv[0])) {\n"
				"\t\ts = JS_ToCStringLen(ctx, &slen, argv[0]);\n"
				"\t\tif (s == NULL) {\n"
				"\t\t\treturn JS_EXCEPTION;\n"
				"\t\t}\n"
				"\t}\n");
		} else {
			outputf(outc,
				"\ts = JS_ToCStringLen(ctx, &slen, argv[0]);\n"
				"\tif (s == NULL) {\n"
				"\t\treturn JS_EXCEPTION;\n"
				"\t}\n");
		}
		outputf(outc,
			"\texc = dom_string_create((const uint8_t *)(s != NULL ? s : \"\"), slen, &str);\n"
			"\tif (s != NULL) {\n"
			"\t\tJS_FreeCString(ctx, s);\n"
			"\t}\n"
			"\tif (exc != DOM_NO_ERR) {\n"
			"\t\treturn JS_UNDEFINED;\n"
			"\t}\n"
			"\texc = dom_%s_set_%s(",
			interfacee->class_name,
			atributee->property_name);
		output_node_cast(outc, interfacee);
		outputf(outc,
			", str);\n"
			"\tdom_string_unref(str);\n"
			"\treturn JS_UNDEFINED;\n");
		break;

	case WEBIDL_TYPE_LONG:
	case WEBIDL_TYPE_SHORT:
		outputf(outc,
			"\tint32_t n;\n"
			"\n"
			"\tif (JS_ToInt32(ctx, &n, argv[0]) < 0) {\n"
			"\t\treturn JS_EXCEPTION;\n"
			"\t}\n"
			"\t(void)dom_%s_set_%s(",
			interfacee->class_name,
			atributee->property_name);
		output_node_cast(outc, interfacee);
		if (atributee->typev[0].base == WEBIDL_TYPE_LONG) {
			outputf(outc,
				atributee->typev[0].modifier ==
						WEBIDL_TYPE_MODIFIER_UNSIGNED
					? ", (dom_ulong)n);\n"
					: ", (dom_long)n);\n");
		} else {
			outputf(outc,
				atributee->typev[0].modifier ==
						WEBIDL_TYPE_MODIFIER_UNSIGNED
					? ", (dom_ushort)n);\n"
					: ", (dom_short)n);\n");
		}
		outputf(outc, "\treturn JS_UNDEFINED;\n");
		break;

	case WEBIDL_TYPE_BOOL:
		outputf(outc,
			"\tint b = JS_ToBool(ctx, argv[0]);\n"
			"\n"
			"\tif (b < 0) {\n"
			"\t\treturn JS_EXCEPTION;\n"
			"\t}\n"
			"\t(void)dom_%s_set_%s(",
			interfacee->class_name,
			atributee->property_name);
		output_node_cast(outc, interfacee);
		outputf(outc,
			", b != 0);\n"
			"\treturn JS_UNDEFINED;\n");
		break;

	case WEBIDL_TYPE_USER:
		res = output_generated_attribute_user_setter(outc,
							     interfacee,
							     atributee);
		break;

	default:
		res = -1;
		break;
	}

	if (res >= 0) {
		WARN(WARNING_GENERATED,
		     "Generated: setter %s::%s();",
		     interfacee->name,
		     atributee->name);
	}

	return res;
}
