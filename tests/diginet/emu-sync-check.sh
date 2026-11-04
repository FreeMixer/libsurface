#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The copies of libdiginet's test doubles in tests/diginet/ (see tests/diginet/SOURCE) are the
# files of the libdiginet tree we build against, byte for byte:
#
#   tests/diginet/emu-sync-check.sh <libdiginet source tree>
#
# libdiginet kept them in tests/ up to 0.1.7; since then they are testsupport/desk_emu.c and
# include/diginet/testsupport/*.h, with <diginet/testsupport/...> includes where the copies
# have "desk_emu.h" (and the header comment names the path). Both layouts are accepted; for
# the newer one those two lines are the only differences allowed.
#
# Exit 0 identical, 1 a copy differs or is missing there, 77 no tree given (an installed
# libdiginet carries no test code: nothing to compare).
set -u
src=${1:-}
[ -n "$src" ] || { echo "emu-sync-check: no libdiginet tree (DIGINET_SRC): nothing compared"; exit 77; }
here=$(cd "$(dirname "$0")" && pwd)
bad=0
norm() { sed -e 's|<diginet/testsupport/desk_emu\.h>|"desk_emu.h"|' -e 's|(diginet/testsupport/desk_emu\.h)|(tests/desk_emu.h)|' "$1"; }
for f in desk_emu.c desk_emu.h emu_pump.h netns.h; do
	if [ -f "$src/tests/$f" ]; then
		theirs=$src/tests/$f
	elif [ "$f" = desk_emu.c ] && [ -f "$src/testsupport/$f" ]; then
		theirs=$src/testsupport/$f
	elif [ -f "$src/include/diginet/testsupport/$f" ]; then
		theirs=$src/include/diginet/testsupport/$f
	else
		echo "emu-sync-check: tests/diginet/$f has no counterpart in $src"
		bad=1
		continue
	fi
	if ! norm "$theirs" | cmp -s "$here/$f" -; then
		echo "emu-sync-check: tests/diginet/$f is not $theirs"
		bad=1
	fi
done
[ $bad -eq 0 ] && echo "emu-sync-check: the emulator copies are libdiginet's"
exit $bad
