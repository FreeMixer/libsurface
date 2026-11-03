#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The sabotage of tests/diginet/no-emu-copy.sh: the tree as it is passes it; the same tree with
# a copy of libdiginet's own desk_emu.h put back where libsurface used to keep one fails it,
# naming that file.
#
#   tests/diginet/no-emu-copy-sabotage.sh <libdiginet include directory>
set -u
inc=${1:?usage: no-emu-copy-sabotage.sh <libdiginet include directory>}
copy=$inc/diginet/testsupport/desk_emu.h
[ -f "$copy" ] || { echo "FAIL no $copy: libdiginet-emu is not installed there"; exit 1; }
top=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
(cd "$top" && git ls-files -co --exclude-standard | while read -r f; do mkdir -p "$tmp/tree/$(dirname "$f")"; cp -p "$f" "$tmp/tree/$f"; done)
sh "$top/tests/diginet/no-emu-copy.sh" "$tmp/tree" >/dev/null || { echo "FAIL the tree as it is fails no-emu-copy.sh"; exit 1; }
cp "$copy" "$tmp/tree/tests/diginet/desk_emu.h"
out=$(sh "$top/tests/diginet/no-emu-copy.sh" "$tmp/tree")
rc=$?
if [ $rc -eq 1 ] && printf '%s\n' "$out" | grep -q '^FAIL tests/diginet/desk_emu.h '; then
	echo "no-emu-copy-sabotage: a copy of libdiginet's desk_emu.h put back is red: $out"
	exit 0
fi
echo "FAIL a copy of libdiginet's desk_emu.h put back is not caught (exit $rc): $out"
exit 1
