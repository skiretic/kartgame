# 50. Bodywork

Front fairing and its mounting kit, front panel, two side pods, rear wheel
protection. Issue #190; the four bold labels and the table format are
`00-front-matter.md` §6, the regulation quotes are §4 and are not repeated here.

**This is the most heavily regulated assembly on the kart and it is the one that
was fitted entirely to chassis tubes.** Every panel is undersized, and
`bodywork.py` says so in its own comments. What follows fits each panel to a
homologation form or to an article, and then states what the neighbor has to do
to make room.

| panel | built | required | short by |
| --- | --- | --- | --- |
| front fairing | 512 mm wide | ≥1000 (9.5.2) | 488 |
| rear wheel protection | 572 mm wide | ≥1340 (9.5.5.1) | 768 |
| side pod outer face | 482 mm from centerline | 618–664 per the tapering datum | 136–182 per side |

Art. **4.10.1**, PDF p. 11, is the part list this section is measured against, and
it is worth quoting because nothing in the repo had it:

> The bodywork must comply with the category in which the kart is entered.
> According to the class, it must be made of one front fairing, one front fairing
> mounting kit, one front panel, two side bodyworks and one rear wheel protection.

Six items. This kart has four panels, no front panel and no mounting kit.

Art. **4.10.2**, PDF p. 11, is the other one nobody had:

> The bodywork must be impeccably finished, not be of a makeshift nature and have
> no sharp edges. Except the wheel covers, the minimum radius of any angles or
> corners is 5 mm.
> If plastic is used, it must not splinter or form sharp edges as a result of
> possible breakage. It may be of any colour.

Art. **4.11**, PDF pp. 11–12, on the rear protection specifically:

> The rear wheel protection must be made by injection blow moulding, without foam
> filling […] The outer edges of the rear wheel protection must be designed in a
> significantly different color than the rear wheel protection Body.
> The rear wheel protection must be fastened to the homologated chassis by at
> least two points using supports homologated with the protection. These supports
> must be mounted (possibly by means of a flexible system) on the two main tubes
> of the chassis (respecting the homologated dimension F).

**The two main tubes**, not the rear bumper. That is the fix for the 5.91 mm gap
`joints.py` waives, and it is a different fix from the one the waiver proposes.

**WITHDRAWN — this section attributed a side-bumper limit to the front bumper.**
The line it quoted, *"Height of the upper bar: 160.0 mm minimum from the ground
(measured to the tube top)"*, is **Art. 9.4.2 Side bumpers**, not 9.4.1, and it
governs the side bar. It is not a floor under the nose bar and it is not half of
any demand on the chassis.

The front bumper's own limits, Art. 9.4.1, PDF p. 23, are two bars at two heights,
both measured to the tube **top**: the **upper** bar 200.0-250.0 mm and the
**lower** bar 70.0-110.0 mm. See front matter §4, which now carries the article in
full and records the correction. Demand 1 in §50.3 below is superseded on the same
grounds.

## 50.1 Three corrections to §4's quoted envelope

§4 is the contract and these do not contradict it; they are the parts of the
article text the ellipses dropped, and each one moves a number.

**1. The fairing's maximum width is the *front* track, not the rear.** §4 quotes
*"Maximum width: overall rear width […]"*. The sentence continues, PDF p. 24:

> Largeur maximum : largeur arrière hors-tout de l’unité roue avant/arbre avant.
> Maximum width: overall rear width **of the front wheel/front axle unit**.

So the ceiling is `track_front` = 1240, not `track_rear` = 1400. It does not bind
here — 1090 clears it by 150 — but a fairing specified against 1400 would have
been 160 mm illegal and read as compliant.

**2. Two side-bodywork rules are missing from §4 and one of them binds.** Same
page:

> The surface of the side bodywork must be uniform and smooth; it must not
> comprise holes other than those necessary for attachment purposes.
> No part of the side bodywork may cover any part of the driver seated in the
> normal driving position.
> In wet weather conditions, the side bodywork must not protrude beyond the plane
> defined by the outer edge of the rear wheels. See TD n°2.1.

The wet-weather line is a hard ceiling of |x| ≤ 700 on the pod face at every
station, and the pod face specified below peaks at 664. The "no holes" line means
the pod's number zone is a *printed* zone and never a cut-out.

**3. The pod datum has two defensible readings and they differ by 11 mm.** §4
derives `x_datum(y) = 660 − (y/1050)·80` by taking the two datum points at the
axle lines. Read literally — *"the outer front edge of the front wheel and the
outer front edge of the rear wheel"* — the points are at the tires' forward-most
faces, y +665 and y −672.5, and the line through them is
`x_strict(y) = 671.0 − 0.0767·y`: same plan angle to within 0.03° (4.39° against
4.36°), but uniformly **11 mm further outboard**. Since the permitted band is
0–40 mm *inboard* of the datum, the strict reading is the binding one on the
inboard side and §4's is the binding one on the outboard side. **Every pod inset
below is stated against both**, and the specified face clears both bounds under
both readings by ≥7 mm. Nothing here needs §4 amended; the inset budget just is
not 40 mm, it is 29.

## 50.2 Compliance table

Every row is a regulated dimension. Margin is signed toward the limit that binds.

