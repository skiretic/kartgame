#!/usr/bin/env bash
#
# The #240 gate: drive the kart on the **real circuit**, not on a plane.
#
#     tools/verify/terrain.sh                     everything
#     tools/verify/terrain.sh --case=curb,verge   a subset
#     tools/verify/terrain.sh --break             the five negative controls
#     tools/verify/terrain.sh --break=input       one of them
#
# ## What this is for
#
# Every other numeric driving rig in this repo drives a plane. `test_vehicle.cpp`'s
# `Rig` synthesizes a `GroundQuery` with `normal = (0,1,0)` and
# `surface_grip = 1.0`, `test_scrub_energy.cpp` and `test_yaw_stability.cpp` each
# carry a copy of it, and `drive.sh` drives the proving ground, which
# `ARCHITECTURE.md` describes as having "no features on purpose". So every §6.4
# figure describes a kart on a plane. Issue #240: a rig that holds a variable
# constant in order to isolate something owes a second rig that varies it.
#
# ## The order matters and is not alphabetical
#
# `calibrate` runs **first**, and if it fails nothing after it means anything: it
# is the check that this rig, on a plane built to `proving_ground.gd`'s own
# numbers, reproduces `drive.sh`'s figures. Until that passes, every other number
# here is measuring the rig.
#
# **Its reference figures are taken from `drive_probe.gd` at run time**, not
# written down here. `CLAUDE.md`'s own table said 139.8 km/h and 4.51 s while the
# build measures 140.1 and 4.38, which is exactly what a hardcoded reference does
# eventually. This script runs `drive.sh --once --scenario=accel` and parses its
# output, so the two can only ever disagree about the kart.
#
# ## Ground pairing
#
# Most cases run **twice**: once with `--ground=circuit` and once with
# `--ground=flat`, same scenario, same entry speed, same input. A figure measured
# on 4.6% of grade means nothing on its own; what it means is the difference from
# the identical run on a plane. The two grounds are two processes on purpose —
# `real_t` is `float`, so separating two worlds inside one scene by the tens of
# kilometers it would take quantizes position to 2.4 mm, which is coarser than the
# suspension travel this rig reports.
#
# The double import is the ADR-0018 workaround; see tools/verify/verify.sh.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
CASES=()
BREAK=""
BREAK_ALL=0
FORWARDED=()

# Every case, in the order they are meant to be read. `survey` and `place` first
# because they are what the driving cases' station choices and placement rest on;
# `calibrate` before anything that publishes a number.
ALL_CASES=(survey place calibrate sixfour slope crossfall curb verge step zero sweep gripsweep)

# Which cases have a flat-ground pair. `survey` and `place` have no ground at all,
# `calibrate` is flat by definition, and `step` is a property of the height field.
PAIRED_CASES=" sixfour slope crossfall curb verge zero sweep "

# The five negative controls, and what each one sabotages. Each is run against the
# case it can actually reach — a control run against a case that cannot see it is
# not a control, it is a coincidence waiting to be reported as a catch.
BREAK_MODES=(input flatground nocurb noterrain calib onroad)
break_case() {
	case "$1" in
		input)      echo "sixfour" ;;
		flatground) echo "sixfour" ;;
		nocurb)     echo "curb" ;;
		noterrain)  echo "step" ;;
		calib)      echo "calibrate" ;;
		# #243. Moves the two-wheel verge row back inside the white line, so its
		# departure check passes having driven asphalt. The straddle check is what
		# has to catch it.
		onroad)     echo "verge" ;;
	esac
}

for argument in "$@"; do
	case "$argument" in
		--godot=*)  GODOT="${argument#*=}" ;;
		--case=*)   IFS=',' read -r -a CASES <<< "${argument#*=}" ;;
		--break)    BREAK_ALL=1 ;;
		--break=*)  BREAK="${argument#*=}" ;;
		*)          FORWARDED+=("$argument") ;;
	esac
done

