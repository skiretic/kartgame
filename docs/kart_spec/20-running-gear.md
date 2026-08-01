# 20. Running gear — wheels, tires, hubs, the front end, the axle, the brakes

Issue #190. Read `00-front-matter.md` first: the provenance vocabulary, the
coordinate and origin convention and the part-entry format are fixed there and
are not restated here. Millimeters throughout; `params.py` is in meters.

**Scope.** Everything between the asphalt and the chassis frame, plus everything
that slows the kart down. The steering *column*, the pitman and the pedals belong
to §Cockpit; the parts **outboard of the tie-rod ends** are here, and so are the
tie rods themselves because they are what the knuckle arms pull on. The rear
sprocket is here because it is keyed to this section's axle; the chain, the
gearing and the output sprocket are §Powertrain's.

**What is built today.** Twelve parts: `wheel_fl_rim`/`_tire` and its three
siblings, `axle_rear`, `axle_sprocket`, `axle_stub_fl`, `axle_stub_fr`.

**What is missing.** Sixty-four parts, and most of them are legally mandatory.
The kart has no brake system at all — a brake pedal that pushes nothing, no
master cylinder, no disc, no caliper, no line. It has no kingpins, no steering
knuckles, no knuckle arms, no tie rods and no rod ends, so the front wheels are
held on by a 90 mm spindle that ends in mid-air. It has no wheel hubs at either
end, no axle bearings, no bearing cassettes and no keyways. Three of the absent
parts exist **because a rule says so** and are the three most likely to be left
out by anyone modeling from photographs: the Art. 4.12.4 rear-disc protective
pad, the Art. 4.12.2 redundant pedal-to-pump link, and the Art. 4.14.1 bead
retention pegs.

This section settles three things, in §20.2, §20.3 and §20.6. The rest is part
entries.

---

## 20.1 The regulation block, quoted once

Every row below cites back to this block instead of re-quoting it. All from the
pinned PDF, `refs/frontend/fia_karting_technical_regulations_2026.pdf`,
sha256 `5db20b68…b45060`, read with `pdftotext -layout`. Every article number here
was located in the PDF's own text during this pass, not recalled — the front
matter's §1 rule, and the reason it exists.

**Art. 4.2.1 *Chassis main parts*, PDF p. 7**
> The chassis main parts transmit the track forces to the chassis frame through
> the tyres. They include:
> - the wheels with hubs;
> - the rear axle;
> - the steering knuckle; and
> - the king pin.
> See TD n°1.0.

Four items, and this section owns all four. It is also the article that makes the
absent hubs, knuckles and kingpins a **structural** omission rather than a
cosmetic one.

**Art. 4.2.2 *Main parts requirements*, PDF p. 7/8**
> The chassis main parts must be securely attached to each other or to the
> chassis frame. A rigid construction is mandatory: no articulations or flexible
> joints are allowed.
> Articulated connections are only allowed for the steering knuckle (through the
> king pin) and the steering.

This is the one place on the kart where an articulation is *permitted*, which is
why `knuckle_??`/`kingpin_??` is the only rotating joint in this section other
than the wheels themselves.

**Art. 4.3 *Rear axle*, PDF pp. 8–9**
> The rear axle diameter must comply with the category in which the kart is
> entered. In all categories, the rear axle must be made of magnetic steel.
> Each rear axle must have, on the inside and outside, a rounded edge or a
> chamfer with a maximum diameter corresponding to the axle thickness. The
> chamfer must not have sharp edges.
> In KZ/KZ2, the rear axle must only have four keyways: one each for the left and
> right hub, one for the brake disc and one for the rear axle sprocket.
> Rear axles with pinned keys and no keyways are not affected by the above
> regulation. […]
> Each rear axle is required to bear a CIK-FIA identification sticker specific to
> the manufacturer (see Appendix 10).
> The axle wall thickness depends on the outside diameter of the axle. It must
> comply with the following criteria at all points (except the keyways):

and the table's first row, PDF p. 9:

| maximum outside diameter (mm) | minimum wall thickness |
| --- | --- |
| **50.0** | **1.9** |
| 49.0 | 2.0 |
| … | … |
| 30.0 | 4.9 |
| 29.0 | 5.2 |
| >28.0 | full |

Three consequences, all of them things the build gets wrong:

1. **A 50 mm rear axle is a tube with a ≥1.9 mm wall.** `params.py` documents
   `axle_diameter` as *"Solid, 50 mm"*. That is a real error and the front matter
   already flagged it from Art. 9.2's *"wall thickness according to Article
   4.3"*; this is the table the cross-reference points at. Only the smallest
   axles are solid.
2. **The keyway count is four, and it is why the disc and the sprocket cannot be
   coplanar.** §20.5 places all four.
3. **Both axle ends carry a chamfer** whose diameter is at most the wall
   thickness, with no sharp edge. That is a 1.9–2.5 mm chamfer, and it is the
   only detail on the axle a camera ever sees.

**Art. 4.4 *Pedals/pedal kits*, PDF p. 9**
> Whatever their position, pedals must never protrude in front of the chassis,
> including the bumper.
> The brake pedal must be placed in front of the master cylinder.

The only constraint on master-cylinder position is an **ordering**, not a
distance. §20.6.4.

**Art. 4.5.3 *Steering arms*, PDF p. 10**
> Steering arms may be made adjustable with rose joints on each end of the arm.
> They must be made of aluminium or steel and securely attached with self-locking
> nuts and bolts.

**Art. 4.12 *Brakes*, PDF p. 12.** 4.12.1 is *"Article reserved"*.

**Art. 4.12.2 *Brake control*, PDF p. 12**
> The brake control, i.e. the link between the pedal and the pump(s), must be
> doubled for safety and always be in conformity with the HF of the chassis it is
> homologated with.
> If a cable is homologated, it must have a minimum diameter of 1.8 mm.

**This is a mechanical redundancy rule, not a two-circuit rule.** It is about the
pedal-to-pump link. The CRG Road Rebel chassis form `04-CH-14` devotes its whole
page 4 to it — *"PHOTO OF BRAKE CONTROL CABLE / The brake control must be
separated from chassis and show the double linkage"* — so it is a photographed,
homologated feature and not a footnote. 1.8 mm is a **floor and not a practice**;
`brake_pushrod_link` is specified at 2.0 mm and says so.

**Art. 4.12.3 *Brake discs*, PDF p. 12**
> Brake discs from steel, stainless steel or cast iron are allowed.
> The surface of the brake discs may be modified by grinding, drilling, grooving,
> but only by the manufacturer and under his sole responsibility. Modified brake
> discs must comply with the dimensions described in the HF.

Drilled and slotted is legal. Carbon and ceramic are not.

**Art. 4.12.4 *Brake disc protective pad*, PDF p. 12**
> An efficient rear brake disc protective pad (in nylon, carbon fibre, Teflon,
> Kevlar, Delrin or equivalent hard plastic) is mandatory in Groups 1, 2 & 3 if
> the brake disc protrudes below or is level with the main chassis frame tubes
> nearest to the ground. This protection must be placed laterally in relation to
> the disc, in the longitudinal axis of the chassis or under the disc.

**This kart triggers it, and not marginally.** `derived`: `rail_z` is 50 and
`tube_main` is 30, so the rails occupy z 35…65. A Ø195 disc concentric with the
rear axle at the built `rear_axle_z` of 147.5 has its bottom edge at
147.5 − 97.5 = **50.0**, i.e. dead level with the rails' centerline and 15 mm
inside their vertical extent. At the sourced rear tire diameter (Ø274, axle
z 137) it is 39.5, i.e. 4.5 mm above the rails' underside and still level with
them. Either way "level with" is satisfied and the pad is **mandatory**.

**Art. 4.12.5 *Rain covers*, PDF p. 13** — *"callipers and disks may be fitted
with professionally made rain covers attached to the stub axle."* Not modeled;
noted because it is the article that confirms the caliper's bracket lives on the
stub-axle side of the joint and not on the rim's.

**Art. 4.12.6 *Brake cooling*, PDF p. 13** — a rear-only cooling tube, *"not
reach further than the seat and not extend under the chassis"*. Not modeled.

**Art. 4.13 *Wheels*, PDF p. 13** — the whole article is quoted in the front
matter's §3 discussion. The one line this section builds to:
> The attachment of the wheels to the hubs and axles must be done via M8
> self-locking nuts and bolts.

**Art. 4.13.1 *Wheel dimensions*, PDF p. 13.** Groups 1 & 2, 5-inch wheel:
maximum outer diameter **280.0 front / 300.0 rear**, maximum width **135.0 front
/ 215.0 rear**, and the footnote *"The above figures are maximum wheel
dimensions, with a matching tyre fitted on the rim and an air pressure of
0.5 bar."* Art. 2.3.2 defines a wheel as rim plus mounted tire. So these are
**inflated rim-plus-tire ceilings**, and §20.2 is about what happens when they
are used as tire dimensions.

**Art. 4.14 *Rims*, PDF p. 13**
> In Groups 1, 2 & 3, only 5-inch rims complying with TD n°1.1 are allowed.
> Coupling diameter of the tyre for the rim: 126.2 mm with a +0/−1 mm tolerance
> for the diameter.
> Width of tyre housing: min. 10.0 mm.
> External diameter for 5-inch rims: 136.2 mm minimum.
> Radius to facilitate the balance of the tyre in its housing: 8 mm.

Two different diameters in one article, and conflating them is §20.2's fourth
row. Note the tolerance direction: 126.2 **+0/−1**, so 126.2 is a *maximum* for
the bead seat and 136.2 is a *minimum* for the flange.

**Art. 4.14.1 *Bead retention*, PDF pp. 13–14**
> In Groups 1 & 2, the front and rear wheels must have some form of bead
> retention with at least three pegs in the outside part of the rim.

Mandatory, on all four wheels, and **absent** from all four rims.

**Art. 4.15 *Tyres*, PDF p. 14** — one sentence, *"CIK-FIA homologated tyres are
mandatory in all categories."* No dimension anywhere in it. Tire dimensions live
on the per-tire homologation form, which is where §20.2's real figures come from.

**Art. 4.17 *Wheel hub*, PDF p. 14**
> The sole purpose of the wheel hub is to enable the transfer of forces between
> the rim and the chassis.

**Art. 8.6 *Brakes* (Group 1 = KZ), PDF p. 21**
> Brakes are free in Group 1, but must comply with Articles 4.12 et seq. of the
> TR. They must be produced by a manufacturer with a valid brake homologation.

**Art. 9.6 *Brakes* (Group 2 = KZ2), PDF p. 26**
> All brakes in Group 2 must be homologated by the CIK-FIA.
> The following brake types must be used:
> 2WP B2 or BRKR in all OK non-gearbox classes; 4WP B4 or "BRKF + BRKR (within
> the same make)" in the KZ2 gearbox classes.

