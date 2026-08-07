#!/usr/bin/env bash
#
# The walk gate: boot to a filed lap time, once, through everything.
#
#     tools/verify/walk.sh [path-to-godot]
#     tools/verify/walk.sh --case=strike,profile
#     tools/verify/walk.sh --patch=none          the unpatched walk, which is
#                                                what a driver gets today
#     tools/verify/walk.sh --break               the negative-control pass
#     tools/verify/walk.sh --break=nolap         one mode
#     tools/verify/walk.sh --trace=600           a trace line every 600 ticks
#
# Every argument that is not a path to Godot is forwarded to walk_probe.gd.
#
# ## What it measures that nothing else does
#
# Every piece of the M5f walk is gated in isolation and the walk itself has never
# run: shell_probe.gd drives the screens against a synthetic ledger, the lap timer
# is gated against a synthetic drive, the ghost against analytic geometry, and the
# shell-to-session hand-off as a set of argument names. This drives the join --
# boot, paddock, setup, a real lap of the real circuit, the pause strike, the
# results sheet, the profile on disk and the ghost beside it -- in one process.
#
# ## --fixed-fps, and it is not optional
#
# Godot's main loop is synchronised to real time, so two timed laps of a 1,375 m
# circuit is two minutes of sitting still. `--fixed-fps 120` matches the project's
# physics rate, decouples the loop from the clock and runs it flat out: measured
# ~2,900 ticks a second headless on this machine, so the 14,476 ticks the walk
# takes are **5.3 s of wall clock**, and the whole gate is the two imports plus
# that. Drop the flag and the same run takes two minutes.
#
# ## What a clean run reports today
#
# **17 of 20, with 3 FAILED**, and all three are product defects this gate found
# rather than flakiness -- the hinted projection, the missing LapLedger, and the
# pause_forgiven flag nothing carries. See the FAIL lines; each names the file.
# The gate goes green the day those are fixed, with no edit here.
#
# ## Where it writes, and what it borrows
#
# Everything the probe owns lives under `user://walk_probe/`, and the circuit is
# driven from a **copy** of the track file under the slug `walkprobe_circuit`,
# because `KartGhost` has no `set_base_dir()` and derives its filename from the
# track -- driving `valdirone_nuova` here would overwrite the runner's own
# personal-best ghost. `--profile-dir=` and `--backdrop=flat` are passed as user
# arguments rather than only injected, because `ShellRoot.return_to_shell()`
# builds the shell the session comes back to with no injection at hand.
#
# The one thing it cannot redirect is `user://settings.cfg`: `circuit.gd`'s pause
# strike builds a bare `KartSettings`. The probe backs the file up byte for byte,
# writes the one field it has to flip, and puts it back. It says so in its report.
#
# ## The negative controls
#
# `--break=<mode>` sabotages one property in-process and asserts the check aimed
# at it went red, with the sabotage's own fingerprint in the measurement. **Its
# exit code is inverted**: 0 means the sabotage was caught. That guard matters
# more here than in shell.sh, because this gate has checks that are red in a clean
# tree on purpose and a mode that accepted any red would report "caught" having
# done nothing. The mode list comes from the probe (`--list-breaks`).

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="godot"
BREAK_ALL=0
FORWARDED=()
for argument in "$@"; do
	case "$argument" in
		--break) BREAK_ALL=1 ;;
		--*)     FORWARDED+=("$argument") ;;
		*)       GODOT="$argument" ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/walk.sh /path/to/godot" >&2
	exit 127
fi

# ADR-0018: the first headless *editor* import of a cold project dies after
# .godot/ has been seeded, so the second run is clean. Import twice, ignore the
# first exit code. It is also what populates the GDScript class cache, without
# which `LapLedger` and `ScreenStack` do not resolve in a --script run.
echo "==> Importing (cold pass; a crash here is the known bug, see ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true

echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null; then
	echo "error: the warm import failed, which is not the known bug" >&2
	exit 1
fi

probe() {
	"$GODOT" --headless --fixed-fps 120 --path "$PROJECT_ROOT" \
		--script tools/verify/walk_probe.gd -- \
		--profile-dir=user://walk_probe/ \
		--backdrop=flat \
		--track=user://walk_probe/walkprobe_circuit.track.json \
		--scatter=false --terrain=false --hud=false --probe=false \
		--auto-shift=true --auto-clutch=true \
		"$@"
}

# --auto-shift/--auto-clutch are named on purpose. AssistSettings skips the stored
# file outright when an assist is named on the command line, so the gate cannot be
# moved by whatever the driver last pressed G on -- an assist state that arrived
# from user://settings.cfg would make this run's lap time a property of somebody's
# saved preference.
#
# --scatter/--terrain/--probe are off because they are 5,187 MultiMesh instances,
# a height field and a reflection probe that nothing headless renders; they cost
# about a second of build and change no measurement here. --mesh is left alone, so
# the run still exercises the same .glb path a driver gets (and still works when
# assets/generated/ is absent, which is a fresh worktree).

echo "==> Walking"
probe ${FORWARDED[@]+"${FORWARDED[@]}"}
STATUS=$?

if [ "$BREAK_ALL" -eq 0 ]; then
	if [ "$STATUS" -eq 0 ]; then
		echo "walk gate: PASSED"
	else
		echo "walk gate: FAILED -- see the FAIL lines above" >&2
	fi
	exit "$STATUS"
fi

echo "==> Negative controls (each sabotage must be caught)"
# Tagged and filtered, because Godot prints its version banner on stdout ahead of
# anything a script says and a bare list is five engine words plus the modes.
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
	echo "walk gate: negative controls $TOTAL of $TOTAL caught"
else
	echo "walk gate: ${#MISSED[@]} of $TOTAL sabotages went through unnoticed --" \
		"${MISSED[*]}" >&2
	STATUS=1
fi

if [ "$STATUS" -eq 0 ]; then
	echo "walk gate: PASSED"
else
	echo "walk gate: FAILED -- see the FAIL lines above" >&2
fi
exit "$STATUS"