| regulated dimension | specified | limit | margin | article (PDF p.) |
| --- | --- | --- | --- | --- |
| fairing width | 1200 | ≥1000 | +200 | 9.5.2 (24) |
| fairing width | 1200 | ≤1240 (front-axle unit) | −40 | 9.5.2 (24) |
| fairing top edge | 267 | ≤280 (front wheel top) | −13 | 9.5.2 (24) |
| gap, front wheels to back of fairing | 77 | ≤180 | −103 | 9.5.2 (24) |
| front overhang, fairing | 504 | ≤680 | −176 | 9.5.2 (24) |
| front overhang, bumper | 425 | ≥350 | +75 | 9.4.1 (23) |
| fairing air vents | 1 | ≤1 | 0 | 9.5.2 (24) |
| vent diameter | 11 | ≤12 | −1 | 9.5.2 (24) |
| vent location | rear face | rear face | — | 9.5.2 (24) |
| clamp support tube spacing | 65 | ≥60.1 | +4.9 | 9.5.2 (24) |
| hook clamp to mounting kit | 1.0 | 1 mm spacing | 0 | 9.5.2 (24) |
| front panel width | 275 | 250–300 | +25 / −25 | 9.5.3 (24) |
| front panel top edge | 500 | ≤552.5 (steering wheel top) | −52.5 | 9.5.3 (24) |
| front panel gap to steering wheel | 166 | ≥50 | +116 | 9.5.3 (24) |
| front panel beyond fairing | no | must not protrude | — | 9.5.3 (24) |
| front panel number zone | 240 × 190 | ≥170 tall (see below) | +20 | 9.5.3 / 3.7 (24 / 5) |
| pod face inset, §4 datum | 8–21 | 0–40 inboard | +8 / −19 | 9.5.4 (24) |
| pod face inset, strict datum | 18.8–32.9 | 0–40 inboard | +18.8 / −7.1 | 9.5.4 (24) |
| pod face, wet-weather plane | 664 max | ≤700 | −36 | 9.5.4 (25) |
| pod top edge | 254 at the rear crest (228 at the widest station) | ≤283.7 at y +265 (tire-top plane) | −29.7 | 9.5.4 (25) |
| pod ground clearance | 42 at the front lip | 25–60 | +17 / −18 | 9.5.4 (25) |
| gap, pod front to front wheels | 120 | ≤150 | −30 | 9.5.4 (25) |
| gap, pod rear to rear wheels | 47.5 | ≤60 | −12.5 | 9.5.4 (25) |
| pod overlaps frame in plan | no; 505 vs rail 300 | must not overlap | +205 | 9.5.4 (24) |
| pod holes | none | attachment only | — | 9.5.4 (24) |
| pod number zone | 220 × 170 | ≥170 tall | 0 | 9.5.4 / 3.7 (25 / 5) |
| rear protection width | 1390 | ≥1340 | +50 | 9.5.5.1 (25) |
| rear protection width | 1390 | ≤1400 (overall rear width) | −10 | 9.5.5.1 (25) |
| rear protection top edge | 217 | ≤295 (rear wheel top) | −78 | 9.5.5.1 (25) |
| clearance windows, count | 3 | ≥3 | 0 | 9.5.5.1 (25) |
| clearance window width, centerline | 200 | ≥200 | 0 | 9.5.5.1 (25) |
| clearance window width, each wheel | 210 | ≥200 | +10 | 9.5.5.1 (25) |
| clearance in those windows | 40 | 25–60 | +15 / −20 | 9.5.5.1 (25) |
| rear overhang | 367 | ≤400 | −33 | 9.5.5.1 (25) |
| gap, rear protection to rear tire | 32.5 | 15–50 | +17.5 / −17.5 | 9.5.5.1 (25) |
| outer parts, separate and differently colored | 2 parts, own material | required | — | 9.5.5.1 / 4.11 (25 / 11) |
| minimum corner radius | 5 | ≥5 | 0 | 4.10.2 (11) |
| bodywork part count | 6 | 6 | 0 | 4.10.1 (11) |

Two rows sit exactly on their limit and say so rather than being padded: the vent
count and the part count are integers, and the 5 mm corner radius is a floor that
a returned rim meets exactly by construction (§50.7). The centerline clearance
window is 200 mm because widening it costs nothing and buys nothing — the panel
is 1390 wide there.

## 50.3 The two demands this section places on other sections

Neither is edited here. `frame.py` is §Chassis and the radiator is §Powertrain,
both being written by other agents right now.

**Demand 1 — the upper nose hoop must not dive.** `bodywork.py:87–112` measured
the whole problem and proposed the fix in its own comment: *"either `nose_width`
should be 0.512, or `frame.py`'s upper nose hoop should tie into the frame at
steering-hoop height instead of diving to rail height at the front cross member,
which is what a real KZ nose bar does and would free the full 0.680."* Take the
second. As geometry:

    the upper nose hoop's tube center must be at z >= 150 over the whole span
    |x| <= 300, and must not fall below z = 130 anywhere forward of y = +600.

`frame.py:_bumpers` builds it at `z + 0.105` = 155 over |x| ≤ 255 — already
compliant there — and then dives to `z + 0.020` = 70 at (±300, +545, 70), which is
what makes the fairing impossible.

**The 150 floor is withdrawn and this demand is superseded.** It rested on
attributing Art. 9.4.2's 160 mm side-bumper minimum to the front bumper. The front
bumper's real bands are 200-250 and 70-110 to the tube top, so a tube center at
z 150 (top 160) is **50 mm above the lower bar's ceiling and 40 mm below the upper
bar's floor** — it is the one height a front bar may not be. §Chassis places the
lower bar's top at 95 and the upper bar's top at 225, and **the fairing picks up on
the upper bar**, which is the only one in a height band that can carry it. The
hoop's dive to z 70 (top 80) is therefore *legal* for a lower bar, not 70 mm
illegal; what was missing was the second, upper bar. The 130 floor over the run
back to the frame stands on its own reasoning — the fairing's cavity: the panel's inner lower skin is at z 43.8 and
its inner upper skin at 263.2, so a tube anywhere in 130–250 is inside the
cavity and clear of both.

