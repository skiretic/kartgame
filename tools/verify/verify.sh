#!/usr/bin/env bash
#
# Run the M0 verification suite against a built extension.
#
#     tools/verify/verify.sh [path-to-godot]
#
# Wraps a macOS-only engine bug. On macOS 26 and 27 the *first* headless import
# of a project segfaults inside MoltenVK while it converts SPIR-V to Metal, even
# with --headless and even with a non-Metal rendering driver. The crash happens
# after .godot/ has been seeded, so the second run is clean.
#
# So: import twice, ignore the first exit code, and let the second one speak.
# Doing this unconditionally rather than only on macOS keeps one code path, and a
# redundant warm import on Linux costs about a second.
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
