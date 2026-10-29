#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The packages, installed in a clean Fedora or Debian container that holds nothing else:
#
#   tests/package-check.sh <directory holding the .rpm or .deb files>
#
# installs libsurface and its development package with what they pull in, then builds a consumer
# outside the tree against the development package through pkg-config alone, and runs it against
# the installed shared library.
set -eu
dir=$(cd "$1" && pwd)
if command -v dnf >/dev/null 2>&1; then
	dnf install -y -q --setopt=install_weak_deps=False gcc pkgconf-pkg-config \
		$(ls "$dir"/libsurface-[0-9]*.rpm "$dir"/libsurface-devel-[0-9]*.rpm | grep -v -e debuginfo -e debugsource -e '\.src\.rpm')
	rpm -q libsurface libsurface-devel
else
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq
	apt-get install -y -qq --no-install-recommends gcc libc6-dev pkgconf \
		"$dir"/libsurface0_*.deb "$dir"/libsurface-dev_*.deb
	dpkg -l libsurface0 libsurface-dev | tail -n 2
fi

tmp=$(mktemp -d)
cat > "$tmp/consumer.c" <<'C'
#include <stdio.h>
#include <surface/surface.h>
int main(void)
{
	struct surface_geometry g;
	if (surface_geometry_preset(SURFACE_MODEL_XTOUCH, &g))
		return 1;
	unsigned step = surface_meter_step(-1, 0.5, g.meter_segments, SURFACE_METER_STEP_HYSTERESIS);
	printf("consumer: libsurface %s, X-Touch %u strips, half scale = %u of %u LEDs\n",
	       SURFACE_VERSION_STRING, g.strips, step, g.meter_segments);
	return g.strips == 8 && step == 6 ? 0 : 1;
}
C
cc "$tmp/consumer.c" $(pkg-config --cflags --libs libsurface) -o "$tmp/consumer"
"$tmp/consumer"
# linked against the shared object, not a copy
ldd "$tmp/consumer" | grep -q 'libsurface\.so\.0 => /' || { echo "FAIL: not linked to libsurface.so.0"; exit 1; }
pkg-config --modversion libsurface
echo "package-check: ok"
