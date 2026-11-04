// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* libFuzzer harness: THE PURE HELPERS (src/meter.c, and the presets and the status text of
 * src/surface.c): the meter law, the ring renderer, the geometry presets, surface_strerror().
 * They take numbers and return numbers, so the harness hands them every number that is the edge
 * of a law (zero, one, a half, NaN, the infinities, magnitudes past a float's range, a fraction
 * that lands a level exactly on a rounding edge).
 *
 * Input: a program, one operation after another until the input ends. An operation is a byte
 * that picks it, and the bytes of its arguments (fuzz/fuzz.h): the meter step (a previous
 * count, a fill, a step count, a hysteresis), the ring mask (a mode, a value, a width), a
 * status code, a model.
 *
 * Every check holds the library to fuzz/spec.h, which is surface.h's words worked out by the
 * harness's own readers. Properties, each an abort() in a check_ function:
 *   - meter: surface_meter_step() with no previous count rounds the clamped fill times the steps,
 *     half up, NaN as empty; with one, holds it while the unrounded value stays within a half
 *     plus the hysteresis of it, and else rounds; never more than `steps` for a count within
 *     them.
 *   - ring: surface_ring_mask() renders each mode as surface.h describes it, bit by bit, gives
 *     nothing for a width of 0 or past 31 LEDs, for NaN and for an unknown mode, and never sets
 *     a bit past the width.
 *   - preset: surface_geometry_preset() fills each field of the two models as surface.h gives
 *     it, refuses a NULL geometry and an unknown model.
 *   - strerror: the text of each code, and "unknown error" for any other.
 */
#include <surface/surface.h>

#include "fuzz.h"

FUZZ_CHECK check_meter(int prev, double fill, unsigned steps, double hyst, unsigned got)
{
	/* A count within the steps stays within them.
	 * contract: surface.h surface_meter_step ("a step 0..steps")
	 * mutant: src/meter.c:24 `v > 1 ? 1 : v` -> `v > 1 ? 2 : v`
	 * input: edges */
	if (prev <= (int)steps && got > steps)
		abort();
	/* With no previous count the fill rounds plainly: its clamp to 0..1 times the steps, half up.
	 * contract: surface.h surface_meter_step ("-1 = none: plain rounding"); spec.h spec_round
	 * mutant: src/meter.c:19 `floor(x + 0.5)` -> `floor(x)`
	 * input: edges */
	if (prev < 0 && fill == fill && got != spec_round(spec_unit(fill) * steps))
		abort();
	/* NaN is an empty meter: it rounds to 0, or holds a previous count within the hysteresis.
	 * contract: surface.h surface_meter_step; meter.c "NaN: an empty meter"
	 * mutant: src/meter.c:33 `fill = 0;` -> `fill = 1;`
	 * input: edges */
	if (fill != fill && got != (prev >= 0 && (double)prev <= 0.5 + hyst ? (unsigned)prev : 0))
		abort();
	/* The previous count holds while the unrounded value stays within 0.5 + hysteresis of it,
	 * the bound included, and else the value rounds.
	 * contract: surface.h SURFACE_METER_STEP_HYSTERESIS; spec.h spec_meter_step
	 * mutant: src/meter.c:35 `<= 0.5 + hysteresis` -> `< 0.5 + hysteresis`
	 * input: hold-edge */
	if (prev >= 0 && got != spec_meter_step(prev, fill, steps, hyst))
		abort();
}

FUZZ_CHECK check_ring(int mode, double v, unsigned leds, uint32_t got)
{
	uint32_t want = spec_ring(mode, v, leds);
	/* No LEDs, more than 31, NaN and the OFF mode light nothing; an unknown mode too.
	 * contract: surface.h SURFACE_MAX_RING_LEDS, SURFACE_RING_OFF
	 * mutant: src/meter.c:85 `leds > SURFACE_MAX_RING_LEDS` -> `leds > SURFACE_MAX_RING_LEDS + 1`
	 * input: ring-edges */
	if ((leds == 0 || leds > SPEC_MAX_RING_LEDS || v != v || mode < 1 || mode > 5) && got != 0)
		abort();
	/* No bit past the ring's width is set.
	 * contract: surface.h surface_ring_mask ("rendered into `leds` bits")
	 * mutant: src/meter.c:96 `span(0, p - 1)` -> `span(0, p)`
	 * input: ring-edges */
	if (leds >= 1 && leds <= SPEC_MAX_RING_LEDS && (got >> leds) != 0)
		abort();
	/* DOT lights one LED at round(v * leds), none at 0.
	 * contract: surface.h SURFACE_RING_DOT
	 * mutant: src/meter.c:92 `p ? 1u << (p - 1) : 0` -> `p > 1 ? 1u << (p - 1) : 0`
	 * input: ring-edges */
	if (mode == 1 && got != want)
		abort();
	/* PAN lights the one LED the value picks across the ring, centred at 0.
	 * contract: surface.h SURFACE_RING_PAN
	 * mutant: src/meter.c:99 `* (leds - 1)` -> `* (leds - 1) / 2`
	 * input: ring-edges */
	if (mode == 2 && got != want)
		abort();
	/* FILL lights from the left up to the value.
	 * contract: surface.h SURFACE_RING_FILL
	 * mutant: src/meter.c:96 `p ? span(0, p - 1) : 0` -> `p ? span(1, p - 1) : 0`
	 * input: ring-edges */
	if (mode == 3 && got != want)
		abort();
	/* BIPOLAR lights from the centre out to the value.
	 * contract: surface.h SURFACE_RING_BIPOLAR
	 * mutant: src/meter.c:104 `span(p, c) : span(c, p)` -> `span(p, c) : span(c + 1, p)`
	 * input: ring-edges */
	if (mode == 4 && got != want)
		abort();
	/* SPREAD lights from the centre out both ways.
	 * contract: surface.h SURFACE_RING_SPREAD
	 * mutant: src/meter.c:108 `span(c - k, ` -> `span(c - k + 1, `
	 * input: ring-edges */
	if (mode == 5 && got != want)
		abort();
}

