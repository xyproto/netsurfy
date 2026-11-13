/* QuickJS binding generation implementation
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

static bool entry_has_object(struct ir_entry *entry)
{
	return (entry->type == IR_ENTRY_TYPE_INTERFACE) &&
	       !entry->u.interface.noobject;
}

static struct opctx *open_header(struct ir *ir, const char *name)
{
	char *fname;
	int fnamel;
	struct opctx *hdrc;
	int res;

	fnamel = strlen(name) + 4;
	fname = malloc(fnamel);
	snprintf(fname, fnamel, "%s.h", name);

	res = output_open(fname, &hdrc);
	free(fname);
	if (res != 0) {
		return NULL;
	}

	output_tool_preface(hdrc);

	output_method_cdata(hdrc,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PREFACE);

	outputf(hdrc, "\n#ifndef %s_%s_h\n", DLPFX, name);
	outputf(hdrc, "#define %s_%s_h\n\n", DLPFX, name);

	return hdrc;
}

static int close_header(struct ir *ir, struct opctx *hdrc)
{
	outputf(hdrc, "\n#endif\n");

	output_method_cdata(hdrc,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_POSTFACE);

	output_close(hdrc);

	return 0;
}


static int output_private_header(struct ir *ir)
{
	int idx;
	struct opctx *privc;

	privc = open_header(ir, "private");

	outputf(privc, "#include \"%s\"\n\n", "binding.h");

	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *interfacee;
		struct ir_entry *inherite;
		struct genbind_node *priv_node;

		interfacee = ir->entries + idx;

		if ((interfacee->type == IR_ENTRY_TYPE_INTERFACE) &&
		    (interfacee->u.interface.noobject)) {
			continue;
		}

		switch (interfacee->type) {
		case IR_ENTRY_TYPE_INTERFACE:
			outputf(privc,
				"/* Private data for %s interface */\n",
				interfacee->name);
			break;

		case IR_ENTRY_TYPE_DICTIONARY:
			outputf(privc,
				"/* Private data for %s dictionary */\n",
				interfacee->name);
			break;
		}

		outputf(privc, "typedef struct {\n");

		inherite = ir_inherit_entry(ir, interfacee);
		if (inherite != NULL) {
			outputf(privc,
				"\t%s_private_t parent;\n",
				inherite->class_name);
		} else {
			outputf(privc, "\tqjsky_object_t object;\n");
		}

		priv_node = genbind_node_find_type(genbind_node_getnode(
							   interfacee->class),
						   NULL,
						   GENBIND_NODE_TYPE_PRIVATE);
		while (priv_node != NULL) {
			outputc(privc, '\t');

			output_ctype(privc, priv_node, true);

			outputf(privc, ";\n");

			priv_node = genbind_node_find_type(
				genbind_node_getnode(interfacee->class),
				priv_node,
				GENBIND_NODE_TYPE_PRIVATE);
		}

		outputf(privc,
			"} __attribute__((aligned)) %s_private_t;\n\n",
			interfacee->class_name);
	}

	close_header(ir, privc);

	return 0;
}

static int output_prototype_header(struct ir *ir)
{
	int idx;
	struct opctx *protoc;

	protoc = open_header(ir, "prototype");

	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry;

		entry = ir->entries + idx;

		switch (entry->type) {
		case IR_ENTRY_TYPE_INTERFACE:
			output_interface_declaration(protoc, entry);
			break;

		case IR_ENTRY_TYPE_DICTIONARY:
			output_dictionary_declaration(protoc, entry);
			break;
		}
	}

	close_header(ir, protoc);

	return 0;
}

static int output_makefile(struct ir *ir)
{
	int idx;
	FILE *makef;

	makef = genb_fopen_tmp("Makefile");
	if (makef == NULL) {
		return -1;
	}

	fprintf(makef, "# QuickJS libdom makefile fragment\n\n");

	fprintf(makef, "NSGENBIND_SOURCES:=binding.c ");
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *interfacee;

		interfacee = ir->entries + idx;

		if ((interfacee->type == IR_ENTRY_TYPE_INTERFACE) &&
		    (interfacee->u.interface.noobject)) {
			continue;
		}

		fprintf(makef, "%s ", interfacee->filename);
	}
	fprintf(makef, "\nNSGENBIND_PREFIX:=%s\n", options->outdirname);

	genb_fclose_tmp(makef, "Makefile");

	return 0;
}


