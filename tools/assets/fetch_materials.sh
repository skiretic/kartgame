#!/usr/bin/env bash
#
# Fetch the CC0 photoscan materials the track surface is built from.
#
#     tools/assets/fetch_materials.sh [name ...]
#
# With no arguments, fetches everything in the manifest. With names, fetches
# only those sets. Already-complete sets are skipped, so re-running is cheap
# and safe.
#
# Everything here comes from ambientCG (https://ambientcg.com), CC0. The
# download URL pattern is https://ambientcg.com/get?file=<AssetId>_2K-JPG.zip,
# confirmed against the v2 API (`/api/v2/full_json?id=<AssetId>`) rather than
# assumed — the API is the authority on what an asset is actually called and
# which resolutions exist.
#
# Why a script rather than a hand-drop:
#
#   - The zip hashes are pinned, so a silently re-encoded upstream asset is a
#     hard failure instead of an unexplained visual change six months later.
#   - Anyone cloning the repo without LFS access can still rebuild the working
#     tree from the original source.
#   - It records, in code, exactly which asset IDs were chosen. ATTRIBUTION.md
#     says the same thing in prose; this file is the executable copy.
#
# Only part of each zip is extracted:
#
#   kept     Color, NormalGL, Roughness, AmbientOcclusion, Displacement
#   dropped  NormalDX     — Godot wants OpenGL-convention (+Y up) normals, and
#                           the DX twin is 8-10 MB of a footgun per set
#   dropped  .tres        — ambientCG ships a pre-made Godot material. Ours are
#                           authored in-repo against our own texel density and
#                           triplanar setup; an upstream one would quietly
#                           compete with them.
#   dropped  .blend, .usdc, .mtlx, preview .png — other renderers' business
#
# Nothing is converted or recompressed. The JPGs land exactly as shipped, under
# their original filenames, so the bytes in LFS match the bytes upstream serves.

set -euo pipefail

readonly RES="2K"
readonly FMT="JPG"
readonly BASE_URL="https://ambientcg.com/get?file="

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly PROJECT_ROOT
readonly DEST_ROOT="$PROJECT_ROOT/assets/materials"
readonly CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/kartgame/materials"

# Maps we keep, in the order they are reported.
readonly WANTED_RE='_(Color|NormalGL|Roughness|AmbientOcclusion|Displacement)\.jpg$'

# Maps without which a set is not usable and the fetch is a failure.
readonly REQUIRED_MAPS="Color NormalGL Roughness"

# name | ambientCG asset ID | sha256 of the 2K-JPG zip
#
# Leave the hash empty when adding a set; the script will download it, print
# the hash it saw, and stop so you can paste it in.
readonly MANIFEST="
asphalt_track    | Asphalt020L | e2a6c34234563523df4d5a0466994afe5fbd97193835447758b631ccb915975a
asphalt_detail   | Asphalt020S | 2988e5da0ee6f117a4dc7c13106c48baece9df04272677a3810d7b8759b18b22
concrete_barrier | Concrete040 | f757c69e3c9e380b76bfd9fd0641f519d3717b8ecd1dd76a99e14c3a3695dd7f
road_markings    | Road007     | 3209b2d02601be1e512f4d7ff3f338215967332e21309fd9e16a3542970ae0e4
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

# --- preflight ---------------------------------------------------------------

for tool in curl unzip awk; do
	command -v "$tool" >/dev/null 2>&1 || die "'$tool' is required but not on PATH"
done
command -v sha256sum >/dev/null 2>&1 || command -v shasum >/dev/null 2>&1 ||
	die "need either sha256sum or shasum on PATH"

# These files are large binaries. If .gitattributes is not routing them to LFS,
# committing them poisons the repository permanently — fixing it after the fact
# means rewriting history. Refuse to write anything until that is confirmed.
probe="assets/materials/.lfs-probe.jpg"
if ! git -C "$PROJECT_ROOT" check-attr filter -- "$probe" 2>/dev/null | grep -q 'filter: lfs$'; then
	die "$probe is not routed to Git LFS. Fix .gitattributes before fetching binaries."
