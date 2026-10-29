/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * The inside of struct surface, shared by the library's own sources. Not installed.
 */
#ifndef SURFACE_IMPL_H
#define SURFACE_IMPL_H

#include <surface/surface.h>

#define MS_US 1000ull

/*
 * Per-target motor state: the whole motor policy is these numbers plus the touch flags and an
 * implicit-touch deadline. There is no reconcile pass and no queue of past frames; the only
 * clock is a short implicit-touch window (a per-move debounce, not a fixed grace period) that
 * withholds served -> motor drive while a divergent report stream says the hand is on the cap
 * even with a flaky or absent touch sensor. The model is the canonical Mackie-Control one (a
 * fader has a touch flag and a target; the motor tracks the target only while untouched),
 * ported from bjoluc/cubase-mcu-midiremote's TouchSensitiveMotorFader, including its
 * implicit-touch handling, and cross-checked against DrivenByMoss's MCU FaderTouchCommand and
 * the TouchMCU protocol notes. Keyed by target (not strip) so it survives banking.
 */
struct fader_state {
	surface_target t;        /* 0: a free slot */
	/* Latest served position (0..1), coalesced: only ever the most recent frame matters. */
	double target;
	/* Position the motor was last driven to (the device's last host-commanded value). The
	 * no-op guard. */
	double motor_at;
	/*
	 * The value the SURFACE ITSELF last reported and is still waiting for the host to echo
	 * back. A pure DAW needs nothing here: the touch flag alone stops it re-driving the
	 * operator's own move, because host and surface share one value with no round trip. A
	 * console host does have a round trip: every move returns as a served frame (often a
	 * whole lagging burst of the gesture), and those land after the finger has lifted, i.e.
	 * untouched. So the touch flag's "don't fight my own move" is carried across that gap:
	 * while awaiting is set, served frames update target but never drive the motor; it clears
	 * the instant the host confirms this exact value (the host delivers in order, so the
	 * confirmation is the last of the burst). Not a timer.
	 */
	double awaiting;
	uint8_t has_awaiting;
	/* The implicit-touch window (SURFACE_IMPLICIT_TOUCH_MS): while now < implicit_until the
	 * operator is treated as holding this cap even without a touch message. */
	uint8_t implicit_set;
	uint64_t implicit_until;
	/* The trailing flush (SURFACE_MOTOR_COALESCE_MS); at most one per fader. It ends a burst,
	 * and it outlives a hold: while the fader is handled it re-arms, so the target withheld
	 * during the hold reaches the motor when the hold lapses. */
	uint8_t flush_armed;
	uint64_t flush_at;
	/* When the last served frame painted this fader: the burst detector's only state. */
	uint8_t has_paint;
	uint64_t last_paint_at;
	/* A repaint was held: whatever the cap showed before was written outside this policy, so
	 * motor_at no longer says where the cap is and the next drive writes even when it matches. */
	uint8_t repaint_held;
	/* The caps show something else now (a send layer): served frames only move the target. */
	uint8_t suspended;
};

struct strip_state {
	/* touch */
	uint8_t touch;            /* the flag; read through touch_held() */
	uint64_t touch_until;     /* the self-heal deadline when touch_hold_ms > 0 */
	uint8_t release_pending;  /* a release inside the debounce window */
	uint64_t release_at;
	/* reports */
	uint8_t have_report;      /* last_raw/last_at hold the last accepted report */
	uint16_t last_raw;
	uint64_t last_at;
	/* where the cap was last commanded, read off our own writes */
	uint8_t commanded_set;
	uint16_t commanded;
	/* shadow */
	uint8_t ring_set;
	uint32_t ring;
	uint8_t scribble_set[SURFACE_MAX_SCRIBBLE_ROWS];
	char scribble[SURFACE_MAX_SCRIBBLE_ROWS][SURFACE_MAX_SCRIBBLE_CHARS];
	int meter_step[SURFACE_MAX_METER_SIDES];   /* -1: fresh or reset */
	uint64_t meter_sent_at[SURFACE_MAX_METER_SIDES];
};

struct led_slot {
	uint16_t key;
	uint8_t used;
	uint8_t st;
};

struct surface {
	struct surface_geometry g;
	struct surface_backend_ops ops;
	void *backend;
	surface_event_fn event;
	void *event_ctx;
	int online;

	struct strip_state strip[SURFACE_MAX_STRIPS + 1];   /* + the master fader */
	surface_target master;

	/* the bank window */
	surface_target *list;
	size_t nlist, cap;
	size_t offset;

	struct fader_state *faders;   /* cap entries */

	struct led_slot *leds;
	size_t ledcap;

	uint8_t counter_set;
	char counter[SURFACE_MAX_COUNTER];
	size_t ncolour;
	uint8_t colour[SURFACE_MAX_STRIPS];
};

/* surface.c */
void surface_emit(struct surface *s, const struct surface_event *ev);
unsigned surface_nstrips(const struct surface *s);   /* strips + master */
int touch_held(const struct surface *s, unsigned strip, uint64_t now_us);
void meters_reset(struct surface *s);

/* motor.c: the per-target policy (port of MotorFaders) */
struct fader_state *motor_find(struct surface *s, surface_target t);
struct fader_state *motor_state(struct surface *s, surface_target t);
double motor_epsilon(const struct surface *s);
uint16_t motor_raw(const struct surface *s, double pos);
int motor_touched(const struct surface *s, surface_target t, uint64_t now_us);
int motor_handled(const struct surface *s, const struct fader_state *f, uint64_t now_us);
void motor_moved(struct surface *s, struct fader_state *f, double pos, uint64_t now_us);
void motor_released(struct surface *s, surface_target t, uint64_t now_us);
void motor_drive(struct surface *s, struct fader_state *f, double pos, uint64_t now_us);
int motor_write_strip(struct surface *s, unsigned strip, double pos, uint64_t now_us);
void motor_paint_strip(struct surface *s, unsigned strip, surface_target t, double pos, uint64_t now_us);
void motor_tick(struct surface *s, uint64_t now_us);
uint64_t motor_deadline(const struct surface *s);

#endif
