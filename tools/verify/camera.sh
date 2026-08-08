#!/usr/bin/env bash
#
# The camera gate. Issue #242 area 3.
#
#     tools/verify/camera.sh                    every case, on the proving ground
#     tools/verify/camera.sh --case=fov,rig     a subset; the probe prints which ran
#     tools/verify/camera.sh --scene=res://scenes/game/valdirone.tscn
#     tools/verify/camera.sh --break            the six negative controls
#
# Nothing in this project has ever measured a camera. Every still is taken with
# --eye/--look, which builds a *fourth* camera and never touches the three driving
# rigs — which is how the chase rig shipped for a milestone pointing the wrong way
# and how --fov was inert for a milestone (#237).
#
# --break runs the six sabotages and **inverts the exit code on each**: a control
# that does not go red is a check that cannot fail, and six of those shipped in one
# file here once. Each verdict demands its own saboteur's fingerprint rather than
# accepting a pre-existing red.
#
# Headless, so the ADR-0018 rendering crash does not apply — but the import does,
# which is why this imports twice like every other gate.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
BREAK=0
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*) GODOT="${argument#*=}" ;;
		--break)   BREAK=1 ;;
		*)         FORWARDED+=("$argument") ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	exit 127
fi

"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

run_probe() {
	"$GODOT" --headless --path "$PROJECT_ROOT" \
		--script tools/verify/camera_probe.gd -- \
		${FORWARDED[@]+"${FORWARDED[@]}"} "$@" 2>&1
}

if [ "$BREAK" = "1" ]; then
	FAILED=0
	for mode in fov attributes lag roll eye cull; do
		OUTPUT="$(run_probe "--break=$mode")"
		STATUS=$?
		VERDICT="$(echo "$OUTPUT" | grep '^negative control' || echo "no verdict printed")"
		if [ "$STATUS" = "0" ]; then
			echo "  ok   --break=$mode   $VERDICT"
		else
			echo "  FAIL --break=$mode   $VERDICT"
			echo "$OUTPUT" | tail -20
			FAILED=1
		fi
	done
	if [ "$FAILED" != "0" ]; then
		echo "camera.sh: a negative control did not fire — a check that cannot fail is not a check"
		exit 1
	fi
	echo "camera.sh: 6/6 negative controls caught"
	exit 0
fi

OUTPUT="$(run_probe)"
STATUS=$?
echo "$OUTPUT"
exit $STATUS
