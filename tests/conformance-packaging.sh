#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The version and the soname have one definition each, and every copy must agree with it:
#   ARM 1  VERSION is the version: surface.h's SURFACE_VERSION_STRING, the spec's Version and the
#          top entry of debian/changelog say the same;
#   ARM 2  SURFACE_ABI of surface.h is the spec's %global abi and the N of the libsurfaceN
#          package, and debian/libsurface-dev depends on that package by that name;
#   ARM 3  SURFACE_DIGINET_ABI of surface_diginet.h (the DigiNet module) is the spec's %global
#          dabi and the N of the libsurface-diginetN package, and debian/libsurface-diginet-dev
#          depends on that package by that name;
#   ARM 4  the same for SURFACE_MACKIE_ABI of surface_mackie.h (the Mackie module), the spec's
#          %global mabi and the libsurface-mackieN package.
# It reads files, not git, so it also runs from a release tarball. Planted copies, one with the spec at
# another version and one with the module's package at another soname, run first and must be
# refused, so the check cannot pass vacuously.
#
# Exit 0 conforms, 1 an arm found an offence, 2 the planted copy was not refused (NOT A RESULT).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)

check() {
	t=$1 bad=0
	ver=$(cat "$t/VERSION" 2>/dev/null)
	hdr=$t/include/surface/surface.h spec=$t/packaging/libsurface.spec
	hver=$(sed -n 's/^#define SURFACE_VERSION_STRING "\(.*\)"/\1/p' "$hdr")
	abi=$(sed -n 's/^#define SURFACE_ABI  *\([0-9][0-9]*\)$/\1/p' "$hdr")
	if [ -z "$ver" ] || [ -z "$abi" ]; then
		echo "  could not read VERSION or SURFACE_ABI"
		return 1
	fi
	spec_ver=$(sed -n 's/^Version: *//p' "$spec")
	spec_abi=$(sed -n 's/^%global abi  *//p' "$spec")
	deb_ver=$(sed -n '1s/^[^ ]* (\([^)]*\)).*/\1/p' "$t/debian/changelog")
	deb_ver=${deb_ver%%~*}   # an untagged build stamps ~git<sha> on the top entry
	deb_abi=$(sed -n 's/^Package: libsurface\([0-9][0-9]*\)$/\1/p' "$t/debian/control")
	dabi=$(sed -n 's/^#define SURFACE_DIGINET_ABI  *\([0-9][0-9]*\)$/\1/p' "$t/include/surface/surface_diginet.h")
	spec_dabi=$(sed -n 's/^%global dabi  *//p' "$spec")
	deb_dabi=$(sed -n 's/^Package: libsurface-diginet\([0-9][0-9]*\)$/\1/p' "$t/debian/control")

	[ "$hver" = "$ver" ] || { echo "  ARM 1: surface.h says $hver, VERSION says $ver"; bad=1; }
	[ "$spec_ver" = "$ver" ] || { echo "  ARM 1: the spec says Version $spec_ver, VERSION says $ver"; bad=1; }
	[ "$deb_ver" = "$ver" ] || { echo "  ARM 1: debian/changelog says $deb_ver, VERSION says $ver"; bad=1; }
	[ "$spec_abi" = "$abi" ] || { echo "  ARM 2: the spec's abi is $spec_abi, SURFACE_ABI is $abi"; bad=1; }
	[ "$deb_abi" = "$abi" ] || { echo "  ARM 2: debian/control builds libsurface$deb_abi, SURFACE_ABI is $abi"; bad=1; }
	p=libsurface$abi
	[ -f "$t/debian/$p.install" ] || { echo "  ARM 2: debian/$p.install is missing"; bad=1; }
	grep -q "^Depends:.* $p (= \${binary:Version})" "$t/debian/control" ||
		{ echo "  ARM 2: libsurface-dev does not depend on $p"; bad=1; }

	[ -n "$dabi" ] || { echo "  ARM 3: could not read SURFACE_DIGINET_ABI"; return 1; }
	[ "$spec_dabi" = "$dabi" ] || { echo "  ARM 3: the spec's dabi is $spec_dabi, SURFACE_DIGINET_ABI is $dabi"; bad=1; }
	[ "$deb_dabi" = "$dabi" ] || { echo "  ARM 3: debian/control builds libsurface-diginet$deb_dabi, SURFACE_DIGINET_ABI is $dabi"; bad=1; }
	p=libsurface-diginet$dabi
	[ -f "$t/debian/$p.install" ] || { echo "  ARM 3: debian/$p.install is missing"; bad=1; }
	grep -q "^Depends:.* $p (= \${binary:Version})" "$t/debian/control" ||
		{ echo "  ARM 3: libsurface-diginet-dev does not depend on $p"; bad=1; }

	mabi=$(sed -n 's/^#define SURFACE_MACKIE_ABI  *\([0-9][0-9]*\)$/\1/p' "$t/include/surface/surface_mackie.h")
	spec_mabi=$(sed -n 's/^%global mabi  *//p' "$spec")
	deb_mabi=$(sed -n 's/^Package: libsurface-mackie\([0-9][0-9]*\)$/\1/p' "$t/debian/control")
	[ -n "$mabi" ] || { echo "  ARM 4: could not read SURFACE_MACKIE_ABI"; return 1; }
	[ "$spec_mabi" = "$mabi" ] || { echo "  ARM 4: the spec's mabi is $spec_mabi, SURFACE_MACKIE_ABI is $mabi"; bad=1; }
	[ "$deb_mabi" = "$mabi" ] || { echo "  ARM 4: debian/control builds libsurface-mackie$deb_mabi, SURFACE_MACKIE_ABI is $mabi"; bad=1; }
	p=libsurface-mackie$mabi
	[ -f "$t/debian/$p.install" ] || { echo "  ARM 4: debian/$p.install is missing"; bad=1; }
	grep -q "^Depends:.* $p (= \${binary:Version})" "$t/debian/control" ||
		{ echo "  ARM 4: libsurface-mackie-dev does not depend on $p"; bad=1; }
	return $bad
}

