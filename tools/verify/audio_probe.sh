#!/usr/bin/env bash
#
# Run the audio boundary probe against both audio drivers and print them
# side by side.
#
#     tools/verify/audio_probe.sh                     both drivers, std::sin
#     tools/verify/audio_probe.sh --table             both drivers, table lookup
#     tools/verify/audio_probe.sh --driver=CoreAudio  one of them
#     tools/verify/audio_probe.sh --seconds=20        a longer window
#
# Two drivers rather than one, and this is the whole reason the wrapper exists.
# `--headless` selects the Dummy audio driver, and the Dummy driver is **not a
# quiet CoreAudio**: measured, its mix thread runs on an ordinary core at
# ordinary speed and calls back in irregular bursts, while CoreAudio's runs at
# 2.5x the integer cost and 6x the floating-point cost on a regular deadline.
# Every cost figure taken headless is understated by about 6x and every
# call-regularity figure taken headless is wrong in shape, not just in scale. A
# probe that only ran one of them would have reported a comfortable fit inside
# `ARCHITECTURE.md` §15's budget that does not exist. See ADR-0035.
#
# The display driver is forced to headless in both cases: nothing here renders,
# and a window would only add a compositor to the measurement.
#
# The double import is the ADR-0018 workaround; see tools/verify/verify.sh.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
DRIVERS=()
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*)   GODOT="${argument#*=}" ;;
		--driver=*)  DRIVERS+=("${argument#*=}") ;;
		*)           FORWARDED+=("$argument") ;;
	esac
done

if [ ${#DRIVERS[@]} -eq 0 ]; then
	DRIVERS=(Dummy CoreAudio)
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

FAILED=0
for driver in "${DRIVERS[@]}"; do
	echo
	echo "######################################################################"
	echo "# audio driver: $driver"
	echo "######################################################################"

	OUTPUT="$("$GODOT" --display-driver headless --audio-driver "$driver" \
		--path "$PROJECT_ROOT" --script tools/verify/audio_probe.gd -- \
		${FORWARDED[@]+"${FORWARDED[@]}"} 2>&1)"

	echo "$OUTPUT" | sed -n '/^=== 0\./,$p'

	# The one assertion this wrapper makes. Everything else here is a number to
	# read, but "_mix was never called" would make every section below section 3
	# a report about nothing while still printing cleanly, which is exactly the
	# failure mode `SKIP_IMPORT=1` and the stale-manifest trap are about.
	if ! echo "$OUTPUT" | grep -q "ANSWER"; then
		echo "error: the probe did not reach its conclusion under $driver" >&2
		FAILED=1
	elif echo "$OUTPUT" | grep -q "_mix was never called"; then
		echo "error: _mix was never called under $driver — the GDExtension" >&2
		echo "       AudioStreamPlayback path is not being reached at all" >&2
		FAILED=1
	fi
done

if [ "$FAILED" = "1" ]; then
	exit 1
fi
