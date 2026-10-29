// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* surface-c24: a Control|24 driven through libsurface, to try the stack on the real desk.
 *
 * It holds no privilege of its own: the packet socket comes from diginet-packet-helper (which
 * holds cap_net_raw) over a Unix socket. It hears every interface, attaches every Control|24 that
 * announces, labels its strips F1..F24, mirrors each fader's position onto the left meter of its
 * own strip, and prints every event. It plays the console: each move the desk reports is served
 * straight back as the strip's value, as a mixer echoes a fader.
 *
 * Writes come from a FIFO (--fifo PATH), one command per line, strips counted from 1:
 *   scrib S TEXT     the scribble of strip S (4 characters show)
 *   led KEY on|off   KEY is NAME:S (pan, eq, dyn, active, auto, select, solo, mute, inserts) or a
 *                    number, button << 8 | strip-or-zone (0x0800 is MUTE on strip 1)
 *   fader S POS      serve POS (0..1) to strip S: an untouched cap moves there
 *   ring S MASK      the 15 ring LEDs of strip S, bit 0 the leftmost (kept and repainted)
 *   quit
 *
 * One thread: poll() on the socket and the FIFO, with libsurface's deadline as the timeout.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <diginet/transport/diginet_io.h>
#include <surface/surface_diginet.h>

#ifndef SURFACE_C24_HELPER
#define SURFACE_C24_HELPER NULL   /* diginet-packet-helper, looked up in PATH */
#endif

#define STRIPS 24

static struct surface_diginet *D;
static struct surface *SF;
static volatile sig_atomic_t stop;
static int ring_set[STRIPS];
static uint16_t ring_mask[STRIPS];
static int pending[STRIPS];       /* a move to serve back once the call that reported it ends */
static double pending_pos[STRIPS];

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static double secs(void)
{
	static uint64_t t0;
	uint64_t t = diginet_clock_us();
	if (!t0)
		t0 = t;
	return (double)(t - t0) / 1e6;
}

static const char *warning_name(int code)
{
	switch (code) {
	case SURFACE_WARN_DESK_DEAF: return "DESK_DEAF";
	case SURFACE_WARN_FOREIGN:   return "FOREIGN";
	}
	return "WARNING";
}

static void paint(void)
{
	char name[8];
	for (unsigned k = 0; k < STRIPS; k++) {
		snprintf(name, sizeof name, "F%u", k + 1);
		surface_scribble(SF, k, 0, name);
	}
}

static void on_event(void *ctx, const struct surface_event *ev)
{
	(void)ctx;
	double t = secs();
	switch (ev->kind) {
	case SURFACE_EV_ONLINE:
		printf("%9.3f online: board replayed\n", t);
		for (unsigned k = 0; k < STRIPS; k++)   /* raw masks live outside the shadow */
			if (ring_set[k])
				surface_diginet_ring_mask(D, SF, k, ring_mask[k]);
		break;
	case SURFACE_EV_OFFLINE:
		printf("%9.3f offline\n", t);
		break;
	case SURFACE_EV_FADER_MOVED:
		printf("%9.3f F%u moved %.4f (%u)\n", t, ev->strip + 1u, ev->position,
		       (unsigned)(ev->position * 1023 + 0.5));
		if (ev->strip < STRIPS) {
			pending[ev->strip] = 1;
			pending_pos[ev->strip] = ev->position;
		}
		break;
	case SURFACE_EV_FADER_TOUCHED:
		printf("%9.3f F%u touched\n", t, ev->strip + 1u);
		break;
	case SURFACE_EV_FADER_RELEASED:
		printf("%9.3f F%u released at %.4f\n", t, ev->strip + 1u, ev->position);
		break;
	case SURFACE_EV_KEY:
		printf("%9.3f key %02x/%02x %s\n", t, ev->key >> 8, ev->key & 0xff,
		       ev->on ? "pressed" : "released");
		break;
	case SURFACE_EV_VPOT:
		printf("%9.3f F%u vpot %+d\n", t, ev->strip + 1u, ev->delta);
		break;
	case SURFACE_EV_JOG:
		printf("%9.3f jog %+d\n", t, ev->delta);
		break;
	case SURFACE_EV_WARNING:
		printf("%9.3f warning %s: %s\n", t, warning_name(ev->code), ev->text ? ev->text : "");
		break;
	}
	fflush(stdout);
}

/* The console's echo, and the fader mirrored on its meter. */
static void host_answer(uint64_t now)
{
	for (unsigned k = 0; k < STRIPS && SF; k++) {
		if (!pending[k])
			continue;
		pending[k] = 0;
		surface_fader_served(SF, k + 1, pending_pos[k], now);
		surface_meter(SF, k, 0, pending_pos[k], now);
	}
}

