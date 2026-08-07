#!/usr/bin/env bash
#
# The M7 AI gate: can it drive Valdirone, and does it drive it through the
# player's own input path?
#
#     tools/verify/ai.sh [path-to-godot]
#     tools/verify/ai.sh --case=lap
#     tools/verify/ai.sh --break            # every negative control
#     tools/verify/ai.sh --break=steer      # one of them
#
# Every argument that is not a path to Godot is forwarded to ai_probe.gd.
#
# **Needs the generated assets.** Unlike line.sh and shell.sh this one drives
# `scenes/game/valdirone.tscn`, which loads `assets/generated/kart.glb` — the
# whole point is that the AI is timed by the real `SessionRunner` on the real
# circuit rather than by a harness written to agree with it. A fresh worktree
# must symlink `assets/generated` from the main checkout first.
#
# ## --fixed-fps, and it is not optional
#
# Godot's main loop is synchronised to real time, so a timed lap of a 1,375 m
# circuit is forty seconds of sitting still — measured 121.6 ticks a second
# headless, which is exactly real time. `--fixed-fps 120` matches the project's
# physics rate, decouples the loop from the clock and runs it flat out.
# `walk.sh` measured ~2,900 ticks a second that way on this machine, so the nine
# laps this gate drives are seconds rather than six minutes. Drop the flag and
# the same run is a coffee break.
#
# Wraps the same engine bug tools/verify/verify.sh does: the first headless
# *editor* import of a cold project dies, after .godot/ has been seeded, so the
# second run is clean. Import twice, ignore the first exit code. ADR-0018.
#
# ## What it measures, and what it deliberately does not
#
# Nothing here asserts a lap time, a corner speed or a number of g. Issue #137 is
# open and the tire model is expected to move; a gate written against an absolute
# would go red on the day the kart got better, failing in a way nobody could tell
# apart from a regression. Every ceiling is read out of `KartRacingLine.model()`
# at run time and every gate is a relation against it. Lap times are **printed**
# in quantity, because "how fast" is the question the next session asks — printed
# is not gated.
#
# ## What is out of scope, and is a ticket rather than a stub
#
# No overtaking, no kart-to-kart contact, no standings. `GAMEDESIGN.md` §13: a
# stubbed mode reads worse than an absent one. What ships is one AI that laps the
# circuit cleanly and quickly through the player's own `DriverInput` path, and a
# grid of them that does the same beside a human — `--ai=N` on the scene.
#
# ## The negative controls
#
# `--break` runs six sabotages and **inverts the exit code on each**: a sabotage
# that goes unnoticed is a failure. Each one also has to be caught by a *named*
# check — see `BREAK_FINGERPRINT` in the probe — because a control satisfied by
# any red at all is satisfied by a red it did not cause, which is how
# `shell_probe.gd`'s first cut reported "caught" on an already-broken gate.
#
#     steer      the steering answer negated; it must leave its own line
#     noline     no racing line; it must push neutral and file no lap
#     nocourse   nothing to project against; same requirement, other half
#     brake      planning against four times the model's brake ceiling; it must
#                arrive at a corner unable to turn
#     lookahead  a lookahead of centimeters; pure pursuit must chatter off line
#     ceiling    the run-time lateral ceiling quartered; the ellipse check must
#                go red, which proves the check divides by the number it prints
#     hint       the carried station hint offset half a lap; the hinted walk must
#                stop agreeing with the unhinted one
#
#     --case=wiring|limits|hint|lap|reverse|tiers|all

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
	echo "Pass an explicit path: tools/verify/ai.sh /path/to/godot" >&2
	exit 127
fi

if [ ! -f "$PROJECT_ROOT/assets/generated/kart.glb" ]; then
	echo "error: no assets/generated/kart.glb" >&2
	echo "This gate drives the real circuit scene. Run tools/blender/genkart.sh," >&2
	echo "or symlink assets/generated from the main checkout in a worktree." >&2
	exit 1
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
	# `ceiling` is run with `--case=all` deliberately: its sabotage is applied in
	# `_case_limits()`, so a subset run would leave nothing to quarter and the
	# control would be inert while still printing a confident number. That is the
	# `replay.sh --break=input` defect — a control whose default value cannot fire
	# — and the probe refuses it by name rather than passing quietly.
	for mode in steer noline nocourse brake lookahead ceiling hint; do
		if ! "$GODOT" --headless --fixed-fps 120 --path "$PROJECT_ROOT" \
			--script tools/verify/ai_probe.gd -- "--break=$mode"; then
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

echo "==> Driving the circuit"
exec "$GODOT" --headless --fixed-fps 120 --path "$PROJECT_ROOT" \
	--script tools/verify/ai_probe.gd -- ${FORWARDED[@]+"${FORWARDED[@]}"}
