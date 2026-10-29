// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The audit of 2026-09-25 against the motor law ("motor policy is touch flag plus target, never
 * fight the hand"; fader touch is a motor override, not a write), X1-X3 and X5, and the fast-throw
 * round trip of the same week, ported from the X-Touch adapter's suites
 * (audit-surfaces-2026-09-25.test.ts, xtouch-fader-final-value-round-trip.test.ts). X4, "the
 * motor timing constants are declared once", is tests/constants-check.sh.
 *
 * A real host does not echo synchronously: a move is sent and its served frame comes back later.
 * HOST_LAG models that by recording the move without echoing, so the test decides when, and
 * which, served frame arrives. */
#include "harness.h"

static void rig(void)
{
	xtouch_start();
	F.manner = HOST_LAG;
}

/* ---- X1: a stale echo becomes the motor target at touch release ------------------------- */

T(x1_release_never_drives_to_older_echo)
{
	rig();
	touch(0, 1);   /* the hand sweeps strip 0 from 0 to 0.6 while touched */
	bend(0, 0.2); bend(0, 0.4); bend(0, 0.6);
	serve(1, 0.2);   /* the echo of the FIRST report lands while the finger is still down */
	clear_ops();
	touch(0, 0);     /* the cap is physically at 0.6 and the host is about to serve 0.6 */
	double w[16];
	int n = motor_writes(0, w, 16);
	for (int i = 0; i < n; i++)
		CHK(NEAR(w[i], 0.6, 0.01));   /* never a stale echo, off the hand position */
}

T(x1_lagging_tail_agrees)
{
	/* after the lagging tail lands, the cap and the host agree (last write wins) */
	rig();
	touch(0, 1);
	bend(0, 0.2); bend(0, 0.4); bend(0, 0.6);
	serve(1, 0.2);
	touch(0, 0);
	serve(1, 0.4);   /* the rest of the gesture's echoes arrive after the finger lifted */
	serve(1, 0.6);
	advance(150);
	double cap = nwrites(0) ? last_write(0) : 0.6;   /* no write: still where the hand left it */
	CHK(NEAR(F.row[1], 0.6, 0.01));
	CHK(NEAR(cap, F.row[1], 0.01));
}

/* ---- X2: a remote change during a lost-release hold is lost when the flag heals --------- */

T(x2_held_target_lands_after_heal)
{
	rig();
	touch(2, 1);     /* grab strip 2 (target 3); the release is LOST */
	serve(3, 0.7);   /* a remote client moves it while the cap reads held: withheld */
	CHK(nwrites(2) == 0);
	advance(1000 + 150);   /* the flag lapses; nothing else is served */
	CHK(nwrites(2) > 0);
	CHK(NEAR(last_write(2), 0.7, 0.01));
}

/* ---- X3: the implicit-touch window closes with no trailing flush ------------------------ */

T(x3_withheld_target_lands_when_window_lapses)
{
	rig();
	bend(0, 0.3); bend(0, 0.5);   /* flaky sensor: reports with no touch open the window */
	serve(1, 0.5);   /* the echo of the hand's own move */
	serve(1, 0.1);   /* the hand leaves; a remote client moves it inside the window: withheld */
	clear_ops();
	advance(SURFACE_IMPLICIT_TOUCH_MS + 150);   /* the window lapses; nothing else is served */
	CHK(nwrites(0) > 0);
	CHK(NEAR(last_write(0), 0.1, 0.01));
}

/* ---- X5: a repaint drives a hand-held (implicit-touch) fader to a stale served value ---- */

#define KEY_SHIFT 0x46

T(x5_repaint_does_not_pull_moving_cap)
{
	rig();
	bend(0, 0.3); bend(0, 0.5); bend(0, 0.7);   /* the hand sweeps up with no touch */
	serve(1, 0.3);   /* the served row lags the hand */
	clear_ops();
	/* the other hand presses SHIFT: the host repaints every strip's fader from its rows */
	surface_in_key(F.s, KEY_SHIFT, 1, F.now);
	CHK(last_event(SURFACE_EV_KEY) && last_event(SURFACE_EV_KEY)->key == KEY_SHIFT);
	for (unsigned k = 0; k < 8; k++) {
		surface_target t = surface_strip_target(F.s, k);
		surface_fader_paint(F.s, t, F.row[t], F.now);
	}
	double w[16];
	int n = motor_writes(0, w, 16);
	for (int i = 0; i < n; i++)
		CHK(w[i] >= 0.69);
}