plant() { # dir [sed-expression file]
	mkdir -p "$1/include/surface" "$1/packaging" "$1/debian"
	cp "$ROOT/VERSION" "$1/"
	cp "$ROOT/include/surface/surface.h" "$ROOT/include/surface/surface_diginet.h" \
	   "$ROOT/include/surface/surface_mackie.h" "$1/include/surface/"
	cp "$ROOT/packaging/libsurface.spec" "$1/packaging/"
	cp "$ROOT/debian/changelog" "$ROOT/debian/control" "$ROOT"/debian/*.install "$1/debian/"
	[ $# -lt 3 ] || sed -i "$2" "$1/$3"
}

tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
plant "$tmp/bad" 's/^Version: .*/Version: 9.9.9/' packaging/libsurface.spec
if check "$tmp/bad" >/dev/null; then
	echo "NOT A RESULT: a tree with the spec at 9.9.9 was not refused"
	exit 2
fi
plant "$tmp/bad3" 's/^Package: libsurface-diginet[0-9]*$/Package: libsurface-diginet9/' debian/control
if check "$tmp/bad3" >/dev/null; then
	echo "NOT A RESULT: a tree with the module packaged as libsurface-diginet9 was not refused"
	exit 2
fi
plant "$tmp/bad4" 's/^Package: libsurface-mackie[0-9]*$/Package: libsurface-mackie9/' debian/control
if check "$tmp/bad4" >/dev/null; then
	echo "NOT A RESULT: a tree with the module packaged as libsurface-mackie9 was not refused"
	exit 2
fi
check "$ROOT" || exit 1
echo "conformance-packaging: VERSION $(cat "$ROOT/VERSION"), sonames $(sed -n 's/^#define SURFACE_ABI  *//p' "$ROOT/include/surface/surface.h") (core) and $(sed -n 's/^#define SURFACE_DIGINET_ABI  *//p' "$ROOT/include/surface/surface_diginet.h") (diginet) and $(sed -n 's/^#define SURFACE_MACKIE_ABI  *//p' "$ROOT/include/surface/surface_mackie.h") (mackie) agree everywhere"
