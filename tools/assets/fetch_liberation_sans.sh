#!/usr/bin/env bash
#
# Fetch Liberation Sans, the metric-compatible substitute for Arial.
#
#     tools/assets/fetch_liberation_sans.sh
#
# The FIA's own documents are set in Arial — pdffonts reads Arial and
# Arial,Bold out of the published entry list, REFERENCES.md's front-end
# section, fact 7 — and Technical Regulations Art. 3.7 names Arial for the
# number panels. Arial is not redistributable; Liberation Sans is
# metric-compatible with it and OFL-1.1 licensed, so panels and paper
# documents render at the regulation's own proportions with a face this
# repository may actually ship. Issue #187, recorded in ATTRIBUTION.md.
#
# Third-party files arrive by script with a pinned checksum, never by hand —
# ARCHITECTURE.md §13, same pattern as fetch_doctest.sh.

set -euo pipefail

VERSION="2.1.5"
SHA256="7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0"
URL="https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-${VERSION}.tar.gz"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DESTINATION="$PROJECT_ROOT/assets/fonts/liberation"

WANTED=(
	LiberationSans-Regular.ttf
	LiberationSans-Bold.ttf
	LICENSE
)

present() {
	for f in "${WANTED[@]}"; do
		[ -f "$DESTINATION/$f" ] || return 1
	done
}

if present; then
	echo "Liberation Sans $VERSION already present."
	exit 0
fi

echo "==> Fetching Liberation fonts $VERSION"
TEMPORARY="$(mktemp)"
trap 'rm -f "$TEMPORARY"' EXIT
curl -fsSL -o "$TEMPORARY" "$URL"

actual="$(shasum -a 256 "$TEMPORARY" | awk '{print $1}')"
if [ "$actual" != "$SHA256" ]; then
	echo "error: checksum mismatch for $URL" >&2
	echo "       expected $SHA256" >&2
	echo "       got      $actual" >&2
	echo "       Refusing to install it." >&2
	exit 1
fi

mkdir -p "$DESTINATION"
for f in "${WANTED[@]}"; do
	tar -xzf "$TEMPORARY" -C "$DESTINATION" --strip-components 1 \
		"liberation-fonts-ttf-${VERSION}/$f"
done
trap - EXIT

echo "    assets/fonts/liberation/  Sans Regular + Bold, OFL-1.1, $SHA256"
