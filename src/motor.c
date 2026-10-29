// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The motor-fader policy every motorised surface realizes: "motor policy is touch flag plus
 * target, never fight the hand". One implementation, ported from the X-Touch adapter's
 * motor-fader module (MotorFaders) with its reasons; each surface supplies only its geometry.
 *
 * Served frames go to surface_fader_served(), the surface's own moves to motor_moved(), a
 * touch release to motor_released(); motor_drive() is the sole policy writer. What changed in
 * the port (libdiginet#29 section 5.1): keyed by surface_target instead of a channel; the
 * trailing flush is a deadline fired by surface_tick() instead of a timer; and the burst
 * detector reads the caller's now_us. The TypeScript read Date.now() there while everything
 * else read the injected clock, so no test could drive that branch deterministically. */
#include <math.h>

#include "surface_impl.h"

struct fader_state *motor_find(struct surface *s, surface_target t)
{
	if (!t)
		return NULL;
	for (size_t i = 0; i < s->cap; i++)
		if (s->faders[i].t == t)
			return &s->faders[i];
	return NULL;
}

/* A slot no strip shows and no timer or guard holds can be given to another target. */
static int motor_idle(const struct surface *s, const struct fader_state *f)
{
	if (f->flush_armed || f->has_awaiting || f->suspended)
		return 0;
	if (surface_target_strip(s, f->t) >= 0)
		return 0;
	return !(s->g.has_master && s->master == f->t);
}

/* The motor state for t, created at rest (0) on first use; NULL when t is 0 or the table is
 * full of targets in use. */
struct fader_state *motor_state(struct surface *s, surface_target t)
{
	struct fader_state *f = motor_find(s, t);
	if (f || !t)
		return f;
	for (size_t i = 0; i < s->cap; i++)
		if (!s->faders[i].t) {
			f = &s->faders[i];
			break;
		}
	if (!f)
		for (size_t i = 0; i < s->cap; i++)
			if (motor_idle(s, &s->faders[i])) {
				f = &s->faders[i];
				break;
			}
	if (!f)
		return NULL;
	*f = (struct fader_state){ .t = t };
	return f;
}

/* The no-op tolerance in position units: motor_epsilon_q device quanta. */
double motor_epsilon(const struct surface *s)
{
	return (double)s->g.motor_epsilon_q / (double)s->g.fader_max;
}

uint16_t motor_raw(const struct surface *s, double pos)
{
	if (!(pos > 0))
		return 0;
	if (pos >= 1)
		return s->g.fader_max;
	return (uint16_t)floor(pos * s->g.fader_max + 0.5);
}

/* Whether a touch holds a cap showing t: its bank strip, or the master fader. */
int motor_touched(const struct surface *s, surface_target t, uint64_t now_us)
{
	if (!t)
		return 0;
	int k = surface_target_strip(s, t);
	if (k >= 0 && touch_held(s, (unsigned)k, now_us))
		return 1;
	return s->g.has_master && s->master == t && touch_held(s, s->g.strips, now_us);
}

/* True while an implicit-touch window is open: a recent report diverged from where the motor
 * was last driven, so a hand is on the cap even with no touch message. */
static int motor_implicit_open(const struct fader_state *f, uint64_t now_us)
{
	return f->implicit_set && now_us < f->implicit_until;
}

/* The hand owns the cap: a touch or an open implicit-touch window. */
int motor_handled(const struct surface *s, const struct fader_state *f, uint64_t now_us)
{
	return motor_touched(s, f->t, now_us) || motor_implicit_open(f, now_us);
}

static void motor_arm_flush(struct fader_state *f, uint64_t now_us)
{
	if (f->flush_armed)
		return;
	f->flush_armed = 1;
	f->flush_at = now_us + SURFACE_MOTOR_COALESCE_MS * MS_US;
}

/* Write pos on strip's cap: skipped under a hand, on a do-not-drive strip, and while offline.
 * 1 when the backend wrote it. */
int motor_write_strip(struct surface *s, unsigned strip, double pos, uint64_t now_us)
{
	struct strip_state *st = &s->strip[strip];
	if (!s->online || st->no_drive || touch_held(s, strip, now_us))
		return 0;
	uint16_t raw = motor_raw(s, pos);
	if (s->ops.fader(s->backend, strip, raw) <= 0)
		return 0;
	st->commanded_set = 1;
	st->commanded = raw;
	return 1;
}

/* Put pos on every cap showing t that no hand holds (the bank strip and the master fader);
 * 1 when one was written. */
static int motor_write(struct surface *s, surface_target t, double pos, uint64_t now_us)
{
	int written = 0;
	int k = surface_target_strip(s, t);
	if (k >= 0)
		written |= motor_write_strip(s, (unsigned)k, pos, now_us);
	if (s->g.has_master && s->master == t)
		written |= motor_write_strip(s, s->g.strips, pos, now_us);
	return written;
}

/* Drive the motor(s) for f to pos: the sole policy writer. A position within the epsilon of
 * the last drive is skipped, so a re-command never shivers the cap. */
void motor_drive(struct surface *s, struct fader_state *f, double pos, uint64_t now_us)
{
	if (!f->repaint_held && fabs(pos - f->motor_at) <= motor_epsilon(s))
		return;
	if (motor_write(s, f->t, pos, now_us)) {
		f->motor_at = pos;
		f->repaint_held = 0;
	}
}

/* The surface itself put the cap at pos: remember it as awaiting (its echoes never drive the
 * motor) and, when it diverges from the last drive, open the implicit-touch window. */
void motor_moved(struct surface *s, struct fader_state *f, double pos, uint64_t now_us)
{
	f->awaiting = pos;
	f->has_awaiting = 1;
	if (fabs(pos - f->motor_at) > motor_epsilon(s)) {
		f->implicit_set = 1;
		f->implicit_until = now_us + SURFACE_IMPLICIT_TOUCH_MS * MS_US;
	}
}

/* The one motor rule (a served frame -> motor). The frame becomes the target; an echo of the
 * surface's own move resolves awaiting and stops (checked BEFORE the handled gate so awaiting
 * always resolves; a stale guard outliving an implicit window would swallow the next genuine
 * change); a handled fader holds the target for the trailing flush; otherwise the leading frame
 * drives at once and a burst coalesces onto one trailing drive of the latest target. */
void surface_fader_served(struct surface *s, surface_target t, double pos, uint64_t now_us)
{
	if (!s)
		return;
	struct fader_state *f = motor_state(s, t);
	if (!f)
		return;
	f->target = pos;
	if (f->suspended)
		return;
	if (f->has_awaiting) {
		if (fabs(pos - f->awaiting) <= motor_epsilon(s))
			f->has_awaiting = 0;
		return;
	}
	if (motor_handled(s, f, now_us)) {
		motor_arm_flush(f, now_us);
		return;
	}
	int burst = f->has_paint && now_us - f->last_paint_at < SURFACE_MOTOR_COALESCE_MS * MS_US;
	f->has_paint = 1;
	f->last_paint_at = now_us;
	if (!burst) {
		motor_drive(s, f, f->target, now_us);
		return;
	}
	motor_arm_flush(f, now_us);
}

/* A touch release: drive the cap once to where the hand left it while that move is
 * unconfirmed, else to the served target. Idempotent through motor_drive(), so a cap already
 * there gets nothing. A second cap showing t still held keeps it untouched. */
void motor_released(struct surface *s, surface_target t, uint64_t now_us)
{
	if (motor_touched(s, t, now_us))
		return;
	struct fader_state *f = motor_state(s, t);
	if (!f)
		return;
	motor_drive(s, f, f->has_awaiting ? f->awaiting : f->target, now_us);
}

/* The host refused the surface's move: the cap sits where the hand put it, the echo that guard
 * waited for never comes, and the window the refused move opened goes with it. */
void surface_fader_refused(struct surface *s, surface_target t)
{
	if (!s)
		return;
	struct fader_state *f = motor_state(s, t);
	if (!f)
		return;
	if (f->has_awaiting)
		f->motor_at = f->awaiting;
	f->has_awaiting = 0;
	f->implicit_set = 0;
}

/* A repaint over a cap inside its implicit-touch window: keep the served pos as the target and
 * leave the motor and the echo guard alone; the trailing flush lands it once the window
 * lapses. 1 when the paint was held. */
static int motor_hold(struct fader_state *f, double pos, uint64_t now_us)
{
	if (!motor_implicit_open(f, now_us))
		return 0;
	f->target = pos;
	f->repaint_held = 1;
	motor_arm_flush(f, now_us);
	return 1;
}

/* Seed t's state to a freshly painted position (target == last driven, in sync). */
static void motor_seed(struct fader_state *f, double pos)
{
	f->target = pos;
	f->motor_at = pos;
	f->has_awaiting = 0;
	f->implicit_set = 0;
	f->repaint_held = 0;
}

/* The repaint of one strip showing t. Skipped under a hand: a repaint (bank switch, a device
 * that came back) must not drive a held fader against the operator's fingers. A cap moving
 * under a hand with no touch message (the implicit window) is held like a touched one. Seed
 * BEFORE the write, never after: a served value for t that arrives while the write is under
 * way (a backend calling back in) advances the motor state correctly, and a seed after the
 * write would clobber it back to this paint's stale value, so the next served change would be
 * wrongly deduped and the motor would never move until a hand re-synced it. */
void motor_paint_strip(struct surface *s, unsigned strip, surface_target t, double pos, uint64_t now_us)
{
	if (touch_held(s, strip, now_us))
		return;
	struct fader_state *f = motor_state(s, t);
	if (!f)
		return;
	if (motor_hold(f, pos, now_us))
		return;
	motor_seed(f, pos);
	motor_write_strip(s, strip, pos, now_us);
}

void surface_fader_paint(struct surface *s, surface_target t, double pos, uint64_t now_us)
{
	if (!s || !t)
		return;
	int k = surface_target_strip(s, t);
	if (k >= 0)
		motor_paint_strip(s, (unsigned)k, t, pos, now_us);
	if (s->g.has_master && s->master == t)
		motor_paint_strip(s, s->g.strips, t, pos, now_us);
}

void surface_fader_suspend(struct surface *s, surface_target t, int on)
{
	if (!s)
		return;
	struct fader_state *f = motor_state(s, t);
	if (f)
		f->suspended = on ? 1 : 0;
}

int surface_strip_fader(struct surface *s, unsigned strip, double pos, uint64_t now_us)
{
	if (!s || strip >= surface_nstrips(s))
		return SURFACE_E_ARG;
	return motor_write_strip(s, strip, pos, now_us);
}

/* The trailing flush: drive the motor to the latest target once the burst has quieted AND no
 * hand owns the cap. While the fader is handled it re-arms instead of dropping the target, so a
 * served change withheld during a hold lands when the hold lapses (a lost release's self-heal or
 * the implicit window's end) with no further frame needed. The surface's own move (awaiting)
 * ends it: that gesture's release re-commands the motor itself. */
void motor_tick(struct surface *s, uint64_t now_us)
{
	for (size_t i = 0; i < s->cap; i++) {
		struct fader_state *f = &s->faders[i];
		if (!f->t || !f->flush_armed || f->flush_at > now_us)
			continue;
		f->flush_armed = 0;
		if (f->has_awaiting || f->suspended)
			continue;
		if (motor_handled(s, f, now_us)) {
			motor_arm_flush(f, now_us);
			continue;
		}
		motor_drive(s, f, f->target, now_us);
	}
}

uint64_t motor_deadline(const struct surface *s)
{
	uint64_t d = UINT64_MAX;
	for (size_t i = 0; i < s->cap; i++) {
		const struct fader_state *f = &s->faders[i];
		if (f->t && f->flush_armed && f->flush_at < d)
			d = f->flush_at;
	}
	return d;
}
