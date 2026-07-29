# KART_SPEC — the kart, specified before it is built

Issue #190. This document exists because the kart had no design. Every part was
fitted to whichever neighbor happened to be built first, in build order, and the
first number in the chain was invented: `length_overall = 1.830` was sourced
nowhere, `frame.py` derived the whole footprint from it, and `bodywork.py` then
clamped five panels to fit the result — the front fairing is built 512 mm wide
against a regulation minimum of 1,000.

So the rule this document enforces is not "be accurate". It is **every number
says where it came from, and no number is fitted to a neighbor when it could be
fitted to a figure.**

> This file is `docs/kart_spec/00-front-matter.md`. `docs/KART_SPEC.md` is
> generated from this directory by `tools/spec/assemble.py` — edit the section
> files, never the assembled document.

## 1. Provenance — the three tags, and what each one costs

Every number in every section carries exactly one tag.

| tag | meaning |
| --- | --- |
| `sourced` | there is a citation, and **the citation was read rather than recalled**. For a regulation: article number, the quoted sentence, and the PDF page it is on. For a part: the catalog listing or drawing, with what it said. |
| `derived` | arithmetic from `sourced` numbers, **with the arithmetic shown inline**. Not "follows from the wheelbase" — `1050/2 = 525`. |
| `estimated` | no source exists. Carries its reasoning: what proportion it was read off, what photograph or part it was checked against, what it is consistent with. |

One qualifier, added because a measurement agent needed it and inventing it was
the right call:

| qualifier | meaning |
| --- | --- |
| `snippet` | the figure exists only in a search-engine summary of a page that **403s from here** — tkart.it, mondokart and iamekarting all do. Corroborated across more than one search, but **nobody in this project has read the primary text.** |

`snippet` is not a weaker `sourced`; it is an `estimated` that names the document
it would be sourced from if the page could be opened. The recheck pass treats it
as `estimated`. It exists so that "we know where this is written down and cannot
reach it" is recorded rather than laundered into a citation.

**`estimated` is a normal outcome and is not a defect to drive to zero.** This is
not a licensed product and the homologation drawings (TD 2.1a and the HF forms)
are not obtainable, so plenty of numbers here cannot be sourced and are somebody's
best judgment. That was always allowed. The goal is realistic, not licensed.

**The two things that are defects:**

1. An **unmarked** number.
2. An estimate written in **the vocabulary of a limit**. `length_overall = 1.830`
   sat under a docstring reading `Overall length 1830 mm max`. Phrased as a
   regulation ceiling, sourced nowhere. Because it read as a constraint rather
   than as a guess, five panels were clamped to it. The estimate was never the
   bug; the mislabeling was.

The same applies to article numbers. An article number is an externally-sourced
constant: five places in this repo once cited §7.4 where the text says §7.2, all
five tracing to one planning sentence nobody checked. Every article number in
this document was located in the pinned PDF's own text, not remembered.

Pinned source, and the only regulation text this document may cite:

    refs/frontend/fia_karting_technical_regulations_2026.pdf
    read with:  pdftotext -layout <pdf> -

## 2. Coordinate convention, and the origin

Stated once, because getting it wrong is invisible until the kart drives
backwards. Same convention as `tools/blender/kartlib/params.py`:

    Blender  +X = kart right, +Y = kart forward, +Z = up
    export_yup maps (x, y, z) -> (x, z, -y), so Blender +Y is Godot's -Z forward

**Origin**: on the ground, laterally centered, midway between the axles. So the
front axle is at y +0.525, the rear axle at y −0.525, and z is height above the
asphalt.

Units in this document are **millimeters**, because that is what every source is
in. `params.py` is in meters. A row reading 432 is `0.432` in the parameter block.

