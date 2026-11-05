// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* libsurface as the harnesses hold it, written from include/surface/surface.h (its comments are
 * the contract) and docs/surface-c24.1, not from src/. Every value carries a `contract:` line
 * naming the text it stands on. No check compares the library with itself: the expected value
 * of a check is one of these, or is worked out here, from the contract's words, by a reader of
 * the harness's own.
 *
 * The few values only the library's header defines (the sizes, the status codes, the timing
 * constants) are written out again, so a change to the header is a change a check sees. */
#ifndef SURFACE_FUZZ_SPEC_H
#define SURFACE_FUZZ_SPEC_H

#include <stddef.h>
#include <stdint.h>

/* contract: surface.h "Sizes" */
#define SPEC_MAX_STRIPS         32
#define SPEC_MAX_SCRIBBLE_ROWS  2
#define SPEC_MAX_SCRIBBLE_CHARS 16
#define SPEC_MAX_COUNTER        32
#define SPEC_MAX_RING_LEDS      31

/* contract: surface.h "Return codes: 0 is success, every failure negative" */
#define SPEC_OK      0
#define SPEC_E_ARG   (-1)
#define SPEC_E_FULL  (-2)
#define SPEC_E_NOMEM (-3)

/* contract: surface.h SURFACE_TOUCH_TIMEOUT_MS, SURFACE_C24_RELEASE_DEBOUNCE_MS,
 * SURFACE_METER_KEEPALIVE_MS, SURFACE_METER_STEP_HYSTERESIS */
#define SPEC_TOUCH_TIMEOUT_MS    300
#define SPEC_RELEASE_DEBOUNCE_MS 250
#define SPEC_KEEPALIVE_MS        250
#define SPEC_HYSTERESIS          0.25

/* A device, as surface.h and surface_geometry_preset() describe it. */
struct spec_geo {
	int model;               /* 1 X-Touch, 2 Control|24 */
	unsigned strips, has_master, fader_max, epsilon_q, segments, sides, keepalive_ms;
	unsigned ring_leds, scribble_rows, scribble_chars, counter_digits;
	unsigned touch_hold_ms, debounce_ms, reports_motor_moves, holds_setpoint;
};

/* contract: surface.h struct surface_geometry field comments ("8 X-Touch, 24 Control|24, 8
 * ProControl", "16383 MCU pitch bend, 1023 DigiNet", "12 X-Touch, 14 Control|24", "1 X-Touch,
 * 2 Control|24", "11 MCU, 15 DigiNet", "2 x 7 X-Touch, 1 x 4 Control|24", "10 X-Touch
 * timecode, 8 Control|24", "0 X-Touch, SURFACE_C24_RELEASE_DEBOUNCE_MS Control|24") and the
 * constants above; motor_epsilon_q "16 of 16383" (X-Touch), 2 quanta (Control|24) */
static const struct spec_geo spec_geos[2] = {
	{ 1, 8, 1, 16383, 16, 12, 1, SPEC_KEEPALIVE_MS, 11, 2, 7, 10, SPEC_TOUCH_TIMEOUT_MS, 0, 1, 0 },
	{ 2, 24, 0, 1023, 2, 14, 2, 0, 15, 1, 4, 8, 0, SPEC_RELEASE_DEBOUNCE_MS, 0, 1 },
};

/* The status text of each code. contract: surface.h "Return codes" and src's table of them is
 * what a host prints, so the words are written out here. */
static inline const char *spec_strerror(int code)
{
	switch (code) {
	case 0:  return "ok";
	case -1: return "invalid argument";
	case -2: return "table full";
	case -3: return "out of memory";
	}
	return "unknown error";
}

/* JavaScript's Math.round of a non-negative number, which the step law was written in: half
 * rounds up. contract: surface.h SURFACE_METER_STEP_HYSTERESIS, meter.c's header comment */
static inline unsigned spec_round(double x)
{
	return (unsigned)(x + 0.5);
}

static inline double spec_unit(double v)   /* a fill 0..1: NaN is empty */
{
	return !(v > 0) ? 0 : v > 1 ? 1 : v;
}

static inline double spec_bi(double v)     /* a pan -1..1 */
{
	return v < -1 ? -1 : v > 1 ? 1 : v;
}

/* contract: surface.h surface_meter_step ("a display fill 0..1 -> a step 0..steps with
 * hysteresis against prev (-1 = none: plain rounding)"), SURFACE_METER_STEP_HYSTERESIS ("a
 * level must move past the current step's own rounding edge by this much more before the step
 * changes"): the previous count holds while the unrounded value stays within 0.5 + hysteresis
 * of it */
static inline unsigned spec_meter_step(int prev, double fill, unsigned steps, double hyst)
{
	double raw = spec_unit(fill) * steps;
	if (prev >= 0) {
		double d = raw - prev;
		if (d < 0)
			d = -d;
		if (d <= 0.5 + hyst)
			return (unsigned)prev;
	}
	return spec_round(raw);
}

/* contract: surface.h enum surface_ring_mode, per bit i of the `leds`-wide mask (bit 0 the
 * leftmost): DOT "one LED at round(v * leds), 0 = all off", PAN "one LED, centred", FILL "from
 * the left up to the value", BIPOLAR "from the centre out to the value (boost/cut)", SPREAD
 * "from the centre out both ways (width)". The centre is LED (leds - 1) / 2. NaN, no LEDs and
 * more than SPEC_MAX_RING_LEDS give nothing. */
static inline uint32_t spec_ring(int mode, double v, unsigned leds)
{
	uint32_t m = 0;
	if (leds == 0 || leds > SPEC_MAX_RING_LEDS || v != v)
		return 0;
	unsigned c = (leds - 1) / 2;
	for (unsigned i = 0; i < leds; i++) {
		int on = 0;
		unsigned p;
		switch (mode) {
		case 1:   /* DOT */
			p = spec_round(spec_unit(v) * leds);
			on = p && i == p - 1;
			break;
		case 2:   /* PAN */
			p = spec_round((spec_bi(v) + 1) / 2 * (leds - 1));
			on = i == p;
			break;
		case 3:   /* FILL */
			p = spec_round(spec_unit(v) * leds);
			on = i < p;
			break;
		case 4:   /* BIPOLAR */
			p = spec_round((spec_bi(v) + 1) / 2 * (leds - 1));
			on = p < c ? i >= p && i <= c : i >= c && i <= p;
			break;
		case 5:   /* SPREAD */
			p = spec_round(spec_unit(v) * c);
			on = i + p >= c && i <= c + p;
			break;
		}
		if (on)
			m |= 1u << i;
	}
	return m;
}

/* contract: surface.h surface_scribble / scribble_text's comment in surface.c: "ASCII, one
 * character per code point (anything else is '?', a control character a space), padded with
 * spaces and cut to the display's width". A continuation byte (10xxxxxx) is part of a code
 * point already counted. */
static inline void spec_scribble(char *out, unsigned width, const char *utf8)
{
	unsigned n = 0;
	for (const unsigned char *p = (const unsigned char *)utf8; *p && n < width; p++) {
		if (*p >= 0x80 && *p < 0xc0)
			continue;
		if (*p >= 0x80)
			out[n++] = '?';
		else if (*p < 0x20 || *p == 0x7f)
			out[n++] = ' ';
		else
			out[n++] = (char)*p;
	}
	while (n < width)
		out[n++] = ' ';
}

#endif /* SURFACE_FUZZ_SPEC_H */