/* ---- the fast throw: the last value before and after touch-off wins ------------------------
 * "If you go very fast then the fader may come back midway after leaving it in a position"
 * (operator, 2026-09-25). The law: where the hand left the cap wins; the last position the
 * surface sent, before or after touch-off, is what the host keeps and what the motor holds on
 * release; an older echo of the same gesture never becomes the target. The defect needs the host's
 * echo to LAG the gesture, so the echoes are held and released after touch-off, in a chosen
 * order. The X-Touch reports a moving fader at ~100 Hz; a fast throw is 0.10 -> 0.90 in 12
 * reports (120 ms), the hand leaving the cap at the last one. */
#define BEND_PERIOD_MS 10
#define THROW_N 12
#define EPS (1.0 / 1024)

enum order { IN_ORDER, REVERSED, FINAL_FIRST };

static void throw_and_release(int final_after_off, enum order order, unsigned lag_ms)
{
	double thr[THROW_N];
	for (int i = 0; i < THROW_N; i++)
		thr[i] = 0.1 + 0.8 * i / (THROW_N - 1);
	double last = thr[THROW_N - 1];
	rig();
	touch(0, 1);
	int before_off = final_after_off ? THROW_N - 1 : THROW_N;
	for (int i = 0; i < before_off; i++) {
		F.now += BEND_PERIOD_MS * MS;
		bend(0, thr[i]);
		if (i == 5)   /* mid-throw the host catches up on the first half: its target is MIDWAY */
			host_deliver(NULL, 0);
	}
	touch(0, 0);
	int at_off = F.nops;
	if (final_after_off) {   /* the X-Touch can send touch-off before the last report */
		F.now += BEND_PERIOD_MS * MS;
		bend(0, last);
	}
	F.now += lag_ms * MS;   /* the host's echoes of the whole gesture land after the finger lifted */
	int n = F.nheld, idx[64];
	for (int i = 0; i < n; i++) {
		switch (order) {
		case IN_ORDER:    idx[i] = i; break;
		case REVERSED:    idx[i] = i < n - 1 ? n - 2 - i : n - 1; break;   /* stale shuffled, final last */
		case FINAL_FIRST: idx[i] = i == 0 ? n - 1 : i - 1; break;          /* the final overtakes */
		}
	}
	host_deliver(idx, n);
	/* Outlast the implicit-touch window, not only the coalesce window: a stale target withheld
	 * inside the window is driven when it lapses, so a clock that stops short of it hides the
	 * defect (#4: at 120 ms both final-first cases passed with the bug in place). */
	advance(SURFACE_IMPLICIT_TOUCH_MS + 400);
	CHK(NEAR(F.last_move, last, 1e-3));   /* the host holds where the hand left the cap... */
	double w[LOG_MAX];
	int nw = motor_writes(0, w, LOG_MAX);
	CHK(nw > 0);
	CHK(nw > 0 && NEAR(w[nw - 1], last, 1e-3));   /* ...the motor's last target equals it... */
	for (int i = at_off; i < F.nops; i++)          /* ...and nothing after touch-off moved it */
		if (F.ops[i].kind == 'f' && F.ops[i].strip == 0)
			CHK(NEAR((double)F.ops[i].value / F.g.fader_max, last, EPS));
}

T(final_value_round_trip)
{
	static const char *names[] = { "in-order", "reversed", "final-first" };
	for (int after = 0; after <= 1; after++)
		for (int o = IN_ORDER; o <= FINAL_FIRST; o++)
			for (int l = 0; l < 2; l++) {
				unsigned lag = l ? 700 : 30;
				/* Final-first at 700 ms, a stale echo reordered BEHIND the final one and
				 * arriving after the implicit window closed, was left out of the
				 * TypeScript: by arrival the policy cannot tell it from a genuine remote
				 * change. Its send time, older than the hand's last move, does (#4). */
				int before = fails;
				throw_and_release(after, (enum order)o, lag);
				if (fails != before)
					fprintf(stderr, "  in: final report %s touch-off, echoes %s, lag %u ms\n",
						after ? "after" : "before", names[o], lag);
			}
}

int main(void)
{
	RUN(x1_release_never_drives_to_older_echo);
	RUN(x1_lagging_tail_agrees);
	RUN(x2_held_target_lands_after_heal);
	RUN(x3_withheld_target_lands_when_window_lapses);
	RUN(x5_repaint_does_not_pull_moving_cap);
	RUN(final_value_round_trip);
	return finish("test_audit");
}
