#!/usr/bin/env bash
#
# Issue #239's gate: the stick -> input.steer path, measured end to end.
#
#   tools/verify/stick.sh                 the whole thing
#     --case=<a,b>                        a subset: deadzone, gamma, lock, depart
#     --break                             the negative controls: three sabotages
#                                         that must each be caught. Exit code is
#                                         INVERTED per mode
#
# ## Why this is a shell script and not one probe run
#
# `depart`'s question is not "did the kart spin". It is "did the STICK spin it",
# and no single run can answer that: the answer is a *difference* between a run
# with the stick and a run without it, and each needs its own process because each
# needs its own scene. So this script measures the reference first, at stick 0.0,
# and hands it to the run that matters with `--reference`.
#
# It also runs the same rig on the proving ground, which is the featureless flat
# plane every S6.4 figure in this project was measured on. Same kart, same input
# path, same throttle, different ground. That comparison is what keeps #239 and
# #240 two tickets instead of one argument.
#
# ## What "green" means here
#
# Green means the input path is not the fault. The circuit run departs and is
# **expected** to depart -- the check that fires is named `#240:` and this script
# reports it as a handoff rather than counting it as its own failure. The check
# this gate owns is `#239:`, and it goes red only if the stick changes the
# outcome. It is also red if it never ran: a missing attribution is not a pass.

set -uo pipefail

cd "$(dirname "$0")/../.."

GODOT="${GODOT:-godot}"
PROBE="tools/verify/stick_probe.gd"
PROFILE_DIR="user://stick_probe/"

CASES=""
BREAK=0
for arg in "$@"; do
	case "$arg" in
		--case=*) CASES="${arg#--case=}" ;;
		--break) BREAK=1 ;;
		*) echo "unknown argument: $arg" >&2; exit 2 ;;
	esac
done

run() {
	# Every run gets --profile-dir. Every worktree shares one `user://`, keyed on
	# application/config/name, so a probe that lets `circuit.gd` reach the real
	# settings file is writing into Anthony's career from inside a worktree.
	"$GODOT" --headless --path . --script "$PROBE" -- \
		--profile-dir="$PROFILE_DIR" "$@" 2>&1
}

# --- the negative controls ----------------------------------------------------

if [ "$BREAK" = 1 ]; then
	echo "=== negative controls: each sabotage must be CAUGHT ==="
	failed=0
	# Each mode is paired with the case that owns the checks it takes down. A
	# control run against a case that does not run those checks reports "these
	# checks did not run" rather than passing, which is the failure mode the
	# fingerprint logic exists for.
	for pair in "deadzone:deadzone" "gamma:gamma,lock" "dead:lock"; do
		mode="${pair%%:*}"
		cases="${pair#*:}"
		echo ""
		echo "--- --break=$mode over --case=$cases ---"
		out=$(run --break="$mode" --case="$cases")
		verdict=$(printf '%s\n' "$out" | grep '^negative control' || true)
		code=$?
		printf '%s\n' "$out" | grep -E '^check|^negative control|^stick probe' || true
		if printf '%s\n' "$verdict" | grep -q 'CAUGHT'; then
			echo "  -> caught"
		else
			echo "  -> MISSED"
			failed=$((failed + 1))
		fi
	done
	echo ""
	if [ "$failed" = 0 ]; then
		echo "negative controls: 3 of 3 caught"
		exit 0
	fi
	echo "negative controls: $failed of 3 MISSED"
	exit 1
fi

# --- the measurement ----------------------------------------------------------

status=0

if [ -z "$CASES" ] || printf '%s' "$CASES" | grep -q 'deadzone\|gamma\|lock'; then
	subset="${CASES:-deadzone,gamma,lock}"
	# `depart` is driven separately below, so strip it out of a subset run.
	subset=$(printf '%s' "$subset" | tr ',' '\n' | grep -v '^depart$' | paste -sd, -)
	if [ -n "$subset" ]; then
		echo "=== the input path: $subset ==="
		out=$(run --case="$subset")
		printf '%s\n' "$out" | grep -vE '^\s*$' | grep -v '^WARNING' | grep -v '^   at:'
		printf '%s\n' "$out" | grep -q 'stick probe: .* 0 failed' || status=1
	fi
