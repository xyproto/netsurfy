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

static char *iface_enum_name(const char *interface_name)
{
	size_t len = strlen(interface_name);
	char *name = malloc(len + SLEN("QJSKY_IFACE_") + 1);
	size_t i;

	strcpy(name, "QJSKY_IFACE_");
	for (i = 0; i <= len; i++) {
		name[SLEN("QJSKY_IFACE_") + i] = toupper(interface_name[i]);
	}
	return name;
}

static bool compare_ctypes(struct genbind_node *a, struct genbind_node *b)
{
	struct genbind_node *ta;
	struct genbind_node *tb;

	ta = genbind_node_find_type(genbind_node_getnode(a),
				    NULL,
				    GENBIND_NODE_TYPE_NAME);
	tb = genbind_node_find_type(genbind_node_getnode(b),
				    NULL,
				    GENBIND_NODE_TYPE_NAME);

	while ((ta != NULL) && (tb != NULL)) {
		char *txt_a;
		char *txt_b;

		txt_a = genbind_node_gettext(ta);
		txt_b = genbind_node_gettext(tb);

		if (strcmp(txt_a, txt_b) != 0) {
			return false;
		}

		ta = genbind_node_find_type(genbind_node_getnode(a),
					    ta,
					    GENBIND_NODE_TYPE_NAME);
		tb = genbind_node_find_type(genbind_node_getnode(b),
					    tb,
					    GENBIND_NODE_TYPE_NAME);
	}
	if (ta != tb) {
		return false;
	}

	return true;
}

static int
output_get_method_private(struct opctx *outc, struct ir_entry *interfacee)
{
	char *ename = iface_enum_name(interfacee->name);

	outputf(outc,
		"\t%s_private_t *priv = %s_private(ctx, this_val, %s);\n"
		"\tif (priv == NULL) {\n"
		"\t\treturn JS_ThrowTypeError(ctx, \"Illegal invocation\");\n"
		"\t}\n\n",
		interfacee->class_name,
		DLPFX,
		ename);
	free(ename);

	return 0;
}

static int output_function_head(struct opctx *outc,
				const char *class_name,
				const char *name,
				const char *suffix)
{
	outputf(outc,
		"static JSValue %s_%s_%s%s(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)\n",
		DLPFX,
		class_name,
		name,
		suffix);
	return 0;
}

static int output_interface_inherit_init(struct opctx *outc,
					 struct ir_entry *interfacee,
					 struct ir_entry *inherite)
{
	struct genbind_node *init_node;
	struct genbind_node *inh_init_node;
	struct genbind_node *param_node;
	struct genbind_node *inh_param_node;

	if (inherite == NULL) {
		return 0;
	}

	init_node = genbind_node_find_method(interfacee->class,
					     NULL,
					     GENBIND_METHOD_TYPE_INIT);

	inh_init_node = genbind_node_find_method(inherite->class,
						 NULL,
						 GENBIND_METHOD_TYPE_INIT);

	outputf(outc,
		"\t%s_%s___init(ctx, &priv->parent",
		DLPFX,
		inherite->class_name);

	inh_param_node = genbind_node_find_type(genbind_node_getnode(
							inh_init_node),
						NULL,
						GENBIND_NODE_TYPE_PARAMETER);
	while (inh_param_node != NULL) {
		char *param_name;
		param_name = genbind_node_gettext(genbind_node_find_type(
			genbind_node_getnode(inh_param_node),
			NULL,
			GENBIND_NODE_TYPE_IDENT));

		param_node = genbind_node_find_type_ident(
			genbind_node_getnode(init_node),
			NULL,
			GENBIND_NODE_TYPE_PARAMETER,
			param_name);
		if (param_node == NULL) {
			fprintf(stderr,
				"class \"%s\" (interface %s) parent class \"%s\" (interface %s) initialisor requires a parameter \"%s\" with compatible identifier\n",
				interfacee->class_name,
				interfacee->name,
				inherite->class_name,
				inherite->name,
				param_name);
			return -1;
		} else {
			outputf(outc, ", ");

			if (compare_ctypes(param_node, inh_param_node) ==
			    false) {
				outputc(outc, '(');
				output_ctype(outc, inh_param_node, false);
				outputc(outc, ')');
			}

			output_cdata(outc, param_node, GENBIND_NODE_TYPE_IDENT);
		}

		inh_param_node = genbind_node_find_type(
			genbind_node_getnode(inh_init_node),
			inh_param_node,
			GENBIND_NODE_TYPE_PARAMETER);
	}

