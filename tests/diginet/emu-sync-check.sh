#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The copies of libdiginet's test doubles in tests/diginet/ (see tests/diginet/SOURCE) are the
# files of the libdiginet tree we build against, byte for byte:
#
#   tests/diginet/emu-sync-check.sh <libdiginet source tree>
#
# Exit 0 identical, 1 a copy differs or is missing there, 77 no tree given (an installed
# libdiginet carries no test code: nothing to compare).
set -u
src=${1:-}
[ -n "$src" ] || { echo "emu-sync-check: no libdiginet tree (DIGINET_SRC): nothing compared"; exit 77; }
here=$(cd "$(dirname "$0")" && pwd)
bad=0
for f in desk_emu.c desk_emu.h emu_pump.h netns.h; do
	if ! cmp -s "$here/$f" "$src/tests/$f"; then
		echo "emu-sync-check: tests/diginet/$f is not $src/tests/$f"
		bad=1
	fi
done
[ $bad -eq 0 ] && echo "emu-sync-check: the emulator copies are libdiginet's"
exit $bad
