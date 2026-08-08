#!/usr/bin/env bash
#
# Issue #241: does a surface under a wheel cost anything, and does a barrier stop
# the kart?
#
#     tools/verify/surface.sh [path-to-godot]
#     tools/verify/surface.sh --case=drive
#     tools/verify/surface.sh --break          the negative controls
#
# The report is `tools/verify/surface_probe.gd`'s header. In one paragraph: the
# surface path was suspected of being a capability built at both ends and never
# joined in the middle -- `surface.h` has a sourced grip table, `circuit.gd` sets
# the metadata, `kart_body.cpp` reads it, `tire.h` multiplies by it -- so this
# walks the chain one hop at a time and puts a number on each. It is not a
# reimplementation of any of them.
#
# ## THIS GATE EXITS 1 ON PURPOSE, and read the table rather than the exit code
#
# Exactly one check is red, in the same shape as `drive.sh`'s standing failure:
#
#     every barrier's head clears the ground around it
#       103 quads have their head below the ground beside them.
#
# **This is a `TrackTerrain` defect, not a barrier defect.** The height field's
# nearest-station flip builds a ridge through the barrier line where the lap
# passes close to itself at two heights -- `track_terrain.gd` documents its own
# 2.343 m irreducible pinned-to-pinned step -- and no closed-form wall height
# reaches over it. 25 of the first 26 measured have the ground falling away again
# outboard, so it is a ridge a kart can climb and drop off the far side of.
#
# ## The red this REPLACES, so a passing check is not read as a regression
#
# This gate used to report `every barrier stands on the ground it is built over`
# as its standing failure -- 282 of 681 quads floating, worst 3.184 m. That was
# issue #244 and it is **FIXED**: `KartTrack::surface_meshes` and
# `tools/blender/tracklib/surfaces.py` now seat the barrier foot on
# `elevation(station) - SHOULDER_DROP`, which is `TrackTerrain`'s own definition
# of the ground inside the corridor and carries no crown or bank term, so the two
# consumers agree by construction. 0 of 1,362 foot vertices stand proud; the worst
# now sits 0.6045 m below the terrain. `circuit.sh --case=agree` measures both
# halves and carries `--break=barrier-agree` and `--break=barrier-seat`.
#
# The head count moved 62 -> 103 when Valdirone's run-off was re-authored to mixed
# gravel/grass (ADR-0073): T4's run-off was capped 26.0 -> 17.0 m, which moves that
# barrier 9 m inboard into a slope, and the larger gravel beds repin the height
# field. The re-author did not cause the defect; it exposed more of it.
#
# Everything else is green, and the headline is that the chain **works**: the grip
# column reaches the tire and costs 70.3 m of stopping distance against asphalt's
# 14.1 from 20 m/s, and a barrier strike at 25 m/s stops the kart dead with the
# collision reaching the solver's own read-out.
#
# ## The negative controls
#
# `--break` runs three sabotages and every one must be caught. The exit code is
# INVERTED for that mode: a sabotage that goes unnoticed is the failure. Each is
# verified at the value this script actually passes, per the CLAUDE.md entry about
# `replay.sh` shipping a default that made its strongest control inert.
#
#     --break=meta      strips `surface_type` off Verge and Gravel
#     --break=grip      gives every drive lane the asphalt metadata
#     --break=barrier   deletes the Barriers body before the strike
#
# ## What it needs
#
# No generated asset. `--case=geometry` and `--case=drive` build their own
# fixtures; `--case=hop` and `--case=barrier` load `valdirone.tscn` with
# `--mesh=false --scatter=false`, so the circuit is its own colliders and the
# missing `.glb` files cost nothing. It writes nothing under `user://` -- every
# worktree shares one -- and passes a private `--profile-dir` anyway.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="godot"
WANT_BREAK=0
CASES=""
for argument in "$@"; do
	case "$argument" in
		--break)  WANT_BREAK=1 ;;
		--case=*) CASES="${argument#--case=}" ;;
		--*)      ;;
		*)        GODOT="$argument" ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/surface.sh /path/to/godot" >&2
	exit 127
fi

# The circuit cases load a scene, so the project has to be imported. The first
# headless *editor* import of a cold project dies after seeding .godot/ -- ADR-0018,
# upstream issue #2 -- so import twice and ignore the first exit code.
echo "==> Importing (cold pass; a crash here is the known bug, ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null; then
	echo "error: the warm import failed, which is not the known bug" >&2
	exit 1
fi

PROBE="tools/verify/surface_probe.gd"
# A private base dir under the shared `user://`, named after the ticket so it
# cannot collide with a real career or with another worktree's probe.
SCENE_ARGS=(--scatter=false --mesh=false --profile-dir=user://surface241/)

run_case() {
	local name="$1"
	shift
	echo
	echo "==> --case=$name"
	"$GODOT" --headless --path "$PROJECT_ROOT" --script "$PROBE" -- --case="$name" "$@"
}

FAILED=0

if [ "$WANT_BREAK" -eq 1 ]; then
	echo "==> negative controls (exit code INVERTED: a sabotage that is missed fails)"
	CAUGHT=0
	TOTAL=0

	# The pairing is not free choice: each sabotage has to be run against the case
	# that can see it. Stripping the metadata is invisible to the flat fixture,
	# which builds its own ground, and deleting the barrier is invisible to
	# everything but the strike.
	for pair in "meta:hop" "grip:drive" "barrier:barrier"; do
		mode="${pair%%:*}"
		which_case="${pair##*:}"
		TOTAL=$((TOTAL + 1))
		echo
		echo "==> --break=$mode against --case=$which_case"
		# `set -u` plus bash 3.2 on macOS: an empty array expands to an unbound
		# variable, so every expansion below is guarded rather than bare.
		extra=()
		if [ "$which_case" != "drive" ]; then
			extra=("${SCENE_ARGS[@]}")
		fi
		if "$GODOT" --headless --path "$PROJECT_ROOT" --script "$PROBE" -- \
				--case="$which_case" --break="$mode" ${extra[@]+"${extra[@]}"}; then
			CAUGHT=$((CAUGHT + 1))
		else
			echo "!! --break=$mode was NOT caught"
			FAILED=1
		fi
	done
	echo
	echo "negative controls: $CAUGHT/$TOTAL caught"
	exit "$FAILED"
fi

for name in geometry drive hop barrier audio; do
	if [ -n "$CASES" ] && [[ ",$CASES," != *",$name,"* ]]; then
		continue
	fi
	extra=()
	if [ "$name" = "hop" ] || [ "$name" = "barrier" ]; then
		extra=("${SCENE_ARGS[@]}")
	fi
	if ! run_case "$name" ${extra[@]+"${extra[@]}"}; then
		FAILED=1
	fi
done

echo
if [ "$FAILED" -eq 0 ]; then
	echo "surface: all cases green"
else
	echo "surface: at least one case is red. The KNOWN one is the terrain ridge"
	echo "described in this script's header -- 103 barrier quads with their head"
	echo "under a ridge the height field builds. The barrier SEATING defect this"
	echo "line used to name is fixed (#244). Anything else is new."
fi
exit "$FAILED"
