// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The Control|24 key table (libsurface#8): every code once, every measured strip button present and marked so, the
 * section keys on zones 0x18..0x1D only, and surface_c24_key_find() answering each entry and nothing else. */
#include <stdio.h>
#include <string.h>
#include "surface/surface_diginet_keys.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	/* the strip buttons measured on a desk (Control|24 1.08, libdiginet docs/protocol.md R10) */
	static const uint8_t measured[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A };
	int nstrip = 0, nmeasured = 0;

	for (size_t i = 0; i < surface_c24_nkeys; i++) {
		const struct surface_c24_key *a = &surface_c24_keys[i];
		CHECK(a->name && *a->name && a->section && *a->section, "entry %zu has a name and a section", i);
		CHECK(a->zone == 0 || (a->zone >= SURFACE_C24_ZONE_FIRST && a->zone <= SURFACE_C24_ZONE_LAST),
		      "%s: zone 0x%02X is a strip (0) or 0x18..0x1D", a->name, a->zone);
		CHECK(a->button < 0x80, "%s: the key byte is a data byte", a->name);
		for (size_t j = i + 1; j < surface_c24_nkeys; j++)
			CHECK(!(surface_c24_keys[j].button == a->button && surface_c24_keys[j].zone == a->zone),
			      "%s and %s share the code %02X/%02X", a->name, surface_c24_keys[j].name, a->button, a->zone);
		uint16_t key = SURFACE_DIGINET_KEY(a->button, a->zone ? a->zone : 23);
		CHECK(surface_c24_key_find(key) == a, "%s is found by its code", a->name);
		nstrip += a->zone == 0;
		nmeasured += a->measured;
		if (a->measured)
			CHECK(a->zone == 0 && memchr(measured, a->button, sizeof measured), "%s: marked measured", a->name);
	}
	for (size_t m = 0; m < sizeof measured; m++) {
		const struct surface_c24_key *k = surface_c24_key_find(SURFACE_DIGINET_KEY(measured[m], 0));
		CHECK(k && k->measured, "the measured strip button %02X is in the table and marked", measured[m]);
	}
	CHECK(nmeasured == (int)sizeof measured, "%d measured entries, %zu expected", nmeasured, sizeof measured);
	CHECK(nstrip == 14, "14 strip buttons, %d found", nstrip);
	CHECK(surface_c24_key_find(SURFACE_DIGINET_KEY(0x7F, 0x18)) == NULL, "an unnamed code finds nothing");
	CHECK(surface_c24_key_find(SURFACE_C24_TRANSPORT_PLAY) &&
	      !strcmp(surface_c24_key_find(SURFACE_C24_TRANSPORT_PLAY)->name, "Play"), "the constants name their entries");

	printf("%s c24 key table: %zu keys, %d strip buttons, %d measured, %d failed\n", fails ? "FAIL" : "ok  ",
	       surface_c24_nkeys, nstrip, nmeasured, fails);
	return fails != 0;
}
