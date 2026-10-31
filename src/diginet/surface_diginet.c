// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* libsurface-diginet (surface/surface_diginet.h): the desk mux under one struct surface per
 * attached desk. libdiginet carries the wire and the session; libsurface owns the behaviour, the
 * Control|24 set-point echo included (holds_setpoint, libdiginet#29 section 5.3): every echo and
 * every drive reaches the desk through the fader op below, under one session queue key per
 * strip, so a newer one replaces a queued one and the two are never both in flight. */
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <surface/surface_diginet.h>

#include <diginet/diginet_codec.h>

struct desk {
	struct surface_diginet *d;
	int used;
	uint8_t mac[DIGINET_MAC_LEN];
	int slot;                                  /* the mux slot, -1 while gone */
	const struct diginet_personality *p;
	struct surface_desk_info info;
	struct surface *s;
	int online;
	int foreign_warned;                        /* until the next ONLINE or a free announce */
	uint8_t auto_bits[32];                     /* each strip's automation LEDs */
	uint8_t counter_bits;                      /* the counter-mode LEDs */
	char warning[96];
};

struct surface_diginet {
	struct diginet_mux mux;
	struct diginet_fd_io fdio;
	struct diginet_mux_io io;
	int fd;
	surface_desk_fn desk;
	void *ctx;
	uint64_t now;                              /* the caller's time of the call in progress */
	struct desk desks[DIGINET_MUX_MAX_DESKS];
};

/* ---- the desk table ---------------------------------------------------------------------- */

static struct desk *desk_by_mac(struct surface_diginet *d, const uint8_t *mac)
{
	for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS; i++)
		if (d->desks[i].used && !memcmp(d->desks[i].mac, mac, DIGINET_MAC_LEN))
			return &d->desks[i];
	return NULL;
}

static struct desk *desk_by_surface(struct surface_diginet *d, const struct surface *s)
{
	for (unsigned i = 0; s && i < DIGINET_MUX_MAX_DESKS; i++)
		if (d->desks[i].used && d->desks[i].s == s)
			return &d->desks[i];
	return NULL;
}

static struct diginet_session *desk_session(struct desk *k)
{
	return k->slot < 0 ? NULL : diginet_mux_session(&k->d->mux, (unsigned)k->slot);
}

static int desk_online(struct desk *k)
{
	struct diginet_session *ss = desk_session(k);
	return ss && ss->state == DIGINET_S_ONLINE;
}

/* 1 queued, 0 skipped (no session online), or a negative code. */
static int submit(struct desk *k, int rc, const struct diginet_cmd *c)
{
	if (rc != DIGINET_OK)
		return SURFACE_E_ARG;
	struct diginet_session *ss = desk_session(k);
	if (!ss || ss->state != DIGINET_S_ONLINE)
		return 0;
	return diginet_session_submit(ss, c) == DIGINET_OK ? 1 : SURFACE_E_FULL;
}

/* ---- the backend ops: libsurface -> the desk ------------------------------------------- */

static int op_fader(void *b, unsigned strip, uint16_t raw)
{
	struct desk *k = b;
	struct diginet_cmd c;
	return submit(k, diginet_fader_encode(k->p, strip, raw, &c), &c);
}

static int op_led(void *b, uint16_t key, enum surface_led st)
{
	struct desk *k = b;
	struct diginet_cmd c;
	int on = st != SURFACE_LED_OFF;
	if (!SURFACE_DIGINET_IS_AUTO_KEY(key))
		return submit(k, diginet_button_encode(key >> 8, key & 0xff, on, &c), &c);
	unsigned bit = (key >> 8) & 0x7f, id = key & 0xff;
	if (!bit || (bit & (bit - 1)))
		return SURFACE_E_ARG;   /* one LED per key */
	if (id == SURFACE_DIGINET_COUNTER_LEDS) {
		k->counter_bits = (uint8_t)(on ? k->counter_bits | bit : k->counter_bits & ~bit);
		return submit(k, diginet_counter_leds_encode(k->p, k->counter_bits, &c), &c);
	}
	if (id >= k->p->strips)
		return SURFACE_E_ARG;
	k->auto_bits[id] = (uint8_t)(on ? k->auto_bits[id] | bit : k->auto_bits[id] & ~bit);
	return submit(k, diginet_strip_leds_encode(k->p, id, k->auto_bits[id], &c), &c);
}

