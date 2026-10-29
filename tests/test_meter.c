// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* Meter steps and ring rendering.
 *
 * The ballistics cases are ported from the X-Touch adapter's suite
 * (xtouch-meter-ballistics.test.ts). The operator's job, 2026-09-25, on the rig: "xtouch meters
 * are not stable, they seem to be updating every instant and look like blinking". Measured: the
 * host's meter frames at ~19 per second, a steady source's level moving a little every frame;
 * every strip was sent every frame, so a level on a segment edge flipped N <-> N-1 LEDs at
 * 19 Hz. The host hands libsurface a display fill; here a fill of leds/segments puts the ladder
 * at `leds` (fractional) LEDs.
 *
 * The step law also exists in the TypeScript host, which keeps it for its other displays. The
 * golden vectors (tests/golden/meter-steps.txt, argv[1]) were generated once from that law and
 * are fixed: both must give the same step for every row. */
#include "harness.h"

#define FRAME_MS 52   /* ~19 frames/s */

static void frame(double leds)
{
	surface_meter(F.s, 0, 0, leds / F.g.meter_segments, F.now);
	F.now += FRAME_MS * MS;
}

/* The meter counts sent to strip 0, side 0. */
static int meter_sends(unsigned *out, int max)
{
	int n = 0;
	for (int i = 0; i < F.nops; i++)
		if (F.ops[i].kind == 'm' && F.ops[i].strip == 0 && F.ops[i].side == 0) {
			if (n < max)
				out[n] = F.ops[i].value;
			n++;
		}
	return n;
}

static void ballistics(void)
{
	xtouch_start();
	frame(6.5);
	clear_ops();
}

T(edge_oscillation_sends_once_per_keepalive)
{
	ballistics();
	int frames = 80;   /* ~4.2 s */
	for (int f = 0; f < frames; f++)
		frame(6.5 + (f % 5 == 0 ? 0.2 : -0.2));
	unsigned v[256];
	int n = meter_sends(v, 256);
	int windows = (frames * FRAME_MS + SURFACE_METER_KEEPALIVE_MS - 1) / SURFACE_METER_KEEPALIVE_MS;
	CHK(n <= windows);
	for (int i = 1; i < n; i++)
		CHK(v[i] == v[0]);   /* and never a flip */
}

T(held_count_resent_inside_keepalive)
{
	/* the surface decays a lit meter on its own */
	ballistics();
	for (int f = 0; f < 40; f++)
		frame(6);
	unsigned v[256];
	int n = meter_sends(v, 256);
	CHK(n >= 40 * FRAME_MS / SURFACE_METER_KEEPALIVE_MS);
	for (int i = 0; i < n; i++)
		CHK(v[i] == 6);
}

T(attack_is_instant)
{
	ballistics();
	frame(11);
	unsigned v[4];
	CHK(meter_sends(v, 4) == 1 && v[0] == 11);
}

T(bar_falls_at_once)
{
	/* the ladder is a bar: full scale for N frames then silence sends 0 LEDs on the NEXT paint */
	ballistics();
	for (int f = 0; f < 20; f++)
		frame(12);
	clear_ops();
	frame(0);
	unsigned v[4];
	CHK(meter_sends(v, 4) == 1 && v[0] == 0);
}

T(bank_change_resets_strip)
{
	/* a bank change resets the strip: the next frame is painted at once, no inherited hold */
	ballistics();
	frame(12);
	surface_bank_step(F.s, 8);
	surface_bank_step(F.s, -8);
	clear_ops();
	frame(3);   /* well inside the keep-alive window, and far under the held 12 */
	unsigned v[4];
	CHK(meter_sends(v, 4) == 1 && v[0] == 3);
}

T(replay_resets_steps)
{
	ballistics();
	frame(5);
	surface_in_online(F.s, F.now);
	clear_ops();
	frame(5);   /* the device came back blank: the same level is sent again at once */
	unsigned v[4];
	CHK(meter_sends(v, 4) == 1 && v[0] == 5);
}

