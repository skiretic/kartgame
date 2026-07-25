#!/usr/bin/env bash
#
# Fetch the CC0 sky HDRIs the track is lit and reflected against.
#
#     tools/assets/fetch_hdri.sh [name ...]
#
# With no arguments, fetches everything in the manifest. With names, fetches
# only those. Already-present files are skipped, so re-running is cheap and
# safe.
#
# Everything here comes from Poly Haven (https://polyhaven.com), CC0. The
# download URLs are taken from the public API (`/files/<slug>`), which reports
# the real CDN path, size and MD5 per resolution and format, rather than being
# guessed from the slug.
#
# Why a script rather than a hand-drop:
#
#   - The hashes are pinned, so a silently re-encoded upstream file is a hard
#     failure instead of an unexplained lighting change six months later. An
#     HDRI is the scene's key light; a quiet change to it moves every shot.
#   - Anyone cloning the repo without LFS access can still rebuild the working
#     tree from the original source.
#   - It records, in code, exactly which skies were chosen and why. ATTRIBUTION.md
#     says the same thing in prose; this file is the executable copy.
#
# Choices baked in here:
#
#   4K, not 8K/16K/24K   Poly Haven goes to 24K. Godot builds a radiance
#                        cubemap from whatever it is handed, at import and
#                        again on any sky change, and the source panorama stays
#                        resident. 4K (4096x2048) is the point where the sun
#                        disc is still a disc and the whole thing still fits in
#                        a modest VRAM budget. Reflections come from probes
#                        (ARCHITECTURE.md §4), not from squinting at the sky.
#
#   .hdr, not .exr       Same pixels, one third the bytes — 24 MB against 92 MB
#                        at 4K. Radiance RGBE carries a shared exponent, which
#                        costs mantissa precision the sky does not have anyway,
#                        and Godot imports both identically. LFS bandwidth is
#                        not free.
#
# Nothing is converted or recompressed. The files land exactly as shipped,
# under their original names, so the bytes in LFS match the bytes Poly Haven
# serves.
#
# On brightness: these are photographic HDRIs, linearly merged from an exposure
# bracket. They are internally consistent but NOT calibrated to absolute lux —
# see ATTRIBUTION.md for the measured numbers and what that means for mixing an
# HDRI with a physically-lit sun.

set -euo pipefail

readonly RES="4k"
readonly FMT="hdr"
readonly BASE_URL="https://dl.polyhaven.org/file/ph-assets/HDRIs/${FMT}/${RES}/"

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly PROJECT_ROOT
readonly DEST_DIR="$PROJECT_ROOT/assets/hdri"
readonly CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/kartgame/hdri"

# What a 4K equirectangular panorama has to be, checked after download.
readonly EXPECT_WIDTH=4096
readonly EXPECT_HEIGHT=2048

# name | Poly Haven slug | sha256 of the 4K .hdr
#
# Leave the hash empty when adding a sky; the script will download it, print
# the hash it saw, and stop so you can paste it in.
#
# The two kloofendal entries are the same location, tripod and stitch, shot in
# different conditions. That is the point: swapping between them changes the
# lighting and nothing else, which is the only way to tell whether a material
# is wrong or the sky is.
readonly MANIFEST="
midday_clear     | kloofendal_43d_clear | c8a14d171ad02442b1a2ea9f979d8b8a339d19de7612dd5b05fdce689179699d
afternoon_low_sun| aarfontein_dirt_road | ee07091c13a55b589f5844352a61a87ab9a1e5a96697555d8fbec71fc79e3613
overcast         | kloofendal_overcast  | 34ea1a9cdb9a378db3d14f7ecc52ffefdcb9cf2787b51a1c1df96eb8b38f72f5
"

die() {
	echo "error: $*" >&2
	exit 1
}

sha256_of() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

size_of() {
	wc -c <"$1" | tr -d ' '
}

human() {
	awk -v b="$1" 'BEGIN { printf "%.1f MB", b / 1048576 }'
}

# Radiance .hdr is a text header, a blank line, then a resolution line such as
# "-Y 2048 +X 4096". Reading it is enough to catch a truncated download or the
# wrong resolution silently landing in the repo.
hdr_dimensions() {
	awk 'BEGIN { RS = "\n" }
	     NR == 1 && $0 !~ /^#\?RADIANCE/ && $0 !~ /^#\?RGBE/ { exit 1 }
	     /^[-+][XY] [0-9]+ [-+][XY] [0-9]+$/ {
	         if ($1 ~ /Y/) print $4, $2; else print $2, $4
	         exit 0
	     }
	     NR > 64 { exit 1 }' "$1"
}

