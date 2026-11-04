// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* surface_colour(s, NULL, 0) is a valid call (an empty colour list: "(!per_strip && n)" is the
 * refusal) and must not hand memcpy a NULL source: that is undefined behaviour whatever the
 * length. The library is compiled into this test with UndefinedBehaviorSanitizer in trap mode
 * (no runtime library needed, gcc or clang; see the top Makefile), so the NULL source stops the
 * test with SIGILL instead of passing by luck. */
#include "harness.h"

T(empty_colour_list_copies_nothing)
{
	fake_setup(SURFACE_MODEL_CONTROL24);
	const uint8_t some[3] = { 1, 2, 3 };
	CHK(surface_colour(F.s, some, 3) == 0);
	CHK(surface_colour(F.s, NULL, 0) == 0);     /* offline */
	surface_in_online(F.s, 0);
	F.nops = 0;
	CHK(surface_colour(F.s, NULL, 0) == 1);     /* online: the empty list reaches the backend */
	CHK(F.nops == 1 && F.ops[0].kind == 'k' && F.ops[0].value == 0);

	/* the refusal is unchanged */
	CHK(surface_colour(F.s, NULL, 1) == SURFACE_E_ARG);
	surface_destroy(F.s);
}

int main(void)
{
	RUN(empty_colour_list_copies_nothing);
	if (fails)
		return 1;
	puts("test_colour_null: ok");
	return 0;
}
