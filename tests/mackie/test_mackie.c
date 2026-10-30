/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * The Mackie module, driven through MIDI bytes both ways: the handshake brings the surface online
 * and replays the board, the device's messages become the core's inputs, and every element goes
 * out in its MCU encoding.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <mackie/mackie_codec.h>
#include <surface/surface_mackie.h>

static int fails;
#define CHK(c) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define MS 1000ull

static const struct mackie_map MAP = {
	.strip_note = {
		{ 0, 1, 2, 3, 4, 5, 6, 7 }, { 8, 9, 10, 11, 12, 13, 14, 15 }, { 16, 17, 18, 19, 20, 21, 22, 23 },
		{ 24, 25, 26, 27, 28, 29, 30, 31 }, { 32, 33, 34, 35, 36, 37, 38, 39 },
	},
	.touch_note = { 104, 105, 106, 107, 108, 109, 110, 111, 112 },
	.vpot_cc = { 16, 17, 18, 19, 20, 21, 22, 23 },
	.ring_cc = { 48, 49, 50, 51, 52, 53, 54, 55 },
	.fader_channels = 0x1ff,
	.global_note = { 46, 47, 48, 49, 50, 84, 86, 91, 92, 93, 94, 95, 70, 71, 72, 73 },
	.assignment_cc = 74,
	.timecode_cc = 64,
	.timecode_digits = 10,
	.jog_cc = 60,
};

/* every message written, and every event */
struct rig {
	struct surface_mackie *m;
	struct surface *s;
	uint8_t wire[512][64];
	size_t len[512];
	int n;
	struct surface_event ev[64];
	int nev;
	uint64_t now;
};

static int on_write(void *ctx, const uint8_t *msg, size_t len)
{
	struct rig *r = ctx;
	if (r->n < 512 && len <= 64) {
		memcpy(r->wire[r->n], msg, len);
		r->len[r->n++] = len;
	}
	return (int)len;
}

static void on_event(void *ctx, const struct surface_event *e)
{
	struct rig *r = ctx;
	if (r->nev < 64)
		r->ev[r->nev++] = *e;
}

static int open_rig(struct rig *r)
{
	memset(r, 0, sizeof *r);
	r->now = 1000 * MS;
	struct surface_mackie_config c = { .map = &MAP, .model = MACKIE_MODEL_MCU, .geometry = SURFACE_MODEL_XTOUCH,
					   .write = on_write, .write_ctx = r, .event = on_event, .event_ctx = r };
	return surface_mackie_open(&r->m, &c, &r->s);
}

/* was this exact message written since index `from`? */
static int wrote(const struct rig *r, int from, const uint8_t *msg, size_t len)
{
	for (int i = from; i < r->n; i++)
		if (r->len[i] == len && !memcmp(r->wire[i], msg, len))
			return 1;
	return 0;
}

static const uint8_t QUERY[] = { 0xf0, 0x00, 0x00, 0x66, 0x14, 0x01, '0', '1', '5', '6', '4', '0', 'B',
				 0x0a, 0x0b, 0x0c, 0x0d, 0xf7 };

static void handshake(struct rig *r)
{
	CHK(surface_mackie_input(r->m, QUERY, sizeof QUERY, r->now) == 1);
}

static void test_handshake_brings_it_online_and_replays_the_board(void)
{
	struct rig r;
	CHK(open_rig(&r) == SURFACE_OK);
	surface_target t[] = { 11, 12 };
	surface_bank_set(r.s, t, 2);
	surface_scribble(r.s, 0, 0, "KICK");
	surface_mackie_assignment(r.m, "GA");
	CHK(r.n == 0);               /* offline: kept, nothing sent */
	CHK(!surface_online(r.s));
	CHK(surface_mackie_query(r.m) == 1);
	const uint8_t q[] = { 0xf0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xf7 };
	CHK(r.n == 1 && r.len[0] == sizeof q && !memcmp(r.wire[0], q, sizeof q));
	int from = r.n;
	handshake(&r);
	CHK(surface_online(r.s));
	/* the reply echoes the serial, then the version request, then the board */
	CHK(r.n > from + 2 && r.wire[from][5] == MACKIE_HS_HOST_CONNECTION_REPLY);
	CHK(r.wire[from + 1][5] == MACKIE_HS_VERSION_REQUEST);
	struct mackie_msg sc;
	mackie_strip_scribble_encode(0, 0, "KICK", MACKIE_MODEL_MCU, &sc);
	CHK(wrote(&r, from, sc.bytes, sc.len));
	struct mackie_msg as[2];
	mackie_assignment_encode(&MAP, "GA", as);
	CHK(wrote(&r, from, as[0].bytes, as[0].len) && wrote(&r, from, as[1].bytes, as[1].len));
	int online = 0;
	for (int i = 0; i < r.nev; i++)
		online += r.ev[i].kind == SURFACE_EV_ONLINE;
	CHK(online == 1);
	const uint8_t ver[] = { 0xf0, 0x00, 0x00, 0x66, 0x14, 0x14, 'V', '1', '.', '2', '2', 0xf7 };
	CHK(surface_mackie_input(r.m, ver, sizeof ver, r.now) == 1);
	CHK(!strcmp(surface_mackie_version(r.m), "V1.22"));
	surface_mackie_close(r.m);
}

