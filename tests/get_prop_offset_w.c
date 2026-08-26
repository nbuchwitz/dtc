// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libfdt - Flat Device Tree manipulation
 *	Testcase for fdt_getprop_by_offset_w()
 * Copyright (C) 2026 Bootlin.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <libfdt.h>

#include "tests.h"
#include "testdata.h"

#define SPACE		65536

int main(int argc, char *argv[])
{
	const void *exp_val;
	const char *name;
	char *modif_val;
	void *propval;
	int proplen;
	int exp_len;
	int poffset;
	void *fdt;
	void *buf;
	int err;

	test_init(argc, argv);
	fdt = load_blob_arg(argc, argv);

	buf = xmalloc(SPACE);

	err = fdt_open_into(fdt, buf, SPACE);
	if (err)
		FAIL("fdt_open_into(): %s", fdt_strerror(err));

	free(fdt);
	fdt = buf;

	/* Retrieve the offset of the 'prop-str' property */
	poffset = get_poffset(fdt, 0, "prop-str");
	if (poffset < 0)
		FAIL("Property 'prop-str' not found");

	/* Get the property value for modification */
	propval = fdt_getprop_by_offset_w(fdt, poffset, &name, &proplen);
	if (!propval)
		FAIL("fdt_getprop_by_offset_w(%d): %s", poffset,
		     fdt_strerror(proplen));

	/* Check original values */
	if (strcmp(name, "prop-str"))
		FAIL("Name mismatch : %s instead of prop-str", name);

	exp_len = strlen(TEST_STRING_1) + 1;
	if (proplen != exp_len)
		FAIL("Original size mismatch on property \"%s\": %d instead of %d",
		     name, proplen, exp_len);

	exp_val = TEST_STRING_1;
	if (memcmp(exp_val, propval, exp_len))
		FAIL("Original data mismatch on property \"%s\"", name);

	if (exp_len < 4)
		CONFIG("Property \"%s\" should be at least 4 bytes", name);

	/* Modify the property value */
	modif_val = xmalloc(proplen);
	memcpy(modif_val, propval, proplen);
	modif_val[1] = (modif_val[1] != 'A') ? 'A' : 'a';
	modif_val[3] = (modif_val[3] != 'B') ? 'B' : 'b';
	memcpy(propval, modif_val, exp_len);

	/* Check that the modified value is taken into account */
	if (!check_get_prop_offset(fdt, poffset, "prop-str", exp_len, modif_val))
		FAIL("Modified property 'prop-str' not found");

	PASS();
}
