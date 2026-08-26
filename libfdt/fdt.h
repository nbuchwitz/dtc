/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause) */
#ifndef FDT_H
#define FDT_H
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 * Copyright 2012 Kim Phillips, Freescale Semiconductor.
 */

#ifndef __ASSEMBLER__

struct fdt_header {
	fdt32_t magic;			 /* magic word FDT_MAGIC */
	fdt32_t totalsize;		 /* total size of DT block */
	fdt32_t off_dt_struct;		 /* offset to structure */
	fdt32_t off_dt_strings;		 /* offset to strings */
	fdt32_t off_mem_rsvmap;		 /* offset to memory reserve map */
	fdt32_t version;		 /* format version */
	fdt32_t last_comp_version;	 /* last compatible version */

	/* version 2 fields below */
	fdt32_t boot_cpuid_phys;	 /* Which physical CPU id we're
					    booting on */
	/* version 3 fields below */
	fdt32_t size_dt_strings;	 /* size of the strings block */

	/* version 17 fields below */
	fdt32_t size_dt_struct;		 /* size of the structure block */

	/* version 18 fields below */
	fdt32_t dt_flags;		/* Ored value of FDT_FLAG_XXXX */
	fdt32_t last_comp_version_w;	/* last compatible version for writing */
};

struct fdt_reserve_entry {
	fdt64_t address;
	fdt64_t size;
};

struct fdt_node_header {
	fdt32_t tag;
	char name[];
};

struct fdt_property {
	fdt32_t tag;
	fdt32_t len;
	fdt32_t nameoff;
	char data[];
};

#endif /* !__ASSEMBLER__ */

#define FDT_MAGIC	0xd00dfeed	/* 4: version, 4: total size */
#define FDT_FLAG_ADDON  0x1
#define FDT_TAGSIZE	sizeof(fdt32_t)
#define FDT_CELLSIZE	sizeof(fdt32_t)

#define FDT_BEGIN_NODE	0x1		/* Start node: full name */
#define FDT_END_NODE	0x2		/* End node */
#define FDT_PROP	0x3		/* Property: name off,
					   size, content */
#define FDT_NOP		0x4		/* nop */
#define FDT_END		0x9

/* Tag values flags */
#define FDT_TAG_STRUCTURED	(1U<<31)
#define FDT_TAG_SKIP_SAFE	(1U<<30)
#define FDT_TAG_DATA_MASK	(3U<<28)
#define FDT_TAG_DATA_NONE	(0U<<28)
#define FDT_TAG_DATA_1CELL	(1U<<28)
#define FDT_TAG_DATA_2CELLS	(2U<<28)
#define FDT_TAG_DATA_VARLEN	(3U<<28)

#define FDT_TAG_NO_SKIP(tag_data, tag_id) \
		(FDT_TAG_STRUCTURED | tag_data | tag_id)

#define FDT_TAG_CAN_SKIP(tag_data, tag_id) \
		(FDT_TAG_STRUCTURED | FDT_TAG_SKIP_SAFE | tag_data | tag_id)

/* Tests reserved tags */
#define FDT_TEST_NONE_CAN_SKIP		FDT_TAG_CAN_SKIP(FDT_TAG_DATA_NONE, 0)
#define FDT_TEST_1CELL_CAN_SKIP		FDT_TAG_CAN_SKIP(FDT_TAG_DATA_1CELL, 0)
#define FDT_TEST_2CELLS_CAN_SKIP	FDT_TAG_CAN_SKIP(FDT_TAG_DATA_2CELLS, 0)
#define FDT_TEST_VARLEN_CAN_SKIP	FDT_TAG_CAN_SKIP(FDT_TAG_DATA_VARLEN, 0)
#define FDT_TEST_NONE_NO_SKIP		FDT_TAG_NO_SKIP(FDT_TAG_DATA_NONE, 0)

/*
 * local phandle tag (meta-data). 1 cell: offset
 *
 * It indicates that the property defined before this tag (FDT_PROP) uses a
 * phandle value and the node related to this phandle value is local (i.e.
 * the node is present in the device-tree blob).
 *
 * offset: Offset in the property data where the phandle is available
 */