static void test_inputs_become_the_cores_events(void)
{
	struct rig r;
	CHK(open_rig(&r) == SURFACE_OK);
	surface_target t[] = { 11, 12, 13 };
	surface_bank_set(r.s, t, 3);
	handshake(&r);
	r.nev = 0;
	const uint8_t touch[] = { 0x90, 105, 0x7f };       /* strip 2's touch */
	const uint8_t bend[] = { 0xe1, 0x00, 0x40 };        /* strip 2's fader, 0x2000 */
	const uint8_t key[] = { 0x90, 26, 0x7f };           /* strip 3's SELECT */
	const uint8_t vpot[] = { 0xb0, 18, 0x41 };          /* strip 3's V-Pot, one tick counter-clockwise */
	const uint8_t jog[] = { 0xb0, 60, 0x02 };           /* the jog, two ticks clockwise */
	const uint8_t other[] = { 0xb0, 99, 0x01 };         /* no control of the map */
	CHK(surface_mackie_input(r.m, touch, 3, r.now) == 1);
	CHK(surface_mackie_input(r.m, bend, 3, r.now) == 1);
	CHK(surface_mackie_input(r.m, key, 3, r.now) == 1);
	CHK(surface_mackie_input(r.m, vpot, 3, r.now) == 1);
	CHK(surface_mackie_input(r.m, jog, 3, r.now) == 1);
	CHK(surface_mackie_input(r.m, other, 3, r.now) == 0);
	int touched = 0, moved = 0, keyed = 0, turned = 0, jogged = 0;
	for (int i = 0; i < r.nev; i++) {
		const struct surface_event *e = &r.ev[i];
		touched += e->kind == SURFACE_EV_FADER_TOUCHED && e->strip == 1 && e->target == 12;
		moved += e->kind == SURFACE_EV_FADER_MOVED && e->strip == 1 && fabs(e->position - 8192.0 / 16383) < 1e-9;
		keyed += e->kind == SURFACE_EV_KEY && e->key == 26 && e->on;
		turned += e->kind == SURFACE_EV_VPOT && e->strip == 2 && e->delta == -1;
		jogged += e->kind == SURFACE_EV_JOG && e->delta == 2;
	}
	CHK(touched == 1 && moved == 1 && keyed == 1 && turned == 1 && jogged == 1);
	surface_mackie_close(r.m);
}

static void test_elements_go_out_in_mcu_encodings(void)
{
	struct rig r;
	CHK(open_rig(&r) == SURFACE_OK);
	surface_target t[] = { 11 };
	surface_bank_set(r.s, t, 1);
	handshake(&r);
	int from = r.n;
	surface_fader_served(r.s, 11, 1.0, r.now);
	const uint8_t top[] = { 0xe0, 0x7f, 0x7f };
	CHK(wrote(&r, from, top, 3));
	surface_led(r.s, 24, SURFACE_LED_ON);
	const uint8_t lit[] = { 0x90, 24, 0x7f };
	CHK(wrote(&r, from, lit, 3));
	/* a ring in the device's own code goes out as it is, and replays as it is */
	CHK(surface_ring_code(r.s, 3, surface_mackie_ring_code(MACKIE_RING_BOOST_CUT, 6, 1)) == 1);
	const uint8_t ring[] = { 0xb0, 51, 0x40 | 0x10 | 6 };
	CHK(wrote(&r, from, ring, 3));
	/* a ring rendered by the core comes back to the device's code: a fill to LED 4 is wrap 4 */
	surface_ring(r.s, 4, SURFACE_RING_FILL, 4.0 / 11);
	const uint8_t wrap[] = { 0xb0, 52, 0x20 | 4 };
	CHK(wrote(&r, from, wrap, 3));
	surface_counter(r.s, "1");
	struct mackie_msg tc[16];
	size_t n = 0;
	mackie_timecode_encode(&MAP, "1", tc, 16, &n);
	CHK(n > 0 && wrote(&r, from, tc[n - 1].bytes, tc[n - 1].len));
	int replay = r.n;
	surface_in_offline(r.s, r.now);
	handshake(&r);
	CHK(wrote(&r, replay, ring, 3));
	surface_mackie_close(r.m);
}