**Demand 2 — the radiator's outboard extremity must stay at |x| ≤ 489.** That is
already what the built mesh measures (front matter §5a: *"outboard edge at −489,
61 mm of margin"*). The pod's mouth is specified at 505, so the pair clears by
16 mm as a half-space test, with no reliance on where the two z bands land.

## 50.4 The pod outer face as a function of y

`sidepod_x` is a single constant and the datum tapers 4.36° in plan. One number
cannot be right at both ends, and 482 is 136–182 mm inboard of where the face
belongs. Replace it with a line plus a fore-aft taper expressed in **millimeters
of extra inset**, never as a fraction:

    x_face(y) = 652.0 - 0.0762 * y - taper(t)          # mm, y in mm

    taper(t), t = (265 - y) / 595, extra inset in mm:
        t 0.00  14      front edge
        t 0.18   3
        t 0.45   0      widest station
        t 0.72   1
        t 0.88   6
        t 1.00  13      rear edge

    front edge, y = +265   ->  x = 618
    widest,     y = -2.8   ->  x = 652
    rear edge,  y = -330   ->  x = 664

The 652 is `derived`: §4's datum is 660 at y = 0 and the base inset is 8 mm.
`SIDEPOD_OUT_FRACTION`'s existing taper was 0.960 at the front and 0.962 at the
rear — read as a *fraction of the face position*, and that is the trap: 4% of 640
is 26 mm, which plus any base inset exceeds the 40 mm band. As millimeters the
same visual taper is bounded by construction. Worst total inset is 21 mm against
§4's datum and 32.9 mm against the strict one, both at the front edge.

## 50.5 Why moving the pods out fixes the engine and the radiator

The two gate-1 overlaps `joints.py` waives against the pods are both the same
fault: the pod's mouth was set against a chassis tube and nothing checked what
was behind it. Both are fixed by a half-space test, which is why the fix is
provable rather than eyeballed.

    pod mouth (inboard-most point of either free edge)         x = 505
    engine_crankcase_upper, CRANKCASE_OUTBOARD_X               x = 398   -> 107 mm clear
    engine_ignition_cover, outboard face (372 + 12.9 measured) x = 385   -> 120 mm clear
    engine_ignition_bolt_4 (372 + 1.4 measured)                x = 373   -> 132 mm clear
    radiator, outboard extremity (front matter 5a, measured)   x = 489   ->  16 mm clear

The arithmetic checks against the waivers: the mouth is at `SIDEPOD_TOP_X` = 372
today, and 398 − 372 = 26 mm against the 26.7 mm the gate measures for the
crankcase. Same 0.7 mm faceting difference on the other two. So the model that
predicts the current failures also predicts the fix, which is the only reason to
believe it.

**The radiator waiver reasons about the wrong plane and this section will not
inherit it.** `OPEN_DEFECTS`' entry says *"params.radiator_x is 0.365 with a half
thickness of 0.020, so the core's outboard face is at 0.385"*. That is the core's
face offset along its **own normal**, and `radiator_rake_delta`'s docstring is
explicit that the normal points *forward*: *"the big fin face ended up pointing
outboard; it points forward, the way the driver does."* The core's lateral extent
is `radiator_width` = 265 about `radiator_x` = 365, i.e. **x 232.5 to 497.5**, and
`radiator_x`'s own docstring says so — *"the core reads 242 mm from the centerline
at its inboard edge and 500 mm at its outboard"*. Front matter §5a measures the
built mesh at 489. Photogrammetry agrees: `crg_roadrebel_kz_front.webp`, anchored
on the front track (1240 mm across 686 px, 1.807 mm/px), puts the radiator's
outboard face at 479 mm and its inboard face at 244 mm. Three independent figures
at 479 / 489 / 497.5 and one at 385. A pod specified to clear 385 would still be
104 mm inside the radiator.

## 50.6 Panel wall thickness, derived from three forms

`PANEL_THICKNESS` = 3.0 mm was `estimated` from "CIK bodywork is thermoformed
about 3 mm of polyethylene". The forms give mass, which with an area and a density
gives thickness, and three of them agree:

    t = mass / (density * developed area),  polyethylene 950 kg/m3

    OTK M4 fairing   1500 g  1090 x ~380 arc = 0.414 m2  ->  3.81 mm
    KG 505 fairing   1600 g  1029 x ~420 arc = 0.432 m2  ->  3.90 mm
    KG C2 rear prot  1450 g  1360 x ~300 arc = 0.408 m2  ->  3.74 mm

**3.8 mm**, `derived`. The developed arc lengths are `estimated` off the forms'
side elevations at about ±10%, which is ±0.4 mm; the density is a range
(940–960) worth ±0.05 mm. Blow-moulded wall is not uniform, so this is the mean
wall and not a caliper reading — but it is three independent masses landing inside
0.16 mm of each other, and it is not 3.0.

## 50.7 The rim cannot be a chamfer

Art. 4.10.2's *"the minimum radius of any angles or corners is 5 mm"* is
`sourced`, and a 3.8 mm wall physically cannot carry a 5 mm edge radius — the most
a flat wall's cut edge can hold is half its thickness, 1.9 mm. `bodywork.py` runs
`build.bevel_object` on the panel's rim, which is a chamfer on a 3.8 mm band and
is 3.1 mm short of legal on every free edge of every panel.

Real thermoformed panels return the edge: the skin rolls inward through 180° on a
5 mm outer radius, which needs a 10 mm band of material and leaves a 1.2 mm slot
behind it. So every free edge of every panel in this section is a **returned lip**,
`sourced` radius 5.0 mm, band 10 mm, and `_loft_shell` gains that rather than
offsetting flat and chamfering. This is the one place where the regulation demands
a change to how the mesh is built and not just to a number.

## 50.8 Front fairing

### `bodywork_nose_fairing`

**Status:** built — resized from 512 to 1090 mm wide; its two molded Ø14 pins are
deleted and replaced by the mounting kit below (Art. 4.10.1 lists the kit as a
separate homologated item, and Art. 9.5.2 specifies its clamp geometry, so it
cannot be two studs inside the panel's mesh).
**Attaches to:** `bodywork_fairing_support_u` (bolted, 2× M8 through the panel's
molded bosses), `bodywork_fairing_strut_l` / `_r` (bolted, 1× M8 each),
`bodywork_fairing_hook_l` / `_r` (bolted). **Not** to `chassis_nose_hoop_lower` —
the existing joint is deleted, because the panel reaches the frame only through
the kit, which is what makes the CIK release mechanism a mechanism.
**Envelope:** Art. 9.5.2 and Art. 4.10.2 — §4 above.
**Verification:** gate 1 (must not overlap `chassis_nose_hoop_*`, `wheel_f?_*`),
gate 2 (four declared joints), `genkart.sh --check`, and the compliance table's
seven 9.5.2 rows.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| width | `nose_width` | 1200 | `estimated` (deviation) | the M4 form's `sourced` 1090 stays recorded; 1200 is part 6's deliberate deviation on Anthony's aero directive -- tips reach |x| 600, covering the tire's inboard 48 mm. Inside 9.5.2's band either way. Was 0.680 and built at 0.512, then 1090 sourced, then briefly 1310 (judged too wide) |
| fore-aft depth at centerline | `nose_depth` | 287 | `sourced` | same drawing, plan view |
| overall height | `nose_height` | 227 | `sourced` | same drawing, front elevation. Was 0.130 |
| frontmost plane (apex, centerline) | `nose_apex_y` | +1029 | `derived` | front matter §5: front axle +525 plus front overhang 504 |
| rear lip, constant across the span | — | +742 | `derived` | 1029 − 287. Constant so the 180 mm gap rule is one number; the OTK plan view's rear edge is straight but for the kit recess |
| kit recess in the rear lip, \|x\| ≤ 120 | — | +800 | `estimated` | the notch visible mid-rear in the OTK plan view; 58 mm forward of the lip clears the two clamp tubes and their hooks |
| bottom edge, centerline | `nose_bottom_z` | 40 | `estimated` | 5 mm above the rails' underside at 35, which are the lowest thing on the kart (`ground_clearance`). Was 46; 40 buys the fairing's top edge 6 mm against the 280 ceiling |
| top edge, centerline | — | 267 | `derived` | 40 + 227. 13 mm under the front tire top at 280 |
| bottom edge at the tips | — | 108 | `estimated` | part 6: the horn's lower edge; the old 130 turned-up read was the M4's flat-tip design |
| top edge at the tips | — | 254 | `estimated` | part 6, the VLR-typology horns: tip tops RISE outboard, 26 under the 280 tire crown. The M4's falling 215 tip is superseded |
| top edge, hump/valley | — | 267 / 245 / 252 | `estimated` | part 6 center hump: valley at |x| 260, ridge shoulder at 350 -- at 260 rather than the reference's ~190 because Art. 9.4.1's upper bar runs its straight at z 217 exactly there (46 measured pairs) |
| rear edge rise (the V walls) | — | −10 .. +24 | `estimated` | `NOSE_REAR_RISE`: rear corners rise flanking the front panel so the panel grows out of the fairing; lobes lean 14 mm forward of the 742 lip to clear the panel's base belly (26 measured pairs at a 5 mm shutline; 14 mm is the working shutline) |
| apex setback at the tips | — | 189 | `estimated` | `NOSE_APEX_SETBACK`'s existing curve × 1.8; tip depth grows to ~143 with the horn's trailing pull |
| horn trailing edge, tips | — | 697 | `estimated` | `NOSE_BACK_TOP_INSET` −45 at the tip: the trailing edge sweeps rearward alongside the tire, 32 mm off its leading face at 665 -- the crescent that reads as sculpted around the wheel |
| apex height, centerline | — | 108 | `derived` | unchanged: mid-height of the 75 mm gap between the two nose-hoop tiers at the bumper plane, and demand 1 keeps both tiers where it was measured |
| wall thickness | `panel_thickness` | 3.8 | `derived` | §50.6 |
| free-edge return radius | — | 5.0 | `sourced` | Art. 4.10.2, PDF p. 11 |
| air vent diameter | — | 11 | `sourced` | Art. 9.5.2 caps it at 12; 1 mm under so a faceted circle cannot measure over |
| air vent position | — | x 0, y +742, z 70 | `estimated` | the article fixes the face and the count, not the spot. Low on the rear wall, because the same article says the fairing *"must not be able to retain water, gravel or any other substance"* and a vent at the crown drains nothing |

Clearances, all `derived`, none of them regulated but each one a gate-1 pair:

    front tires: center lip at 742 is 77 mm clear; the horn trailing
    edges chase the tire to 32 mm at |x| 600 (static, unsteered)
    nose hoop lower tier, tube surface z 50..70 at y +950              inside the cavity, 6.2 mm under the inner lower skin at 43.8
    nose hoop upper tier, tube surface z 145..165 at y +950            inside the cavity by 101 mm above and 98 mm below

The lower tier is 6.2 mm clear rather than clamped, and that is deliberate now:
the panel is held by the kit, so panel-to-hoop is a **forbidden** pair rather than
a declared joint. Same shape as front matter §5a's radiator-and-seat rule.

### `bodywork_fairing_support_u`

**Status:** new.
**Attaches to:** `bodywork_nose_fairing` (bolted, 2× M8), `chassis_cross_front`
(bolted, 2× M8 clamp), `bodywork_fairing_kit_tube_fwd` / `_aft` (welded).
**Envelope:** Art. 9.5.2 — the mounting kit, TD n°2.2.
**Verification:** gate 2 (three declared joints); its own tube spec is a `--check`
determinism item only.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outside diameter | — | 20.0 | `sourced` | OTK M4 HF p. 2, `acciaio Ø20x1.5` |
| wall thickness | — | 1.5 | `sourced` | same |
| leg spacing | — | 450 | `sourced` | same drawing, dimensioned |
| material | — | magnetic steel | `sourced` | Art. 9.4, PDF p. 22, *"magnetic steel round tubing"*, and `acciaio` on the form |
| front ends, at the panel | — | (±225, +790, 150) | `derived` | 450/2 spacing, inside the panel's cavity |
| rear ends, at the frame | — | (±225, +545, 60) | `estimated` | the front cross member is at y +525; the clamp sits just ahead of it |

### `bodywork_fairing_strut_l`, `bodywork_fairing_strut_r`

**Status:** new.
**Attaches to:** `bodywork_nose_fairing` (bolted, 1× M8),
`chassis_nose_hoop_lower` (clamped).
**Envelope:** Art. 9.5.2. The 550 mm spacing is **not** in Art. 9.4.1 — see the
row below.
**Verification:** gate 2.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outside diameter | — | 16.0 | `sourced` | OTK M4 HF p. 2, `acciaio Ø16x1.5` |
| wall thickness | — | 1.5 | `sourced` | same |
| spacing | — | 550 | `sourced` | same drawing — the form, and **only** the form. An earlier version of this row claimed it cross-checks Art. 9.4.1's *"550.0 mm apart"*; **the article says 450.0 mm** for the front bumper and 500.0 ± 5 mm for the side bumpers, and there is no 550 anywhere in Art. 9.4. The form's *other* pair, at 450, is the regulated one; this 550 pair is unregulated. Corrected in front matter §4 |
| panel end | — | (±275, +830, 175) | `estimated` | outboard and above the U-frame's legs, matching the form's photo where the thin struts stand outside the thick ones |

### `bodywork_fairing_kit_tube_fwd`, `bodywork_fairing_kit_tube_aft`

**Status:** new. The two tubes Art. 9.5.2 names by their spacing.
**Attaches to:** `bodywork_fairing_support_u` (welded),
`bodywork_fairing_hook_l` / `_r` (clamped, 1.0 mm).
**Envelope:** Art. 9.5.2 — *"the distance of 60.1 mm minimum between the 2 support
tubes of the clamps"*.
**Verification:** gate 2. The 1.0 mm hook standoff is inside
`joints.CONTACT_TOLERANCE` = 2.0 by construction, so this joint passes without a
waiver — unlike the fairing's old pins, which needed `MOUNT_STANDOFF` to explain
themselves.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| fore-aft spacing | — | 65.0 | `sourced` | Art. 9.5.2 floor is 60.1; +4.9 so a bend fillet cannot pull it under |
| outside diameter | — | 12.0 | `estimated` | no form dimensions the release tubes. 12 mm is what an M10 hook clamp closes on and is consistent with the OTK photo's central mechanism |
| lateral span | — | ±90 | `estimated` | inboard of the U-frame legs at ±225, matching the mechanism's width in the OTK photo |
| height | — | z 150 | `derived` | on the U-frame's front span |

### `bodywork_fairing_hook_l`, `bodywork_fairing_hook_r`

**Status:** new.
**Attaches to:** `bodywork_nose_fairing` (bolted),
`bodywork_fairing_kit_tube_fwd` (clamped), `bodywork_fairing_kit_tube_aft`
(clamped).
**Envelope:** Art. 9.5.2 — *"the 1 mm spacing between the hook clamps and the
front fairing mounting kits"*.
**Verification:** gate 2, three declared joints each. Also the reason
`nose_fairing_pivot` stays published: Art. 9.5.2's Appendix 9 vertical push test
(PDF p. 58 — five loads on the centerline through a 200 × 450 × 10 mm plate,
average peak above 75% of the form's figure within 30 mm of travel) is a
displacement about this hook line, so M3's contact displacement rotates about it.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| standoff from the tubes | — | 1.0 | `sourced` | Art. 9.5.2, quoted above |
| lateral position | — | ±90 | `derived` | on the tubes |

## 50.9 Front panel

### `bodywork_front_panel`

The nassau panel. Art. 4.10.1 requires it, Art. 9.5.3 dimensions it. **Its
50 mm gap is to the steering wheel** — a hands clearance — and not to the front
road wheel; front matter §4 says so because it was misread once.

**Status:** built — raked rebuild (#205), then the 2026-07-31 photo-match
sculpt: violin outline, S face profile, stadium head, rolled top deck. The
sculpt's ratios were **measured off a gridded crop** of
`liv_travisanutto_kr_rosberg.jpg` rather than eyeballed, because the eyeballed
first pass (waist 0.845) read as the same flat strip it replaced.
**Attaches to:** `bodywork_front_panel_stay_l` / `_r` (bolted),
`bodywork_front_panel_bar` (bolted).
**Envelope:** Art. 9.5.3, Art. 4.10.2.
**Verification:** gate 1 (must not overlap `steering_*`, `pedal_*`), gate 2, and
the five 9.5.3 rows of the compliance table.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| width | `front_panel_width` | 275 | `derived` | midpoint of Art. 9.5.3's 250–300, so ±25 either way. The widest stations are the base flare and the plate head, both at the full 275 |
| top edge | `front_panel_top_z` | 500 | `derived` | 52.5 under the steering wheel's top. That top is z **552.5**, itself `derived`: `wheel_center_z` 480 plus `wheel_diameter`/2 × sin(`wheel_angle`) = 160 × sin(0.470) = 72.5, because a wheel raked 26.9° from vertical has its highest rim point leaning *forward*, at y +462.6 |
| bottom edge | `front_panel_bottom_z` | 240 | `estimated` | #205: foot tucked just behind the fairing's rear top edge (spine z 267, 27 mm below it, zero overlap). The pre-rake row said 190 and went stale when the rebuild landed without a spec pass |
| top edge, fore-aft | — | y +578 | `estimated` as rake, `derived` as clearance | 32.2° of rake (was 28.6°; all three front-on refs lie the plate back harder). Gap to the wheel's nearest point (y +462.6, z 552.5): hypot(115.4, 52.5) = **126 mm**; the roll deck's trailing edge (y +554, z 493) clears by **109 mm**, both against the 50 minimum |
| bottom edge, fore-aft | — | y +742 | `derived` | on the fairing's rear-lip plane, 27 mm under its top skin |
| height | — | 260 | `derived` | 500 − 240 |
| width profile | `FRONT_PANEL_WIDTH` (bodywork.py) | waist **0.615** at t 0.38, plate 1.00 at t 0.82, top 0.80 | `estimated` | measured off the gridded KR crop: head ~245 px, waist ~145, base ~230 — waist/head 0.59, base/head 0.94. Eight face controls (`FRONT_PANEL_FACE_T`); the outline has three inflections and five points hold one |
| head rounding | `FRONT_PANEL_CROWN` | corner drop 100 max | `estimated` | stadium top, corner radius ~40% of plate width on the same crop; the old 34 mm was a tombstone with clipped corners |
| face profile | `FRONT_PANEL_BULGE`, `FRONT_PANEL_BOW` | S: belly 14, waist hollow 6, plate 16; × (1 − 0.65·a²) | `estimated` | the KR pod's side highlight. Belly zeroed below t 0.12 — a 12 mm belly there was 8 triangle pairs inside the fairing, gate 1, measured |
| edge sweep | `FRONT_PANEL_SWEEP` | max 88 rearward, knee at 0.80 | `estimated` | side returns wrap toward the column; the old flat 55 mm ramp read as an arc that stops |
| top roll | `FRONT_PANEL_ROLL_DEPTH/_DROP` | deck 30 rearward, 7 drop, ~60° dihedral | `estimated` | #199 crease pattern, two spline runs at a shared vertex — the top is a fold, not a cut, in every front-on |
| wall thickness | `panel_thickness` | 3.8 | `derived` | §50.6 |
| free-edge return radius | — | 5.0 | `sourced` | Art. 4.10.2 |
| number zone | — | **flagged for the livery wave** | `derived`, stale | Art. 9.5.3 *"A space for racing numbers must be provided on the front panel"*. The pre-sculpt row put 240 × 190 at (0, y 602, z 345) — that station is now the waist, 173 mm wide, and the zone does not fit there. The usable flat is the plate head, ~275 × 150 with the crown falloff, which holds a two-digit marking per §60's reading but not 240 × 190. Zone layout is albedo work and deferred with the livery pick; the geometry constraint it must satisfy is recorded here |

Not protruding beyond the front fairing, Art. 9.5.3: the panel's frontmost
points are the foot at y +742, exactly on the fairing's rear-lip plane, and the
belly's peak at y ~+715 — both behind the fairing's apex at +1029 — and its
half-width 137.5 against the fairing's 545. Inside on both axes.

### `bodywork_front_panel_stay_l`, `bodywork_front_panel_stay_r`

**Status:** new.
**Attaches to:** `bodywork_front_panel` (bolted), `chassis_cross_front` (clamped).
**Envelope:** Art. 9.5.3 — *"The panel's lower section must be securely attached
to the front part of the chassis frame, directly or indirectly."* Indirectly is
what these are.
**Verification:** gate 2.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outside diameter | — | 16.0 | `estimated` | not dimensioned on any form here; matches the fairing's Ø16 strut so the kart carries one small-tube size |
| panel end | — | (±110, +585, 200) | `derived` | inboard of the panel's edges at ±137.5 |
| frame end | — | (±110, +525, 60) | `derived` | on the front cross member |

### `bodywork_front_panel_bar`

**Status:** new. Art. 9.5.3 requires it by name.
**Attaches to:** `bodywork_front_panel` (bolted), `chassis_steering_hoop`
(clamped).
**Envelope:** Art. 9.5.3 — *"Its upper part must be securely attached to the
steering column support with one or more independent bars."*
**Verification:** gate 2. Note this bar makes `chassis_steering_hoop` carry a
second load, and the hoop is 5.1 mm off the frame today (`joints.py`, #192) — so
this part's gate-2 result depends on the chassis fix, not on this section.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outside diameter | — | 16.0 | `estimated` | as the stays |
| count | — | 1 | `sourced` | *"one or more independent bars"* |
| panel end | — | (0, +620, 470) | `derived` | just under the panel's top edge, on the centerline |
| hoop end | — | (0, +525, 130) | `estimated` | the steering hoop's crown, which `joints.py` measures at z 127.2 |

## 50.10 Side bodywork

### `bodywork_sidepod_r`, `bodywork_sidepod_l`

**Status:** built — outer face respecified as a function of y (§50.4), fore-aft
window extended 35 mm rearward, mouth moved from 372 to 505, mount stubs replaced
by named brackets. Built right and mirrored; Art. 9.5 requires *"the two side pods
must be used together as a set"*, which is what makes the mirror correct rather
than convenient.
**Attaches to:** `bodywork_sidepod_bracket_?f` (bolted),
`bodywork_sidepod_bracket_?r` (bolted). **Not** directly to `chassis_side_bar_?` —
the existing joint is deleted, because a mouth at 505 stands 50 mm outboard of the
bar's surface at 455 and a joint that cannot touch is a gate-2 failure by
construction.
**Envelope:** Art. 9.5.4, Art. 4.10.2 — §4 above and §50.1 item 2.
**Verification:** gate 1 (must not overlap `engine_*`, `radiator_*`,
`chassis_rail_?`, `wheel_??_tire`), gate 2, and the nine 9.5.4 rows.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outer face | `sidepod_datum_x0`, `sidepod_datum_slope`, `sidepod_inset` | 652 − 0.0762·y − taper | `derived` | §50.4, from Art. 9.5.4's datum and the two frozen tracks. Replaces `sidepod_x` = 0.480 |
| — front edge, y +265 | — | 586 | `derived` | the nose blade: `SIDEPOD_TAPER` curls the face 46 mm inboard over the leading 15% the way the reference pod's own nose does (`crg_roadrebel_kz_front.webp`), so the *band* is read from t 0.15 back, where the inset is 20 (§4 datum) — the 46 is also load-bearing against the upper side bar, whose straight ends 5 mm short of the pod's front edge |
| — widest, y −2.8 | — | 652 | `derived` | inset 8.0 / 18.8 |
| — rear edge, y −330 | — | 664 | `derived` | inset 21.0 / 32.3; and 36 mm inside the wet-weather 700 plane |
| forward edge | `sidepod_front_y` | +265 | `sourced` (in range) | gap to the front tire's rear face at y +385 is 120, against Art. 9.5.4's 150 maximum. Hoisted out of `bodywork.py` |
| length | `sidepod_length` | 595 | `derived` | 265 − (−330). **Was 560, and that was non-compliant**: a rear edge at −295 leaves an 82.5 mm gap to the rear tire's forward face at −377.5, against a 60 mm maximum. Nobody had measured it |
| rear edge | — | −330 | `derived` | gap 47.5, 12.5 under the maximum |
| mouth, both free edges | `sidepod_mouth_x` | 505 | `estimated` | §50.5 sets the floor at 489 + clearance; 505 gives the radiator 16 mm. Cross-checked photogrammetrically at 519 ±10 on `crg_roadrebel_kz_front.webp` at 1.807 mm/px, where the pod's top lip stands visibly outboard of the radiator's outer face |
| bottom edge, front lip | — | **42** | `sourced` (in range) | Art. 9.5.4's ground clearance 25–60. The front lip *dips* below the 48 mid-pod figure — the reference pod's lowest point is its forward lower lip — so the panel minimum, which is what the article measures, is 42 |
| bottom edge, widest station | — | 48 | `sourced` (in range) | `SIDEPOD_BOTTOM_Z`; rear edge lifts to 53 |
| height | `sidepod_height` | 180 | `estimated` | unchanged as a section height. The top edge now *rises* toward the rear — `SIDEPOD_HEIGHT_FRACTION` crests at 1.130 at t 0.90, top edge 254, 29.7 mm under the 283.7 tire-top plane — because both reference karts sweep the trailing quarter up into a hump ahead of the rear tire, and the old sagging rear is most of what #199 called ballooning |
| louver stack | `SIDEPOD_LOUVER_T`, `SIDEPOD_LOUVER_RADIUS` | 3 ribs, r 9, t 0.070/0.145/0.220 | `estimated` | `crg_roadrebel_kz_bodywork.webp`: closed crescents stepping rearward-down along the shoulder behind the mouth. **Raised ribs, never holes** — Art. 9.5.4 forbids holes other than for attachment |
| section, mouth to face | — | 147 laterally | `derived` | 652 − 505 at the widest station. The side bar at x 430–445 now runs *outside* the C rather than inside it, which is what the brackets are for |
| wall thickness | `panel_thickness` | 3.8 | `derived` | §50.6 |
| free-edge return radius | — | 5.0 | `sourced` | Art. 4.10.2 |
| number zone | — | 220 × 170, centered (±face, y −230, z 140) | `derived` | Art. 9.5.4 *"on the vertical surface close to the rear wheels"*; 170 from Art. 3.7 as in §50.9. **Printed, never cut** — Art. 9.5.4 forbids holes other than for attachment |

The number zone is the one row with zero margin, and it is worth saying why: the
pod's flank is 180 mm tall and a compliant racing number with its border is
170 mm. That is not a coincidence — it is most of why a CIK pod is as deep as it
is, and a pod built 40 mm shallower would have no legal place to put a number.

### `bodywork_sidepod_bracket_rf`, `_rr`, `_lf`, `_lr`

**Status:** new — the pods' two Ø14 mount stubs per side become named parts,
because a 50 mm reach with a bushed clamp is a bracket and gate 2 cannot see a
stub buried in the pod's mesh.
**Attaches to:** `bodywork_sidepod_?` (bolted), `chassis_side_bar_?` (clamped).
**Envelope:** Art. 9.5.4 — *"must be securely attached to the side bumpers"*.
**Verification:** gate 2, two declared joints each. Contact at the clamp, not at
the shell, which is what `bodywork.MOUNT_STANDOFF` = 1.5 mm exists for and is
inside `CONTACT_TOLERANCE`.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| fore-aft positions | — | y +180, −200 | `estimated` | unchanged from `SIDEPOD_MOUNT_Y`; two per side is what a CIK pod carries |
| arm diameter | — | 14.0 | `estimated` | unchanged from `SIDEPOD_MOUNT_DIAMETER` |
| reach | — | 50 | `derived` | mouth 505 to the side bar's outer surface at 455 |
| clamp | — | M10 through a rubber bush | `sourced` | KG C2 HF p. 3 parts list — `M10` (2), `RC.182.28/30/32` (2), `RC.228.28/30/32` (2), `M10-S` (2). The `.28/.30/.32` suffixes are the tube diameters the bush fits; `tube_bumper` is 20, so a side-bar clamp is a smaller variant of the same part |

## 50.11 Rear wheel protection

**This assembly is the KZ2 part, deliberately.** Issue #197, ADR-0056, and front
matter §2b carries the reasoning. KZ is Group 1 and Art. 8 delegates rear
protection *only* to the wheel-cover variant 8.5.5.2 → 9.5.5.2; there is no plain
8.5.5.1 for Group 1. Everything below is specified against **9.5.5.1**, which is a
Group 2 part, because the reference corpus is KZ2 throughout, because the 1,340
minimum is sourced through a plain-protection homologation form (KG C2,
`003-BR-48`), and because 9.5.5.2's covers reach *beyond* and *above* the rear
wheels where the plain part must not — a different silhouette, not an addition. The
deviation is `estimated` in front matter §1's sense and every Envelope field in
this subsection says so.

Three parts, not one, and that is a **geometry** requirement before it is a livery
one: Art. 9.5.5.1 requires the two adjustable outer parts to be *"a color that is
clearly different from the main part"*, and Art. 4.11 repeats it. Two colors need
two materials, two materials need two meshes.

### `bodywork_rear_panel`

**Status:** built — respecified from 572 to 1390 mm wide, given three ground
clearance windows, and re-attached to the main rails rather than to the rear
bumper.
**Attaches to:** `bodywork_rear_support_l` / `_r` (bolted),
`bodywork_rear_outer_l` / `_r` (bolted, through the adjustment slot). **Not** to
`chassis_rear_bumper` — the existing joint is deleted and so is its 5.91 mm gap
waiver, because Art. 4.11 puts the supports on the two main tubes and the bumper
is a pair the panel must **not** overlap.
**Envelope:** Art. 9.5.5.1 (KZ2 part; Group 1 delegates only to 9.5.5.2 --
deliberate deviation, see #197), Art. 4.11, Art. 4.10.2.
**Verification:** gate 1 (must not overlap `chassis_rear_bumper`,
`wheel_r?_tire`, `exhaust_silencer`), gate 2, and the nine 9.5.5.1 rows.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| overall width, all three parts | `rear_prot_width` | 1390 | `derived` | see the note below. Was `REAR_HALF_WIDTH` × 2 = 572 |
| fore-aft depth | `rear_prot_depth` | 187 | `sourced` | KG C2 HF `003-BR-48` p. 2 drawing |
| height, C2 figure of record | `rear_prot_height` | 177 | `sourced` | same drawing — **no longer builds the part**, ADR-0062: kept on `FIELD_COVERAGE_EXEMPT`, the manifest still publishes it |
| front face | `rear_prot_front_y` | −705 | `derived` | rear tire's rearmost surface at −672.5 plus a 32.5 mm gap, which is the center of Art. 9.5.5.1's 15–50 band |
| rear face | — | −892 | `derived` | −705 − 187; rear overhang 367, 33 under the 400 cap. Matches front matter §5's arithmetic exactly, which is where the 1920 came from |
| top edge, crown \|x\| = 0 | — | 295 | `derived` | `tire_rear_diameter` — Art. 9.5.5.1's *"no higher than the rear wheels"* ceiling, which the reference photo brackets at 290 ±15. ADR-0062: the top edge is `bodywork._rear_top_profile`, a measured station table, not a constant |
| top edge, valley wall \|x\| = 300 | — | 180 | `estimated` | the silencer's Ø32 outlet stubs (z 174–206) show above the edge in the reference dead-rear, so the edge is under ~174 by \|x\| 290 |
| top edge, valley floor \|x\| = 455 | — | 170 | `estimated` | measured both sides of the reference photo, 168 L / 175 R |
| top edge, lobe crest | — | 250 | `estimated` height at a `derived` station, `rear_hub_x` = 592.5 |
| top edge, outer end \|x\| = 695 | — | 235 | `estimated` | edge roll at the panel end |
| bottom edge, in the three windows | `rear_prot_bottom_z` | 40 | `sourced` (in range) | Art. 9.5.5.1's 25–60 |
| bottom edge, between the windows | — | 95 | `estimated` | the article only regulates the windows; lifting between them is what makes them windows |
| window, centerline | — | \|x\| ≤ 100 | `derived` | 200 mm, *"in the extension of […] the centreline of the chassis"* |
| windows, wheels | — | \|x\| 485–695 | `derived` | 210 mm, *"in the extension of the rear wheels"* — the rear tire spans x 485–700 |
| wall thickness | `panel_thickness` | 3.8 | `derived` | §50.6, and the KG C2's own 1450 g is one of the three masses it comes from |
| free-edge return radius | — | 5.0 | `sourced` | Art. 4.10.2 |
| moulding | — | injection blow, no foam | `sourced` | Art. 4.11, PDF p. 11. Not geometry, but it is why the wall is hollow and why §50.6's mean-wall caveat exists |

**1390 is the one number in this section that departs from a form, and the reason
is arithmetic.** The KG C2 measures 1360, and 1360 on *this* kart cannot present a
200 mm clearance window under a rear wheel: the panel's edge at 680 against the
tire's inner edge at 485 leaves 195 mm, 5 mm short. 1390 gives 210 mm with 10 mm
still under the 1400 overall-rear-width ceiling, and it also satisfies Art.
9.5.5.1's *"Whatever the conditions, the rear wheel protection must be in line
with the outside of the rear wheels"* — 5 mm inboard of the tire's outer plane at
700. So this is `derived` from a `sourced` shape plus two `sourced` limits, and the
form's 1360 is recorded here as the reason the depth and height are not estimates.
A real KG C2 fits legally on a kart running slightly under maximum rear track,
which is most of them.

### `bodywork_rear_outer_l`, `bodywork_rear_outer_r`

**Status:** new — Art. 9.5.5.1's *"two adjustable outer parts"*, absent because the
panel was built as one 572 mm piece.
**Attaches to:** `bodywork_rear_panel` (bolted, through the adjustment slot).
**Envelope:** Art. 9.5.5.1 (KZ2 part; Group 1 delegates only to 9.5.5.2 --
deliberate deviation, see #197), Art. 4.11.
**Verification:** gate 1 (declared overlap with the main part over the slot),
gate 2, and a **material assertion**: the outer parts' material must not be the
main part's. That is the one regulation in this document satisfied by a material
slot rather than by a coordinate, and it is checkable in the manifest.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| span | — | \|x\| 360–695 | `estimated` | the split falls at 400 with a 40 mm slot, i.e. between the centerline window and the wheel windows so it cuts neither. Read off the KG C2's photo, where the differently-colored ends are about the outer quarter per side |
| adjustment travel | — | ±20 | `estimated` | *"adjustable"* is the article's word and it is what the slot is for; ±20 spans a 1350–1430 setting range, of which 1340–1400 is legal |
| main part span | — | \|x\| ≤ 400 | `derived` | 800 wide |

### `bodywork_rear_support_l`, `bodywork_rear_support_r`

**Status:** new — Art. 4.11 requires them and they are the fix for the floating
rear panel.
**Attaches to:** `bodywork_rear_panel` (bolted), `chassis_rail_l` / `_r`
(clamped).
**Envelope:** Art. 4.11 — *"fastened to the homologated chassis by at least two
points using supports homologated with the protection. These supports must be
mounted […] on the two main tubes of the chassis."*
**Verification:** gate 2, two declared joints each.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| count | — | 2 | `sourced` | *"at least two points"*; the KG C2 form's parts list is quantity 2 of everything |
| rail end | — | (±215, −620, 50) | `derived` | `frame.py:_rail_path`'s last control point |
| panel end | — | (±215, −701, 100) | `derived` | just inside the panel's front face at −705 |
| clamp | — | M10 through a rubber bush on a 30 mm tube | `sourced` | KG C2 HF p. 3: `M10` bolt, `RC.182.30` spacer, `RC.228.30` bush, `RC.103` arm, `RC.229` washer, `M10-S` nut, `RC.102` bracket plate — 2 of each. `tube_main` is 30, which is the `.30` variant |
| arm | — | stamped steel, 4 mm | `estimated` | the `RC.103` arm in the form's exploded view; thickness is not dimensioned |

## 50.12 What this section demands of the rear bumper

Not a demand on this section's own parts, but the pair has to work and
`chassis_rear_bumper` belongs to §Chassis:

    the rear bumper's rearmost tube surface must be at y >= -880

so it sits inside the protection's cavity — inner rear wall at −888.2 — with the
protection's rear face at −892 respecting the 400 mm rear overhang. At
`tube_secondary` = 22 that puts the rear straight's center at y ≈ −869 or
forward. **`frame.py` currently derives it as `-length_overall/2 + tube/2`**, which
at the new `length_overall` = 1920 is **−949**, i.e. 424 mm behind the rear axle
and 24 mm past a limit that Art. 9.5.5.1 states as a maximum.

That is the same class of error as `length_overall` itself: **1920 is not
symmetric about the origin and cannot be placed as ±960.** Front matter §5 derives
it as front overhang 504 + wheelbase 1050 + rear overhang 367, and 504 ≠ 367 by
137 mm. Placed symmetrically the rear overhang comes out 435, which is 35 mm over
the cap while the front sits 176 mm under its own. Both bumper ends have to be
placed from their own overhang, not from half a length.

## 50.13 Counts

| status | count | parts |
| --- | --- | --- |
| `built` | 4 | `bodywork_nose_fairing`, `bodywork_sidepod_l`, `bodywork_sidepod_r`, `bodywork_rear_panel` |
| `new` | 19 | fairing kit 7, front panel group 4, pod brackets 4, rear protection 4 |
| `delete` | 0 parts | but three pieces of geometry go: the fairing's 2 molded pins, the pods' 4 mount stubs, and 3 joint declarations (`nose_fairing`/`nose_hoop_lower`, `sidepod_?`/`side_bar_?`, `rear_panel`/`rear_bumper`) with the `rear_panel` gap waiver |

23 parts, against Art. 4.10.1's six homologated *items* — the difference is
mounting hardware, which the article rolls into "one front fairing mounting kit"
and gate 2 needs individually.
