# Circuit designs

**These are designs, not tracks.** Nothing here is loaded by the game. `track.json`
is [ADR-0046](../DECISIONS.md#adr-0046--trackjson-owns-the-whole-track-and-furniture-is-placed-by-distance)'s
schema and does not exist yet; ROADMAP M5 (#63) writes it. What is here is the
geometry and the argument a `track.json` would be authored *from*, kept under
version control because the reasoning cost more than the file.

## What is in here

| Path | What |
| --- | --- |
| `valdirone_nuova.json` | **The approved-pending design.** Segments, corners, overtaking, elevation schedule, reverse layout, sectors, grid, risks |
| `valdirone_nuova.md` | The design argument and the corner-by-corner walkthrough |
| `valdirone_nuova_centerline.csv` | The walked centerline, 1 m spacing: `distance_m,x_m,y_m,elevation_m,segment_index` |
| `valdirone_nuova_speed.csv` | The quasi-static speed profile that proves the lap and sector times |
| `valdirone_nuova_svg.json` | Generated. Plan and profile path data for the review page |
| `candidates/` | The four layouts this was chosen from, their adversarial verifications, and the three judge scorecards |

## How it was made, and why that matters more than the result

Four layouts were designed **to the same sourced constraint set** — the CIK-FIA
figures in [`REFERENCES.md`](../REFERENCES.md), *Circuit design and regulation* —
each from a different lens: an instrument where every corner provokes one named
sim feature, a racecraft layout designed backwards from where a pass happens, a
terrain-led layout where the landform generates the plan, and a rhythm layout
treating the lap as a composition. Each was then **verified adversarially** by a
reader whose job was to catch numbers the author had not measured, and scored by
three judge panels: regulation and buildability, simulation value, racing quality.

That structure earned its cost. Every one of the four came back `major issues`,
and the dominant fault was the same in all four: **the racing-line radius was
computed as roughly twice the centerline radius** instead of derived from the
corridor. The true geometric optima ran up to 103% higher, and everything
downstream inherited it — apex speeds, lap times, sector splits, and both racing
arguments. In one candidate the error was large enough that *the defender came
out faster than the attacker in its own frame*, which invalidated its whole
overtaking case while reading as a confident set of numbers.

The correction is in `valdirone_nuova.json` and it is the reusable part:

    rho = R + h(1 + cos(theta/2)) / (1 - cos(theta/2)),   h = (W - 1.400) / 2

`R` centerline radius, `theta` total turn angle, `W` road width, and the 1.400 m
is the kart's own width — FIA Karting Art. 8.1.1's overall maximum, which this
kart's rear track already is. It reproduces **45 of the 49 line radii the four
independent verifiers measured, to within 0.1 m**. Any future layout must use it
rather than a cap, and must publish `line_radius_m` per corner so the check is
possible at all.

## Regenerating the drawings

    python3 tools/circuits/render_circuit.py     # centerline CSV -> *_svg.json
    python3 tools/circuits/build_page.py         # -> valdirone.html, the review page

Both read `docs/circuits/` and take `CIRCUIT_STEM` to point at a different design.
`build_page.py` also takes `CIRCUIT_OUT`. The page draws the asphalt from the
walked centerline at each segment's real width, so a widened passing corner is
*geometry* on the map and not a caption — one candidate described its widenings
only in prose, and every check it published had therefore been run against
constant widths.

## Status

- **Valdirone Nuova** — 1,375.13 m, 8 corners, 12.55 m of elevation, 44.82 s.
  Designed, verified, approved and **built**. ROADMAP M5 authored it into
  [`data/tracks/valdirone_nuova.track.json`](../../data/tracks/valdirone_nuova.track.json)
  with `tools/circuits/author_track.py`; it is driveable both ways at
  `scenes/game/valdirone.tscn` and measured by `tools/verify/circuit.sh`. Every
  corner's radius, angle and racing-line radius came through exactly; the lap is
  12 mm shorter than the design's own figure because the closure was re-solved
  from the published two-decimal straight lengths rather than the gate being
  loosened, and both are inside the regulation's 1 m accuracy.

  **One thing this file's own derived artifact got wrong, recorded because it will
  waste somebody's afternoon otherwise.** `valdirone_nuova_centerline.csv` drifts
  from the exact walk of the segment list by up to **0.49 m**. Every radius and
  every turn angle in it is right — circle fits reproduce 57.000, 42.000, 14.999,
  22.000, 32.000 and 70.000 m — but its generator advanced a quarter of a meter at
  every corner entry: the CSV's T1 arc centre is at (−56.9995, 87.7501) where the
  segment list puts it at (−57, 88), one 0.25 m step short, and the error
  accumulates corner by corner. The **segment list is normative** — this file
  already says it is the thing a reader can reproduce — and `author_track.py`
  does not read the CSV.
- **Circuit 2** — not designed. `GAMEDESIGN.md` §10 wants two circuits, each with
  an authored reverse layout, to fill a four-round calendar; Valdirone's reverse
  is designed, so this is two of the four. `candidates/instrument.json`
  (*Pietrarossa*, 10 corners, ranked first by the racing judge) is the strongest
  starting point and needs the line-radius correction applied before it is
  comparable.

## Open, and owed

- **Grid slot spacing is unsourced, and it stays unsourced.** CIK-FIA Appendices
  9, 10 and 15 are referenced by the Part 1 text and are not public. M5 did not
  wait for them: four figures were picked, derived from clearances a reader can
  check, and recorded as **ours** in `src/core/circuit_reference.h`'s
  `circuit::ours::` namespace with a `_SOURCED = false` on each.
  [ADR-0050](../DECISIONS.md#adr-0050--the-starting-grid-is-ours-and-it-is-namespaced-so-it-reads-that-way)
  has the derivation table and says what changes if the appendix turns up.
- **There is no slipstream model in the project**, so the tow that makes the T1
  pass work does not exist. That is a vehicle gap, not a track gap, and it is the
  reason one of Valdirone's two overtaking places is weaker than it reads.
- Six of Valdirone's eight corners are steering-lock-limited, so they rest on
  `drive_probe.gd`'s lock sweep — which confounds lock with throttle, because
  every row varies both.
