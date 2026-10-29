/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * The test helpers every suite shares: a CHK macro that counts failures, a fake backend that
 * records every op, a fake host that answers the surface's moves the way a console does, and a
 * virtual clock that runs the surface's timers in order.
 *
 * The host has three manners, as the TypeScript suites' fakes had:
 *   HOST_ECHO    every move comes straight back as a served frame (FakeHost);
 *   HOST_LAG     a move is recorded and its echo held until the test delivers it (LaggingHost),
 *                stamped with the time the host committed the move: its send time;
 *   HOST_REFUSE  a move is refused: the surface hears refused() and the row the host holds.
 * An echo is delivered after the input call returns, at the same time.
 */
#ifndef SURFACE_TEST_HARNESS_H
#define SURFACE_TEST_HARNESS_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <surface/surface.h>

static int fails;
#define CHK(cond) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

#define MS 1000ull

/* One case: a function, run by name; a failing case names itself after its checks. */
#define T(name) static void name(void)
#define RUN(name) do { \
	int before_ = fails; \
	name(); \
	if (fails != before_) fprintf(stderr, "  ^ in %s\n", #name); \
} while (0)

/* ---- the backend ------------------------------------------------------------------------- */

#define LOG_MAX 4096

struct op {
	char kind;          /* 'f' fader, 'l' led, 'r' ring, 'm' meter, 's' scribble, 'c' counter, 'k' colour */
	unsigned strip, side, row;
	uint32_t value;     /* raw, LED state, mask, segments */
	char text[40];
};

struct fake {
	struct surface *s;
	struct surface_geometry g;
	uint64_t now;
	struct op ops[LOG_MAX];
	int nops;
	/* a hook on every fader write, for the tests that call back in from the backend */
	void (*on_fader)(struct fake *f, unsigned strip, uint16_t raw);
	/* events */
	struct surface_event ev[LOG_MAX];
	int nev;
	/* the host */
	enum { HOST_ECHO, HOST_LAG, HOST_REFUSE } manner;
	surface_target pending_t[LOG_MAX];
	double pending_p[LOG_MAX];
	int npending;
	surface_target held_t[LOG_MAX];    /* HOST_LAG: the echoes the test delivers */
	double held_p[LOG_MAX];
	uint64_t held_at[LOG_MAX];         /* HOST_LAG: when the host committed each (its send time) */
	int nheld;
	double row[2048];                  /* the host's row per target (targets < 2048) */
	int moves;                         /* FADER_MOVED events seen */
	double last_move;
};

static struct fake F;

static inline void log_op(struct op o)
{
	if (F.nops < LOG_MAX)
		F.ops[F.nops++] = o;
}

static inline int op_fader(void *b, unsigned strip, uint16_t raw)
{
	(void)b;
	log_op((struct op){ .kind = 'f', .strip = strip, .value = raw });
	if (F.on_fader)
		F.on_fader(&F, strip, raw);
	return 1;
}
static inline int op_led(void *b, uint16_t key, enum surface_led st)
{
	(void)b;
	log_op((struct op){ .kind = 'l', .strip = key, .value = (uint32_t)st });
	return 1;
}
static inline int op_ring(void *b, unsigned strip, uint32_t mask)
{
	(void)b;
	log_op((struct op){ .kind = 'r', .strip = strip, .value = mask });
	return 1;
}
static inline int op_meter(void *b, unsigned strip, unsigned side, unsigned segments)
{
	(void)b;
	log_op((struct op){ .kind = 'm', .strip = strip, .side = side, .value = segments });
	return 1;
}
static inline int op_scribble(void *b, unsigned strip, unsigned row, const char *text, size_t n)
{
	(void)b;
	struct op o = { .kind = 's', .strip = strip, .row = row };
	memcpy(o.text, text, n < sizeof o.text - 1 ? n : sizeof o.text - 1);
	log_op(o);
	return 1;
}
static inline int op_counter(void *b, const char *text)
{
	(void)b;
	struct op o = { .kind = 'c' };
	strncpy(o.text, text, sizeof o.text - 1);
	log_op(o);
	return 1;
}
static inline int op_colour(void *b, const uint8_t *per_strip, size_t n)
{
	(void)b;
	struct op o = { .kind = 'k', .value = (uint32_t)n };
	memcpy(o.text, per_strip, n < sizeof o.text ? n : sizeof o.text);
	log_op(o);
	return 1;
}