T(c24_holds_without_keepalive)
{
	/* The Control|24 twin: 14 segments, two sides, keep-alive 0 because the desk holds a lit
	 * meter (R7). A steady level is sent once, however long it stays. */
	c24_start();
	for (int f = 0; f < 200; f++) {
		surface_meter(F.s, 0, 0, 9.0 / 14, F.now);
		surface_meter(F.s, 0, 1, 3.0 / 14, F.now);
		F.now += FRAME_MS * MS;
	}
	int l = 0, r = 0;
	for (int i = 0; i < F.nops; i++)
		if (F.ops[i].kind == 'm') {
			if (F.ops[i].side == 0) { l++; CHK(F.ops[i].value == 9); }
			else { r++; CHK(F.ops[i].value == 3); }
		}
	CHK(l == 1 && r == 1);
	CHK(surface_meter(F.s, 0, 2, 0.5, F.now) == SURFACE_E_ARG);
}

static const char *golden_path;

T(golden_steps)
{
	const char *path = golden_path;
	FILE *fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "NOT A RESULT: cannot open %s\n", path);
		exit(2);
	}
	char line[256];
	int rows = 0;
	while (fgets(line, sizeof line, fp)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		int prev;
		double fill;
		unsigned steps, want;
		if (sscanf(line, "%d %lf %u %u", &prev, &fill, &steps, &want) != 4) {
			fprintf(stderr, "NOT A RESULT: bad golden line: %s", line);
			exit(2);
		}
		unsigned got = surface_meter_step(prev, fill, steps, SURFACE_METER_STEP_HYSTERESIS);
		if (got != want) {
			fails++;
			fprintf(stderr, "FAIL golden: step(%d, %.17g, %u) = %u, want %u\n", prev, fill, steps, got, want);
		}
		rows++;
	}
	fclose(fp);
	if (rows < 1000) {
		fprintf(stderr, "NOT A RESULT: only %d golden rows\n", rows);
		exit(2);
	}
}

T(ring_masks)
{
	/* MCU positions on 11 LEDs: DOT round(v * 11), 0 off; PAN 1..11 centred at 6 */
	CHK(surface_ring_mask(SURFACE_RING_DOT, 0, 11) == 0);
	CHK(surface_ring_mask(SURFACE_RING_DOT, 1, 11) == 1u << 10);
	CHK(surface_ring_mask(SURFACE_RING_DOT, 0.5, 11) == 1u << 5);   /* round(5.5) = 6: LED 6 */
	CHK(surface_ring_mask(SURFACE_RING_PAN, 0, 11) == 1u << 5);
	CHK(surface_ring_mask(SURFACE_RING_PAN, -1, 11) == 1u << 0);
	CHK(surface_ring_mask(SURFACE_RING_PAN, 1, 11) == 1u << 10);
	CHK(surface_ring_mask(SURFACE_RING_FILL, 1, 11) == 0x7ff);
	CHK(surface_ring_mask(SURFACE_RING_FILL, 0, 11) == 0);
	/* 15 LEDs on DigiNet, bit 0 the leftmost (A1): centre LED 7 */
	CHK(surface_ring_mask(SURFACE_RING_PAN, 0, 15) == 1u << 7);
	CHK(surface_ring_mask(SURFACE_RING_BIPOLAR, 0, 15) == 1u << 7);
	CHK(surface_ring_mask(SURFACE_RING_BIPOLAR, 1, 15) == 0x7f80);    /* 7..14 */
	CHK(surface_ring_mask(SURFACE_RING_BIPOLAR, -1, 15) == 0x00ff);   /* 0..7 */
	CHK(surface_ring_mask(SURFACE_RING_SPREAD, 0, 15) == 1u << 7);
	CHK(surface_ring_mask(SURFACE_RING_SPREAD, 1, 15) == 0x7fff);
	CHK(surface_ring_mask(SURFACE_RING_OFF, 0.7, 15) == 0);
	CHK(surface_ring_mask(SURFACE_RING_DOT, 0.7, 0) == 0);
	/* through the surface: the mask reaches the backend and the shadow */
	c24_start();
	CHK(surface_ring(F.s, 3, SURFACE_RING_FILL, 1) == 1);
	CHK(F.nops == 1 && F.ops[0].kind == 'r' && F.ops[0].strip == 3 && F.ops[0].value == 0x7fff);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "NOT A RESULT: usage: test_meter tests/golden/meter-steps.txt\n");
		return 2;
	}
	RUN(edge_oscillation_sends_once_per_keepalive);
	RUN(held_count_resent_inside_keepalive);
	RUN(attack_is_instant);
	RUN(bar_falls_at_once);
	RUN(bank_change_resets_strip);
	RUN(replay_resets_steps);
	RUN(c24_holds_without_keepalive);
	golden_path = argv[1];
	RUN(golden_steps);
	RUN(ring_masks);
	return finish("test_meter");
}
