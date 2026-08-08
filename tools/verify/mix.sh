#!/usr/bin/env bash
#
# The mix of a MOVING kart. Issue #242 area 4.
#
#     tools/verify/mix.sh                      # the level tables and the checks
#     tools/verify/mix.sh --case=surface       # a subset; the probe prints which ran
#     tools/verify/mix.sh --wav                # also render the listening WAVs
#     tools/verify/mix.sh --break              # the negative controls
#
# `tools/verify/mix_probe.gd` is the probe and its header says what it asserts and
# what it deliberately does not. This script is the four things a probe cannot do
# for itself: it forces the audio driver, it forces the scripted-input arguments
# that keep the run out of the real `user://settings.cfg`, it picks the output
# directory, and it runs the sabotages with the verdict demanded.
#
# ## Why `--throttle=0 --steer=0 --brake=0` is passed and is not a typo
#
# `AssistSettings` consults the stored `settings.cfg` only when the run is a person
# driving, and any of `--throttle`, `--steer` or `--brake` marks a run as scripted
# and skips the file entirely. Every worktree shares one `user://`, so a probe that
# read the stored auto-shift preference would be a probe whose gear numbers moved
# when somebody pressed G in another session. The probe then installs its own
# `input_driver`, which per ADR-0040 overrides the pushed input, so the zeros never
# reach the solver.
#
# `--grass=false` removes the proving ground's grass run-off. The cornering cells
# drive a circle and the patch is 20 m off the center line; a cell that clipped it
# would measure a surface the row does not name.
#
# ## Why the driver is forced
#
# ADR-0035 measured the Dummy driver reaching `_mix` in irregular bursts on an
# ordinary thread. This probe's entire output is a capture buffer drained on a
# physics tick against that mixer, so a run under Dummy is a sampling problem
# nobody has characterized. The probe refuses rather than warns.
#
# ## The negative controls
#
# Seven, each breaking exactly one link this gate claims to check, and each run
# with the smallest case list that reaches its own check so the suite stays under a
# couple of minutes:
#
#     capture     12 dB of attenuation inserted before the capture in the same bus
#                 chain. This is ADR-0039's own recorded failure -- a level probe
#                 that reports the whole chain minus the one number it exists to
#                 set, silently, with a clean-looking table.
#     solo        nothing is ever muted, so every per-layer row is the whole mix.
#     mute        the floor row leaves the engine up, so the measured leakage floor
#                 is the loudest layer in the mix rather than the mute's own floor.
#     static      the throttle is pinned to zero. The kart never reaches a target
#                 speed and every level in the table describes a stationary kart.
#     level       +20 dB on the kart bus master. Must be caught by headroom and NOT
#                 by the calibration, because the calibration player sits on the
#                 probe bus directly and never sees the kart bus.
#     listener    the AudioListener3D is cleared in the cells that are supposed to
#                 have it pinned -- the pre-#160 configuration exactly.
#     surface     every cell is asphalt whatever it says it is.
#
# Each mode names the check it must turn red and the probe asserts that specific
# one failed. CLAUDE.md records `shell.sh`'s first controls reporting "caught" off a
# pre-existing red they had not caused, so a control that fires for the wrong
# reason is reported as WRONG RED and treated as a failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GODOT="${GODOT:-godot}"
OUT="${MIX_OUT:-$ROOT/shots/mix}"

BREAK=0
WAV=0
FORWARD=()
for argument in "$@"; do
	case "$argument" in
		--break) BREAK=1 ;;
		--wav) WAV=1 ;;
		--out=*) OUT="${argument#--out=}" ;;
		*) FORWARD+=("$argument") ;;
	esac
done

mkdir -p "$OUT"

run_probe() {
	"$GODOT" --headless --audio-driver CoreAudio --path "$ROOT" \
		--script tools/verify/mix_probe.gd -- \
		--throttle=0 --steer=0 --brake=0 --grass=false --out="$OUT" "$@" 2>&1
}

if [ "$BREAK" -eq 0 ]; then
	CASES="level,corner,surface,camera"
	if [ "$WAV" -eq 1 ]; then
		CASES="$CASES,wav"
	fi
	HAS_CASE=0
	for argument in ${FORWARD[@]+"${FORWARD[@]}"}; do
		case "$argument" in --case=*) HAS_CASE=1 ;; esac
	done
	if [ "$HAS_CASE" -eq 1 ]; then
		OUT_TEXT="$(run_probe ${FORWARD[@]+"${FORWARD[@]}"})"
	else
		OUT_TEXT="$(run_probe --case="$CASES" ${FORWARD[@]+"${FORWARD[@]}"})"
	fi
	STATUS=$?
	echo "$OUT_TEXT" | grep -v '^WARNING: .* ObjectDB' | grep -v '^   at: cleanup'
	exit $STATUS
fi

# mode:case:speeds. The speed list is trimmed to whatever the mode's own check
# needs, because a control verified at a value the script does not pass is a
# control that has never been verified -- `replay.sh` shipped one of those and its
# strongest sabotage was inert.
MODES="capture:level:20 solo:level:20 mute:level:20 static:level:20 level:level:30 listener:camera: surface:surface:"

echo
echo "=== negative controls: each sabotage must turn its own check red ==="
echo
FAILED=0
for entry in $MODES; do
	mode="${entry%%:*}"
	rest="${entry#*:}"
	case_list="${rest%%:*}"
	speeds="${rest#*:}"
	ARGS=(--break="$mode" --case="$case_list")
	if [ -n "$speeds" ]; then
		ARGS+=(--speeds="$speeds")
	fi
	TEXT="$(run_probe "${ARGS[@]}")"
	STATUS=$?
	VERDICT="$(echo "$TEXT" | grep -E '^    (CAUGHT|MISSED|WRONG RED)' | head -1)"
	if [ "$STATUS" -eq 0 ] && echo "$VERDICT" | grep -q 'CAUGHT'; then
		printf '    %-10s caught\n' "$mode"
	else
		printf '    %-10s NOT CAUGHT (exit %d)\n' "$mode" "$STATUS"
		echo "$VERDICT" | sed 's/^/        /'
		echo "$TEXT" | grep -E '^    (ok  |FAIL)' | sed 's/^/        /'
		FAILED=1
	fi
done
echo
if [ "$FAILED" -ne 0 ]; then
	echo "    one or more controls did not fire. A check that cannot fail is not a check."
	exit 1
fi
echo "    all $(echo $MODES | wc -w | tr -d ' ') controls fired on their own fingerprint."
exit 0
