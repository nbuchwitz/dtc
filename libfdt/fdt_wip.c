// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

int fdt_setprop_inplace_namelen_partial(void *fdt, int nodeoffset,
					const char *name, int namelen,
					uint32_t idx, const void *val,
					int len)
{
	int prop_offset;
	void *propval;
	int proplen;
	int err;

	prop_offset = fdt_getprop_offset_namelen(fdt, nodeoffset, name, namelen);
	if (prop_offset < 0)
		return prop_offset;

	propval = fdt_getprop_by_offset_w(fdt, prop_offset, NULL, &proplen);
	if (!propval)
		return proplen;

	if ((unsigned)proplen < (len + idx))
		return -FDT_ERR_NOSPACE;

	if (idx < (unsigned)proplen) {
		/*
		 * The property existing data are going to be modified.
		 * The property metadata tags are related to the property
		 * data. Remove them.
		 */
		err = fdt_prop_remove_metadata_tags(fdt, true, prop_offset);
		if (err)
			return err;
	}

	/*
	 * Remove unknown tags related to the property downgrading the dtb
	 * version.
	 */
	err = fdt_prop_remove_unknown_tags(fdt, true, prop_offset, true);
	if (err)
		return err;

	memcpy((char *)propval + idx, val, len);
	return 0;
}

int fdt_setprop_inplace(void *fdt, int nodeoffset, const char *name,
			const void *val, int len)
{
	const void *propval;
	int proplen;

	propval = fdt_getprop(fdt, nodeoffset, name, &proplen);
	if (!propval)
		return proplen;

	if (proplen != len)
		return -FDT_ERR_NOSPACE;

	return fdt_setprop_inplace_namelen_partial(fdt, nodeoffset, name,
						   strlen(name), 0,
						   val, len);
}

static void fdt_nop_region_(void *start, int len)
{
	fdt32_t *p;

	for (p = start; (char *)p < ((char *)start + len); p++)
		*p = cpu_to_fdt32(FDT_NOP);
}

int fdt_nop_property(void *fdt, int nodeoffset, const char *name)
{
	struct fdt_property *prop;
	int prop_offset;
	int len;
	int err;

	prop = fdt_get_property_w(fdt, nodeoffset, name, &len);
	if (!prop)
		return len;

	/*
	 * The property is going to be removed (nopified). Remove its metadata
	 * tags
	 */
	prop_offset = fdt_ptr_offset_(fdt, prop);
	err = fdt_prop_remove_metadata_tags(fdt, true, prop_offset);
	if (err)
		return err;

	/*
	 * Remove also possible unknown tags related to this property.
	 * Keep the dtb version unchanged.
	 */
	err = fdt_prop_remove_unknown_tags(fdt, true, prop_offset, false);
	if (err)
		return err;

	fdt_nop_region_(prop, len + sizeof(*prop));

	return 0;
}

int fdt_node_end_offset_(void *fdt, int offset)
{
	int depth = 0;

	while ((offset >= 0) && (depth >= 0))
		offset = fdt_next_node(fdt, offset, &depth);

	return offset;
}

int fdt_nop_node(void *fdt, int nodeoffset)
{
	int endoffset;

	endoffset = fdt_node_end_offset_(fdt, nodeoffset);
	if (endoffset < 0)
		return endoffset;

	fdt_nop_region_(fdt_offset_ptr_w(fdt, nodeoffset, 0),
			endoffset - nodeoffset);
	return 0;
}