static int op_ring(void *b, unsigned strip, uint32_t mask)
{
	struct desk *k = b;
	struct diginet_cmd c;
	return submit(k, diginet_ring_encode(k->p, strip, (uint16_t)(mask & 0x7fff), &c), &c);
}

static int op_meter(void *b, unsigned strip, unsigned side, unsigned segments)
{
	struct desk *k = b;
	struct diginet_cmd c;
	return submit(k, diginet_meter_encode(k->p, strip, side != 0, segments, &c), &c);
}

/* Scribble n of strip n: one 4-character display per strip (R6), row 0 only. */
static int op_scribble(void *b, unsigned strip, unsigned row, const char *text, size_t n)
{
	struct desk *k = b;
	if (row || strip >= k->p->strips)
		return SURFACE_E_ARG;
	char t[SURFACE_MAX_SCRIBBLE_CHARS + 1];
	if (n > SURFACE_MAX_SCRIBBLE_CHARS)
		n = SURFACE_MAX_SCRIBBLE_CHARS;
	memcpy(t, text, n);
	t[n] = 0;
	uint8_t chars[DIGINET_SCRIBBLE_MAX];
	diginet_scribble_text(t, chars, k->p->scribble_chars);
	struct diginet_cmd c;
	return submit(k, diginet_scribble_encode(k->p, strip, chars, &c), &c);
}

static int op_counter(void *b, const char *text)
{
	struct desk *k = b;
	uint8_t seg[DIGINET_COUNTER_DIGITS], dots;
	if (diginet_counter_text(text, seg, &dots) != DIGINET_OK)
		return SURFACE_E_ARG;
	struct diginet_cmd c;
	return submit(k, diginet_counter_encode(k->p, seg, dots, &c), &c);
}

static const struct surface_backend_ops OPS = {
	.fader = op_fader, .led = op_led, .ring = op_ring, .meter = op_meter,
	.scribble = op_scribble, .counter = op_counter, .colour = NULL,
};

/* ---- the desk -> libsurface ------------------------------------------------------------ */

static void data_in(struct desk *k, const uint8_t *body, size_t len)
{
	struct diginet_decoder dec;
	struct diginet_event ev;
	uint64_t now = k->d->now;
	diginet_decoder_init(&dec, k->p, body, len);
	for (int rc; (rc = diginet_decoder_next(&dec, &ev)) != 0; ) {
		if (rc < 0 || !k->s)
			continue;
		switch (ev.kind) {
		case DIGINET_EV_FADER:
			surface_in_fader(k->s, ev.strip, ev.value, now);
			break;
		case DIGINET_EV_TOUCH:
			surface_in_touch(k->s, ev.strip, ev.on, now);
			break;
		case DIGINET_EV_STRIP_BUTTON:
			surface_in_key(k->s, SURFACE_DIGINET_KEY(ev.button, ev.strip), ev.on, now);
			break;
		case DIGINET_EV_SECTION_BUTTON:
			surface_in_key(k->s, SURFACE_DIGINET_KEY(ev.button, ev.zone), ev.on, now);
			break;
		case DIGINET_EV_VPOT:
			surface_in_vpot(k->s, ev.strip, ev.delta, now);
			break;
		case DIGINET_EV_JOG:
			surface_in_jog(k->s, ev.delta, now);
			break;
		default:
			break;   /* the desk sends nothing else a host acts on */
		}
	}
}

static void warn(struct desk *k, int code)
{
	if (k->s)
		surface_in_warning(k->s, code, k->warning, k->d->now);
}