	outputf(outc, ");\n");

	return 0;
}

static enum ir_init_argtype guess_argtype_from(struct genbind_node *param_node)
{
	const char *type_cdata = NULL;
	struct genbind_node *typename_node;
	bool unsigned_ = false;
	bool int_ = false;
	bool bool_ = false;

	typename_node = genbind_node_find_type(genbind_node_getnode(param_node),
					       NULL,
					       GENBIND_NODE_TYPE_NAME);
	while (typename_node != NULL) {
		type_cdata = genbind_node_gettext(typename_node);
		if (strcmp(type_cdata, "unsigned") == 0) {
			unsigned_ = true;
		} else if (strcmp(type_cdata, "int") == 0) {
			int_ = true;
		} else if (strcmp(type_cdata, "bool") == 0) {
			bool_ = true;
		}
		typename_node = genbind_node_find_type(genbind_node_getnode(
							       param_node),
						       typename_node,
						       GENBIND_NODE_TYPE_NAME);
	}

	if (type_cdata[0] == '*') {
		return IR_INIT_ARG_POINTER;
	} else if (unsigned_) {
		return IR_INIT_ARG_UNSIGNED;
	} else if (int_) {
		return IR_INIT_ARG_INT;
	} else if (bool_) {
		return IR_INIT_ARG_BOOL;
	}

	return IR_INIT_ARG_POINTER;
}

static int output_init_params(struct opctx *outc,
			      struct ir_entry *interfacee,
			      struct genbind_node *init_node,
			      bool types)
{
	struct genbind_node *param_node;

	param_node = genbind_node_find_type(genbind_node_getnode(init_node),
					    NULL,
					    GENBIND_NODE_TYPE_PARAMETER);
	while (param_node != NULL) {
		outputf(outc, ", ");
		if (types) {
			output_ctype(outc, param_node, true);
		} else {
			output_cdata(outc, param_node, GENBIND_NODE_TYPE_IDENT);
		}
		param_node = genbind_node_find_type(
			genbind_node_getnode(init_node),
			param_node,
			GENBIND_NODE_TYPE_PARAMETER);
	}
	return 0;
}

static int output_interface_init_declaration(struct opctx *outc,
					     struct ir_entry *interfacee,
					     struct genbind_node *init_node)
{
	struct genbind_node *param_node;

	outputf(outc,
		"void %s_%s___init(JSContext *ctx, %s_private_t *priv",
		DLPFX,
		interfacee->class_name,
		interfacee->class_name);

	interfacee->class_init_argc = 0;
	interfacee->class_init_argt = NULL;

	param_node = genbind_node_find_type(genbind_node_getnode(init_node),
					    NULL,
					    GENBIND_NODE_TYPE_PARAMETER);
	while (param_node != NULL) {
		interfacee->class_init_argc++;
		interfacee->class_init_argt = realloc(
			interfacee->class_init_argt,
			interfacee->class_init_argc *
				sizeof(enum ir_init_argtype));
		interfacee->class_init_argt[interfacee->class_init_argc - 1] =
			guess_argtype_from(param_node);
		outputf(outc, ", ");

		output_ctype(outc, param_node, true);

		param_node = genbind_node_find_type(
			genbind_node_getnode(init_node),
			param_node,
			GENBIND_NODE_TYPE_PARAMETER);
	}

	outputc(outc, ')');

	interfacee->class_create_ptr = (interfacee->class_init_argc == 0) ||
				       ((interfacee->class_init_argc == 1) &&
					(interfacee->class_init_argt[0] ==
					 IR_INIT_ARG_POINTER));

	return 0;
}

static int output_interface_init(struct opctx *outc,
				 struct ir_entry *interfacee,
				 struct ir_entry *inherite)
{
	struct genbind_node *init_node;
	int res;

	init_node = genbind_node_find_method(interfacee->class,
					     NULL,
					     GENBIND_METHOD_TYPE_INIT);

	output_interface_init_declaration(outc, interfacee, init_node);

	outputf(outc, "\n{\n");

	res = output_interface_inherit_init(outc, interfacee, inherite);
	if (res != 0) {
		return res;
	}

	output_ccode(outc, init_node);

	outputf(outc, "}\n\n");

