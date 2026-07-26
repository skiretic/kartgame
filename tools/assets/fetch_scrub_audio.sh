#!/usr/bin/env bash
#
# Fetch the CC0 recordings the tire-scrub band was measured off.
#
#     tools/assets/fetch_scrub_audio.sh [output-directory]
#
# Everything in `docs/REFERENCES.md` §"Tire scrub and wind" — the 1000 Hz peak,
# the 0.67 octave width, the +9.7 / -14.0 dB per octave skirts, the 85 ms event
# duration — was measured off these four files. `src/core/kz_audio_reference.h`
# carries the results and `src/core/scrub_wind.h` builds a filter from them, so
# this script is what makes those numbers checkable by somebody who was not here.
#
# Why a script rather than a hand-drop, same three reasons as `fetch_hdri.sh`:
# the hashes are pinned so a silently re-encoded upstream file is a hard failure
# rather than an unexplained change to how the kart sounds; anyone cloning the
# repo can rebuild the working tree from the original source; and it records in
# code exactly which recordings were used.
#
# ## Two things about these files that the hashes do not say
#
# **They are lossy mp3 previews.** Freesound serves the CC0 originals — 96 kHz
# 24-bit WAV for the three car recordings — only to logged-in accounts, so what
# is pinned here is the public preview and it is what was actually analyzed. The
# codec ceilings are tabulated in REFERENCES.md; the measured upper slope is a
# lower bound on how fast the real thing falls, never an upper one. Anything that
# ever ships audio rather than measuring it should fetch the originals.
#
# **They are passenger cars, not karts.** That is the load-bearing caveat and it
# is why `kz_audio::SCRUB_MEASURED_ON_KART_TIRE` is false and why `core/tuning.h`
# classifies the derived rows `Derived` rather than `Measured`. The fourth file is
# an indoor electric kart track — the only engine-free vehicle recording found —
# and it is fetched because it is the **negative** result: its tone is present in
# 97% of frames, which is a room and a motor rather than stick-slip. A negative
# control that nobody can re-measure is a claim, not a control.
#
# The audio is not committed. This drops it in a gitignored scratch path for
# re-measurement only.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$PROJECT_ROOT/build/audio/scrub}"
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

BASE=https://cdn.freesound.org/previews

# The three the band was measured from. All by one recordist, in one session, on
# one rig — REFERENCES.md item 12 says why that matters and it is not a small
# caveat.
fetch fs71736_chrysler_squeal01.mp3 "$BASE/71/71736_995351-hq.mp3" \
	13d94db3651156da567c71bc6e7eed62db5db08d173c97f00752b54a43cf081f
fetch fs71737_chrysler_squeal02.mp3 "$BASE/71/71737_995351-hq.mp3" \
	43d890acfcb577988d7d8c9594a534e7893bca7757527cff613e3809288cc408
fetch fs71740_maxima_burnout.mp3 "$BASE/71/71740_995351-hq.mp3" \
	20aeb47e6c8bc49376123030edc1cd174feb56a8e452c99bb84b32b639e3437d

# The negative control.
fetch fs173931_electric_gokart_indoor.mp3 "$BASE/173/173931_3229685-hq.mp3" \
	e1b445779c92e195f95c1a4fecdfc4bd20d3186ef8a843ed3a8454c16298a9e8

echo "fetched to $OUT — all CC0 1.0, credited in ATTRIBUTION.md"
