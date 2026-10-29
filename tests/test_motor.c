// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The motor policy, ported case by case from the X-Touch adapter's suite (xtouch.test.ts), on
 * the X-Touch geometry through a fake backend that records every fader write. The adapter's
 * suite drove bytes through its MIDI port; here the same gestures go in through the backend
 * side of libsurface (surface_in_fader, surface_in_touch) and the host side
 * (surface_fader_served), and the motor writes are read off the fake backend.
 *
 * The whole policy is the canonical Mackie-Control one: a fader has a touch flag and a target,
 * the motor tracks the target only while untouched, and on release the surface resumes tracking
 * with ONE write to the current target. There is no grace window, no reconcile timer and no
 * replay of past frames (bjoluc/cubase-mcu-midiremote's TouchSensitiveMotorFader, cross-checked
 * against DrivenByMoss's MCU FaderTouchCommand and the TouchMCU protocol notes). The one
 * console-specific addition is the awaiting-echo guard: every move returns as a served frame (a
 * whole lagging burst on a sweep), so the surface owns the fader until the host echoes its own
 * last move; otherwise that burst replays onto the motor. */
#include "harness.h"

/* ---- "motor policy (canonical touch-flag model)" -------------------------------------- */

T(policy_a_one_write_on_release)
{
	/* (a) single fast move + release: exactly ONE motor write on release, none mid-move, none after */
	xtouch_start();
	touch(0, 1);
	bend(0, 0.2); bend(0, 0.5); bend(0, 0.7);
	/* while the hand owns the fader nothing is written: the motor never fights the finger */
	CHK(nwrites(0) == 0);
	touch(0, 0);
	/* release re-commands the cap exactly once, to the finger's final position */
	CHK(nwrites(0) == 1);
	CHK(NEAR(last_write(0), 0.7, 0.01));
	/* the host confirms the release value: our own echo, dropped: still exactly one write */
	serve(1, 0.7);
	advance(2000);
	CHK(nwrites(0) == 1);
}

T(policy_b_release_where_held)
{
	/* (b) release when the device held the cap (target already commanded): ZERO writes, no twitch */
	xtouch_start();
	serve(1, 0.5);   /* a remote change drives the motor to 0.5 while untouched */
	advance(200);
	clear_ops();
	touch(0, 1);     /* the operator grabs it and leaves it exactly where it already was */
	bend(0, 0.5);
	touch(0, 0);
	CHK(nwrites(0) == 0);
}

T(policy_c_flick_tracks_forward)
{
	/* (c) fast flick, touch off beats the coast: the motor tracks the cap forward, never reverts */
	xtouch_start();
	touch(0, 1);
	bend(0, 0.2); bend(0, 0.4);   /* touched reports */
	touch(0, 0);                  /* the finger lifts early... */
	bend(0, 0.6); bend(0, 0.9);   /* ...the cap coasts to rest, untouched */
	double w[64];
	int n = motor_writes(0, w, 64);
	CHK(n > 0);
	double max = 0;
	for (int i = 0; i < n; i++) {
		CHK(w[i] >= 0.19);   /* every re-command forward toward the cap's rest */
		if (w[i] > max)
			max = w[i];
	}
	CHK(NEAR(max, 0.9, 0.01));
	/* the host's last move is the cap's rest, so surface and other clients agree (no revert) */
	CHK(NEAR(F.last_move, 0.9, 0.01));
}

T(policy_d_settle_coast)
{
	/* (d) settle-coast converging to rest: untouched reports near the release value write nothing */
	xtouch_start();
	touch(0, 1);
	bend(0, 0.9);
	touch(0, 0);   /* one release write commands the motor to 0.9 */
	CHK(nwrites(0) == 1);
	clear_ops();
	bend(0, 0.9); bend(0, 0.9);   /* the cap wobbles to rest: already there */
	CHK(nwrites(0) == 0);
}

T(policy_f_remote_change_drives_once)
{
	/* (f) a genuine remote change while untouched drives the motor once */
	xtouch_start();
	serve(1, 0.35);
	CHK(nwrites(0) == 1);
	CHK(NEAR(last_write(0), 0.35, 0.01));
}

T(policy_g_never_while_touched)
{
	/* (g) never writes the motor while touched, even under a concurrent remote frame */
	xtouch_start();
	touch(0, 1);
	bend(0, 0.1); bend(0, 0.4); bend(0, 0.8);
	serve(1, 0.55);   /* remote, same target */
	CHK(nwrites(0) == 0);
}

T(policy_master_fader)
{
	/* the same policy on the master fader (pitch-bend channel 8) */
	xtouch_start();
	serve(MAIN, 0.6);   /* a remote change to the main mix drives the master fader once */
	CHK(nwrites(MASTER) == 1);
	clear_ops();
	touch(MASTER, 1);   /* held master: a remote change is suppressed */
	advance(100);
	touch(MASTER, 1);
	serve(MAIN, 0.2);
	CHK(nwrites(MASTER) == 0);
}

T(policy_e_sweep_no_ramp)
{
	/* (e) multi-fader sweep: <= 1 write per fader on release and NO ramp from the lagging echo
	 * storm. A fast MULTI-fader move: 8 faders swept and released, then the host's frames of the
	 * whole gesture arrive as a lagging burst after the finger lifted, i.e. untouched and past
	 * any fixed grace window. The old policy replayed every frame onto the motor, driving each
	 * cap back through the sweep it just came from (the ramp). */
	static const double sweep[] = { 0.15, 0.3, 0.45, 0.6, 0.75, 0.9 };
	xtouch_start();
	for (unsigned k = 0; k < 8; k++) {
		touch(k, 1);
		for (int i = 0; i < 6; i++)
			bend(k, sweep[i]);
		touch(k, 0);
	}
	for (unsigned k = 0; k < 8; k++) {
		CHK(nwrites(k) == 1);   /* exactly once, to its final position */
		CHK(NEAR(last_write(k), 0.9, 0.01));
	}
	clear_ops();
	advance(400);   /* the old policy's 330 ms grace crossed: the policy here has no such clock */
	for (unsigned k = 0; k < 8; k++)
		for (int i = 0; i < 6; i++)
			serve(k + 1, sweep[i]);
	for (unsigned k = 0; k < 8; k++)
		CHK(nwrites(k) == 0);
	advance(2000);   /* and nothing lands later either: the storm's latest is where the cap is */
	for (unsigned k = 0; k < 8; k++)
		CHK(nwrites(k) == 0);
}

/* ---- "implicit touch (divergent inbound reports -> motor held off, flaky sensor)" --------
 * The X-Touch's capacitive sensor drops out mid-move and lags at grab-start, so the touch can be
 * false while the hand is on the cap. Meanwhile the host serves a lagging, stale target. An
 * untouched fader would be driven toward it: the operator feels the motor fight the hand. The
 * implicit-touch window, opened whenever a report diverges from where the motor was last driven,
 * holds that drive off, refreshed by every divergent report and lapsing once reports stop. */

T(implicit_a_stale_target_held_off)
{
	xtouch_start();
	serve(1, 0.2);   /* motor resting at 0.2, the cap physically there */
	advance(200);
	clear_ops();
	/* the host echoes each report at once, so the echo guard is resolved and only the
	 * implicit window stands between the stale frame and the motor */
	static const double up[] = { 0.4, 0.6, 0.8 };
	for (int i = 0; i < 3; i++) {
		bend(0, up[i]);   /* the hand moves the cap up, with NO touch */
		serve(1, 0.2);    /* a stale echo still reports the old target */
		advance(10);
	}
	double w[64];
	int n = motor_writes(0, w, 64), back = 0, fwd = 0;
	for (int i = 0; i < n; i++) {
		back += w[i] <= 0.3;   /* the resistance: a write dragging the cap toward 0.2 */
		fwd += w[i] > 0.3;
	}
	CHK(back == 0);
	CHK(fwd > 0);   /* the forward self-writes toward the hand's own position are unaffected */
}

T(implicit_b_window_lapses)
{
	/* (b) the window lapses when reports stop: a later remote change drives the motor again */
	xtouch_start();
	bend(0, 0.3); bend(0, 0.5); bend(0, 0.7);   /* flaky-sensor move: the window opens */
	clear_ops();
	advance(SURFACE_IMPLICIT_TOUCH_MS + 80);
	serve(1, 0.15);
	CHK(nwrites(0) == 1);
	CHK(NEAR(last_write(0), 0.15, 0.01));
}

T(implicit_c_touch_and_window_compose)
{
	/* (c) touch and implicit window compose: one settle write on release, no double-suppress */
	xtouch_start();
	touch(0, 1);
	bend(0, 0.4); bend(0, 0.7);   /* the window is ALSO open */
	CHK(nwrites(0) == 0);
	touch(0, 0);
	/* the release path is not gated by "handled": one write, to the final position */
	CHK(nwrites(0) == 1);
	CHK(NEAR(last_write(0), 0.7, 0.01));
	serve(1, 0.7);   /* the lagging own echo inside the still-open window adds nothing */
	advance(2000);
	CHK(nwrites(0) == 1);
}

T(implicit_d_idle_drives)
{
	/* (d) idle with no recent reports: a remote change drives the motor (window never opened) */
	xtouch_start();
	serve(1, 0.42);
	CHK(nwrites(0) == 1);
	CHK(NEAR(last_write(0), 0.42, 0.01));
}

/* ---- "fader-touch self-heal (a lost release must never park a motor)" --------------------
 * The touch flag is cleared by the release edge, but that edge can be LOST (dropped across a
 * reconnect), leaving a strip stuck "touched" forever, and the policy never drives a touched
 * strip. Two self-heals: a per-strip touch timeout (touch_hold_ms) and a reset of every flag when
 * the device comes back. Strip 2 shows target 3. */

T(selfheal_timeout)
{
	xtouch_start();
	touch(2, 1);   /* grab strip 2, and the release is LOST */
	serve(3, 0.4);
	CHK(nwrites(2) == 0);   /* still genuinely held: suppressed */
	F.now += 400 * MS;      /* no touch message for longer than the timeout */
	serve(3, 0.8);
	CHK(nwrites(2) > 0);    /* the very next host update drives strip 2 again */
}

T(selfheal_reset_on_reconnect)
{
	/* a frozen clock: the timeout can never fire, the reset is the only thing that clears it */
	xtouch_start();
	touch(2, 1);
	clear_ops();
	surface_in_online(F.s, F.now);   /* the device comes back */
	clear_ops();
	serve(3, 0.7);
	CHK(nwrites(2) > 0);
}

T(selfheal_keeps_held_cap)
{
	/* the kept anti-fight behaviour: a refresh keeps a held cap held past its first deadline */
	xtouch_start();
	touch(2, 1);
	F.now += 200 * MS;
	serve(3, 0.6);
	CHK(nwrites(2) == 0);
	touch(2, 1);   /* the sensor repeats while held */
	F.now += 200 * MS;   /* 400 ms since the first, 200 since the refresh */
	serve(3, 0.9);
	CHK(nwrites(2) == 0);
}

T(selfheal_c24_twin)
{
	/* The Control|24 twin: touch_hold_ms 0 (edges only, DigiNet delivers a release reliably), so
	 * a still finger that never repeats its touch stays held however long it rests. */
	c24_start();
	touch(2, 1);
	F.now += 5000 * MS;
	surface_tick(F.s, F.now);
	serve(3, 0.6);
	CHK(nwrites(2) == 0);
	CHK(count_events(SURFACE_EV_FADER_RELEASED) == 0);
	touch(2, 0);
	advance(SURFACE_C24_RELEASE_DEBOUNCE_MS + 10);
	CHK(count_events(SURFACE_EV_FADER_RELEASED) == 1);
	CHK(NEAR(last_write(2), 0.6, 0.002));   /* the release lands the served target */
}

/* ---- single cases ------------------------------------------------------------------------ */

T(suppresses_write_back_while_touched)
{
	xtouch_start();
	touch(2, 1);
	clear_ops();
	serve(3, 0.9);
	CHK(nwrites(2) == 0);   /* strip 2 shows target 3; touched: nothing written */
	touch(2, 0);
	advance(100);
	serve(3, 0.9);
	CHK(nwrites(2) > 0);
}

T(bank_switch_skips_held_cap)
{
	/* does not drive a held fader's motor on a bank-switch repaint */
	xtouch_start();
	touch(2, 1);
	clear_ops();
	CHK(surface_bank_step(F.s, 8) == 1);
	for (unsigned k = 0; k < 8; k++)   /* the host repaints the new window */
		surface_fader_paint(F.s, surface_strip_target(F.s, k), 0.25, F.now);
	CHK(nwrites(2) == 0);
	CHK(nwrites(3) > 0);   /* its neighbours repaint normally */
}

T(echo_burst_coalesces)
{
	/* A fader ECHO BURST coalesces: the leading frame drives, stale intermediates never do, the
	 * tail lands once. The 2026-08-09 rig report: the motor walked through every stale echo
	 * position and fought the hand. A burst costs exactly TWO writes. */
	xtouch_start();
	static const double frames[] = { 0.1, 0.2, 0.3, 0.4, 0.5 };
	for (int i = 0; i < 5; i++) {
		serve(5, frames[i]);
		F.now += 5 * MS;
	}
	advance(SURFACE_MOTOR_COALESCE_MS + 40);
	double w[8];
	CHK(motor_writes(4, w, 8) == 2);
	CHK(NEAR(w[0], 0.1, 0.005));
	CHK(NEAR(w[1], 0.5, 0.005));
}

T(served_reads_caller_clock)
{
	/* The burst detector reads the caller's now_us (the TypeScript read Date.now() there, so a
	 * suite with a virtual clock could not tell a burst from two frames far apart). */
	xtouch_start();
	serve(5, 0.1);
	F.now += 100 * MS;   /* past the coalesce window on the caller's clock: not a burst */
	serve(5, 0.2);
	CHK(nwrites(4) == 2);
	serve(5, 0.3);       /* the same instant: a burst, held for the trailing flush */
	CHK(nwrites(4) == 2);
	advance(SURFACE_MOTOR_COALESCE_MS);
	CHK(nwrites(4) == 3);
	CHK(NEAR(last_write(4), 0.3, 0.005));
}

T(master_echo)
{
	/* echoes a main-mix change on the dedicated master fader */
	xtouch_start();
	serve(MAIN, 0.5);
	CHK(NEAR(last_write(MASTER), 0.5, 0.01));
}

T(master_suppressed_while_touched)
{
	xtouch_start();
	touch(MASTER, 1);
	clear_ops();
	serve(MAIN, 0.9);
	CHK(nwrites(MASTER) == 0);
	touch(MASTER, 0);
	advance(100);
	serve(MAIN, 0.9);
	CHK(nwrites(MASTER) > 0);
}

T(reannounce_clears_stuck_touch)
{
	/* clears a stuck touch flag on re-announce: a lost release must not freeze a fader */
	xtouch_start();
	touch(2, 1);   /* and the release is lost */
	clear_ops();
	surface_in_online(F.s, F.now);   /* the device rebooted: no fingers on anything */
	CHK(nwrites(2) > 0);
}

T(reannounce_repaints)
{
	/* repaints the surface when a power-cycled unit re-announces itself mid-session */
	xtouch_start();
	surface_in_online(F.s, F.now);
	int faders = 0;
	for (int i = 0; i < F.nops; i++)
		faders += F.ops[i].kind == 'f';
	CHK(faders >= 8);
}

/* ---- "fader-init race (the host's state landing mid-repaint)" ---------------------------
 * The host's served values can land INSIDE a repaint: here the backend calls back in on the
 * first write to strip 0 of the startup paint and delivers target 1's genuine value (0.79)
 * between the paint and the rest. With the seed after the write, motor_at was reset to the
 * stale 0 (== the next target) and the next change was wrongly deduped: the cap stuck at 0.79
 * until a hand re-synced it. */
static int gated;
static void gate(struct fake *f, unsigned strip, uint16_t raw)
{
	(void)raw;
	if (!gated && strip == 0) {
		gated = 1;
		surface_fader_served(f->s, 1, 0.79, f->now);
	}
}

T(fader_init_race)
{
	xtouch_start();
	gated = 0;
	F.on_fader = gate;
	F.now += 500 * MS;
	surface_in_online(F.s, F.now);    /* the startup paint, where the value lands */
	F.on_fader = NULL;
	CHK(gated);
	advance(SURFACE_MOTOR_COALESCE_MS + 40);
	clear_ops();
	serve(1, 0);   /* pulled fully down from another client: the motor MUST follow */
	advance(SURFACE_MOTOR_COALESCE_MS + 40);
	CHK(nwrites(0) > 0);
	CHK(NEAR(last_write(0), 0, 1e-5));
}

/* ---- "refused writes", the motor part ------------------------------------------------------ */

static void refused_setup(void)
{
	xtouch_start();
	serve(1, 0.2);   /* the host's own value: what the surface falls back to on a refusal */
	advance(200);
	clear_ops();
	F.manner = HOST_REFUSE;
}

T(refused_snaps_back)
{
	refused_setup();
	bend(0, 0.8);   /* the operator pulls the cap up */
	advance(SURFACE_MOTOR_COALESCE_MS + 40);
	CHK(NEAR(F.last_move, 0.8, 0.01));   /* the move reached the host, never swallowed */
	CHK(nwrites(0) > 0);
	CHK(NEAR(last_write(0), 0.2, 0.01));   /* refused: back on the value the host holds */
}

T(refused_under_held_cap)
{
	refused_setup();
	touch(0, 1);
	bend(0, 0.9);
	CHK(nwrites(0) == 0);   /* held: not driven while the finger owns the cap */
	touch(0, 0);
	CHK(NEAR(last_write(0), 0.2, 0.01));
}

T(refused_guard_does_not_outlive)
{
	/* keeps tracking the host after a refusal: the echo guard does not outlive the refused move */
	refused_setup();
	bend(0, 0.8);
	advance(SURFACE_MOTOR_COALESCE_MS + 40);
	clear_ops();
	F.manner = HOST_ECHO;
	serve(1, 0.55);   /* a genuine change from another client */
	advance(SURFACE_MOTOR_COALESCE_MS + 40);
	CHK(NEAR(last_write(0), 0.55, 0.01));
}

/* ---- "motor reports (a cap reporting where the motor put it is never a gesture)" ---------
 * A motor that moves a cap reports where it came to rest as an ordinary fader frame. With no
 * hand on the cap, that frame is served state coming back: forwarding it would write the host's
 * own value back as an operator gesture. */

static uint16_t last_raw(unsigned strip)
{
	for (int i = F.nops - 1; i >= 0; i--)
		if (F.ops[i].kind == 'f' && F.ops[i].strip == strip)
			return (uint16_t)F.ops[i].value;
	return 0xffff;
}

T(reports_served_change_writes_nothing)
{
	xtouch_start();
	F.manner = HOST_LAG;
	serve(1, 0.3);
	CHK(NEAR(last_write(0), 0.3, 0.001));
	bend_raw(0, last_raw(0));   /* the cap's report of it */
	CHK(F.moves == 0);
}

T(reports_hand_at_commanded_is_gesture)
{
	xtouch_start();
	F.manner = HOST_LAG;
	serve(1, 0.3);
	touch(0, 1);
	bend_raw(0, last_raw(0));
	CHK(F.moves == 1);
}

T(reports_hand_free_move_is_gesture)
{
	/* a hand-free move away from the commanded position is a gesture (the flaky-sensor path) */
	xtouch_start();
	F.manner = HOST_LAG;
	serve(1, 0.3);
	bend(0, 0.6);
	CHK(F.moves == 1);
	CHK(NEAR(F.last_move, 0.6, 0.01));
	/* back onto the first spot while the implicit window is open: still the hand (the cap's
	 * commanded position is now 0.6, where the self-write put it) */
	bend(0, 0.3);
	CHK(F.moves == 2);
}

T(reports_c24_twin)
{
	/* The Control|24 twin: the desk never reports its own moves (R4), so the rule is off and a
	 * report at the commanded position is the hand. */
	c24_start();
	F.manner = HOST_LAG;
	serve(1, 0.3);
	CHK(nwrites(0) == 1);
	bend_raw(0, last_raw(0));
	CHK(F.moves == 1);
}

int main(void)
{
	RUN(policy_a_one_write_on_release);
	RUN(policy_b_release_where_held);
	RUN(policy_c_flick_tracks_forward);
	RUN(policy_d_settle_coast);
	RUN(policy_f_remote_change_drives_once);
	RUN(policy_g_never_while_touched);
	RUN(policy_master_fader);
	RUN(policy_e_sweep_no_ramp);
	RUN(implicit_a_stale_target_held_off);
	RUN(implicit_b_window_lapses);
	RUN(implicit_c_touch_and_window_compose);
	RUN(implicit_d_idle_drives);
	RUN(selfheal_timeout);
	RUN(selfheal_reset_on_reconnect);
	RUN(selfheal_keeps_held_cap);
	RUN(selfheal_c24_twin);
	RUN(suppresses_write_back_while_touched);
	RUN(bank_switch_skips_held_cap);
	RUN(echo_burst_coalesces);
	RUN(served_reads_caller_clock);
	RUN(master_echo);
	RUN(master_suppressed_while_touched);
	RUN(reannounce_clears_stuck_touch);
	RUN(reannounce_repaints);
	RUN(fader_init_race);
	RUN(refused_snaps_back);
	RUN(refused_under_held_cap);
	RUN(refused_guard_does_not_outlive);
	RUN(reports_served_change_writes_nothing);
	RUN(reports_hand_at_commanded_is_gesture);
	RUN(reports_hand_free_move_is_gesture);
	RUN(reports_c24_twin);
	return finish("test_motor");
}