	return 0;
}

static int output_interface_fini(struct opctx *outc,
				 struct ir_entry *interfacee,
				 struct ir_entry *inherite)
{
	struct genbind_node *fini_node;

	fini_node = genbind_node_find_method(interfacee->class,
					     NULL,
					     GENBIND_METHOD_TYPE_FINI);

	outputf(outc,
		"void %s_%s___fini(JSRuntime *rt, %s_private_t *priv)\n",
		DLPFX,
		interfacee->class_name,
		interfacee->class_name);
	outputf(outc, "{\n");

	output_ccode(outc, fini_node);

	if (inherite != NULL) {
		outputf(outc,
			"\t%s_%s___fini(rt, &priv->parent);\n",
			DLPFX,
			inherite->class_name);
	}
	outputf(outc, "}\n\n");

	return 0;
}

static int
output_interface_create(struct opctx *outc, struct ir_entry *interfacee)
{
	struct genbind_node *init_node;
	char *ename;

	if (interfacee->u.interface.primary_global) {
		return 0;
	}

	init_node = genbind_node_find_method(interfacee->class,
					     NULL,
					     GENBIND_METHOD_TYPE_INIT);
	ename = iface_enum_name(interfacee->name);

	outputf(outc,
		"JSValue %s_%s___create(JSContext *ctx",
		DLPFX,
		interfacee->class_name);
	output_init_params(outc, interfacee, init_node, true);
	outputf(outc, ")\n{\n");
	outputf(outc,
		"\t%s_private_t *priv = calloc(1, sizeof(*priv));\n"
		"\tJSValue obj;\n\n"
		"\tif (priv == NULL) {\n"
		"\t\treturn JS_ThrowOutOfMemory(ctx);\n"
		"\t}\n"
		"\t((%s_object_t *)priv)->iface = %s;\n"
		"\t%s_%s___init(ctx, priv",
		interfacee->class_name,
		DLPFX,
		ename,
		DLPFX,
		interfacee->class_name);
	output_init_params(outc, interfacee, init_node, false);
	outputf(outc,
		");\n"
		"\tobj = %s_new_object(ctx, %s, priv);\n"
		"\treturn obj;\n"
		"}\n\n",
		DLPFX,
		ename);

	if (interfacee->class_create_ptr) {
		outputf(outc,
			"JSValue %s_%s___create_ptr(JSContext *ctx, void *ptr)\n"
			"{\n",
			DLPFX,
			interfacee->class_name);
		if (interfacee->class_init_argc == 0) {
			outputf(outc,
				"\t(void)ptr;\n"
				"\treturn %s_%s___create(ctx);\n",
				DLPFX,
				interfacee->class_name);
		} else {
			outputf(outc,
				"\treturn %s_%s___create(ctx, ptr);\n",
				DLPFX,
				interfacee->class_name);
		}
		outputf(outc, "}\n\n");
	}

	free(ename);
	return 0;
}

static int output_default_value(struct opctx *outc,
				struct ir_operation_argument_entry *cure,
				bool *allocated)
{
	struct webidl_node *lit_node;
	int *lit_int;
	char *lit_str;

	*allocated = false;
	lit_node = webidl_node_getnode(
		webidl_node_find_type(webidl_node_getnode(cure->node),
				      NULL,
				      WEBIDL_NODE_TYPE_OPTIONAL));

	if (lit_node == NULL) {
		outputf(outc, "JS_UNDEFINED");
		return 0;
	}

	switch (webidl_node_gettype(lit_node)) {
	case WEBIDL_NODE_TYPE_LITERAL_NULL:
		outputf(outc, "JS_NULL");
		break;

	case WEBIDL_NODE_TYPE_LITERAL_INT:
		lit_int = webidl_node_getint(lit_node);
		outputf(outc, "JS_NewInt32(ctx, %d)", *lit_int);
		break;

	case WEBIDL_NODE_TYPE_LITERAL_BOOL:
		lit_int = webidl_node_getint(lit_node);
		outputf(outc, "JS_NewBool(ctx, %d)", *lit_int);
		break;

	case WEBIDL_NODE_TYPE_LITERAL_STRING:
		lit_str = webidl_node_gettext(lit_node);
		outputf(outc, "JS_NewString(ctx, \"%s\")", lit_str);
		*allocated = true;
		break;

	case WEBIDL_NODE_TYPE_LITERAL_FLOAT:
	default:
		outputf(outc, "JS_UNDEFINED");
		break;
	}
	return 0;
}

