#!/usr/bin/env bash
#
# What has been tuned away from its sourced default, and by how much.
#
#     tools/verify/tuning.sh                        audit the defaults
#     tools/verify/tuning.sh --preset=path/to.tuning  audit one preset
#
# `.tuning`, not `.tune`. This line and CLAUDE.md's command table both said
# `.tune` for a milestone while `tuning_panel.gd:71` has always written
# `PRESET_EXTENSION := ".tuning"`. Harmless so far, because `--preset=` takes any
# path -- but a preset picker built off the documented spelling would list an
# empty directory and report it as "no presets saved".
#     tools/verify/tuning.sh --check                the gate
#
# `ARCHITECTURE.md` §19 names unbounded vehicle tuning as the live risk, and
# ADR-0033 refused to retune M3a's constants rather than restore a figure by
# moving one. This command is what makes tuning auditable instead of unbounded:
# it prints how many tunables have moved, how many of those overrode a **defended**
# default — a citation, not a guess — and the citation each one is overriding.
# The defended count leads whenever it is not zero, because a session that turned
# four guesses is within the rules and a session that moved one published
# measurement is a different kind of event.
#
# With no `--preset` the answer is "nothing has been tuned", and the run is worth
# making anyway: it is the machine agreeing with itself, and `--check`'s first
# assertion is the same statement in a form that returns an exit code.
#
# ## --check is the gate; the audit is not
#
# The audit reports and exits 0 however alarming its contents are — an overridden
# default is a decision somebody is entitled to make and record, not a build
# failure. `--check` is the gate: fifteen properties of the preset format, each
# one something the audit would report *wrongly rather than loudly* if it did not
# hold. It exits non-zero on the first failure it finds and names it.
#
# ## Why this is cheap enough to run before every commit
#
# `src/tuning/tuning_registry.h` promises that the audit and the file format work
# with an empty vehicle path, and `tuning_probe.gd` takes it at its word: no
# scene, no ground plane, no kart, and not one physics tick. What remains is the
# import, and the double import is the ADR-0018 workaround — see
# tools/verify/verify.sh. The probe touches no asset, but a cold `.godot/` still
# has to be seeded and the probe's own `.uid` still has to be written.
#
# ## The one thing this wrapper insists on
#
# It requires the probe's conclusion line before it will call anything a pass.
# `audio_probe.sh` learned that the hard way: a probe that dies part way through
# still prints cleanly and still looks like a report. It is load-bearing here for
# a second reason — `KartTuning` is a static type inside the probe, so a build
# without it registered fails at *parse* time, before the probe's own guard can
# say so, and a missing conclusion line is the only symptom this side of the wire.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
MODE=audit
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*)   GODOT="${argument#*=}" ;;
		--check)     MODE=check; FORWARDED+=("--check") ;;
		--preset=*)
			# Absolutized here rather than in the probe. Godot resolves a path with
			# no `res://` or `user://` prefix against the process's working
			# directory, and `--path` moves the *project* root without moving that,
			# so a relative `--preset=` read from a shell in some other directory
			# would open a different file or none at all. A `res://` or `user://`
			# path is passed through untouched, because those are already
			# unambiguous and re-rooting them would break them.
			PRESET="${argument#*=}"
			case "$PRESET" in
				res://*|user://*|/*) ;;
				*) PRESET="$PWD/$PRESET" ;;
			esac
			FORWARDED+=("--preset=$PRESET")
			;;
		*)           FORWARDED+=("$argument") ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/tuning.sh --godot=/path/to/godot" >&2
	exit 127
fi

"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

OUTPUT="$("$GODOT" --headless --path "$PROJECT_ROOT" \
	--script tools/verify/tuning_probe.gd -- \
	${FORWARDED[@]+"${FORWARDED[@]}"} 2>&1)"
STATUS=$?

if [ "$MODE" = "check" ]; then
	# The check lines and the conclusion. Godot writes its own startup chatter to
	# the same stream, so the report is selected rather than echoed whole.
	echo "$OUTPUT" | grep -E '^(===|    |check |checks )'

	SUMMARY="$(echo "$OUTPUT" | awk '/^checks / {print}')"
	if [ -z "$SUMMARY" ]; then
		echo "error: the gate did not reach its conclusion. Either the extension is" >&2
		echo "       not built with KartTuning registered — scons target=editor" >&2
		echo "       arch=arm64 — or the probe died part way through:" >&2
		echo "$OUTPUT" | tail -20 >&2
		exit 1
	fi

	# "checks 15 passed 0 failed"
	FAILED="$(echo "$SUMMARY" | awk '{print $4}')"
	if [ "$FAILED" != "0" ] || [ "$STATUS" != "0" ]; then
		echo "error: $SUMMARY (probe exited $STATUS)" >&2
		echo "       Each FAIL line above names the property that did not hold. The" >&2
		echo "       reason each one is held is in tools/verify/tuning_probe.gd, on" >&2
		echo "       the check's own doc comment." >&2
		exit 1
	fi
	echo "$SUMMARY"
	exit 0
fi

# The audit. Everything the probe prints is meant to be read, so the only
# filtering is Godot's own startup lines.
echo "$OUTPUT" | sed -n '/^=== tuning audit/,$p'

# The probe's own status is checked before the conclusion line, and the order
# matters. A preset that failed to open is reported by the probe, in the report
# above, with the error it got — and it deliberately does NOT print the hash
# footer, because a footer describing the defaults under a heading naming a file
# that never loaded is the `SKIP_IMPORT=1` trap: a true-looking report about the
# wrong thing. Checking the footer first would bury that diagnosis under a
# generic "did not reach its conclusion".
if [ "$STATUS" != "0" ]; then
	exit "$STATUS"
fi

if ! echo "$OUTPUT" | grep -q '^default-hash '; then
	echo "error: the audit did not reach its conclusion:" >&2
	echo "$OUTPUT" | tail -20 >&2
	exit 1
fi

# An audit that merely found overridden defaults exits 0. That is the point of
# the distinction: an override is a decision somebody is entitled to make, and
# this command exists to record it rather than to forbid it. `--check` is the
# gate.
exit 0
