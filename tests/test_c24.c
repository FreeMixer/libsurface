// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The Control|24 rules of libdiginet#29 section 5, on the Control|24 geometry through the fake
 * backend: the release debounce over a chattering touch (R4), the set-point echo (R4), faders
 * that cannot be trusted (R16: do-not-drive, do-not-read, the impossible-jump filter and the
 * quarantine), and the board replay on every ONLINE (R15: a new session blanks the desk).
 * Strip k shows target k + 1. Faders are 10-bit. */
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
	for (uint16_t r = 100; r <= 300; r += 100) {   /* 10 units/ms: a hand, not a jump */
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
	for (uint16_t r = 100; r <= 300; r += 100) {   /* 10 units/ms: a hand, not a jump */
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

/* ---- faders that cannot be trusted (R16) --------------------------------------------------- */

T(no_drive_never_driven_still_echoed)
{
	c24_start();
	F.manner = HOST_LAG;
	surface_fader_no_drive(F.s, 0, 1);
	CHK(surface_fader_health(F.s, 0) == 1);
	serve(1, 0.5);
	advance(200);
	CHK(nwrites(0) == 0);   /* never driven to a served target */
	touch(0, 1);
	bend_raw(0, 700);
	unsigned w[8];
	CHK(raw_writes(0, w, 8) == 1 && w[0] == 700);   /* the hand is still echoed */
	touch(0, 0);
	advance(SURFACE_C24_RELEASE_DEBOUNCE_MS);
	/* the release echo, so the motor (which still pulls down) does not snap the cap to a stale
	 * set-point; and no drive */
	CHK(raw_writes(0, w, 8) == 2 && w[1] == 700);
	clear_ops();
	surface_in_online(F.s, F.now);   /* a replay skips it too */
	CHK(nwrites(0) == 0);
	CHK(nwrites(1) == 1);
}

T(no_read_drops_reports)
{
	c24_start();
	surface_fader_no_read(F.s, 1, 1);
	CHK(surface_fader_health(F.s, 1) == 2);
	touch(1, 1);
	bend_raw(1, 400);
	CHK(F.moves == 0);       /* no event up */
	CHK(nwrites(1) == 0);    /* no echo down */
	surface_fader_no_read(F.s, 1, 0);
	bend_raw(1, 410);
	CHK(F.moves == 1);
}

T(jump_filter_r16)
{
	/* one sensor jumped 13 -> 1023 within 30 ms and its motor hunted */
	c24_start();
	F.manner = HOST_LAG;
	bend_raw(3, 13);
	F.now += 30 * MS;
	bend_raw(3, 1023);   /* ~34 units/ms: held back */
	CHK(F.moves == 1);
	F.now += 10 * MS;
	bend_raw(3, 15);     /* not confirmed: the jump is dropped, this one is taken */
	CHK(F.moves == 2 && NEAR(F.last_move, 15.0 / 1023, 1e-9));
	unsigned w[8];
	int n = raw_writes(3, w, 8);
	for (int i = 0; i < n; i++)
		CHK(w[i] != 1023);   /* the jump never reached the motor */
	/* a real fast move is confirmed by the next report */
	F.now += 10 * MS;
	bend_raw(3, 600);
	CHK(F.moves == 2);
	F.now += 10 * MS;
	bend_raw(3, 601);
	CHK(F.moves == 3 && NEAR(F.last_move, 601.0 / 1023, 1e-9));
	/* the first report after a touch-on is always taken */
	F.now += 10 * MS;
	touch(3, 1);
	bend_raw(3, 50);
	CHK(F.moves == 4);
}

static void jump_once(unsigned strip, uint16_t from)
{
	bend_raw(strip, from);
	F.now += 10 * MS;
	bend_raw(strip, 1023);
	F.now += 10 * MS;
	bend_raw(strip, from);   /* drops the held jump */
	F.now += 10 * MS;
}

T(quarantine_after_k_jumps)
{
	c24_start();
	F.manner = HOST_LAG;
	for (int i = 0; i < SURFACE_QUARANTINE_JUMPS - 1; i++)
		jump_once(5, 13);
	CHK(count_events(SURFACE_EV_WARNING) == 0);
	jump_once(5, 13);
	CHK(count_events(SURFACE_EV_WARNING) == 1);
	const struct surface_event *ev = last_event(SURFACE_EV_WARNING);
	CHK(ev && ev->code == SURFACE_WARN_FADER_QUARANTINED && ev->strip == 5 && ev->target == 6);
	CHK(surface_fader_health(F.s, 5) == 3);   /* do-not-read and do-not-drive */
	int moves = F.moves;
	clear_ops();
	touch(5, 1);
	bend_raw(5, 300);
	serve(6, 0.9);
	advance(1000);
	CHK(F.moves == moves);   /* no report goes up */
	CHK(nwrites(5) == 0);    /* and nothing is written to the hunting motor */
	surface_fader_no_read(F.s, 5, 0);   /* the host clears it */
	surface_fader_no_drive(F.s, 5, 0);
	bend_raw(5, 310);
	CHK(F.moves == moves + 1);
}

T(quarantine_needs_jumps_close_together)
{
	c24_start();
	F.manner = HOST_LAG;
	for (int i = 0; i < 2 * SURFACE_QUARANTINE_JUMPS; i++) {
		jump_once(7, 13);
		F.now += (SURFACE_QUARANTINE_WINDOW_MS / 2) * MS;
	}
	CHK(count_events(SURFACE_EV_WARNING) == 0);
	CHK(surface_fader_health(F.s, 7) == 0);
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
	RUN(no_drive_never_driven_still_echoed);
	RUN(no_read_drops_reports);
	RUN(jump_filter_r16);
	RUN(quarantine_after_k_jumps);
	RUN(quarantine_needs_jumps_close_together);
	RUN(replay_on_every_online);
	RUN(replay_resets_touch);
	RUN(scribble_fits_display);
	return finish("test_c24");
}
