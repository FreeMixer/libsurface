#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The DigiNet module installed: `make install` into a scratch prefix, then build and run a
# consumer of libsurface-diginet against it through pkg-config alone: the header, the archives,
# the .pc file and its Requires (libsurface, libdiginet-transport) all have to resolve.
#
#   tests/diginet/test_install.sh [libdiginet tree]
#
# With the libdiginet tree the build used, it is installed into the same prefix first; without
# it, libdiginet is the one pkg-config already finds.
set -u
command -v pkg-config >/dev/null 2>&1 || { echo "no pkg-config: nothing tested"; exit 77; }
top=$(cd "$(dirname "$0")/../.." && pwd)
dsrc=${1:-}
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
p=$tmp/prefix
if [ -n "$dsrc" ]; then
	make -s -C "$dsrc" install PREFIX="$p" >/dev/null || { echo "FAIL: make install in $dsrc"; exit 1; }
fi
make -s -C "$top" install PREFIX="$p" DIGINET_SRC="$dsrc" >/dev/null ||
	{ echo "FAIL: make install"; exit 1; }
[ -x "$p/bin/surface-c24" ] || { echo "FAIL: surface-c24 not installed"; exit 1; }
cat > "$tmp/consumer.c" <<'C'
#include <errno.h>
#include <string.h>
#include <surface/surface_diginet.h>
static int tx(void *c, int i, const uint8_t *f, size_t n) { (void)c; (void)i; (void)f; (void)n; return 0; }
static int mac(void *c, int i, uint8_t m[6]) { (void)c; (void)i; memset(m, 2, 6); return 0; }
static void desk(void *c, const struct surface_desk_info *d, int present) { (void)c; (void)d; (void)present; }
int main(void)
{
	struct surface_diginet *d;
	struct diginet_mux_io io = { .send = tx, .if_mac = mac };
	if (surface_diginet_open(&d, -1, desk, NULL) != SURFACE_E_ARG || d)
		return 1;
	if (surface_diginet_open_io(&d, &io, desk, NULL) != SURFACE_OK)
		return 1;
	int idle = surface_diginet_idle(d);
	surface_diginet_close(d, 0);
	return idle == 1 && !strcmp(SURFACE_VERSION_STRING, "@VERSION@") ? 0 : 1;
}
C
sed -i "s/@VERSION@/$(cat "$top/VERSION")/" "$tmp/consumer.c"
PKG_CONFIG_PATH="$p/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
	sh -c "${CC:-cc} '$tmp/consumer.c' \$(pkg-config --static --cflags --libs libsurface-diginet) -o '$tmp/consumer'" ||
	{ echo "FAIL: a consumer does not build against the installed tree"; exit 1; }
LD_LIBRARY_PATH="$p/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$tmp/consumer" ||
	{ echo "FAIL: the consumer gave the wrong answer"; exit 1; }
grep -qx "includedir=$p/include" "$p/lib/pkgconfig/libsurface-diginet.pc" ||
	{ echo "FAIL: libsurface-diginet.pc names another prefix"; exit 1; }
echo "test_install: ok"
