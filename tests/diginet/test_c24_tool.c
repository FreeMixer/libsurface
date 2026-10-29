// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* tools/surface-c24 end to end, in a user and network namespace of the test's own (libdiginet's
 * netns.h, copied beside this file): a veth pair hA <-> dA, libdiginet's desk emulator on dA,
 * the tool on the real clock with its socket from diginet-packet-helper and its writes from a
 * FIFO. Nothing here touches the machine's own interfaces.
 *
 *   the desk appears and is attached; online, its strips are labelled F1..F24;
 *   a ride on the desk is printed (touch, move, release) and served back;
 *   the FIFO writes a scribble, an LED and a fader target, and refuses nonsense;
 *   quit leaves "OFF " on the scribbles, sends e3 and exits 0.
 *
 *   test_c24_tool <diginet-packet-helper> <surface-c24>
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include "netns.h"
#include "emu_pump.h"

#include <diginet/diginet_codec.h>

static int fails;
#define CHK(cond) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static const uint8_t DESK[6] = { 0x00, 0xa0, 0x7e, 0x24, 0x00, 0x0a };

static struct desk_emu E;
static int desk_fd, out_fd;
static uint64_t next_ann;
static char out[1 << 18];
static size_t outlen;

static void step(void)
{
	ssize_t k;
	while (outlen < sizeof out - 1 && (k = read(out_fd, out + outlen, sizeof out - 1 - outlen)) > 0)
		outlen += (size_t)k;
	out[outlen] = '\0';
	emu_pump_fd(&E, desk_fd, &next_ann, 2000000);
	struct timespec ts = { 0, 1000000 };
	nanosleep(&ts, NULL);
}

/* Pump until the tool has said `text`, at most ms. */
static int wait_for(const char *text, int ms)
{
	uint64_t end = diginet_clock_us() + (uint64_t)ms * 1000;
	while (diginet_clock_us() < end) {
		step();
		if (strstr(out, text))
			return 1;
	}
	return 0;
}

static void pump_ms(int ms)
{
	uint64_t end = diginet_clock_us() + (uint64_t)ms * 1000;
	while (diginet_clock_us() < end)
		step();
}

/* What the desk got: decoded commands of every first send, with a predicate. */
static int desk_got(int (*match)(const struct diginet_event *ev, const void *arg), const void *arg)
{
	const struct diginet_personality *p = diginet_personality_of(DIGINET_DESK_CONTROL24);
	int n = 0;
	for (unsigned i = 0; i < E.nlog; i++) {
		const struct emu_rx *r = &E.log[i];
		if (r->cmd != DIGINET_CMD_DATA || r->retry)
			continue;
		struct diginet_decoder dec;
		struct diginet_event ev;
		diginet_decoder_init(&dec, p, r->body, r->body_len);
		for (int rc; (rc = diginet_decoder_next(&dec, &ev)) != 0; )
			if (rc > 0 && match(&ev, arg))
				n++;
	}
	return n;
}

static int is_label(const struct diginet_event *ev, const void *arg)
{
	(void)arg;
	char want[8];
	snprintf(want, sizeof want, "F%-3u", ev->addr + 1u);
	return ev->kind == DIGINET_EV_SCRIBBLE && ev->addr < 24 && !strcmp(ev->text, want);
}

static int is_text(const struct diginet_event *ev, const void *arg)
{
	return ev->kind == DIGINET_EV_SCRIBBLE && !strcmp(ev->text, arg);
}

static int is_fader(const struct diginet_event *ev, const void *arg)
{
	const unsigned *sv = arg;
	return ev->kind == DIGINET_EV_FADER && ev->strip == sv[0] && ev->value == sv[1];
}

static int is_mute1(const struct diginet_event *ev, const void *arg)
{
	(void)arg;
	return ev->kind == DIGINET_EV_STRIP_BUTTON && ev->button == 0x08 && ev->strip == 0 && ev->on;
}

static int is_meter(const struct diginet_event *ev, const void *arg)
{
	const unsigned *sv = arg;
	return ev->kind == DIGINET_EV_METER && !ev->right && ev->addr == sv[0] && ev->segments == (int)sv[1];
}

