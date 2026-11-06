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
static const char usage_short_opts[] = "i:o:t:e:p:v" USAGE_COMMON_SHORT_OPTS;
static const struct option usage_long_opts[] = {
	{ "input", required_argument, NULL, 'i' },
	{ "output", required_argument, NULL, 'o' },
	{ "target", required_argument, NULL, 't' },
	{ "export", required_argument, NULL, 'e' },
	{ "param", required_argument, NULL, 'p' },
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
					       "Set an addon parameter before applying, <name>=<value>"
					       "\n\tMultiple --param or -p can be present.",
					       "Verbose messages",
					       USAGE_COMMON_OPTS_HELP };

struct param {
	char *name;
	char *value;
	struct param *next;
};

static struct param *params;

static void add_param(const char *arg)
{
	struct param *p = xmalloc(sizeof(*p));
	char *eq;

	p->name = xstrdup(arg);
	eq = strchr(p->name, '=');
	if (!eq)
		usage("--param expects <name>=<value>");
	*eq = '\0';
	p->value = eq + 1;
	p->next = params;
	params = p;
}

static int is_true(const char *s)
{
	return !strcmp(s, "") || !strcmp(s, "1") || !strcmp(s, "on") ||
	       !strcmp(s, "yes") || !strcmp(s, "true") || !strcmp(s, "okay");
}

static int is_false(const char *s)
{
	return !strcmp(s, "0") || !strcmp(s, "off") || !strcmp(s, "no") ||
	       !strcmp(s, "false") || !strcmp(s, "disabled");
}

/* Encode the value according to the type dtc recorded for the default */
static int apply_param(void *fdta, struct param *p, int verbose)
{
	char buf[1024], *end;
	const char *s, *prop;
	uint32_t type;
	int len = 0, node, ret;

	ret = fdt_addon_param_lookup(fdta, p->name, 0, &node, &prop, &type);
	if (ret) {
		fprintf(stderr, "Unknown parameter '%s': %s\n", p->name,
			fdt_strerror(ret));
		return ret;
	}

	switch (type) {
	case FDT_PARAM_TYPE_U32:
	case FDT_PARAM_TYPE_U64:
		for (s = p->value; *s; s = end) {
			unsigned long long v = strtoull(s, &end, 0);

			if (end == s) {
				fprintf(stderr, "%s: bad number '%s'\n", p->name, s);
				return -1;
			}
			if (type == FDT_PARAM_TYPE_U32) {
				fdt32_t c = cpu_to_fdt32(v);

				memcpy(buf + len, &c, sizeof(c));
				len += sizeof(c);
			} else {
				fdt64_t c = cpu_to_fdt64(v);

				memcpy(buf + len, &c, sizeof(c));
				len += sizeof(c);
			}
			while (*end == ' ' || *end == ',')
				end++;
		}
		break;
	case FDT_PARAM_TYPE_STRING:
		s = p->value;
		if (!strcmp(prop, "status"))
			s = is_false(s) ? "disabled" : is_true(s) ? "okay" : s;
		len = strlen(s) + 1;
		memcpy(buf, s, len);
		break;
	case FDT_PARAM_TYPE_BOOL:
		if (is_false(p->value))
			len = -1;
		else if (!is_true(p->value)) {
			fprintf(stderr, "%s: expected a boolean\n", p->name);
			return -1;
		}
		break;
	default:
		fprintf(stderr, "%s: untyped parameter not supported\n", p->name);
		return -1;
	}

	if (verbose)
		printf("param %s=%s\n", p->name, p->value);

	ret = fdt_addon_set_param(fdta, p->name, buf, len);
	if (ret)
		fprintf(stderr, "Failed to set '%s': %s\n", p->name,
			fdt_strerror(ret));
	return ret;
}

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

	tmp_addon = xmalloc(fdt_totalsize(addon) + 4096);
	ret = fdt_open_into(addon, tmp_addon, fdt_totalsize(addon) + 4096);
	if (ret) {
		fprintf(stderr, "\nFailed to copy addon: %s\n", fdt_strerror(ret));
		goto fail;
	}

	for (struct param *p = params; p; p = p->next)
		if (apply_param(tmp_addon, p, resolver ? resolver->verbose : 0))
			goto fail;

	/* The merged size must also cover what the params added */
	max_merged_size += 4096;
	tmp_merged = xrealloc(tmp_merged, max_merged_size);
	fdt_set_totalsize(tmp_merged, max_merged_size);

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
	int verbose = 0;
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
		case 'p':
			add_param(optarg);
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
