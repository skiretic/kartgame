# `track.json` — the schema

[ADR-0046](DECISIONS.md#adr-0046--trackjson-owns-the-whole-track-and-furniture-is-placed-by-distance)
decided that one file owns everything a track is. This is that file's schema,
written when ROADMAP M5 implemented it. `ARCHITECTURE.md` §11 has the principle —
one definition, three consumers — and `docs/circuits/README.md` has the design
work a `track.json` is authored *from*.

Three readers, and none of them may disagree with another:

```
data/tracks/*.track.json  ──┬──►  tools/blender/gentrack.py   visual mesh, curbs,
                            │                                 run-off, barriers, UVs
                            ├──►  src/track/kart_track.cpp    collision, checkpoints,
                            │                                 grid, projection
                            └──►  tools/verify/circuit.sh     the validation pass
```

The two geometry consumers read the **same control points** through the **same
interpolation rules**, which is why those rules are stated here in arithmetic
rather than described. `src/core/track.h` is the executable copy of this document
and it is engine-free, so the rules can be unit-tested without a rendering server.

---

## Frames and units

Everything is meters, degrees where an angle is authored for a human, and
**Godot's coordinate frame**: Y up, −Z forward.

    position           [x, z]      the ground plane, Godot's own
    heading 0                      points down −Z
    heading positive               turns right, toward +X
    curvature positive             a right-hand corner
    elevation                      y, positive up
    grade positive                 climbing in the direction of travel
    bank positive                  the road falls to the right of travel

ADR-0046 settled the frame and the reason: the kart pipeline builds toward
Blender's +Y and `export_yup` converts on export, and doing the same here would
put a second conversion inside the file the *runtime* reads. `gentrack.py`
converts once, on read. The runtime does not convert at all.

The design documents under `docs/circuits/` use a plan frame with +Y forward. The
mapping is `x_godot = x_design`, `z_godot = −y_design`, headings and turn signs
unchanged — `tools/circuits/author_track.py` is where that conversion happens and
it is the only place it happens.

---

## Top level

```json
{
  "schema_version": 1,
  "meta":      { ... },
  "spline":    [ ... ],
  "corners":   [ ... ],
  "elevation": { ... },
  "surfaces":  [ ... ],
  "furniture": { ... },
  "layouts":   [ ... ]
}
```

**`schema_version` refuses rather than migrates.** Opposite to
[ADR-0042](DECISIONS.md#adr-0042--a-save-always-loads-and-the-migration-tests-eat-real-old-files),
and for the same reason it applies there in reverse: a save is user data and a
track is authored project data under version control. A schema bump is a commit
that edits the tracks in the same commit, and a loud failure is correct.

---

## `meta`

| Field | Type | Meaning |
| --- | --- | --- |
| `name` | string | Display name |
| `length_m` | number | Centerline plan length, the walk's own total |
| `grade` | int | CIK-FIA circuit licence grade the layout is validated against |
| `net_turn_deg` | number | Sum of every control point's turn; ±360 for a closed loop |
| `designed_from` | string | Path to the design document, for provenance |
| `authored_by` | string | The tool and version that emitted the file |

`length_m` is a **checksum, not an input**: the loader walks the spline and
rejects the file if the walk disagrees by more than a millimeter.

---

## `spline` — the control points

An ordered list, one entry per control point, closing back onto the first. Each
entry describes the point *and the span that leaves it*:

```json
{
  "distance_m":    88.0,
  "position":      [0.0, -88.0],
  "heading_deg":   0.0,
  "curvature_1pm": -0.017544,
  "width_m":       12.0,
  "crown_pct":     0.0,
  "bank_pct":      -5.0,
  "elevation_m":   0.6927,
  "grade_pct":     0.787,
  "segment":       1,
  "note":          "T1 - Ronda, turn-in"
}
```

### What is normative and what is a checksum

**`distance_m` and `curvature_1pm` are normative.** The span from a control point
to the next is a circular arc of constant curvature, of length equal to the
difference of their `distance_m`; the last span wraps to `meta.length_m`. Position
and heading are *derived* from that walk.

**`position` and `heading_deg` are a checksum.** They are written so the file can
be read, diffed and plotted without running the walk, and the loader **verifies**
them: 1 mm on position, 1e-4 rad on heading. A hand edit that moves a control
point without fixing the curvature is a load failure, not a track that quietly
bends somewhere else.

That split is deliberate. Storing positions alone would make every corner a
polyline and put the radius of a 15 m hairpin at the mercy of a sampling rate;
storing curvature alone would make the file unreadable. Storing both, with one of
them checked against the other, costs a hundred lines of validator and buys a
format a person can edit.

### Interpolation, in arithmetic

Along a span of length `L` from control point `a` to control point `b`, with
`t = (d − a.distance_m) / L`:

**Plan geometry** — exact arc, never a chord:

    turn      = a.curvature_1pm * L * t
    heading   = a.heading + turn
    if a.curvature_1pm == 0:
        position = a.position + forward(a.heading) * (L * t)
    else:
        r      = 1 / a.curvature_1pm            signed; the center is r to the right
        center = a.position + right(a.heading) * r
        position = center − right(heading) * r

**Width, crown and bank** — linear in arc length:

    width = lerp(a.width_m, b.width_m, t)

A taper is therefore geometry and not a caption: a corner that widens from 11 m
to 14 m over its last 25 m is two control points 25 m apart with two widths. This
is what makes `min_clear_ground` checkable at all, and one of the four candidate
circuit designs published every check it had run against constant widths while
describing its widenings only in prose.

**Elevation** — cubic Hermite on `(elevation_m, grade_pct)`:

    h00 =  2t³ − 3t² + 1        h10 =  t³ − 2t² + t
    h01 = −2t³ + 3t²            h11 =  t³ − t²
    y   = h00*a.elev + h10*L*a.grade + h01*b.elev + h11*L*b.grade

Hermite and not linear, and the reason is exact rather than aesthetic: a
regulation vertical curve is a **parabola**, and cubic Hermite through a
parabola's two endpoints with its two endpoint slopes reproduces that parabola
*identically* — a parabola is a cubic and the interpolant is the unique cubic
matching four constraints. So the three vertical curves of a circuit need six
control points between them and are then exact everywhere, rather than needing a
control point every few meters and still being a polyline.

Linear elevation would need ~5 m spacing to hold the sag under the road's own
2 mm lip, and would hand the suspension a 0.26% grade step every 5 m — 0.10 m/s
of vertical velocity arriving in one tick at 140 km/h, which is a bump the road
does not have.

`grade_pct` is redundant with the elevations on a constant-grade band and is
**not** a checksum there: it is the interpolant's second input and it is what
makes a band straight instead of gently wavy.

### Cross-section

`crown_pct` and `bank_pct` are two different shapes and a control point carries
both:

* **`crown_pct`** is a roof — both edges sit `crown_pct/100 × halfwidth` below the
  centerline. This is drainage camber and CIK-FIA Part I §7.2 requires
  1.5–3% of it on a straight. A straight is never flat.
* **`bank_pct`** is a single plane — the road falls from one edge to the other,
  positive to the right of travel. §7.2 caps it at 10% and says adverse banking is
  "not generally acceptable"; the validator rejects a bank whose sign opposes the
  turn.

The road's surface height at a lateral offset `u` from the centerline, positive to
the right of travel, is the one formula both consumers implement:

    y(u) = elevation − (crown_pct/100)·|u| − (bank_pct/100)·u

so `y(0)` is the control point's own elevation, a crown falls away on both sides,
and a positive bank falls to the right. It is piecewise linear in `u`, which is
why three columns of vertices — `−w/2`, `0`, `+w/2` — reproduce it exactly rather
than approximately, in the mesh and in the collider both.

A corner has bank and no crown; a straight has crown and no bank; the transition
between them is the linear interpolation above, over
`circuit::ours::BANKING_TRANSITION_M`. That length is **ours** — the regulation
says changes are "to be made over an appropriate distance" and gives no distance —
and it is recorded in `src/core/circuit_reference.h` with the rest.

---

## `corners`

A corner is **not** a control-point span. It is a direction change, and the
distinction is load-bearing: Valdirone's T5 Vigna is a 24° arc into a 71° arc with
no straight between them, and a validator reading the spline alone sees two spans
that both clear the 80° run-off threshold when the corner is 95° and mandatory.

```json
{
  "name": "T5 - Vigna",
  "from_m": 796.25, "to_m": 848.64,
  "hand": "left",
  "direction_change_deg": 95.0,
  "min_radius_m": 22.0,
  "line_radius_m": 44.21,
  "line_radius_construction": "rho = R + h(1+cos(theta/2))/(1-cos(theta/2)), h = (W - 1.400)/2",
  "runoff": { "side": "left", "apron_m": 10.0, "outfield_m": 18.0,
              "outfield": "grass", "barrier": "tire",
              "sized_for": "reverse", "approach_kmh": 123.6 }
}
```

`line_radius_m` is **required on every corner** and it is the number the
"no radius the kart physically cannot take" gate is run against. It is not the
centerline radius and it is not `R + W/2`:

    rho = R + h (1 + cos(theta/2)) / (1 − cos(theta/2)),   h = (W − 1.400) / 2

`R` is the tightest arc of the corner, `theta` the whole direction change, `W` the
road width, and 1.400 m is the kart's own width — FIA Karting Art. 8.1.1's overall
maximum, which this kart's rear track already is. The multiplier is 16.70 at 55°,
6.61 at 85° and exactly **1.00 at 180°**, so a corner is a direction change and not
a radius: below about 80° nothing in a 9–14 m road can be slow, and at 180° the
line cannot open at all.

**Nobody drives the centerline.** Every corner on a driveable circuit exceeds the
kart's 1.86 g on its own centerline radius at its own apex speed — Valdirone does
it at all eight — so a validator written from ARCHITECTURE §11's words alone
rejects every circuit that has ever worked. The check is against `rho`, and the
threshold against `rho` is `min(grip, lock)` and not 1.86 g.

---

## `elevation`

The longitudinal profile's *declaration*, cross-checked against the spline rather
than duplicating it.

```json
{ "vertical_curves": [
    { "at_m": 261.29, "profile": "convex", "K": 15.0,
      "grade_in_pct": 0.787, "grade_out_pct": -4.6,
      "radius_m": 1900.0, "length_m": 102.36 } ] }
```

The loader recomputes each curve's start and end from `at_m ± length_m/2`, reads
the spline's own grade there, and rejects the file if the two disagree. That is
the check that catches an elevation edited in one place and declared in another.

`K` does **not** swap when the layout reverses. Vertical curvature `d²z/ds²` is
invariant under `s → L−s`, so a crest is a crest driven either way and only the
speed changes; the gate is `R ≥ max(V_forward, V_reverse)² / K`. Two of the four
candidate circuit designs got this wrong in opposite directions.

---

## `surfaces`

Spans of something that is not plain asphalt, placed by arc length and by side.

```json
{ "from_m": 344.31, "to_m": 368.31, "side": "left",
  "type": "curb", "width_m": 1.0, "height_m": 0.030, "profile": "vertical",
  "note": "T2 Lama, the reference kerb - the only one struck above 120 km/h" }
```

| Field | Values |
| --- | --- |
| `side` | `left`, `right`, `full` — relative to the direction of travel of the **forward** layout |
| `type` | `asphalt`, `curb`, `grass`, `dirt` |
| `width_m` | outboard of the asphalt edge for a curb; ignored for `full` |
| `height_m` | curbs only, above the road surface |
| `profile` | `flat`, `rippled`, `vertical` |

The default everywhere is `asphalt` at the spline's own width, so the array
carries only departures. Surface *types* are `kart::core::SurfaceType`'s
integers at the far end — that header calls them a wire format — and the strings
here map to them in exactly one place, `src/track/kart_track.cpp`.

**Curb geometry is shared between layouts even where the design wanted two.** The
geometric inside of a bend is the same physical edge whichever way it is driven,
so every apex curb survives a reversal untouched; an *exit* curb does not, and
Valdirone's design specifies T8a's inside kerb as 30 mm rippled forward and 25 mm
flat reversed. One piece of concrete cannot be two heights. It is built at the
gentler of the two, which is the same rule the design already applies to run-off —
the binding direction drives the build — and it is recorded in ADR-0049 rather
than resolved silently.

---

## `furniture`

What is the same in every layout: the start line, the light gantry, and the pit
lane's parallel run. Everything that is *not* — grid, sectors, checkpoints, the
four pit junctions — belongs to a layout.

```json
{ "start_line": { "distance_m": 0.0, "width_m": 0.30 },
  "lights":     { "distance_m": 10.0, "height_m": 3.0, "span_m": 12.0 },
  "pit_lane":   { "side": "left", "from_m": 1304.5, "to_m": 70.619417,
                  "width_m": 3.5, "separation_m": 3.2 } }
```

`pit_lane.side` is in the **forward** frame, the same convention `surfaces[].side`
uses, because there is one meaning of "left" in this file and it is the forward
layout's.

---

## `layouts`

Two entries for Valdirone: `forward` and `reverse`. ADR-0046: **reverse is an
authored layout, not a programmatic reversal.** Flipping a spline is trivial and
everything attached to it is not — run-off is sized for an approach speed that
changes, a sector split that made sense one way lands mid-corner the other, and a
pit lane's deceleration lane at 20° to the direction of travel is a 160° merge
driven backwards, over Part I §7.4's 30° cap, on either edge.

```json
{
  "name": "reverse",
  "direction": "reverse",
  "sector_marks_m":  [473.0, 862.0],
  "checkpoints_m":   [0.0, 98.22, ...],
  "grid": { "slots": 8, "pole_side": "right" },
  "pit_entry_m": 1305.0,
  "pit_exit_m": 62.0,
  "pit": { "side": "right", "entry_angle_deg": 22.0, "exit_angle_deg": 16.0 },
  "racing_line_seed": [ { "at_m": 77.0, "lateral_m": 5.3 }, ... ],
  "corner_speeds_kmh": [ ... ],
  "vertical_curve_speeds_kmh": [ 132.5, 52.0, 122.0 ]
}
```

**Every distance in a layout is a station in that layout's own direction**, zero
at that layout's start line, increasing the way that layout is driven. For the
reverse layout `station = (meta.length_m − forward_distance) mod length`. The
loader converts once, on selection, and nothing downstream knows which way it is
facing. Writing reverse furniture as forward distances was tried and is the kind
of thing that reads correctly and puts a sector mark on the wrong side of a corner.

### `sector_marks_m` and `checkpoints_m` are two lists and the timer keeps them apart

They answer different questions and neither substitutes for the other.
`sector_marks_m` is **where the timing screen splits the lap** and it excludes the
start line, because a mark at 0.0 would be the line twice — two entries make three
sectors. `checkpoints_m` is **the anti-cut resolution**, normally including 0.0,
spaced under the 100 m ADR-0050 names as ours rather than the FIA's.

At load they merge into one ordered set of marks. **Every mark has to be crossed in
order or the lap was cut; only a sector mark may produce a split.** The arithmetic,
because the two consumers share no code:

* Take the union of `{0.0}`, `sector_marks_m` and `checkpoints_m`, sorted
  ascending.
* Two stations within **1 mm** are one mark, and the sector mark's station wins.
  Same figure and same reason as the coincident-control-point rule above: a
  checkpoint half a millimeter short of 524.0 that became the split would put the
  timing screen a hair off the number the design specifies.
* A mark that is in `sector_marks_m` — or is the start line — opens a sector. Every
  other mark is crossed for its ordering alone and does not touch the sector clock.

Valdirone's forward layout merges 2 splits and 14 checkpoints into **16 marks in 3
sectors**. `src/core/lap_timing.h` is the executable copy, ADR-0051 is the
decision, and `tools/verify/circuit.sh --case=timing` is the gate — it walks both
layouts at a constant speed, where each split is the authored station over that
speed in closed form, so an implementation that measured thirds cannot pass it.

`racing_line_seed` is what M7's line solver starts from, not a racing line: a
lateral offset per corner from the `line_radius_m` construction above, positive to
the right of travel. It is a seed because minimum-curvature and maximum-apex-radius
are **different curves** — on a clean 90° corner of 57 m radius through 12 m of
road the closed form gives an 87.89 m apex radius and a minimum-curvature solver
gives 66.98 m, and both are right. A future session that feeds a minimum-curvature
line into this document's speed model will get corner speeds 15–25% low and will
not know why.

---

## Pit geometry, in arithmetic

The pit lane is the one thing in this file that is **geometry and does not
reverse**, and it is the reason ADR-0046's "reverse is an authored layout" is not
just a convenience. A deceleration lane leaving at 22° to the direction of travel
is a **158° merge** driven the other way, over §7.2's 30° cap, on either edge. So
the *lane* is shared and the four *junctions* are not.

Two implementations, `src/core/track.h`'s `pit_stubs` and
`tools/blender/tracklib/geometry.py`'s, and `circuit.sh --case=pit` measures them
against each other. Everything below is stated as arithmetic for that reason.

### The three sourced figures and the one that is ours

| Figure | Value | Source |
| --- | --- | --- |
| Merge angle, entry and exit | ≤ **30°** | Part I §7.2 |
| Deceleration lane width | **3–4 m** | Part I §7.4 |
| Chicane at the entry to the deceleration lane | required, **no geometry given** | Part I §7.4 |
| Clear ground between track edge and lane edge | **3.20 m** | **ours** |

The 30° cap is in **§7.2**, not §7.4 — this project cited it as 7.4 in five places
before the text was read line by line. §7.4 is where the 3–4 m and the chicane are.

3.20 m is `circuit::ours::PIT_LANE_SEPARATION_M`, and like every figure in that
namespace it is a sum of sourced numbers rather than a choice: §7.5's **1.80 m of
mandatory verge**, which the pit lane may not eat, plus FIA Karting Art. 8.1.1's
**1.400 m kart width**, so a kart that lands squarely on its own verge is still
short of the lane. The servicing-park plan that would give a real figure is
**Appendix No. 9**, referenced by §7.4 and not published — the same hole as the
grid's Appendix 10 in ADR-0050, probed again for this issue and still a 404.

### Which edge a junction goes on

§7.2 again: the two lanes must meet the track *"in such a way that there may be no
crossing between the lines of karts that are on the track and those of karts that
enter the Repairs Area or leave it."*

    entry junction side = hand of the corner most recently LEFT
    exit  junction side = hand of the corner about to be ENTERED

because a kart tracks out to a corner's **outside** and sets up on the **outside**
of the next one, so the free edge is that corner's own **inside**, which is its
hand. Valdirone forward: T8b and T1 are both lefts, both junctions go left.
Reversed: both are rights, both junctions go right — **the same physical edge**,
which is why one pit lane serves both and only the gores are per layout.

### The gore

A junction is a **gore**: the wedge of asphalt between the white line and the pit
lane's inner edge. Not a full-width ribbon laid over the lane — built that way the
two occupy one band for the length of the taper, and coplanar collider faces along
a boundary make a suspension raycast's answer arbitrary.

The taper length is **derived, never authored**, because the angle is the
regulated quantity and an authored length is a second place for it to be wrong:

    taper = separation / tan(angle)

which is 7.9203 m at 22° and 11.1597 m at 16°. Then, with `sign = −1` for a
reversed layout and `+1` otherwise, and `hand` the layout's own side times `sign`:

    junction_m = to_forward(layout, pit_entry_m or pit_exit_m)
    outboard_m = wrap(junction_m + (entry ? +1 : −1) * sign * taper)

An entry gore opens *ahead* of its junction and an exit gore closes *into* it, and
both flip again with the layout — which is exactly the four different signs the
two consumers have to agree on.

At a forward station `d`, with `t` the fraction of the gore's own signed span:

    t         = clamp(signed_gap(junction_m, d) / signed_gap(junction_m, outboard_m), 0, 1)
    u_inner   = hand * half_width(d)
    u_outer   = hand * (half_width(d) + separation * t)

`signed_gap` and not a subtraction: one of Valdirone's four gores sits eleven
meters the far side of the start line, and `b − a` there is −1,364 m, which runs
the taper backwards round the whole circuit.

### The lane

    u_inner = hand * (half_width(d) + separation)
    u_outer = hand * (half_width(d) + separation + width)

over the arc `from_m → to_m`, wrapping. Because the gore's offset never exceeds
`separation` and the lane's never falls below it, the two are adjacent bands and
**cannot overlap** — which is what lets both be built unconditionally, for both
layouts, with no selected-layout state anywhere in either consumer.

### Sampling

Both consumers step the pit geometry on **its own stations**, not off the shared
polyline. The polyline is subdivided to a chord tolerance, so its samples land
where the sagitta rule puts them; a 7.92 m gore snapped to that grid starts most of
a meter past its own junction, which draws a wedge of asphalt beginning in the
middle of the verge. The step count is the same rule in both:

    steps = max(1, ceil(span / max_spacing))      max_spacing = 2.0 m

so the lane is 71 cells and an entry gore is 4. `y` comes from the same
cross-section formula the road uses, so the lane continues the track's transverse
profile outward exactly as §7.5 requires of the verge it sits beyond.

### What is not built

**The chicane §7.4 requires at the entry to the deceleration lane.** The
regulation requires one and gives no geometry for it anywhere — not a length, not
an offset, not a width. Inventing one would put a fifth unsourced figure in a
namespace built to keep them countable, so it is recorded as required-and-absent
rather than filled in. Issue #184.

---

## Validation

Part of the schema, not a later pass, and the loader **refuses** rather than
warning. A track that loads and cannot be raced is worse than one that will not
load. `src/core/track.h` owns every rule below and
`tools/verify/circuit.sh` is the gate.

### Geometry

1. `schema_version` equals the loader's, exactly.
2. At least three control points; `distance_m` strictly increasing; the first is 0;
   all are below `meta.length_m`.
3. The walk closes: the last span's endpoint is within **1 mm** of the first
   control point, and the accumulated turn is within 1e-4 rad of a whole number of
   turns.
4. Every stored `position` and `heading_deg` agrees with the walk to 1 mm / 1e-4 rad.
5. `meta.length_m` agrees with the walk to 1 mm.
6. **No self-intersection.** For every pair of stations more than
   `max(100 m, both corners' arc lengths)` apart along the lap, the plan distance
   between them is at least `6 + h_a + h_b` — six meters of *clear ground* plus both
   half-widths, per Part I §7.5. A flat 14 m constant is only correct at the 8 m
   width floor and passes an illegal layout anywhere wider.
7. **The vertical companion to the same rule.** Where two such sections pass within
   the horizontal check's own margin, the ground slope between them must not exceed
   10% — §7.5's own cap on run-off up-slope — or a retaining structure must be
   declared. One candidate design cleared the horizontal rule at 20.56 m while the
   same two sections were 8.09 m apart *vertically* across 10.56 m of ground: a
   76.6% face where the regulation requires 1.80 m of non-negative-slope verge.

The 100 m window in rule 6 is not the 40 m the earlier brief used. A 180° corner of
15 m radius has 47.1 m of arc, so its own entry and exit tangents are "more than
40 m apart along the lap" and 30 m apart in plan — and that self-pair was the
reported minimum of all four candidate designs. Four for four.

### The kart

8. Every corner declares `line_radius_m`, and the layout's apex speed for it is at
   or below `min(grip ceiling, lock ceiling)` computed **against that radius**, not
   against the centerline.
9. Width is at least the grade's floor everywhere — 8.0 m for Grade 1.
10. `crown_pct` is within 1.5–3% wherever curvature is zero; `|bank_pct|` ≤ 10%;
    no bank opposes its own turn.

### The regulation's own dimensions

11. Lap length at or above the grade's minimum.
12. Starting straight within 120–200 m and its gradient at or under 2%.
13. No straight longer than 200 m.
14. Start line to first corner ≥ 50 m; last corner to start line ≥ 70 m.
15. Every corner whose direction change exceeds 80° declares a run-off.
16. Every vertical curve's radius is at least `max(V_forward, V_reverse)² / K`,
    with `K` taken from the curve's own profile and **not** swapped for the reverse
    layout.

### Furniture

17. Grid slots fit: `|lateral| + 0.700 + 0.120 ≤ width/2` at each slot's own
    station, and the whole grid sits on the starting straight.
18. Sector marks are ordered, distinct and inside the lap.
19. Checkpoints are ordered, distinct, spaced no more than
    `circuit::ours::CHECKPOINT_MAX_SPACING_M` apart, and close the loop — the wrap
    from the last back to the first counts as a spacing.
20. Pit entry and exit stations are on the lap and do not sit inside a corner arc.

### The pit lane

21. Every layout with pit stations declares a side and two angles, and both angles
    are inside §7.2's 30°. Stations with no side are refused rather than ignored —
    that state loaded for a milestone and the pit lane did not exist.
22. The lane's width is inside §7.4's 3–4 m and its separation is at least §7.5's
    1.80 m of verge.
23. Every junction is on the **inside** of its adjacent corner, per §7.2's
    no-crossing rule above. The two layouts' sides therefore differ in their own
    frames and agree in the forward frame; a stub that comes out on the far
    physical edge is **two pit lanes** and is refused as such.
24. The lane's arc contains every gore's junction and outboard station, so no gore
    is a wedge of asphalt leading to grass.
25. The **whole** gore is on a straight, walked at nine stations — not just the
    junction rule 20 checks. An 11.16 m exit gore can have its mouth on a straight
    and its far end in an arc, where the merge angle is a function of how far along
    the junction you are.
26. The lane does not share a side and a station range with any corner's run-off.
    Both are built outboard of the verge on a named side, and where they overlap
    the collider has two surfaces over one band — which is how a kart ends up
    reported as standing on gravel in the pit lane.

### The negative control

`data/tracks/self_intersecting.track.json` is a deliberately broken circuit: a
figure-eight whose two legs cross at 4.4 m of clear ground. It is committed, it is
run by `tools/verify/circuit.sh`, and **the gate fails if it loads**. Same
principle as `input_push_probe.gd --break`: a validator with no negative control
is a validator nobody has proven can say no.

`--case=pit` carries two more of its own, built at run time from one-field edits of
the real circuit rather than committed: a 40° merge angle, which must be refused
naming §7.2's cap, and a reverse stub moved onto the far edge, which must be
refused as two pit lanes. Run time rather than committed because committing them
would mean four track files to keep in step with every schema bump, and a one-line
mutation is more legible than a 2,000-line diff.
