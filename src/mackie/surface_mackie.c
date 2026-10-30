/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * surface_mackie: a Mackie Control surface (the X-Touch) under libsurface's core, libmackie as its
 * codec, the MIDI transport the host's (include/surface/surface_mackie.h).
 */
#include <stdlib.h>
#include <string.h>

#include <mackie/mackie.h>
#include <mackie/mackie_codec.h>
#include <surface/surface_mackie.h>

#define TIMECODE_MAX 16

struct surface_mackie {
	struct surface *s;
	const struct mackie_map *map;
	uint8_t model;
	surface_mackie_write_fn write;
	void *write_ctx;
	char assignment[3];
	int assignment_set;
	char version[32];
};

static int send(struct surface_mackie *m, const struct mackie_msg *msg)
{
	int rc = m->write(m->write_ctx, msg->bytes, msg->len);
	return rc < 0 ? rc : 1;
}

/* The master fader sits on pitch-bend channel 8 and touch note 8 of the map, whatever the width. */
static unsigned wire_strip(const struct surface_mackie *m, unsigned strip)
{
	return strip >= surface_geometry_of(m->s)->strips ? MACKIE_STRIPS : strip;
}

static unsigned core_strip(const struct surface_mackie *m, unsigned wire)
{
	return wire >= MACKIE_STRIPS ? surface_geometry_of(m->s)->strips : wire;
}

/* ---- The backend: libsurface's element writes in MCU encodings ---------------------------- */

static int op_fader(void *b, unsigned strip, uint16_t raw)
{
	struct surface_mackie *m = b;
	struct mackie_msg msg;
	int rc = mackie_fader_encode14(wire_strip(m, strip), raw, &msg);
	return rc < 0 ? SURFACE_E_ARG : send(m, &msg);
}

static int op_led(void *b, uint16_t key, enum surface_led st)
{
	struct surface_mackie *m = b;
	struct mackie_msg msg;
	enum mackie_led l = st == SURFACE_LED_ON ? MACKIE_LED_STATE_ON
		: st == SURFACE_LED_FLASH ? MACKIE_LED_STATE_FLASH : MACKIE_LED_STATE_OFF;
	int rc = mackie_button_led_encode(key, l, &msg);
	return rc < 0 ? SURFACE_E_ARG : send(m, &msg);
}

static int op_ring_code(void *b, unsigned strip, uint32_t code)
{
	struct surface_mackie *m = b;
	if (strip >= MACKIE_STRIPS)
		return SURFACE_E_ARG;
	struct mackie_msg msg = { .bytes = { MACKIE_CONTROL_CHANGE, m->map->ring_cc[strip] & 0x7f,
					     (uint8_t)(code & 0x7f) }, .len = 3 };
	return send(m, &msg);
}

/* A rendered mask back to the MCU's own code, for a host that writes surface_ring(): the device
 * draws a dot, a fill from the left, a boost/cut from the centre or a spread, never any mask. */
static uint32_t mask_code(uint32_t mask, unsigned leds)
{
	if (!mask)
		return 0;
	unsigned lo = (unsigned)__builtin_ctz(mask), hi = 31u - (unsigned)__builtin_clz(mask);
	unsigned centre = leds / 2;
	if (lo == hi)
		return surface_mackie_ring_code(MACKIE_RING_SINGLE, lo + 1, 0);
	if (lo == 0)
		return surface_mackie_ring_code(MACKIE_RING_WRAP, hi + 1, 0);
	if (centre - lo == hi - centre)
		return surface_mackie_ring_code(MACKIE_RING_SPREAD, hi - centre + 1, 0);
	return surface_mackie_ring_code(MACKIE_RING_BOOST_CUT, lo < centre ? lo + 1 : hi + 1, 0);
}

static int op_ring(void *b, unsigned strip, uint32_t mask)
{
	struct surface_mackie *m = b;
	const struct surface_geometry *g = surface_geometry_of(m->s);
	return op_ring_code(b, strip, mask_code(mask, g ? g->ring_leds : 11));
}

static int op_meter(void *b, unsigned strip, unsigned side, unsigned segments)
{
	struct surface_mackie *m = b;
	struct mackie_msg msg;
	if (side != 0)
		return 0;
	int rc = mackie_meter_encode(strip, (double)segments, MACKIE_OVERLOAD_NONE, &msg);
	return rc < 0 ? SURFACE_E_ARG : send(m, &msg);
}