**Cite 9.6 for four-wheel brakes, and nothing else.** The explicit requirement is
Art. 9.6 and it names **KZ2 / Group 2**. For KZ itself (Group 1) Art. 8.6 says
brakes are *free*, subject to 4.12 and to a homologated manufacturer — there is
no Art. 8 rule compelling four wheels, and citing one would be inventing an
article. `4WP` is four wheels braked, `2WP` is two. Four wheels are modeled
because every KZ chassis is sold that way and none has run rear-only in decades,
and the citation is 9.6. This corrects issue #190's ticket text, which asserted
the requirement without an article. It is the same failure family as the five
places in this repo that once cited §7.4 where the text says §7.2, and as the
nonexistent Art. 9.5.4.1 the front matter caught.

**Art. 8.7 / 9.7 *Wheels*, PDF pp. 21 / 26** — both are the same sentence: only
5-inch rims with CIK-FIA homologated 5-inch tires, *"See Articles 4.13-4.15 of
the TR."* There is **no** KZ- or KZ2-specific tire rule anywhere.

---

## 20.2 The tires are wrong, they are frozen, and both facts are recorded

`refs/kart-visual/notes_running.md` did the sourcing off **CIK-FIA tire
homologation forms 047-TO-12 and 047-TO-14** (Vega XH4 CIK Option, 10x4.60-5
front and 11x7.10-5 rear, Groups 1 & 2, 20/09/2023), page 3 of each, *"Drawing
of tyre cross section and dimensions of the tyre fitted to a rim"*. That is a
dimensioned orthographic drawing with tolerances, and it is the best tire
material this project has. All figures ±5 mm, all `sourced`:

| | overall diameter | overall mounted width | tread width | rim width |
| --- | --- | --- | --- | --- |
| front | **260** | 130 | 110 | 120 |
| rear | **274** | 207 | 179 | 198 |

The 11x7.10-5 rear is the **entire** homologated 5-inch slick field for 2024–2026
— LeCont, Maxxis, Dunlop, Mojo, Shinko, MG and Vega all list it — so the rear
figures are not one maker's choice. The front is 4.50 or 4.60 depending on maker,
about 2.5 mm of section width apart, inside the form's own ±5 mm.

### 20.2.1 The disposition table

Follows `00-front-matter.md` §3 exactly. Nothing in this row set changes a built
value; the columns are the built number, the sourced truth, the delta, and what a
change would invalidate.

| field | built | sourced truth | delta | disposition | what a change invalidates |
| --- | --- | --- | --- | --- | --- |
| `tire_front_diameter` | 280 | **260** (047-TO-12 p3) | +20 (+7.7%) | **frozen, relabel `estimated`.** 280 is the Art. 4.13.1 wheel-dimension *maximum*, not a tire. The word "max" comes off. | `front_axle_z()` = d/2, so the whole front rolling radius, ride height and every steering-jacking figure. `src/core/steering.h`'s `FRONT_WHEEL_RADIUS = 0.140` and `src/core/chassis.h`'s `FRONT_AXLE_Z` are the same number in the solver. |
| `tire_rear_diameter` | 295 | **274** (047-TO-14 p3) | +21 (+7.7%) | **frozen, relabel `estimated`.** 295 is neither the Art. 4.13.1 maximum (which is **300**) nor any real tire. Nothing in the regulations says 295. Invented. | `rear_axle_z()`, so the rear rolling radius — hence road speed per engine rpm, the whole §6.4 gearing table, and the CoM height. |
| `tire_front_width` | 135 | **130** | +5 | frozen; 135 is the *wheel* ceiling, 4% over the real tire | `front_hub_x()` = (track − width)/2, so the front wheel stations and `chassis.h`'s `FRONT_HALF_TRACK = 0.5525`. |
| `tire_rear_width` | 215 | **207** | +8 | frozen; 215 is the *wheel* ceiling | `rear_hub_x()`, and the rear track the tire model loads. |
| `rim_diameter` | 127 | **126.2 +0/−1** bead seat (Art. 4.14) | +0.8 | **fixable now**, see §20.2.2 | nothing. Read only by `wheels.py`. |
| `tire_sidewall_bulge` | 8 (widest point at r 71.5) | widest point at r **≈ 101** (047-TO-14 p3, derived) | −29.5 radially | **fixable now**, see §20.2.3 | nothing. Read only by `wheels.py`. |

**Why the four dimensions are frozen and not fixed.** Every §6.4 driving figure,
every `drive.sh` scenario and the whole M3a/M3b tire model were measured against
280/295/135/215. A 20 mm diameter change moves the rolling radius, the axle
heights, the gearing and the center of mass **together**, and `chassis.h` and
`steering.h` each carry their own copy of the affected numbers. Re-deriving that
is a driving-model change wearing a geometry hat. It needs its own ticket and a
re-measurement of §6.4, and this document flags rather than changes.

What is free today is the **label**. These four are `estimated`, they are not
maxima, and a real KZ wheel sits about 7% under the diameter cap and 4% under the
width cap — which is the assertion worth keeping, because it is what catches a
future edit going out of bounds.

### 20.2.2 The rim: bead versus flange, and the notes are wrong about this one

`notes_running.md` recommends splitting `rim_diameter` because *"if any module
draws a rim flange at 127 mm it is 9 mm undersize"* against the Art. 4.14 minimum
of 136.2. **Measured, this build does not have that defect.** `wheels.py`
`_rim_barrel_profile` builds the bead seat at `rim_diameter/2 − RIM_SEAT_CLEARANCE`
= 63.44 mm and then adds `RIM_FLANGE_LIP = 6` to get the flange lip at radius
**69.44 mm, i.e. Ø138.9** — which clears the 136.2 minimum by 2.7 mm. The visible
flange is already legal, by accident, because the lip constant happens to be the
right size.

The real defect is smaller and the other way round: the **bead seat** is built at
Ø126.9 where Art. 4.14 gives 126.2 **+0/−1**, so it is **0.7 mm over the
permitted maximum**. A tire bead diameter is a fit, not a styling choice.

`rim_diameter` is read by `wheels.py` and by nothing else — not `src/`, not
`scripts/`, not `lookdev.gd`. **It is a pure visual number the physics never
reads, and it can be fixed now.** The fix is two parameters with unambiguous
names:

    rim_bead_diameter   = 0.1262   # sourced, Art. 4.14, +0/-1 mm; a fit
    rim_flange_diameter = 0.1362   # sourced, Art. 4.14, minimum; what is visible

and `RIM_FLANGE_LIP` becomes derived as `(flange − bead)/2 = 5.0` rather than an
authored 6.0. Net visible change: the flange lip shrinks 1 mm and the bead seat
0.35 mm in radius. `genkart.sh --check` and gate 1/gate 2 are the only gates that
see it.

### 20.2.3 The sidewall bulge: also visual, also fixable now

`tire_sidewall_bulge = 0.008` is documented precisely — the widest point sits at
`rim_diameter/2 + bulge`, radial reading, stated because the parameter has two
plausible meanings. The docstring is right and the value is wrong: 63.5 + 8 =
**71.5 mm**, against a measured **≈101 mm** on 047-TO-14 (`derived` in the notes:
63.1 + (950−632) px ÷ 8.34 px·mm⁻¹). That is **29.5 mm too low radially**, and it
is what makes the built tire read as a cylinder with a chamfer instead of a slick
that bulges past its rim and then tucks back in.

The measurement is on the rear. For the front the same station is consistent:
mid-sidewall on a Ø260 tire over a Ø136 flange is (130 + 68)/2 = 99 mm, so a
single widest-point radius of ~101 serves both ends, which is why one parameter
is enough. (`derived`.)

Read only by `wheels.py` `_tire_profile`. **Fixable now**, no physics reads it:

    tire_sidewall_bulge = 0.038    # derived: 101.0 - 63.1 = 37.9 mm, 047-TO-14 p3

with one warning for whoever owns `wheels.py`: the profile runs the sidewall from
`shoulder_top` down to `bulge_radius` and then turns in to the bead over
`TIRE_BEAD_INSET = 0.014` axially. Raising the bulge by 29.5 mm shortens the
sidewall run from 54 mm to 24 mm on the rear and steepens the bead turn-in to a
38 mm radial drop in 14 mm of axial travel. That is roughly what a real slick's
lower sidewall does, but `TIRE_BEAD_INSET` should be re-checked against the form
in the same pass rather than left alone.

The axial shape is also sourced and also worth building: rear 207 overall > 198
rim > 179 tread, front 130 > 120 > 110. So **the sidewall stands 4.5 mm proud of
each rim flange and the tread band tucks 9.5 mm inboard of it** (front: 5 and 5).
`wheels.py`'s `TIRE_SIDEWALL_LEAN = 0.004` is the parameter for the first half of
that and there is none for the second — the built tread band is
`half_width − LEAN − shoulder` wide, which is a shoulder-radius accident rather
than the sourced 179/110.

---

## 20.3 The front track chain, which does not close by 142.5 mm

`notes_column.md` §9 item 4 reported this and could not resolve it: kingpin
flanges 639 mm apart on the CRG plan view, a 1240 mm front track needing the tire
centerline 230 mm outboard of the kingpin, and `stub_axle_length = 0.090`. Here
is the full lateral chain, one side, outward from the centerline, with the
arithmetic.

### 20.3.1 Where the kingpin actually is

Three independent lines of evidence, and they agree.

**1. From two sourced catalog part lengths.** `derived`.
The pitman's tie-rod hole offset from the column axis is **50 mm** (`sourced`:
OTK's "38/50" designation means *"the centre distance of the holes of the steering
unibol from the centre of the column is 38/50 mm"*, and a KZ runs the outer hole).
The tie rod is **270 mm** eye to eye (`sourced`: OTK "STEERING TIE-ROD 270 mm").
The knuckle arm points **straight rearward** from the kingpin, so the outer rod
end shares the kingpin's lateral station (`sourced` as shape: measured on both
sides of `col_crg_form_planview_1417.jpg`, both reading their own kingpin flange's
lateral coordinate to within 1 px). The rod's fore-aft and vertical components are
20 mm each, so its lateral span is

    sqrt(270^2 - 20^2 - 20^2) = 268.5 mm
    kingpin_x = 50 + 268.5 = 318.5  ->  320

**2. Direct measurement.** `sourced` (photogrammetric, scale stated). The two
front kingpin flanges are **639 mm ± 20** apart on the same plan view at
1.1236 mm/px, a scale fixed twice on that image — off the 1050 mm wheelbase and
off the 50 mm axle diameter, agreeing to 0.4%. Half of 639 is **319.5**.

**3. Consistency with the frame.** `sourced` + `derived`. CRG Road Rebel
homologation form `04-CH-14` section B publishes **E = outer front width =
735 ±10** and **B = main tubes 32 ±0.5**. Half of 735 is 367.5, so the kingpin at
320 sits **47.5 mm inboard of the frame's outermost front surface** — one 32 mm
tube plus a 15.5 mm yoke plate stack, which is what a kingpin yoke welded to the
inboard side of the tube end measures. The frame does not contradict the 320; it
is the third reading that makes it worth adopting rather than averaging.

**Adopted: kingpin axis at x = ±320, i.e. 640 mm apart.** `derived`, ±10.

### 20.3.2 The chain, outward

