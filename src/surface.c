// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The surface: creation, the geometries, touch, the inputs from a backend, the board shadow
 * and its replay, and the timers. The motor policy is motor.c, the bank
 * window bank.c, the meter step and ring rendering meter.c. */
#include <stdlib.h>
#include <string.h>

#include "surface_impl.h"

int surface_geometry_preset(enum surface_model m, struct surface_geometry *g)
{
	if (!g)
		return SURFACE_E_ARG;
	memset(g, 0, sizeof *g);
	switch (m) {
	case SURFACE_MODEL_XTOUCH:
		g->strips = 8;
		g->has_master = 1;
		g->fader_max = 16383;          /* 14-bit pitch bend */
		/* Motor no-op tolerance: a target within this of where the motor was last driven
		 * needs no write; the cap is already there, so re-sending it would only make it
		 * shiver. This is the reference model's "prevent identical messages to reduce fader
		 * noise" (difference !== 0) guard, widened one quantum: 1/1024 of the travel,
		 * comfortably above the 14-bit step and far below a visible move. 16 of 16383. */
		g->motor_epsilon_q = 16;
		g->meter_segments = 12;
		g->meter_sides = 1;
		g->meter_keepalive_ms = SURFACE_METER_KEEPALIVE_MS;
		g->ring_leds = 11;
		g->scribble_rows = 2;
		g->scribble_chars = 7;
		g->counter_digits = 10;
		g->touch_hold_ms = SURFACE_TOUCH_TIMEOUT_MS;
		g->reports_motor_moves = 1;    /* a motor's rest arrives as an ordinary fader frame */
		return SURFACE_OK;
	case SURFACE_MODEL_CONTROL24:
		g->strips = 24;
		g->fader_max = 1023;           /* 10-bit faders, docs/protocol.md */
		/* On a 10-bit fader one step is 1/1023 > 1/1024, so the X-Touch's 1/1024 would let
		 * sensor jitter of +-1 LSB open implicit-touch windows. 2 quanta is the design's
		 * proposal until the idle jitter is measured per strip (libdiginet#29 Q4). */
		g->motor_epsilon_q = 2;
		g->meter_segments = 14;        /* 9 green, 4 orange, 1 red (R7) */
		g->meter_sides = 2;            /* L at ch, R at 0x20 + ch */
		g->meter_keepalive_ms = 0;     /* a lit meter holds >= 10 s (R7) */
		g->ring_leds = 15;
		g->scribble_rows = 1;
		g->scribble_chars = 4;         /* R6 */
		g->counter_digits = 8;
		g->touch_hold_ms = 0;          /* edges only, see SURFACE_TOUCH_TIMEOUT_MS */
		g->touch_release_debounce_ms = SURFACE_C24_RELEASE_DEBOUNCE_MS;
		g->reports_motor_moves = 0;    /* the desk never reports its own moves (R4) */
		g->holds_setpoint = 1;         /* the motor drives back to its set-point (R4) */
		return SURFACE_OK;
	}
	return SURFACE_E_ARG;
}

static int geometry_ok(const struct surface_geometry *g)
{
	return g->strips && g->strips <= SURFACE_MAX_STRIPS && g->fader_max &&
		g->meter_sides <= SURFACE_MAX_METER_SIDES && g->ring_leds <= SURFACE_MAX_RING_LEDS &&
		g->scribble_rows <= SURFACE_MAX_SCRIBBLE_ROWS &&
		g->scribble_chars <= SURFACE_MAX_SCRIBBLE_CHARS;
}

