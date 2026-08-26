// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fdtdump.c - Contributed by Pantelis Antoniou <pantelis.antoniou AT gmail.com>
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include <libfdt.h>
#include <libfdt_env.h>
#include <fdt.h>

#include "util.h"

#define FDT_MAGIC_SIZE	4
#define MAX_VERSION 20U

#define ALIGN(x, a)	(((x) + ((a) - 1)) & ~((a) - 1))
#define PALIGN(p, a)	((void *)(ALIGN((uintptr_t)(p), (a))))
#define GET_CELL(p)	(p += 4, *((const fdt32_t *)(p-4)))

static const char *tagname(uint32_t tag)
{
	static const char * const names[] = {
#define TN(t) [t] = #t
		TN(FDT_BEGIN_NODE),
		TN(FDT_END_NODE),
		TN(FDT_PROP),
		TN(FDT_NOP),
		TN(FDT_END),
#undef TN
	};
	if (tag < ARRAY_SIZE(names))
		if (names[tag])
			return names[tag];
	return "FDT_???";
}

#define dumpf(fmt, args...) \
	do { if (debug) printf("// " fmt, ## args); } while (0)

static const char *get_structured_tag_data(uint32_t tag, const char *blob,
					   const char **data, int *data_size)
{
	int sz = 0;

	switch (tag & FDT_TAG_DATA_MASK) {
	case FDT_TAG_DATA_NONE:
		break;
	case FDT_TAG_DATA_1CELL:
		sz = FDT_CELLSIZE;
		break;
	case FDT_TAG_DATA_2CELLS:
		sz = 2 * FDT_CELLSIZE;
		break;
	case FDT_TAG_DATA_VARLEN:
		/* Get the length */
		sz = fdt32_to_cpu(GET_CELL(blob));
		break;
	}

	*data_size = sz;
	*data = sz ? blob : NULL;

	/* Skip the data bytes */
	return PALIGN(blob + sz, 4);
}

static void dump_blob(void *blob, bool debug, int dump_unknown)
{
	uintptr_t blob_off = (uintptr_t)blob;
	struct fdt_header *bph = blob;
	uint32_t off_mem_rsvmap = fdt32_to_cpu(bph->off_mem_rsvmap);
	uint32_t off_dt = fdt32_to_cpu(bph->off_dt_struct);
	uint32_t off_str = fdt32_to_cpu(bph->off_dt_strings);
	struct fdt_reserve_entry *p_rsvmap =
		(struct fdt_reserve_entry *)((char *)blob + off_mem_rsvmap);
	const char *p_struct = (const char *)blob + off_dt;
	const char *p_strings = (const char *)blob + off_str;
	uint32_t version = fdt32_to_cpu(bph->version);
	uint32_t totalsize = fdt32_to_cpu(bph->totalsize);
	uint32_t tag, offset, val32;
	const char *p, *d, *s, *t;
	const char *last_prop_name = NULL;
	int depth, sz, shift;
	int i;
	uint64_t addr, size;

	depth = 0;
	shift = 4;

	printf("/dts-v1/;\n");
	printf("// magic:\t\t0x%"PRIx32"\n", fdt32_to_cpu(bph->magic));
	printf("// totalsize:\t\t0x%"PRIx32" (%"PRIu32")\n",
	       totalsize, totalsize);
	printf("// off_dt_struct:\t0x%"PRIx32"\n", off_dt);
	printf("// off_dt_strings:\t0x%"PRIx32"\n", off_str);
	printf("// off_mem_rsvmap:\t0x%"PRIx32"\n", off_mem_rsvmap);
	printf("// version:\t\t%"PRIu32"\n", version);
	printf("// last_comp_version:\t%"PRIu32"\n",
	       fdt32_to_cpu(bph->last_comp_version));
	if (version >= 2)
		printf("// boot_cpuid_phys:\t0x%"PRIx32"\n",
		       fdt32_to_cpu(bph->boot_cpuid_phys));

	if (version >= 3)
		printf("// size_dt_strings:\t0x%"PRIx32"\n",
		       fdt32_to_cpu(bph->size_dt_strings));
	if (version >= 17)
		printf("// size_dt_struct:\t0x%"PRIx32"\n",
		       fdt32_to_cpu(bph->size_dt_struct));
	if (version >= 18) {
		printf("// dt_flags:\t\t0x%"PRIx32"\n",
		       fdt32_to_cpu(bph->dt_flags));
		printf("// last_comp_version_w:\t%"PRIu32"\n",
		       fdt32_to_cpu(bph->last_comp_version_w));
	}
	if (version >= 18) {
		if (fdt32_to_cpu(bph->dt_flags) & FDT_FLAG_ADDON)
			printf("/addon/;\n");
	}
	printf("\n");

	for (i = 0; ; i++) {
		addr = fdt64_to_cpu(p_rsvmap[i].address);
		size = fdt64_to_cpu(p_rsvmap[i].size);
		if (addr == 0 && size == 0)
			break;

		printf("/memreserve/ %#"PRIx64" %#"PRIx64";\n",
		       addr, size);
	}

	p = p_struct;
	while ((tag = fdt32_to_cpu(GET_CELL(p))) != FDT_END) {

		dumpf("%04"PRIxPTR": tag: 0x%08"PRIx32" (%s)\n",
		        (uintptr_t)p - blob_off - 4, tag, tagname(tag));

		if (tag == FDT_BEGIN_NODE) {
			last_prop_name = NULL;
			s = p;
			p = PALIGN(p + strlen(s) + 1, 4);

			if (*s == '\0')
				s = "/";

			printf("%*s%s {\n", depth * shift, "", s);

			depth++;
			continue;
		}

		if (tag == FDT_END_NODE) {
			last_prop_name = NULL;
			depth--;

			printf("%*s};\n", depth * shift, "");
			continue;
		}

		if (tag == FDT_NOP) {
			printf("%*s// [NOP]\n", depth * shift, "");
			continue;
		}

		if (tag == FDT_PROP) {
			sz = fdt32_to_cpu(GET_CELL(p));
			s = p_strings + fdt32_to_cpu(GET_CELL(p));
			if (version < 16 && sz >= 8)
				p = PALIGN(p, 8);
			t = p;

			p = PALIGN(p + sz, 4);

			dumpf("%04"PRIxPTR": string: %s\n", (uintptr_t)s - blob_off, s);
			dumpf("%04"PRIxPTR": value\n", (uintptr_t)t - blob_off);
			printf("%*s%s", depth * shift, "", s);
			utilfdt_print_data(t, sz);
			printf(";\n");
			last_prop_name = s;
			continue;
		}

		if (tag == FDT_PROPDATA_PHANDLE) {
			p = get_structured_tag_data(tag, p, &d, &sz);
			offset = fdt32_to_cpu(GET_CELL(d));

			printf("%*s// [FDT_PROPDATA_PHANDLE] %s[%"PRIu32"]\n", depth * shift, "",
				last_prop_name, offset);
			continue;
		}

		if (tag == FDT_PROPDATA_PHANDLE_REF) {
			p = get_structured_tag_data(tag, p, &d, &sz);
			offset = fdt32_to_cpu(GET_CELL(d));
			s = d;

			printf("%*s// [FDT_PROPDATA_PHANDLE_REF] %s[%"PRIu32"], ref = %s\n",
				depth * shift, "", last_prop_name, offset, s);
			continue;
		}

		if (tag == FDT_EXPORT_SYM) {
			p = get_structured_tag_data(tag, p, &d, &sz);
			val32 = fdt32_to_cpu(GET_CELL(d));
			s = d;
			printf("%*s// [FDT_EXPORT_SYM] '%s' -> phandle 0x%08"PRIx32"\n",
				depth * shift, "", s, val32);
			continue;
		}

		if (tag == FDT_EXPORT_SYM_REF) {
			p = get_structured_tag_data(tag, p, &d, &sz);
			t = d;
			s = d + strlen(t) + 1;
			printf("%*s// [FDT_EXPORT_SYM_REF] '%s' -> '%s'\n", depth * shift, "",
				s, t);
			continue;
		}

		if ((tag & FDT_TAG_STRUCTURED) && (tag & FDT_TAG_SKIP_SAFE)) {
			p = get_structured_tag_data(tag, p, &d, &sz);

			if (dump_unknown) {
				printf("%*s// Unknown tag ignored: 0x%08"PRIx32", data len %d",
				       depth * shift, "", tag, sz);
				if (dump_unknown > 1 && sz != 0) {
					printf(" ");
					for (i = 0; i < sz; i++)
						printf("%02hhx", *(d + i));
				}
				printf("\n");
			}
			continue;
		}

		die("** Unknown tag 0x%08"PRIx32"\n", tag);
	}
}

/* Usage related data. */
static const char usage_synopsis[] = "fdtdump [options] <file>";
static const char usage_short_opts[] = "dus" USAGE_COMMON_SHORT_OPTS;
static struct option const usage_long_opts[] = {
	{"debug",            no_argument, NULL, 'd'},
	{"unknown",          no_argument, NULL, 'u'},
	{"scan",             no_argument, NULL, 's'},
	USAGE_COMMON_LONG_OPTS
};
static const char * const usage_opts_help[] = {
	"Dump debug information while decoding the file",
	"Dump unknown tags information while decoding the file (-uu to dump data)",
	"Scan for an embedded fdt in file",
	USAGE_COMMON_OPTS_HELP
};

static bool valid_header(char *p, size_t len)
{
	if (len < sizeof(struct fdt_header) ||
	    fdt_magic(p) != FDT_MAGIC ||
	    fdt_last_comp_version(p) > MAX_VERSION ||
	    fdt_totalsize(p) >= len ||
	    fdt_off_dt_struct(p) >= len ||
	    fdt_off_dt_strings(p) >= len)
		return 0;
	else
		return 1;
}

int main(int argc, char *argv[])
{
	int opt;
	const char *file;
	char *buf;
	bool debug = false;
	int dump_unknown = 0;
	bool scan = false;
	size_t len;

	fprintf(stderr, "\n"
"**** fdtdump is a low-level debugging tool, not meant for general use.\n"
"**** If you want to decompile a dtb, you probably want\n"
"****     dtc -I dtb -O dts <filename>\n\n"
		);
	while ((opt = util_getopt_long()) != EOF) {
		switch (opt) {
		case_USAGE_COMMON_FLAGS

		case 'd':
			debug = true;
			break;
		case 'u':
			dump_unknown++;
			break;
		case 's':
			scan = true;
			break;
		}
	}
	if (optind != argc - 1)
		usage("missing input filename");
	file = argv[optind];

	buf = utilfdt_read(file, &len);
	if (!buf)
		die("could not read: %s\n", file);

	/* try and locate an embedded fdt in a bigger blob */
	if (scan) {
		unsigned char smagic[FDT_MAGIC_SIZE];
		char *p = buf;
		char *endp = buf + len;

		fdt32_st(smagic, FDT_MAGIC);

		/* poor man's memmem */
		while ((endp - p) >= FDT_MAGIC_SIZE) {
			p = memchr(p, smagic[0], endp - p - FDT_MAGIC_SIZE);
			if (!p)
				break;
			if (fdt_magic(p) == FDT_MAGIC) {
				/* try and validate the main struct */
				off_t this_len = endp - p;
				if (valid_header(p, this_len))
					break;
				if (debug)
					printf("%s: skipping fdt magic at offset %#tx\n",
						file, p - buf);
			}
			++p;
		}
		if (!p || (size_t)(endp - p) < sizeof(struct fdt_header))
			die("%s: could not locate fdt magic\n", file);
		printf("%s: found fdt at offset %#tx\n", file, p - buf);
		buf = p;
	} else if (!valid_header(buf, len))
		die("%s: header is not valid\n", file);

	dump_blob(buf, debug, dump_unknown);

	return 0;
}