All at y +525, z +140 (built front axle height). Built values in the middle
column; the frozen `track_front` of 1240 and the built `tire_front_width` of 135
are the two inputs that are not negotiable.

| # | station | x | provenance | basis |
| --- | --- | --- | --- | --- |
| 0 | centerline | 0 | — | — |
| 1 | front cross member, tube centerline | 351.5 | `derived` | E/2 − tube/2 = 367.5 − 16 |
| 2 | frame's outermost front surface | 367.5 | `sourced` | E = 735 ±10, halved, `04-CH-14` §B |
| 3 | **kingpin axis** | **320** | `derived` | §20.3.1, three ways |
| 4 | knuckle body, outboard face | 345 | `estimated` | 25 mm half-width; the casting has to house a 17 mm spindle boss and two kingpin bushes, and 25 mm is what the plan view's knuckle mask supports at 1.1236 mm/px |
| 5 | stub axle, exposed run to the hub's inboard end | 345 → 435 | `estimated` | **90 mm — and this is what `stub_axle_length` actually measures.** `wheels.py`'s own `STUB_DIAMETER` docstring says so: *"25 mm is the carrier, which is what is actually visible between the kingpin and the hub"* |
| 6 | front hub, inboard end | 435 | `derived` | 345 + 90 |
| 7 | front hub, wheel mounting flange = rim plate plane | 552.5 | `derived` | `front_hub_x()` = (1240 − 135)/2 |
| 8 | rim bead-seat center plane | 552.5 | `derived` | the rim is symmetric about its own plate, `wheels.py` module docstring |
| 9 | rim inner flange | 495.0 as built, 492.5 at the sourced 120 mm rim width | `derived` | 552.5 − (67.5 − 14 + 4) |
| 10 | tire inner face | 485.0 | `derived` | 552.5 − 135/2 |
| 11 | tire outer face | 620.0 | `derived` | 552.5 + 67.5; ×2 = `track_front` 1240 by construction, Art. 2.3.3 *"outer planes of each wheel on the same axle"* |

### 20.3.3 Which link is wrong: none of them. Two are missing.

The **spindle arm** — kingpin axis to wheel center plane — is forced by the two
frozen numbers:

    spindle arm = front_hub_x - kingpin_x = 552.5 - 320 = 232.5 mm

and `stub_axle_length` holds **90**. The 142.5 mm the earlier pass could not
account for is exactly that difference, and it decomposes without a remainder:

    232.5  required spindle arm      (derived, forced by frozen track_front)
    -  25  knuckle body half-width   (estimated, NOT MODELLED)
    -  90  stub axle exposed run     (= stub_axle_length, built)
    - 117.5 front hub length         (derived as the residual, NOT MODELLED)
    ------
       0.0

**So the chain is not wrong; the build is missing two of its four links.** There
is no knuckle and there is no front hub, `stub_axle_length` is the only thing
between the kingpin and the wheel, and it silently absorbed both. 90 mm is a
perfectly good *spindle* length and a badly wrong *spindle arm*. This is the
front-end instance of the front matter's own thesis: a number fitted to whichever
neighbor happened to exist.

The consequence in the built frame is large and it is the §Chassis agent's
business. `frame.py` `_kingpin_x` returns `front_hub_x − 0.090` = **462.5**, so the
built kingpins are **925 mm apart** against a required 640, and they sit **190 mm
outboard of the frame's own published 735 mm outer front width**. The front cross
member is built to reach them.

**→ Required kingpin spacing, for reconciliation with §Chassis: 640 mm, x = ±320
±10.** `derived`, §20.3.1. `frame.py` must author this rather than deriving it
from the hub, because the kingpin is a *frame* feature the wheel hangs off, not
the other way round — the same inversion `notes_column.md` §7 item 1 identified
in the steering column.

### 20.3.4 What the frozen 1240 costs, since it is not the chain's fault

The **CRG setup guide** (`refs/kart-visual/ctl_crg_setup_guide.pdf`) publishes
front width as a setup range, in three places, `sourced`:

> Front width should be 45-1/2" to 46".   (baseline setup)
> Front width should be 45" to 46".        (maximum grip)
> Front width should be 44" to 44-1/2".    (minimum grip)

That is **1117.6 to 1168.4 mm**, and the same guide's rear track figures — 54" to
55", i.e. 1371.6 to 1397 mm — bracket the frozen `track_rear` of 1400 correctly,
which is what makes the front numbers credible rather than a unit slip.

`track_front = 1240` is **48.8"**, 71.6 mm wider than the widest sourced setting
and 97 mm over the 45" center of the range. At 45" the chain closes on a spindle
arm of (1143 − 135)/2 − 320 = **184 mm**; the frozen 1240 forces **232.5**, which
is 48.5 mm longer than a real KZ's, and the surplus lands on the last link, the
front hub, giving 117.5 mm where a real KZ front hub is 90–110.

