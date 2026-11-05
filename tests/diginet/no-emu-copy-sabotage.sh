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
# the tree's files as no-emu-copy.sh reads them: git's list in a checkout, every file in an
# unpacked tarball (a package build has no .git)
cd "$top" || exit 2
if [ -d .git ] || git rev-parse --show-toplevel 2>/dev/null | grep -qx "$(pwd -P)"; then
	files=$(git ls-files -co --exclude-standard)
else
	files=$(find . -type f ! -path './.git/*' | sed 's|^\./||')
fi
[ -n "$files" ] || { echo "FAIL no file listed in $top"; exit 1; }
for f in $files; do
	mkdir -p "$tmp/tree/$(dirname "$f")" && cp -p "$f" "$tmp/tree/$f" || exit 2
done
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
