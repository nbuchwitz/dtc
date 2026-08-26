// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libfdt - Flat Device Tree manipulation
 *	Wrapper wip functions family
 * Copyright (C) 2026 Herve Codina, Bootlin.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>

#include <libfdt.h>

#include "tests.h"

struct args {
	char *dtb_path;
	enum {
		NOP_NODE,
		NOP_PROP,
		SET_PROP,
	} op;
	char *node_path;
	char *prop_name;
	int index;
	char *value;
};

const char *usage_txt = "Usage:\n"
	"  - wip_func <dtb> nop_node <node_path>\n"
	"  - wip_func <dtb> nop_prop <node_path> <prop_name>\n"
	"  - wip_func <dtb> set_prop <node_path> <prop_name> <index> <str>\n"
	;

static void parse_args(struct args *args, int argc, char *argv[])
{
	char *endptr;

	memset(args, 0, sizeof(*args));

	if (argc < 3)
		goto fail;

	args->dtb_path = argv[1];

	if (!strcmp(argv[2], "nop_node")) {
		args->op = NOP_NODE;
		if (argc != 4)
			goto fail;
		args->node_path = argv[3];
	} else if (!strcmp(argv[2], "nop_prop")) {
		args->op = NOP_PROP;
		if (argc != 5)
			goto fail;
		args->node_path = argv[3];
		args->prop_name = argv[4];
	} else if (!strcmp(argv[2], "set_prop")) {
		args->op = SET_PROP;
		if (argc != 7)
			goto fail;
		args->node_path = argv[3];
		args->prop_name = argv[4];
		errno = 0;
		args->index = strtol(argv[5], &endptr, 0);
		if (*endptr != '\0' || errno)
			goto fail;
		args->value = argv[6];
	} else {
		goto fail;
	}

	return;

fail:
	CONFIG("%s\n", usage_txt);
}

int main(int argc, char *argv[])
{
	struct args args;
	void *fdt;
	int node;
	int err;

	test_init(argc, argv);

	parse_args(&args, argc, argv);

	fdt = load_blob(args.dtb_path);

	node = fdt_path_offset(fdt, args.node_path);
	if (node < 0)
		FAIL("Fail to get '%s': %s", args.node_path, fdt_strerror(node));

	switch (args.op) {
	case NOP_NODE:
		err = fdt_nop_node(fdt, node);
		if (err)
			FAIL("fdt_nop_node('%s'): %s", args.node_path, fdt_strerror(err));
		break;

	case NOP_PROP:
		err = fdt_nop_property(fdt, node, args.prop_name);
		if (err)
			FAIL("fdt_nop_node('%s', '%s'): %s", args.node_path,
			     args.prop_name, fdt_strerror(err));
		break;

	case SET_PROP:
		err = fdt_setprop_inplace_namelen_partial(fdt, node,
			args.prop_name, strlen(args.prop_name), args.index,
			args.value, strlen(args.value) + 1);
		if (err)
			FAIL("fdt_setprop_inplace_namelen_partial('%s', '%s', %d, \"%s\"): %s",
			     args.node_path, args.prop_name, args.index,
			     args.value, fdt_strerror(err));
		break;
	}

	save_blob(args.dtb_path, fdt);

	PASS();
}
