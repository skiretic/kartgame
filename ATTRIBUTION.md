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
| _none yet_ | | | |

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