int surface_create(struct surface **out, const struct surface_config *c)
{
	if (!out)
		return SURFACE_E_ARG;
	*out = NULL;
	if (!c || !c->ops || !c->ops->fader || !c->ops->led || !c->ops->ring || !c->ops->meter ||
	    !c->ops->scribble || !c->ops->counter || !geometry_ok(&c->geometry))
		return SURFACE_E_ARG;
	size_t cap = c->max_targets ? c->max_targets : 256;
	size_t ledcap = c->max_leds ? c->max_leds : 256;
	/* one block: the surface, the bank list, the motor states, the LED shadow */
	size_t size = sizeof(struct surface) + cap * sizeof(surface_target) +
		cap * sizeof(struct fader_state) + ledcap * sizeof(struct led_slot);
	unsigned char *mem = calloc(1, size);
	if (!mem)
		return SURFACE_E_NOMEM;
	struct surface *s = (struct surface *)mem;
	mem += sizeof *s;
	s->faders = (struct fader_state *)mem;
	mem += cap * sizeof(struct fader_state);
	s->list = (surface_target *)mem;
	mem += cap * sizeof(surface_target);
	s->leds = (struct led_slot *)mem;
	s->g = c->geometry;
	s->ops = *c->ops;
	s->backend = c->backend;
	s->event = c->event;
	s->event_ctx = c->event_ctx;
	s->cap = cap;
	s->ledcap = ledcap;
	meters_reset(s);
	*out = s;
	return SURFACE_OK;
}

void surface_destroy(struct surface *s)
{
	free(s);
}

const struct surface_geometry *surface_geometry_of(const struct surface *s)
{
	return s ? &s->g : NULL;
}

int surface_online(const struct surface *s)
{
	return s ? s->online : 0;
}

unsigned surface_nstrips(const struct surface *s)
{
	return s->g.strips + (s->g.has_master ? 1u : 0u);
}

void surface_emit(struct surface *s, const struct surface_event *ev)
{
	if (s->event)
		s->event(s->event_ctx, ev);
}

/* ---- touch ----------------------------------------------------------------------------- */

/* Whether strip is held now. The flag is set by a touch and cleared by a release (after its
 * debounce on a chattering sensor); with touch_hold_ms it also lapses that long after the last
 * touch message, so a lost release heals on its own. surface_tick() clears a lapsed flag and
 * says so; until then it already reads as released. */
int touch_held(const struct surface *s, unsigned strip, uint64_t now_us)
{
	const struct strip_state *st = &s->strip[strip];
	if (!st->touch)
		return 0;
	return !s->g.touch_hold_ms || now_us < st->touch_until;
}

static double strip_position(const struct surface *s, const struct strip_state *st,
			     surface_target t)
{
	if (st->have_report)
		return (double)st->last_raw / s->g.fader_max;
	struct fader_state *f = motor_find((struct surface *)s, t);
	return f ? f->target : 0;
}

/* Echo raw as strip's set-point: the Control|24's motor holds its own set-point and drives back
 * to it when the cap is let go, and the desk never reports its own moves (R4). So while a hand
 * owns the cap, every reported position is written back as the set-point, and once more at the
 * release (procontrol-linux's fix, docs/protocol.md). This is set-point maintenance, not a
 * motor drive. It is recorded as the cap's commanded
 * position, so the policy's release drive finds the cap there and writes nothing. */
static void setpoint_echo(struct surface *s, unsigned strip, uint16_t raw)
{
	if (!s->online)
		return;
	if (s->ops.fader(s->backend, strip, raw) > 0)
		surface_note_commanded(s, strip, raw);
}

static void touch_release(struct surface *s, unsigned strip, uint64_t now_us)
{
	struct strip_state *st = &s->strip[strip];
	st->touch = 0;
	st->release_pending = 0;
	surface_target t = surface_strip_target(s, strip);
	if (s->g.holds_setpoint && st->have_report)
		setpoint_echo(s, strip, st->last_raw);
	/* Where the hand left the cap wins: while the surface's last report is still unconfirmed,
	 * the target is an OLDER echo of this very gesture. */
	if (t)
		motor_released(s, t, now_us);
	struct surface_event ev = {
		.kind = SURFACE_EV_FADER_RELEASED, .strip = (uint8_t)strip, .target = t,
		.position = strip_position(s, st, t),
	};
	surface_emit(s, &ev);
}