static int op_scribble(void *b, unsigned strip, unsigned row, const char *text, size_t n)
{
	struct surface_mackie *m = b;
	char cell[MACKIE_STRIP_CELL_CHARS + 1];
	size_t k = n < MACKIE_STRIP_CELL_CHARS ? n : MACKIE_STRIP_CELL_CHARS;
	memcpy(cell, text, k);
	cell[k] = 0;
	struct mackie_msg msg;
	int rc = mackie_strip_scribble_encode(strip, row, cell, m->model, &msg);
	return rc < 0 ? SURFACE_E_ARG : send(m, &msg);
}

static int op_counter(void *b, const char *text)
{
	struct surface_mackie *m = b;
	struct mackie_msg msg[TIMECODE_MAX];
	size_t n = 0;
	int rc = mackie_timecode_encode(m->map, text, msg, TIMECODE_MAX, &n);
	if (rc < 0)
		return SURFACE_E_ARG;
	for (size_t i = 0; i < n; i++)
		if ((rc = send(m, &msg[i])) < 0)
			return rc;
	return 1;
}

static int op_colour(void *b, const uint8_t *per_strip, size_t n)
{
	struct surface_mackie *m = b;
	unsigned c[MACKIE_STRIPS];
	if (n > MACKIE_STRIPS)
		n = MACKIE_STRIPS;
	for (size_t i = 0; i < n; i++)
		c[i] = per_strip[i];
	struct mackie_msg msg;
	int rc = mackie_scribble_colours_encode(c, n, m->model, &msg);
	return rc < 0 ? SURFACE_E_ARG : send(m, &msg);
}

static const struct surface_backend_ops OPS = {
	.fader = op_fader, .led = op_led, .ring = op_ring, .meter = op_meter, .scribble = op_scribble,
	.counter = op_counter, .colour = op_colour, .ring_code = op_ring_code,
};

/* ---- Open, close ---------------------------------------------------------------------------- */

uint32_t surface_mackie_ring_code(enum mackie_ring_mode mode, unsigned position, int centre)
{
	if (position > 11)
		position = 11;
	return (centre ? 0x40u : 0) | (((unsigned)mode & 3u) << 4) | position;
}

int surface_mackie_open(struct surface_mackie **out, const struct surface_mackie_config *c,
			struct surface **s)
{
	if (!out || !c || !c->map || !c->write || !s || mackie_map_check(c->map) != MACKIE_OK)
		return SURFACE_E_ARG;
	*out = NULL;
	*s = NULL;
	struct surface_mackie *m = calloc(1, sizeof *m);
	if (!m)
		return SURFACE_E_NOMEM;
	m->map = c->map;
	m->model = c->model ? c->model : MACKIE_MODEL_MCU;
	m->write = c->write;
	m->write_ctx = c->write_ctx;
	struct surface_config sc = { .ops = &OPS, .backend = m, .event = c->event, .event_ctx = c->event_ctx,
				     .max_targets = c->max_targets, .max_leds = c->max_leds };
	int rc = surface_geometry_preset(c->geometry ? c->geometry : SURFACE_MODEL_XTOUCH, &sc.geometry);
	if (rc == SURFACE_OK && c->strips) {
		if (c->strips > MACKIE_STRIPS)
			rc = SURFACE_E_ARG;
		else
			sc.geometry.strips = c->strips;
	}
	if (rc == SURFACE_OK)
		rc = surface_create(&m->s, &sc);
	if (rc != SURFACE_OK) {
		free(m);
		return rc;
	}
	*out = m;
	*s = m->s;
	return SURFACE_OK;
}

void surface_mackie_close(struct surface_mackie *m)
{
	if (!m)
		return;
	surface_destroy(m->s);
	free(m);
}

const char *surface_mackie_version(const struct surface_mackie *m)
{
	return m ? m->version : "";
}

/* ---- The assignment display: the module's own element, replayed on every online ------------ */

static int assignment_write(struct surface_mackie *m)
{
	struct mackie_msg msg[2];
	if (mackie_assignment_encode(m->map, m->assignment, msg) < 0)
		return SURFACE_E_ARG;
	int rc = send(m, &msg[0]);
	return rc < 0 ? rc : send(m, &msg[1]);
}