if [ ${#CASES[@]} -eq 0 ]; then
	CASES=("${ALL_CASES[@]}")
fi

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	exit 127
fi

"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

# `drive.sh`'s own figures, read from the running probe. See the header.
REFERENCE=""
read_reference() {
	local report
	report="$("$PROJECT_ROOT/tools/verify/drive.sh" --once --scenario=accel --godot="$GODOT" 2>&1)"
	local top zero100
	top="$(echo "$report" | awk '/top speed/ {print $3}')"
	zero100="$(echo "$report" | awk '/0-100 km\/h/ {print $3}')"
	if [ -z "$top" ] || [ -z "$zero100" ]; then
		echo "error: could not read drive.sh's accel figures; calibration cannot run" >&2
		return 1
	fi
	REFERENCE="$top,$zero100"
	echo "reference from drive_probe.gd: top $top km/h, 0-100 $zero100 s"
}

run_case() {
	local name="$1"
	local ground="$2"
	shift 2
	"$GODOT" --headless --path "$PROJECT_ROOT" \
		--script tools/verify/terrain_probe.gd -- \
		"--case=$name" "--ground=$ground" "$@" \
		${FORWARDED[@]+"${FORWARDED[@]}"} 2>&1
}

FAILED=0

# --- the negative controls -----------------------------------------------------
#
# The exit code is INVERTED: a sabotage that is *not* caught is the failure. And
# catching is not "something went red" — `terrain_probe.gd`'s `_break_fingerprint`
# names the one check each mode has to have taken red and refuses a run where that
# specific check is still green. `shell_probe.gd`'s first cut reported "caught" off
# a pre-existing red it had not caused, and this is the fix for that shape.
if [ "$BREAK_ALL" = "1" ] || [ -n "$BREAK" ]; then
	MODES=("${BREAK_MODES[@]}")
	if [ -n "$BREAK" ]; then
		MODES=("$BREAK")
	fi
	if printf '%s\n' "${MODES[@]}" | grep -qx calib; then
		read_reference || exit 1
	fi
	CAUGHT=0
	for mode in "${MODES[@]}"; do
		case_name="$(break_case "$mode")"
		if [ -z "$case_name" ]; then
			echo "error: no case is wired to --break=$mode" >&2
			exit 2
		fi
		ground="circuit"
		if [ "$mode" = "calib" ]; then
			ground="flat"
		fi
		if [ -n "$REFERENCE" ]; then
			OUT="$(run_case "$case_name" "$ground" "--break=$mode" "--reference=$REFERENCE")"
		else
			OUT="$(run_case "$case_name" "$ground" "--break=$mode")"
		fi
		STATUS=$?
		echo "$OUT" | grep -E '^    (break=|negative control|[a-z].* (ok|FAIL))' | sed "s/^/  [$mode] /"
		if [ "$STATUS" = "0" ]; then
			CAUGHT=$((CAUGHT + 1))
		else
			echo "  [$mode] NOT CAUGHT — the check this sabotage targets is still green" >&2
			FAILED=1
		fi
	done
	echo "  $CAUGHT of ${#MODES[@]} negative control(s) caught"
	if [ "$FAILED" = "1" ]; then
		echo "error: a sabotage went undetected. A check that cannot fail is not a check." >&2
		exit 1
	fi
	exit 0
fi

# --- the measurement run -------------------------------------------------------

for case_name in "${CASES[@]}"; do
	echo ""
	echo "=== $case_name ============================================================"
	EXTRA=()
	if [ "$case_name" = "calibrate" ]; then
		if [ -z "$REFERENCE" ]; then
			read_reference || exit 1
		fi
		EXTRA=("--reference=$REFERENCE")
	fi

	GROUNDS=("circuit")
	case " $case_name " in
		" survey "|" place ") GROUNDS=("circuit") ;;
		" calibrate ")        GROUNDS=("flat") ;;
		# #243's bisection is a plane by definition — its whole claim is that the
		# only variable in the run is `surface_grip`, and the circuit is a second
		# variable. Running it on the circuit would produce a table nobody could read.
		" gripsweep ")        GROUNDS=("flat") ;;
		*)
			if [[ "$PAIRED_CASES" == *" $case_name "* ]]; then
				# Flat first: it is the control, and reading the control before the
				# measurement is the difference between a comparison and a story.
				GROUNDS=("flat" "circuit")
			fi
			;;
	esac

	for ground in "${GROUNDS[@]}"; do
		if [ ${#GROUNDS[@]} -gt 1 ]; then
			echo "--- ground: $ground"
		fi
		OUT="$(run_case "$case_name" "$ground" ${EXTRA[@]+"${EXTRA[@]}"})"
		STATUS=$?
		echo "$OUT" | grep -vE '^(Godot Engine|WARNING:|   at: cleanup)'
		if [ "$STATUS" != "0" ]; then
			echo "    $case_name/$ground FAILED — see its own checks above" >&2
			FAILED=1
		fi
		# The calibration gates everything after it. A rig that does not reproduce
		# `drive.sh` on a plane is measuring itself.
		if [ "$case_name" = "calibrate" ] && [ "$STATUS" != "0" ]; then
			echo "error: calibration failed. Every figure after this would be the rig," >&2
			echo "       not the kart. Stopping." >&2
			exit 1
		fi
	done
done

echo ""
if [ "$FAILED" = "1" ]; then
	echo "error: a case failed. Read its check list, not just this line." >&2
	exit 1
fi
echo "all cases green"
