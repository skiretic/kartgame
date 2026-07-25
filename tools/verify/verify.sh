#!/usr/bin/env bash
#
# Run the M0 verification suite against a built extension.
#
#     tools/verify/verify.sh [path-to-godot]
#
# Wraps an engine bug in the headless *editor* import. The first import of a cold
# project dies; the crash happens after .godot/ has been seeded, so the second
# run is clean.
#
# So: import twice, ignore the first exit code, and let the second one speak.
#
# This was believed to be macOS-only when it was written. It is not. On macOS it
# is EXC_BAD_ACCESS inside MoltenVK converting SPIR-V to Metal -- in --headless,
# where no shader compilation should happen at all, and even with a non-Metal
# rendering driver. On Linux it is SIGABRT, exit 134. Different signal, same
# operation and the same cold-run-only behavior.
#
# So the double import is load-bearing on both platforms, not a redundant second
# behind a shared code path. CI calls this script rather than repeating its steps
# in YAML, which is how the Linux case was found in the first place.
#
# See docs/DECISIONS.md ADR-0018.

set -uo pipefail

GODOT="${1:-godot}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	echo "Pass an explicit path: tools/verify/verify.sh /path/to/godot" >&2
	exit 127
fi

echo "==> Importing (cold pass; a crash here is the known macOS bug, see ADR-0018)"
"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true

echo "==> Importing (warm pass)"
if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import; then
	echo "error: the warm import failed, which is not the known macOS bug" >&2
	exit 1
fi

echo "==> Verifying"
exec "$GODOT" --headless --path "$PROJECT_ROOT" --script tools/verify/verify_extension.gd
