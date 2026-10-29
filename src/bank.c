// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The bank window (the X-Touch adapter's FaderBank, over surface_target).
 *
 * A console has more channels than a surface has strips, so the surface shows a WINDOW of
 * `strips` targets into the host's ordered list and pages it: a bank step moves by a whole
 * width, a channel step by one. The order of the list is the host's (mixing semantics); this
 * file only moves the window. Every move clears the meter steps: a strip's ballistic hold
 * belongs to the target it showed, so the next frame starts it fresh. */
#include <string.h>

#include "surface_impl.h"

static size_t bank_width(const struct surface *s)
{
	return s->g.strips;
}

static void bank_clamp(struct surface *s)
{
	size_t w = bank_width(s);
	size_t max = s->nlist > w ? s->nlist - w : 0;
	if (s->offset > max)
		s->offset = max;
}

/* Move by delta, clamped to the list; 1 when the window moved. */
static int bank_move_by(struct surface *s, long delta)
{
	size_t before = s->offset;
	long at = (long)before + delta;
	s->offset = at < 0 ? 0 : (size_t)at;
	bank_clamp(s);
	if (s->offset == before)
		return 0;
	meters_reset(s);
	return 1;
}

static long bank_index(const struct surface *s, surface_target t)
{
	if (!t)
		return -1;
	for (size_t i = 0; i < s->nlist; i++)
		if (s->list[i] == t)
			return (long)i;
	return -1;
}

int surface_bank_set(struct surface *s, const surface_target *list, size_t n)
{
	if (!s || (!list && n))
		return SURFACE_E_ARG;
	if (n > s->cap)
		n = s->cap;
	if (n)
		memcpy(s->list, list, n * sizeof *list);
	s->nlist = n;
	bank_clamp(s);
	meters_reset(s);
	return (int)n;
}

int surface_bank_step(struct surface *s, int delta)
{
	if (!s)
		return SURFACE_E_ARG;
	return bank_move_by(s, delta);
}

/* Bring t into the window on its own bank (the bank-aligned page holding it), so the window
 * follows a selection that left it. An on-window or unknown target moves nothing. */
int surface_bank_show(struct surface *s, surface_target t)
{
	if (!s)
		return SURFACE_E_ARG;
	if (surface_target_strip(s, t) >= 0)
		return 0;
	long at = bank_index(s, t);
	if (at < 0)
		return 0;
	long w = (long)bank_width(s);
	return bank_move_by(s, (at / w) * w - (long)s->offset);
}

/* Put t on strip 0: "strip 1 is the first channel of that set". Near the list's end the window
 * overhangs it and the strips past the last target are blank, rather than clamping the set's
 * first target off strip 0; the next step clamps as always. Unknown moves nothing. */
int surface_bank_jump(struct surface *s, surface_target t)
{
	if (!s)
		return SURFACE_E_ARG;
	long at = bank_index(s, t);
	if (at < 0 || (size_t)at == s->offset)
		return 0;
	s->offset = (size_t)at;
	meters_reset(s);
	return 1;
}

int surface_bank_offset(const struct surface *s)
{
	return s ? (int)s->offset : SURFACE_E_ARG;
}

surface_target surface_strip_target(const struct surface *s, unsigned strip)
{
	if (!s)
		return 0;
	if (strip < s->g.strips) {
		size_t i = s->offset + strip;
		return i < s->nlist ? s->list[i] : 0;
	}
	if (strip == s->g.strips && s->g.has_master)
		return s->master;
	return 0;
}

int surface_target_strip(const struct surface *s, surface_target t)
{
	if (!s || !t)
		return -1;
	for (unsigned k = 0; k < s->g.strips; k++)
		if (surface_strip_target(s, k) == t)
			return (int)k;
	return -1;
}

void surface_master_set(struct surface *s, surface_target t)
{
	if (s && s->g.has_master)
		s->master = t;
}