static void session_notice(struct desk *k, const struct diginet_notice *n)
{
	switch (n->kind) {
	case DIGINET_N_STATE:
		if (n->state == DIGINET_S_ONLINE) {
			k->online = 1;
			k->foreign_warned = 0;
			/* A new session blanks the desk (R15): the whole board again, faders too. */
			if (k->s)
				surface_in_online(k->s, k->d->now);
		} else if (n->old_state == DIGINET_S_ONLINE) {
			k->online = 0;
			if (k->s)
				surface_in_offline(k->s, k->d->now);
		}
		break;
	case DIGINET_N_DATA:
		data_in(k, n->body, n->body_len);
		break;
	case DIGINET_N_DESK:
		/* an announce that names no other host ends a foreign run */
		if (n->frame && (n->frame->h.cmd == DIGINET_CMD_ANNOUNCE || n->mac_offset >= 0))
			k->foreign_warned = 0;
		break;
	case DIGINET_N_FOREIGN:
		if (k->foreign_warned)
			break;   /* the desk announces it every few seconds: once per run */
		k->foreign_warned = 1;
		if (n->frame && n->frame->body_len >= 6) {
			const uint8_t *m = n->frame->body;
			snprintf(k->warning, sizeof k->warning,
				 "desk in session with another host %02x:%02x:%02x:%02x:%02x:%02x",
				 m[0], m[1], m[2], m[3], m[4], m[5]);
		} else {
			snprintf(k->warning, sizeof k->warning, "desk in session with another host");
		}
		warn(k, SURFACE_WARN_FOREIGN);
		break;
	case DIGINET_N_DESK_DEAF:
		snprintf(k->warning, sizeof k->warning,
			 "desk deaf: %u wakes in a row timed out while it announces; power-cycle it on a quiet link",
			 (unsigned)n->counter);
		warn(k, SURFACE_WARN_DESK_DEAF);
		break;
	default:
		break;
	}
}

static void desk_info(struct desk *k, const struct diginet_mux_notice *n)
{
	struct surface_desk_info *i = &k->info;
	memset(i, 0, sizeof *i);
	memcpy(i->mac, n->mac, DIGINET_MAC_LEN);
	i->ifindex = n->ifindex;
	char ifn[IF_NAMESIZE];
	if (n->ifindex > 0 && if_indextoname((unsigned)n->ifindex, ifn))
		snprintf(i->ifname, sizeof i->ifname, "%s", ifn);
	if (n->announce) {
		snprintf(i->name, sizeof i->name, "%s", n->announce->name);
		snprintf(i->version, sizeof i->version, "%s", n->announce->version);
	}
	i->model = n->personality ? (int)n->personality->desk : DIGINET_DESK_UNKNOWN;
}

static void mux_notice(void *ctx, const struct diginet_mux_notice *n)
{
	struct surface_diginet *d = ctx;
	struct desk *k = desk_by_mac(d, n->mac);
	switch (n->kind) {
	case DIGINET_MUX_N_APPEARED:
		if (!k) {
			for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS && !k; i++)
				if (!d->desks[i].used)
					k = &d->desks[i];
			if (!k)
				return;   /* the mux holds no more desks than this table */
			memset(k, 0, sizeof *k);
			k->used = 1;
			k->d = d;
			memcpy(k->mac, n->mac, DIGINET_MAC_LEN);
		}
		k->slot = (int)n->desk;
		desk_info(k, n);
		/* The surface's ops read k->p, so a surface keeps the personality it was built for. A
		 * desk the host kept attached through its absence gets its session back; the same MAC
		 * announcing anything else (a name no personality has: n->personality is NULL) gets
		 * none, and its surface waits offline until the desk itself returns. */
		if (!k->s)
			k->p = n->personality;
		else if (k->info.model == DIGINET_DESK_CONTROL24)
			diginet_mux_attach(&d->mux, n->desk, NULL);
		d->desk(d->ctx, &k->info, 1);
		break;
	case DIGINET_MUX_N_GONE:
		if (!k)
			return;
		if (k->online && k->s)
			surface_in_offline(k->s, d->now);
		k->online = 0;
		k->slot = -1;
		d->desk(d->ctx, &k->info, 0);
		if (!k->s)
			k->used = 0;
		break;
	case DIGINET_MUX_N_SESSION:
		if (k)
			session_notice(k, n->session);
		break;
	}
}