static bool
operation_has_allocated_default(struct ir_operation_overload_entry *overloade)
{
	int argidx;

	for (argidx = 0; argidx < overloade->argumentc; argidx++) {
		struct webidl_node *lit_node;
		lit_node = webidl_node_getnode(webidl_node_find_type(
			webidl_node_getnode(overloade->argumentv[argidx].node),
			NULL,
			WEBIDL_NODE_TYPE_OPTIONAL));
		if ((lit_node != NULL) && (webidl_node_gettype(lit_node) ==
					   WEBIDL_NODE_TYPE_LITERAL_STRING)) {
			return true;
		}
	}
	return false;
}

static int output_operation_body(struct opctx *outc,
				 struct ir_entry *interfacee,
				 struct ir_operation_entry *operatione)
{
	int cdatac;

	output_get_method_private(outc, interfacee);

	cdatac = output_ccode(outc, operatione->method);
	if (cdatac == 0) {
		WARN(WARNING_UNIMPLEMENTED,
		     "Unimplemented: method %s::%s();",
		     interfacee->name,
		     operatione->name);
		outputf(outc, "\treturn JS_UNDEFINED;\n");
	}
	return 0;
}

static int
output_interface_plain_operation(struct opctx *outc,
				 struct ir_entry *interfacee,
				 struct ir_operation_entry *operatione)
{
	output_function_head(
		outc, interfacee->class_name, operatione->name, "");
	outputf(outc, "{\n");
	output_operation_body(outc, interfacee, operatione);
	outputf(outc, "}\n\n");

	return 0;
}

static int output_interface_operation(struct opctx *outc,
				      struct ir_entry *interfacee,
				      struct ir_operation_entry *operatione)
{
	struct ir_operation_overload_entry *overloade;
	int fixedargc;
	int argidx;
	bool split;

	if (operatione->name == NULL) {
		outputf(outc,
			"/* Special method definition - UNIMPLEMENTED */\n\n");
		WARN(WARNING_UNIMPLEMENTED,
		     "Special operation on interface %s (operation entry %p)",
		     interfacee->name,
		     operatione);
		return 0;
	}

	if ((operatione->overloadc != 1) ||
	    (operatione->overloadv->elipsisc != 0)) {
		if (operatione->overloadc == 1) {
			WARN(WARNING_UNIMPLEMENTED,
			     "Elipsis parameters not checked: method %s::%s();",
			     interfacee->name,
			     operatione->name);
		}
		return output_interface_plain_operation(outc,
							interfacee,
							operatione);
	}

	overloade = operatione->overloadv;
	fixedargc = overloade->argumentc - overloade->optionalc;
	split = operation_has_allocated_default(overloade);

	if (split) {
		output_function_head(outc,
				     interfacee->class_name,
				     operatione->name,
				     "__impl");
		outputf(outc, "{\n");
		output_operation_body(outc, interfacee, operatione);
		outputf(outc, "}\n\n");
	}

	output_function_head(
		outc, interfacee->class_name, operatione->name, "");
	outputf(outc, "{\n");

	if (fixedargc > 0) {
		outputf(outc,
			"\tif (argc < %d) {\n"
			"\t\treturn JS_ThrowTypeError(ctx, \"%%d argument required, but only %%d present.\", %d, argc);\n"
			"\t}\n",
			fixedargc,
			fixedargc);
	}