static int output_binding_header(struct ir *ir)
{
	struct opctx *bindc;
	int idx;

	bindc = open_header(ir, "binding");

	outputf(bindc, "enum %s_iface {\n", DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		char *ename;

		if (!entry_has_object(entry)) {
			continue;
		}
		ename = iface_enum_name(entry->name);
		outputf(bindc, "\t%s,\n", ename);
		free(ename);
	}
	outputf(bindc, "\tQJSKY_IFACE__COUNT\n};\n\n");

	outputf(bindc,
		"typedef struct {\n"
		"\tint iface;\n"
		"} %s_object_t;\n\n",
		DLPFX);

	outputf(bindc,
		"typedef JSValue (*%s_create_fn)(JSContext *ctx, void *ptr);\n"
		"typedef void (*%s_fini_fn)(JSRuntime *rt, void *priv);\n"
		"typedef int (*%s_proto_fn)(JSContext *ctx, JSValueConst proto, JSValueConst ctor);\n"
		"typedef JSValue (*%s_ctor_fn)(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv);\n\n",
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX);

	outputf(bindc,
		"extern const int %s_iface_parent[QJSKY_IFACE__COUNT];\n"
		"extern const char *const %s_iface_name[QJSKY_IFACE__COUNT];\n"
		"extern const %s_create_fn %s_iface_create[QJSKY_IFACE__COUNT];\n"
		"extern const %s_fini_fn %s_iface_fini[QJSKY_IFACE__COUNT];\n"
		"extern const %s_proto_fn %s_iface_proto[QJSKY_IFACE__COUNT];\n"
		"extern const %s_ctor_fn %s_iface_ctor[QJSKY_IFACE__COUNT];\n"
		"extern const int %s_iface_global;\n\n",
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX);

	outputf(bindc,
		"bool %s_iface_is(int iface, int ancestor);\n"
		"bool %s_instanceof(JSContext *ctx, JSValueConst val, int iface);\n"
		"void *%s_private(JSContext *ctx, JSValueConst val, int iface);\n"
		"JSValue %s_new_object(JSContext *ctx, int iface, void *priv);\n"
		"void *%s_global_private(JSContext *ctx);\n",
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX,
		DLPFX);

	close_header(ir, bindc);

	return 0;
}


