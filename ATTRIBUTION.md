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

## Audio

| Asset | Source | License | Imported |
|---|---|---|---|
| _none yet_ | | | |

---

## Sources used

| Source | License | Notes |
|---|---|---|
| [Poly Haven](https://polyhaven.com) | CC0 | HDRIs, PBR materials, models. Unambiguous. |
| [ambientCG](https://ambientcg.com) | CC0 | PBR materials, scanned surfaces. Unambiguous. |

## Sources deliberately avoided

| Source | Reason |
|---|---|
| Fab / Quixel Megascans | Not CC0. Epic's terms have carried engine-use restrictions. License clarity matters more here than scan quality. |

## Software

| Tool | License |
|---|---|
| Godot Engine 4.7.1 | MIT |
| Jolt Physics (via Godot) | MIT |
| Blender 5.2 LTS | GPL — used as an offline tool; output is not a derivative work |
| godot-cpp (`third_party/godot-cpp`, submodule @ `9c7567d2`) | MIT — vendored as a submodule, so its source and history stay upstream rather than being copied in |
| SCons 4.10.1 | MIT — build tool only |
| doctest 2.4.12 (`third_party/doctest`, fetched by `tools/assets/fetch_doctest.sh`) | MIT — single header, gitignored and reproduced from the script's pinned SHA-256 rather than copied into the tree |
