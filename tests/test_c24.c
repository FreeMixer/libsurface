// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The Control|24 rules of libdiginet#29 section 5, on the Control|24 geometry through the fake
 * backend: the release debounce over a chattering touch (R4), the set-point echo (R4), and the
 * board replay on every ONLINE (R15: a new session blanks the desk). Every fader is treated the
 * same. Strip k shows target k + 1. Faders are 10-bit. */
#include "harness.h"

/* The fader writes to strip, as raw values. */
static int raw_writes(unsigned strip, unsigned *out, int max)
{
	int n = 0;
	for (int i = 0; i < F.nops; i++)
		if (F.ops[i].kind == 'f' && F.ops[i].strip == strip) {
			if (n < max)
				out[n] = F.ops[i].value;
			n++;
		}
	return n;
}

/* ---- touch chatter (R4): drops out for 30-240 ms every few positions during a move ------- */

T(debounce_dropout_is_one_touch)
{
	c24_start();
	F.manner = HOST_LAG;
	touch(0, 1);
	static const unsigned gaps[] = { 30, 80, 150, 240 };
	uint16_t raw = 100;
	for (int i = 0; i < 4; i++) {
		bend_raw(0, raw += 10);
		touch(0, 0);   /* the sense drops out... */
		advance(gaps[i]);
		touch(0, 1);   /* ...and comes back inside the window */
		bend_raw(0, raw += 10);
	}
	CHK(count_events(SURFACE_EV_FADER_TOUCHED) == 1);   /* the host sees one continuous touch */
	CHK(count_events(SURFACE_EV_FADER_RELEASED) == 0);
	unsigned w[64];
	int n = raw_writes(0, w, 64);
	CHK(n == 8);   /* only the set-point echoes: never a drive back while the hand is on */
	for (int i = 0; i < n; i++)
		CHK(w[i] == 110u + 10u * (unsigned)i);
}

T(debounce_release_after_window)
{
	c24_start();
	touch(0, 1);
	bend_raw(0, 512);
	touch(0, 0);
	advance(SURFACE_C24_RELEASE_DEBOUNCE_MS - 1);
	CHK(count_events(SURFACE_EV_FADER_RELEASED) == 0);
	advance(1);
	CHK(count_events(SURFACE_EV_FADER_RELEASED) == 1);
	const struct surface_event *ev = last_event(SURFACE_EV_FADER_RELEASED);
	CHK(ev && ev->strip == 0 && ev->target == 1 && NEAR(ev->position, 512.0 / 1023, 1e-9));
}

/* ---- the set-point echo (R4): the motor holds its own set-point ------------------------- */

T(setpoint_echoes_touched_reports)
{
	c24_start();
	F.manner = HOST_LAG;
	touch(0, 1);
	for (uint16_t r = 100; r <= 300; r += 100) {   /* a hand's move, 10 ms apart */
		bend_raw(0, r);
		F.now += 10 * MS;
	}
	unsigned w[8];
	CHK(raw_writes(0, w, 8) == 3);
	CHK(w[0] == 100 && w[1] == 200 && w[2] == 300);   /* each position written back as it came */
	CHK(F.moves == 3);   /* and each one went up */
}

T(setpoint_release_echo_and_no_snap)
{
	c24_start();
	F.manner = HOST_LAG;
	touch(0, 1);
	for (uint16_t r = 100; r <= 300; r += 100) {   /* a hand's move, 10 ms apart */
		bend_raw(0, r);
		F.now += 10 * MS;
	}
	clear_ops();
	touch(0, 0);
	advance(SURFACE_C24_RELEASE_DEBOUNCE_MS);
	unsigned w[8];
	/* the last report once more (procontrol-linux's fix), and the policy's release drive finds
	 * the cap already there: nothing else */
	CHK(raw_writes(0, w, 8) == 1 && w[0] == 300);
	clear_ops();
	host_deliver(NULL, 0);   /* the gesture's lagging echoes */
	advance(2000);
	CHK(raw_writes(0, w, 8) == 0);
}

T(setpoint_untouched_report_self_writes)
{
	/* a report with no touch is the hand too (the desk reports no motor moves): the policy's
	 * self-write puts the set-point there, once */
	c24_start();
	F.manner = HOST_LAG;
	bend_raw(0, 500);
	bend_raw(0, 500);
	unsigned w[8];
	CHK(raw_writes(0, w, 8) == 1 && w[0] == 500);
}

