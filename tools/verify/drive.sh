#!/usr/bin/env bash
#
# Run the M3a driving scenarios and check that they are reproducible.
#
#     tools/verify/drive.sh                       # all three scenarios, twice each
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
	echo "$FIRST" | grep -E '^(---|    |state-hash)'

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
	echo "error: a scenario did not reproduce. Look for wall-clock reads in the" >&2
	echo "       drive model, an unseeded generator, or iteration over a Dictionary." >&2
	exit 1
fi
