// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* libFuzzer harness: THE CONTROL|24 KEY TABLE (src/diginet/c24_keys.c), the names and LED flags
 * of every key a desk sends, and the lookup that turns a decoded key (button << 8 | zone-or-strip)
 * into its entry. The table is data alone; the module's other sources need libdiginet, and the
 * lookup does not.
 *
 * Input: keys, 16 bits each, little-endian, until the input ends. Every key is looked up, the
 * ones with bit 15 set (an automation LED's key) and the ids between the strips and the zones
 * included. The table itself is checked once for each input.
 *
 * The expected entry is worked out here from surface_diginet_keys.h's words: a key is
 * SURFACE_DIGINET_KEY(button, id), the button the seven bits at 8, the id the low byte; an id
 * of 0x18..0x1D (SURFACE_C24_ZONE_FIRST..LAST) names a section key of that zone, any other id a
 * strip button, whose entry has zone 0. Properties, each an abort() in a check_ function:
 *   - find: the entry surface_c24_key_find() returns is one of the table's, of that button and
 *     that zone, and it returns NULL exactly when the table has no such entry.
 *   - table: no two entries share a button and a zone; a zone is 0 or 0x18..0x1D; every entry
 *     has a name and a section; the entries are listed as the header says, section keys by zone
 *     and button and then the strip buttons by code.
 *   - measured: the strip buttons measured on the desk (surface_diginet_keys.h, R10) are in the
 *     table as measured, and light but SURFACE_DIGINET_BTN_AUTO, whose mode shows on the
 *     automation LEDs.
 */
#include <surface/surface_diginet_keys.h>

#include "fuzz.h"

#define ZONE_FIRST 0x18
#define ZONE_LAST  0x1d

FUZZ_CHECK check_find(uint16_t key, const struct surface_c24_key *got)
{
	unsigned button = key >> 8 & 0x7f, id = key & 0xff;
	int section = id >= ZONE_FIRST && id <= ZONE_LAST;
	size_t at = 0, matches = 0;
	for (size_t i = 0; i < surface_c24_nkeys; i++)
		if (surface_c24_keys[i].button == button && surface_c24_keys[i].zone == (section ? id : 0)) {
			at = i;
			matches++;
		}
	/* A key with an entry in the table finds that entry.
	 * contract: surface_diginet_keys.h surface_c24_key_find ("the entry of a decoded key")
	 * mutant: src/diginet/c24_keys.c:203 `(section ? k->zone == id : k->zone == 0)` -> `(section ? k->zone == id : 1)`
	 * input: table
	 * mutant: src/diginet/c24_keys.c:199 `(key >> 8) & 0x7f` -> `(key >> 8) & 0xff`
	 * input: bit15 */
	if (matches == 1 && got != &surface_c24_keys[at])
		abort();
	/* A key the table does not name finds NULL.
	 * contract: surface_diginet_keys.h surface_c24_key_find ("or NULL for a key the table does not name")
	 * mutant: src/diginet/c24_keys.c:203 `k->button == button && ` -> ``
	 * input: unknown */
	if (matches == 0 && got)
		abort();
}

FUZZ_CHECK check_table(void)
{
	int dup = 0;
	for (size_t i = 0; i < surface_c24_nkeys; i++)
		for (size_t j = i + 1; j < surface_c24_nkeys; j++)
			dup |= surface_c24_keys[i].button == surface_c24_keys[j].button &&
			       surface_c24_keys[i].zone == surface_c24_keys[j].zone;
	/* No two entries share a button and a zone: a lookup has one answer.
	 * contract: surface_diginet_keys.h struct surface_c24_key ("button: the key byte", "zone:
	 * 0x18..0x1D for a section key; 0 for a strip button")
	 * mutant: src/diginet/c24_keys.c:10 `{ 0x01, 0x18,` -> `{ 0x00, 0x18,`
	 * input: keys */
	if (dup)
		abort();
	for (size_t i = 0; i < surface_c24_nkeys; i++) {
		const struct surface_c24_key *k = &surface_c24_keys[i];
	/* A zone is 0 or a section's; every entry is named and in a section of the panel; a flag is
	 * 0 or 1.
	 * contract: surface_diginet_keys.h struct surface_c24_key
	 * mutant: src/diginet/c24_keys.c:11 `{ 0x02, 0x18,` -> `{ 0x02, 0x1e,`
	 * input: keys */
		if ((k->zone != 0 && (k->zone < ZONE_FIRST || k->zone > ZONE_LAST)) || !k->name || !k->name[0] ||
		    !k->section || !k->section[0] || k->led > 1 || k->measured > 1)
			abort();
	}
	for (size_t i = 1; i < surface_c24_nkeys; i++) {
		const struct surface_c24_key *a = &surface_c24_keys[i - 1], *b = &surface_c24_keys[i];
	/* The entries are listed section keys by zone and button, then the strip buttons by code.
	 * contract: surface_diginet_keys.h "Every key, section keys by zone and button, then the strip
	 * buttons by code."
	 * mutant: src/diginet/c24_keys.c:180 `{ 0x00, 0x00, 1, 0, "RecArm"` -> `{ 0x0e, 0x00, 1, 0, "RecArm"`
	 * input: keys */
		if ((a->zone == 0 && b->zone != 0) || (a->zone == b->zone && a->button >= b->button) ||
		    (a->zone != 0 && b->zone != 0 && a->zone > b->zone))
			abort();
	}
}

FUZZ_CHECK check_measured(void)
{
	static const uint8_t buttons[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0a };
	for (size_t b = 0; b < sizeof buttons; b++) {
		const struct surface_c24_key *k = NULL;
		for (size_t i = 0; i < surface_c24_nkeys; i++)
			if (surface_c24_keys[i].button == buttons[b] && surface_c24_keys[i].zone == 0)
				k = &surface_c24_keys[i];
	/* The strip buttons measured on the desk (R10) are measured, and have an LED but the AUTO
	 * button.
	 * contract: surface_diginet_keys.h "The strip buttons measured on the desk (R10)",
	 * SURFACE_DIGINET_BTN_AUTO ("its mode shows on the automation LEDs"), BTN_ACTIVE ("lights
	 * SWITCH ACTIVE")
	 * mutant: src/diginet/c24_keys.c:181 `{ 0x01, 0x00, 1, 1,` -> `{ 0x01, 0x00, 1, 0,`
	 * input: keys
	 * mutant: src/diginet/c24_keys.c:185 `{ 0x05, 0x00, 0, 1,` -> `{ 0x05, 0x00, 1, 1,`
	 * input: keys */
		if (!k || !k->measured || k->led != (buttons[b] != 0x05))
			abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size >= FUZZ_INPUT_MAX)
		return 0;
	struct fuzz_in in = fuzz_in(data, size);
	check_table();
	check_measured();
	while (!fuzz_done(&in)) {
		uint16_t key = fuzz_u16(&in);
		check_find(key, surface_c24_key_find(key));
	}
	return 0;
}
