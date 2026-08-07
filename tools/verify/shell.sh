#!/usr/bin/env bash
#
# The M5f gate: the front-end shell's structure. Every screen reachable, back
# returning where it came from, focus landing somewhere a person can see, a pad
# alone crossing every control, and a lap time surviving a relaunch.
#
#     tools/verify/shell.sh [path-to-godot]
#     tools/verify/shell.sh --case=focus,pad_reach
#     tools/verify/shell.sh --break                 the negative-control pass
#     tools/verify/shell.sh --break=occlude         one mode
#
# Every argument that is not a path to Godot is forwarded to shell_probe.gd.
#
# Wraps the same engine bug tools/verify/verify.sh does: the first headless
# *editor* import of a cold project dies, after .godot/ has been seeded, so the
# second run is clean. Import twice, ignore the first exit code. ADR-0018.
#
# **This gate needs no generated asset and no font.** It forces --backdrop=flat,
# which builds a solid field and one light and touches neither
# assets/generated/kart.glb nor the circuit mesh -- so it runs in an agent
# worktree, where those are gitignored and absent. Proven by running it in
# exactly such a worktree: with no assets/generated/ and no assets/fonts/ at all
# it reports 69 of 79, the missing one being the tabular-figure assertion, which
# is deliberately downgraded to a note on the engine fallback face. The probe's
# own first line reads `backdrop flat: no generated asset touched`. Nothing here
# may grow a dependency on a .glb; if it does, the gate stops running in the
# place the work actually happens.
#
# The negative controls are `--break`. circuit.sh carries a circuit that must
# fail to load and input_push_probe.gd has its own --break; this gate had neither,
# and a check that cannot fail is not a check. Each mode sabotages one
# property in-process -- the InputMap, the live tree, a file under
# user://shell_probe/ -- and the probe then asserts the check aimed at it went
# red. **Its exit code is inverted**: 0 means the sabotage was caught. Bare
# --break runs every mode and fails if any one of them goes through unnoticed.
# The mode list comes from the probe (`--list-breaks`) rather than being repeated
# here, so a new mode joins this pass without a second edit.

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
	echo "Pass an explicit path: tools/verify/shell.sh /path/to/godot" >&2
	exit 127
fi

echo "==> Importing (cold pass; a crash here is the known bug, see ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true

echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null; then
	echo "error: the warm import failed, which is not the known bug" >&2
	exit 1
fi

probe() {
	"$GODOT" --headless --path "$PROJECT_ROOT" \
		--script tools/verify/shell_probe.gd -- "$@"
}

echo "==> Verifying the shell"
probe ${FORWARDED[@]+"${FORWARDED[@]}"}
STATUS=$?

if [ "$BREAK_ALL" -eq 0 ]; then
	if [ "$STATUS" -eq 0 ]; then
		echo "shell gate: PASSED"
	else
		echo "shell gate: FAILED -- see the FAIL lines above" >&2
	fi
	exit "$STATUS"
fi

# The negative-control pass. Every mode must be caught; a mode that slips through
# is a hole in the gate and is reported as one, by name.
echo "==> Negative controls (each sabotage must be caught)"
# Godot prints its version banner on stdout ahead of anything a script says, so
# the list is tagged and filtered rather than read raw -- five engine words were
# being run as five --break modes, and each of them "went through unnoticed".
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
	echo "shell gate: negative controls $TOTAL of $TOTAL caught"
else
	echo "shell gate: ${#MISSED[@]} of $TOTAL sabotages went through unnoticed --" \
		"${MISSED[*]}" >&2
	STATUS=1
fi

if [ "$STATUS" -eq 0 ]; then
	echo "shell gate: PASSED"
else
	echo "shell gate: FAILED -- see the FAIL lines above" >&2
fi
exit "$STATUS"
