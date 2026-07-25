#!/usr/bin/env bash
#
# Fetch the doctest single-header test framework.
#
#     tools/assets/fetch_doctest.sh
#
# Third-party files arrive by script with a pinned checksum, never by hand —
# ARCHITECTURE.md §13, and the same pattern the CC0 asset downloaders use. A
# header pasted into the tree by whoever needed it is a header nobody can
# re-derive, and "which version is this" becomes an archaeology problem the first
# time it misbehaves.
#
# doctest rather than Catch2: one header, no build system of its own, and it
# compiles fast enough that the test binary is not a reason to avoid running the
# tests. ARCHITECTURE.md §14 names either.
#
# MIT licensed. Recorded in ATTRIBUTION.md.

set -euo pipefail

VERSION="2.4.12"
SHA256="94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced"
URL="https://raw.githubusercontent.com/doctest/doctest/v${VERSION}/doctest/doctest.h"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DESTINATION="$PROJECT_ROOT/third_party/doctest"
TARGET="$DESTINATION/doctest.h"

mkdir -p "$DESTINATION"

# Godot's importer would otherwise walk the C++ headers under third_party/. The
# godot-cpp subtree carries one of these for the same reason.
if [ ! -f "$DESTINATION/.gdignore" ]; then
	: > "$DESTINATION/.gdignore"
fi

verify() {
	local actual
	actual="$(shasum -a 256 "$1" | awk '{print $1}')"
	[ "$actual" = "$SHA256" ]
}

if [ -f "$TARGET" ] && verify "$TARGET"; then
	echo "doctest $VERSION already present and matches its checksum."
	exit 0
fi

echo "==> Fetching doctest $VERSION"
TEMPORARY="$(mktemp)"
trap 'rm -f "$TEMPORARY"' EXIT
curl -fsSL -o "$TEMPORARY" "$URL"

if ! verify "$TEMPORARY"; then
	echo "error: checksum mismatch for $URL" >&2
	echo "       expected $SHA256" >&2
	echo "       got      $(shasum -a 256 "$TEMPORARY" | awk '{print $1}')" >&2
	echo "       Refusing to install it. If the upstream tag genuinely moved," >&2
	echo "       verify the new file by hand and update SHA256 in this script." >&2
	exit 1
fi

mv "$TEMPORARY" "$TARGET"
trap - EXIT
echo "    third_party/doctest/doctest.h  $SHA256"
