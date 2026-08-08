#!/usr/bin/env bash
#
# The HUD gate: what is on screen while the kart is moving.
#
#     tools/verify/hud.sh [path-to-godot]
#     tools/verify/hud.sh --case=glyphs,layout
#     tools/verify/hud.sh --break                the negative-control pass
#     tools/verify/hud.sh --break=tofu           one mode
#
# Issue #242 area 2. `shell.sh` covers the ten menu screens and `session_probe.gd`
# covers the lap timer underneath; the two overlays a driver actually reads --
# `scripts/ui/driving_hud.gd` and `scripts/ui/timing_hud.gd` -- had no gate at all
# for five milestones. This is it.
#
# ## Why there is no --headless here, and why that is the whole point
#
# Both overlays open with
#
#     if DisplayServer.get_name() == "headless": hide(); return
#
# for a good reason: headless has no rendering device, so every draw call in them
# emits an error with a stack trace, about forty a tick, and that once buried a
# drive.sh scenario report under what looked like a solver fault. The cost of that
# guard is that **no headless gate can ever see these files** -- a headless probe
# measures a blank frame and reports every check green. So this one runs against a
# real device the way tools/shots/shoot.gd does, and a window opens for the few
# seconds it takes. hud_probe.gd refuses outright under --headless rather than
# reporting a confident pass over nothing.
#
# ## The negative controls
#
# --break sabotages one property in-process and the probe asserts the check aimed
# at it went red carrying the saboteur's own fingerprint. **The exit code is
# inverted**: 0 means the sabotage was caught. Bare --break runs every mode and
# fails if any one goes through unnoticed. The mode list comes from the probe
# (--list-breaks) rather than being repeated here, so a new mode joins this pass
# without a second edit.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
RESOLUTION="1600x900"
BREAK_ALL=0
FORWARDED=()
for argument in "$@"; do
	case "$argument" in
		--break)        BREAK_ALL=1 ;;
		--resolution=*) RESOLUTION="${argument#*=}" ;;
		--*)            FORWARDED+=("$argument") ;;
		*)              GODOT="$argument" ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/hud.sh /path/to/godot" >&2
	exit 127
fi

# The first headless *editor* import of a cold project dies after .godot/ has been
# seeded, so the second run is clean. Import twice, ignore the first exit code.
# ADR-0018, and verify.sh and shoot.sh both do the same.
echo "==> Importing (cold pass; a crash here is the known bug, see ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true

echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

probe() {
	"$GODOT" \
		--path "$PROJECT_ROOT" \
		--resolution "$RESOLUTION" \
		--audio-driver Dummy \
		--script tools/verify/hud_probe.gd \
		-- "$@"
}

if [ "$BREAK_ALL" -eq 0 ]; then
	echo "==> Verifying the HUD"
	probe ${FORWARDED[@]+"${FORWARDED[@]}"}
	STATUS=$?
	if [ "$STATUS" -eq 0 ]; then
		echo "hud gate: PASSED"
	else
		echo "hud gate: FAILED -- see the FAIL lines above" >&2
	fi
	exit "$STATUS"
fi

# The negative-control pass. Every mode must be caught; one that slips through is
# a hole in the gate and is reported as one, by name. Godot prints its version
# banner on stdout ahead of anything a script says, so the mode list is tagged and
# filtered rather than read raw -- shell.sh records five engine words having been
# run as five --break modes, each of which "went through unnoticed".
echo "==> Negative controls (each sabotage must be caught)"
MODES="$(probe --list-breaks | sed -n 's/^break-mode //p')"
if [ -z "$MODES" ]; then
	echo "error: the probe listed no --break modes" >&2
	exit 1
fi

MISSED=()
TOTAL=0
for mode in $MODES; do
	TOTAL=$((TOTAL + 1))
	LINE="$(probe --break="$mode" ${FORWARDED[@]+"${FORWARDED[@]}"} \
		| grep '^negative control' || true)"
	if [ -z "$LINE" ]; then
		LINE="negative control --break=$mode: the probe printed no verdict"
	fi
	echo "    $LINE"
	case "$LINE" in
		*"NOT CAUGHT"*|*"no verdict"*) MISSED+=("$mode") ;;
	esac
done

if [ "${#MISSED[@]}" -eq 0 ]; then
	echo "hud gate: negative controls $TOTAL of $TOTAL caught"
	exit 0
fi
echo "hud gate: ${#MISSED[@]} of $TOTAL sabotages went through unnoticed -- ${MISSED[*]}" >&2
exit 1