static void desk_says(const struct diginet_cmd *c)
{
	emu_send_data(&E, c->bytes, c->len, 1, 0, 0);
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: test_c24_tool <diginet-packet-helper> <surface-c24>\n");
		return 2;
	}
	if (access(argv[1], X_OK) != 0) {
		printf("test_c24_tool: no diginet-packet-helper at \"%s\" (DIGINET_SRC, or one in PATH): "
		       "nothing tested\n", argv[1]);
		return 77;
	}
	if (ns_enter() < 0) {
		printf("test_c24_tool: no unprivileged user+net namespace (%s): nothing tested\n", strerror(errno));
		return 77;
	}
	if (ns_veth("hA", "dA") < 0) {
		printf("test_c24_tool: cannot make a veth pair in the namespace (%s): nothing tested\n", strerror(errno));
		return 77;
	}
	uint8_t mac[6];
	desk_fd = diginet_packet_open("dA", mac);
	if (desk_fd == -EPERM) {
		printf("test_c24_tool: no CAP_NET_RAW in the namespace: nothing tested\n");
		return 77;
	}
	CHK(desk_fd >= 0);
	if (desk_fd < 0)
		return 1;
	emu_init(&E, DESK, "CNTRL|24", "1.08");
	/* the tool takes its socket from the helper: it gets no capability of ours */
	CHK(ns_drop_caps() == 0);

	char dir[] = "/tmp/test_c24_tool.XXXXXX", fifo[64];
	CHK(mkdtemp(dir) != NULL);
	snprintf(fifo, sizeof fifo, "%s/fifo", dir);

	int po[2];
	CHK(pipe(po) == 0);
	pid_t pid = fork();
	if (pid == 0) {
		int nul = open("/dev/null", O_RDONLY);
		dup2(nul, 0);
		dup2(po[1], 1);
		dup2(po[1], 2);
		for (int fd = 3; fd < 64; fd++)
			close(fd);
		execl(argv[2], argv[2], "--helper", argv[1], "--fifo", fifo, (char *)NULL);
		_exit(127);
	}
	close(po[1]);
	out_fd = po[0];
	fcntl(out_fd, F_SETFL, O_NONBLOCK);
	next_ann = diginet_clock_us() + 300000;

	CHK(wait_for("listening on every interface", 5000));
	CHK(wait_for("desk 00:a0:7e:24:00:0a appeared on hA", 5000));
	CHK(wait_for("online: board replayed", 5000));
	pump_ms(300);
	CHK(desk_got(is_label, NULL) == 24);

	/* a ride on strip 3: touch, two positions, release */
	const struct diginet_personality *p = diginet_personality_of(DIGINET_DESK_CONTROL24);
	struct diginet_cmd c;
	CHK(diginet_touch_encode(p, 2, 1, &c) == 0);
	desk_says(&c);
	pump_ms(20);
	CHK(diginet_fader_encode(p, 2, 300, &c) == 0);
	desk_says(&c);
	pump_ms(20);
	CHK(diginet_fader_encode(p, 2, 700, &c) == 0);
	desk_says(&c);
	pump_ms(20);
	CHK(diginet_touch_encode(p, 2, 0, &c) == 0);
	desk_says(&c);
	CHK(wait_for("F3 released at 0.6843", 2000));
	pump_ms(300);   /* the release echo is queued when the tool prints the release: let it land */
	CHK(strstr(out, "F3 touched") && strstr(out, "F3 moved 0.2933 (300)") && strstr(out, "F3 moved 0.6843 (700)"));
	unsigned echo[2] = { 2, 700 };
	CHK(desk_got(is_fader, echo) >= 2);         /* the set-point while touched, and at release */
	unsigned meter[2] = { 2, 10 };               /* 0.684 of 14 segments, on its own meter */
	CHK(desk_got(is_meter, meter) >= 1);

	/* a key and a jog */
	CHK(diginet_button_encode(0x07, 4, 1, &c) == 0);
	desk_says(&c);
	CHK(wait_for("key 07/04 pressed", 2000));
	CHK(diginet_jog_encode(-2, &c) == 0);
	desk_says(&c);
	CHK(wait_for("jog -2", 2000));

	/* writes from the FIFO */
	int wf = open(fifo, O_WRONLY | O_CLOEXEC);
	CHK(wf >= 0);
	static const char cmds[] = "scrib 1 Hey!\nled mute:1 on\nfader 5 0.5\nring 6 0x7\nbogus 1\nfader 99 2\n";
	CHK(write(wf, cmds, sizeof cmds - 1) == (ssize_t)(sizeof cmds - 1));
	CHK(wait_for("refused: fader 99 2", 2000));
	pump_ms(300);
	CHK(strstr(out, "refused: bogus") != NULL);
	CHK(desk_got(is_text, "Hey!") == 1);
	CHK(desk_got(is_mute1, NULL) == 1);
	unsigned mid[2] = { 4, 512 };
	CHK(desk_got(is_fader, mid) == 1);

	/* quit: the parting word, e3, exit 0 */
	CHK(write(wf, "quit\n", 5) == 5);
	close(wf);
	int st = -1;
	uint64_t end = diginet_clock_us() + 5000000;
	while (diginet_clock_us() < end) {
		step();
		if (waitpid(pid, &st, WNOHANG) == pid)
			break;
	}
	if (diginet_clock_us() >= end) {
		kill(pid, SIGKILL);
		waitpid(pid, &st, 0);
	}
	step();
	CHK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	CHK(desk_got(is_text, "OFF ") == 24);
	CHK(E.disconnects == 1);
	close(out_fd);
	unlink(fifo);
	rmdir(dir);

	if (fails) {
		fprintf(stderr, "--- surface-c24 said:\n%s---\ntest_c24_tool: %d failure(s)\n", out, fails);
		return 1;
	}
	printf("test_c24_tool: ok (surface-c24 unprivileged, its socket from the helper, on a veth pair)\n");
	return 0;
}