fi

mkdir -p "$CACHE_DIR" "$DEST_ROOT"

# --- fetch -------------------------------------------------------------------

selected=("$@")
fetched_bytes=0
fetched_files=0
skipped_sets=0

while IFS='|' read -r name asset_id want_sha; do
	name="$(echo "$name" | xargs)"
	asset_id="$(echo "$asset_id" | xargs)"
	want_sha="$(echo "$want_sha" | xargs)"
	[ -n "$name" ] || continue

	if [ ${#selected[@]} -gt 0 ]; then
		match=0
		for s in "${selected[@]}"; do
			[ "$s" = "$name" ] && match=1
		done
		[ "$match" -eq 1 ] || continue
	fi

	dest="$DEST_ROOT/$name"
	stem="${asset_id}_${RES}-${FMT}"

	# Idempotence: a set is complete when every required map is already on disk.
	complete=1
	for map in $REQUIRED_MAPS; do
		[ -f "$dest/${stem}_${map}.jpg" ] || complete=0
	done
	if [ "$complete" -eq 1 ]; then
		echo "skip  $name ($asset_id) — already present"
		skipped_sets=$((skipped_sets + 1))
		continue
	fi

	zip="$CACHE_DIR/${stem}.zip"
	url="${BASE_URL}${stem}.zip"

	if [ -f "$zip" ] && [ -n "$want_sha" ] && [ "$(sha256_of "$zip")" = "$want_sha" ]; then
		echo "cache $name ($asset_id) — using $zip"
	else
		echo "get   $name ($asset_id) — $url"
		curl --fail --location --silent --show-error \
			--retry 3 --retry-delay 2 \
			-o "$zip.part" "$url" || die "download failed for $asset_id"
		mv "$zip.part" "$zip"
	fi

	got_sha="$(sha256_of "$zip")"
	if [ -z "$want_sha" ]; then
		echo
		echo "$asset_id has no pinned hash. Add it to MANIFEST and re-run:"
		echo "    $got_sha"
		exit 1
	fi
	if [ "$got_sha" != "$want_sha" ]; then
		rm -f "$zip"
		die "checksum mismatch for $asset_id
     expected $want_sha
     got      $got_sha
   Upstream changed the file, or the download was corrupted. Cached zip removed."
	fi

	members="$(unzip -Z1 "$zip" | grep -E "$WANTED_RE" || true)"
	[ -n "$members" ] || die "$asset_id: the zip contains none of the maps we want"

	for map in $REQUIRED_MAPS; do
		echo "$members" | grep -q "_${map}\.jpg$" ||
			die "$asset_id: required map '$map' is missing from the zip"
	done

	mkdir -p "$dest"
	while IFS= read -r member; do
		[ -n "$member" ] || continue
		unzip -o -q -j "$zip" "$member" -d "$dest"
		out="$dest/$(basename "$member")"
		bytes="$(size_of "$out")"
		fetched_bytes=$((fetched_bytes + bytes))
		fetched_files=$((fetched_files + 1))
		printf '      %-64s %9s\n' "assets/materials/$name/$(basename "$member")" "$(human "$bytes")"
	done <<<"$members"
done <<<"$MANIFEST"

# --- report ------------------------------------------------------------------

echo
if [ "$fetched_files" -gt 0 ]; then
	echo "wrote $fetched_files files, $(human "$fetched_bytes"), under assets/materials/"
fi
if [ "$skipped_sets" -gt 0 ]; then
	echo "skipped $skipped_sets set(s) already on disk"
fi
echo "zip cache: $CACHE_DIR"
echo
echo "Normals are OpenGL convention (+Y up) — the *_NormalGL.jpg files. Godot"
echo "expects that convention; do not swap in the DX variants."
exit 0
