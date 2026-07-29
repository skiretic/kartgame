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
# The gate includes a negative control. data/tracks/self_intersecting.track.json
# is a circuit that closes, turns +360 degrees, is 1,105 m long and crosses
# itself; if it LOADS, this script fails. A validator with nothing proving it can
# say no is a validator nobody has tested.

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