static void on_desk(void *ctx, const struct surface_desk_info *d, int present)
{
	(void)ctx;
	printf("%9.3f desk %02x:%02x:%02x:%02x:%02x:%02x %s on %s (ifindex %d), \"%s\" %s\n", secs(),
	       d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5],
	       present ? "appeared" : "gone", d->ifname[0] ? d->ifname : "?", d->ifindex, d->name,
	       d->version);
	fflush(stdout);
	if (!present || SF)
		return;   /* one desk: the first Control|24 that announces */
	struct surface *s = NULL;
	int rc = surface_diginet_attach(D, d->mac, on_event, NULL, &s);
	if (rc != SURFACE_OK) {
		printf("%9.3f not attached: %s\n", secs(), rc < 0 && rc > -200 ? strerror(-rc) : "not a Control|24");
		return;
	}
	SF = s;
	surface_target list[STRIPS];
	for (unsigned k = 0; k < STRIPS; k++)
		list[k] = k + 1;
	surface_bank_set(SF, list, STRIPS);
	paint();
}

/* ---- the FIFO ---------------------------------------------------------------------------- */

static const struct { const char *name; unsigned button; } BUTTONS[] = {
	{ "pan", SURFACE_DIGINET_BTN_PAN_SEND }, { "eq", SURFACE_DIGINET_BTN_EQ },
	{ "dyn", SURFACE_DIGINET_BTN_DYNAMICS }, { "active", SURFACE_DIGINET_BTN_ACTIVE },
	{ "auto", SURFACE_DIGINET_BTN_AUTO }, { "select", SURFACE_DIGINET_BTN_SELECT },
	{ "solo", SURFACE_DIGINET_BTN_SOLO }, { "mute", SURFACE_DIGINET_BTN_MUTE },
	{ "inserts", SURFACE_DIGINET_BTN_INSERTS },
};

static int parse_strip(const char *s, unsigned *k)
{
	char *end;
	long v = s ? strtol(s, &end, 10) : 0;
	if (!s || *end || v < 1 || v > STRIPS)
		return -1;
	*k = (unsigned)v - 1;
	return 0;
}

static int parse_key(const char *s, uint16_t *key)
{
	const char *colon = strchr(s, ':');
	if (colon) {
		unsigned k;
		if (parse_strip(colon + 1, &k))
			return -1;
		for (size_t i = 0; i < sizeof BUTTONS / sizeof BUTTONS[0]; i++)
			if (strlen(BUTTONS[i].name) == (size_t)(colon - s) &&
			    !strncmp(BUTTONS[i].name, s, (size_t)(colon - s))) {
				*key = SURFACE_DIGINET_KEY(BUTTONS[i].button, k);
				return 0;
			}
		return -1;
	}
	char *end;
	unsigned long v = strtoul(s, &end, 0);
	if (*end || v > 0xffff)
		return -1;
	*key = (uint16_t)v;
	return 0;
}

static void command(char *line, uint64_t now)
{
	while (isspace((unsigned char)*line))
		line++;
	char *cmd = strtok(line, " \t");
	if (!cmd)
		return;
	if (!strcmp(cmd, "quit")) {
		stop = 1;
		return;
	}
	if (!SF) {
		printf("%9.3f fifo: no desk yet: %s\n", secs(), cmd);
		return;
	}
	char *a = strtok(NULL, " \t");
	char *b = strtok(NULL, "");
	unsigned k;
	int rc = -1;
	if (!strcmp(cmd, "scrib") && a && !parse_strip(a, &k)) {
		rc = surface_scribble(SF, k, 0, b ? b : "");
	} else if (!strcmp(cmd, "led") && a && b) {
		uint16_t key;
		while (isspace((unsigned char)*b))
			b++;
		if (!parse_key(a, &key) && (!strcmp(b, "on") || !strcmp(b, "off")))
			rc = surface_led(SF, key, !strcmp(b, "on") ? SURFACE_LED_ON : SURFACE_LED_OFF);
	} else if (!strcmp(cmd, "fader") && a && b && !parse_strip(a, &k)) {
		char *end;
		double pos = strtod(b, &end);
		if (end != b && pos >= 0 && pos <= 1) {
			surface_fader_served(SF, k + 1, pos, now);
			surface_meter(SF, k, 0, pos, now);
			rc = 0;
		}
	} else if (!strcmp(cmd, "ring") && a && b && !parse_strip(a, &k)) {
		char *end;
		unsigned long m = strtoul(b, &end, 0);
		if (end != b && m <= 0x7fff) {
			ring_set[k] = 1;
			ring_mask[k] = (uint16_t)m;
			rc = surface_diginet_ring_mask(D, SF, k, ring_mask[k]);
		}
	}
	if (rc < 0)
		printf("%9.3f fifo: refused: %s %s %s\n", secs(), cmd, a ? a : "", b ? b : "");
	fflush(stdout);
}