static const struct surface_backend_ops FAKE_OPS = {
	op_fader, op_led, op_ring, op_meter, op_scribble, op_counter, op_colour,
};

/* ---- the host ----------------------------------------------------------------------------- */

static inline void on_event(void *ctx, const struct surface_event *ev)
{
	(void)ctx;
	if (F.nev < LOG_MAX)
		F.ev[F.nev++] = *ev;
	if (ev->kind != SURFACE_EV_FADER_MOVED || !ev->target)
		return;
	F.moves++;
	F.last_move = ev->position;
	if (ev->target < 2048 && F.manner == HOST_ECHO)
		F.row[ev->target] = ev->position;
	if (F.manner == HOST_LAG) {
		F.held_t[F.nheld] = ev->target;
		F.held_at[F.nheld] = F.now;
		F.held_p[F.nheld++] = ev->position;
		return;
	}
	F.pending_t[F.npending] = ev->target;
	F.pending_p[F.npending++] = ev->position;
}

/* What the host does after the surface's input returns: echo, or refuse and repaint its row. */
static inline void host_answer(void)
{
	for (int i = 0; i < F.npending; i++) {
		surface_target t = F.pending_t[i];
		if (F.manner == HOST_REFUSE) {
			surface_fader_refused(F.s, t);
			surface_fader_served(F.s, t, F.row[t], F.now);
		} else {
			surface_fader_served(F.s, t, F.pending_p[i], F.now);
		}
	}
	F.npending = 0;
}

/* HOST_LAG: deliver the held echoes in `order` (indices into the held list; NULL = in order),
 * each with its send time. */
static inline void host_deliver(const int *order, int n)
{
	surface_target t[LOG_MAX];
	double p[LOG_MAX];
	uint64_t at[LOG_MAX];
	int nh = F.nheld;
	memcpy(t, F.held_t, sizeof t[0] * (size_t)nh);
	memcpy(p, F.held_p, sizeof p[0] * (size_t)nh);
	memcpy(at, F.held_at, sizeof at[0] * (size_t)nh);
	F.nheld = 0;
	if (!order)
		n = nh;
	for (int i = 0; i < n; i++) {
		int k = order ? order[i] : i;
		surface_fader_served_at(F.s, t[k], p[k], at[k], F.now);
	}
}

/* ---- setup and the clock -------------------------------------------------------------- */

static inline void fake_setup(enum surface_model m)
{
	if (F.s)
		surface_destroy(F.s);
	memset(&F, 0, sizeof F);
	F.now = 1000 * MS;
	surface_geometry_preset(m, &F.g);
	struct surface_config c = {
		.geometry = F.g, .ops = &FAKE_OPS, .event = on_event,
	};
	if (surface_create(&F.s, &c) != SURFACE_OK) {
		fprintf(stderr, "NOT A RESULT: surface_create failed\n");
		exit(2);
	}
}

/* Run every timer due up to now + ms, each at its own time, then stand at now + ms. */
static inline void advance(uint64_t ms)
{
	uint64_t end = F.now + ms * MS;
	int guard = 0;
	for (uint64_t d; (d = surface_deadline(F.s, F.now)) <= end; ) {
		if (++guard > 100000) {
			fprintf(stderr, "NOT A RESULT: a timer that never ends\n");
			exit(2);
		}
		F.now = d;
		surface_tick(F.s, F.now);
		host_answer();
	}
	F.now = end;
	surface_tick(F.s, F.now);
	host_answer();
}