T(setpoint_older_echo_after_release)
{
	/* #4 on the Control|24: the final echo overtakes the gesture's older ones and the clock runs
	 * past the implicit-touch window. The set-point echo and the release carry the hand's
	 * position; the older echoes never become the target, so nothing drives the cap off it. */
	c24_start();
	F.manner = HOST_LAG;
	touch(0, 1);
	for (uint16_t r = 100; r <= 300; r += 100) {   /* a hand's move, 10 ms apart */
		bend_raw(0, r);
		F.now += 10 * MS;
	}
	clear_ops();
	touch(0, 0);
	advance(SURFACE_C24_RELEASE_DEBOUNCE_MS);
	unsigned w[8];
	CHK(raw_writes(0, w, 8) == 1 && w[0] == 300);
	clear_ops();
	static const int final_first[] = { 2, 0, 1 };
	host_deliver(final_first, 3);
	advance(SURFACE_IMPLICIT_TOUCH_MS + 400);
	int n = raw_writes(0, w, 8);
	for (int i = 0; i < n && i < 8; i++)
		CHK(w[i] == 300);
}

/* ---- the board shadow and replay (R15: a new session blanks the desk) ------------------- */

static int count_kind(char kind)
{
	int n = 0;
	for (int i = 0; i < F.nops; i++)
		n += F.ops[i].kind == kind;
	return n;
}

T(replay_on_every_online)
{
	c24_start();
	CHK(surface_led(F.s, 0x0708, SURFACE_LED_ON) == 1);   /* SOLO on strip 8 */
	surface_ring(F.s, 2, SURFACE_RING_PAN, 0);
	surface_scribble(F.s, 0, 0, "KICK");
	surface_counter(F.s, "012 013");
	serve(1, 0.75);
	surface_meter(F.s, 0, 0, 0.5, F.now);
	for (int round = 0; round < 2; round++) {
		surface_in_offline(F.s, F.now);
		CHK(last_event(SURFACE_EV_OFFLINE) != NULL);
		clear_ops();
		clear_events();
		/* while offline, writes go to the shadow only */
		surface_scribble(F.s, 0, 0, round ? "Bass" : "Snär");
		serve(1, 0.25);
		CHK(F.nops == 0);
		F.now += 30000 * MS;
		surface_in_online(F.s, F.now);
		CHK(count_kind('l') == 1);
		CHK(count_kind('r') == 1);
		CHK(count_kind('c') == 1);
		CHK(count_kind('m') == 0);   /* meters are not replayed: the next level paints */
		int found = 0;
		for (int i = 0; i < F.nops; i++)
			if (F.ops[i].kind == 's' && F.ops[i].strip == 0)
				found = !strcmp(F.ops[i].text, round ? "Bass" : "Sn?r");
		CHK(found);
		CHK(nwrites(0) == 1 && NEAR(last_write(0), 0.25, 0.001));   /* the latest target */
		CHK(nwrites(23) == 1);   /* every strip, written though the motor is where it was */
		/* ONLINE goes up after the replay is queued */
		CHK(F.nev == 1 && F.ev[0].kind == SURFACE_EV_ONLINE);
		clear_ops();
		surface_meter(F.s, 0, 0, 0.5, F.now);
		CHK(count_kind('m') == 1);
	}
}

T(replay_resets_touch)
{
	/* a finger on a cap when the session dropped did not survive it */
	c24_start();
	touch(4, 1);
	surface_in_offline(F.s, F.now);
	surface_in_online(F.s, F.now);
	clear_ops();
	serve(5, 0.4);
	CHK(nwrites(4) == 1);
}

T(scribble_fits_display)
{
	c24_start();
	surface_scribble(F.s, 1, 0, "Overheads");
	CHK(F.nops == 1 && !strcmp(F.ops[0].text, "Over"));
	clear_ops();
	surface_scribble(F.s, 1, 0, "K");
	CHK(F.nops == 1 && !strcmp(F.ops[0].text, "K   "));
	CHK(surface_scribble(F.s, 1, 1, "x") == SURFACE_E_ARG);   /* one row on the Control|24 */
}

int main(void)
{
	RUN(debounce_dropout_is_one_touch);
	RUN(debounce_release_after_window);
	RUN(setpoint_echoes_touched_reports);
	RUN(setpoint_release_echo_and_no_snap);
	RUN(setpoint_untouched_report_self_writes);
	RUN(setpoint_older_echo_after_release);
	RUN(replay_on_every_online);
	RUN(replay_resets_touch);
	RUN(scribble_fits_display);
	return finish("test_c24");
}
