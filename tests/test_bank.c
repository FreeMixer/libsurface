// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The bank window, ported from the X-Touch adapter's FaderBank suite (bank.test.ts), over
 * surface_target on an 8-strip geometry. Target i is input i. */
#include "harness.h"

static surface_target L[32];

static void bank(size_t n)
{
	fake_setup(SURFACE_MODEL_XTOUCH);
	for (size_t i = 0; i < n; i++)
		L[i] = (surface_target)(i + 1);
	CHK(surface_bank_set(F.s, L, n) == (int)n);
}

T(maps_strips_to_window)
{
	bank(3);
	CHK(surface_strip_target(F.s, 0) == 1);
	CHK(surface_strip_target(F.s, 2) == 3);
	CHK(surface_strip_target(F.s, 3) == 0);
}

T(pages_by_bank_and_clamps)
{
	bank(20);
	CHK(surface_bank_offset(F.s) == 0);
	CHK(surface_bank_step(F.s, 8) == 1);    /* 0 -> 8 */
	CHK(surface_bank_offset(F.s) == 8);
	CHK(surface_strip_target(F.s, 0) == 9);
	CHK(surface_bank_step(F.s, 8) == 1);    /* 8 -> 12, clamped from 16: max = 20 - 8 */
	CHK(surface_bank_offset(F.s) == 12);
	CHK(surface_bank_step(F.s, 8) == 0);    /* already at max */
	CHK(surface_bank_step(F.s, -8) == 1);   /* 12 -> 4 */
	CHK(surface_bank_offset(F.s) == 4);
}

T(pages_by_one)
{
	bank(20);
	CHK(surface_bank_step(F.s, 1) == 1);
	CHK(surface_bank_offset(F.s) == 1);
	CHK(surface_strip_target(F.s, 0) == 2);
}

T(show_pages_to_bank)
{
	/* show() pages the window to the bank holding a target, and moves nothing for one on-window
	 * or unknown */
	bank(20);
	CHK(surface_bank_show(F.s, 3) == 0);
	CHK(surface_bank_offset(F.s) == 0);
	CHK(surface_bank_show(F.s, 11) == 1);
	CHK(surface_bank_offset(F.s) == 8);
	CHK(surface_target_strip(F.s, 11) == 2);
	CHK(surface_bank_show(F.s, 20) == 1);
	CHK(surface_bank_offset(F.s) == 12);    /* clamped: the last full window */
	CHK(surface_bank_show(F.s, 500) == 0);  /* unknown */
	CHK(surface_bank_offset(F.s) == 12);
}

T(finds_strip_or_minus_one)
{
	bank(20);
	CHK(surface_target_strip(F.s, 3) == 2);
	CHK(surface_target_strip(F.s, 15) == -1);
	surface_bank_step(F.s, 8);
	CHK(surface_target_strip(F.s, 9) == 0);
}

T(reclamps_when_list_shrinks)
{
	bank(20);
	surface_bank_step(F.s, 8);   /* offset 8 */
	CHK(surface_bank_set(F.s, L, 2) == 2);   /* now shorter than the window */
	CHK(surface_bank_offset(F.s) == 0);
	CHK(surface_strip_target(F.s, 0) == 1);
	CHK(surface_strip_target(F.s, 1) == 2);
	CHK(surface_strip_target(F.s, 2) == 0);
}

T(jump_puts_target_on_strip_zero)
{
	/* the set jump: strip 0 is the first target of the set, the window overhanging the list's
	 * end rather than clamping it off strip 0; the next step clamps as always */
	bank(20);
	CHK(surface_bank_jump(F.s, 17) == 1);
	CHK(surface_bank_offset(F.s) == 16);
	CHK(surface_strip_target(F.s, 0) == 17);
	CHK(surface_strip_target(F.s, 4) == 0);   /* past the last target: blank */
	CHK(surface_bank_jump(F.s, 17) == 0);     /* already there */
	CHK(surface_bank_jump(F.s, 500) == 0);    /* unknown */
	CHK(surface_bank_step(F.s, -1) == 1);
	CHK(surface_bank_offset(F.s) == 12);      /* 15, clamped to the last full window */
}

T(master_is_not_in_the_window)
{
	bank(20);
	surface_master_set(F.s, 1000);
	CHK(surface_strip_target(F.s, 8) == 1000);
	CHK(surface_target_strip(F.s, 1000) == -1);
}

int main(void)
{
	RUN(maps_strips_to_window);
	RUN(pages_by_bank_and_clamps);
	RUN(pages_by_one);
	RUN(show_pages_to_bank);
	RUN(finds_strip_or_minus_one);
	RUN(reclamps_when_list_shrinks);
	RUN(jump_puts_target_on_strip_zero);
	RUN(master_is_not_in_the_window);
	return finish("test_bank");
}
