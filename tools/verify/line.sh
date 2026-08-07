#!/usr/bin/env bash
#
# The M7 racing-line gate: is the line on the road, and does the speed profile
# promise anything the kart cannot do?
#
#     tools/verify/line.sh [path-to-godot]
#     tools/verify/line.sh --case=valdirone
#     tools/verify/line.sh --break            # every negative control
#     tools/verify/line.sh --break=corridor   # one of them
#
# Every argument that is not a path to Godot is forwarded to line_probe.gd.
#
# Needs NO generated asset: the probe loads `track.json` and walks
# `TrackLayout`'s own constants, and never instantiates a scene. So it runs in a
# fresh worktree with no kart.glb, which the M5f shell gate learned to do the
# hard way.
#
# Wraps the same engine bug tools/verify/verify.sh does: the first headless
# *editor* import of a cold project dies, after .godot/ has been seeded, so the
# second run is clean. Import twice, ignore the first exit code. ADR-0018.
#
# ## The negative controls
#
# `--break` runs six sabotages and **inverts the exit code on each**: a sabotage
# that goes unnoticed is a failure. A gate with nothing proving it can say no is
# a gate nobody has tested, and `shell_probe.gd` shipped six checks that could
# not fail before that was written down.
#
#     corridor   a corridor wider than the road; the line must leave the asphalt
#     unsolved   reading a line that was never solved; it must be empty, not
#                plausible
#     nocourse   build_from_course on an object with no sample(); must refuse
#     noline     the corridor pinned shut; the line must collapse onto the
#                centerline and stop beating it
#     grip       half the grip; every corner and the lap must get slower
#     rollover   no station may sit above its own tipping point, whatever the
#                tire says
#
#     --case=model|valdirone|testtrack|grip|all

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="godot"
FORWARDED=()
BREAK_ALL=0
for argument in "$@"; do
	case "$argument" in
		--break)  BREAK_ALL=1 ;;
		--*)      FORWARDED+=("$argument") ;;
		*)        GODOT="$argument" ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/line.sh /path/to/godot" >&2
	exit 127
fi

echo "==> Importing (cold pass; a crash here is the known bug, see ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true

echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null; then
	echo "error: the warm import failed, which is not the known bug" >&2
	exit 1
fi

if [ "$BREAK_ALL" = "1" ]; then
	echo "==> Negative controls"
	FAILED=0
	for mode in corridor unsolved nocourse noline grip rollover; do
		if ! "$GODOT" --headless --path "$PROJECT_ROOT" \
			--script tools/verify/line_probe.gd -- "--break=$mode"; then
			echo "error: --break=$mode was NOT caught" >&2
			FAILED=$((FAILED + 1))
		fi
	done
	if [ "$FAILED" != "0" ]; then
		echo "==> $FAILED sabotage(s) went unnoticed" >&2
		exit 1
	fi
	echo "==> every sabotage caught"
	exit 0
fi

echo "==> Measuring the racing line"
exec "$GODOT" --headless --path "$PROJECT_ROOT" \
	--script tools/verify/line_probe.gd -- ${FORWARDED[@]+"${FORWARDED[@]}"}