#define FDT_PROPDATA_PHANDLE   FDT_TAG_CAN_SKIP(FDT_TAG_DATA_1CELL, 0x1000)

/*
 * external phandle tag (meta-data). varlen: offset, string label
 *
 * It indicates that the property defined before this tag (FDT_PROP) uses a
 * phandle value and the node related to this phandle value is not local
 * node (i.e. the node is not present in the device-tree blob). The phandle
 * value used in the property has to be resolved when the device-tree blob
 * is applied on top of a base device-tree.
 *
 * offset: Offset in the property data where the phandle is available
 * label: The label to use to resolve the phandle value
 */
#define FDT_PROPDATA_PHANDLE_REF  FDT_TAG_CAN_SKIP(FDT_TAG_DATA_VARLEN, 0x1001)

/*
 * Export symbol tag (meta-data). varlen: name, phandle
 *
 * It defines an exported symbol. It can be present in a node bloc meaning that
 * a symbol is exported at this node level. The node pointed to by this symbol
 * is a local node and identified by a phandle value.
 *
 * phandle: phandle value identifying the node referenced by this symbol
 * name: export symbol name, i.e. the name used to reference this exported
 *       symbol.
 */
#define FDT_EXPORT_SYM	FDT_TAG_CAN_SKIP(FDT_TAG_DATA_VARLEN, 0x2000)

/*
 * Export external symbol tag (meta-data). varlen: label, name
 *
 * It defines an exported symbol. It can be present in a node bloc meaning that
 * a symbol is exported at this node level. The node pointed to by this symbol
 * is not a local node (i.e. the node is not present in the device-tree blob.).
 * This tag can be available only in overlay or addon device-tree blobs. The
 * symbol has to be resolved when the device-tree blob is applied on top of a
 * base device-tree.
 *
 * label: Label to use to resolve this symbol. This label is the reference to
 *        the external phandle.
 * name: export symbol name, i.e. the name used to reference this exported
 *       symbol.
 */
#define FDT_EXPORT_SYM_REF FDT_TAG_CAN_SKIP(FDT_TAG_DATA_VARLEN, 0x2001)

/*
 * Import symbol tag (meta-data). varlen: name, compatible
 *
 * It defines an imported symbol. It can be present globally in an addon dtb
 * (i.e. outside nodes definition) meaning that this symbol needs to be resolved
 * when the dtb is applied.
 *
 * name: import symbol name, i.e. the name used to reference this imported
 *       symbol.
 * compatible: compatible string that can be used for symbol resolution.
 *             This string can be an empty string if it is not relevant.
 */
#define FDT_IMPORT_SYM	FDT_TAG_CAN_SKIP(FDT_TAG_DATA_VARLEN, 0x2100)

/*
 * Node reference tag. varlen: node symbol name
 *
 * It defines a node external to the dtb but modified by the dtb. It can be
 * present in an addon dtb.
 *
 * For instance, if an addon adds a sub-node to a node existing in the
 * device-tree the addon is applied to, the existing node needs to be
 * referenced by the addon dtb. This is the purpose of FDT_BEGIN_NODE_REF.
 *
 * node symbol name: The symbol name used to reference the node in the base
 *                   device-tree when the addon is applied.
 *
 * Following the tag and its related node symbol name, the node structure is
 * present. After the node structure, the FTD_END_NODE tag is present.
 */
#define FDT_BEGIN_NODE_REF FDT_TAG_NO_SKIP(FDT_TAG_DATA_VARLEN, 0x2200)

#define FDT_V1_SIZE	(7*sizeof(fdt32_t))
#define FDT_V2_SIZE	(FDT_V1_SIZE + sizeof(fdt32_t))
#define FDT_V3_SIZE	(FDT_V2_SIZE + sizeof(fdt32_t))
#define FDT_V16_SIZE	FDT_V3_SIZE
#define FDT_V17_SIZE	(FDT_V16_SIZE + sizeof(fdt32_t))
#define FDT_V18_SIZE	(FDT_V17_SIZE + 2 * sizeof(fdt32_t))
#define FDT_V20_SIZE	FDT_V18_SIZE

#endif /* FDT_H */
