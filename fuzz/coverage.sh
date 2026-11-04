#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# THE COVERAGE WITNESS (`make fuzz-coverage`). Each harness, built for llvm-cov without
# sanitizers, runs once over its seed corpus (-runs=0 executes the corpus and nothing else);
# this prints the line coverage of the reader functions the harness exists to drive, and
# every line of them the corpus never reached. A file's other functions (log strings, the
# socket code) are not the harness's job and are not counted.
#
# THE FLOOR. fuzz/coverage-floor.txt holds, per harness, the number of covered lines the
# committed corpus reaches. A harness whose covered lines fall below its floor turns this
# red: a seed lost, a harness that stopped driving a function, or a reader that grew lines
# its corpus never reaches. Raise a floor when coverage rises; lower one only together with
# the change that removed the lines.
#
# THE HARNESS FLOOR. Every property check of the harness itself, each abort() in
# fuzz/fuzz_<harness>.c and the statement that guards it (fuzz/checks.awk), must be executed
# by at least one seed. A check no seed reaches is a property nobody tests, and the
# library's line count cannot see it; this names the harness file and the line of every
# such check and turns red. So does a check whose comment holds no `mutant:` and `input:`
# record and no `not a property:` line (fuzz/records.sh proves the records): a check that
# names no bug it catches is a property nobody showed can fail.
#
# Usage: coverage.sh <cov build dir> <harness>...   (LLVM_COV names llvm-cov)
set -eu
B=$1
shift
LLVM_COV=${LLVM_COV:-llvm-cov}
FLOOR=fuzz/coverage-floor.txt
[ -f "$FLOOR" ] || { echo "coverage.sh: no $FLOOR" >&2; exit 2; }
low=
dead=

# The functions each harness covers: what "every reader line the harness means to cover"
# means, one list per harness. Static functions are matched by bare name.
funcs() {
	case $1 in
	pure) echo src/meter.c src/surface.c :: \
		round_half_up clamp01 surface_meter_step span surface_ring_mask surface_geometry_preset \
		surface_strerror ;;
	board) echo src/surface.c :: \
		geometry_ok surface_create surface_destroy surface_geometry_of surface_online \
		surface_nstrips led_slot surface_led surface_ring surface_ring_code scribble_text \
		surface_scribble surface_counter surface_colour surface_blank_strip surface_touch_reset \
		surface_in_online surface_in_offline ;;
	bank) echo src/bank.c :: \
		bank_width bank_clamp bank_move_by bank_index surface_bank_set surface_bank_step \
		surface_bank_show surface_bank_jump surface_bank_offset surface_strip_target \
		surface_target_strip surface_master_set ;;
	motor) echo src/motor.c src/surface.c :: \
		motor_find motor_idle motor_state motor_epsilon motor_raw motor_touched motor_implicit_open \
		motor_handled motor_arm_flush motor_write_strip motor_write motor_drive motor_moved \
		surface_fader_served_at surface_fader_served motor_released surface_fader_refused \
		motor_hold motor_seed motor_paint_strip surface_fader_paint surface_fader_paint_strip \
		surface_fader_suspend surface_strip_fader motor_tick motor_deadline touch_held \
		strip_position setpoint_echo touch_release surface_in_touch surface_note_commanded \
		surface_fader_is_report surface_in_fader surface_in_key surface_in_vpot surface_in_jog \
		surface_in_warning surface_deadline surface_tick ;;
	keys) echo src/diginet/c24_keys.c :: surface_c24_key_find ;;
	*) echo "coverage.sh: no function list for harness '$1'" >&2; exit 2 ;;
	esac
}

for h in "$@"; do
	set -- $(funcs "$h")
	files=
	while [ "$1" != "::" ]; do files="$files $1"; shift; done
	shift
	bin=$B/fuzz_$h prof=$B/$h.profdata
	echo "== fuzz_$h: $(ls fuzz/corpus/$h | wc -l) seed inputs"
	# Name Regions Miss Cover Lines Miss Cover ...: keep the listed functions.
	tot=$B/$h.covered
	"$LLVM_COV" report -show-functions "$bin" -instr-profile="$prof" $files |
	awk -v want="$*" -v tot="$tot" '
		BEGIN { n = split(want, w, " "); for (i = 1; i <= n; i++) keep[w[i]] = 1 }
		{ name = $1; sub(/^.*:/, "", name) }
		(name in keep) && NF >= 10 {
			printf "  %-34s lines %4d  missed %3d  %s\n", name, $5, $6, $7
			lines += $5; missed += $6; seen[name] = 1
		}
		END {
			for (k in keep) if (!(k in seen)) printf "  %-34s NOT FOUND in the report\n", k
			printf "  %-34s lines %4d  missed %3d  %.2f%%\n", "TOTAL", lines, missed,
			       lines ? 100 * (lines - missed) / lines : 0
			print lines - missed > tot
		}'
	for f in "$@"; do
		"$LLVM_COV" show "$bin" -instr-profile="$prof" -name-regex="^(.*:)?$f\$" $files \
			2>/dev/null |
		awk -v f="$f" -F'|' '$2 ~ /^ *0$/ {
			sub(/^ */, "", $1); printf "    unreached %s:%s %s\n", f, $1, $3 }'
	done
	src=fuzz/fuzz_$h.c
	"$LLVM_COV" show "$bin" -instr-profile="$prof" "$src" |
		awk -v file="$src" -f fuzz/checks.awk >"$B/$h.checks"
	nchk=$(wc -l <"$B/$h.checks")
	if [ "$nchk" -eq 0 ]; then
		echo "  CHECKS: no property check found in $src"
		dead="$dead $h"
	elif awk -F: '$4 == "0" && $5 != "not-a-property" { n++ } END { exit !n }' "$B/$h.checks"; then
		awk -F: '$4 == "0" && $5 != "not-a-property" { t = $0; sub(/^([^:]*:){5}/, "", t)
			printf "  CHECK: %s:%d no seed reaches (abort at line %d): %s\n", $1, $2, $3, t }' \
			"$B/$h.checks"
		dead="$dead $h"
	elif awk -F: '$5 == "NONE" || $5 == "BAD" { n++ } END { exit !n }' "$B/$h.checks"; then
		awk -F: '$5 == "NONE" || $5 == "BAD" { t = $0; sub(/^([^:]*:){5}/, "", t)
			printf "  RECORD: %s:%d has no mutant: and input: record and is no stop after a failed allocation (%s): %s\n",
			       $1, $3, $5, t }' "$B/$h.checks"
		dead="$dead $h"
	else
		echo "  checks: all $nchk property checks of $src reached"
	fi
	covered=$(cat "$tot")
	floor=$(awk -v h="$h" '$1 == h { print $2 }' "$FLOOR")
	if [ -z "$floor" ]; then
		echo "  FLOOR: fuzz_$h has no line in $FLOOR"
		low="$low $h"
	elif [ "$covered" -lt "$floor" ]; then
		echo "  FLOOR: fuzz_$h covers $covered reader lines, below its floor of $floor"
		low="$low $h"
	else
		echo "  floor $floor: held ($covered covered)"
	fi
done

[ -z "$low" ] || echo "make fuzz-coverage: RED -- below the coverage floor:$low"
[ -z "$dead" ] || echo "make fuzz-coverage: RED -- harness checks no seed reaches, or with no record:$dead"
[ -z "$low$dead" ] || exit 1
echo "make fuzz-coverage: every harness at or above its coverage floor, every check reached"