void surface_in_touch(struct surface *s, unsigned strip, int on, uint64_t now_us)
{
	if (!s || strip >= surface_nstrips(s))
		return;
	struct strip_state *st = &s->strip[strip];
	int was = touch_held(s, strip, now_us);
	if (on) {
		st->touch = 1;
		if (s->g.touch_hold_ms)
			st->touch_until = now_us + (uint64_t)s->g.touch_hold_ms * MS_US;
		if (st->release_pending) {
			/* a dropout inside the debounce: one continuous touch */
			st->release_pending = 0;
			return;
		}
		if (was)
			return;   /* the sensor repeating itself while held: the deadline is refreshed */
		struct surface_event ev = {
			.kind = SURFACE_EV_FADER_TOUCHED, .strip = (uint8_t)strip, .on = 1,
			.target = surface_strip_target(s, strip),
		};
		surface_emit(s, &ev);
		return;
	}
	if (!was) {
		st->touch = 0;
		return;
	}
	if (s->g.touch_release_debounce_ms) {
		if (!st->release_pending) {
			st->release_pending = 1;
			st->release_at = now_us + (uint64_t)s->g.touch_release_debounce_ms * MS_US;
		}
		return;
	}
	touch_release(s, strip, now_us);
}

/* ---- inputs ------------------------------------------------------------------------------ */

void surface_note_commanded(struct surface *s, unsigned strip, uint16_t raw)
{
	if (!s || strip >= surface_nstrips(s))
		return;
	struct strip_state *st = &s->strip[strip];
	st->commanded_set = 1;
	st->commanded = raw;
	struct fader_state *f = motor_find(s, surface_strip_target(s, strip));
	if (f)
		f->motor_at = (double)raw / s->g.fader_max;
}

/* A fader report from the device: the physical fader IS at raw (the finger, or the cap coasting
 * to rest after a fast flick; the touch may already read released). It goes up as a move, and is
 * remembered as the target's awaiting value so the host's echo of it (and, on a sweep, a whole
 * lagging burst) never drives the motor: the cap is physically there already.
 *
 * While touched the policy never writes the motor (the hand owns it). While UNTOUCHED the report
 * is the cap settling on its own: a hard flick releases the touch before the cap stops, so the
 * motor's last commanded value can be the pre-coast position and it would drive the cap back
 * (the "revert"). Re-command it to hold where the cap now is: an idempotent write, so a fader that
 * already settled gets nothing while an over-thrown one is tracked forward to its rest, never
 * backward. This self-write follows the hand's OWN position, so it is gated on the touch alone;
 * the implicit window governs only served -> motor drive. */
/* A motor's report of where it put the cap is served state coming back, never a gesture: with no
 * hand on the cap, a frame at the commanded position (within the epsilon) is that report. Only
 * where the device reports its motor's moves at all. */
int surface_fader_is_report(const struct surface *s, unsigned strip, uint16_t raw, uint64_t now_us)
{
	if (!s || strip >= surface_nstrips(s))
		return 0;
	const struct strip_state *st = &s->strip[strip];
	return s->g.reports_motor_moves && !touch_held(s, strip, now_us) && st->commanded_set &&
		abs((int)raw - (int)st->commanded) <= s->g.motor_epsilon_q;
}

