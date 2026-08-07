#!/usr/bin/env bash
#
# The M6 gate: a recorded lap re-sims to an identical state hash.
#
#     tools/verify/replay.sh                    # record, play back, compare
#     tools/verify/replay.sh --break            # the negative controls
#     tools/verify/replay.sh --ticks=6000       # a longer run
#     tools/verify/replay.sh --scene=res://scenes/game/proving_ground.tscn
#
# ROADMAP M6's acceptance is two sentences and this script is both of them:
#
#   1. "A recorded lap re-sims to an identical state hash." The record pass drives
#      a scripted lap, snaps every tick of input onto the storage grid, writes the
#      stream and a per-tick state hash to a file, and prints the last hash. The
#      play pass loads that file **in a second process**, re-simulates from the
#      decoded input, and compares every checkpoint.
#   2. "The harness fails loudly when determinism is deliberately broken." That is
#      `--break`, and its **exit code is INVERTED**: each sabotage must be caught,
#      and a sabotage that is not caught fails the gate.
#
# ## Two processes, not two runs in one
#
# The same argument `drive.sh` makes. A second run inside one process shares
# allocator state, RID numbering and every cache Godot has warmed up, so it can
# agree for reasons that have nothing to do with the simulation being
# deterministic. Here it matters more than it does there: the file is the subject
# of the test, and a round trip that never left the process would not have
# exercised the encoder, the writer, the reader or the decoder.
#
# ## What a red means
#
# **A refusal is not a divergence and the difference is the whole point.** ADR-0041
# is explicit that CI must treat them as separate failures: a refusal says the
# replay could not answer the question — the configuration moved, the file is
# truncated, the format is from another build — and a divergence says it answered
# and the answer is that the simulation is not deterministic. Folding them together
# turns every stale recording into a determinism alarm, and the alarms then get
# ignored. This script prints which one it got.
#
# The double import is the ADR-0018 workaround; see tools/verify/verify.sh.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
BREAK=0
TICKS=3000
INTERVAL=1
SCENE=""
SCENARIO=""
# Where the sabotage lands, and it is not a free choice.
#
# `--break=input` moves the throttle by **one quantization code**, 1.53e-5, which is
# the smallest change the format can express -- that is the point of it. Whether
# that grows into a state-hash divergence depends on what the kart is doing at the
# time, because the hash quantizes to 1e-4 and a coasting kart absorbs a throttle
# nudge without ever crossing a grid line.
#
# Measured on the `mixed` scenario: at tick 400 the kart is accelerating and the
# perturbation diverges at tick **426**, 26 ticks later. At tick 900 it never
# diverges at all over the remaining 2,100 ticks, and the negative control reports
# ESCAPED -- correctly, since nothing was caught. This script shipped with 900 as
# its default, which made the strongest of the five controls unable to fire.
#
# `--break=state` (a 1 mm/s nudge) diverges at the very next checkpoint from either
# tick, so it is not sensitive to this and is the control to trust if the scenario
# is ever rewritten.
BREAK_TICK=400
CASES=()
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*)      GODOT="${argument#*=}" ;;
		--break)        BREAK=1 ;;
		--ticks=*)      TICKS="${argument#*=}" ;;
		--interval=*)   INTERVAL="${argument#*=}" ;;
		--scene=*)      SCENE="${argument#*=}" ;;
		--scenario=*)   SCENARIO="${argument#*=}" ;;
		--break-tick=*) BREAK_TICK="${argument#*=}" ;;
		--case=*)       IFS=',' read -ra CASES <<< "${argument#*=}" ;;
		*)              FORWARDED+=("$argument") ;;
	esac
done

# The recording lives in the probe's own private directory under `user://`.
# **Every worktree shares one `user://`** — it is keyed on
# `application/config/name` — so the name has to be one that cannot collide with
# a real career, and the probe scrubs it at the end rather than leaving it in the
# main checkout's directory.
REPLAY="user://replay_probe/gate.replay"

COMMON=("--ticks=$TICKS" "--interval=$INTERVAL")
[ -n "$SCENE" ] && COMMON+=("--scene=$SCENE")
[ -n "$SCENARIO" ] && COMMON+=("--scenario=$SCENARIO")

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	exit 127
fi

"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

probe() {
	"$GODOT" --headless --path "$PROJECT_ROOT" \
		--script tools/verify/replay_probe.gd -- "$@" 2>&1
}

scrub() {
	probe --mode=scrub >/dev/null 2>&1 || true
}

# Only the probe's own lines. The scenes print a page of setup first.
filter() {
	grep -E '^(---|    |break |state-hash|FAIL)'
}

FAILED=0

# --- the gate ---------------------------------------------------------------

