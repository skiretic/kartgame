#!/usr/bin/env bash
#
# Run the M3a driving scenarios and check that they are reproducible.
#
#     tools/verify/drive.sh                       # all four scenarios, twice each
#     tools/verify/drive.sh --scenario=skidpad    # one of them
#     tools/verify/drive.sh --once                # skip the determinism half
#
# Each scenario is run twice in two separate processes and the two state hashes
# are compared. That is ARCHITECTURE.md §8 item 6 at the smallest scale it can be
# tested: the vehicle is not the C++ solver yet, but the harness that will judge
# the C++ solver is, and it is much cheaper to have it working before there is a
# solver to blame.
#
# Two processes rather than two runs inside one, deliberately. A second run in
# the same process shares allocator state, RID numbering, and every cache Godot
# has warmed up, so it can agree for reasons that have nothing to do with the
# simulation being deterministic.
#
# The double import is the ADR-0018 workaround; see tools/verify/verify.sh.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
ONCE=0
SCENARIOS=()
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*)     GODOT="${argument#*=}" ;;
		--once)        ONCE=1 ;;
		--scenario=*)  SCENARIOS+=("${argument#*=}") ;;
		*)             FORWARDED+=("$argument") ;;
	esac
done

if [ ${#SCENARIOS[@]} -eq 0 ]; then
	# Both skidpad directions, because the kart is not laterally symmetric: 27 kg
	# of engine, exhaust and radiator hang off the right, which puts the center of
	# mass 41 mm right of the centerline and makes the rollover threshold 2.43 g
	# turning left against 2.81 g turning right. A harness that only ever turned
	# one way measured one of those two karts. See src/core/chassis.h.
	SCENARIOS=(accel brake skidpad skidpad_right)
fi

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	exit 127
fi

"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

run_scenario() {
	"$GODOT" --headless --path "$PROJECT_ROOT" \
		--script tools/verify/drive_probe.gd -- \
		"--scenario=$1" ${FORWARDED[@]+"${FORWARDED[@]}"} 2>&1
}

FAILED=0
for scenario in "${SCENARIOS[@]}"; do
	FIRST="$(run_scenario "$scenario")"
	STATUS=$?
	echo "$FIRST" | grep -E '^(---|    |state-hash|tuning-hash)'

	# The probe exits non-zero when a run departed — body slip past 30 degrees,
	# which is a spin and not a corner. It prints its state hash first, so the
	# determinism half below still runs and still means something; what it refuses
	# to do is publish a lateral figure off a kart that has spun.
	#
	# This gate had no such check, and for two milestones `--scenario=skidpad`
	# reported "lateral sust 0.09 g" in green off a run whose max body slip was
	# 174.2 degrees.
	if [ "$STATUS" != "0" ]; then
		echo "    scenario        FAILED: see the run's own report above" >&2
		FAILED=1
	fi

	if [ "$ONCE" = "1" ]; then
		continue
	fi

	HASH_A="$(echo "$FIRST" | awk '/^state-hash/ {print $2}')"
	HASH_B="$(run_scenario "$scenario" | awk '/^state-hash/ {print $2}')"

	if [ -z "$HASH_A" ] || [ -z "$HASH_B" ]; then
		echo "    determinism  NO HASH — the scenario did not finish" >&2
		FAILED=1
	elif [ "$HASH_A" = "$HASH_B" ]; then
		echo "    determinism     reproducible across two processes"
	else
		echo "    determinism     DIVERGED: $HASH_A vs $HASH_B" >&2
		FAILED=1
	fi
done

if [ "$FAILED" = "1" ]; then
	echo "error: a scenario either departed or did not reproduce." >&2
	echo "       DEPARTED means the kart spun and the run measures nothing; the fix" >&2
	echo "       is to the vehicle, not to the scenario's throttle." >&2
	echo "       DIVERGED means look for wall-clock reads in the drive model, an" >&2
	echo "       unseeded generator, or iteration over a Dictionary." >&2
	exit 1
fi
