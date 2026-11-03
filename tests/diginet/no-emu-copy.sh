#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# libdiginet's desk emulator, its pump and its private-network helper are libdiginet's
# (libdiginet-emu): no C file of this tree defines any of them again.
#
#   tests/diginet/no-emu-copy.sh [tree]
#
# Every tracked file of a git tree (every file of any other) is read. Exit 0 when none defines
# them, 1 naming each file that does.
set -u
top=${1:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$top" || exit 2
if [ -d .git ] || git rev-parse --show-toplevel 2>/dev/null | grep -qx "$(pwd -P)"; then
	files=$(git ls-files -co --exclude-standard)
else
	files=$(find . -type f ! -path './.git/*' | sed 's|^\./||')
fi
# what only libdiginet-emu defines: the emulator's state and its first function, the pump, and
# the include guards of its three headers
pat='^[[:space:]]*(void[[:space:]]+emu_init[[:space:]]*\(|struct[[:space:]]+desk_emu[[:space:]]*\{|static[[:space:]]+inline[[:space:]]+void[[:space:]]+emu_pump_fd[[:space:]]*\(|#[[:space:]]*define[[:space:]]+DIGINET_(DESK_EMU|EMU_PUMP|TEST_NETNS)_H)'
bad=0
for f in $files; do
	case $f in *.c|*.h) ;; *) continue ;; esac
	if grep -qE "$pat" "$f"; then
		echo "FAIL $f defines libdiginet's desk emulator again: build against libdiginet-emu instead"
		bad=1
	fi
done
[ $bad -eq 0 ] && echo "no-emu-copy: no file defines libdiginet's desk emulator"
exit $bad
