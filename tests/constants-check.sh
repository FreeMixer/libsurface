#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The motor and meter timing constants are declared ONCE, in include/surface/surface.h, and every
# other file reads them by name (audit 2026-09-25 X4: the TypeScript suites re-declared
# IMPLICIT_TOUCH_MS as 600 and hard-coded the coalesce window's 80 as a 120 ms settle: two
# copies of each, free to drift apart).
#   ARM 1  each constant has exactly one #define in the tree, in surface.h;
#   ARM 2  no source or test file defines a name with a constant's value and a unit of its
#          kind (a re-declaration under another name);
#   ARM 3  the library's code (src/*.c, comments aside) never spells a constant's value.
# A planted copy with a re-declaration runs first and must be refused, so the check cannot pass
# vacuously.
#
# Exit 0 conforms, 1 an arm found an offence, 2 the planted copy was not refused (NOT A RESULT).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAMES='SURFACE_IMPLICIT_TOUCH_MS SURFACE_MOTOR_COALESCE_MS SURFACE_TOUCH_TIMEOUT_MS
SURFACE_C24_RELEASE_DEBOUNCE_MS SURFACE_METER_KEEPALIVE_MS SURFACE_METER_STEP_HYSTERESIS'

check() {
	t=$1 bad=0
	hdr=$t/include/surface/surface.h
	for n in $NAMES; do
		v=$(sed -n "s/^#define $n  *\([0-9.][0-9.]*\)\$/\1/p" "$hdr")
		[ -n "$v" ] || { echo "  ARM 1: $n is not defined in surface.h"; bad=1; continue; }
		defs=$(grep -rn -E "^[[:space:]]*#[[:space:]]*define[[:space:]]+$n([[:space:](]|\$)" \
			"$t/include" "$t/src" "$t/tests" 2>/dev/null | wc -l)
		[ "$defs" -eq 1 ] || { echo "  ARM 1: $n has $defs definitions"; bad=1; }
		case $n in *_MS) unit='_MS' ;; *) unit='' ;; esac
		[ -n "$unit" ] || continue
		hits=$(grep -rn -E "^[[:space:]]*#[[:space:]]*define[[:space:]]+[A-Z0-9_]*$unit[[:space:]]+\(?$v\)?[[:space:]]*(/[*/].*)?\$" \
			"$t/src" "$t/tests" 2>/dev/null)
		[ -z "$hits" ] || { echo "  ARM 2: $n's value $v declared again:"; echo "$hits"; bad=1; }
		code=$(for f in "$t"/src/*.c; do
			sed -e 's|//.*||' -e 's|/\*.*\*/||g' -e 's|^[[:space:]]*\*.*||' -e 's|^[[:space:]]*/\*.*||' "$f" |
				grep -n -w -F "$v" | sed "s|^|$f:|"; done)
		[ -z "$code" ] || { echo "  ARM 3: $n's value $v spelled in the library's code:"; echo "$code"; bad=1; }
	done
	return $bad
}

tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bad"
cp -r "$ROOT/include" "$ROOT/src" "$ROOT/tests" "$tmp/bad/"
printf '#define IMPLICIT_TOUCH_MS 600 /* mirrors surface.h */\n' >> "$tmp/bad/tests/harness.h"
if check "$tmp/bad" >/dev/null; then
	echo "NOT A RESULT: a tree that re-declares the implicit-touch window was not refused"
	exit 2
fi
check "$ROOT" || exit 1
echo "constants-check: $(echo $NAMES | wc -w) constants, each declared once in surface.h"