static int output_binding_src(struct ir *ir)
{
	int idx;
	struct ir_entry *pglobale = NULL;
	struct opctx *bindc;
	int res;

	res = output_open("binding.c", &bindc);
	if (res != 0) {
		return -1;
	}

	output_tool_preface(bindc);

	output_method_cdata(bindc,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PREFACE);

	output_tool_prologue(bindc);

	output_method_cdata(bindc,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_PROLOGUE);

	outputc(bindc, '\n');

	outputf(bindc,
		"const int %s_iface_parent[QJSKY_IFACE__COUNT] = {\n",
		DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		struct ir_entry *inherite;
		char *ename;

		if (!entry_has_object(entry)) {
			continue;
		}
		if (entry->u.interface.primary_global) {
			pglobale = entry;
		}
		ename = iface_enum_name(entry->name);
		inherite = ir_inherit_entry(ir, entry);
		if (inherite != NULL) {
			char *pname = iface_enum_name(inherite->name);
			outputf(bindc, "\t[%s] = %s,\n", ename, pname);
			free(pname);
		} else {
			outputf(bindc, "\t[%s] = -1,\n", ename);
		}
		free(ename);
	}
	outputf(bindc, "};\n\n");

	outputf(bindc,
		"const char *const %s_iface_name[QJSKY_IFACE__COUNT] = {\n",
		DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		char *ename;

		if (!entry_has_object(entry)) {
			continue;
		}
		ename = iface_enum_name(entry->name);
		outputf(bindc, "\t[%s] = \"%s\",\n", ename, entry->name);
		free(ename);
	}
	outputf(bindc, "};\n\n");

	outputf(bindc,
		"const %s_ctor_fn %s_iface_ctor[QJSKY_IFACE__COUNT] = {\n",
		DLPFX,
		DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		char *ename;

		if (!entry_has_object(entry) ||
		    genbind_node_find_method(entry->class,
					     NULL,
					     GENBIND_METHOD_TYPE_CONSTRUCTOR) ==
			    NULL) {
			continue;
		}
		ename = iface_enum_name(entry->name);
		outputf(bindc,
			"\t[%s] = %s_%s___constructor,\n",
			ename,
			DLPFX,
			entry->class_name);
		free(ename);
	}
	outputf(bindc, "};\n\n");

	outputf(bindc,
		"const %s_create_fn %s_iface_create[QJSKY_IFACE__COUNT] = {\n",
		DLPFX,
		DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		char *ename;

		if (!entry_has_object(entry) ||
		    entry->u.interface.primary_global ||
		    !entry->class_create_ptr) {
			continue;
		}
		ename = iface_enum_name(entry->name);
		outputf(bindc,
			"\t[%s] = %s_%s___create_ptr,\n",
			ename,
			DLPFX,
			entry->class_name);
		free(ename);
	}
	outputf(bindc, "};\n\n");

	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;

		if (!entry_has_object(entry)) {
			continue;
		}
		outputf(bindc,
			"static void %s_%s___fini_any(JSRuntime *rt, void *priv)\n"
			"{\n"
			"\t%s_%s___fini(rt, priv);\n"
			"}\n\n",
			DLPFX,
			entry->class_name,
			DLPFX,
			entry->class_name);
	}

	outputf(bindc,
		"const %s_fini_fn %s_iface_fini[QJSKY_IFACE__COUNT] = {\n",
		DLPFX,
		DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		char *ename;

		if (!entry_has_object(entry)) {
			continue;
		}
		ename = iface_enum_name(entry->name);
		outputf(bindc,
			"\t[%s] = %s_%s___fini_any,\n",
			ename,
			DLPFX,
			entry->class_name);
		free(ename);
	}
	outputf(bindc, "};\n\n");

	outputf(bindc,
		"const %s_proto_fn %s_iface_proto[QJSKY_IFACE__COUNT] = {\n",
		DLPFX,
		DLPFX);
	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *entry = ir->entries + idx;
		char *ename;

		if (!entry_has_object(entry)) {
			continue;
		}
		ename = iface_enum_name(entry->name);
		outputf(bindc,
			"\t[%s] = %s_%s___proto,\n",
			ename,
			DLPFX,
			entry->class_name);
		free(ename);
	}
	outputf(bindc, "};\n\n");

	if (pglobale != NULL) {
		char *ename = iface_enum_name(pglobale->name);
		outputf(bindc,
			"const int %s_iface_global = %s;\n\n",
			DLPFX,
			ename);
		free(ename);
	} else {
		outputf(bindc, "const int %s_iface_global = -1;\n\n", DLPFX);
	}

	outputf(bindc,
		"bool %s_iface_is(int iface, int ancestor)\n"
		"{\n"
		"\twhile (iface >= 0) {\n"
		"\t\tif (iface == ancestor) {\n"
		"\t\t\treturn true;\n"
		"\t\t}\n"
		"\t\tiface = %s_iface_parent[iface];\n"
		"\t}\n"
		"\treturn false;\n"
		"}\n\n",
		DLPFX,
		DLPFX);

	outputf(bindc,
		"bool %s_instanceof(JSContext *ctx, JSValueConst val, int iface)\n"
		"{\n"
		"\treturn %s_private(ctx, val, iface) != NULL;\n"
		"}\n\n",
		DLPFX,
		DLPFX);

	output_method_cdata(bindc,
			    ir->binding_node,
			    GENBIND_METHOD_TYPE_POSTFACE);

	output_close(bindc);

	return 0;
}

static int output_interfaces_dictionaries(struct ir *ir)
{
	int res;
	int idx;

	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *irentry;

		irentry = ir->entries + idx;

		switch (irentry->type) {
		case IR_ENTRY_TYPE_INTERFACE:
			if (!irentry->u.interface.noobject) {
				res = output_interface(ir, irentry);
				if (res != 0) {
					return res;
				}
			}
			break;

		case IR_ENTRY_TYPE_DICTIONARY:
			res = output_dictionary(ir, irentry);
			if (res != 0) {
				return res;
			}

		default:
			break;
		}
	}

	return 0;
}

int qjs_libdom_output(struct ir *ir)
{
	int idx;
	int res = 0;

	for (idx = 0; idx < ir->entryc; idx++) {
		struct ir_entry *irentry;

		irentry = ir->entries + idx;

		irentry->class_name = gen_idl2c_name(irentry->name);

		if (irentry->class_name != NULL) {
			int ifacenamelen;

			ifacenamelen = strlen(irentry->class_name) + 4;
			irentry->filename = malloc(ifacenamelen);
			snprintf(irentry->filename,
				 ifacenamelen,
				 "%s.c",
				 irentry->class_name);
		}
	}

	res = output_interfaces_dictionaries(ir);
	if (res != 0) {
		goto output_err;
	}

	res = output_private_header(ir);
	if (res != 0) {
		goto output_err;
	}

	res = output_prototype_header(ir);
	if (res != 0) {
		goto output_err;
	}

	res = output_binding_header(ir);
	if (res != 0) {
		goto output_err;
	}

	res = output_binding_src(ir);
	if (res != 0) {
		goto output_err;
	}

	res = output_makefile(ir);

output_err:

	return res;
}