Sides are stated as the **kart's** left and right, never the image's. On a KZ the
radiator is on the kart's **left** and the engine, exhaust and gear lever are on
the kart's **right**. `docs/REFERENCES.md`'s V3 caption says otherwise and is
wrong (#193): it was written from a dead-rear photograph without mirroring it,
and the mirrored caption is what put `radiator_x` at +0.308 beside `engine_x` at
+0.319 and built the core through three other parts. Every time a lateral offset
is read off a photograph, this document states the viewpoint and states which way
it was mirrored.

## 3. Frozen, and verified rather than assumed

These four are frozen because every §6.4 driving figure and every `drive.sh`
scenario is measured against them, and moving them invalidates the driving work.
Frozen is not the same as unchecked — each is proved against Art. 9.1.1 here.

Art. 9.1.1 *Chassis dimensions*, verbatim, PDF page 22 of the pinned file:

> Wheelbase: 1010.0 - 1070.0 mm.
> Track: at least 2/3 of the wheelbase used.
> Track width: maximum 1400.0 mm.
> Overall length: according to TD n°2.1a
> Height: 650.0 mm maximum from the ground, without the seat.
> The chassis must respect at all times the dimensions given.

| frozen | value | provenance | check against 9.1.1 |
| --- | --- | --- | --- |
| `wheelbase` | 1050 | `sourced` (in range) | inside 1010–1070. **`params.py` calls it "1050 mm max (KZ runs at the limit)" and that is a mislabel — the maximum is 1070.** The value is fine; the word is not. |
| `track_rear` | 1400 | `sourced` | equals the 1400.0 mm track-width maximum. |
| `track_front` | 1240 | `derived` + `estimated` | 1240 ≥ 2/3 × 1050 = 700, so it clears the minimum-track rule with 540 mm to spare. That the front is *narrower than the rear* is KZ practice, not a regulation; the specific 1240 is `estimated`. |
| `length_overall` | **1920** | `derived` | §5. Was 1830, sourced nowhere, labeled "max". |
| tire diameters 280 / 295, widths 135 / 215 | | **`estimated` pending** | see the warning below — these were **not found** in Art. 9 of the 2026 TR. |

**Overall width is not track width, and `params.py` conflates them.** Its docstring
says "Overall width 1400 mm max" and `track_rear`'s says "Also the kart's overall
width limit". Art. 9.1.1 says *"Track width: maximum 1400.0 mm"*, and Art. 9.5.5.1
separately lets the rear protection reach *"the overall rear width"* — the two are
one number only in the maximum case, by coincidence. Same mislabeling class as
`length_overall`, smaller consequence.

**The tire figures are mislabeled, and the diameters are worse than the widths.**
This paragraph replaces an earlier claim in this document that 215 and 135 appear
nowhere in the regulations. They do: **Art. 4.13.1 *Wheel dimensions*, PDF p. 13.**
But that article governs the **wheel** — Art. 2.3.2 defines a wheel as rim plus
mounted tire — and its own footnote says *"maximum wheel dimensions […] at 0.5
bar"*. So 215 and 135 are **inflated rim-plus-tire ceilings**, not tire widths, and
`params.py` uses them as tire widths. Art. **4.15 *Tyres* is one sentence and
contains no dimension at all**; real tire dimensions live in the tire homologation
forms.

Measured from CIK-FIA tire homologation forms **047-TO-12 / 047-TO-14** (Vega XH4,
Groups 1 & 2), page 3, a dimensioned cross-section of the tire fitted to its rim,
all ±5 mm and all `sourced`:

| | diameter | overall width | tread width | rim width |
| --- | --- | --- | --- | --- |
| front | **260** | 130 | 110 | 120 |
| rear | **274** | 207 | 179 | 198 |

Against which `tire_front_diameter` 280 is the Art. 4.13.1 **maximum**, and
`tire_rear_diameter` 295 is **neither the maximum (300) nor any real tire**.

**These stay frozen, and this document flags rather than changes them.** Every
§6.4 driving figure, every `drive.sh` scenario and the whole M3a/M3b tire model are
measured against 280/295; a 20 mm diameter change moves the rolling radius, the
axle heights, the gearing and the center of mass together, and re-deriving that is
not a geometry task. So the spec records the truth beside the value:

| field | built | sourced truth | disposition |
| --- | --- | --- | --- |
| `tire_front_diameter` | 280 | 260 | frozen, **relabel** `estimated` — it is the wheel-dimension ceiling, not a tire |
| `tire_rear_diameter` | 295 | 274 | frozen, **relabel** `estimated` — invented; not even the ceiling |
| `tire_front_width` | 135 | 130 | frozen; 135 is the *wheel* ceiling |
| `tire_rear_width` | 215 | 207 | frozen; 215 is the *wheel* ceiling |
| `rim_diameter` | 127 | 136.2 flange minimum (Art. 4.14) | **bead vs flange conflation** — a module drawing the visible flange at 127 is 9 mm undersize |

Changing the diameters needs its own ticket and a re-measurement of §6.4, and it
is a driving-model change wearing a geometry hat. What is free today is the
**label**: these are `estimated`, and the word "max" comes off.

Two further limits from the same article and its neighbors, which bound parts
elsewhere in this document:

| limit | value | article | note |
| --- | --- | --- | --- |
| chassis height above ground, **seat excluded** | ≤650 | 9.1.1, PDF p. 22 | the steering wheel, the radiator's top edge and the airbox all live under this. |
| rear axle outside diameter | ≤50.0 | 9.2, PDF p. 22 | *"Maximum 50.0 mm outside diameter (wall thickness according to Article 4.3)."* A wall thickness clause means the axle is a **tube**. `params.py` documents `axle_diameter` as "Solid, 50 mm", which is a real error — see §Running gear. |
| fuel tank capacity | ≥8 litres | 9.3, PDF p. 22 | *"8 litres minimum."* The kart currently has **no fuel tank at all**. |

## 4. The bodywork envelope, quoted once

Every bodywork row in §Bodywork cites back to this block rather than re-quoting
it. All from the pinned PDF, Art. 9.5: 9.5.2/9.5.3/9.5.4 on PDF page 24, 9.5.5.1 on PDF page 25.

**9.5.2 Front fairing**
> Minimum width: 1.000 mm. Maximum width: overall rear width of the front
> wheel/front axle unit.
> Maximum gap between the front wheels and the back of the fairing: 180.0 mm.
> Front overhang: 680 mm maximum, see TD n°2.1.
> The front fairing must be placed no higher than the front wheels and must not
> have any sharp edges.
> Only one air vent hole is allowed, its diameter must not exceed 12mm and it
> must be located on the rear face of the front fairing.

**9.5.3 Front panel**
> The front panel must not be located above the horizontal plane defined by the
> top of the steering wheel.
> […] must allow for a gap of at least 50.0 mm between the panel and the
> steering wheel and must not protrude beyond the front fairing.
> Width: 250.0 mm minimum and 300.0 mm maximum.
> The panel's lower section must be securely attached to the front part of the
> chassis frame, directly or indirectly. Its upper part must be securely
> attached to the steering column support with one or more independent bars.

The 50 mm gap is to the **steering wheel**, not to the front road wheel. It is a
hands clearance, and it constrains the panel's rake against `wheel_center_y` and
`wheel_angle`. This was misread once.

**9.5.4 Side bodywork**
> The side bodywork must under no circumstance be positioned above the plane
> defined by the tops of the front and rear tyres and must be located between
> 0mm and 40.0 mm (inwards) from the plane defined by the outer front edge of
> the front wheel and the outer front edge of the rear wheel (with the front
> wheels in the straight-ahead position), in accordance with technical drawing
> 2.1.a.
> The side bodywork must have a ground clearance of 25.0 mm minimum and 60.0 mm
> maximum.
> Gap between the front of the side bodywork and the front wheels: 150.0 mm
> maximum.
> Gap between the back of the side bodywork and the rear wheels: 60.0 mm maximum.
> The side bodywork must not overlap the chassis frame seen from underneath.
> […] must be securely attached to the side bumpers.
> A space for racing numbers must be provided on the vertical surface close to
> the rear wheels.

**The pod datum tapers, and this is the single most consequential line in the
article.** The plane runs through the outer front edge of the *front* wheel and
the outer front edge of the *rear* wheel. With front track 1240 and rear track
1400 those are x = 620 at y +0.525 and x = 700 at y −0.525, so the plane is
`derived` as:

    x_datum(y) = 660 - (y / 1050) * 80      # 620 at the front axle, 700 at the rear
    plan angle = atan(80 / 1050) = 4.36 deg

and the pod's outer face must lie between `x_datum(y) - 40` and `x_datum(y)`.
`sidepod_x` is a single constant, so the pods are currently parallel-sided and
482 mm out — 118 to 160 mm inboard of where they belong, per side. Real pods
splay outward toward the back for this reason, which had been read as styling.

Note also that the fairing's **maximum** width is *"the overall rear width of the
front wheel/front axle unit"*, so the ceiling is `track_front` **1240**, not 1400.
This document dropped that qualifier once and 1400 is the wrong number.

**The datum has two readings and they differ by 11 mm; a pod that spends the full
40 mm is illegal under one of them.** The arithmetic above places the datum points
at the wheel *axis* planes, y ±525. The article's literal words are the outer
front **edge** of each wheel — the tire's forward face, at y +665 and y −672.5 —
which gives

    x_datum_literal(y) = 671.0 - 0.0767 * y      # same plan angle to within 0.03 deg

i.e. **11 mm outboard** of the axis-plane reading everywhere. A face placed 40 mm
inboard of the axis-plane datum is 51 mm inboard of the literal one, and 51 > 40.
So the usable inset budget is **29 mm, not 40**, and every pod dimension in
§Bodywork is specified against that narrower band. This is not an amendment to the
article — it is the same article read two ways, and the spec takes the conservative
intersection because a scrutineer holding a straightedge to the tire is the
literal reading.

**9.5.5.1 Rear wheel protection**
> Width: minimum 1.340 mm, maximum that of the overall rear width, at any time
> and under any circumstance.
> Ground clearance: 25 mm minimum and 60.0 mm maximum in at least three spaces
> of a 200.0 mm minimum width, located in the extension of the rear wheels and
> the centreline of the chassis.
> Rear overhang: 400.0 mm maximum.
> Gap between the front of the rear wheel protection and the surface of the rear
> wheels: 15.0 mm minimum and 50.0 mm maximum.
> The two adjustable outer parts of the homologated rear wheel protection must
> have a color that is clearly different from the main part of the rear wheel
> protection.
> The rear wheel protection must be placed no higher than the rear wheels.

**Art. 9.4 Bumpers** (PDF p. 22)
> Front and side protections are compulsory. They must be made of magnetic steel
> round tubing and be homologated with the bodywork.

**Art. 9.4.1 Front bumper** (PDF p. 23). Two bars, at two different heights, and
an earlier version of this section got both the height and the attachment spacing
wrong — see the correction note below.
> Height: 200.0 mm minimum and 250.0 mm maximum from the ground (measured to the
> tubing top).  ← **upper** bar
> [a] lower bar with a minimum diameter of 20.0 mm and two corner bends with one
> constant radius. The straight length between the bends must be 295.0 mm minimum
> and 315.0 mm maximum.
> Height: 70.0 mm minimum and 110.0 mm maximum (measured to the tube top).
> ← **lower** bar
> The bar must be fixed to two welded chassis frame attachments, which must be
> **450.0 mm apart** and centred on the kart's longitudinal axis. The attachments
> must be horizontally and vertically parallel to the kart's axis and allow for a
> 50.0 mm insertion of the bar.
> Front overhang: 350.0 mm minimum.
> Both bars must be connected by the front bumper support.
> The front bumper must be independent from the pedal attachment and allow for the
> mounting of the mandatory front fairing.

**Art. 9.4.2 Side bumpers** (PDF p. 23)
> The side bumper consists of two elements of magnetic steel round tubing that are
> centred in relation to the longitudinal axis of the kart. Each element must be
> composed of a lower and an upper bar. They must have a diameter of 20.0 mm.
> Minimum straight length is 400.0 mm for the lower bar and 300.0 mm for the upper
> bar.
> Overall width: 480.0 mm minimum and 520.0 mm maximum for the lower bar, 480.0 mm
> minimum and 600.0 mm maximum for the upper bar (measured to the tube midpoint)
> in relation to the longitudinal axis of the kart.
> Each bar must be fixed to two welded tube attachments that must be 500.0 ± 5 mm
> apart (measured to the tube midpoint).
> Height of the upper bar: 160.0 mm minimum from the ground (measured to the tube
> top). See TD n°2.0.

Front overhang carries **two** limits in two different articles and they are not
the same kind of number: 9.4.1 sets a **minimum of 350** on the front bumper, and
9.5.2 sets a **maximum of 680** on the front fairing. Both apply.

**Correction, and it is the §7.2/§7.4 failure happening inside this document.**
Two numbers above were wrong when first written here and were relayed to two
section agents before being caught:

1. The attachment spacing was quoted as **550.0 mm**. The text says **450.0 mm**
   for the front bumper and **500.0 ± 5 mm** for the side bumpers. There is no 550
   anywhere in Art. 9.4. §5b separately claims the OTK M4 form's 550 mm tube
   spacing "matches Art. 9.4.1" — it does not; the form's **450** is the one that
   matches, and the 550 is an unregulated second pair.
2. The **160 mm minimum** was attributed to 9.4.1 and to the *front* bumper. It
   belongs to **9.4.2**, and it governs the **side** bumper's upper bar. The front
   bumper's own limits are 200–250 (upper) and 70–110 (lower), both to the tube
   **top**.

The practical consequence is the opposite of what was relayed: a front nose bar
with its tube center at z 150 — top 160 — is not a legal minimum, it is **50 mm
above the 110 mm ceiling** on the lower bar and 40 mm below the 200 mm floor on the
upper. §Bodywork's "nose hoop demand" is specified against the withdrawn number and
is superseded by §Chassis, which places the lower bar's top at 95 and the upper
bar's top at 225. **The fairing picks up on the upper bar**, which is the only one
in the right height band to carry it.

Both errors were caught by a section agent reading the PDF rather than reading
this file, which is the entire reason §1 forbids citing project text.

**Do not cite Art. 9.5.4.1 for side bodywork — it does not exist.** The FIA's own
text has a dangling cross-reference: Art. 8.5.4.1 (PDF p. 21) says "See Article
9.5.4.1", and the article set is 9.5.4 with a 9.5.4.2 for the wheel-cover
variant. The correct citation for the pod datum is **9.5.4**.

## 5. Overall length — the number this whole document was blocked on

**1,920 mm, `derived`.** Range 1,830–2,040; regulation ceiling 2,130. Issue #191.

Art. 9.1.1 defers overall length to TD n°2.1a, which is a drawing and is not in
the PDF, and **no manufacturer publishes an overall length** — CRG, Tony Kart /
OTK, Birel ART, Kart Republic, IPK, Parolin and six dealers were checked, and
every spec sheet lists wheelbase, tube diameter, axle diameter and frame widths
and then stops. What *is* published, per-part and dimensioned, is the CIK-FIA
**homologation form** set, and that is what this number is built from.

**The overhang datum is the wheel axis lines.** It is defined nowhere in the
technical regulations or in the 2025 Homologation Regulations — both were grepped.
It is fixed by the chassis homologation form's 1:10 frame drawing, where `A`
(wheelbase) spans the two wheel-axis lines and `G1`/`G2` (rear/front overhang) are
dimensioned from those same lines: rear axle centerline and front stub-axle
centerline. That is also the only reading under which 680 + 1050 + 400 = 2,130 is
self-consistent.

    front overhang = front tire radius 140 + gap g_f + fairing depth D_f
      D_f = 287 (OTK M4 HF) or 317 (KG 505 HF);  g_f <= 180 (Art. 9.5.2)
      => 427 .. 637, capped at 680 by Art. 9.5.2
      photogrammetric, tonykart_racer401T_product.png, anchored on
      wheelbase = 397 px = 1050 mm:  (705 - 514.5) px = 504 mm
    rear overhang  = rear tire radius 147.5 + g_r + protection depth 187 (KG C2 HF)
      g_r = 15 .. 50 (Art. 9.5.5.1)  =>  349.5 .. 384.5, under the 400 cap
    overall = 504 + 1050 + 367 = 1921 mm  ->  1920

The rear half of that is from published part depths, not from the photograph, and
deliberately: the same photo puts the rearmost feature only 295 mm behind the rear
axle, **below the 349.5 mm floor any homologated rear protection forces**, so that
kart was shot with no rear protection fitted — rear bumper tube and silencer only.
Anyone measuring rear bodywork off that image comes up ~60 mm short.
Independently, scaling that photo by tire diameter instead gives 3.01 mm/px and
implies a 1,195 mm wheelbase, which Art. 9.1.1 caps at 1,070 — so the image has
real perspective error and only the wheelbase-anchored ratio is usable.

Frame-only footprint from two chassis forms, as a sanity floor: CRG Road Rebel
250 + 1050 + 210 = 1,510; Gillard TG16 275 + 1046 + 210 = 1,531.

**1,830 was not wrong by much** — it sits exactly on the bottom edge of the
derived range, reachable only with the shallowest fairing at zero gap. It was
unsourced and mislabeled. 1,920 is the honest center of the same range.

## 5a. The cooling envelope — Art. 5.3, and it is harder than the bodywork's

KZ is **Group 2**. Art. 5.3.1 *Radiator*, PDF page 15, verbatim, Groups 1 & 2:

> Radiators must be placed above the chassis frame at a maximum height of 500 mm
> from the ground and within an area situated between 550mm and 10mm ahead of the
> rear-wheel axle. They must not interfere with the seat.
> Any radiator placed at the rear must not be located less than 150 mm from the
> lateral extremities of the kart.
> All tubing must be made of a material designed to withstand heat (150 °C) and
> pressure (10 bar).
> To control the temperature, a system of fairings and covers may be placed at the
> front or rear of the radiator(s). This device may be adjustable, but it must not
> be detachable when the kart is in motion or comprise dangerous parts.

Art. 5.3.2 *Water pump*, same page: *"In Groups 1 & 2, the water pump must be
mechanically [driven]"* — which is what makes the axle-driven toothed belt the
correct choice rather than a styling one.

Four hard constraints fall out, all `derived` from the quote with the rear axle at
y −525:

| constraint | numeric form | current build |
| --- | --- | --- |
| top edge ≤500 above ground | `z_top <= 500` | 497 — **3 mm of margin**, and it is the reason the core reads as standing proud |
| fore-aft window, 10 to 550 mm **ahead of** the rear axle | `-515 <= y <= +25` | core spans y −375 … −95, inside |
| ≥150 mm from the kart's lateral extremities | `|x_outboard| <= 700 - 150 = 550` | outboard edge at −489, 61 mm of margin |
| **must not interfere with the seat** | zero contact with `seat_shell` | this is a **gate 1 assertion, inverted**: the radiator and the seat must *not* be a declared joint. The radiator hangs off the seat **stays**, not off the shell. |

That last row is the first case in this document of a regulation forbidding a
joint rather than requiring one, and gate 1 already expresses it: any overlap
between `radiator_*` and `seat_shell` is fatal because no `Joint` may be declared
between them.

## 5b. Bodywork dimensions that are actually published

These come from homologation forms in `refs/kart-visual/`, hash-pinned in
`sources.txt`. They are `sourced`, they are per-part, and they are the only real
bodywork dimensions this project has ever had. Every panel in §Bodywork is fitted
to these rather than to a chassis tube.

| part | form | width | fore-aft depth | height |
| --- | --- | --- | --- | --- |
| front fairing | OTK M4, `100-CA-20` | 1090 | 287 | 227 |
| front fairing | KG 505, `2-CA-20` | 1029 | 317 | 203 |
| rear wheel protection | KG C2, `003-BR-48` | 1360 | 187 | 177 |

Both fairings clear the 1,000 minimum; the KG C2 clears the 1,340 minimum by
20 mm. The fairing support tube mounts are at **450 and 550 mm** spacing, Ø20×1.5
and Ø16×1.5 magnetic steel — and the 550 matches Art. 9.4.1's *"550.0 mm apart
and centred on the kart's longitudinal axis"*, which is a cross-check between a
form and the text rather than an assumption.

Frame widths, same forms, and note these are **frame** widths, not track: CRG Road
Rebel outer front 735 ±10, outer rear 650 ±10.

## 6. How to read a part entry

Every part gets one block. The four columns #190 asks for are the four bold
labels, and none of them is optional.

    ### `part_name`
    **Status:** built | new | renamed from `old_name` | delete
    **Attaches to:** `other_part` (welded), `another` (bolted, 4× M8)
    **Envelope:** Art. 9.5.4 — the pod datum, §4 above. | none
    **Verification:** gate 1, gate 2 (declared joints), `genkart.sh --check`

    | dimension | `params.py` field | value | prov | basis |
    | --- | --- | --- | --- | --- |
    | outer diameter | `tube_main` | 30 | `sourced` | Art. 4.x, quoted; p. N |

- **Status** — `built` means a mesh of that name exists in
  `assets/generated/kart.json` today. `new` means it does not and must be
  created; a real KZ has parts this kart does not (no fuel tank, no brake disc,
  no caliper, no tie rods, no front panel) and their absence is a spec item, not
  an omission.
- **Attaches to** is what makes a floating steering column a build failure
  instead of a screenshot. It is not prose: **each entry becomes a `Joint` row in
  `tools/blender/kartlib/joints.py`**, and one declaration serves both #192
  gates — a declared joint *permits* interpenetration and *requires* contact
  within 2.0 mm. A part that touches nothing has to say so and say why.
- **Envelope** is the article that constrains the part, or the word `none`.
  `none` is a legitimate and common answer; most of a chassis is unregulated
  beyond the tube material.
- **Verification** names the gate that proves the claim, so that a number in
  this table is checkable at 3am without a human looking at a viewport.

## 7. The gates this document is written against

| gate | asserts | fails on |
| --- | --- | --- |
| winding | every watertight part encloses positive volume | a mesh wound inside out, which no render shows because materials export `doubleSided` |
| **gate 1 — interpenetration** | no part is built inside another, except at a **declared joint** | world-space triangle overlap between two parts with no `Joint` between them |
| **gate 2 — attachment** | every part touches something within 2.0 mm, and every declared joint's two parts touch within 2.0 mm | a floater, or a part resting on the wrong neighbor |
| `genkart.sh --check` | the whole build is deterministic | any nondeterminism in reporting order or geometry |

Both new gates are fatal and both run in the geometry stage, so `--watch`
reports them on every save. Known-outstanding defects are itemized waivers in
`joints.py` carrying their measured number and an issue number; a waiver that
stops failing is itself an error, so the list cannot rot.

## 8. Assemblies

| § | assembly | section file |
| --- | --- | --- |
| 10 | Chassis — rails, cross members, hoops, bumpers, floor tray, seat struts | `10-chassis.md` |
| 20 | Running gear — wheels, tires, hubs, stub axles, kingpins, rear axle, bearings, brakes | `20-running-gear.md` |
| 30 | Powertrain — engine, intake, ignition, clutch, chain, sprockets, exhaust, cooling | `30-powertrain.md` |
| 40 | Cockpit — steering, seat, pedals, controls, gear lever, fuel tank | `40-cockpit.md` |
| 50 | Bodywork — front fairing, front panel, side pods, rear protection, number panels | `50-bodywork.md` |
| 60 | Driver package and finishes — driver, materials, livery zones | `60-driver-and-finishes.md` |
