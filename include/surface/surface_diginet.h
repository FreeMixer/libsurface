/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * libsurface-diginet: libsurface's DigiNet module, a Control|24 under libsurface. One listener
 * for every desk over one packet socket (libdiginet's desk mux, diginet_mux.h); one struct surface
 * per desk the host attaches. The design is FreeMixer/libdiginet#29, sections 4.2, 5.3 and 6.
 *
 *   - A desk that announces is reported to the host (the desk callback, present = 1) with its
 *     MAC, interface, name and version. The host attaches it: a surface with the Control|24
 *     geometry and a session of its own, which wakes the desk.
 *   - On every ONLINE (the a0 that ACKs our e2) the surface replays its whole board, faders
 *     included: a new session blanks the desk (docs/protocol.md, R15). Leaving ONLINE makes the
 *     surface offline; the session wakes the desk again on its next announce.
 *   - Writes go down through the libdiginet encoders, one command per element, into the session
 *     queue, where a newer write to the same address replaces a queued one.
 *   - Inputs come up decoded: fader, touch, strip and section keys, V-Pot, jog.
 *   - A desk that answers no wake while it announces (DIGINET_N_DESK_DEAF) and a desk in session
 *     with another host (DIGINET_N_FOREIGN) become SURFACE_EV_WARNING on its surface.
 *   - A desk silent for the mux's gone_ms is reported gone (present = 0) and its surface goes
 *     offline; the surface stays the host's until it detaches it. When the same desk announces
 *     again its surface gets a session again by itself.
 *
 * Every fader is treated alike.
 *
 * Like libsurface and libdiginet it runs on caller time with no thread: surface_diginet_service()
 * takes what the socket holds and runs every timer, surface_diginet_deadline() says when it must
 * run again, surface_diginet_fd() is the descriptor to poll. Not from inside a callback of this
 * library: surface_diginet_detach() and surface_diginet_close().
 */
#ifndef SURFACE_DIGINET_H
#define SURFACE_DIGINET_H

#include <stddef.h>
#include <stdint.h>

#include <surface/surface.h>
#include <diginet/transport/diginet_mux.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The module is part of libsurface and carries its version (SURFACE_VERSION_STRING). Its
 * soname's major: libsurface-diginet.so.SURFACE_DIGINET_ABI. packaging/ and debian/ carry copies;
 * tests/conformance-packaging.sh holds them to this. */
#define SURFACE_DIGINET_ABI 0

/* ---- Key ids ----------------------------------------------------------------------------
 * A key is button << 8 | strip-or-zone, as the desk sends it (90 bb (40|id)): strip buttons on
 * strip 0..23, section buttons on zones 0x18..0x1D. surface_led() with such a key lights it.
 * The strip buttons measured on the desk (R10): */
#define SURFACE_DIGINET_KEY(button, id) ((uint16_t)(((button) & 0x7f) << 8 | ((id) & 0xff)))
#define SURFACE_DIGINET_BTN_PAN_SEND 0x01
#define SURFACE_DIGINET_BTN_EQ       0x02
#define SURFACE_DIGINET_BTN_DYNAMICS 0x03
#define SURFACE_DIGINET_BTN_ACTIVE   0x04   /* lights SWITCH ACTIVE */
#define SURFACE_DIGINET_BTN_AUTO     0x05   /* its mode shows on the automation LEDs */
#define SURFACE_DIGINET_BTN_SELECT   0x06
#define SURFACE_DIGINET_BTN_SOLO     0x07
#define SURFACE_DIGINET_BTN_MUTE     0x08
#define SURFACE_DIGINET_BTN_INSERTS  0x0a

/* The automation LEDs of a strip (F0 13 01 20 tt bits F7) as LED keys of their own: bit 15 set,
 * the LED's bit (DIGINET_AUTO_WRITE ... DIGINET_AUTO_READ, one bit) above the strip. The
 * backend keeps the strip's byte and sends all of it on every change. id
 * SURFACE_DIGINET_COUNTER_LEDS addresses the counter-mode LEDs the same way (bits 0x01..0x40). */
#define SURFACE_DIGINET_AUTO_KEY(id, bit) ((uint16_t)(0x8000u | (((bit) & 0x7f) << 8) | ((id) & 0xff)))
#define SURFACE_DIGINET_COUNTER_LEDS 0xff
#define SURFACE_DIGINET_IS_AUTO_KEY(key) (((key) & 0x8000u) != 0)

/* SURFACE_LED_FLASH lights the LED: the desk has no blink of its own that is measured. */

/* ---- The desk table -------------------------------------------------------------------- */

struct surface_desk_info {
	uint8_t mac[6];
	int ifindex;            /* the interface it is heard on */
	char ifname[16];        /* its name, "" when the index names no interface here */
	char name[10];          /* the announce name: "CNTRL|24" */
	char version[10];       /* the announce version: "1.08" */
	int model;              /* enum diginet_desk; DIGINET_DESK_UNKNOWN for a name no source has */
};

/* A desk appeared (present = 1) or is gone (present = 0). The host may attach it from here. */
typedef void (*surface_desk_fn)(void *ctx, const struct surface_desk_info *d, int present);

struct surface_diginet;   /* opaque */

/* Over an any-interface packet socket (diginet_packet_open_any(), or the descriptor
 * diginet_packet_open_by_helper() hands over). The descriptor stays the caller's. desk may not be
 * NULL. The one allocation of the backend; *out is NULL on failure. */
int surface_diginet_open(struct surface_diginet **out, int packet_fd, surface_desk_fn desk, void *ctx);

/* Over any wire (a test's desk emulator, a host that carries frames itself). io->send and
 * io->if_mac may not be NULL; with io->recv NULL frames come only through surface_diginet_rx(). */
int surface_diginet_open_io(struct surface_diginet **out, const struct diginet_mux_io *io,
			    surface_desk_fn desk, void *ctx);

/* Send e3 to every attached desk, destroy every surface, free the backend. */
void surface_diginet_close(struct surface_diginet *d, uint64_t now_us);

/* Attach the present desk with this MAC: a surface with the Control|24 geometry, its events to
 * ev (may be NULL), and a session that wakes the desk. *out is the desk's surface (the same one
 * when it is attached already). SURFACE_E_ARG for a MAC no present desk has, or a desk that is
 * not a Control|24; SURFACE_E_NOMEM; a negative errno from the mux. */
int surface_diginet_attach(struct surface_diginet *d, const uint8_t mac[6], surface_event_fn ev,
			   void *ev_ctx, struct surface **out);

/* Close the desk's session (e3) and destroy its surface. */
void surface_diginet_detach(struct surface_diginet *d, struct surface *s, uint64_t now_us);

/* The descriptor to poll for input, or -1 over an io of its own. */
int surface_diginet_fd(const struct surface_diginet *d);

/* One frame from interface ifindex. */
int surface_diginet_rx(struct surface_diginet *d, const uint8_t *frame, size_t len, int ifindex,
		       uint64_t now_us);

/* Take every frame waiting on the wire (without waiting), then run the timers of every surface
 * and every session. 0, or a negative errno from the wire. */
int surface_diginet_service(struct surface_diginet *d, uint64_t now_us);

/* The latest time at which service must run again (now_us when it has work now). */
uint64_t surface_diginet_deadline(const struct surface_diginet *d, uint64_t now_us);

/* ---- Below the shadow ------------------------------------------------------------------- */

/* A ring written as a raw 15-LED mask, bit 0 the leftmost, outside the board shadow (a desk
 * walk, a test): a replay does not repeat it. 1 written, 0 offline, or SURFACE_E_ARG. */
int surface_diginet_ring_mask(struct surface_diginet *d, struct surface *s, unsigned strip,
			      uint16_t mask);

/* Write text on every scribble of every online desk, outside the shadow: the parting message a
 * host leaves before surface_diginet_close(), which the desk keeps under "Offline" (R15). The
 * number of desks it was queued for. */
int surface_diginet_part(struct surface_diginet *d, const char *text);

/* 1 when no session has a command queued or a frame waiting for its ACK. */
int surface_diginet_idle(const struct surface_diginet *d);

#ifdef __cplusplus
}
#endif

#endif