	if (overloade->argumentc > 0) {
		outputf(outc,
			"\tJSValue %s_args[%d];\n"
			"\tif (argc < %d) {\n",
			DLPFX,
			overloade->argumentc,
			overloade->argumentc);
		for (argidx = 0; argidx < overloade->argumentc; argidx++) {
			bool allocated;
			if (argidx < fixedargc) {
				outputf(outc,
					"\t\t%s_args[%d] = argv[%d];\n",
					DLPFX,
					argidx,
					argidx);
				continue;
			}
			outputf(outc,
				split ? "\t\t%s_args[%d] = argc > %d ? JS_DupValue(ctx, argv[%d]) : "
				      : "\t\t%s_args[%d] = argc > %d ? argv[%d] : ",
				DLPFX,
				argidx,
				argidx,
				argidx);
			output_default_value(outc,
					     overloade->argumentv + argidx,
					     &allocated);
			outputf(outc, ";\n");
		}
		if (split) {
			outputf(outc,
				"\t\tJSValue %s_ret = %s_%s_%s__impl(ctx, this_val, %d, %s_args);\n",
				DLPFX,
				DLPFX,
				interfacee->class_name,
				operatione->name,
				overloade->argumentc,
				DLPFX);
			for (argidx = fixedargc; argidx < overloade->argumentc;
			     argidx++) {
				outputf(outc,
					"\t\tJS_FreeValue(ctx, %s_args[%d]);\n",
					DLPFX,
					argidx);
			}
			outputf(outc,
				"\t\treturn %s_ret;\n"
				"\t}\n"
				"\treturn %s_%s_%s__impl(ctx, this_val, %d, argv);\n"
				"}\n\n",
				DLPFX,
				DLPFX,
				interfacee->class_name,
				operatione->name,
				overloade->argumentc);
			return 0;
		}
		outputf(outc,
			"\t\targv = %s_args;\n"
			"\t}\n"
			"\targc = %d;\n"
			"\t(void)argc;\n\n",
			DLPFX,
			overloade->argumentc);
	} else {
		outputf(outc, "\targc = 0;\n\t(void)argc;\n\n");
	}

	output_operation_body(outc, interfacee, operatione);
	outputf(outc, "}\n\n");

	return 0;
}

static int output_interface_operations(struct opctx *outc, struct ir_entry *ife)
{
	int opc;
	int res = 0;

	for (opc = 0; opc < ife->u.interface.operationc; opc++) {
		res = output_interface_operation(
			outc, ife, ife->u.interface.operationv + opc);
		if (res != 0) {
			break;
		}
	}

	return res;
}

static int output_attribute_getter(struct opctx *outc,
				   struct ir_entry *interfacee,
				   struct ir_attribute_entry *atributee)
{
	output_function_head(
		outc, interfacee->class_name, atributee->name, "_getter");
	outputf(outc, "{\n");

	output_get_method_private(outc, interfacee);

	if (atributee->getter != NULL) {
		int res;
		res = output_ccode(outc, atributee->getter);
		if (res == 0) {
			res = output_generated_attribute_getter(outc,
								interfacee,
								atributee);
		}
		if (res >= 0) {
			outputf(outc, "}\n\n");
			return res;
		}
	}

	const char *type_str;
	if (atributee->typec == 0) {
		type_str = "";
	} else if (atributee->typec == 1) {
		type_str = webidl_type_to_str(atributee->typev[0].modifier,
					      atributee->typev[0].base);
	} else {
		type_str = "multiple";
	}

	WARN(WARNING_UNIMPLEMENTED,
	     "Unimplemented: getter %s::%s(%s);",
	     interfacee->name,
	     atributee->name,
	     type_str);

	outputf(outc,
		"\treturn JS_UNDEFINED;\n"
		"}\n\n");

	return 0;
}

static int output_putforwards_setter(struct opctx *outc,
				     struct ir_entry *interfacee,
				     struct ir_attribute_entry *atributee)
{
	outputf(outc,
		"\tJSValue target = %s_%s_%s_getter(ctx, this_val, 0, NULL);\n"
		"\tint res;\n\n"
		"\t(void)priv;\n"
		"\tif (JS_IsException(target)) {\n"
		"\t\treturn target;\n"
		"\t}\n"
		"\tres = JS_SetPropertyStr(ctx, target, \"%s\", JS_DupValue(ctx, argv[0]));\n"
		"\tJS_FreeValue(ctx, target);\n"
		"\treturn res < 0 ? JS_EXCEPTION : JS_UNDEFINED;\n",
		DLPFX,
		interfacee->class_name,
		atributee->name,
		atributee->putforwards);

	return 0;
}

static int output_attribute_setter(struct opctx *outc,
				   struct ir_entry *interfacee,
				   struct ir_attribute_entry *atributee)
{
	int res = -1;

	output_function_head(
		outc, interfacee->class_name, atributee->name, "_setter");
	outputf(outc, "{\n");

	output_get_method_private(outc, interfacee);

	if (atributee->setter != NULL) {
		res = output_ccode(outc, atributee->setter);
		if (res == 0) {
			res = output_generated_attribute_setter(outc,
								interfacee,
								atributee);
		}
	} else if (atributee->putforwards != NULL) {
		res = output_putforwards_setter(outc, interfacee, atributee);
	}

