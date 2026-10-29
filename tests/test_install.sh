#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# `make install` into a scratch root, then build and run a consumer against that root
# through pkg-config alone: every installed header, archive and .pc file has to be there.
set -u
command -v pkg-config >/dev/null 2>&1 || { echo "no pkg-config: nothing tested"; exit 77; }
top=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
make -s -C "$top" install PREFIX=/usr DESTDIR="$tmp/root" >/dev/null || { echo "FAIL: make install"; exit 1; }
cat > "$tmp/consumer.c" <<'C'
#include <surface/surface.h>
int main(void)
{
	struct surface_geometry g;
	if (surface_geometry_preset(SURFACE_MODEL_CONTROL24, &g) || g.strips != 24)
		return 1;
	return surface_meter_step(-1, 0.5, 14, SURFACE_METER_STEP_HYSTERESIS) == 7 ? 0 : 1;
}
C
PKG_CONFIG_SYSROOT_DIR="$tmp/root" PKG_CONFIG_LIBDIR="$tmp/root/usr/lib/pkgconfig" \
	sh -c "${CC:-cc} '$tmp/consumer.c' \$(pkg-config --static --cflags --libs libsurface) -o '$tmp/consumer'" ||
	{ echo "FAIL: a consumer does not build against the installed tree"; exit 1; }
"$tmp/consumer" || { echo "FAIL: the consumer gave the wrong answer"; exit 1; }

# A second install to another prefix names that prefix: nothing from the first one is reused.
make -s -C "$top" install PREFIX=/opt/elsewhere DESTDIR="$tmp/other" >/dev/null || { echo "FAIL: second make install"; exit 1; }
grep -qx 'includedir=/opt/elsewhere/include' "$tmp/other/opt/elsewhere/lib/pkgconfig/libsurface.pc" ||
	{ echo "FAIL: libsurface.pc of the second install names another prefix"; exit 1; }
echo "test_install: ok"