int surface_mackie_assignment(struct surface_mackie *m, const char *text)
{
	if (!m || !text)
		return SURFACE_E_ARG;
	size_t n = strlen(text);
	if (n > 2)
		n = 2;
	memcpy(m->assignment, text, n);
	m->assignment[n] = 0;
	m->assignment_set = 1;
	return surface_online(m->s) ? assignment_write(m) : 0;
}

int surface_mackie_clip(struct surface_mackie *m, unsigned strip, int clipped)
{
	struct mackie_msg msg;
	if (!m || strip >= MACKIE_STRIPS ||
	    mackie_meter_encode(strip, 0, clipped ? MACKIE_OVERLOAD_SET : MACKIE_OVERLOAD_CLEAR, &msg) < 0)
		return SURFACE_E_ARG;
	return send(m, &msg);
}

int surface_mackie_reset(struct surface_mackie *m)
{
	struct mackie_msg msg;
	if (!m || mackie_reset_encode(m->model, &msg) < 0)
		return SURFACE_E_ARG;
	return send(m, &msg);
}

void surface_mackie_online(struct surface_mackie *m, uint64_t now_us)
{
	if (!m)
		return;
	surface_in_online(m->s, now_us);
	if (m->assignment_set)
		assignment_write(m);
}

int surface_mackie_query(struct surface_mackie *m)
{
	struct mackie_msg msg;
	if (!m || mackie_device_query_encode(m->model, &msg) < 0)
		return SURFACE_E_ARG;
	return send(m, &msg);
}

/* ---- Input: one MIDI message from the device ------------------------------------------------ */

static int sysex_in(struct surface_mackie *m, const uint8_t *msg, size_t len, uint64_t now_us)
{
	char text[sizeof m->version];
	if (mackie_version_reply_decode(msg, len, m->model, text, sizeof text) >= 0) {
		memcpy(m->version, text, sizeof text);
		return 1;
	}
	struct mackie_handshake h;
	if (mackie_handshake_decode(msg, len, m->model, &h) < 0)
		return 0;
	if (h.sub != MACKIE_HS_HOST_CONNECTION_QUERY)
		return 1;
	uint8_t resp[MACKIE_CHALLENGE_LEN];
	struct mackie_msg out;
	mackie_handshake_response(h.challenge, h.challenge_len, resp);
	if (mackie_host_connection_reply_encode(h.serial, h.serial_len, resp, sizeof resp, m->model, &out) < 0)
		return SURFACE_E_ARG;
	send(m, &out);
	if (mackie_version_request_encode(m->model, &out) >= 0)
		send(m, &out);
	surface_mackie_online(m, now_us);
	return 1;
}

int surface_mackie_input(struct surface_mackie *m, const uint8_t *msg, size_t len, uint64_t now_us)
{
	if (!m || !msg || !len)
		return SURFACE_E_ARG;
	uint8_t status = msg[0] & 0xf0;
	if (msg[0] == MACKIE_SYSEX_START)
		return sysex_in(m, msg, len, now_us);
	if (status == MACKIE_PITCH_BEND) {
		struct mackie_fader f;
		if (mackie_fader_decode(m->map, msg, len, &f) < 0)
			return 0;
		surface_in_fader(m->s, core_strip(m, f.channel), f.value, now_us);
		return 1;
	}
	if (status == MACKIE_NOTE_ON || status == MACKIE_NOTE_OFF) {
		struct mackie_button btn;
		if (mackie_button_decode(m->map, msg, len, &btn) < 0)
			return 0;
		if (btn.kind == MACKIE_BTN_FADER_TOUCH)
			surface_in_touch(m->s, core_strip(m, (unsigned)btn.strip), btn.pressed, now_us);
		else
			surface_in_key(m->s, msg[1], btn.pressed, now_us);
		return 1;
	}
	if (status == MACKIE_CONTROL_CHANGE) {
		struct mackie_vpot v;
		int delta;
		if (mackie_vpot_decode(m->map, msg, len, &v) >= 0) {
			surface_in_vpot(m->s, v.encoder, v.delta, now_us);
			return 1;
		}
		if (mackie_jog_decode(m->map, msg, len, &delta) >= 0) {
			surface_in_jog(m->s, delta, now_us);
			return 1;
		}
	}
	return 0;
}
