## ADR-0054 — Scatter is placed from the track and lit by nothing baked

**Status.** Implemented for the placement half, ROADMAP M5, issue #182.
**Blocked** for the second half of the bake half, with the measurement below.
Extends [ADR-0046](DECISIONS.md#adr-0046--trackjson-owns-the-whole-track-and-furniture-is-placed-by-distance)'s
"furniture is placed by distance" from the circuit's own furniture to its
dressing, and adds a fifth entry to the ambient double-counting family that
[ADR-0021](DECISIONS.md#adr-0021) and
[ADR-0022](DECISIONS.md#adr-0022) started.

**Context.** M5 asks for "runtime scatter: seeded Poisson-disk props and
foliage" and "lightmap bake integration". `scenes/game/valdirone.tscn` rendered
as bare asphalt, verge, gravel and barriers on a flat grass plane, and three
things were wrong before any of that could be built:

* **The ground was flat and the circuit is not.** One `PlaneMesh` sat at the
  circuit's lowest point. Valdirone climbs 12.55 m, so at the top of the climb
  the road and its run-off were a ribbon hanging **12.05 m** in the air over a
  lawn. That is invisible while the only thing standing on it is the road, which
  stands on its own spline, and it is the first thing you see the moment
  anything is placed beside the track.
* **Nothing hand-placed survives a re-author.** The circuit is one command away
  from being regenerated, and the design's corners are still moving.
* **A lightmap needs a scene the editor can see.** `circuit.gd` builds
  everything at run time, and `LightmapGI` bakes what is in the tree in the
  editor. `bake_test.gd` recorded that constraint at M1 and said it would carry
  through to M5. It did.

**Decision.**

**One keep-out table, two consumers.** `scripts/track/track_corridor.gd` walks
the lap and records how far the built circuit reaches at each station — road
half-width, plus the 1.80 m verge, plus any corner's apron and outfield over the
corner and 30 m either side. Terrain and scatter both read it. It is indexed by
**forward** distance and read through `to_forward`, so it describes the road
rather than the layout: a table indexed by layout station moved four shrubs, one
tree and the terrain's worst step between the two layouts, all of it from the
1 m quantization landing on different physical points.

**The ground is a height field taken off the circuit.**
`scripts/track/track_terrain.gd` samples `project` and `sample` on a 5 m grid:
inside the corridor the ground sits 0.25 m under the road, and beyond it relaxes
to the base plane over 90 m. The old box slab stays underneath as the tunneling
backstop it always was, sunk 1 m so the two are never coplanar. Four Jacobi
smoothing passes with corridor vertices **pinned** — pinned because an unpinned
pass drags the shoulder up through the run-off apron, which is a hill on the
racing line, which is a scatter change that moves a lap time.

**Everything is placed from `(station, side, lateral offset)` and a PCG32
stream.** `src/core/pcg32.h` through `KartRandom`, seeded from the track's
content hash, one stream per class per ARCHITECTURE.md §8 rule 3. Trees and
shrubs are dart-thrown Poisson-disk; marshal posts, braking boards and tire
stacks come off the corner list. Nothing carries a collider.

**Nothing may be inboard of the corridor, tested against the *nearest* part of
the lap.** Not the station it was drawn from. Valdirone's closest approach is
18.32 m of clear ground and its widest run-off reaches 47.8 m, so the two
overlap: measured before the test existed, the worst-placed tree was **34.97 m
inside** T8's gravel, having been drawn 6 m outboard of the straight beside it.

**The scatter is `MultiMeshInstance3D`, so it can never be baked.** Seven draw
calls against 5,187. `LightmapGI` walks `MeshInstance3D` and nothing else, so
the trade is deliberate and its consequence is that scatter takes indirect light
from probes.

**The bake gets its own scene.** `scenes/game/valdirone_bake.tscn` and
`tools/bake/circuit_bake.gd` build the circuit's static half — environment, sun,
the generated `.glb`, the terrain — under `@tool`. Making `circuit.gd` itself
`@tool` would construct a `KartBody`, a `PlayerDriver`, a `SessionRunner`, three
cameras and an audio rig every time the editor opened the scene.

**And the driveable scene defaults to `--gi=none`.** Measured, not cautious; see
below.

**What was measured.**

The bake works, on a circuit this size, driven by the existing `bake.sh` path:

| | |
|---|---|
| Preflight | 9 bakeable meshes, 4.4 Mtexel, no failures |
| Bake time | **38.1 s** at quality `high`, 2 bounces |
| Output | 2048x2048, 2 slices, 6.9 MB `.exr`, 42 KB `.lmbake` |
| Static geometry, unlit vs baked | far band 89.1 -> 96.0, near ground 46.9 -> 54.1 mean channel value |

What does not work is putting anything that **moves** in the same scene. At one
reference camera, mean channel value per band:

| | sky | far | near ground |
|---|---|---|---|
| no lightmap | 120.3 | 89.1 | 46.8 |
| lightmap, static geometry only | 120.3 | 96.0 | 53.8 |
| lightmap, scatter on probes | 193.7 | **253.9** | **210.6** |

253.9 of 255 is clipped. Turning off the reflection probe changes none of those
three numbers; removing the scatter fixes all of them. The same happens to the
kart. **A dynamic object's indirect light comes from the `.lmbake`'s probe field,
that field holds unnormalized physical radiance, and under physical light units
it arrives about four orders of magnitude too bright.**

`LightmapGI.camera_attributes` is the only lever on baked exposure and it does
not reach the probes. Baking with it set moved the *texture* path — the far band
from 96.0 to 126.6 — and left the probe path **byte-identical** at 253.9. So
there is no value of it that makes both paths right, which is the decisive
result: this is not a setting that was got wrong.

**Consequences.**

- Placement is deterministic and it is measured rather than asserted:
  `tools/verify/scatter_probe.gd` dumps all 5,187 transforms and two separate
  processes produce **byte-identical** files.
- Scatter is **layout-invariant**. The 5,135 objects that are physically part of
  the place — trees, shrubs, tire stacks, marshal panels — are byte-identical
  between the forward and reverse layouts. Braking boards deliberately are not:
  a board is read on the approach, so it belongs to the direction of travel.
- Re-authoring the circuit moves the scatter with it, because the seed is the
  track's content hash and every position is a function of the spline.
- **The scatter has no colliders**, so a kart that clears a barrier drives
  through a tree. Deliberate: a new static body beside a racing line is a new way
  to move a measured figure, for a decoration. Ticket, not prose.
- The lightmap is committed as `--gi=baked` and left off by default, so nothing
  regresses and the work is not thrown away. `valdirone_bake.tscn` renders it
  correctly today and is the demonstrator.
- `tools/bake/preflight.gd` grew no code and earned its keep: it caught that
  every mesh in the `.glb` arrives with `lightmap_size_hint` at zero, which bakes
  at 64x64 whatever the mesh's real size.

**Two defects found in files this work does not own, reported rather than
fixed.**

1. **`snake_uv2`'s two axes are swapped**, `tools/blender/tracklib/surfaces.py`.
   Its docstring says ten rows make the lap "square-ish per texel". Measured off
   the shipped `.glb` by solving the tangent basis per triangle: one UV2 unit
   spans **11.5 m** across the asphalt and **1,393 m** along it — a texel aspect
   of **120.9 : 1**, which is exactly what an unsnaked 1,375 x 12 m ribbon would
   give. The function returns `(u, v)` as `(across, along)`; the layout it
   describes needs `(along, across)`. Swapping them gives 1.15 : 1. Until then,
   a square atlas sized for a sane density along the lap spends about 113x the
   texels to get there, and the shipped bake resolves 2 m along the lap and
   1.7 cm across it. Verge is 60.5 : 1, Kerbs 47.5 : 1, Barriers 1869 : 1.
2. **`KartTrack.corner()` does not publish `runoff.side` or `runoff.barrier`**,
   `src/track/kart_track.cpp`. The collider builds run-off on one side; every
   other consumer has to reserve it on both, which costs scatter on the inside of
   six corners and is a duplication waiting to drift.