fi

if [ -n "$CASES" ] && ! printf '%s' "$CASES" | grep -q 'depart'; then
	exit "$status"
fi

echo ""
echo "=== the same rig on a flat plane, stick 0.0: can it drive straight at all? ==="
flat=$(run --case=depart --scene=flat --stick=0.0)
printf '%s\n' "$flat" | grep -E '^  (phase|accel|hold|speed|body|peak|wheel|ticks|first|crossed)|^check|^stick probe'
printf '%s\n' "$flat" | grep -q 'stick probe: .* 0 failed' || {
	echo "the flat-plane baseline departed. Nothing below means anything until that is"
	echo "understood -- this rig cannot drive straight on the ground every figure in"
	echo "the repo was measured on."
	exit 1
}

echo ""
echo "=== the circuit, stick 0.0: the reference ==="
ref_out=$(run --case=depart --scene=circuit --stick=0.0)
printf '%s\n' "$ref_out" | grep -E '^  (phase|accel|hold|speed|body|peak|wheel|ticks|first|crossed)|^check|^stick probe'
reference=$(printf '%s\n' "$ref_out" \
	| sed -n 's/.*peak body slip while the stick was held \([0-9.]*\) deg.*/\1/p')
if [ -z "$reference" ]; then
	echo "could not read the reference peak body slip out of the run" >&2
	exit 1
fi

echo ""
echo "=== the circuit, the smallest producible stick: does the stick change it? ==="
real=$(run --case=depart --scene=circuit --reference="$reference")
printf '%s\n' "$real" | grep -E '^  (phase|accel|hold|speed|body|peak|wheel|ticks|first|crossed)|^check|^stick probe'

owned=$(printf '%s\n' "$real" | grep '^check #239:' || true)
handoff=$(printf '%s\n' "$real" | grep '^check #240:' || true)

echo ""
echo "=== verdict ==="
if [ -z "$owned" ]; then
	echo "#239 INPUT PATH: NOT MEASURED -- the attribution check did not run."
	status=1
elif printf '%s' "$owned" | grep -q 'FAIL'; then
	echo "#239 INPUT PATH: the stick changed the outcome. This ticket owns it."
	echo "  $owned"
	status=1
else
	echo "#239 INPUT PATH: clean. The stick did not change the outcome --"
	echo "  the same departure happens at stick 0.0, with input.steer bit-identical zero."
	echo "  $owned"
fi
if printf '%s' "$handoff" | grep -q 'FAIL'; then
	echo ""
	echo "HANDOFF TO #240: the kart departs on the circuit and does NOT on the flat plane,"
	echo "  with the same rig, the same input path and no steering at all. The ground is"
	echo "  the variable. Not counted against this gate."
	echo "  $handoff"
	echo
	echo "  ANSWERED, and not the way this gate's shape implies -- read #240's own rig"
	echo "  (tools/verify/terrain.sh) before drawing anything from the line above."
	echo "  A kart at full throttle with the steering at zero does not follow a road that"
	echo "  turns, so this protocol runs off the circuit at the first corner and departs"
	echo "  on gravel. terrain_probe.gd --case=zero holds 130 km/h at 56 stations round"
	echo "  the whole lap and measures a worst body slip ON THE ROAD of 1.20 deg against"
	echo "  0.37 on the plane. Nothing departs while the chassis is inside the lines."
	echo
	echo "  The reason this looked like a road departure is that surface_type 0 covers"
	echo "  the racing surface, the pit lane, the run-off aprons AND the barriers, so a"
	echo "  four-wheels-on-asphalt census cannot tell a kart on the road from one 14 m"
	echo "  into the run-off. Only project() against sample()[\"width\"] can, and this"
	echo "  probe has no such column. The real mechanism is two wheels on the verge at"
	echo "  129 km/h, which #240 measures at 179.5 deg of slip."
fi
exit "$status"
