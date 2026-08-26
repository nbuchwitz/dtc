// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

int fdt_check_full(const void *fdt, size_t bufsize)
{
	int err;
	int num_memrsv;
	int offset, nextoffset = 0, d, tmp;
	uint32_t tag;
	unsigned int depth = 0;
	const void *prop = NULL;
	const char *propname;
	int proplen;
	bool expect_end = false;
	bool subnode_found = false;
	const fdt32_t *pfdt32;
	const char *str;
	uint32_t val;
	int len;

	if (can_assume(PERFECT))
		return 0;
	if (bufsize < FDT_V1_SIZE)
		return -FDT_ERR_TRUNCATED;
	if (bufsize < fdt_header_size(fdt))
		return -FDT_ERR_TRUNCATED;
	err = fdt_check_header(fdt);
	if (err != 0)
		return err;
	if (bufsize < fdt_totalsize(fdt))
		return -FDT_ERR_TRUNCATED;

	num_memrsv = fdt_num_mem_rsv(fdt);
	if (num_memrsv < 0)
		return num_memrsv;

	while (1) {
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);

		if (nextoffset < 0)
			return nextoffset;

		/* If we see two root nodes, something is wrong */
		if (expect_end && tag != FDT_END && tag != FDT_NOP)
			return -FDT_ERR_BADSTRUCTURE;

		switch (tag) {
		case FDT_NOP:
			break;

		case FDT_END:
			if (depth != 0)
				return -FDT_ERR_BADSTRUCTURE;
			return 0;

		case FDT_BEGIN_NODE:
			prop = NULL;
			subnode_found = false;
			depth++;
			if (depth > INT_MAX)
				return -FDT_ERR_BADSTRUCTURE;

			/* The root node must have an empty name */
			if (depth == 1) {
				const char *name;

				name = fdt_get_name(fdt, offset, &len);
				if (!name)
					return len;

				if (*name || len)
					return -FDT_ERR_BADSTRUCTURE;
			}
			break;

		case FDT_END_NODE:
			prop = NULL;
			subnode_found = true;
			if (depth == 0)
				return -FDT_ERR_BADSTRUCTURE;
			depth--;
			if (depth == 0)
				expect_end = true;
			break;

		case FDT_PROP:
			prop = fdt_getprop_by_offset(fdt, offset, &propname,
						     &proplen);
			if (!prop)
				return proplen;
			break;

		case FDT_PROPDATA_PHANDLE:
			/* Be sure that a property is available before this tag */
			if (!prop)
				return -FDT_ERR_BADSTRUCTURE;

			d = fdt_get_structured_tag_data(tag, fdt,
							offset + FDT_TAGSIZE, NULL);
			if (d < 0)
				return d;

			/* Retrieve the offset of the phandle in the property */
			pfdt32 = fdt_offset_ptr(fdt, d, sizeof(*pfdt32));
			if (!pfdt32)
				return -FDT_ERR_BADSTRUCTURE;
			tmp = fdt32_to_cpu(*pfdt32);

			/* Check this offset */
			if (tmp % sizeof(fdt32_t))
				return -FDT_ERR_BADVALUE;
			if ((uint32_t)proplen < tmp + sizeof(fdt32_t))
				return -FDT_ERR_BADSTRUCTURE;

			/* Retrieve the phandle value from the property */
			pfdt32 = prop;
			pfdt32 += tmp / sizeof(fdt32_t);
			val = fdt32_ld(pfdt32);

			/* Check the phandle value */
			tmp = fdt_node_offset_by_phandle(fdt, val);
			if (tmp < 0)
				return tmp;
			break;

		case FDT_PROPDATA_PHANDLE_REF:
			/* Be sure that a property is available before this tag */
			if (!prop)
				return -FDT_ERR_BADSTRUCTURE;

			d = fdt_get_structured_tag_data(tag, fdt,
							offset + FDT_TAGSIZE, NULL);
			if (d < 0)
				return d;

			/* Retrieve the offset of the phandle in the property */
			pfdt32 = fdt_offset_ptr(fdt, d, sizeof(*pfdt32));
			if (!pfdt32)
				return -FDT_ERR_BADSTRUCTURE;
			tmp = fdt32_to_cpu(*pfdt32);

			/* Check this offset */
			if (tmp % sizeof(fdt32_t))
				return -FDT_ERR_BADVALUE;
			if ((uint32_t)proplen < tmp + sizeof(fdt32_t))
				return -FDT_ERR_BADSTRUCTURE;

			/* Retrieve the reference */
			d += sizeof(fdt32_t);
			str = fdt_offset_ptr(fdt, d, 1);
			if (!str)
				return -FDT_ERR_BADSTRUCTURE;

			/* Check that the reference is not an empty string */
			if (!strlen(str))
				return -FDT_ERR_BADVALUE;
			break;

		case FDT_EXPORT_SYM:
			/* Export symbols must be in a node */
			if (depth == 0)
				return -FDT_ERR_BADSTRUCTURE;

			/* and before any properties or subnodes */
			if (prop || subnode_found)
				return -FDT_ERR_BADSTRUCTURE;

			d = fdt_get_structured_tag_data(tag, fdt,
							offset + FDT_TAGSIZE, NULL);
			if (d < 0)
				return d;

			/* Retrieve the phandle value */
			pfdt32 = fdt_offset_ptr(fdt, d, sizeof(*pfdt32));
			if (!pfdt32)
				return -FDT_ERR_BADSTRUCTURE;
			val = fdt32_ld(pfdt32);

			/* Check the phandle value */
			tmp = fdt_node_offset_by_phandle(fdt, val);
			if (tmp < 0)
				return tmp;

			/* Retrieve the export symbol name */
			d += sizeof(fdt32_t);
			str = fdt_offset_ptr(fdt, d, 1);
			if (!str)
				return -FDT_ERR_BADSTRUCTURE;

			/* Check that the name is not an empty string */
			if (!strlen(str))
				return -FDT_ERR_BADVALUE;

			break;

		case FDT_EXPORT_SYM_REF:
			/* Export symbols must be in a node */
			if (depth == 0)
				return -FDT_ERR_BADSTRUCTURE;

			/* and before any properties or subnodes */
			if (prop || subnode_found)
				return -FDT_ERR_BADSTRUCTURE;

			d = fdt_get_structured_tag_data(tag, fdt,
							offset + FDT_TAGSIZE, NULL);
			if (d < 0)
				return d;

			/* Retrieve the reference */
			str = fdt_offset_ptr(fdt, d, 1);
			if (!str)
				return -FDT_ERR_BADSTRUCTURE;

			/* Check that the reference is not an empty string */
			len = strlen(str);
			if (!len)
				return -FDT_ERR_BADVALUE;

			/* Retrieve the export symbol name */
			d += len + 1;
			str = fdt_offset_ptr(fdt, d, 1);
			if (!str)
				return -FDT_ERR_BADSTRUCTURE;

			/* Check that the name is not an empty string */
			if (!strlen(str))
				return -FDT_ERR_BADVALUE;

			break;

		default:
			return -FDT_ERR_INTERNAL;
		}
	}
}