	if (res < 0) {
		const char *type_str;
		if (atributee->typec == 0) {
			type_str = "";
		} else if (atributee->typec == 1) {
			type_str = webidl_type_to_str(
				atributee->typev[0].modifier,
				atributee->typev[0].base);
		} else {
			type_str = "multiple";
		}
		WARN(WARNING_UNIMPLEMENTED,
		     "Unimplemented: setter %s::%s(%s);",
		     interfacee->name,
		     atributee->name,
		     type_str);

		outputf(outc, "\treturn JS_UNDEFINED;\n");
	}

	outputf(outc, "}\n\n");

	return res;
}

static bool attribute_has_setter(struct ir_attribute_entry *atributee)
{
	return (atributee->modifier != WEBIDL_TYPE_MODIFIER_READONLY) ||
	       (atributee->putforwards != NULL);
}

static int output_interface_attribute(struct opctx *outc,
				      struct ir_entry *interfacee,
				      struct ir_attribute_entry *atributee)
{
	int res;

	if (atributee->property_name == NULL) {
		atributee->property_name = gen_idl2c_name(atributee->name);
	}

	res = output_attribute_getter(outc, interfacee, atributee);

	if (attribute_has_setter(atributee)) {
		res = output_attribute_setter(outc, interfacee, atributee);
	}

	return res;
}

static int output_interface_attributes(struct opctx *outc, struct ir_entry *ife)
{
	int attrc;

	for (attrc = 0; attrc < ife->u.interface.attributec; attrc++) {
		output_interface_attribute(outc,
					   ife,
					   ife->u.interface.attributev + attrc);
	}

	return 0;
}

static int operation_length(struct ir_operation_entry *operatione)
{
	if ((operatione->overloadc != 1) ||
	    (operatione->overloadv->elipsisc != 0)) {
		return 0;
	}
	return operatione->overloadv->argumentc -
	       operatione->overloadv->optionalc;
}

static int
output_interface_prototype(struct opctx *outc, struct ir_entry *interfacee)
{
	struct genbind_node *proto_node;
	int idx;

	proto_node = genbind_node_find_method(interfacee->class,
					      NULL,
					      GENBIND_METHOD_TYPE_PROTOTYPE);

	outputf(outc,
		"int %s_%s___proto(JSContext *ctx, JSValueConst proto, JSValueConst ctor)\n",
		DLPFX,
		interfacee->class_name);
	outputf(outc, "{\n");

	if (output_ccode(outc, proto_node) != 0) {
		outputf(outc, "\n");
	}

	for (idx = 0; idx < interfacee->u.interface.operationc; idx++) {
		struct ir_operation_entry *operatione;
		operatione = interfacee->u.interface.operationv + idx;
		if (operatione->name == NULL) {
			continue;
		}
		outputf(outc,
			"\tif (%s_define_method(ctx, proto, \"%s\", %s_%s_%s, %d) < 0) {\n"
			"\t\treturn -1;\n"
			"\t}\n",
			DLPFX,
			operatione->name,
			DLPFX,
			interfacee->class_name,
			operatione->name,
			operation_length(operatione));
	}

	for (idx = 0; idx < interfacee->u.interface.attributec; idx++) {
		struct ir_attribute_entry *atributee;
		atributee = interfacee->u.interface.attributev + idx;
		if (attribute_has_setter(atributee)) {
			outputf(outc,
				"\tif (%s_define_accessor(ctx, proto, \"%s\", %s_%s_%s_getter, %s_%s_%s_setter) < 0) {\n",
				DLPFX,
				atributee->name,
				DLPFX,
				interfacee->class_name,
				atributee->name,
				DLPFX,
				interfacee->class_name,
				atributee->name);
		} else {
			outputf(outc,
				"\tif (%s_define_accessor(ctx, proto, \"%s\", %s_%s_%s_getter, NULL) < 0) {\n",
				DLPFX,
				atributee->name,
				DLPFX,
				interfacee->class_name,
				atributee->name);
		}
		outputf(outc,
			"\t\treturn -1;\n"
			"\t}\n");
	}

	for (idx = 0; idx < interfacee->u.interface.constantc; idx++) {
		struct ir_constant_entry *constante;
		int *value;
		constante = interfacee->u.interface.constantv + idx;
		value = webidl_node_getint(webidl_node_find_type(
			webidl_node_getnode(constante->node),
			NULL,
			WEBIDL_NODE_TYPE_LITERAL_INT));
		outputf(outc,
			"\tif (%s_define_constant(ctx, proto, ctor, \"%s\", %d) < 0) {\n"
			"\t\treturn -1;\n"
			"\t}\n",
			DLPFX,
			constante->name,
			*value);
	}

	outputf(outc, "\treturn 0;\n");
	outputf(outc, "}\n\n");

	return 0;
}

