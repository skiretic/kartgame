#!/usr/bin/env bash
#
# The M5 gate: a circuit authored in track.json loads, measures what it claims,
# and agrees with the mesh built from the same file.
#
#     tools/verify/circuit.sh [path-to-godot]
#     tools/verify/circuit.sh --case=schema
#     tools/verify/circuit.sh --track=res://data/tracks/other.track.json
#
# Every argument that is not a path to Godot is forwarded to circuit_probe.gd.
#
# Wraps the same engine bug tools/verify/verify.sh does: the first headless
# *editor* import of a cold project dies, after .godot/ has been seeded, so the
# second run is clean. Import twice, ignore the first exit code. ADR-0018.
#
# The gate includes three negative controls. data/tracks/self_intersecting.track.json
# is a circuit that closes, turns +360 degrees, is 1,105 m long and crosses
# itself; if it LOADS, this script fails. --case=pit builds two more at run time
# out of one-field edits of the real circuit -- a merge angle over Part I art
# 7.2's 30 degree cap, and a reverse pit stub moved onto the far edge -- and both
# must be refused for the thing they break. A validator with nothing proving it
# can say no is a validator nobody has tested.
#
# --case=agree carries two more of its own, aimed at the barrier checks issue #244
# added. They are run with --break=<mode> and the probe INVERTS its exit code under
# it, so a break run that passes is the failure -- it means the check it targets
# cannot fail. Catching it is not sufficient either: the verdict demands that the
# measurement moved to the value that particular sabotage predicts, because a check
# reporting "caught" off a pre-existing red it did not cause is not a check.
#
#     --case=schema|measure|agree|layouts|timing|pit|place|all
#     --break=barrier-agree   moves the mesh's barrier rows 5 mm and leaves the
#                             collider alone: a one-consumer regression
#     --break=barrier-seat    lifts every barrier foot 1.5 m, which is through the
#                             0.60 m of skirt margin Valdirone actually has

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="godot"
FORWARDED=()
for argument in "$@"; do
	case "$argument" in
		--*) FORWARDED+=("$argument") ;;
		*)   GODOT="$argument" ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/circuit.sh /path/to/godot" >&2
	exit 127
fi

if [ ! -f "$PROJECT_ROOT/data/tracks/valdirone_nuova.manifest.json" ]; then
	echo "note: no track manifest; generating the geometry stages (no Blender needed)"
	python3 "$PROJECT_ROOT/tools/blender/gentrack.py" \
		--stages=geometry,uv,manifest --quiet || exit 1
fi

echo "==> Importing (cold pass; a crash here is the known bug, see ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true

echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null; then
	echo "error: the warm import failed, which is not the known bug" >&2
	exit 1
fi

echo "==> Verifying the circuit"
exec "$GODOT" --headless --path "$PROJECT_ROOT" \
	--script tools/verify/circuit_probe.gd -- ${FORWARDED[@]+"${FORWARDED[@]}"}
