// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */
#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

/*
 * Minimal sanity check for a read-only tree. fdt_ro_probe_() checks
 * that the given buffer contains what appears to be a flattened
 * device tree with sane information in its header.
 */
int32_t fdt_ro_probe_(const void *fdt)
{
	uint32_t totalsize = fdt_totalsize(fdt);

	if (can_assume(VALID_DTB))
		return totalsize;

	/* The device tree must be at an 8-byte aligned address */
	if ((uintptr_t)fdt & 7)
		return -FDT_ERR_ALIGNMENT;

	if (fdt_magic(fdt) == FDT_MAGIC) {
		/* Complete tree */
		if (!can_assume(LATEST)) {
			if (fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
				return -FDT_ERR_BADVERSION;
			if (fdt_last_comp_version(fdt) >
					FDT_LAST_SUPPORTED_VERSION)
				return -FDT_ERR_BADVERSION;
		}
	} else if (fdt_magic(fdt) == FDT_SW_MAGIC) {
		/* Unfinished sequential-write blob */
		if (!can_assume(VALID_INPUT) && fdt_size_dt_struct(fdt) == 0)
			return -FDT_ERR_BADSTATE;
	} else {
		return -FDT_ERR_BADMAGIC;
	}

	if (totalsize < INT32_MAX)
		return totalsize;
	else
		return -FDT_ERR_TRUNCATED;
}

static int check_off_(uint32_t hdrsize, uint32_t totalsize, uint32_t off)
{
	return (off >= hdrsize) && (off <= totalsize);
}

static int check_block_(uint32_t hdrsize, uint32_t totalsize,
			uint32_t base, uint32_t size)
{
	if (!check_off_(hdrsize, totalsize, base))
		return 0; /* block start out of bounds */
	if ((base + size) < base)
		return 0; /* overflow */
	if (!check_off_(hdrsize, totalsize, base + size))
		return 0; /* block end out of bounds */
	return 1;
}

size_t fdt_header_size_(uint32_t version)
{
	if (version <= 1)
		return FDT_V1_SIZE;
	else if (version <= 2)
		return FDT_V2_SIZE;
	else if (version <= 3)
		return FDT_V3_SIZE;
	else if (version <= 16)
		return FDT_V16_SIZE;
	else
		return FDT_V17_SIZE;
}

size_t fdt_header_size(const void *fdt)
{
	return can_assume(LATEST) ? FDT_V17_SIZE :
		fdt_header_size_(fdt_version(fdt));
}

int fdt_check_header(const void *fdt)
{
	size_t hdrsize;

	/* The device tree must be at an 8-byte aligned address */
	if ((uintptr_t)fdt & 7)
		return -FDT_ERR_ALIGNMENT;

	if (fdt_magic(fdt) != FDT_MAGIC)
		return -FDT_ERR_BADMAGIC;
	if (!can_assume(LATEST)) {
		if ((fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
		    || (fdt_last_comp_version(fdt) >
			FDT_LAST_SUPPORTED_VERSION))
			return -FDT_ERR_BADVERSION;
		if (fdt_version(fdt) < fdt_last_comp_version(fdt))
			return -FDT_ERR_BADVERSION;
	}
	hdrsize = fdt_header_size(fdt);
	if (!can_assume(VALID_DTB)) {
		if ((fdt_totalsize(fdt) < hdrsize)
		    || (fdt_totalsize(fdt) > INT_MAX))
			return -FDT_ERR_TRUNCATED;

		/* memrsv block must be 8 byte aligned */
		if (fdt_off_mem_rsvmap(fdt) % sizeof(uint64_t))
			return -FDT_ERR_ALIGNMENT;

		/* Structure block must be 4 byte aligned */
		if (fdt_off_dt_struct(fdt) % FDT_TAGSIZE)
			return -FDT_ERR_ALIGNMENT;

		/* Bounds check memrsv block */
		if (!check_off_(hdrsize, fdt_totalsize(fdt),
				fdt_off_mem_rsvmap(fdt)))
			return -FDT_ERR_TRUNCATED;

		/* Bounds check structure block */
		if (!can_assume(LATEST) && fdt_version(fdt) < 17) {
			if (!check_off_(hdrsize, fdt_totalsize(fdt),
					fdt_off_dt_struct(fdt)))
				return -FDT_ERR_TRUNCATED;
		} else {
			if (!check_block_(hdrsize, fdt_totalsize(fdt),
					  fdt_off_dt_struct(fdt),
					  fdt_size_dt_struct(fdt)))
				return -FDT_ERR_TRUNCATED;
		}

		/* Bounds check strings block */
		if (!check_block_(hdrsize, fdt_totalsize(fdt),
				  fdt_off_dt_strings(fdt),
				  fdt_size_dt_strings(fdt)))
			return -FDT_ERR_TRUNCATED;
	}

	return 0;
}

const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int len)
{
	unsigned int uoffset = offset;
	unsigned int absoffset = offset + fdt_off_dt_struct(fdt);

	if (offset < 0)
		return NULL;

	if (!can_assume(VALID_INPUT))
		if ((absoffset < uoffset)
		    || ((absoffset + len) < absoffset)
		    || (absoffset + len) > fdt_totalsize(fdt))
			return NULL;

	if (can_assume(LATEST) || fdt_version(fdt) >= 0x11)
		if (((uoffset + len) < uoffset)
		    || ((offset + len) > fdt_size_dt_struct(fdt)))
			return NULL;

	return fdt_offset_ptr_(fdt, offset);
}

int fdt_get_structured_tag_data(uint32_t tag, const void *fdt, int offset,
				uint32_t *data_len)
{
	int data_offset = offset;
	uint32_t len = 0, sum;
	const fdt32_t *lenp;

	if (!(tag & FDT_TAG_STRUCTURED))
		return -FDT_ERR_INTERNAL;

	switch (tag & FDT_TAG_DATA_MASK) {
	case FDT_TAG_DATA_NONE:
		break;
	case FDT_TAG_DATA_1CELL:
		len = FDT_CELLSIZE;
		break;
	case FDT_TAG_DATA_2CELLS:
		len = 2 * FDT_CELLSIZE;
		break;
	case FDT_TAG_DATA_VARLEN:
		/* Get the length */
		lenp = fdt_offset_ptr(fdt, offset, sizeof(*lenp));
		if (!can_assume(VALID_DTB) && !lenp)
			return -FDT_ERR_BADSTRUCTURE;
		len = fdt32_to_cpu(*lenp);

		/* Skip the cell encoding the length */
		data_offset += sizeof(*lenp);

		/* Check for the length value */
		sum = len + data_offset;
		if (!can_assume(VALID_DTB) &&
		    (sum >= INT_MAX || sum < (uint32_t) offset))
			return -FDT_ERR_BADSTRUCTURE;

		break;
	}

	if (data_len)
		*data_len = len;

	return data_offset;
}

static uint32_t fdt_next_tag_all(const void *fdt, int startoffset, int *nextoffset)
{
	const fdt32_t *tagp, *lenp, *tmp32p;
	uint32_t tag, len, sum;
	int offset = startoffset;
	int tmp_offset;
	const char *p;

	*nextoffset = -FDT_ERR_TRUNCATED;
	tagp = fdt_offset_ptr(fdt, offset, FDT_TAGSIZE);
	if (!can_assume(VALID_DTB) && !tagp)
		return FDT_END; /* premature end */
	tag = fdt32_to_cpu(*tagp);
	offset += FDT_TAGSIZE;

	*nextoffset = -FDT_ERR_BADSTRUCTURE;
	switch (tag) {
	case FDT_BEGIN_NODE:
		/* skip name */
		do {
			p = fdt_offset_ptr(fdt, offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */
		break;

	case FDT_PROP:
		lenp = fdt_offset_ptr(fdt, offset, sizeof(*lenp));
		if (!can_assume(VALID_DTB) && !lenp)
			return FDT_END; /* premature end */

		len = fdt32_to_cpu(*lenp);
		sum = len + offset;
		if (!can_assume(VALID_DTB) &&
		    (INT_MAX <= sum || sum < (uint32_t) offset))
			return FDT_END; /* premature end */

		/* skip-name offset, length and value */
		offset += sizeof(struct fdt_property) - FDT_TAGSIZE + len;

		if (!can_assume(LATEST) &&
		    fdt_version(fdt) < 0x10 && len >= 8 &&
		    ((offset - len) % 8) != 0)
			offset += 4;
		break;

	case FDT_END:
	case FDT_END_NODE:
	case FDT_NOP:
		break;

	case FDT_PROPDATA_PHANDLE:
		tmp_offset = fdt_get_structured_tag_data(tag, fdt, offset, &len);
		if (tmp_offset < 0)
			return FDT_END; /* premature end */

		sum = tmp_offset + len;

		if (!can_assume(VALID_DTB) && len < FDT_CELLSIZE)
			return FDT_END; /* premature end */

		/* Check offset value */
		tmp32p = fdt_offset_ptr(fdt, tmp_offset, sizeof(*tmp32p));
		if (!can_assume(VALID_DTB) && !tmp32p)
			return FDT_END; /* premature end */

		/* Skip the whole data bloc */
		offset = sum;
		break;

	case FDT_PROPDATA_PHANDLE_REF:
		tmp_offset = fdt_get_structured_tag_data(tag, fdt, offset, &len);
		if (tmp_offset < 0)
			return FDT_END; /* premature end */

		sum = tmp_offset + len;

		/* Check offset value */
		tmp32p = fdt_offset_ptr(fdt, tmp_offset, sizeof(*tmp32p));
		if (!can_assume(VALID_DTB) && !tmp32p)
			return FDT_END; /* premature end */
		tmp_offset += sizeof(fdt32_t);

		/* Check ref string */
		do {
			p = fdt_offset_ptr(fdt, tmp_offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */

		if (!can_assume(VALID_INPUT) && (uint32_t)tmp_offset > sum)
			return FDT_END; /* premature end */

		/* Skip the whole data bloc */
		offset = sum;
		break;

	case FDT_EXPORT_SYM:
		tmp_offset = fdt_get_structured_tag_data(tag, fdt, offset, &len);
		if (tmp_offset < 0)
			return FDT_END; /* premature end */

		sum = tmp_offset + len;

		/* Check phandle */
		tmp32p = fdt_offset_ptr(fdt, tmp_offset, sizeof(*tmp32p));
		if (!can_assume(VALID_DTB) && !tmp32p)
			return FDT_END; /* premature end */
		tmp_offset += sizeof(fdt32_t);

		/* Check name */
		do {
			p = fdt_offset_ptr(fdt, tmp_offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */

		if (!can_assume(VALID_INPUT) && (uint32_t)tmp_offset > sum)
			return FDT_END; /* premature end */

		/* Skip the whole data bloc */
		offset = sum;
		break;

	case FDT_EXPORT_SYM_REF:
		tmp_offset = fdt_get_structured_tag_data(tag, fdt, offset, &len);
		if (tmp_offset < 0)
			return FDT_END; /* premature end */

		sum = tmp_offset + len;

		/* Check ref */
		do {
			p = fdt_offset_ptr(fdt, tmp_offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */

		/* Check name */
		do {
			p = fdt_offset_ptr(fdt, tmp_offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */

		if (!can_assume(VALID_INPUT) && (uint32_t)tmp_offset > sum)
			return FDT_END; /* premature end */

		/* Skip the whole data bloc */
		offset = sum;
		break;

	case FDT_IMPORT_SYM:
		tmp_offset = fdt_get_structured_tag_data(tag, fdt, offset, &len);
		if (tmp_offset < 0)
			return FDT_END; /* premature end */

		sum = tmp_offset + len;

		/* Check name */
		do {
			p = fdt_offset_ptr(fdt, tmp_offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */

		/* Check compatible */
		do {
			p = fdt_offset_ptr(fdt, tmp_offset++, 1);
		} while (p && (*p != '\0'));
		if (!can_assume(VALID_DTB) && !p)
			return FDT_END; /* premature end */

		if (!can_assume(VALID_INPUT) && (uint32_t)tmp_offset > sum)
			return FDT_END; /* premature end */

		/* Skip the whole data bloc */
		offset = sum;
		break;

	default:
		if (!(tag & FDT_TAG_STRUCTURED) || !(tag & FDT_TAG_SKIP_SAFE))
			return FDT_END;

		tmp_offset = fdt_get_structured_tag_data(tag, fdt, offset, &len);
		if (tmp_offset < 0)
			return FDT_END; /* premature end */

		/* Skip the whole data bloc */
		offset = tmp_offset + len;
		break;
	}

	if (!fdt_offset_ptr(fdt, startoffset, offset - startoffset))
		return FDT_END; /* premature end */

	*nextoffset = FDT_TAGALIGN(offset);
	return tag;
}

static bool fdt_tag_is_unknown(uint32_t tag)
{
	switch (tag) {
	case FDT_BEGIN_NODE:
	case FDT_END_NODE:
	case FDT_PROP:
	case FDT_NOP:
	case FDT_END:
	case FDT_PROPDATA_PHANDLE:
	case FDT_PROPDATA_PHANDLE_REF:
	case FDT_EXPORT_SYM:
	case FDT_EXPORT_SYM_REF:
	case FDT_IMPORT_SYM:
		return false;
	default:
		break;
	}
	return true;
}

uint32_t fdt_next_tag_(const void *fdt, int startoffset, int *nextoffset, bool *is_unknown)
{
	uint32_t tag;
	bool unknown = false;

	/* Retrieve next tag */
	tag = fdt_next_tag_all(fdt, startoffset, nextoffset);
	if (tag == FDT_END)
		goto end;

	if (fdt_tag_is_unknown(tag)) {
		unknown = true;
		/* Use a known tag that should be skipped by the caller */
		tag = FDT_NOP;
	}
end:
	if (is_unknown)
		*is_unknown = unknown;
	return tag;
}

uint32_t fdt_next_tag(const void *fdt, int startoffset, int *nextoffset)
{
	return fdt_next_tag_(fdt, startoffset, nextoffset, NULL);
}

uint32_t fdt_next_tag_filter(const void *fdt, int offset, int *nextoffset,
			     bool (*tag_filter)(void *priv, uint32_t tag),
			     void *priv)
{
	int startoffset = offset;
	uint32_t tag;

	tag = fdt_next_tag_(fdt, startoffset, nextoffset, NULL);
	if (tag == FDT_END)
		return tag;

	if (tag_filter(priv, tag))
		return FDT_NOP;

	return tag;
}

bool fdt_tag_filter_skip_metadata(void *data, uint32_t tag)
{
	switch (tag) {
	case FDT_PROPDATA_PHANDLE:
	case FDT_PROPDATA_PHANDLE_REF:
	case FDT_EXPORT_SYM:
	case FDT_EXPORT_SYM_REF:
	case FDT_IMPORT_SYM:
		return true;

	default:
		break;
	}

	return false;
}

bool fdt_tag_is_property_metadata(uint32_t tag)
{
	switch (tag) {
	case FDT_PROPDATA_PHANDLE:
	case FDT_PROPDATA_PHANDLE_REF:
		return true;
	default:
		break;
	}

	return false;
}

int fdt_check_node_offset_(const void *fdt, int offset)
{
	if (!can_assume(VALID_INPUT)
	    && ((offset < 0) || (offset % FDT_TAGSIZE)))
		return -FDT_ERR_BADOFFSET;

	if (fdt_next_tag(fdt, offset, &offset) != FDT_BEGIN_NODE)
		return -FDT_ERR_BADOFFSET;

	return offset;
}

int fdt_check_prop_offset_(const void *fdt, int offset)
{
	if (!can_assume(VALID_INPUT)
	    && ((offset < 0) || (offset % FDT_TAGSIZE)))
		return -FDT_ERR_BADOFFSET;

	if (fdt_next_tag(fdt, offset, &offset) != FDT_PROP)
		return -FDT_ERR_BADOFFSET;

	return offset;
}

int fdt_root_offset(const void *fdt)
{
	int nextoffset = 0;
	int offset;
	uint32_t tag;

	do {
		offset = nextoffset;
		tag = fdt_next_tag_filter(fdt, offset, &nextoffset,
					  fdt_tag_filter_skip_metadata, NULL);
		switch (tag) {
		case FDT_END_NODE:
		case FDT_PROP:
			return -FDT_ERR_BADSTRUCTURE;

		case FDT_BEGIN_NODE:
			return offset;

		default:
			break;
		}
	} while (tag != FDT_END);

	return (nextoffset < 0) ? nextoffset : -FDT_ERR_NOTFOUND;
}

int fdt_next_node(const void *fdt, int offset, int *depth)
{
	int nextoffset = offset;
	uint32_t tag;

	/*
	 * Get the root node if asked for next node from the root node
	 * (offset == 0) or if the given offset is not valid (negative).
	 */
	if (offset <= 0) {
		nextoffset = fdt_root_offset(fdt);
		if (nextoffset < 0)
			return nextoffset;
	}

	if (offset >= 0)
		if ((nextoffset = fdt_check_node_offset_(fdt, nextoffset)) < 0)
			return nextoffset;

	do {
		offset = nextoffset;
		tag = fdt_next_tag_filter(fdt, offset, &nextoffset,
					  fdt_tag_filter_skip_metadata, NULL);
		switch (tag) {
		case FDT_PROP:
		case FDT_NOP:
			break;

		case FDT_BEGIN_NODE:
			if (depth)
				(*depth)++;
			break;

		case FDT_END_NODE:
			if (depth && ((--(*depth)) < 0))
				return nextoffset;
			break;

		case FDT_END:
			if ((nextoffset >= 0)
			    || ((nextoffset == -FDT_ERR_TRUNCATED) && !depth))
				return -FDT_ERR_NOTFOUND;
			else
				return nextoffset;
		}
	} while (tag != FDT_BEGIN_NODE);

	return offset;
}

int fdt_first_subnode(const void *fdt, int offset)
{
	int depth = 0;

	offset = fdt_next_node(fdt, offset, &depth);
	if (offset < 0 || depth != 1)
		return -FDT_ERR_NOTFOUND;

	return offset;
}

int fdt_next_subnode(const void *fdt, int offset)
{
	int depth = 1;

	/*
	 * With respect to the parent, the depth of the next subnode will be
	 * the same as the last.
	 */
	do {
		offset = fdt_next_node(fdt, offset, &depth);
		if (offset < 0 || depth < 1)
			return -FDT_ERR_NOTFOUND;
	} while (depth > 1);

	return offset;
}

const char *fdt_find_string_len_(const char *strtab, int tabsize, const char *s,
				 int slen)
{
	const char *last = strtab + tabsize - (slen + 1);
	const char *p;

	for (p = strtab; p <= last; p++)
		if (memcmp(p, s, slen) == 0 && p[slen] == '\0')
			return p;
	return NULL;
}

int fdt_move(const void *fdt, void *buf, int bufsize)
{
	if (!can_assume(VALID_INPUT) && bufsize < 0)
		return -FDT_ERR_NOSPACE;

	FDT_RO_PROBE(fdt);

	if (fdt_totalsize(fdt) > (unsigned int)bufsize)
		return -FDT_ERR_NOSPACE;

	memmove(buf, fdt, fdt_totalsize(fdt));
	return 0;
}