if [ "$BREAK" = "0" ]; then
	echo "== record"
	RECORD="$(probe --mode=record "--out=$REPLAY" "${COMMON[@]}" \
		${FORWARDED[@]+"${FORWARDED[@]}"})"
	echo "$RECORD" | filter
	HASH_A="$(echo "$RECORD" | awk '/^state-hash/ {print $2}')"

	if [ -z "$HASH_A" ]; then
		echo "error: the record pass produced no hash" >&2
		scrub
		exit 1
	fi

	echo
	echo "== play back, in a second process"
	PLAY="$(probe --mode=play "--in=$REPLAY" "${COMMON[@]}" \
		${FORWARDED[@]+"${FORWARDED[@]}"})"
	echo "$PLAY" | filter
	HASH_B="$(echo "$PLAY" | awk '/^state-hash/ {print $2}')"
	VERDICT="$(echo "$PLAY" | awk '/^    verdict/ {print $2}')"

	echo
	case "$VERDICT" in
		passed)
			if [ "$HASH_A" = "$HASH_B" ]; then
				echo "    M6              a recorded lap re-sims to an identical state hash"
				echo "                    $HASH_A over $TICKS ticks, two processes"
			else
				# Belt and braces: the verdict is the footer comparison and this is
				# the final hash. They cannot disagree, and if they ever do it is
				# this harness that is wrong rather than the solver.
				echo "    HARNESS BUG     verdict passed but the final hashes differ:" >&2
				echo "                    $HASH_A vs $HASH_B" >&2
				FAILED=1
			fi
			;;
		diverged)
			echo "    DIVERGED        the configuration matched and the state hash did not." >&2
			echo "                    This is a determinism bug, not a stale replay." >&2
			echo "                    Look for a wall-clock read in the solver, an unseeded" >&2
			echo "                    generator, or iteration over a Dictionary." >&2
			FAILED=1
			;;
		refused)
			# **Its own failure, deliberately not folded in with a divergence.**
			echo "    REFUSED         the replay could not answer the question." >&2
			echo "                    That is not a determinism result: nothing was" >&2
			echo "                    re-simulated. Read the sentence above it." >&2
			FAILED=1
			;;
		*)
			echo "    NO VERDICT      the play pass did not finish" >&2
			FAILED=1
			;;
	esac
	scrub
	if [ "$FAILED" = "1" ]; then
		exit 1
	fi
	exit 0
fi

# --- the negative controls ---------------------------------------------------
#
# Five sabotages, and each one has to be caught for its own reason rather than by
# any red that happens to be lying around. The probe checks the saboteur's own
# fingerprint: `config` must be refused *naming tick_hz*, `truncate` must be
# refused *for a truncated body*, and the two that corrupt the simulation must
# DIVERGE rather than refuse — a refusal there would mean the run never happened.
#
# `grid` is the odd one out: it runs in the *record* pass, because the thing being
# proved is that `KartReplay.record_input` refuses input that is not already on
# the storage grid. That refusal is the structural half of ADR-0041 — the recorder
# cannot quietly introduce the quantize-on-write defect, because the only function
# that can write a body byte will not do it.

ALL_CASES=(input state config truncate grid)
if [ ${#CASES[@]} -eq 0 ]; then
	CASES=("${ALL_CASES[@]}")
fi

echo "== recording the subject of the sabotages"
RECORD="$(probe --mode=record "--out=$REPLAY" "${COMMON[@]}" --quiet=true)"
if ! echo "$RECORD" | grep -q '^state-hash'; then
	echo "$RECORD" | tail -20 >&2
	echo "error: could not record a replay to sabotage" >&2
	scrub
	exit 1
fi
echo "$RECORD" | grep -E '^    (recorded|checkpoints|file|config)'
echo

CAUGHT=0
ESCAPED=0
for case_name in "${CASES[@]}"; do
	case "$case_name" in
		grid)
			# Its own recording, to its own path, so it cannot disturb the one the
			# other four are replaying.
			OUT="$(probe --mode=record --out=user://replay_probe/grid.replay \
				"${COMMON[@]}" --break=grid "--break-tick=$BREAK_TICK" --quiet=true)"
			;;
		*)
			OUT="$(probe --mode=play "--in=$REPLAY" "${COMMON[@]}" \
				"--break=$case_name" "--break-tick=$BREAK_TICK" --quiet=true)"
			;;
	esac
	STATUS=$?
	echo "$OUT" | grep -E '^(break |    note|    verdict|    FIRST|      recorded|      live)'
	if [ "$STATUS" = "0" ] && echo "$OUT" | grep -q 'CAUGHT'; then
		CAUGHT=$((CAUGHT + 1))
	else
		ESCAPED=$((ESCAPED + 1))
		echo "    ESCAPED         --break=$case_name was not caught" >&2
	fi
	echo
done

scrub

echo "    negative controls   $CAUGHT caught, $ESCAPED escaped, of ${#CASES[@]}"
if [ "$ESCAPED" != "0" ]; then
	echo "error: a deliberate break was not caught, so the gate cannot fail and" >&2
	echo "       therefore is not a gate." >&2
	exit 1
fi
exit 0