/* ---- the API ---------------------------------------------------------------------------- */

int surface_diginet_open_io(struct surface_diginet **out, const struct diginet_mux_io *io,
			    surface_desk_fn desk, void *ctx)
{
	if (!out)
		return SURFACE_E_ARG;
	*out = NULL;
	if (!io || !io->send || !io->if_mac || !desk)
		return SURFACE_E_ARG;
	struct surface_diginet *d = calloc(1, sizeof *d);
	if (!d)
		return SURFACE_E_NOMEM;
	d->io = *io;
	d->fd = -1;
	d->desk = desk;
	d->ctx = ctx;
	struct diginet_mux_config mc;
	diginet_mux_config_default(&mc);
	if (diginet_mux_init(&d->mux, &mc, &d->io, mux_notice, d) != DIGINET_OK) {
		free(d);
		return SURFACE_E_ARG;
	}
	*out = d;
	return SURFACE_OK;
}

int surface_diginet_open(struct surface_diginet **out, int packet_fd, surface_desk_fn desk, void *ctx)
{
	if (!out)
		return SURFACE_E_ARG;
	*out = NULL;
	if (packet_fd < 0)
		return SURFACE_E_ARG;
	/* the io's descriptor store must outlive it: a placeholder first, then the backend's own */
	struct diginet_fd_io tmp;
	struct diginet_mux_io io;
	diginet_mux_io_packet(&io, &tmp, packet_fd);
	int rc = surface_diginet_open_io(out, &io, desk, ctx);
	if (rc != SURFACE_OK)
		return rc;
	struct surface_diginet *d = *out;
	diginet_mux_io_packet(&d->io, &d->fdio, packet_fd);
	d->mux.io = d->io;
	d->fd = packet_fd;
	return SURFACE_OK;
}

int surface_diginet_attach(struct surface_diginet *d, const uint8_t mac[6], surface_event_fn ev,
			   void *ev_ctx, struct surface **out)
{
	if (out)
		*out = NULL;
	if (!d || !mac || !out)
		return SURFACE_E_ARG;
	struct desk *k = desk_by_mac(d, mac);
	if (!k || k->slot < 0)
		return SURFACE_E_ARG;
	if (k->s) {
		*out = k->s;
		return SURFACE_OK;
	}
	if (k->info.model != DIGINET_DESK_CONTROL24)
		return SURFACE_E_ARG;   /* a ProControl's fader packs are not known yet (Q11) */
	struct surface_config c = { .ops = &OPS, .backend = k, .event = ev, .event_ctx = ev_ctx };
	surface_geometry_preset(SURFACE_MODEL_CONTROL24, &c.geometry);
	int rc = surface_create(&k->s, &c);
	if (rc != SURFACE_OK)
		return rc;
	k->online = 0;
	memset(k->auto_bits, 0, sizeof k->auto_bits);
	k->counter_bits = 0;
	if (!diginet_mux_session(&d->mux, (unsigned)k->slot)) {
		rc = diginet_mux_attach(&d->mux, (unsigned)k->slot, NULL);
		if (rc != DIGINET_OK) {
			surface_destroy(k->s);
			k->s = NULL;
			return rc < 0 ? rc : SURFACE_E_ARG;
		}
	}
	*out = k->s;
	return SURFACE_OK;
}

static void desk_detach(struct surface_diginet *d, struct desk *k, uint64_t now_us)
{
	d->now = now_us;
	if (k->slot >= 0 && diginet_mux_session(&d->mux, (unsigned)k->slot))
		diginet_mux_detach(&d->mux, (unsigned)k->slot, now_us);
	if (k->s)
		surface_destroy(k->s);
	k->s = NULL;
	k->online = 0;
	if (k->slot < 0)
		k->used = 0;
}

