// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* Meter steps and ring rendering.
 *
 * The meter law: the host turns a level into a display fill (0..1, dB-scaled; that function
 * stays with the host, shared with its other displays). libsurface steps the fill into the
 * surface's LED count with hysteresis and sends a strip only when its count changes, or, on a
 * surface that decays a lit meter by itself, again inside the keep-alive window. The bar
 * follows the live fill: instant attack AND instant fall. No release reaches it (a release
 * belongs to a peak-hold marker), so "the host does ballistics" means only this step. */
#include <math.h>

#include "surface_impl.h"

/* JavaScript's Math.round, which the step law was written in: half rounds up. */
static double round_half_up(double x)
{
	return floor(x + 0.5);
}

static double clamp01(double v)
{
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* A display fill (0..1) -> an integer step 0..steps, with hysteresis against prev: the previous
 * count holds while the unrounded value stays within 0.5 + hysteresis steps of it. No previous
 * count (-1, a fresh or reset strip) rounds plainly. */
unsigned surface_meter_step(int prev, double fill, unsigned steps, double hysteresis)
{
	if (fill != fill)   /* NaN: an empty meter */
		fill = 0;
	double raw = clamp01(fill) * steps;
	if (prev >= 0 && fabs(raw - prev) <= 0.5 + hysteresis)
		return (unsigned)prev;
	return (unsigned)round_half_up(raw);
}

void meters_reset(struct surface *s)
{
	for (unsigned k = 0; k <= SURFACE_MAX_STRIPS; k++)
		for (unsigned i = 0; i < SURFACE_MAX_METER_SIDES; i++)
			s->strip[k].meter_step[i] = -1;
}

void surface_meters_reset(struct surface *s)
{
	if (s)
		meters_reset(s);
}

int surface_meter(struct surface *s, unsigned strip, unsigned side, double fill, uint64_t now_us)
{
	if (!s || strip >= s->g.strips || side >= s->g.meter_sides)
		return SURFACE_E_ARG;
	if (!s->online)
		return 0;
	struct strip_state *st = &s->strip[strip];
	int prev = st->meter_step[side];
	unsigned step = surface_meter_step(prev, fill, s->g.meter_segments, SURFACE_METER_STEP_HYSTERESIS);
	int changed = prev < 0 || step != (unsigned)prev;
	int keepalive = s->g.meter_keepalive_ms &&
		now_us - st->meter_sent_at[side] >= (uint64_t)s->g.meter_keepalive_ms * MS_US;
	if (!changed && !keepalive)
		return 0;
	st->meter_step[side] = (int)step;
	st->meter_sent_at[side] = now_us;
	return s->ops.meter(s->backend, strip, side, step);
}

static uint32_t span(unsigned a, unsigned b)
{
	uint32_t m = 0;
	for (unsigned i = a; i <= b; i++)
		m |= 1u << i;
	return m;
}

/* A ring value rendered into `leds` bits, bit 0 the leftmost. DOT and FILL follow the MCU ring
 * position (round(v * 11), 0 = off); PAN and BIPOLAR centre at LED (leds - 1) / 2, the MCU's
 * position 6 of 11. */
uint32_t surface_ring_mask(enum surface_ring_mode m, double v, unsigned leds)
{
	if (leds == 0 || leds > SURFACE_MAX_RING_LEDS || v != v)
		return 0;
	unsigned c = (leds - 1) / 2;
	double b = v < -1 ? -1 : v > 1 ? 1 : v;
	switch (m) {
	case SURFACE_RING_DOT: {
		unsigned p = (unsigned)round_half_up(clamp01(v) * leds);
		return p ? 1u << (p - 1) : 0;
	}
	case SURFACE_RING_FILL: {
		unsigned p = (unsigned)round_half_up(clamp01(v) * leds);
		return p ? span(0, p - 1) : 0;
	}
	case SURFACE_RING_PAN: {
		unsigned p = (unsigned)round_half_up((b + 1) / 2 * (leds - 1));
		return 1u << p;
	}
	case SURFACE_RING_BIPOLAR: {
		unsigned p = (unsigned)round_half_up((b + 1) / 2 * (leds - 1));
		return p < c ? span(p, c) : span(c, p);
	}
	case SURFACE_RING_SPREAD: {
		unsigned k = (unsigned)round_half_up(clamp01(v) * c);
		return span(c - k, c + k < leds ? c + k : leds - 1);
	}
	case SURFACE_RING_OFF:
	default:
		return 0;
	}
}
