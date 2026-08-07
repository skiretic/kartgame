#!/usr/bin/env bash
#
# The M8 gate for the shift, clutch and rolling layers. Issues #83 and #85.
#
#     tools/verify/audio.sh                    # the checks
#     tools/verify/audio.sh --case=join        # a subset; the probe prints which ran
#     tools/verify/audio.sh --break            # the negative controls
#
# `tools/verify/shift_probe.gd` is the probe and its header says what it asserts
# and what it deliberately does not. This script is the two things a probe cannot
# do for itself: it forces the audio driver, and it runs the sabotages with the
# exit code INVERTED so that a control which fails to fire is a failure.
#
# ## Why the driver is forced
#
# ADR-0035 measured the Dummy driver reaching `_mix` in bursts on an ordinary
# thread, so a cost figure taken there is wrong in scale and in shape. The probe
# refuses to report one and this script makes sure it never has to -- the same
# arrangement `scrub_cost_probe.gd` has, and the reason both refuse rather than
# warn is that a plausible wrong number is worse than no number.
#
# ## The negative controls
#
# Four, each breaking exactly one link this gate claims to check:
#
#     unwired            the body stops publishing to both new layers. This is the
#                        defect the whole probe exists for -- a capability built at
#                        both ends and not joined in the middle -- and it is the
#                        one that has actually shipped in this project four times.
#     silent             both layers at zero gain. Must be caught by the LEVEL
#                        check and NOT by the join check, because a clack still
#                        fires when it is inaudible. That the two checks fail
#                        independently is what makes having both worth anything.
#     wrong_layer        the rolling stream mounted as a scrub layer. The in-range
#                        version of the mistake, which is the dangerous one: it
#                        renders a real, plausible noise layer that ignores the
#                        surface completely.
#     stale_shift_time   the clack duration left on the synth's compiled default
#                        while the solver's moved. The join that would rot
#                        silently, because the two numbers are equal today.
#
# **Each mode names the check it must turn red and the probe asserts that specific
# one failed.** CLAUDE.md records the first cut of `shell.sh`'s controls reporting
# "caught" off a pre-existing red it had not caused; a control that fires for the
# wrong reason is reported as WRONG RED and treated as a failure.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GODOT="${GODOT:-godot}"
MODES="unwired silent wrong_layer stale_shift_time"

BREAK=0
FORWARD=()
for argument in "$@"; do
	case "$argument" in
		--break) BREAK=1 ;;
		*) FORWARD+=("$argument") ;;
	esac
done

run_probe() {
	"$GODOT" --headless --audio-driver CoreAudio --path "$ROOT" \
		--script tools/verify/shift_probe.gd -- "$@" 2>&1
}

if [ "$BREAK" -eq 0 ]; then
	OUT="$(run_probe "${FORWARD[@]+"${FORWARD[@]}"}")"
	STATUS=$?
	echo "$OUT" | grep -v '^WARNING: .* ObjectDB' | grep -v '^   at: cleanup'
	exit $STATUS
fi

echo
echo "=== negative controls: each sabotage must be caught, exit code inverted ==="
echo
FAILED=0
for mode in $MODES; do
	OUT="$(run_probe --break="$mode" "${FORWARD[@]+"${FORWARD[@]}"}")"
	STATUS=$?
	VERDICT="$(echo "$OUT" | grep -E '^    (CAUGHT|MISSED|WRONG RED)' | head -1)"
	if [ "$STATUS" -eq 0 ] && echo "$VERDICT" | grep -q 'CAUGHT'; then
		printf '    %-18s caught\n' "$mode"
	else
		printf '    %-18s NOT CAUGHT (exit %d)\n' "$mode" "$STATUS"
		echo "$VERDICT" | sed 's/^/        /'
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