# --- preflight ---------------------------------------------------------------

for tool in curl awk; do
	command -v "$tool" >/dev/null 2>&1 || die "'$tool' is required but not on PATH"
done
command -v sha256sum >/dev/null 2>&1 || command -v shasum >/dev/null 2>&1 ||
	die "need either sha256sum or shasum on PATH"

# These files are 25 MB each. If .gitattributes is not routing them to LFS,
# committing them poisons the repository permanently — fixing it after the fact
# means rewriting history. Refuse to write anything until that is confirmed.
probe="assets/hdri/.lfs-probe.${FMT}"
if ! git -C "$PROJECT_ROOT" check-attr filter -- "$probe" 2>/dev/null | grep -q 'filter: lfs$'; then
	die "$probe is not routed to Git LFS. Fix .gitattributes before fetching binaries."
fi

mkdir -p "$CACHE_DIR" "$DEST_DIR"

# --- fetch -------------------------------------------------------------------

selected=("$@")
fetched_bytes=0
fetched_files=0
skipped_files=0

while IFS='|' read -r name slug want_sha; do
	name="$(echo "$name" | xargs)"
	slug="$(echo "$slug" | xargs)"
	want_sha="$(echo "$want_sha" | xargs)"
	[ -n "$name" ] || continue

	if [ ${#selected[@]} -gt 0 ]; then
		match=0
		for s in "${selected[@]}"; do
			[ "$s" = "$name" ] && match=1
		done
		[ "$match" -eq 1 ] || continue
	fi

	file="${slug}_${RES}.${FMT}"
	dest="$DEST_DIR/$file"

	if [ -f "$dest" ]; then
		echo "skip  $name ($slug) — already present"
		skipped_files=$((skipped_files + 1))
		continue
	fi

	cached="$CACHE_DIR/$file"
	url="${BASE_URL}${file}"

	if [ -f "$cached" ] && [ -n "$want_sha" ] && [ "$(sha256_of "$cached")" = "$want_sha" ]; then
		echo "cache $name ($slug) — using $cached"
	else
		echo "get   $name ($slug) — $url"
		curl --fail --location --silent --show-error \
			--retry 3 --retry-delay 2 \
			-o "$cached.part" "$url" || die "download failed for $slug"
		mv "$cached.part" "$cached"
	fi

	got_sha="$(sha256_of "$cached")"
	if [ -z "$want_sha" ]; then
		echo
		echo "$slug has no pinned hash. Add it to MANIFEST and re-run:"
		echo "    $got_sha"
		exit 1
	fi
	if [ "$got_sha" != "$want_sha" ]; then
		rm -f "$cached"
		die "checksum mismatch for $slug
     expected $want_sha
     got      $got_sha
   Upstream changed the file, or the download was corrupted. Cached file removed."
	fi

	dims="$(hdr_dimensions "$cached")" ||
		die "$slug: not a Radiance HDR, or no resolution line in the header"
	read -r width height <<<"$dims"
	if [ "$width" != "$EXPECT_WIDTH" ] || [ "$height" != "$EXPECT_HEIGHT" ]; then
		die "$slug: expected ${EXPECT_WIDTH}x${EXPECT_HEIGHT}, got ${width}x${height}"
	fi

	cp "$cached" "$dest"
	bytes="$(size_of "$dest")"
	fetched_bytes=$((fetched_bytes + bytes))
	fetched_files=$((fetched_files + 1))
	printf '      %-52s %6sx%-5s %9s\n' \
		"assets/hdri/$file" "$width" "$height" "$(human "$bytes")"
done <<<"$MANIFEST"

# --- report ------------------------------------------------------------------

echo
if [ "$fetched_files" -gt 0 ]; then
	echo "wrote $fetched_files file(s), $(human "$fetched_bytes"), under assets/hdri/"
fi
if [ "$skipped_files" -gt 0 ]; then
	echo "skipped $skipped_files file(s) already on disk"
fi
echo "download cache: $CACHE_DIR"
echo
echo "These are equirectangular panoramas — import as PanoramaSkyMaterial, not"
echo "as a texture. Keep the radiance cubemap size modest; the sun disc, not the"
echo "cubemap resolution, is what produces the shadow."
exit 0
