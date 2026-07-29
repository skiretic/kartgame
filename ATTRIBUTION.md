# Attribution

Provenance for every third-party asset in this repository.

Maintained **at import time**, not reconstructed later. CC0 imposes no legal obligation to credit — this is recorded anyway, because knowing where an asset came from and what its terms are is worth more than the credit is.

Assets under `assets/generated/` are produced by the tools in `tools/` and are not third-party.

---

## Materials

| Asset | Source | License | Imported |
|---|---|---|---|
| `assets/materials/asphalt_track` — Asphalt 020 L | [ambientCG Asphalt020L](https://ambientcg.com/a/Asphalt020L) | CC0 | 2026-07-24 |
| `assets/materials/asphalt_detail` — Asphalt 020 S | [ambientCG Asphalt020S](https://ambientcg.com/a/Asphalt020S) | CC0 | 2026-07-24 |
| `assets/materials/concrete_barrier` — Concrete 040 | [ambientCG Concrete040](https://ambientcg.com/a/Concrete040) | CC0 | 2026-07-24 |
| `assets/materials/road_markings` — Road 007 | [ambientCG Road007](https://ambientcg.com/a/Road007) | CC0 | 2026-07-24 |

All four are 2K JPG, fetched by `tools/assets/fetch_materials.sh`, which pins the SHA-256 of each source zip. Files keep their upstream names, so `Asphalt020L_2K-JPG_Color.jpg` here is byte-identical to the one ambientCG serves. Nothing was converted or recompressed.

Asphalt020L and Asphalt020S are the same scan captured at two scales — 4.6 m and 1.85 m square. That is why they are paired: a detail layer that matches its base exactly, rather than two unrelated asphalts fighting each other under the cockpit camera.

**Normals are OpenGL convention.** ambientCG ships both, `_NormalGL` (+Y up) and `_NormalDX` (+Y down). Godot expects OpenGL. Only the GL files are extracted, so there is no DX twin sitting next to them waiting to be picked up by mistake.

Each zip also carries a `.blend`, a `.usdc`, an `.mtlx`, and a ready-made Godot `.tres`. None of them are extracted. Our materials are authored in-repo against our own texel density and triplanar setup, and an upstream `.tres` would quietly compete with them.

Scanned real-world area, recorded here because the texel density standard in `docs/ARCHITECTURE.md` §5 cannot be applied without it:

| Asset | Capture | Scanned area | Native density at 2K |
|---|---|---|---|
| Asphalt020L | Height field photogrammetry | 4.60 m square | 445 px/m |
| Asphalt020S | Height field photogrammetry | 1.85 m square | 1,107 px/m |
| Concrete040 | Height field photogrammetry | 1.40 m square | 1,463 px/m |
| Road007 | Procedural, not a scan | 7.50 m square | 273 px/m |

Road007 is the one to watch. It is the only CC0 painted-marking material ambientCG has, and it is procedural rather than scanned. At 2K it holds 273 px/m natively — above the 256 px/m prop standard, barely half the 512 px/m the track surface requires. Treat it as a source for line decals, or refetch at 4K (546 px/m) before it goes anywhere near the racing surface.

## Environments / HDRIs

| Asset | Source | License | Imported |
|---|---|---|---|
| `assets/hdri/kloofendal_43d_clear_4k.hdr` — Kloofendal 43d Clear | [Poly Haven kloofendal_43d_clear](https://polyhaven.com/a/kloofendal_43d_clear) | CC0 | 2026-07-24 |
| `assets/hdri/aarfontein_dirt_road_4k.hdr` — Aarfontein Dirt Road | [Poly Haven aarfontein_dirt_road](https://polyhaven.com/a/aarfontein_dirt_road) | CC0 | 2026-07-24 |
| `assets/hdri/kloofendal_overcast_4k.hdr` — Kloofendal Overcast | [Poly Haven kloofendal_overcast](https://polyhaven.com/a/kloofendal_overcast) | CC0 | 2026-07-24 |

All three are 4K equirectangular Radiance `.hdr`, fetched by `tools/assets/fetch_hdri.sh`, which pins the SHA-256 of each file. Files keep their upstream names, so `kloofendal_43d_clear_4k.hdr` here is byte-identical to the one Poly Haven serves — the MD5 was checked against the one the API reports. Nothing was converted or recompressed.

`.hdr` rather than `.exr` because the two carry the same pixels and the `.hdr` is a third of the bytes — 24 MB against 92 MB at 4K. 4K rather than the 24K Poly Haven offers because Godot rebuilds a radiance cubemap from the panorama on import and on every sky change, and the source stays resident. Reflections come from probes (§4), not from sky resolution.

The two Kloofendal plates are the same location, tripod, and stitch in different weather. That is deliberate: swapping between them changes the lighting and nothing else, which is the only way to tell whether a material is wrong or the sky is.

| Asset | Time of day | Weather | Sun disc | Sun elevation | Dynamic range | White balance |
|---|---|---|---|---|---|---|
| Kloofendal 43d Clear | Midday | Clear | **Yes**, hard-edged | 42.9° | 22 EV | 5313 K |
| Aarfontein Dirt Road | Late afternoon | Clear | **Yes**, warm and low | 9.5° | 22 EV | 5313 K |
| Kloofendal Overcast | Afternoon | Overcast | **No** — fully diffuse | n/a | 6 EV | 5303 K |

Sun elevation is measured from the files themselves, not read off the API — Poly Haven publishes GPS coordinates and a capture timestamp, not a sun vector. For Kloofendal 43d Clear the two agree: the solar position computed from its coordinates and timestamp is 43.5°, the brightest pixel sits at 42.9°, and the asset is named `43d`. For the other two the published timestamp does not reconcile with any plausible time zone, so only the measured figure is trustworthy. Azimuth is not recorded because it is not a useful number here: a panorama's yaw is whichever way the photographer's tripod happened to face. What matters for placing a `DirectionalLight3D` is where the sun sits *in the image* — in Godot's convention, where `u = 0.5` faces −Z, both sunny plates put the sun at `u ≈ 0.600`, about 36° to the right of −Z.

**Dynamic range is a span, not a level.** The API's `evs_cap` is the number of stops between the brightest and darkest bracket, so 22 EV describes how much range was captured, not how bright the result is. White balance is the color temperature the plate was shot at, which matters when a plate is mixed with an in-engine light of a different temperature — all three here are close enough to daylight that no correction is needed between them.

### These are relative, not lux

This is the part that matters for moving to physical light units, so it was measured rather than assumed. Integrating each panorama's upper hemisphere with a cosine weight gives the illuminance it would produce on a horizontal surface, in whatever units its pixels are in:

| Asset | Measured illuminance | Sun's share | Real-world equivalent | Implied scale |
|---|---|---|---|---|
| Kloofendal 43d Clear | 5.5 | 76% | ~80,000 lx | ~14,600 |
| Aarfontein Dirt Road | 2.4 | 71% | ~13,000 lx | ~5,300 |
| Kloofendal Overcast | 4.1 | 0% | ~10,000 lx | ~2,500 |

**Poly Haven HDRIs are not calibrated to absolute illuminance.** A clear midday sky produces roughly 80,000 lx in reality; this one integrates to 5.5. The pixels are scene-referred, merged linearly from an exposure bracket and then normalized per asset toward a comfortable mid-gray — the median luminance of the clear plate is 0.179, which is middle gray to three decimal places, and that is not a coincidence.

For Kloofendal 43d Clear the scale factor lands at about 1.4 × 10⁴ from two independent directions: dividing real horizontal illuminance by the measured integral gives 14,600, and dividing the real luminance of the solar disc (~1.6 × 10⁹ cd/m²) by the brightest pixel gives 13,900. Agreeing within 5% via two unrelated routes is good evidence the file is a *uniformly scaled* copy of physical reality. Within one HDRI, ratios are physically meaningful.

**Between HDRIs they are not.** The scale factor is per asset and varies by a factor of six across these three. Measured, the clear plate is only 1.34× the overcast one; in reality clear midday is about 6.7× brighter than an overcast afternoon. Drop these into the same scene at the same energy multiplier and the weather change is nearly invisible — the entire "the light dropped" cue is gone. Each plate needs its own multiplier before a time-of-day comparison means anything.

Two consequences for the lighting rig:

- **Do not light with the HDRI and a physical sun at the same time.** The solar disc carries 71–76% of the total illuminance in both sunny plates. Adding a `DirectionalLight3D` on top of an unmodified HDRI roughly doubles the key light. Either the plate drives everything, or the sun is masked out of the plate and the directional light replaces it. The second is what a racetrack wants — a real directional light gives PSSM shadows and a sun angle that is a lever rather than baked into a photograph.
- **The overcast plate has no sun to remove.** Its brightest pixel is 6.0 against the clear plate's 115,156, and its peak-to-median ratio is 11 against 644,219. It is pure ambient, so it is the correct plate for testing that the scene still reads without a directional key — and the wrong one for anything that needs a shadow.

Both are consistent with §4's decision to drive the sky from `PhysicalSkyMaterial` with a real sun angle. These plates are for look development, IBL reference, and reflection content — not for being the sun.

## Fonts

| Asset | Source | License | Imported |
|---|---|---|---|
| `assets/fonts/liberation/` — Liberation Sans Regular + Bold 2.1.5 | [liberationfonts/liberation-fonts](https://github.com/liberationfonts/liberation-fonts), fetched by `tools/assets/fetch_liberation_sans.sh` (pinned SHA-256) | SIL OFL 1.1 | 2026-07-28 |

Liberation Sans is the metric-compatible substitute for **Arial**, which is
what both halves of the front end's typography actually call for and cannot
ship: Technical Regulations Art. 3.7 names Arial for the number panels, and
`pdffonts` reads embedded `Arial`/`Arial,Bold` out of the FIA's own published
entry list (REFERENCES.md, front-end section, fact 7). Metric compatibility is
the point — the regulation's proportions survive the substitution. Issue #187.

## Audio

**No audio ships in this repository, and none of the files below is committed.**
`ARCHITECTURE.md` §12 synthesizes the engine note rather than sampling it, so these
recordings are *reference material* — they were measured to find the numbers the
synthesizer needs, and then discarded. They are listed here for the same reason the
HDRIs are: knowing where a number came from and what its terms are is worth more
than the credit is, and a later session that wants to re-derive or challenge a
figure in `docs/REFERENCES.md` §"Engine audio" needs to be able to fetch exactly
the same bytes.

All twelve are from Wikimedia Commons. Nothing with an ambiguous license was used.

| File | Engine | License | Author / credit line | SHA-256 (first 16) |
|---|---|---|---|---|
| [`WWS_MotorcycleTOMOSD-9.ogg`](https://commons.wikimedia.org/wiki/File:WWS_MotorcycleTOMOSD-9.ogg) | Tomos D-9, 1965 50 cc two-stroke GP racer | CC BY 4.0 | Work With Sounds / Technical Museum of Slovenia; recordist Boštjan Troha | `14a32409a5f82988` |
| [`WWS_MotorcycleTOMOSD7.ogg`](https://commons.wikimedia.org/wiki/File:WWS_MotorcycleTOMOSD7.ogg) | Tomos D-7, 1962 50 cc two-stroke GP racer | CC BY 4.0 | Work With Sounds / Technical Museum of Slovenia; recordist Dušan Oblak | `7b7a0e97c4429ef6` |
| [`WWS_MotorcycleTOMOSColibrispecialD-3.ogg`](https://commons.wikimedia.org/wiki/File:WWS_MotorcycleTOMOSColibrispecialD-3.ogg) | Tomos Colibri special D-3, 1959 50 cc racer | CC BY 4.0 | Work With Sounds / Technical Museum of Slovenia; recordist Dušan Oblak | `33c818d6117ac996` |
| [`WWS_Chainsaw.ogg`](https://commons.wikimedia.org/wiki/File:WWS_Chainsaw.ogg) | Stihl MS 150 C, 23.6 cc two-stroke, muffler | CC BY 4.0 | Work With Sounds / Werstas | `d38921e33a47bc34` |
| [`Yamaha_RX-100_accelerates_to_top_speed.ogg`](https://commons.wikimedia.org/wiki/File:Yamaha_RX-100_accelerates_to_top_speed.ogg) | Yamaha RX-100, 98.2 cc two-stroke | CC BY-SA 4.0 | Marianoberna | `87e580700bf803d0` |
| [`Piaggio_Vespa_Suono_Motore.ogg`](https://commons.wikimedia.org/wiki/File:Piaggio_Vespa_Suono_Motore.ogg) | Vespa PK 125 S, 125 cc two-stroke | CC BY-SA 4.0 | Dan1gia2 | `d8011a525efaaf71` |
| [`Garelli_Bonanza_starten_01.ogg`](https://commons.wikimedia.org/wiki/File:Garelli_Bonanza_starten_01.ogg) | Garelli Bonanza moped | CC BY-SA 3.0 | Huhu Uet | `6c8da5d2b53f5e55` |
| [`Wurstelprater_Wien_2024_GoKart_Honda.ogg`](https://commons.wikimedia.org/wiki/File:Wurstelprater_Wien_2024_GoKart_Honda.ogg) | Honda 200 cc four-stroke rental kart | CC BY-SA 4.0 | DrTrumpet | `f8c570d08a13fe8b` |
| [`Wurstelprater_Wien_2024_Bandito_Rennbahn_Go-Kart.ogg`](https://commons.wikimedia.org/wiki/File:Wurstelprater_Wien_2024_Bandito_Rennbahn_Go-Kart.ogg) | Amusement-park kart, type unstated | CC BY-SA 4.0 | DrTrumpet | `d900aeaf27c2d8c5` |
| [`USSR_bicycle_2T_engine_D-4.wav`](https://commons.wikimedia.org/wiki/File:USSR_bicycle_2T_engine_D-4.wav) | D-4 auxiliary bicycle engine, 45 cc, 1956 | Public domain | Віктор Ходєєв / Viktor Khodyeyev | `f5decafe374eb8f7` |
| [`Chainsaw_1.ogg`](https://commons.wikimedia.org/wiki/File:Chainsaw_1.ogg) | Unnamed two-stroke chainsaw | Public domain | ezwa, via PDSounds (rec. 648) | `f3c89a03a08cec37` |
| [`Chainsaw_5.ogg`](https://commons.wikimedia.org/wiki/File:Chainsaw_5.ogg) | Unnamed two-stroke chainsaw | Public domain | ezwa, via PDSounds (rec. 652) | `3821c83d77837703` |

A second sweep found the recordings that actually matter — two-stroke racing karts
at KZ engine speeds. These are from archive.org's `radio-aporee-maps` collection,
Freesound and Commons video:

| File | What it is | License | Author / credit line | SHA-256 (first 16) |
|---|---|---|---|---|
| [Eindhoven kartbaan](https://freesound.org/people/peter1955/sounds/529071) | Kart track, Netherlands 2020 — measured 9,430–12,430 rpm two-stroke | **CC0 1.0** | peter1955 | `c8897be7d88bbae9` |
| [3ο P.I.C.K Patras 2011](https://commons.wikimedia.org/wiki/File:3ο_P.I.C.K_Patras_2011_video.ogv) | Patras International Circuit for Kart — measured 12,270–15,670 rpm two-stroke | CC BY-SA 3.0 | Tony Esopi | `80122451865e74c8` |
| [Scarborough 2012, 125-400cc practice](https://archive.org/details/aporee_33170_38124) | Oliver's Mount road races, "125-400 cc practice two stroke", binaural | CC BY 3.0 | david m, via radio aporee ::: maps | `047e63b42c13787e` |
| [Oliver's Mount, Lightweight 400, Mere Hairpin](https://archive.org/details/aporee_33573_38630) | Same series, recorded at a hairpin — has the full off-throttle→on-throttle transition | CC BY 3.0 | david m, via radio aporee ::: maps | `0eb98f2eec5c4a50` |
| [Karting CCaroya 2010, jump start](https://freesound.org/people/rfhache/sounds/98210) | Colonia Caroya, Argentina — race start | CC BY 3.0 | rfhache | `3803907b46abf4fd` |
| [Go-kart racing outdoors](https://freesound.org/people/rodincoil/sounds/317470) | **Negative control** — four-stroke rental kart, measured 3,020 rpm | **CC0 1.0** | rodincoil | `0c96699ef4e0c4f4` |

The Freesound files are the **public lossy mp3 previews**, not the originals — the
original WAV/FLAC requires an account. Fine for measuring a harmonic ladder up to
a few kHz, not fine for anything that ships. `docs/REFERENCES.md` records that
caveat against the measurements that depend on it.

**Nine of these carry a share-alike or attribution obligation that would bind
shipped audio.** None is shipped, and nothing derived from them is either: what
crossed from these files into the repository is a set of *measurements* — decay
slopes, a comb delay, harmonic gain ratios — written into `docs/REFERENCES.md` as
prose and numbers. Measured facts about a recording are not a derivative work of
it. If a sample from any of these ever does ship, the CC BY-SA files
(Yamaha, Vespa, Garelli, both Wurstelprater) would make that audio share-alike,
and the CC BY files (all four Work With Sounds recordings) would require the
credit lines above to appear in-product. That is the reason to keep synthesizing.

The three Work With Sounds motorcycle recordings and the Stihl chainsaw are the
load-bearing ones — they are the only engines here with a documented specification
and a controlled, stationary recording. The Vespa is listed but **deliberately
unused**; see `docs/REFERENCES.md` for why a 125 cc scooter is the least
representative file in the set despite being the only one that matches a KZ's
displacement.

### Tire scrub — issue #84

The tire-scrub band in `src/core/kz_audio_reference.h` was measured off these
four. **All CC0 1.0**, so no attribution is legally required and it is recorded
anyway, because provenance is the thing that makes a measurement checkable.
`tools/assets/fetch_scrub_audio.sh` pulls them, verifies the hashes, and drops
them in a gitignored path — nothing here is committed.

| File | Source | Author | License | SHA-256 (first 16) |
|---|---|---|---|---|
| [`fs71736`](https://freesound.org/s/71736/) | Chrysler LHS tire squeal 01, Rode NTG2 into a Zoom H4 | audible-edge | **CC0 1.0** | `13d94db3651156da` |
| [`fs71737`](https://freesound.org/s/71737/) | Chrysler LHS tire squeal 02, same car and session | audible-edge | **CC0 1.0** | `43d890acfcb57798` |
| [`fs71740`](https://freesound.org/s/71740/) | Nissan Maxima standing burnout, same recordist | audible-edge | **CC0 1.0** | `20aeb47e6c8bc493` |
| [`fs173931`](https://freesound.org/s/173931/) | Indoor electric kart track — the **negative control**, no engine anywhere in it | JohnsonBrandEditing | **CC0 1.0** | `e1b445779c92e195` |

The truncation to sixteen hex characters matches the tables above; the full
digests are in `docs/REFERENCES.md` and in the fetch script, which is what
actually verifies them.

**Two sets of recordings were measured and deliberately not fetched or shipped,
and the reasons are worth keeping.** `fs481668` and `fs481678` are marked CC0 on
Freesound but are described by the uploader as transfers of Hollywood optical and
magnetic effects from the 1930s to the 1960s: a CC0 mark applied by a re-uploader
to third-party archival material is not a license. `fs233558`, `fs536769` and
`fs237312` are short library screeches with no stated provenance, gear or
vehicle. All five were measured for corroboration and appear in REFERENCES.md's
table marked unusable; none is fetched by the script and none contributed a
number to the header.

Three published sources are cited for this work as text rather than as assets —
Transport Infrastructure Ireland's CPX tables, Brown and Gordon (2011) and Lower
et al. (1994). Full citations are in `docs/REFERENCES.md`; nothing was copied
from them beyond figures transcribed with their uncertainty stated.

---

## Sources used

| Source | License | Notes |
|---|---|---|
| [Poly Haven](https://polyhaven.com) | CC0 | HDRIs, PBR materials, models. Unambiguous. |
| [ambientCG](https://ambientcg.com) | CC0 | PBR materials, scanned surfaces. Unambiguous. |
| [Wikimedia Commons](https://commons.wikimedia.org) | mixed, per file | Reference audio. License is per file and must be read per file — the twelve above span PD, CC BY 4.0, CC BY-SA 3.0 and CC BY-SA 4.0. The API reports `LicenseShortName` and `UsageTerms` per file and that is what the table records. |
| [Work With Sounds](https://workwithsounds.eu) | CC BY 4.0 | Industrial and vehicle sound archive, EU-funded, mirrored onto Commons. The only source found with documented engine specifications attached to each recording. |
| [radio aporee ::: maps](https://archive.org/details/radio-aporee-maps) | mixed CC, per file | Field-recording collection on archive.org. The richest source of real motorsport audio found. Note the collection identifier is `radio-aporee-maps`; searching `aporee` returns nothing. |
| [Freesound](https://freesound.org) | mixed, per file | No token needed for the `cdn.freesound.org/previews/…-hq.mp3` URLs, which Openverse indexes directly. Those are **lossy previews**; originals need an account. License is per file — CC0, CC BY and CC BY-NC all coexist there, and the NC ones are unusable. |

## Sources deliberately avoided

| Source | Reason |
|---|---|
| Fab / Quixel Megascans | Not CC0. Epic's terms have carried engine-use restrictions. License clarity matters more here than scan quality. |
| _(none — Freesound was expected to need a token and does not; see the sources table above)_ | |
| YouTube, karting vendor sites (tkart.it, mondokart, iamekarting) | No license grant compatible with reuse, and all three vendor sites return 403 to anything without a browser. This is where actual KZ engine audio lives and none of it is usable. |

## Software

| Tool | License |
|---|---|
| Godot Engine 4.7.1 | MIT |
| Jolt Physics (via Godot) | MIT |
| Blender 5.2 LTS | GPL — used as an offline tool; output is not a derivative work |
| godot-cpp (`third_party/godot-cpp`, submodule @ `9c7567d2`) | MIT — vendored as a submodule, so its source and history stay upstream rather than being copied in |
| SCons 4.10.1 | MIT — build tool only |
| doctest 2.4.12 (`third_party/doctest`, fetched by `tools/assets/fetch_doctest.sh`) | MIT — single header, gitignored and reproduced from the script's pinned SHA-256 rather than copied into the tree |