void surface_diginet_detach(struct surface_diginet *d, struct surface *s, uint64_t now_us)
{
	struct desk *k = d ? desk_by_surface(d, s) : NULL;
	if (k)
		desk_detach(d, k, now_us);
}

void surface_diginet_close(struct surface_diginet *d, uint64_t now_us)
{
	if (!d)
		return;
	for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS; i++)
		if (d->desks[i].used)
			desk_detach(d, &d->desks[i], now_us);
	free(d);
}

int surface_diginet_fd(const struct surface_diginet *d)
{
	return d ? d->fd : -1;
}

int surface_diginet_rx(struct surface_diginet *d, const uint8_t *frame, size_t len, int ifindex,
		       uint64_t now_us)
{
	if (!d || !frame)
		return SURFACE_E_ARG;
	d->now = now_us;
	return diginet_mux_rx(&d->mux, frame, len, ifindex, now_us);
}

int surface_diginet_service(struct surface_diginet *d, uint64_t now_us)
{
	if (!d)
		return SURFACE_E_ARG;
	d->now = now_us;
	int err = 0;
	if (d->io.recv) {
		uint8_t buf[DIGINET_MAX_FRAME_LEN];
		/* bounded, so a flood on the link cannot hold the caller's loop */
		for (int i = 0; i < 1024; i++) {
			int ifindex = 0;
			int n = d->io.recv(d->io.ctx, buf, sizeof buf, &ifindex, 0);
			if (n < 0 && n != -EAGAIN && n != -EINTR)
				err = n;
			if (n <= 0)
				break;
			diginet_mux_rx(&d->mux, buf, (size_t)n, ifindex, now_us);
		}
	}
	for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS; i++)
		if (d->desks[i].used && d->desks[i].s)
			surface_tick(d->desks[i].s, now_us);
	diginet_mux_tick(&d->mux, now_us);
	return err;
}

uint64_t surface_diginet_deadline(const struct surface_diginet *d, uint64_t now_us)
{
	if (!d)
		return UINT64_MAX;
	uint64_t t = diginet_mux_deadline(&d->mux, now_us);
	for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS; i++)
		if (d->desks[i].used && d->desks[i].s) {
			uint64_t u = surface_deadline(d->desks[i].s, now_us);
			if (u < t)
				t = u;
		}
	return t < now_us ? now_us : t;
}

int surface_diginet_ring_mask(struct surface_diginet *d, struct surface *s, unsigned strip,
			      uint16_t mask)
{
	struct desk *k = d ? desk_by_surface(d, s) : NULL;
	if (!k || !k->p || strip >= k->p->strips)
		return SURFACE_E_ARG;
	return op_ring(k, strip, mask);
}

int surface_diginet_part(struct surface_diginet *d, const char *text)
{
	if (!d || !text)
		return SURFACE_E_ARG;
	int n = 0;
	for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS; i++) {
		struct desk *k = &d->desks[i];
		if (!k->used || !k->p || !desk_online(k))
			continue;
		for (unsigned s = 0; s < k->p->strips; s++)
			op_scribble(k, s, 0, text, strlen(text));
		n++;
	}
	return n;
}

int surface_diginet_idle(const struct surface_diginet *d)
{
	if (!d)
		return 1;
	for (unsigned i = 0; i < DIGINET_MUX_MAX_DESKS; i++) {
		struct diginet_session *ss = (struct diginet_session *)
			(d->desks[i].used && d->desks[i].slot >= 0
			 ? diginet_mux_session((struct diginet_mux *)&d->mux, (unsigned)d->desks[i].slot) : NULL);
		if (ss && ss->state == DIGINET_S_ONLINE && (ss->queued || ss->inflight))
			return 0;
	}
	return 1;
}
