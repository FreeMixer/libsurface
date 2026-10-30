/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * libsurface-mackie: a Mackie Control (MCU) surface such as the Behringer X-Touch, driven through
 * libsurface's core with libmackie as its codec.
 *
 * The MIDI transport is the host's, as libmackie's is: every message this module writes goes to
 * the host's write callback as one complete MIDI message, and the host hands every message it
 * receives to surface_mackie_input(), one at a time. The module owns no port, thread or clock.
 * The control map is the host's struct mackie_map (its surface profile), checked whole at open.
 *
 * What it adds to the core: the MCU encodings of every element, the decoding of faders, touches,
 * keys, V-Pots and the jog into libsurface's inputs, the host-connection handshake, and the
 * two-character assignment display, which the core has no element for (kept and replayed here).
 */
#ifndef SURFACE_MACKIE_H
#define SURFACE_MACKIE_H

#include <stddef.h>
#include <stdint.h>

#include <surface/surface.h>
#include <mackie/mackie.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SURFACE_MACKIE_ABI 0

/* One complete MIDI message to the device. Returns >= 0 on success, negative on failure. */
typedef int (*surface_mackie_write_fn)(void *ctx, const uint8_t *msg, size_t len);

struct surface_mackie_config {
	const struct mackie_map *map;   /* the host's; must outlive the module */
	uint8_t model;                  /* SysEx model id: MACKIE_MODEL_MCU (0x14), or 0x15 for an extender */
	enum surface_model geometry;    /* SURFACE_MODEL_XTOUCH */
	uint8_t strips;                 /* 0: the geometry's (8); fewer for a device with fewer strips */
	surface_mackie_write_fn write;
	void *write_ctx;
	surface_event_fn event;         /* the core's events, as surface_config's */
	void *event_ctx;
	uint16_t max_targets, max_leds; /* as surface_config's; 0 = the core's default */
};

struct surface_mackie;   /* opaque */

/* Opens the module and its surface (*s, owned by the module). The surface is offline until the
 * device answers the handshake (surface_mackie_input of its host-connection query) or the host
 * calls surface_mackie_online() for a device that never handshakes. */
int  surface_mackie_open(struct surface_mackie **out, const struct surface_mackie_config *c,
			 struct surface **s);
void surface_mackie_close(struct surface_mackie *m);

/* Asks the device who it is (the device query): its answer starts the handshake. */
int  surface_mackie_query(struct surface_mackie *m);

/* The device is ready without a handshake: the board replays (surface_in_online). */
void surface_mackie_online(struct surface_mackie *m, uint64_t now_us);

/* One received MIDI message. Faders, touches, keys, V-Pots and the jog become the core's inputs;
 * the handshake is answered. Returns 1 handled, 0 not this module's, negative on a bad message. */
int  surface_mackie_input(struct surface_mackie *m, const uint8_t *msg, size_t len, uint64_t now_us);

/* The two-character assignment display; kept and replayed on every online. */
int  surface_mackie_assignment(struct surface_mackie *m, const char *text);

/* The strip's overload LED, lit or cleared: the meter's channel-pressure message with its own
 * nibble, so it never races a level. */
int  surface_mackie_clip(struct surface_mackie *m, unsigned strip, int clipped);

/* The device reset (clears the LCD and every LED). The shadow stays: online replays it. */
int  surface_mackie_reset(struct surface_mackie *m);

/* An MCU ring's code for surface_ring_code(): mode, position 0..11 (0 dark), centre LED. */
uint32_t surface_mackie_ring_code(enum mackie_ring_mode mode, unsigned position, int centre);

/* The firmware version the device reported (empty before its reply). */
const char *surface_mackie_version(const struct surface_mackie *m);

#ifdef __cplusplus
}
#endif

#endif
