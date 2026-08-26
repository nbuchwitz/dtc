// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 Bootlin. All rights reserved.
 *
 * Author:
 *	 Herve Codina <herve.codina@bootlin.com>
 */

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libfdt.h>

#include "util.h"

/* Usage related data. */
static const char usage_synopsis[] =
	"apply an addon to a base blob\n"
	"	fdtaddon <options> <addon.dtba>";
static const char usage_short_opts[] = "i:o:t:v" USAGE_COMMON_SHORT_OPTS;
static const struct option usage_long_opts[] = {
	{ "input", required_argument, NULL, 'i' },
	{ "output", required_argument, NULL, 'o' },
	{ "target", required_argument, NULL, 't' },
	{ "verbose", no_argument, NULL, 'v' },
	USAGE_COMMON_LONG_OPTS,
};
static const char *const usage_opts_help[] = { "Input base DT blob",
					       "Output DT blob", "Target node",
					       "Verbose messages",
					       USAGE_COMMON_OPTS_HELP };

static void *do_apply(void *base, const void *addon, const char *target)
{
	void *tmp_merged;
	void *tmp_addon;
	size_t max_merged_size;
	int ret;

	/*
	 * We take copies first, because a failed apply can trash
	 * both the base blob and the overlay.
	 */

	/*
	 * The merged size should not be greater than the sum of the size of
	 * individual items.
	 */
	max_merged_size = fdt_totalsize(base) + fdt_totalsize(addon);

	tmp_merged = xmalloc(max_merged_size);
	ret = fdt_open_into(base, tmp_merged, max_merged_size);
	if (ret) {
		fprintf(stderr,
			"\nFailed to make temporary copy: %s\n",
			fdt_strerror(ret));
		free(tmp_merged);
		return NULL;
	}

	tmp_addon = xmalloc(fdt_totalsize(addon));
	memcpy(tmp_addon, addon, fdt_totalsize(addon));

	if (!(fdt_dt_flags(tmp_addon) & FDT_FLAG_ADDON)) {
		fprintf(stderr,
			"\nAddon dtb is not an 'addon'\n");
		goto fail;
	}

	ret = fdt_addon_apply(tmp_merged, tmp_addon, target);
	if (ret) {
		fprintf(stderr, "\nFailed to apply %s\n", fdt_strerror(ret));
		goto fail;
	}

	free(tmp_addon);
	return tmp_merged;

fail:
	free(tmp_merged);
	free(tmp_addon);
	return NULL;
}

static int do_fdtaddon(const char *input_filename, const char *output_filename,
		       const char *addon_filename, const char *target)
{
	void *base_blob = NULL;
	void *addon_blob = NULL;
	void *merged_blob = NULL;
	size_t base_buflen;
	size_t addon_buflen;
	int ret = -1;

	base_blob = utilfdt_read(input_filename, &base_buflen);
	if (!base_blob) {
		fprintf(stderr, "\nFailed to read '%s'\n", input_filename);
		goto out_err;
	}
	if (fdt_totalsize(base_blob) > base_buflen) {
		fprintf(stderr,
			"\nBase blob is incomplete (%zu / %"PRIu32" bytes read)\n",
			base_buflen, fdt_totalsize(base_blob));
		goto out_err;
	}

	addon_blob = utilfdt_read(addon_filename, &addon_buflen);
	if (!addon_blob) {
		fprintf(stderr, "\nFailed to read '%s'\n", addon_filename);
		goto out_err;
	}
	if (fdt_totalsize(addon_blob) > addon_buflen) {
		fprintf(stderr,
			"\nAddon blob is incomplete (%zu / %"PRIu32" bytes read)\n",
			addon_buflen, fdt_totalsize(addon_blob));
		goto out_err;
	}

	/* apply the addon  */
	merged_blob = do_apply(base_blob, addon_blob, target);
	if (!merged_blob)
		goto out_err;

	fdt_pack(merged_blob);
	ret = utilfdt_write(output_filename, merged_blob);
	if (ret)
		fprintf(stderr, "\nFailed to write '%s'\n", output_filename);

out_err:
	free(merged_blob);
	free(addon_blob);
	free(base_blob);

	return ret;
}

int main(int argc, char *argv[])
{
	char *input_filename = NULL;
	char *output_filename = NULL;
	char *addon_filename = NULL;
	const char *target = NULL;
	int verbose;
	int opt;

	while ((opt = util_getopt_long()) != EOF) {
		switch (opt) {
		case_USAGE_COMMON_FLAGS

		case 'i':
			input_filename = optarg;
			break;
		case 'o':
			output_filename = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 't':
			target = optarg;
			break;
		}
	}

	if (!input_filename)
		usage("missing input file");

	if (!output_filename)
		usage("missing output file");

	if (!target)
		usage("missing target");

	argv += optind;
	argc -= optind;

	if (argc != 1)
		usage("missing addon file");

	addon_filename = argv[0];

	if (verbose) {
		printf("input  = %s\n", input_filename);
		printf("output = %s\n", output_filename);
		printf("addon = %s\n", addon_filename);
	}

	if (do_fdtaddon(input_filename, output_filename, addon_filename, target))
		return 1;

	return 0;
}
