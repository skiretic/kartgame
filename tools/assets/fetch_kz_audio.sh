#!/usr/bin/env bash
#
# Fetch the two-stroke recordings the engine note was measured off.
#
#     tools/assets/fetch_kz_audio.sh [output-directory]
#
# `src/core/kz_audio_reference.h` is the whole reason this exists. Every constant
# in it — the harmonic ladders, the throttle split, the comb delay, and the
# `CENTROID_EXPONENT_*` figures that say how fast the note brightens with rpm —
# was measured off one of these files. `docs/REFERENCES.md` §"Engine audio" is the
# derivation and `ATTRIBUTION.md` is the license record; this script is what makes
# any of it checkable by somebody who was not here.
#
# Why a script rather than a hand-drop, same three reasons as `fetch_hdri.sh`: the
# hashes are pinned so a silently re-encoded upstream file is a hard failure rather
# than an unexplained change to how the kart sounds; anyone cloning the repo can
# rebuild the working tree from the original source; and it records in code exactly
# which recordings were used.
#
# **This script arrived several milestones after the measurements did**, which is
# the defect it closes. The engine corpus was analyzed in a session scratchpad and
# only its results were committed, so for a milestone the numbers in
# `kz_audio_reference.h` were unreproducible in principle — the file names were in
# `ATTRIBUTION.md` and nothing said how to get the bytes back. The scrub band had a
# fetch script from the day it was measured and the engine ladder did not.
#
# ## Three things the hashes do not say
#
# **No recording here is known to be a KZ.** Nothing under an acceptable license
# identifies a TM, Vortex, IAME or Modena. The racers are identified by their
# spectra rather than by their captions, and three of the four whose ladders are
# used are 50 cc museum recordings. `kz_audio_reference.h` says so at length and
# that caveat is the weakest joint in the whole audio model.
#
# **They are lossy.** Ogg Vorbis for the Commons files, public mp3 previews for the
# Freesound ones — the originals need an account. Measured codec ceilings are 19.0
# to 20.8 kHz, which is high enough that the centroid figures are unaffected and
# low enough that the top of every harmonic ladder is a lower bound.
#
# **Two of them are negative controls and are not optional.** The chainsaw has an
# ordinary muffler and the rental kart is a four-stroke; between them they are what
# establishes that a rising spectral centroid is the tuned pipe and not something
# every small engine does. A control nobody can re-measure is a claim.
#
# The audio is not committed. This drops it in a gitignored scratch path for
# re-measurement only.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$PROJECT_ROOT/build/audio/engine}"
mkdir -p "$OUT"

# name url sha256
fetch() {
	local name="$1" url="$2" want="$3" path="$OUT/$1"
	if [ -f "$path" ] && [ "$(shasum -a 256 "$path" | cut -d' ' -f1)" = "$want" ]; then
		echo "ok (cached)  $name"
		return
	fi
	curl -fsSL -A 'kartgame/1.0' -o "$path" "$url"
	local got
	got="$(shasum -a 256 "$path" | cut -d' ' -f1)"
	if [ "$got" != "$want" ]; then
		echo "HASH MISMATCH $name" >&2
		echo "  want $want" >&2
		echo "  got  $got" >&2
		# Removed rather than left behind, so a re-run cannot pick up the bad copy
		# from the cache branch above and report it as fine.
		rm -f "$path"
		exit 1
	fi
	echo "ok           $name"
}

# Wikimedia Commons. `Special:FilePath` redirects to the original upload rather
# than to one of the transcodes, which matters — a transcode has a different hash
# and is a different measurement.
COMMONS=https://commons.wikimedia.org/wiki/Special:FilePath

# The three racing expansion chambers the ladders come from. Work With Sounds
# museum captures: stationary engine, fixed short microphone distance, which is the
# only geometry in the corpus whose absolute spectrum survives.
fetch tomos_d9.ogg "$COMMONS/WWS_MotorcycleTOMOSD-9.ogg" \
	14a32409a5f829883f6bb5c4d28e16046b09bf568283ad7d6af3510cad72e20a
fetch tomos_d7.ogg "$COMMONS/WWS_MotorcycleTOMOSD7.ogg" \
	7b7a0e97c4429ef6aa3f82e0eb04ed9eae9f11d61801bb1da8d0eee3b6a47aa2
fetch tomos_colibri_d3.ogg "$COMMONS/WWS_MotorcycleTOMOSColibrispecialD-3.ogg" \
	33c818d6117ac99621344c704859c6baf1f76af39b4abed2e9b2db6245e73453

# The negative control that matters most: a two-stroke with an ordinary muffler.
# Same displacement class, same combustion cycle, no tuned pipe — so anything that
# separates this from the three above is the pipe and nothing else.
fetch stihl_ms150c_chainsaw.ogg "$COMMONS/WWS_Chainsaw.ogg" \
	d38921e33a47bc3415bcbde7fdfab3f107996ced59d831abaddc3c8f7ae0c2fe

# Freesound public previews.
FS=https://cdn.freesound.org/previews

# A real two-stroke kart track, and the only CC0 one. Measured 9,430-12,430 rpm,
# which is a KZ's working range.
fetch fs529071_eindhoven_kartbaan.mp3 "$FS/529/529071_109901-hq.mp3" \
	c8897be7d88bbae9be0c5942fa1faba0da341d4fbddb4fa9ba4baf56a27a5544

# The second negative control: a governed four-stroke rental kart at 3,020 rpm.
fetch fs317470_gokart_four_stroke.mp3 "$FS/317/317470_4709749-hq.mp3" \
	0c96699ef4e0c4f4c5fcf381a35151edb7f755d7b4618e1e56357b695d89d74f

echo
echo "fetched to $OUT"
echo "licenses: WWS files CC BY 4.0, Eindhoven and the four-stroke control CC0 1.0."
echo "Credit lines are in ATTRIBUTION.md and are an obligation, not a courtesy."