void surface_in_fader(struct surface *s, unsigned strip, uint16_t raw, uint64_t now_us)
{
	if (!s || strip >= surface_nstrips(s))
		return;
	struct strip_state *st = &s->strip[strip];
	if (raw > s->g.fader_max)
		raw = s->g.fader_max;
	int held = touch_held(s, strip, now_us);
	int report = surface_fader_is_report(s, strip, raw, now_us);
	st->have_report = 1;
	st->last_raw = raw;
	st->last_at = now_us;
	if (report)
		return;
	surface_target t = surface_strip_target(s, strip);
	double pos = (double)raw / s->g.fader_max;
	struct fader_state *f = motor_state(s, t);
	int suspended = f && f->suspended;
	if (f && !suspended)
		motor_moved(s, f, pos, now_us);
	struct surface_event ev = {
		.kind = SURFACE_EV_FADER_MOVED, .strip = (uint8_t)strip, .target = t, .position = pos,
	};
	surface_emit(s, &ev);
	if (suspended)
		return;   /* the cap carries a layer: the host holds what it means */
	if (s->g.holds_setpoint && held) {
		setpoint_echo(s, strip, raw);
		return;
	}
	if (t && !motor_touched(s, t, now_us)) {
		f = motor_find(s, t);   /* the event may have called back in */
		if (f)
			motor_drive(s, f, pos, now_us);
	}
}

void surface_in_key(struct surface *s, uint16_t key, int pressed, uint64_t now_us)
{
	(void)now_us;
	if (!s)
		return;
	struct surface_event ev = { .kind = SURFACE_EV_KEY, .key = key, .on = pressed ? 1 : 0 };
	surface_emit(s, &ev);
}

void surface_in_vpot(struct surface *s, unsigned strip, int delta, uint64_t now_us)
{
	(void)now_us;
	if (!s || strip >= s->g.strips)
		return;
	struct surface_event ev = {
		.kind = SURFACE_EV_VPOT, .strip = (uint8_t)strip,
		.target = surface_strip_target(s, strip), .delta = (int16_t)delta,
	};
	surface_emit(s, &ev);
}

void surface_in_jog(struct surface *s, int delta, uint64_t now_us)
{
	(void)now_us;
	if (!s)
		return;
	struct surface_event ev = { .kind = SURFACE_EV_JOG, .delta = (int16_t)delta };
	surface_emit(s, &ev);
}

void surface_in_warning(struct surface *s, int code, const char *text, uint64_t now_us)
{
	(void)now_us;
	if (!s)
		return;
	struct surface_event ev = { .kind = SURFACE_EV_WARNING, .code = code, .text = text };
	surface_emit(s, &ev);
}

/* ---- the board shadow ------------------------------------------------------------------- */

static struct led_slot *led_slot(struct surface *s, uint16_t key, int create)
{
	struct led_slot *free_slot = NULL;
	for (size_t i = 0; i < s->ledcap; i++) {
		if (s->leds[i].used && s->leds[i].key == key)
			return &s->leds[i];
		if (!s->leds[i].used && !free_slot)
			free_slot = &s->leds[i];
	}
	if (!create || !free_slot)
		return NULL;
	free_slot->used = 1;
	free_slot->key = key;
	return free_slot;
}

int surface_led(struct surface *s, uint16_t key, enum surface_led st)
{
	if (!s || (unsigned)st > SURFACE_LED_FLASH)
		return SURFACE_E_ARG;
	struct led_slot *l = led_slot(s, key, 1);
	if (l)
		l->st = (uint8_t)st;
	int rc = s->online ? s->ops.led(s->backend, key, st) : 0;
	return l ? rc : SURFACE_E_FULL;
}

int surface_ring(struct surface *s, unsigned strip, enum surface_ring_mode m, double v)
{
	if (!s || strip >= s->g.strips)
		return SURFACE_E_ARG;
	struct strip_state *st = &s->strip[strip];
	st->ring_set = 1;
	st->ring_is_code = 0;
	st->ring = surface_ring_mask(m, v, s->g.ring_leds);
	return s->online ? s->ops.ring(s->backend, strip, st->ring) : 0;
}

int surface_ring_code(struct surface *s, unsigned strip, uint32_t code)
{
	if (!s || strip >= s->g.strips || !s->ops.ring_code)
		return SURFACE_E_ARG;
	struct strip_state *st = &s->strip[strip];
	st->ring_set = 1;
	st->ring_is_code = 1;
	st->ring = code;
	return s->online ? s->ops.ring_code(s->backend, strip, code) : 0;
}

