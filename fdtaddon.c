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
static const char usage_short_opts[] = "i:o:t:e:v" USAGE_COMMON_SHORT_OPTS;
static const struct option usage_long_opts[] = {
	{ "input", required_argument, NULL, 'i' },
	{ "output", required_argument, NULL, 'o' },
	{ "target", required_argument, NULL, 't' },
	{ "export", required_argument, NULL, 'e' },
	{ "verbose", no_argument, NULL, 'v' },
	USAGE_COMMON_LONG_OPTS,
};
static const char *const usage_opts_help[] = { "Input base DT blob",
					       "Output DT blob",
					       "Target node",
					       "Custom export symbol"
					       "\n\tMultiple --export or -e can be present in order to export multiple symbols."
					       "\n\tThe following syntaxes are supported:"
					       "\n\t  - Defines a export symbol pointing to a node using its symbol"
					       "\n\t      --export '<export_name>=&<symbol>'"
					       "\n\t  - Defines a export symbol pointing to a node by path"
					       "\n\t      --export '<export_name>=&{path_to_node}'",
					       "Verbose messages",
					       USAGE_COMMON_OPTS_HELP };

struct export_symbol {
	char *name;
	char *ref;
	struct export_symbol *next;
};

struct resolver {
	struct export_symbol *export_list;
	int verbose;
};

static void resolver_init(struct resolver *resolver)
{
	memset(resolver, 0, sizeof(*resolver));
}

static void resolver_exit(struct resolver *resolver)
{
	struct export_symbol *e, *next;

	e = resolver->export_list;
	while (e) {
		free(e->name);
		free(e->ref);
		next = e->next;
		free(e);
		e = next;
	}
}

static char *str_strip_spaces(char *str)
{
	char *strip, *tmp;

	tmp = str;
	strip = str;
	while (*tmp != '\0') {
		if (*tmp != ' ' && *tmp != '\t')
			*(strip++) = *tmp;
		tmp++;
	}
	*strip = *tmp;
	return str;
}

static char *str_split(char *str, char token, char **remaining)
{
	char *tmp;

	tmp = strchr(str, token);
	if (!tmp)
		return NULL;
	*(tmp++) = '\0'; /* Split the string at token char */

	*remaining = tmp;
	return str;
}

static int resolver_parse_export(struct resolver *resolver, const char *arg)
{
	struct export_symbol *export_symbol;
	const char *name, *ref;
	char *tmp, *strip;

	strip = xstrdup(arg);
	strip = str_strip_spaces(strip);

	tmp = strip;

	name = str_split(tmp, '=', &tmp);
	if (!name || !strlen(name))
		goto fail;

	if (*(tmp++) != '&')
		goto fail;
	if (*tmp == '\0')
		goto fail;

	if (*tmp == '{') {
		/* ref by path : &{/xxxx/yyyy} */
		tmp++;
		if (*(tmp) != '/')
			goto fail;

		ref = str_split(tmp, '}', &tmp);
		if (!ref || !strlen(ref))
			goto fail;
		if (*tmp != '\0')
			goto fail;
	} else {
		/* ref by symbol : &xxxx */
		ref = tmp;
		if (!strlen(ref))
			goto fail;
	}

	export_symbol = xmalloc(sizeof(*export_symbol));
	export_symbol->name = xstrdup(name);
	export_symbol->ref = xstrdup(ref);
	free(strip);

	export_symbol->next = resolver->export_list;
	resolver->export_list = export_symbol;
	return 0;
fail:
	free(strip);
	return -1;
}

static int resolver_resolve(void *priv, const void *fdt, int target_node,
			    const char *importsym_name,
			    const char *importsym_compatible)
{
	struct resolver *resolver = priv;
	int node = -FDT_ERR_NOTFOUND;
	struct export_symbol *e;

	e = resolver->export_list;
	while (e) {
		if (strcmp(importsym_name, e->name)) {
			e = e->next;
			continue;
		}

		if (e->ref[0] == '/')
			node = fdt_path_offset(fdt, e->ref);
		else
			node = fdt_addon_resolve_default(fdt, target_node, e->ref,
							 NULL);

		return node;
	}
	return -FDT_ERR_NOTFOUND;
}

static int resolver_validate(void *priv, const void *fdt, int target_node)
{
	struct resolver *resolver = priv;
	struct export_symbol *e;
	uint32_t phandle;
	int node;

	/*
	 * Check the export_symbol list. The related node must exist in the fdt
	 * blob and must have a valid phandle value.
	 *
	 * Simply revolve all export symbols available in the list to validate
	 * that they can be resolved.
	 */

	e = resolver->export_list;
	while (e) {
		node = resolver_resolve(resolver, fdt, target_node, e->name,
					NULL);
		if (node < 0) {
			fprintf(stderr,
				"\nExport symbol '%s': Cannot find reference '%s'\n",
				e->name, e->ref);
			return node;
		}

		phandle = fdt_get_phandle(fdt, node);
		if ((phandle == 0) || (phandle == ~0U)) {
			fprintf(stderr,
				"\nExport symbol '%s', reference '%s': Invalid phandle\n",
				e->name, e->ref);
			return -FDT_ERR_BADPHANDLE;
		}

		if (resolver->verbose) {
			printf("Export symbol '%s', ref '%s' -> node at 0x%x, phandle 0x%"PRIx32"\n",
				e->name, e->ref, node, phandle);
		}

		e = e->next;
	}
	return 0;
}

static const struct fdt_addon_resolver_ops resolver_ops = {
	.validate = resolver_validate,
	.resolve = resolver_resolve,
};


static void *do_apply(void *base, const void *addon, const char *target,
		      struct resolver *resolver)
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

	if (resolver)
		ret = fdt_addon_apply_resolver(tmp_merged, tmp_addon, target,
					       &resolver_ops, resolver);
	else
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
		       const char *addon_filename, const char *target,
		       struct resolver *resolver)
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
	merged_blob = do_apply(base_blob, addon_blob, target, resolver);
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
	bool use_resolver = false;
	struct resolver resolver;
	int opt;
	int ret;

	resolver_init(&resolver);

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
		case 'e':
			if (resolver_parse_export(&resolver, optarg) < 0) {
				resolver_exit(&resolver);
				return 1;
			}
			use_resolver = true;
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

	resolver.verbose = verbose;

	ret = do_fdtaddon(input_filename, output_filename, addon_filename, target,
			  use_resolver ? &resolver : NULL);
	resolver_exit(&resolver);
	if (ret)
		return 1;

	return 0;
}