static void fifo_read(int fd, uint64_t now)
{
	static char buf[1024];
	static size_t have;
	ssize_t n;
	while ((n = read(fd, buf + have, sizeof buf - 1 - have)) > 0) {
		have += (size_t)n;
		buf[have] = 0;
		char *nl;
		while ((nl = memchr(buf, '\n', have))) {
			*nl = 0;
			command(buf, now);
			size_t used = (size_t)(nl + 1 - buf);
			memmove(buf, nl + 1, have - used);
			have -= used;
		}
		if (have == sizeof buf - 1)
			have = 0;   /* a line longer than any command: dropped */
	}
}

/* ---- main -------------------------------------------------------------------------------- */

static void usage(FILE *f)
{
	fprintf(f,
		"usage: surface-c24 [--helper PATH] [--fifo PATH]\n"
		"  --helper PATH  diginet-packet-helper to take the packet socket from (default: %s)\n"
		"  --fifo PATH    read write commands from this FIFO (created when missing)\n"
		"FIFO commands, strips 1..24: scrib S TEXT | led NAME:S|KEY on|off | fader S 0..1 |\n"
		"                             ring S MASK | quit\n",
		SURFACE_C24_HELPER ? SURFACE_C24_HELPER : "diginet-packet-helper in PATH");
}

int main(int argc, char **argv)
{
	const char *helper = SURFACE_C24_HELPER, *fifo = NULL;
	static const struct option opts[] = {
		{ "helper", required_argument, NULL, 'H' },
		{ "fifo", required_argument, NULL, 'f' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	for (int c; (c = getopt_long(argc, argv, "H:f:h", opts, NULL)) != -1; ) {
		switch (c) {
		case 'H': helper = optarg; break;
		case 'f': fifo = optarg; break;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (optind != argc) {
		usage(stderr);
		return 2;
	}

	int fd = diginet_packet_open_by_helper(helper);
	if (fd < 0) {
		fprintf(stderr, "surface-c24: no packet socket from %s: %s%s\n",
			helper ? helper : "diginet-packet-helper", strerror(-fd),
			fd == -EPERM ? " (the helper needs cap_net_raw: setcap cap_net_raw+ep on it)" : "");
		return 1;
	}
	int ffd = -1;
	if (fifo) {
		if (mkfifo(fifo, 0600) < 0 && errno != EEXIST) {
			fprintf(stderr, "surface-c24: %s: %s\n", fifo, strerror(errno));
			return 1;
		}
		/* read-write, so the FIFO never reads end-of-file between writers */
		ffd = open(fifo, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (ffd < 0) {
			fprintf(stderr, "surface-c24: %s: %s\n", fifo, strerror(errno));
			return 1;
		}
	}
	if (surface_diginet_open(&D, fd, on_desk, NULL) != SURFACE_OK) {
		fprintf(stderr, "surface-c24: the backend did not open\n");
		return 1;
	}
	struct sigaction sa = { .sa_handler = on_signal };
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	printf("%9.3f listening on every interface%s%s\n", secs(), fifo ? ", commands from " : "",
	       fifo ? fifo : "");
	fflush(stdout);

	while (!stop) {
		uint64_t now = diginet_clock_us();
		uint64_t due = surface_diginet_deadline(D, now);
		int timeout = due <= now ? 0 : due - now > 1000000 ? 1000 : (int)((due - now + 999) / 1000);
		struct pollfd p[2] = { { .fd = fd, .events = POLLIN }, { .fd = ffd, .events = POLLIN } };
		if (poll(p, ffd >= 0 ? 2 : 1, timeout) < 0 && errno != EINTR) {
			perror("surface-c24: poll");
			break;
		}
		now = diginet_clock_us();
		if (ffd >= 0 && (p[1].revents & POLLIN))
			fifo_read(ffd, now);
		int rc = surface_diginet_service(D, now);
		if (rc < 0)
			fprintf(stderr, "surface-c24: wire: %s\n", strerror(-rc));
		host_answer(now);
	}

	/* a parting word the desk keeps under "Offline" (R15), then e3 */
	if (surface_diginet_part(D, "OFF ") > 0) {
		uint64_t until = diginet_clock_us() + 500000;
		while (!surface_diginet_idle(D) && diginet_clock_us() < until) {
			struct pollfd p = { .fd = fd, .events = POLLIN };
			poll(&p, 1, 5);
			surface_diginet_service(D, diginet_clock_us());
		}
	}
	surface_diginet_close(D, diginet_clock_us());
	close(fd);
	if (ffd >= 0)
		close(ffd);
	printf("%9.3f closed\n", secs());
	return 0;
}