static int open_rig_strips(struct rig *r, uint8_t strips)
{
	memset(r, 0, sizeof *r);
	r->now = 1000 * MS;
	struct surface_mackie_config c = { .map = &MAP, .model = MACKIE_MODEL_MCU, .geometry = SURFACE_MODEL_XTOUCH, .strips = strips,
					   .write = on_write, .write_ctx = r, .event = on_event, .event_ctx = r };
	return surface_mackie_open(&r->m, &c, &r->s);
}

static int count_ev(const struct rig *r, enum surface_event_kind k)
{
	int n = 0;
	for (int i = 0; i < r->nev; i++)
		n += r->ev[i].kind == k;
	return n;
}

static int fader_writes(const struct rig *r, int from, unsigned wire)
{
	int n = 0;
	for (int i = from; i < r->n; i++)
		n += r->wire[i][0] == (0xe0 | wire);
	return n;
}

static void bend(struct rig *r, unsigned wire, unsigned value)
{
	const uint8_t m[] = { (uint8_t)(0xe0 | wire), value & 0x7f, (value >> 7) & 0x7f };
	surface_mackie_input(r->m, m, sizeof m, r->now);
}

static void touch(struct rig *r, unsigned note, int on)
{
	const uint8_t m[] = { 0x90, (uint8_t)note, on ? 0x7f : 0 };
	surface_mackie_input(r->m, m, sizeof m, r->now);
}

/* A meter goes out in LEDs: six segments are the nibble 6, as libmackie's golden has it. */
static void test_meter_goes_out_in_segments(void)
{
	struct rig r;
	CHK(open_rig(&r) == SURFACE_OK);
	handshake(&r);
	int from = r.n;
	CHK(surface_meter(r.s, 2, 0, 0.5, r.now) == 1);
	const uint8_t six[] = { 0xd0, 0x20 | 6 };
	CHK(wrote(&r, from, six, 2));
	/* the overload LED is its own message and leaves the level alone */
	CHK(surface_mackie_clip(r.m, 2, 1) == 1);
	const uint8_t lit[] = { 0xd0, 0x20 | 0x0e };
	CHK(wrote(&r, from, lit, 2));
	CHK(surface_mackie_clip(r.m, 2, 0) == 1);
	const uint8_t clear[] = { 0xd0, 0x20 | 0x0f };
	CHK(wrote(&r, from, clear, 2));
	/* the keep-alive and the reset of the hold: the same level again is silent, until it is owed */
	r.now += 10 * MS;
	int before = r.n;
	CHK(surface_meter(r.s, 2, 0, 0.5, r.now) == 0);
	surface_meters_reset(r.s);
	CHK(surface_meter(r.s, 2, 0, 0.5, r.now) == 1 && r.n == before + 1);
	surface_mackie_close(r.m);
}

/* The master sits on channel 8 and its touch note whatever the width; a channel past a narrower
 * surface's last strip is nobody's. */