static void
output_interface_constructor(struct opctx *outc, struct ir_entry *interfacee)
{
	struct genbind_node *ctor_node;

	ctor_node = genbind_node_find_method(interfacee->class,
					     NULL,
					     GENBIND_METHOD_TYPE_CONSTRUCTOR);
	if (ctor_node == NULL) {
		return;
	}

	outputf(outc,
		"JSValue %s_%s___constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv)\n",
		DLPFX,
		interfacee->class_name);
	outputf(outc, "{\n");
	output_ccode(outc, ctor_node);
	outputf(outc, "}\n\n");
}

int output_interface(struct ir *ir, struct ir_entry *interfacee)
{
	struct opctx *ifop;
	struct ir_entry *inherite;
	int res = 0;

	res = output_open(interfacee->filename, &ifop);
	if (res != 0) {
		return res;
	}

	inherite = ir_inherit_entry(ir, interfacee);

	output_tool_preface(ifop);

	output_method_cdata(ifop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PREFACE);

	output_method_cdata(ifop,
			    interfacee->class,
			    GENBIND_METHOD_TYPE_PREFACE);

	output_tool_prologue(ifop);

	output_method_cdata(ifop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PROLOGUE);

	output_method_cdata(ifop,
			    interfacee->class,
			    GENBIND_METHOD_TYPE_PROLOGUE);

	outputf(ifop, "\n");

	res = output_interface_init(ifop, interfacee, inherite);
	if (res != 0) {
		goto op_error;
	}

	output_interface_fini(ifop, interfacee, inherite);

	output_interface_create(ifop, interfacee);

	output_interface_constructor(ifop, interfacee);

	output_interface_operations(ifop, interfacee);

	output_interface_attributes(ifop, interfacee);

	output_interface_prototype(ifop, interfacee);

	outputf(ifop, "\n");

	output_method_cdata(ifop,
			    interfacee->class,
			    GENBIND_METHOD_TYPE_EPILOGUE);

	output_method_cdata(ifop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_EPILOGUE);

	output_method_cdata(ifop,
			    interfacee->class,
			    GENBIND_METHOD_TYPE_POSTFACE);

	output_method_cdata(ifop,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_POSTFACE);

op_error:
	output_close(ifop);

	return res;
}

int output_interface_declaration(struct opctx *outc,
				 struct ir_entry *interfacee)
{
	struct genbind_node *init_node;

	if (interfacee->u.interface.noobject) {
		return 0;
	}

	outputf(outc,
		"int %s_%s___proto(JSContext *ctx, JSValueConst proto, JSValueConst ctor);\n",
		DLPFX,
		interfacee->class_name);

	outputf(outc,
		"void %s_%s___fini(JSRuntime *rt, %s_private_t *priv);\n",
		DLPFX,
		interfacee->class_name,
		interfacee->class_name);

	if (genbind_node_find_method(interfacee->class,
				     NULL,
				     GENBIND_METHOD_TYPE_CONSTRUCTOR) != NULL) {
		outputf(outc,
			"JSValue %s_%s___constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);\n",
			DLPFX,
			interfacee->class_name);
	}

	init_node = genbind_node_find_method(interfacee->class,
					     NULL,
					     GENBIND_METHOD_TYPE_INIT);

	output_interface_init_declaration(outc, interfacee, init_node);
	outputf(outc, ";\n");

	if (!interfacee->u.interface.primary_global) {
		outputf(outc,
			"JSValue %s_%s___create(JSContext *ctx",
			DLPFX,
			interfacee->class_name);
		output_init_params(outc, interfacee, init_node, true);
		outputf(outc, ");\n");
		if (interfacee->class_create_ptr) {
			outputf(outc,
				"JSValue %s_%s___create_ptr(JSContext *ctx, void *ptr);\n",
				DLPFX,
				interfacee->class_name);
		}
	}

	outputf(outc, "\n");

	return 0;
}