/* ---- gestures and reading back ---------------------------------------------------------- */

static inline uint16_t raw_of(double pos)
{
	return (uint16_t)floor(pos * F.g.fader_max + 0.5);
}

static inline void touch(unsigned strip, int on)
{
	surface_in_touch(F.s, strip, on, F.now);
	host_answer();
}

static inline void bend(unsigned strip, double pos)
{
	surface_in_fader(F.s, strip, raw_of(pos), F.now);
	host_answer();
}

static inline void bend_raw(unsigned strip, uint16_t raw)
{
	surface_in_fader(F.s, strip, raw, F.now);
	host_answer();
}

static inline void serve(surface_target t, double pos)
{
	if (t < 2048)
		F.row[t] = pos;
	surface_fader_served(F.s, t, pos, F.now);
}

static inline void clear_ops(void)
{
	F.nops = 0;
}

static inline void clear_events(void)
{
	F.nev = 0;
}

/* The positions written to strip's motor, in order; returns how many. */
static inline int motor_writes(unsigned strip, double *out, int max)
{
	int n = 0;
	for (int i = 0; i < F.nops; i++)
		if (F.ops[i].kind == 'f' && F.ops[i].strip == strip) {
			if (n < max)
				out[n] = (double)F.ops[i].value / F.g.fader_max;
			n++;
		}
	return n;
}

static inline int nwrites(unsigned strip)
{
	return motor_writes(strip, NULL, 0);
}

static inline double last_write(unsigned strip)
{
	double w[LOG_MAX];
	int n = motor_writes(strip, w, LOG_MAX);
	return n ? w[n - 1] : -1;
}

static inline int count_events(enum surface_event_kind k)
{
	int n = 0;
	for (int i = 0; i < F.nev; i++)
		if (F.ev[i].kind == k)
			n++;
	return n;
}

static inline const struct surface_event *last_event(enum surface_event_kind k)
{
	for (int i = F.nev - 1; i >= 0; i--)
		if (F.ev[i].kind == k)
			return &F.ev[i];
	return NULL;
}

/* ---- the X-Touch fixture -----------------------------------------------------------------
 * Input i is target i (strip i - 1 on the first bank); the main mix is MAIN, last in the list
 * and on the master fader (strip 8), as the adapter's fader list puts it. The board is painted
 * at start, as the adapter's start() paints it: every strip seeded at 0. */
#define MAIN 1000
#define MASTER 8
#define NINPUTS 16

static inline void xtouch_start(void)
{
	fake_setup(SURFACE_MODEL_XTOUCH);
	surface_target list[NINPUTS + 1];
	for (int i = 0; i < NINPUTS; i++)
		list[i] = (surface_target)(i + 1);
	list[NINPUTS] = MAIN;
	surface_bank_set(F.s, list, NINPUTS + 1);
	surface_master_set(F.s, MAIN);
	surface_in_online(F.s, F.now);
	for (unsigned k = 0; k <= MASTER; k++)
		surface_fader_paint(F.s, surface_strip_target(F.s, k), 0, F.now);
	clear_ops();
	clear_events();
}

/* The Control|24 fixture: 24 strips showing targets 1..24, a little time after the paint. */
static inline void c24_start(void)
{
	fake_setup(SURFACE_MODEL_CONTROL24);
	surface_target list[24];
	for (int i = 0; i < 24; i++)
		list[i] = (surface_target)(i + 1);
	surface_bank_set(F.s, list, 24);
	surface_in_online(F.s, F.now);
	for (unsigned k = 0; k < 24; k++)
		surface_fader_paint(F.s, (surface_target)(k + 1), 0, F.now);
	clear_ops();
	clear_events();
}

static inline int finish(const char *name)
{
	if (F.s)
		surface_destroy(F.s);
	F.s = NULL;
	if (fails) {
		fprintf(stderr, "%s: %d check(s) failed\n", name, fails);
		return 1;
	}
	printf("%s: ok\n", name);
	return 0;
}

#endif