FUZZ_CHECK check_preset(int model, int rc, const struct surface_geometry *g, int rc_null)
{
	/* A NULL geometry is refused, for a known model too.
	 * contract: surface.h surface_geometry_preset; SURFACE_E_ARG ("a NULL pointer")
	 * mutant: src/surface.c:15 `return SURFACE_E_ARG;` -> `return SURFACE_OK;`
	 * input: presets */
	if (rc_null != SPEC_E_ARG)
		abort();
	/* An unknown model is refused.
	 * contract: surface.h surface_geometry_preset ("SURFACE_E_ARG for an unknown model")
	 * mutant: src/surface.c:58 `return SURFACE_E_ARG;` -> `return SURFACE_OK;`
	 * input: presets */
	if ((model < 1 || model > 2) && rc != SPEC_E_ARG)
		abort();
	if (model < 1 || model > 2)
		return;
	const struct spec_geo *w = &spec_geos[model - 1];
	/* A known model is filled in.
	 * contract: surface.h surface_geometry_preset
	 * mutant: src/surface.c:37 `return SURFACE_OK;` -> `return SURFACE_E_ARG;`
	 * input: presets */
	if (rc != SPEC_OK)
		abort();
	/* The strips, the master, the fader's range and the motor's tolerance.
	 * contract: spec.h spec_geos (surface.h struct surface_geometry)
	 * mutant: src/surface.c:19 `g->strips = 8;` -> `g->strips = 9;`
	 * input: presets */
	if (g->strips != w->strips || g->has_master != w->has_master || g->fader_max != w->fader_max ||
	    g->motor_epsilon_q != w->epsilon_q)
		abort();
	/* The meter and the ring.
	 * contract: spec.h spec_geos (surface.h struct surface_geometry)
	 * mutant: src/surface.c:45 `g->meter_segments = 14;` -> `g->meter_segments = 12;`
	 * input: presets */
	if (g->meter_segments != w->segments || g->meter_sides != w->sides ||
	    g->meter_keepalive_ms != w->keepalive_ms || g->ring_leds != w->ring_leds)
		abort();
	/* The displays.
	 * contract: spec.h spec_geos (surface.h struct surface_geometry)
	 * mutant: src/surface.c:33 `g->scribble_chars = 7;` -> `g->scribble_chars = 6;`
	 * input: presets */
	if (g->scribble_rows != w->scribble_rows || g->scribble_chars != w->scribble_chars ||
	    g->counter_digits != w->counter_digits)
		abort();
	/* The touch: how long a held flag lasts, how long a release waits, who reports the motor.
	 * contract: spec.h spec_geos (surface.h SURFACE_TOUCH_TIMEOUT_MS,
	 * SURFACE_C24_RELEASE_DEBOUNCE_MS)
	 * mutant: src/surface.c:55 `g->holds_setpoint = 1;` -> `g->holds_setpoint = 0;`
	 * input: presets */
	if (g->touch_hold_ms != w->touch_hold_ms || g->touch_release_debounce_ms != w->debounce_ms ||
	    g->reports_motor_moves != w->reports_motor_moves || g->holds_setpoint != w->holds_setpoint)
		abort();
}

FUZZ_CHECK check_strerror(int code, const char *got)
{
	/* Each status has its text, any other code "unknown error".
	 * contract: surface.h enum surface_status; spec.h spec_strerror
	 * mutant: src/surface.c:580 `"unknown error"` -> `"error"`
	 * input: strerror
	 * mutant: src/surface.c:577 `"table full"` -> `"full"`
	 * input: strerror */
	if (!got || strcmp(got, spec_strerror(code)))
		abort();
}

static void op_meter(struct fuzz_in *in)
{
	unsigned steps = fuzz_u8(in) % 65;
	uint8_t p = fuzz_u8(in);
	int prev = p == 0xff ? -1 : (int)(p % 70);
	double fill = fuzz_double(in);
	static const double hyst[] = { 0.0, SPEC_HYSTERESIS, 0.5, 1.0 };
	double h = hyst[fuzz_u8(in) & 3];
	check_meter(prev, fill, steps, h, surface_meter_step(prev, fill, steps, h));
}

static void op_ring(struct fuzz_in *in)
{
	int mode = fuzz_u8(in) % 8;
	double v = fuzz_double(in);
	unsigned leds = fuzz_u8(in) % 36;
	check_ring(mode, v, leds, surface_ring_mask((enum surface_ring_mode)mode, v, leds));
}

static void op_preset(struct fuzz_in *in)
{
	int model = fuzz_u8(in) % 4;
	struct surface_geometry g;
	memset(&g, 0xa5, sizeof g);
/* The 0..1 steps a program names are 0..64; a previous count is one of them or none. */
	check_preset(model, surface_geometry_preset((enum surface_model)model, &g), &g,
		     surface_geometry_preset((enum surface_model)model, NULL));
}

static void op_strerror(struct fuzz_in *in)
{
	int code = (int8_t)fuzz_u8(in);
	check_strerror(code, surface_strerror(code));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size >= FUZZ_INPUT_MAX)
		return 0;
	struct fuzz_in in = fuzz_in(data, size);
	while (!fuzz_done(&in)) {
		switch (fuzz_u8(&in) & 3) {
		case 0: op_meter(&in); break;
		case 1: op_ring(&in); break;
		case 2: op_preset(&in); break;
		default: op_strerror(&in); break;
		}
	}
	return 0;
}