So: `track_front` 1240 is a second frozen number that is wrong, its error is
about 97 mm, and it arrives in this section as an over-long hub and an over-large
scrub radius (§20.4). Flag, do not change — `chassis.h`'s `FRONT_HALF_TRACK =
0.5525` is the mass model's front wheel station and `steering.h`'s
`track_front = 1.105` is the Ackermann relation's input. Its own ticket, alongside
the tire diameters.

---

## 20.4 Camber, caster, Ackermann — and where the model and the parts disagree

Nothing in the technical regulations constrains any of this: `carrossage`,
`camber`, `chasse`, `caster`, `castor` and `Ackermann` return **zero hits** in the
pinned PDF. So every figure here is a source outside the regulations or an
estimate, and `src/core/steering.h` already carries four of them — it is a
built, tested consumer, so this section specifies what it uses and says where the
geometry has to change to match.

| quantity | value | prov | basis |
| --- | --- | --- | --- |
| **caster** | **18.0°**, kingpin top rearward | `sourced` | Angle-gauge measurements of real chassis kingpin carriers: a 2012 Kosmic at 19.7° and 18.0° left/right (the difference attributed to a bent chassis), a Tony Kart at "18.something", and a stated cross-manufacturer range of **15–20°** with "10 is unheard of" — KartPulse, *Comparing caster across kart brands*, read in full via the forum's JSON API. Not a manufacturer drawing; it is measurements of real karts, which is why it beats the 20–25° a hobby text gives and the 8–14° a published go-kart design paper uses on a slower vehicle. `steering.h` carries 18.0°. |
| **kingpin inclination** | **11.0°**, top inboard | `sourced` | *The NatSKA Guide to Karts and Karting* via the Kartbuilding blog: the inward lean is *"generally between 10 degrees and 12 degrees"*. 11 is the middle. A go-kart design paper says 14–15; the 10–12 source is taken because it is the one that also explains what the angle is *for* — counteracting the caster's jacking. `steering.h` carries 11.0°. |
| **static camber** | **0°** | `sourced` (mechanism) | Same source, same sentence: the inclination *"to allow the wheels to stand flat on the floor, is offset by a similar angle on the stub axle"*. The spindle is machined at the inclination so the wheel stands vertical. This is a statement about the **`knuckle_??` casting**, and it is why the spindle in the part entry is specified as 11° off the kingpin's normal rather than square to it. |
| static camber, setup range | **−0.5° to 0°** | `sourced` | CRG setup guide, minimum-grip setup: *"Camber should be set at -1/2 degree (negative ½) to 0 degree"*. The only camber figure in degrees anywhere in the repo's references. |
| caster/camber adjuster | eccentric pills, three positions per pill (I / II / III), factory neutral **II top / II bottom** | `sourced` | CRG Caster/Camber Chart: III/III is maximum caster, I/I minimum, II/II *"Factory neutral setting"*, and the off-axis combinations trade caster against camber. The chart gives positions and **no degrees**. |
| adjuster range | ≈3° of caster total, ≈0.5° per position | `estimated` | No source gives it in degrees. Carried forward from `steering.h`'s own comment, which is where "one dot of caster" comes from. Consistent with the chart having three positions and with 18° sitting mid-range. |
| **static toe** | **1.5 mm out per side**, = **0.31° per wheel** measured over the front tire's 280 mm diameter | `sourced` (value), `derived` (angle) | CRG guide: *"a toe setting of 0-3mm out is recommended"* and *"1-1.5mm on each side"*; *"Toe in is not normally used on a kart."* The angle is `atan(1.5/280)`. **The convention is stated because it is ambiguous** — the same 1.5 mm read across a 136 mm rim flange instead of the tire is 0.63°, twice the number, and an unlabeled toe figure is a front-matter §1 defect. |
| **scrub radius** | **205.3 mm** as built | `derived` | `spindle_arm − wheel_radius·tan(KPI)` = 232.5 − 140·tan 11° . At the sourced 45" front width it is 156.8 mm. `steering.h` computes 62.8 mm from a 90 mm spindle offset. **All three are the same formula fed three different arms, and none of them is right** — see below. |
| **max lock** | **25.0°** at the inner wheel | inherited | Not chosen here. It is the angle the bodywork tire-clearance tables in #109/#110 were measured at, and holding the *inner* wheel to it means no front wheel ever exceeds it. `steering.h`. |
| **Ackermann** | **1.0** (true) in the solver; the sourced hardware chain builds **parallel steer** | conflict, see below | |

### 20.4.1 The scrub radius does not close, and the number to watch is the kingpin spacing

The only *published* scrub radius for any kart is **92.96 mm** (JETIR2501641, a
go-kart of wheelbase 1066.8 and front track 965.3, with 14–15° of kingpin
inclination). Back out its spindle arm: 93 + 140·tan 14.5° = 129 mm, and its
kingpin spacing is therefore 2 × ((965.3 − 130)/2 − 129) = **706 mm** — 66 mm
wider than the ±320 this section adopts, on a much narrower track.

So there are two mutually inconsistent anchors: a CRG chassis whose own catalog
parts put the kingpin at ±320, and a published design whose scrub radius implies
±353. At ±320 the scrub comes out 157–205 mm, which is 1.7–2.2× the only
published figure. `steering.h`'s 62.8 mm is *below* it by a third.

This does not resolve on the evidence in the repo, and inventing a middle would
be exactly what the front matter forbids. Recorded as an open conflict:

* the geometry in this section is internally consistent and is what the mesh
  should be built to;
* `steering.h`'s `FRONT_SPINDLE_OFFSET = 0.090` is the mesh's *spindle* length
  being used as the solver's *spindle arm*, the same substitution §20.3.3 found —
  so the solver's jacking lever is short by a factor of 2.6 and its scrub radius
  by 142 mm;
* changing it is a **driving-model change**, because the spindle arm is the lever
  the whole jacking effect works through and `steering.h` says so in its own
  words. It needs a ticket and `drive.sh` re-measured, exactly like the tire
  diameters. It must **not** ride along with a `params.py` edit.

`stub_axle_length` in `params.py` is free — nothing outside `wheels.py` and
`frame.py` reads it. Its twin constant in `steering.h` is not.

### 20.4.2 Ackermann: the solver assumes a construction the parts do not build

`steering.h` defaults `ackermann = 1.0`, true Ackermann, on a sourced
construction rule: *"lines projected through the center of the King Pins, and
through the bolts holding the track rods, should meet at the center point of the
rear axle"* (NatSKA via Kartbuilding). At kingpin ±320 and wheelbase 1050 that
construction requires the knuckle arm to sweep inboard by

    atan(320 / 1050) = 16.95 deg
    -> a 108 mm arm puts its rod end 31.5 mm INBOARD of the kingpin

`derived`. Measured on the plan view at 1.1236 mm/px, the two rod-end eyes sit
**5.7 mm and 1.1 mm outboard** of their own kingpin flanges — the opposite sign,
and 28 px short of the 31.5 mm the construction needs, on an image where 28 px is
comfortably resolvable. And the arithmetic closes the other way: with the arm
parallel to the centerline, the sourced 50 mm pitman offset and the sourced 270 mm
tie rod give a lateral span of 268.5 mm and land the kingpin at 318.5 — which is
§20.3.1's first derivation. A 16.95° swept arm needs a **240 mm** rod, and OTK
sells 235 and 270, not 240.

So the sourced hardware describes **parallel knuckle arms**, and the geometric
Ackermann of a parallel-steer four-bar is zero. The reconciliation is that a
kart's differential steer does not come from a swept knuckle arm at all: the
**column is raked 36° from vertical** (`notes_column.md` §2, derived and measured)
and the pitman plate therefore rotates in a plane tilted 36° from horizontal, so
the two ears do not travel symmetrically fore-and-aft and the two tie rods do not
push equally. The linkage is spatial, not planar, and that is where the Ackermann
lives.

Specified, then:

* build the knuckle arm **parallel to the centerline**, 108 mm, rod end at the
  kingpin's lateral station — `sourced` hardware, three ways;
* carry `ackermann = 1.0` in the solver **unchanged**, because it is a §6.4
  input and this section has no measurement to replace it with;
* **file a ticket** to solve the real linkage — kingpin ±320, arm 108 mm rearward,
  pitman offset 50 mm on a 36°-raked column, tie rod 270 mm — and measure the
  Ackermann fraction it actually produces across the 25° lock range. Until then
  the 1.0 is an assumption in the vocabulary of a construction, which is the
  milder cousin of an estimate in the vocabulary of a limit.

---

## 20.5 The rear axle: a tube, four keyways, three bearings

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outside diameter | `axle_diameter` | 50.0 | `sourced` | Art. 9.2 *"Maximum 50.0 mm outside diameter"*; KZ runs the cap |
| wall thickness | *new* `axle_wall` | **2.5** | `estimated` | Art. 4.3's table sets **1.9 mm minimum** at 50.0 mm OD (`sourced`, PDF p. 9). KZ axles are sold soft/medium/hard by wall and 2.5 is mid-range; it also leaves 0.6 mm over the floor for the four keyways, which the table exempts but which cannot be cut into 1.9 mm of wall without going through. **`axle_diameter`'s docstring says "Solid, 50 mm" and that is wrong** — a wall-thickness clause is meaningless on a solid shaft, and the table's own last row shows solid construction is for the small diameters only. |
| material | — | magnetic steel | `sourced` | Art. 4.3 |
| end chamfer | *new* | 2.5 mm, no sharp edge, both ends | `sourced` | Art. 4.3: *"a rounded edge or a chamfer with a maximum diameter corresponding to the axle thickness"* |
| length | `axle_length` | 1080 built; **1185 specified** | `derived` | 2 × `rear_hub_x` = 2 × 592.5, so each end lands flush with its rim's mounting plane. At the built 1080 the axle stops **52.5 mm short** of each wheel center plane, so a 90 mm rear hub is supported over 37.5 mm of its bore and the Art. 4.3 keyway for it is 20 mm from the axle's own chamfered end. See §20.9. |
| keyway count | — | exactly **four** | `sourced` | Art. 4.3, KZ/KZ2 |
| keyway section | *new* | 8 wide × 4 deep | `estimated` | Karting 50 mm axles run 8 mm keys. DIN 6885 would call for 14 × 9 with 5.5 mm of shaft depth on a 50 mm shaft, which a 2.5 mm wall cannot take — the tube is the reason the kart key is small, and that reasoning is the tag's justification. |
| identification sticker | — | one, manufacturer-specific | `sourced` | Art. 4.3, Appendix 10. A decal for §Driver and finishes, not a mesh. |

**The four keyway stations** — Art. 4.3 names them and this is where they land.
All `derived` from the parts they carry:

| keyway | x | carries |
| --- | --- | --- |
| left hub | −565 | `hub_rl`, keyed near the axle's left end |
| **brake disc** | **−260** | `brake_disc_rear_hub` — §20.6.5 |
| **sprocket** | **+115** | `axle_sprocket`, `wheels.SPROCKET_X` |
| right hub | +565 | `hub_rr` |

Two of the four are the disc and the sprocket, so **by construction they are not
coplanar** — which is Art. 4.3's formal answer to "why is the rear brake on the
left". §20.6.2.

**Bearings and cassettes.** `frame.py` puts three hanger plates at x 0 and ±185,
each 12 mm thick in x and 75 mm in y, and a KZ carries a center bearing as well as
the outer pair. So:

| part | value | prov | basis |
| --- | --- | --- | --- |
| bearing, 3 off | 50 ID × 80 OD × 16 W, self-aligning ball | `estimated` | 50 mm bore is forced by the axle. 50 × 80 × 16 is a real bearing envelope (6010 section) and kart axle bearings are sold as self-aligning units in this size class. No homologation form dimensions it. |
| cassette / axle carrier, 3 off | 90 OD × 40 W, aluminium, 4 bolts to the hanger plate | `estimated` | Sized to house an 80 mm bearing OD with a 5 mm wall. Its **outboard face lands at x −205** on the left, which is the clearance that fixes the rear disc's inboard limit. |

---

## 20.6 The brake system

Art. 9.6 wants a **4WP** system, and two complete homologated 4WP gearbox systems
were obtained by the measurement pass. They agree closely enough that the shared
figures are settled and the differences are a real between-manufacturer spread.

* **007-B4-69** — Birel ART / FREE LINE **RR**, *"BRAKING SYSTEM - (4WP)"*,
  category *Boîte de Vitesses / Gearbox*. Page 2 exploded CAD drawing, page 3
  dimension table.
* **82/FR/11** — C.R.G. / **VEN BK-05-125**, "All category", 4-wheel.
* **007-BRKF-01** — Birel ART Freeline FL RR EVO, BRK-F, Group 2: the
  front-circuit-only form for the current 2025–2027 period, i.e. half of 9.6's
  "BRKF + BRKR" option.

**The CRG VEN set is adopted whole** — rear Ø195 × 18.5, front Ø150 × 12, 2
pistons rear at 32 mm, 4 pistons front at 26 mm — because CRG Road Rebel is
already this repo's primary chassis reference and the VEN system is what that
chassis wears. Anything in 180–206 rear / 140–150 front is a real KZ, so the
choice is a consistency argument, not an accuracy one.

### 20.6.1 Hydraulics: two circuits, two pumps, one bore that never moves

| figure | Birel RR `007-B4-69` | **CRG VEN `82/FR/11`** | Freeline front `007-BRKF-01` | prov |
| --- | --- | --- | --- | --- |
| master cylinders | 2 — one front, one rear | **2** | 1 (front circuit only) | `sourced` |
| **master cylinder bore** | 22 mm | **22 mm** | 22 mm | `sourced` |
| balance regulator | yes | **yes** | yes | `sourced` |
| distributor | yes, `10.10659.00` | not listed | not listed | `sourced` |
| front calipers | 2 | **2** | 2 | `sourced` |
| front pistons | 2 per caliper | **4 per wheel** | 2 per caliper | `sourced` |
| front caliper bore | 25 mm | **26 mm** | 25 mm | `sourced` |
| front pads | 2 per caliper | **2 per wheel** | 2 per caliper | `sourced` |
| rear calipers | 1 | **1** | — | `sourced` |
| rear pistons | 4 | **2** | — | `sourced` |
| rear caliper bore | 25 mm | **32 mm** | — | `sourced` |
| rear pads | 4 | **2** | — | `sourced` |
| caliper material | aluminium | aluminium | aluminium | `sourced` |

**22 mm is the one figure identical across all three forms and across a 2005
homologation and a 2024 one.** Treat it as settled.

Clamp area per wheel, `derived`: front 4 × π × 13² = **2124 mm²**, rear
2 × π × 16² = **1608 mm²**. Birel splits the piston count the opposite way (2
front / 4 rear) and compensates with bore, landing at front 982 / rear 1963 — so
the two makers put the front-to-rear clamp ratio on opposite sides of 1.0, which
is exactly what the balance regulator exists to trim and is worth knowing before
anyone hangs a brake-bias tunable off this.

### 20.6.2 Which side the rear disc is on

**The kart's left, x negative.** Three reasons and they are independent.

1. **Art. 4.3, quoted in §20.1.** KZ/KZ2 axles have exactly four keyways — left
   hub, right hub, brake disc, sprocket. Four stations on one shaft, of which the
   disc and the sprocket are two, so they cannot be coplanar. This is the formal
   version and it is the one to cite.
2. **Packaging.** The engine is on the driver's right (`engine_x = +0.319`), so
   the chain must reach a sprocket on the right (`SPROCKET_X = +0.445`, equal to
   `params.chain_x` since the corridor audit moved both off +0.115) and the
   brake goes left to balance it. A chain crossing under the seat to the far side
   is not a thing any kart does. `wheels.py`'s `SPROCKET_X` docstring already has
   this right, including the correction history.
3. **Photograph.** `crg_roadrebel_kz_detail11.webp` is the Road Rebel's **left**
   side and the drilled disc, its star carrier and the caliper above it are
   plainly there, inboard of the left rear wheel.

Separation check, `derived`: sprocket plane +445 (8 thick), disc plane −400
(18.5 thick) — **845 mm apart**, on opposite sides of the center bearing at x 0.
No interaction. The disc's clearance problem is with the **left bearing cassette**
and not with the sprocket.

### 20.6.3 Discs

| figure | Birel RR | **CRG VEN — adopted** | OTK catalog | prov |
| --- | --- | --- | --- | --- |
| rear disc external Ø | 180 ±1.5 | **195 ±1.5** | 206 (KZ), 180 (OK) | `sourced` |
| rear disc thickness, new | 16 ±1 | **18.5 ±1** | 16 | `sourced` |
| rear pad rubbing Ø, outer / inner | 177 / 126 | **194 / 136** | — | `sourced` |
| rear pad overall length | 40 ±1.5 | **58 ±1.5** | — | `sourced` |
| front disc external Ø | 150 ±1.5 | **150 ±1.5** | 140 | `sourced` |
| front disc thickness, new | 12 ±1 | **12 ±1** | — | `sourced` |
| front pad rubbing Ø, outer / inner | 146.5 / 95.5 | **149 / 92** | — | `sourced` |
| front pad overall length | 40 ±1.5 | **38 ±1.5** | — | `sourced` |
| front pad friction height | 25 ±1.5 | — | — | `sourced` (`007-BRKF-01`) |

"Ventilated" on these forms means drilled and slotted through a single plate, not
a two-plate vented rotor — the drawing shows one plate. Art. 4.12.3 permits
exactly that and only as the manufacturer made it.

**Patterns, read off the `007-B4-69` page-2 exploded CAD at 170 dpi.** `sourced`
as shape, measured as count:

* **Rear disc** — floating two-piece. The outer friction ring carries **two
  concentric rings of drilled holes** (~28 outer, ~14 inner) plus **12 radial
  slots** alternating between them, and it hangs off a separate inner carrier on
  **6 floating bobbins on a bolt circle** so the ring can expand. The carrier is a
  lobed star whose bore clamps the axle-mounted hub. One bobbin position carries
  the homologation-number boss required by Art. 4.12.3.
* **Front disc** — one piece, no floating carrier. **6 curved slots**, two rings
  of drilled holes, and **3 integral drive tangs at 120°** on the inner bore that
  bolt to the front hub.

### 20.6.4 Master cylinders, pedal link, regulator

Art. 4.4 constrains only the **ordering** — brake pedal ahead of the master
cylinder — and the two published control-rod lengths prove the position is a
chassis-design choice: OTK sells *"BRAKE PUMP'S CONTROL ROD 490MM"* and a 525 mm
(`sourced`), which is an OTK-style layout with the pumps back near the seat, while
`crg_roadrebel_kz_detail7.webp` shows **both red reservoir caps right at the pedal
bracket**, which is a CRG-style layout with a short rod.

**CRG-style is adopted**, to match the chassis the rest of the model is built
from. That means the sourced 490/525 rods do **not** apply, and the link is a
short clevis rod instead. Recording the rejection matters: a 490 mm rod is a
sourced number for a layout this kart does not have, and dropping it into a
CRG-style front end would put the master cylinders behind the seat.

| part | x | y | z | prov | basis |
| --- | --- | --- | --- | --- | --- |
| brake pedal pad (§Cockpit's part) | −75 | +560 | +90 | `derived` | `pedal_separation`/2, `pedal_y`, `pedal_z` |
| `brake_master_rear` | −150 | +430 | +120 | `estimated` | `crg_roadrebel_kz_detail7.webp`: both reservoirs at the pedal bracket, left of the column, ahead of the front cross member's rear face. Fore-aft is the estimate; the ordering is Art. 4.4 and is exact |
| `brake_master_front` | −105 | +430 | +120 | `estimated` | the pair, 45 mm apart on one bracket |
| `brake_pushrod` | −112 | +430…+560 | +100 | `derived` | 135 mm, pedal arm eye to master piston eye |
| `brake_pushrod_link` | −118 | +430…+560 | +100 | `sourced` (required), `estimated` (route) | Art. 4.12.2. **2.0 mm** steel cable, one step over the 1.8 mm floor — and 1.8 is a floor, not a practice |
| `brake_balance_regulator` | −170 | +400 | +150 | `estimated` | inline, reachable from the seat; `82/FR/11` lists it and does not place it |
| `brake_distributor` | −128 | +398 | +120 | `estimated` | on the master bracket. **Birel-only**: `007-B4-69` item `10.10659.00`; the CRG form does not list one, so this part is a Birel feature grafted onto a CRG layout and is the weakest-sourced part in the section |

Master cylinder body: **~130 mm** long including the reservoir, **~110 mm** high
including the reservoir tower, ~40 mm wide. `estimated` — 180 px × 0.72 mm/px on
`007-B4-69` p2, in a different region of the drawing from the calipers, so scale
confidence is lower than §20.6.6's.

**Lines** (`sourced` construction, `estimated` route): braided steel hose with
banjo ends. The rear is a **single run**; the front is a **tee'd assembly**
feeding both calipers from one pump (`007-B4-69` item 9, *"BRAKE FRONT TUBE
ASSY."*, drawn as a tee with two equal branches). Routing from
`col_crg_form_planview_1417.jpg` and `crg_roadrebel_kz_detail7.webp`: hoses are
**cable-tied along the upper surface of the chassis tubes**, the front pair
crossing the front of the chassis and turning outboard to each upright, the rear
run going back along the left rail. **Nothing crosses under the floor tray**,
which is the rule that keeps the route out of Art. 4.12.6's territory and off the
skid plates.

### 20.6.5 Rear brake positions

All at the built `rear_axle_z` of 147.5. At the sourced Ø274 tire every z drops
10.5 mm; the built figures are the ones the mesh is checked against.

| part | x | y | z | prov | basis |
| --- | --- | --- | --- | --- | --- |
| rear axle centerline | — | −525 | +147.5 | `derived` | `−wheelbase/2`; `tire_rear_diameter/2` |
| **`brake_disc_rear`** friction plane | **−260 ±25** | −525 | +147.5 | x `estimated`, y/z `derived` | below |
| `brake_disc_rear_hub` | −260, 55 wide | −525 | +147.5 | `estimated` | OTK *"MG DISK'S HUB D.50mm FOR BRAKE"* clamps the 50 mm axle |
| **`brake_caliper_rear`** pad center | −260 | **−496.8** | **+225.0** | x `estimated`, clock `estimated`, radius `derived` | pad mean radius (194 + 136)/4 = 82.5; clock 20° ±15 forward of top dead center. y = −525 + 82.5 sin 20°, z = 147.5 + 82.5 cos 20° |
| `brake_caliper_rear_bracket` | −205 → −260 | −497 | +190 | `derived` | from the left cassette's outboard face out to the caliper's two lugs, 55 mm of reach |
| **`brake_disc_protector`** | −295 … −215 | −565 … −485 | 35 … 45 | `sourced` (required), `estimated` (form) | Art. 4.12.4, *"under the disc"* option. Its underside is at 35, level with the rails' lowest point, so it is the thing that grounds first |

**How x = −260 was reached.** `estimated`, ±25, and the reasoning is carried
because that is what the tag costs. Going outboard along the left of the axle: the
center bearing at 0, the **left hanger plate at −185** spanning −191…−179
(`frame.py`), a cassette whose outboard face lands at **−205**, then the disc
carrier hub of ~55 mm clamped immediately outboard of it — which puts the friction
plane at about **−255 to −265**. It has to clear the left rear hub, whose inboard
end is at −502.5, so there is ~215 mm of spare axle and **the constraint is the
bearing, not the wheel**. Corroborated qualitatively by
`crg_roadrebel_kz_detail11.webp`, where the disc sits much nearer the cassette
than the wheel. ±25 mm is the honest band: no dimensioned drawing fixes it, the
chassis homologation form's dimension table is bumpers, tubes and overhangs only,
and the keyway position is the manufacturer's choice, so it is genuinely
chassis-specific. This is one of the two places the measurement pass concentrated
its estimates and it is carried forward as an estimate, not upgraded.

**Clock angle.** `estimated`, ±15°. `crg_roadrebel_kz_detail11.webp` and
`tonykart_racer401T_p03.jpg` both show the rear caliper near the **top** of the
disc on a bracket bolted to the bearing cassette, tipped slightly **forward** of
vertical. 20° is the middle of what those two photographs support.

**Clearances**, `derived`: the caliper body is ~74 mm across the disc and centered
on the disc plane, so it spans x −223 … −297 and clears the cassette's outboard
face at −205 by **18 mm**. The disc's own bottom edge is at z **50.0** — §20.1's
Art. 4.12.4 trigger — and the sprocket's is at 75, so the disc is the lowest
rotating part on the kart.

### 20.6.6 Front brake positions, and the caliper that fouls the rim

The measurement pass placed the front disc at **±480**, boxed between a rim inner
flange at 495 and a kingpin at ±465, and then found the interference itself: *"with
a 66 mm body centered at 480 it reaches 513 and fouls… this is the one place in
the front assembly where a naive symmetric caliper will intersect the wheel."*

**§20.3 dissolves the box.** That placement inherited `stub_axle_length` as the
spindle arm, so the kingpin was at ±465 and there were only 30 mm of axle to work
in. With the kingpin at **±320** there are **172.5 mm** of clear spindle between
the knuckle and the rim's inner flange, and the disc no longer has to sit in the
rim's mouth.

Resolved by **moving the disc 35 mm inboard and keeping the caliper symmetric
about it**, which is the physical construction — an opposed-piston caliper has a
piston on each side and cannot be offset 23 mm without the outboard half becoming
4 mm of aluminium. The disc goes where the hub can carry it: bolted by its three
tangs to the hub's inboard flange.

    disc plane                 x = 445
    caliper body, 66 across    x = 412 ... 478      (33 either side of the disc)
    tire inner face            x = 485              ->  7.0 mm clear
    rim inner flange, built    x = 495              -> 17.0 mm clear
    rim inner flange, sourced  x = 492.5            -> 14.5 mm clear
    knuckle outboard face      x = 345              -> 67   mm clear

`derived`, all of it. The 7.0 mm to the tire is the binding clearance and it is
the one to re-check if `tire_front_width` is ever relabeled — at the sourced
130 mm the tire's inner face moves outboard to 487.5 and the margin grows to 9.5.

| part | x | y | z | prov | basis |
| --- | --- | --- | --- | --- | --- |
| front stub axle centerline | — | +525 | +140 | `derived` | `wheelbase/2`; `tire_front_diameter/2` |
| **`brake_disc_fl` / `_fr`** | **∓445 / ±445** | +525 | +140 | `derived` | above |
| **`brake_caliper_fl` / `_fr`** pad center | ±445 | **+583.2** | **+155.6** | x `derived`, clock `estimated` | pad mean radius (149 + 92)/4 = 60.25; clock 75° ±15 forward of top. y = 525 + 60.25 sin 75°, z = 140 + 60.25 cos 75° |
| `brake_caliper_??_bracket` | ±345 → ±445 | +570 | +150 | `derived` | from the knuckle's outboard face to the caliper's 4-bolt flange, 100 mm of reach. Art. 4.12.5 confirms the caliper hangs off the stub-axle side |

**Clock angle**, `estimated` ±15°: `crg_roadrebel_kz_detail7.webp` shows the front
caliper on the upright **ahead of** the kingpin and roughly level with the axle,
and `col_crg_form_planview_1417.jpg` agrees in plan that the body sits forward of
the stub axle. 75° forward of top puts it just above axle height and well forward,
which is what both images show. That plan view is 4% low on rear track when
checked against its own scale, so it is good for "forward or aft" and not for a
millimeter, and no millimeter above came from it.

**Ground clearance**, `derived`: a Ø150 disc at z 140 has its bottom edge at
**65 mm**, clear of the rails at 35…65 and clear of Art. 4.12.4, which is a
rear-disc rule anyway.

### 20.6.7 Caliper bodies — measured off a drawing, at a stated scale

Both scale references are taken **inside the same orthographic CAD projection**,
so there is no perspective term:

* front group: front disc Ø150 (`sourced`) spans 200 px → **0.750 mm/px**
* rear group: rear disc Ø180 (`sourced`) spans 252 px → **0.714 mm/px**

The two scales agree to **4.8%**, derived independently from two different parts
of one drawing, and that agreement is the evidence the exploded view is drawn to a
single scale. **5% is the honest error bar on every figure below.** The limitation
is not viewpoint: the calipers are drawn face-on to the disc, so the two in-plane
dimensions are readable and the through-thickness is not in the drawing at all.

| figure | value | prov | basis |
| --- | --- | --- | --- |
| rear caliper, circumferential length lug to lug | **138 ±7** | `estimated` | 193 px × 0.714, `007-B4-69` p2 |
| rear caliper, radial height | **55 ±3** | `estimated` | 77 px × 0.714 |
| rear caliper, thickness across the disc | **~74** | `estimated` | 18.5 disc + 2×9 pad + 2×19 cylinder wall. Not in any drawing |
| front caliper, circumferential length | **103 ±5** | `estimated` | 137 px × 0.750 |
| front caliper, radial height | **62 ±3** | `estimated` | 83 px × 0.750 |
| front caliper, thickness across the disc | **~66** | `estimated` | 12 + 2×9 + 2×18 |
| rear caliper mounting | 2 lugs at the ends of the long axis, plus a large central through-boss | `sourced` | drawing |
| front caliper mounting | 4 bolt holes in a flange, 2 top 2 bottom | `sourced` | drawing |

Cross-check on the rear length, `derived`: the CRG VEN rear runs 2 pads at 58 mm
overall, so 116 mm of pad inside a 138 mm body leaves 11 mm per end for the lugs —
tighter than the Birel 4-piston version's 29 mm, which is consistent with CRG
using longer pads and fewer of them.

**Shape**, from the drawing and `tonykart_racer401T_p03.jpg`: an
**opposed-piston one-piece aluminium body** with a peanut/waisted outline,
**externally finned** across the top face for cooling, a banjo fitting on each
half and a bleed nipple. Not a sliding caliper — the body straddles the disc and
both halves carry pistons.

This sub-section and the rear disc's lateral station are the **two places the
measurement pass concentrated its 16 estimates**, and both are carried forward
with their reasoning rather than promoted. Nobody publishes a caliper envelope.

---

## 20.7 Part entries

### `wheel_??_rim` (4)
**Status:** built
**Attaches to:** `wheel_??_tire` (seated), `hub_f?` / `hub_r?` (bolted, 3× M8)
**Envelope:** Art. 4.13.1 — the wheel ceilings, §20.1. Art. 4.14 — rim diameters.
**Verification:** gate 1, gate 2, `genkart.sh --check`

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| bead coupling Ø | `rim_bead_diameter` (new) | 126.2 +0/−1 | `sourced` | Art. 4.14, p. 13; built 127, 0.7 over |
| flange external Ø | `rim_flange_diameter` (new) | ≥136.2 | `sourced` | Art. 4.14; built Ø138.9 and compliant, §20.2.2 |
| tire housing width | — | ≥10.0 | `sourced` | Art. 4.14 |
| rim width, front / rear | `rim_front_width` / `rim_rear_width` (new) | 120 / 198 | `sourced` | 047-TO-12 / -14 p3, flange to flange |
| bead retention pegs | — | **≥3, outboard flange** | `sourced` | Art. 4.14.1. **Mandatory and absent on all four rims.** Belongs on the high-detail mesh; low detail can carry them in the normal bake |
| wheel fixing | — | 3× M8 self-locking | `sourced` (M8), `estimated` (3 off) | Art. 4.13 names M8; the bolt count is kart practice, not published |

**Two things this entry changes.** The rim mesh currently carries the wheel hub as
an integral `HUB_BOSS` sleeve, `HUB_BOSS_HALF_FRONT = 0.055` at the front and a
rear sleeve derived from `axle_length`. Art. 4.17 makes the hub a **separate
chassis main part** (Art. 4.2.1) and §20.3 needs it to be 117.5 mm long, so
`hub_f?`/`hub_r?` become real parts and the rim keeps only its mounting plate.
And `_tire_profile` reads `rim_diameter` for the bead radius, so the split in
§20.2.2 touches both meshes in one edit.

### `wheel_??_tire` (4)
**Status:** built
**Attaches to:** `wheel_??_rim` (seated)
**Envelope:** Art. 4.13.1 wheel ceilings; Art. 4.15 (homologated tires mandatory, no dimension)
**Verification:** gate 1, gate 2, `genkart.sh --check`, `drive.sh` (diameters and widths are §6.4 inputs)

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| front overall Ø | `tire_front_diameter` | **280 frozen** (260 sourced) | `estimated` | §20.2.1 |
| rear overall Ø | `tire_rear_diameter` | **295 frozen** (274 sourced) | `estimated` | §20.2.1 |
| front / rear overall mounted width | `tire_front_width` / `tire_rear_width` | **135 / 215 frozen** (130 / 207 sourced) | `estimated` | §20.2.1 |
| front / rear tread width | `tire_front_tread_width` / `tire_rear_tread_width` (new) | 110 / 179 | `sourced` | 047-TO-12 / -14 p3 |
| widest-point radius | `tire_sidewall_bulge` | 38 (fix from 8) | `derived` | §20.2.3 |
| tread-to-sidewall corner | `tire_shoulder_radius` | 22 | `estimated` | kart slicks have a soft shoulder; unchanged, no source found |
| tread depth, front / rear | — | 3.3 ±0.5 / 3.5 ±0.5 | `sourced` | 047-TO-12/-14 p2 item 9 |
| tread thickness, front / rear | — | 3.5 ±1.0 / 3.8 ±1.0 | `sourced` | p2 item 12 |
| mass, front / rear | — | 1200 g / 1600 g, ±10% | `sourced` | p2 item 10 |
| carcass | — | 2-ply polyester, tubeless | `sourced` | p2 items 11/14/17 |
| service pressure | — | 0.85 bar ±0.3 | `sourced` | p2 item 3 |
| maximum assembly pressure | — | 4.0 bar | `sourced` | Art. 4.13 |

**Make.** Vega XH4 dimensions are used. **Bridgestone must not appear on this
kart**: there is no Bridgestone entry of any kind in the CIK-FIA 2024–2026 tire
technical list, so a 2026 KZ cannot be wearing them. LeCont is the FIA's
designated supplier for KZ, KZ2 and KZ2 Masters in 2026, and its front is
10x4.50-5 against Vega's 10x4.60-5 — about 2.5 mm of section width, inside the
form's own ±5 mm. For §Driver and finishes: the sidewall lettering is the only
place the make is visible, and it should be a fictional brand rather than either
of these.

### `hub_fl`, `hub_fr` (2)
**Status:** new
**Attaches to:** `knuckle_f?` (pierced — the spindle runs through both bearings), `wheel_f?_rim` (bolted, 3× M8), `brake_disc_f?` (bolted, 3 tangs)
**Envelope:** Art. 4.17 — *"The sole purpose of the wheel hub is to enable the transfer of forces between the rim and the chassis."*
**Verification:** gate 1, gate 2, `genkart.sh --check`

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length | 117.5 | `derived` | §20.3.3, the residual of the spindle arm. A real KZ front hub is 90–110; the surplus is `track_front`'s error, and at a sourced 45" front width the same chain gives 69 |
| inboard end | x ±435 | `derived` | 320 + 25 + 90 |
| flange face | x ±552.5 | `derived` | `front_hub_x()` — the rim's mounting plane |
| body Ø | 45 | `estimated` | has to house a bearing pair on a 17 mm spindle; read off the plan view's hub mask at 1.1236 mm/px |
| flange Ø | 76 | `estimated` | a 3 × M8 bolt circle at Ø58 plus 9 mm of edge |
| bearing bore | 17 | `estimated` | the standard kart stub-axle bolt; not published |

### `hub_rl`, `hub_rr` (2)
**Status:** new
**Attaches to:** `axle_rear` (keyed — `pressed`), `axle_key_hub_?` (seated), `wheel_r?_rim` (bolted, 3× M8)
**Envelope:** Art. 4.17; Art. 4.3 (one keyway each, and only one)
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length | 90 | `estimated` | kart rear hubs are sold in lengths and are how rear track is set; 90 is mid-range. The CRG guide's *"Rear wheel hubs should be the shortest length (for minimum rear grip)"* is the confirmation that length is the tunable |
| bore | 50 | `derived` | the axle |
| body Ø | 70 | `estimated` | 50 bore plus 10 mm of wall |
| flange face | x ±592.5 | `derived` | `rear_hub_x()` |
| span | x ±502.5 … ±592.5 | `derived` | 90 mm inboard of the flange |
| keyed length | 37.5 as built, 90 at `axle_length` 1185 | `derived` | §20.5 — the built axle stops at ±540 |

### `axle_rear`
**Status:** built
**Attaches to:** `chassis_bearing_hanger_?` via `axle_bearing_?` (pierced), `hub_r?` (pressed), `axle_sprocket` (clamped), `brake_disc_rear_hub` (clamped), all four `axle_key_*` (seated)
**Envelope:** Art. 9.2 (≤50.0 mm OD), Art. 4.3 (wall, chamfer, four keyways, magnetic steel)
**Verification:** gate 1, gate 2, `genkart.sh --check`
Dimensions: §20.5. **`axle_diameter`'s docstring is wrong** — the axle is a tube.

### `axle_key_hub_l`, `axle_key_hub_r`, `axle_key_disc`, `axle_key_sprocket` (4)
**Status:** new
**Attaches to:** `axle_rear` (seated), and one of `hub_r?` / `brake_disc_rear_hub` / `axle_sprocket` (seated)
**Envelope:** Art. 4.3 — **exactly four keyways, and these are them.** A fifth is not legal.
**Verification:** gate 1, gate 2
8 × 4 × 30 mm, `estimated`, §20.5. Stations x −565, −260, +115, +565.

### `axle_bearing_l`, `_c`, `_r` and `axle_cassette_l`, `_c`, `_r` (6)
**Status:** new
**Attaches to:** bearing → `axle_rear` (pierced) and `axle_cassette_?` (pressed); cassette → `chassis_bearing_hanger_?` (bolted, 4× M8)
**Envelope:** none
**Verification:** gate 1, gate 2
Dimensions: §20.5. `axle_cassette_l`'s outboard face at x −205 is what fixes the
rear disc's inboard limit, and `brake_caliper_rear_bracket` bolts to it.

### `axle_sprocket`
**Status:** built
**Attaches to:** `axle_rear` (clamped), `axle_key_sprocket` (seated), `drive_chain` (meshed — §Powertrain)
**Envelope:** none
**Verification:** gate 1, gate 2
Ø145 pitch at x +115, thickness 8. The pitch diameter and the #219 chain
derivation belong to §Powertrain; recorded here only because the sprocket sits on
this section's axle and consumes one of Art. 4.3's four keyways.

### `axle_stub_fl`, `axle_stub_fr` (2)
**Status:** built, **and renamed in meaning.** The mesh stays; what it *is* changes.
**Attaches to:** `knuckle_f?` (pressed — the spindle is a bolt through the knuckle's boss), `hub_f?` (pierced)
**Envelope:** none. Art. 4.12.5 makes it the mounting point for rain covers.
**Verification:** gate 1, gate 2

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| exposed run, knuckle face to hub | `stub_axle_length` | 90 | `estimated` | §20.3.3. **This is what the 90 measures.** Its docstring says "Kingpin to front hub center", which is the 232.5 mm spindle arm, and that is the mislabel |
| spindle Ø | `STUB_DIAMETER` | 25 carrier over a 17 bolt | `estimated` | `wheels.py`'s own docstring, unchanged |
| spindle arm (new, derived) | — | 232.5 | `derived` | `front_hub_x − 320`. Derive it; do not author it |

**Do not fix `stub_axle_length` and `steering.h`'s `FRONT_SPINDLE_OFFSET` in one
change.** §20.4.1: the first is free, the second moves the jacking lever by 2.6×.

### `kingpin_fl`, `kingpin_fr` (2)
**Status:** new
**Attaches to:** `chassis_cross_front` (pierced — the yoke), `kingpin_pill_f?_upper` / `_lower` (pierced), `knuckle_f?` (pierced)
**Envelope:** Art. 4.2.1 (a chassis main part), Art. 4.2.2 (*"Articulated connections are only allowed for the steering knuckle (through the king pin)"*)
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| axis | x ±320, y +525 | `derived` | §20.3.1, three independent lines |
| caster | 18.0° top rearward | `sourced` | §20.4 |
| inclination | 11.0° top inboard | `sourced` | §20.4 |
| bolt Ø / length | 10 × 95 | `estimated` | an M10 through-bolt is kart practice; length spans a 75 mm yoke plus nut |

### `kingpin_pill_f?_upper`, `_lower` (4)
**Status:** new
**Attaches to:** `kingpin_f?` (pierced), `chassis_cross_front` (seated — the pill sits in the yoke's bore)
**Envelope:** none
**Verification:** gate 1, gate 2
The eccentric caster/camber adjusters. Ø25 outer, Ø10 bore offset 1.5 mm,
`estimated`; three indexed positions I/II/III with **II/II the factory neutral
setting** (`sourced`, CRG Caster/Camber Chart). These are the parts that *carry*
the 18° — the chart's III/III is maximum caster and I/I minimum, and the whole
±1.5° a setup screen would expose lives in these four pieces.

### `knuckle_fl`, `knuckle_fr` (2)
**Status:** new
**Attaches to:** `kingpin_f?` (pierced), `axle_stub_f?` (pressed), `knuckle_arm_f?` (welded — one casting), `brake_caliper_f?_bracket` (bolted)
**Envelope:** Art. 4.2.1, Art. 4.2.2
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| body half-width, kingpin axis to outboard face | 25 | `estimated` | §20.3.2 row 4 |
| spindle boss angle | **11° off the kingpin's normal** | `sourced` (mechanism) | *"to allow the wheels to stand flat on the floor, [the inclination] is offset by a similar angle on the stub axle"*. This is the part that makes static camber zero, and building the spindle square to the kingpin instead gives the kart 11° of positive camber |
| height | 105 | `estimated` | has to span the two kingpin bushes; plan-view mask |

### `knuckle_arm_fl`, `knuckle_arm_fr` (2)
**Status:** new
**Attaches to:** `knuckle_f?` (welded), `tierod_end_?_outer` (bolted, self-locking)
**Envelope:** Art. 4.5.3 — *"made of aluminium or steel and securely attached with self-locking nuts and bolts"*
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length, kingpin to rod-end center | **108** | `derived` | plan view, both sides independently: 96 px and 98 px at 1.1236 mm/px = 108 and 110 |
| direction | **straight rearward**, parallel to the centerline | `sourced` (shape) | both sides read their own kingpin's lateral station to within 1 px. §20.4.2 — this is what makes the sourced 270 mm tie rod fit, and it is not the true-Ackermann construction |
| rod-end station | (±320, +417, +140) | `derived` | 108 mm rearward of the kingpin |
| section | 30 × 8 plate | `estimated` | plan-view mask; not published |

### `tierod_l`, `tierod_r` and `tierod_end_?_inner`, `_outer` (6)
**Status:** new
**Attaches to:** rod → both its own ends (threaded); `_inner` → `steering_pitman` (bolted, §Cockpit's part); `_outer` → `knuckle_arm_f?` (bolted)
**Envelope:** Art. 4.5.3 — rose joints at each end are explicitly permitted
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length, eye to eye | **270** | `sourced`, and independently `derived` | OTK *"STEERING TIE-ROD 270 mm"*. The geometry predicts it without being told: pitman ear (50, 431, 160) to rod end (320, 417, 140) = 270.7 mm. **A sourced part length and a measured geometry agreeing to 0.3% is the strongest single result in this section**, and it is the third leg of §20.3.1 |
| tube Ø | 12 | `estimated` | not measurable at 1.12 mm/px against a dark floor tray; 12 is the usual aluminium track-rod tube and is consistent with the rods reading 9–11 px |
| rod end | M8 rose joint, Ø22 eye | `estimated` | Art. 4.5.3 requires the joint and does not size it |

**The pitman offset is 50 mm** (`sourced`, OTK "38/50", outer hole) and belongs to
§Cockpit. It is quoted here because §20.3.1 and this entry both stand on it, and
because moving the rod to the 38 mm hole changes the steering ratio and the
Ackermann together.

### `brake_master_front`, `brake_master_rear` (2)
**Status:** new
**Attaches to:** `brake_master_bracket` (bolted), `brake_pushrod` (pierced), `brake_line_front` / `_rear` (routed), `brake_distributor` (routed)
**Envelope:** Art. 4.4 — *"The brake pedal must be placed in front of the master cylinder."* Satisfied: pedal y +560, pumps y +430.
**Verification:** gate 1, gate 2
Bore **22 mm** (`sourced`, three homologation forms across nineteen years). Body
~130 × ~110 × ~40 with an integral reservoir on top (`estimated`, §20.6.4). Two
red reservoir caps, visible in `crg_roadrebel_kz_detail7.webp` (`sourced`).

### `brake_master_bracket`
**Status:** new
**Attaches to:** `chassis_cross_front` (welded), `brake_master_?` (bolted), `brake_distributor` (bolted)
**Envelope:** Art. 4.2.5 lists the pedal kit and steering column holder as chassis components that may be welded; the pump bracket is the same class.
**Verification:** gate 1, gate 2
A plate at (−128, +430, +105), 90 × 60 × 4, `estimated`.

### `brake_pushrod` and `brake_pushrod_link` (2)
**Status:** new
**Attaches to:** `pedal_brake` (pierced), `brake_master_?` (pierced)
**Envelope:** **Art. 4.12.2 — the link between the pedal and the pumps must be doubled for safety; a homologated cable must be ≥1.8 mm.**
**Verification:** gate 1, gate 2

`brake_pushrod`: 135 mm clevis rod, Ø8, `derived` from the pedal-arm eye to the
piston eye. `brake_pushrod_link`: **2.0 mm** steel cable alongside it, `sourced`
as required and `estimated` at 2.0 because **1.8 is a floor and not a practice** —
the front matter's rule, and the reason this is not written as "1.8 mm max".

**This part exists because a rule says so** and it is one of the three most likely
to be left out. The CRG chassis form devotes a whole page to photographing it.

### `brake_balance_regulator`, `brake_distributor` (2)
**Status:** new
**Attaches to:** regulator → `chassis_rail_l` (bolted), `brake_line_rear` (routed); distributor → `brake_master_bracket` (bolted), `brake_line_front` (routed)
**Envelope:** none
**Verification:** gate 1, gate 2
§20.6.4. The regulator is on both homologation forms; the distributor is on the
Birel form only and is the weakest-sourced part in this section.

### `brake_line_front`, `brake_line_rear` (2)
**Status:** new
**Attaches to:** front → `brake_master_front`, `brake_caliper_fl`, `brake_caliper_fr`, `brake_distributor` (all routed), `chassis_cross_front` (routed — cable-tied); rear → `brake_master_rear`, `brake_balance_regulator`, `brake_caliper_rear` (routed), `chassis_rail_l` (routed)
**Envelope:** none. Art. 4.12.6 keeps the *cooling* tube from extending under the chassis; the same rule of thumb is applied to the hoses.
**Verification:** gate 1, gate 2
Braided steel, Ø6 over the braid, banjo ends. The front is a **tee'd assembly**
with two equal branches (`sourced`, `007-B4-69` item 9); the rear is a single run.
Cable-tied along the **upper** surface of the tubes, nothing under the floor tray
(`sourced` route, §20.6.4).

### `brake_disc_rear`
**Status:** new
**Attaches to:** `brake_disc_rear_bobbin_?` (pierced — the ring floats on them), `brake_pad_rear_?` (seated)
**Envelope:** Art. 4.12.3 — steel, stainless or cast iron; drilled/grooved only as the manufacturer made it.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| external Ø | **195 ±1.5** | `sourced` | `82/FR/11` |
| thickness, new | **18.5 ±1** | `sourced` | `82/FR/11` |
| friction plane | x −260 ±25, y −525, z +147.5 | x `estimated`, y/z `derived` | §20.6.5 |
| pad rubbing Ø, outer / inner | 194 / 136 | `sourced` | `82/FR/11` |
| hole pattern | two concentric rings, ~28 outer + ~14 inner | `sourced` (shape), counted | `007-B4-69` p2 at 170 dpi |
| slots | 12 radial, alternating between the rings | `sourced` (shape) | same |
| bottom edge | z **+50.0** | `derived` | 147.5 − 97.5. **Level with the rails: Art. 4.12.4 triggers** |

### `brake_disc_rear_carrier`, `brake_disc_rear_hub`, `brake_disc_rear_bobbin_?` (8)
**Status:** new
**Attaches to:** carrier → `brake_disc_rear_hub` (clamped), `brake_disc_rear_bobbin_?` (pierced); hub → `axle_rear` (clamped), `axle_key_disc` (seated)
**Envelope:** none
**Verification:** gate 1, gate 2
Floating two-piece construction, `sourced` as shape from `007-B4-69` p2: a lobed
star carrier, **6 bobbins on a bolt circle** so the friction ring can expand, and
one bobbin position carrying the Art. 4.12.3 homologation-number boss. The hub is
~55 mm wide and clamps the 50 mm axle (OTK *"MG DISK'S HUB D.50mm FOR BRAKE"*).

### `brake_caliper_rear`, `brake_caliper_rear_bracket`, `brake_pad_rear_?` (4)
**Status:** new
**Attaches to:** caliper → `brake_disc_rear` (via the pads, seated), `brake_caliper_rear_bracket` (bolted, 2 lugs + a central boss), `brake_line_rear` (routed); bracket → `axle_cassette_l` (bolted)
**Envelope:** none
**Verification:** gate 1, gate 2
138 × 55 × ~74, opposed-piston finned aluminium, **2 pistons at 32 mm bore, 2
pads at 58 mm** (`sourced`, `82/FR/11`). Position and clock angle: §20.6.5.
Clamp area 1608 mm² (`derived`).

### `brake_disc_protector`
**Status:** new
**Attaches to:** `chassis_rail_l` (bolted)
**Envelope:** **Art. 4.12.4 — mandatory in Groups 1, 2 & 3 when the disc is level with or below the lowest chassis tubes. This kart's disc bottom is at z 50.0 and the rails occupy 35…65, so it is mandatory.** Material: nylon, carbon fibre, Teflon, Kevlar, Delrin or equivalent hard plastic.
**Verification:** gate 1, gate 2
80 × 80 × 10 hard plastic skid under the disc, x −295…−215, y −565…−485, top at
z 45, underside at 35 — level with the rails' lowest point, so it grounds before
the disc does, which is the entire point of the article.

**This part exists because a rule says so.** Second of the three.

### `brake_disc_fl`, `brake_disc_fr` (2)
**Status:** new
**Attaches to:** `hub_f?` (bolted, 3 tangs at 120°), `brake_pad_f?_?` (seated)
**Envelope:** Art. 4.12.3
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| external Ø | **150 ±1.5** | `sourced` | `82/FR/11`, and `007-B4-69` agrees |
| thickness, new | **12 ±1** | `sourced` | both forms |
| plane | x ±445, y +525, z +140 | `derived` | §20.6.6 |
| pad rubbing Ø, outer / inner | 149 / 92 | `sourced` | `82/FR/11` |
| slots / holes | 6 curved slots, two rings of drilled holes | `sourced` (shape) | `007-B4-69` p2 |
| drive | 3 integral tangs at 120° on the inner bore | `sourced` (shape) | same. One piece, no floating carrier |
| bottom edge | z +65 | `derived` | clear of the rails |

### `brake_caliper_fl`, `brake_caliper_fr`, `brake_caliper_f?_bracket`, `brake_pad_f?_?` (8)
**Status:** new
**Attaches to:** caliper → `brake_disc_f?` (via the pads, seated), `brake_caliper_f?_bracket` (bolted, 4-bolt flange), `brake_line_front` (routed); bracket → `knuckle_f?` (bolted)
**Envelope:** none. Art. 4.12.5 puts rain covers on the **stub axle**, which confirms the bracket belongs to the knuckle and not to the rim.
**Verification:** gate 1, gate 2 — **and this is the pair to watch.** The caliper's outboard face at x ±478 clears the tire's inner face at ±485 by 7.0 mm, which is inside the 2.0 mm CONTACT_TOLERANCE's neighborhood and is a gate-1 failure the moment the disc plane moves outboard.
103 × 62 × ~66, **4 pistons per wheel at 26 mm bore, 2 pads at 38 mm overall and
25 mm friction height** (`sourced`, `82/FR/11` and `007-BRKF-01`). Clamp area
2124 mm² per wheel (`derived`). Position and clock angle: §20.6.6.

---

## 20.8 Provenance and part tally

**Parts: 76 in this section — 12 built, 64 new.**

| status | count | which |
| --- | --- | --- |
| `built` | 12 | 4 rims, 4 tires, `axle_rear`, `axle_sprocket`, 2 stub axles |
| `new` | 64 | 4 wheel hubs, 4 axle keys, 3 bearings, 3 cassettes; 2 kingpins, 4 pills, 2 knuckles, 2 knuckle arms, 2 tie rods, 4 rod ends; 2 masters, 1 bracket, 1 push rod, 1 redundant link, 1 regulator, 1 distributor, 2 lines; rear: 1 disc, 1 carrier, 1 hub, 6 bobbins, 1 caliper, 1 bracket, 2 pads, 1 protector; front: 2 discs, 2 calipers, 2 brackets, 4 pads |
| `renamed` | 0 | `axle_stub_f?` keeps its name and changes meaning, which is recorded in its entry rather than as a rename |
| `delete` | 0 | |

Three of the 64 exist **because a rule says so**: `brake_disc_protector`
(Art. 4.12.4), `brake_pushrod_link` (Art. 4.12.2) and the bead-retention pegs
(Art. 4.14.1, specified on `wheel_??_rim` rather than as four more parts).

**Numbers: 168 tagged figures.**

| tag | count | where it concentrates |
| --- | --- | --- |
| `sourced` | **97** | the regulation block (28 figures across 14 articles), the two tire homologation forms (24), the two brake homologation forms plus the front-only form (33), the CRG chassis form (4), the CRG setup guide (5), OTK/Birel catalog part lengths (3) |
| `derived` | **48** | the whole front lateral chain (12 stations), every brake position's y and z, the clamp areas, the scrub radius, the Ackermann sweep, the disc ground clearances, the caliper clearance set |
| `estimated` | **23** | **two clusters, unchanged from the measurement pass**: the rear disc's lateral station (1, ±25 mm) and caliper/master envelopes and clock angles (9). The rest are the parts nobody dimensions — knuckle body, hub bodies, bearings, cassettes, keys, pills, rod ends, bracket plates |

`estimated` is 14% of the figures and that is a normal outcome, not a defect to
drive to zero. Every one carries its reasoning inline and none is written in the
vocabulary of a limit. The two things this section refuses to do are promote an
estimate to a citation because it agrees with a photograph, and write "max" over
a number nobody sourced.

**Where this section disagrees with `notes_running.md`,** with the measurement:

1. **The rim flange is not 9 mm undersize.** The notes say a module drawing the
   flange at 127 is 9 mm under the Art. 4.14 minimum of 136.2. Measured in
   `wheels.py`: `RIM_FLANGE_LIP = 6` puts the lip at radius 69.44 mm, **Ø138.9**,
   which clears the minimum by 2.7 mm. The real defect is the bead seat at Ø126.9
   against a 126.2 **+0**/−1 fit — 0.7 mm over, not 9 mm under. §20.2.2.
2. **The front caliper interference is not resolved by offsetting the caliper.**
   The notes propose an asymmetric caliper at x −465 with the disc at −480. A
   66 mm opposed-piston body offset 15 mm has 18 mm of aluminium on one side and
   48 on the other, and the outboard piston has nowhere to live. Resolved instead
   by moving the **disc** inboard to ±445, which §20.3's kingpin fix makes
   possible. §20.6.6.
3. **The front disc is not boxed into a 12 mm band.** The notes derive
   x = ±480 ±12 from a kingpin at ±465, which is `stub_axle_length` used as the
   spindle arm. With the kingpin at ±320 there are 172.5 mm of clear spindle.
4. **The four-keyway article is the reason, and the notes' figures for the
   sprocket separation stand** — 375 mm, recomputed here and unchanged.

---

## 20.9 What looks wrong in files this section does not own

Reported, not acted on, with the arithmetic. Verify before believing any of it.

1. **`frame.py`'s kingpins are 285 mm too far apart, and 190 mm outboard of the
   frame's own published width.** `_kingpin_x` returns `front_hub_x − 0.090` =
   **462.5**, so 925 mm apart; §20.3.1 puts them at ±320, 640 apart, three ways;
   the CRG form publishes an outer front width of 735 ±10. The front cross member
   is built to reach the wrong place, and `_kingpin_x`'s own docstring admits the
   derivation is a placeholder — *"90 mm is a representative stub length; what
   matters is that the frame stops short of the wheel"*. **This is the number the
   §Chassis agent needs: 640 mm, x = ±320 ±10, authored not derived.**
2. **`params.py` `axle_diameter`'s docstring says "Solid, 50 mm".** Art. 4.3's
   table sets a 1.9 mm minimum wall at 50.0 mm OD, and a wall clause on a solid
   shaft is meaningless. The axle is a tube. Two sentences to fix, and it changes
   the mesh: a solid 50 mm steel axle 1.08 m long is 16.6 kg against 3.2 kg
   for a 2.5 mm wall, on a kart with a 170 kg minimum (Art. 8.9).
3. **`params.py` `axle_length = 1.080` is 105 mm short.** Each rear hub's flange
   is at ±592.5 and the axle stops at ±540, so a 90 mm hub is keyed over 37.5 mm
   of its bore and Art. 4.3's hub keyway sits 20 mm from the axle's chamfered end.
   2 × `rear_hub_x` = **1.185** puts each end flush with its rim's mounting plane.
   Knock-on: `wheels.py`'s `boss_half = hub_x − axle_length/2 + HUB_SLEEVE_OVERLAP`
   collapses to 10 mm, which is correct once `hub_r?` is a real part.
4. **`src/core/steering.h` `FRONT_SPINDLE_OFFSET = 0.090` is the mesh's spindle
   length used as the solver's spindle arm.** The arm is 232.5 mm at the frozen
   track, so the scrub radius is 205.3 mm and not the 62.8 the solver computes,
   and the jacking lever is short by 2.6×. **Do not fix this alongside a
   `params.py` edit** — it is a §6.4 input and needs `drive.sh` re-measured under
   its own ticket. `steering.h`'s own comment says the spindle arm is "the number
   the wheel lift is most sensitive to", which is exactly why.
5. **`src/core/steering.h` `ackermann = 1.0` assumes a construction the reference
   chassis's own catalog parts do not build.** True Ackermann at ±320 needs the
   knuckle arm swept 16.95° inboard and a 240 mm tie rod; the sourced parts are a
   50 mm pitman offset and a 270 mm rod, which is parallel steer, and the plan
   view measures the rod ends 1–6 mm **outboard** of their kingpins where the
   construction needs 31.5 mm inboard. The real Ackermann comes from the 36° column
   rake making the pitman a spatial linkage. Needs a linkage solve, not an edit.
   §20.4.2.
6. **`params.py` `tire_shoulder_radius = 0.022` has a sourced replacement
   available and nobody has taken it.** The forms give tread widths (110 front,
   179 rear) and overall widths (130, 207), which fixes the shoulder by
   subtraction instead of by taste. Measured in `_tire_profile`: the flat tread
   band is `2 × (width/2 − TIRE_SIDEWALL_LEAN − tire_shoulder_radius)`, which is
   **163 mm** on the rear against a sourced 179 and **83 mm** on the front against
   a sourced 110. So the built tread band is 16 mm narrow at the rear and 27 mm
   narrow at the front, and the shoulder radius is eating the difference — the
   front is worse because the same 22 mm shoulder is a bigger fraction of a
   narrower tire. §20.2.3.
7. **`wheels.py` builds no bead retention.** Art. 4.14.1 makes at least three
   pegs in the outboard flange mandatory on all four wheels in Groups 1 & 2.
   Twelve pegs, and they are visible in every wheel-level photograph.
8. **`joints.py` will need `axle_rear`/`wheel_r?_rim` re-examined.** Its `pierced`
   entry says *"the live axle runs right through both rear hubs and out to the
   wheel nuts; on a kart the rear wheels are keyed to it, not to hubs"* — the
   second clause is wrong under Art. 4.2.1 and Art. 4.3, which make the hub a
   separate main part with its own keyway. Once `hub_r?` exists, the axle is
   keyed to the **hub** and the hub is bolted to the rim, and the direct
   axle-to-rim joint should go away rather than coexist with it.