/* The text as the display shows it: ASCII, one character per code point (anything else is
 * '?', a control character a space), padded with spaces and cut to the display's width. */
static void scribble_text(char *out, unsigned n, const char *utf8)
{
	unsigned i = 0;
	for (const unsigned char *p = (const unsigned char *)utf8; *p && i < n; p++) {
		if ((*p & 0xc0) == 0x80)
			continue;   /* a continuation byte: its code point is already counted */
		out[i++] = *p >= 0x80 ? '?' : *p < 0x20 || *p == 0x7f ? ' ' : (char)*p;
	}
	while (i < n)
		out[i++] = ' ';
}

int surface_scribble(struct surface *s, unsigned strip, unsigned row, const char *utf8)
{
	if (!s || !utf8 || strip >= s->g.strips || row >= s->g.scribble_rows)
		return SURFACE_E_ARG;
	struct strip_state *st = &s->strip[strip];
	scribble_text(st->scribble[row], s->g.scribble_chars, utf8);
	st->scribble_set[row] = 1;
	return s->online ? s->ops.scribble(s->backend, strip, row, st->scribble[row], s->g.scribble_chars) : 0;
}

int surface_counter(struct surface *s, const char *text)
{
	if (!s || !text)
		return SURFACE_E_ARG;
	size_t n = strlen(text);
	if (n >= sizeof s->counter)
		n = sizeof s->counter - 1;
	memcpy(s->counter, text, n);
	s->counter[n] = 0;
	s->counter_set = 1;
	return s->online ? s->ops.counter(s->backend, s->counter) : 0;
}

int surface_colour(struct surface *s, const uint8_t *per_strip, size_t n)
{
	if (!s || (!per_strip && n))
		return SURFACE_E_ARG;
	if (n > s->g.strips)
		n = s->g.strips;
	memcpy(s->colour, per_strip, n);
	s->ncolour = n;
	if (!s->ops.colour || !s->online)
		return 0;
	return s->ops.colour(s->backend, s->colour, s->ncolour);
}

/* One strip showing nothing: its scribble rows, ring and meter blank, its cap at the bottom
 * unless a hand holds it. Its keys' LEDs are the host's (libsurface does not know which keys a
 * strip has). */
int surface_blank_strip(struct surface *s, unsigned strip)
{
	if (!s || strip >= s->g.strips)
		return SURFACE_E_ARG;
	for (unsigned r = 0; r < s->g.scribble_rows; r++)
		surface_scribble(s, strip, r, "");
	surface_ring(s, strip, SURFACE_RING_OFF, 0);
	struct strip_state *st = &s->strip[strip];
	for (unsigned side = 0; side < s->g.meter_sides; side++) {
		st->meter_step[side] = -1;
		if (s->online && s->ops.meter(s->backend, strip, side, 0) > 0)
			st->meter_step[side] = 0;
	}
	uint64_t never = 0;   /* a held cap is skipped: the self-heal reads no clock here */
	motor_write_strip(s, strip, 0, never);
	return SURFACE_OK;
}

/* ---- online, offline, replay ---------------------------------------------------------------
 * A device that comes back blank (a new DigiNet session blanks every scribble, LED, ring, meter
 * and the counter, R15; a power-cycled X-Touch re-announcing; a port re-opened after a stall)
 * gets the whole board again from the shadow, without asking the host:
 *  1. touch flags reset: no finger survived the gap, whatever the flags said before (including
 *     a flag stuck by a release lost on the wire), or the repaint would skip that cap;
 *  2. meter steps reset: the next level paints at once;
 *  3. every element written. Faders go through the repaint path: a cap inside an implicit-touch
 *     window is not driven, the rest are written even where the motor was last driven, because
 *     the device may have moved them;
 *  4. SURFACE_EV_ONLINE goes up after the replay. */
