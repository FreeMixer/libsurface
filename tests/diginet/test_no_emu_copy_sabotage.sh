#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# tests/diginet/no-emu-copy-sabotage.sh where a package build runs it: in a source tree with no
# git metadata (an unpacked release tarball), against libdiginet's desk emulator installed under
# a prefix (<root>/usr/include/diginet/testsupport/desk_emu.h). It must pass there, as it does
# in a git checkout.
set -u
top=$(cd "$(dirname "$0")/../.." && pwd -P)
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
fail=0

# the installed layout: an include directory holding libdiginet's emulator header
inc=$tmp/root/usr/include
mkdir -p "$inc/diginet/testsupport"
printf '#ifndef DIGINET_DESK_EMU_H\n#define DIGINET_DESK_EMU_H\nstruct desk_emu {\n\tint fd;\n};\n#endif\n' \
	> "$inc/diginet/testsupport/desk_emu.h"

# the unpacked tarball: this tree's files, no .git
if git -C "$top" rev-parse --show-toplevel 2>/dev/null | grep -qx "$top"; then
	files=$(cd "$top" && git ls-files -co --exclude-standard)
else
	files=$(cd "$top" && find . -type f ! -path './.git/*' | sed 's|^\./||')
fi
for f in $files; do
	mkdir -p "$tmp/src/$(dirname "$f")" && cp -p "$top/$f" "$tmp/src/$f" || exit 2
done
[ ! -e "$tmp/src/.git" ] || { echo "NOT A RESULT: the tarball fixture carries .git"; exit 2; }

expect() { # name tree
	out=$(cd "$tmp" && sh "$2/tests/diginet/no-emu-copy-sabotage.sh" "$inc" 2>&1); rc=$?
	if [ "$rc" -ne 0 ]; then echo "FAIL: $1 gave $rc: $out"; fail=1; else echo "ok: $1 -> 0"; fi
}
expect tarball "$tmp/src"
# reading nothing is red: an empty tree, and a checkout git lists no file of
mkdir -p "$tmp/empty" "$tmp/emptygit"; git -C "$tmp/emptygit" init -q
for e in empty emptygit; do
	out=$(sh "$top/tests/diginet/no-emu-copy.sh" "$tmp/$e" 2>&1); rc=$?
	if [ "$rc" -eq 1 ] && printf '%s\n' "$out" | grep -q 'nothing was read'; then echo "ok: $e -> 1"; else echo "FAIL: $e gave $rc: $out"; fail=1; fi
done
if git -C "$top" rev-parse --show-toplevel 2>/dev/null | grep -qx "$top"; then
	expect checkout "$top"
fi
exit $fail