static void test_a_narrow_surface_keeps_its_master_on_channel_8(void)
{
	struct rig r;
	CHK(open_rig_strips(&r, 4) == SURFACE_OK);
	surface_target t[] = { 11, 12, 13, 14 };
	surface_bank_set(r.s, t, 4);
	surface_master_set(r.s, 99);
	handshake(&r);
	int from = r.n;
	surface_fader_served(r.s, 99, 1.0, r.now);
	const uint8_t top[] = { 0xe8, 0x7f, 0x7f };
	CHK(wrote(&r, from, top, 3));
	CHK(fader_writes(&r, from, 4) == 0);
	r.nev = 0;
	touch(&r, 112, 1);              /* the master's touch */
	CHK(count_ev(&r, SURFACE_EV_FADER_TOUCHED) == 1 && r.ev[0].strip == 4 && r.ev[0].target == 99);
	touch(&r, 106, 1);              /* wire strip 3 is on the surface */
	CHK(count_ev(&r, SURFACE_EV_FADER_TOUCHED) == 2);
	const uint8_t off[] = { 0x90, 108, 0x7f }, past[] = { 0xe6, 0, 0x40 };
	CHK(surface_mackie_input(r.m, off, 3, r.now) == 0);
	CHK(surface_mackie_input(r.m, past, 3, r.now) == 0);
	/* the eight-colour message stays eight */
	uint8_t c[] = { 1, 2, 3, 4 };
	from = r.n;
	CHK(surface_colour(r.s, c, 4) == 1);
	const uint8_t want[] = { 0xf0, 0, 0, 0x66, 0x14, 0x72, 1, 2, 3, 4, 0, 0, 0, 0, 0xf7 };
	CHK(wrote(&r, from, want, sizeof want));
	surface_mackie_close(r.m);
}

/* The motor, through MIDI bytes both ways: a hand on the cap writes nothing until it lets go, once. */
static void test_the_motor_never_fights_the_hand(void)
{
	struct rig r;
	CHK(open_rig(&r) == SURFACE_OK);
	surface_target t[] = { 11 };
	surface_bank_set(r.s, t, 1);
	handshake(&r);
	int from = r.n;
	touch(&r, 104, 1);
	bend(&r, 0, 4000);
	bend(&r, 0, 8000);
	surface_fader_served(r.s, 11, 0.1, r.now);      /* a lagging echo under the hand */
	CHK(fader_writes(&r, from, 0) == 0);
	touch(&r, 104, 0);
	CHK(fader_writes(&r, from, 0) == 1);
	const uint8_t rest[] = { 0xe0, 8000 & 0x7f, 8000 >> 7 };
	CHK(wrote(&r, from, rest, 3));
	/* the motor's report of that write is no gesture */
	r.nev = 0;
	CHK(surface_fader_is_report(r.s, 0, 8000, r.now) == 1);
	CHK(surface_fader_is_report(r.s, 0, 4000, r.now) == 0);
	bend(&r, 0, 8000);
	CHK(count_ev(&r, SURFACE_EV_FADER_MOVED) == 0);
	/* a touch that never lifts lapses on its own, and a reset clears it at once */
	touch(&r, 104, 1);
	CHK(surface_fader_is_report(r.s, 0, 8000, r.now) == 0);
	surface_touch_reset(r.s);
	CHK(surface_fader_is_report(r.s, 0, 8000, r.now) == 1);
	surface_mackie_close(r.m);
}

/* A strip painted on its own, and a target under a layer: its moves go up and move no motor. */
static void test_a_layer_keeps_the_motor_state_out(void)
{
	struct rig r;
	CHK(open_rig(&r) == SURFACE_OK);
	surface_target t[] = { 11 };
	surface_bank_set(r.s, t, 1);
	handshake(&r);
	int from = r.n;
	surface_fader_paint_strip(r.s, 0, 0.5, r.now);
	CHK(fader_writes(&r, from, 0) == 1);
	surface_fader_suspend(r.s, 11, 1);
	from = r.n;
	r.nev = 0;
	touch(&r, 104, 1);
	bend(&r, 0, 2000);
	touch(&r, 104, 0);
	r.now += 2000 * MS;
	surface_tick(r.s, r.now);
	CHK(count_ev(&r, SURFACE_EV_FADER_MOVED) == 1 && count_ev(&r, SURFACE_EV_FADER_RELEASED) == 1);
	CHK(fader_writes(&r, from, 0) == 0);
	/* leaving the layer: no echo guard was left behind, the next served value drives the cap */
	surface_fader_suspend(r.s, 11, 0);
	surface_fader_served(r.s, 11, 0.9, r.now);
	CHK(fader_writes(&r, from, 0) == 1);
	surface_mackie_close(r.m);
}

int main(void)
{
	test_meter_goes_out_in_segments();
	test_a_narrow_surface_keeps_its_master_on_channel_8();
	test_the_motor_never_fights_the_hand();
	test_a_layer_keeps_the_motor_state_out();
	test_handshake_brings_it_online_and_replays_the_board();
	test_inputs_become_the_cores_events();
	test_elements_go_out_in_mcu_encodings();
	if (fails)
		fprintf(stderr, "test_mackie: %d failed\n", fails);
	else
		printf("test_mackie: ok\n");
	return fails ? 1 : 0;
}