void surface_touch_reset(struct surface *s)
{
	if (!s)
		return;
	for (unsigned k = 0; k < surface_nstrips(s); k++) {
		s->strip[k].touch = 0;
		s->strip[k].release_pending = 0;
	}
}

void surface_in_online(struct surface *s, uint64_t now_us)
{
	if (!s)
		return;
	s->online = 1;
	surface_touch_reset(s);
	meters_reset(s);
	for (size_t i = 0; i < s->ledcap; i++)
		if (s->leds[i].used)
			s->ops.led(s->backend, s->leds[i].key, (enum surface_led)s->leds[i].st);
	for (unsigned k = 0; k < s->g.strips; k++) {
		struct strip_state *st = &s->strip[k];
		if (st->ring_set && st->ring_is_code)
			s->ops.ring_code(s->backend, k, st->ring);
		else if (st->ring_set)
			s->ops.ring(s->backend, k, st->ring);
		for (unsigned r = 0; r < s->g.scribble_rows; r++)
			if (st->scribble_set[r])
				s->ops.scribble(s->backend, k, r, st->scribble[r], s->g.scribble_chars);
	}
	if (s->counter_set)
		s->ops.counter(s->backend, s->counter);
	if (s->ncolour && s->ops.colour)
		s->ops.colour(s->backend, s->colour, s->ncolour);
	for (unsigned k = 0; k < surface_nstrips(s); k++) {
		surface_target t = surface_strip_target(s, k);
		if (!t) {
			if (k < s->g.strips)
				motor_write_strip(s, k, 0, now_us);
			continue;
		}
		struct fader_state *f = motor_find(s, t);
		if (f)
			motor_paint_strip(s, k, t, f->target, now_us);
	}
	struct surface_event ev = { .kind = SURFACE_EV_ONLINE };
	surface_emit(s, &ev);
}

void surface_in_offline(struct surface *s, uint64_t now_us)
{
	(void)now_us;
	if (!s)
		return;
	s->online = 0;
	struct surface_event ev = { .kind = SURFACE_EV_OFFLINE };
	surface_emit(s, &ev);
}

/* ---- time --------------------------------------------------------------------------------- */

uint64_t surface_deadline(const struct surface *s, uint64_t now_us)
{
	if (!s)
		return UINT64_MAX;
	uint64_t d = motor_deadline(s);
	for (unsigned k = 0; k < surface_nstrips(s); k++) {
		const struct strip_state *st = &s->strip[k];
		if (st->release_pending && st->release_at < d)
			d = st->release_at;
		else if (st->touch && s->g.touch_hold_ms && !st->release_pending && st->touch_until < d)
			d = st->touch_until;
	}
	return d < now_us ? now_us : d;
}

int surface_tick(struct surface *s, uint64_t now_us)
{
	if (!s)
		return SURFACE_E_ARG;
	for (unsigned k = 0; k < surface_nstrips(s); k++) {
		struct strip_state *st = &s->strip[k];
		if (st->release_pending && st->release_at <= now_us) {
			touch_release(s, k, now_us);
		} else if (st->touch && s->g.touch_hold_ms && now_us >= st->touch_until) {
			/* a lost release healed: the host hears the touch end; the motor follows the
			 * served target through the trailing flush, as a lapsed hold always does */
			st->touch = 0;
			struct surface_event ev = {
				.kind = SURFACE_EV_FADER_RELEASED, .strip = (uint8_t)k,
				.target = surface_strip_target(s, k),
				.position = strip_position(s, st, surface_strip_target(s, k)),
			};
			surface_emit(s, &ev);
		}
	}
	motor_tick(s, now_us);
	return SURFACE_OK;
}

const char *surface_strerror(int code)
{
	switch (code) {
	case SURFACE_OK:      return "ok";
	case SURFACE_E_ARG:   return "invalid argument";
	case SURFACE_E_FULL:  return "table full";
	case SURFACE_E_NOMEM: return "out of memory";
	}
	return "unknown error";
}
