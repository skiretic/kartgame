# References

`ARCHITECTURE.md` §5 item 3 puts measured reality above anything hand-authored,
and issue #116 is the worked example of what happens without it: a powertrain
modeled from memory passed its own acceptance criteria and still did not look
like an engine. This file records what was actually looked at, so a later
session can check a shape against the same source rather than against a
recollection of it.

A reference is only listed here once it has been *looked at*. A search result
that was never opened is not a reference.

It began as a record of **photographs**, because §5 item 10 was written after a
powertrain built from prose failed. M3b widened it: a tire's vertical stiffness
and a frame's torsional rate are references too, and they are harder to come by
than a photograph. So every section below carries its own **"what could not be
sourced"** heading, and those headings are the most useful part of this file.
A number that is admitted to be an assumption can be replaced; one that is
quietly presented as measured cannot.

## Powertrain — issue #116

### Photographs

| Ref | Subject | Source |
| --- | --- | --- |
| R1 | Honda CR125 shifter engine on a kart, right side | Wikimedia Commons, [`Shifter Kart Engine.jpg`](https://commons.wikimedia.org/wiki/File:Shifter_Kart_Engine.jpg) |
| R2 | **Vortex KZ engine installed on a kart**, three-quarter rear-right, 3264 × 2448 | Wikimedia Commons, [`Vortex kart engine (13274903104).jpg`](https://commons.wikimedia.org/wiki/File:Vortex_kart_engine_(13274903104).jpg) |
| R3 | Air-cooled 100 cc kart engine, side | Wikimedia Commons, [`Kosmic TS28.JPG`](https://commons.wikimedia.org/wiki/File:Kosmic_TS28.JPG) |
| R4 | **Kart radiator mounted on a CRG, front three-quarter, with the 55° annotated** | TKART, *Tricks and secrets for the correct mounting of a radiator*, `radiatore-inclinazione-1.jpg` |
| R5 | **The same radiator face-on**, phone held against it to check the angle | TKART, same article, `radiatore-inclinazione-2.jpg` |

R4 and R5 were supplied by the project owner after two attempts at the radiator
were built from R2 plus prose and both came out wrong. They are not redistributed
here — tkart.it returns 403 to scripted fetches and the images are theirs — but
they are the authority for everything in the radiator section below, and the next
session should ask for them again rather than re-derive from text.

R2 is the primary reference and is worth re-fetching at full resolution: one
frame carries the cylinder, head, spark plug, clutch cover, reed block,
carburetor, exhaust springs, and the radiator, all on an installed KZ engine of
exactly the class this project models.

### Written sources

- New-Line Racing radiator range, core sizes — Fastech-Racing,
  <https://fastech-racing.com/new-line-radiators/>. Kart radiator cores are
  **17 in × 9.5–11.4 in**, i.e. **432 mm fore-aft × 241–290 mm tall**.
- Radiator mounting angle and bracket practice — TKART, *Tricks and secrets for
  the correct mounting of a radiator*, and *New-Line Racing 2020 radiators*
  (both read through search extracts; tkart.it returns 403 to scripted
  fetches). **Reference angle 55° to the horizontal** at 20–30 °C ambient, 45°
  at 10–20 °C, up to 60° above 30 °C, adjusted in 5° steps. Two brackets, and
  **the front bracket is the shorter of the two**.
- Dell'Orto VHSH 30 — 30 mm bore, 35 mm engine spigot, 64 mm air-filter spigot.
  SIP Scootershop listing and the Dell'Orto VHSH handbook,
  <https://www.fastech-racing.com/VHSH_Manual.pdf>.
- Exhaust attachment — Fastech-Racing TM KZ exhaust parts: the chamber is held
  to the cylinder by an **elbow, a flange and springs**.
- KZ2 class construction — 125 cc, water-cooled cylinder, head *and* crankcase,
  reed-valve induction, six-speed gearbox.

### What the references settled

1. **A KZ cylinder has no cooling fins.** It is a water-cooled sand casting: a
   round jacket on a square base flange. The finned barrel in R3 is a 100 cc
   air-cooled engine — a different class. See ADR-0028.
2. **The cylinder and head are bodies of revolution**, not boxes. The head is a
   disc with a six-nut bolt circle and a spark plug standing proud of a boss at
   its center.
3. **The clutch cover is an openwork casting**, a lattice of webs and windows
   with the clutch pressure plate visible through it, not a flat disc.
4. **The float bowl is rectangular and the carburetor body is round** — the
   opposite way round from how the module had it.
5. **A radiator core is a fine mesh**, not a slab with ribs, and the tanks are
   visibly separate sections standing proud of both faces. A New-Line core is
   **dual-pass**: a baffle inside the high tank shows from outside as a welded
   rib splitting the face into a wide inboard section and a narrow outboard one,
   and it is most of why one of these reads as a kart radiator.
6. The radiator hangs off the seat's right wing on two brackets through
   **coil-spring silentblocks**. The filler is a **raised neck**, tall enough to
   pour a bottle into, not a flat cap.
7. **The core sits in the plane a second seat's back would occupy**, immediately
   outboard of the driver's — big fin face pointing forward, reclined by the
   same angle the seat is. R4 annotates 55° to the horizontal and
   `seat_back_angle` is 35° from vertical: one angle, not two. See ADR-0029.

### What was got wrong twice before R4 and R5 arrived

Worth recording, because both wrong versions were built *from* references and
still failed — having a photograph is not the same as having read it.

- **First version**: core leaned sideways about the kart's fore-and-aft axis,
  432 mm running fore-and-aft, face pointing outboard. A long low panel lying
  over the sidepod.
- **Second version**: same wrong axis, extents swapped, so a tall narrow panel
  still leaning sideways.
- **Correct**: rake about the kart's **lateral** axis, face forward. The tell
  that both were wrong was available without any measurement — the big fin face
  was not pointing where the air comes from.

The prose source said "55° with respect to the horizontal", which is true and
was not enough: it does not say which axis, and three axes are consistent with
it. A sentence naming the axis, or one look at R4, would have settled it before
any geometry was written.

## Steering geometry — issue #35

> **Fragment.** Written by the M3b steering agent to be merged into
> `docs/REFERENCES.md` as a new section. It follows that file's rule: a reference
> is only listed once it has been *looked at*.

Everything in `src/core/steering.h` that is an angle, a length or a fraction is
sourced here, and the numbers that could **not** be sourced are listed too, with
what was assumed instead. `ARCHITECTURE.md` §5 item 10.

### Photographs

**None found that are worth citing, which is itself the finding.** Wikimedia
Commons has no usable image of a CIK kart front end. The two candidates were
fetched and looked at and neither is a racing kart:

| Ref | Subject | Source | Verdict |
| --- | --- | --- | --- |
| — | `MODIFIED GO KART FRAME.jpg`, 1952 × 3264, front three-quarter of a bare frame | Wikimedia Commons, [`MODIFIED GO KART FRAME.jpg`](https://commons.wikimedia.org/wiki/File:MODIFIED_GO_KART_FRAME.jpg) | A toy pedal kart. Vertical kingpins, no caster, no steering geometry to read. Not a reference. |
| — | `Stoxkart-contact-kart-Side-view.jpg`, 1313 × 985, side view | Wikimedia Commons, [`Stoxkart-contact-kart-Side-view.jpg`](https://commons.wikimedia.org/wiki/File:Stoxkart-contact-kart-Side-view.jpg) | An oval contact kart with the front end fully enclosed by bodywork. Nothing visible. |

What would settle the caster figure in one frame is a **side-on photograph of a
KZ front end with the wheel off**, or a manufacturer setup sheet. The project
owner has reference material and asking is cheaper than another afternoon of text
sources — that is what happened with the radiator in issue #116.

### Written sources

- **Caster and kingpin inclination measured on real chassis** — KartPulse,
  *Comparing caster across kart brands (standard caster angle)*,
  <https://forums.kartpulse.com/t/comparing-caster-across-kart-brands-standard-caster-angle/6854>.
  Read in full via the forum's JSON API; `WebFetch` gets a 403.
  - A 2012 Kosmic measured with an angle gauge: **19.7° and 18.0°** of caster on
    the left and right kingpin carriers, the difference attributed to a bent
    chassis. The poster concludes standard is 18°.
  - A second poster measured a Tony Kart and reports **"18.something"**, and gives
    the range across manufacturers as **15–20°**, with "10 is unheard of".
  - The 10.2° figure quoted for OTK in the same thread is corrected in it: that is
    the **"camber" of the C section**, karting's name for how far the kingpin
    carrier leans in toward the centerline — which is the kingpin inclination, not
    the caster.
  - Also from the thread, and consistent with the geometry in `steering.h`: the
    effective caster at the wheel is "a combination of the kingpin inclination
    (longitudinally and latitudinally), scrub radius and the angle of the stub, and
    they are all intertied", and adding kingpin inclination subtracts from
    effective caster.
- **Kingpin inclination, and the Ackermann construction** — Kartbuilding Blog,
  *Steering geometry and setup for go-karts*, quoting *The NatSKA Guide to Karts
  and Karting*, <http://blog.kartbuilding.net/2007/07/12/steering-geometry-and-setup-for-go-karts/>
  (self-signed certificate; fetched with `curl -k`).
  - The inward lean of the kingpin is **"generally between 10 degrees and 12
    degrees, and to allow the wheels to stand flat on the floor, is offset by a
    similar angle on the stub axle"**. That second clause is the source for
    modeling **zero static camber**.
  - Ackermann: **"lines projected through the center of the King Pins, and through
    the bolts holding the track rods, should meet at the center point of the rear
    axle"** — the true-Ackermann construction exactly, and the source for the 1.0
    default.
  - It also states the purpose of the inclination is to **counteract the jacking
    effect of the caster**, which the model reproduces: the two terms oppose each
    other on the outside front wheel.
  - Caution: this source calls the kingpin inclination "camber angle", and gives
    **20–25°** of caster for amateur karts, which is above what the racing-chassis
    measurements above show.
- **A published scrub radius, and a worked go-kart steering design** — *Design &
  Calculations of Ackermann Steering System in a Go-kart*, JETIR, paper
  JETIR2501641, <https://www.jetir.org/papers/JETIR2501641.pdf> (PDF read in
  full). For a kart of wheelbase 1066.8 mm and front track 965.3 mm:
  **caster 8–14°**, **kingpin inclination 14–15°**, **positive scrub radius
  92.96 mm**, inner wheel angle 45.818° and outer 29.683° at full lock, steering
  ratio 1.02:1. The scrub radius is the only *published* one found for any kart,
  and it establishes the order of magnitude: a kart runs tens of millimeters where
  a road car runs ±15 mm.
- **The Ackermann relation** — Vroom Kart, *Ackermann's angles*,
  <https://www.vroomkart.com/news/28965/ackermanns-angles>. Gives the condition
  binding the two steer angles as a difference of cotangents against track over
  wheelbase, and states that Ackermann **"accentuates load transfer in diagonal
  sense, and increases inside rear wheel lift"**. (The article prints the
  difference with the inner term first; the sign convention that makes it a
  positive track/wheelbase is `cot(outer) − cot(inner)`.)
- **Which corner jacks, and which wheel lifts** — ANGRI Racing Academy, *Chassis
  cornering dynamics*, <https://www.angriracing.com/chassis-cornering-dynamics>.
  The independent check on the sign convention, and the reason to be confident in
  it: turning the wheel makes **"the outer front tire lift while the inner front
  tire presses down"** relative to the chassis, after which the chassis **"pivots
  around a line joining the inside front and outside rear, causing the inside rear
  to lift"**. Both statements match what the derivation in `steering.h` produces,
  and neither was used to build it.
- **What increases the jacking** — ANGRI Racing Academy, *Kart setup*,
  <https://www.angriracing.com/kart-setup>, and Top Kart USA, *Advanced front end
  alignment*, <https://topkartusa.net/advanced-front-end-alignment/>.
  - "Widening the front track will create more of a jacking effect when the wheels
    are turned", adjusted with **5 mm and 10 mm spacers**; and "increased caster
    also increases the jacking effect on the front wheels which helps unload the
    rear axle (inside wheel) more on corner entry".
  - Top Kart: moving the track rod to the inner hole on the stub axle **increases
    Ackermann**, which "will increase the amount of lift off the track you'll get
    with the inside rear tire through a corner". Their published alignment target
    is toe, not caster: **2–3 mm total toe out**.
- **The caster adjustment range** — ANGRI Racing Academy, *Caster and camber
  adjusters*, <https://www.angriracing.com/caster-and-camber-adjusters>, plus
  retailer listings for OTK eccentric pills (Comet Kart Sales, Fastech-Racing).
  Eccentric kingpin pills give a **total of about 3°** of caster range, half a
  degree per dot, and the page is explicit that the marked figures are approximate
  because they depend on chassis width and stub-axle boss height. Spindle inserts
  are sold with a wider **±3°** range.

### What the references settled

1. **Caster is 18°.** Two independent gauge measurements on racing chassis (18.0,
   19.7, "18.something") and a stated 15–20° range. The 20–25° from the NatSKA
   guide and the 8–14° from the JETIR paper both describe different vehicles —
   amateur and student karts — and are recorded but not used.
2. **Kingpin inclination is 11°.** The NatSKA guide's 10–12°, corroborated by the
   10.2° measured on an OTK C section. The JETIR paper's 14–15° is the outlier.
   The model is insensitive to this choice for wheel lift — under 4% across
   0–16° — so the disagreement costs little.
3. **Static camber is zero.** The stub axle is machined at the inclination angle
   so the wheel stands flat. Stated directly by the NatSKA guide.
4. **Scrub radius is derived, not authored.** It is the spindle arm less
   `wheel_radius × tan(inclination)`, which for this kart's 90 mm stub axle gives
   **63 mm** — between the 93 mm the JETIR paper publishes and the ±15 mm a road
   car runs, and on the correct side of both.
5. **The default Ackermann is 1.0.** The construction quoted by the NatSKA guide
   *is* true Ackermann. Karts are run past it, so the parameter is not clamped.
6. **The sign convention is right.** Confirmed against a source that describes the
   same motion in words: outer front tire up relative to chassis, inner front
   down, chassis pivoting on the inside-front-to-outside-rear diagonal, inside
   rear lifting.

### What could not be sourced

- **Any KZ manufacturer's published front-end geometry.** No OTK, CRG, Birel or
  Kosmic setup sheet with caster or inclination in degrees was found. Everything
  above is forum measurement, a guide book quoted second-hand, or a paper about a
  different class of kart. The angles are therefore *plausible and cited*, not
  *specified*.
- **A published Ackermann percentage for any kart.** Vroom Kart and Top Kart both
  discuss adding and removing Ackermann without quoting a number, and none of the
  chassis manufacturers publish one. **Assumed: 1.0**, the geometric solution,
  which is at least the construction the guides describe rather than a guess at
  how far past it a given chassis sits.
- **Steering lock.** No source for a CIK or KZ maximum steer angle was found; the
  JETIR paper's 45.8° is for a slow kart with a very different job. **Assumed:
  25° at the inner wheel**, which is not really an assumption — it is inherited
  from `scripts/game/kart_debug_vehicle.gd`, where it is the angle the bodywork
  clearance tables in issues #109 and #110 were measured at. Applying it to the
  *inner* wheel means no front wheel ever exceeds the measured figure.
- **Spindle arm length.** Taken from `tools/blender/kartlib/params.py`'s
  `stub_axle_length` (90 mm), which is the generated kart's own dimension rather
  than an independent source. It is the single most sensitive input to the wheel
  lift, so it is worth a real measurement if one turns up.
- **Any measured figure for how far a real kart's inside rear lifts.** Every
  source says it lifts and none says by how much. The model's rigid-frame
  geometric answer is 20.9 mm at full lock, and there is nothing to check it
  against beyond "centimeters, visibly off the ground".

## Chassis flex and suspension — issues #31 and #32

A fragment for `REFERENCES.md`. Everything here is a written or numeric source
rather than a photograph: the quantities the model needed are stiffnesses, and a
stiffness cannot be read off a picture.

### Written sources

- **Go-kart frame torsional stiffness — the primary source.** Fu, C.-C. and
  Wang, S.-C., *A study on torsional stiffness of the competition go-kart frame*,
  WIT Transactions on The Built Environment Vol 91 (2007), pp. 189–198,
  <https://www.witpress.com/Secure/elibrary/papers/OP07/OP07018FU1.pdf>. PDF read
  in full.
  - Baseline competition frame, model (a), taken from Solazzi: **193,620
    N·mm/deg = 193.6 N·m/deg**, at 10.21 kg, with a 628 mm kingpin width.
  - Modified frames run 205,962 to 253,357 N·mm/deg — so **206 to 253 N·m/deg**
    covers a realistic design range. Kingpin width matters more than added
    tubes: about **7% more stiffness per 50 mm** of width, up to the CIK-FIA
    limit of 1400 mm.
  - Method: rear anchor points fully constrained, a vertical force applied at
    **one** kingpin, twist angle taken across the kingpin span. The paper's own
    numbers are internally consistent with that reading — 6.8428 mm of
    deflection over 628 mm is 0.6244°, against the 0.623513° tabulated.
  - Cites **Biancolini et al.** as recommending a minimum of **165,000–169,000
    N·mm/deg** for a kart frame. Not read directly; recorded as a secondary
    citation.
  - Also states the mechanism this project is modeling, in the paper's own
    words: the torsional stiffness of a kart frame "must be able to compensate
    the fact of no differential gear by producing load transfers during
    cornering".

- **A second, much larger published figure.** Sampayo, Luque, Mántaras and
  Rodríguez, *Go-Kart Chassis Design Using Finite Element Analysis and Multibody
  Dynamic Simulation*, International Journal of Simulation Modelling 20(2)
  (2021), pp. 267–278, <http://www.ijsimm.com/Full_Papers/Fulltext2021/text20-2_555.pdf>.
  PDF read in full.
  - Four frame variants at **1,051 / 1,625 / 2,314 / 3,464 N·m/deg**.
  - Different test: rear hard points fixed and a **couple** applied at two front
    hard points, two equal and opposite vertical forces. Not the same
    measurement as Fu and Wang's single-force test, which is the most likely
    reason for the factor of five to eighteen between them.
  - Total modelled kart mass 187 kg; AISI 4130 tube frame; the reference
    cornering case is 13.27 m/s at 7.8 m/s² lateral.
  - **The disagreement is not academic.** `tests/core/test_chassis_flex.cpp`
    sweeps the range, and the crossover between "the inside front lifts" and
    "the inside rear lifts" is at roughly 100 N·m/deg — below both figures, but
    the *margin* changes from 0.06 g to 0.42 g across them. This is the single
    number most worth measuring on a real frame.

- **Rear axles are sold in stiffness grades, and the grade is a tuning tool.**
  - Kartech 50 mm axle range: Extra Soft, Soft, Medium Soft, Medium, Medium
    Hard, Hard, Extra Hard — seven grades in one product line. DPE Kart
    Superstore, <https://dpekartsuperstore.com/products/kartech-axle-50mm-bundle-soft-medium-hard-hard>;
    the Medium Soft addition is reported by KartSportNews,
    <https://www.kartsportnews.com/2026/03/07/additional-axle-grade/>.
  - Intrepid sells Ø50 × 1000 mm axles by grade,
    <https://www.intrepid-kart.com/en/parts-and-accessories/axles-4/axle-50mm-330>.
  - Direction of the effect, from Albino Parolin via TKART (read through search
    extract; tkart.it returns 403 to scripted fetches): a **softer axle** gives
    less grip on corner entry, more mid-corner, and **less traction on exit**; a
    **harder axle** gives better entry grip and **more traction on exit**, and
    "frees the kart" off the turn. Karts leave the factory on a medium.
  - **This corroborates the model's most surprising result.** A harder rear axle
    raises the rear roll stiffness, biases the roll-stiffness split rearward,
    and lifts the inside rear earlier — which is exactly "frees the kart off the
    turn". The model reproduces the direction of a real tuning knob without
    having been fitted to it.

- **Frame stiffness is the kart's only suspension.** Sampayo et al., §1: a
  go-kart "does not have a suspension system based on springs and dampers
  actuating between the wheels and the structure. Due to this lack, the frame
  stiffness will characterize the dynamic response of the vehicle." Recorded
  because it is the sentence `src/core/suspension.h` is written around.

- **CRG chassis setup and tuning manual**,
  <https://nhka.net/wp-content/uploads/2018/02/crg-setup-guide.pdf>. Located but
  not read in this session; listed so the next one does not re-search for it. It
  is the obvious source for real caster, kingpin-inclination and tire-pressure
  numbers, which `steering.h` will want.

### What could not be sourced, and what was assumed instead

**Kart slick vertical stiffness, in N/mm.** Not found. Searched for measured
load-deflection curves for 10×4.50-5 and 11×7.10-5 kart slicks across WebSearch,
OpenAlex and the two multibody papers above; the papers model the tires but do
not publish the rate. General tire-stiffness literature exists (215–293 N/mm for
a 255/40R17 road tire) and is the wrong size and pressure to extrapolate from.

**Kart tire static vertical deflection.** Also not found.

What is used instead, and it is a derivation and not a measurement:

    F = 2 * p * w * sqrt(2 * R * d)        pressure-membrane model
    k = dF/dd = 4 * p^2 * w^2 * R / F

| | pressure | patch width | radius | static load | → rate | → deflection |
|---|---|---|---|---|---|---|
| front | 0.75 bar | 0.110 m | 0.140 m | 360 N | **106 kN/m** | 1.7 mm |
| rear | 0.85 bar | 0.180 m | 0.1475 m | 498 N | **277 kN/m** | 0.9 mm |

The model ignores carcass stiffness, so it under-predicts a stiff tire and is
roughly right on a soft low-pressure one — which a kart slick is. Two
consequences are worth stating with the assumption:

1. **The absolute rates only set the corner frequency**, which comes out at 8.5
   and 11.8 Hz. That is where a suspensionless vehicle on tires should be, an
   order above a road car's 1.5 Hz, so the absolute values are at least not
   absurd.
2. **The front-to-rear ratio is what decides which wheel lifts**, and the ratio
   here is 2.6. It follows from the tire sizes — 215 mm of rear tire against 135
   mm of front, at a higher pressure — rather than from a preference for the
   answer, but it is derived and it is load-bearing, so it is the first thing to
   attack if the kart lifts the wrong wheel. With equal front and rear rates the
   model lifts the inside **front** instead.

If the project owner has a real load-deflection curve for a kart slick, or a
number from a chassis manual, it would replace the single weakest input in this
model.

**Caster jacking magnitude.** Not sourced here, and deliberately: it belongs to
`src/core/steering.h`, which another agent owns. The chassis model consumes it
through `CornerState::geometric_offset` and reports what it needs — about 6.5 mm
of antisymmetric front rise at 2.0 g — rather than computing it.

<!--
Fragment for docs/REFERENCES.md — merge as a new section after "Powertrain —
issue #116". Written by the M3b drivetrain agent (issues #36-#40); it does not
belong in the tree as a separate file once merged.
-->

## Drivetrain — issues #36-#40

`src/core/engine.h`, `gearbox.h`, `clutch.h` and `drivetrain.h` are built from
these. The rule from ARCHITECTURE.md §5 item 10 applies to numbers as much as to
shapes: nothing below was recalled, and where a number could not be found it says
so in the same breath as what was assumed instead.

There are no photographs in this section. A gear ratio is not a shape, and the
authority for one is a parts catalog with a tooth count on it.

### What was read

| Ref | Subject | Source |
| --- | --- | --- |
| D1 | **TM KZ R1/R2/R3 gearbox, every gear with its tooth count** | Kartshop, [Gear Box, TM KZ R1 and 10C](https://kartshop.com/shop/gear-box-796c1.html) |
| D2 | TM part 40454, "Gear 6th Mainshaft, Z27, KZ10" — the one gear D1 does not list | Kartshop, [product page](https://kartshop.com/shop/gear-6th-mainshaft-31213p.html) |
| D3 | **TM clutch gear, 75 teeth (part 40385)**, and the KZ10C clutch parts list | Direct-Karting, [KZ10C Clutch](https://direct-karting.com/en/kz10c-clutch) |
| D4 | TM primary drive gear Z18 (part 40318), KZ10C/KZ-R1 | Direct-Karting, [Primary gear TM Z18](https://direct-karting.com/en/primargear-tm-z18-kz10c-kzr1); TM Racing, [part 40318](https://www.tmracingonline.com/tm-40318/) |
| D5 | TM engine sprockets, 14-21 teeth, 428 chain | Direct-Karting, [KZ Engine Sprockets](https://direct-karting.com/en/kz-sprockets-drive) |
| D6 | KZ10C primary shaft and gear shaft part lists, cross-check on D1's naming | Prespo, [gear shaft KZ 10 C](https://www.prespo-kartshop.com/engines-tm/spare-parts-kz-10-c/gear-shaft-kz-10-c/); Direct-Karting, [KZ10C Primary Shaft](https://direct-karting.com/en/kz10c-primary-shaft) |
| D7 | KZ10C clutch part list — dry multi-plate, coated and steel discs, springs | Prespo, [clutch KZ 10 C](https://www.prespo-kartshop.com/engines-tm/spare-parts-kz-10-c/clutch-kz-10-c/) |
| D8 | **KZ dyno figures in the field**: 49.8 bhp measured, ~53 hp claimed for factory engines, and "a KZ in CIK configuration is reasonably about 45 cv" | KartPulse, [KZ Dyno Figures](https://forums.kartpulse.com/t/kz-dyno-figures/13544) |
| D9 | **"With gears the KZ can work with a narrower powerband, say 2500 RPM"**, against 9,000-12,000 rpm for a single-speed kart | KartPulse, [Old Super RoK Info](https://forums.kartpulse.com/t/old-super-rok-info/12434) |
| D10 | **A TM KZ10 "limited to 14,000 rpm"**, plus a real 20/20 sprocket setup and what another karter says it must be doing | KartPulse, [Carburation for TM KZ10 144cc Fun 56 Upgrade](https://forums.kartpulse.com/t/carburation-for-tm-kz10-144cc-fun-56-upgrade/11035) |
| D11 | **KZ1R worked between 11,500 and 14,500 rpm in the high gears, extendable to 15,000**; and an explicit refusal to publish a power scale on a dyno curve | Vroomkart, [TM KZ1R 125, preparazione base vs full, Galiffa Tuning](https://www.vroomkart.it/news/41205/tm-kz1r-125-preparazione-base-vs-preparazione-full-by-galiffa-tuning) |
| D12 | TM 125 KZ10C: 54 mm bore, 54 mm stroke, 30 mm carburetor, six-speed, 20 kg complete | Sodikart, [TM 125 KZ10 C](https://www.sodikart.com/en-gb/karts/engines/tm/125-kz10-c-9.html) |
| D13 | Shifter final drive practice: engine sprocket 16-19, axle sprocket 21-30, worked examples 19/23 = 1.210, 18/22 = 1.222, 17/21 = 1.235, largest sprint axle gear 28 | Bob's 4 Cycle Karting, [Shifter kart rear axle sprocket](https://4cycle.com/karting/threads/shifter-kart-rear-axle-sprocket.102670/) |

Read through search extracts only, and used only as corroboration — the sites
return 403 to a scripted fetch, the same way tkart.it does:

- IAME Screamer 4 KZ: 124.59 cc, 54.00 mm bore, 54.40 mm stroke, reed valve,
  Dell'Orto VHSH 30, six-speed, **five-disc dry clutch**. `iamekarting.com`.
- Honda CR125R, a 125 cc reed-valve two-stroke with published ratios (2.357,
  1.867, 1.579, 1.333, 1.130 in five speeds), as a sanity check that a
  close-ratio 125 gearbox looks like the one below. `motorbikecatalog.com`.

### What the references settled

1. **The whole ratio chain is tooth counts.** From D1, D2 and D4:

   | | 1st | 2nd | 3rd | 4th | 5th | 6th |
   | --- | --- | --- | --- | --- | --- | --- |
   | Mainshaft | 13 | 16 | 18 | 22 | 22 | 27 |
   | Countershaft | 33 | 29 | 27 | 27 | 23 | 25 |
   | Ratio | 2.538 | 1.813 | 1.500 | 1.227 | 1.045 | 0.926 |

   with a primary reduction of **75/18 = 4.167** (D3, D4). Sixth is an
   overdrive, which looks wrong and is not: a 4.167 primary has to be given back
   somewhere. D1 and the KZ10B listings agree gear for gear, and D2 supplies the
   sixth mainshaft gear D1 omits — under the same part number, 40454, that
   Direct-Karting lists as the KZ10-C sixth primary-shaft gear.

2. **Final drive is 18/25 here, and it is a choice, not a specification.** D5
   gives 14-21 teeth on the engine and D13 gives 21-30 on the axle, so the pair
   is a track-by-track setting rather than part of the engine. 18/25 = 1.389 is
   shorter than D13's road-race examples (1.21-1.24) and inside its sprint range;
   it is the value that puts sixth gear at the rev limiter inside
   `kz_reference.h`'s 135-145 km/h, and `Gearbox` exposes both sprockets for
   exactly that reason.

3. **The rev limiter sits between 14,000 and 15,000 rpm.** D10 has a TM KZ10
   limited to 14,000; D11 has a KZ1R worked to 14,500 and extendable to 15,000.
   `engine.h` uses a soft cut at 14,300 tapering to a hard cut at 14,800.

4. **Peak power is 45 hp and the field measures more.** D8 is the honest picture:
   49.8 bhp on one dyno, 55 hp claimed on others, ~53 hp believed genuine for a
   factory engine, and a French poster's summary that a KZ in CIK configuration
   is reasonably about 45 cv given ten separate regulatory restrictions.
   `kz_reference.h` already says 45 hp and this section is the reason to leave it
   there: it is the conservative, class-legal figure, and every larger number in
   D8 is explicitly relative to one dyno.

5. **The powerband is about 2,500 rpm wide.** D9, from an engine builder,
   contrasted directly with the 9,000-12,000 rpm a single-speed kart has to pull
   over. The curve in `engine.h` holds 90% of peak torque from 10,740 to 13,400
   rpm — a 2,660 rpm band — and 80% from 9,995 to 13,915, which is
   `kz_reference.h`'s usable range almost exactly.

6. **The clutch is a dry multi-plate pack on the gearbox input shaft**, not a
   centrifugal clutch (D3, D7, and the IAME extract's five-disc dry clutch). It
   is released by a hand lever, and the parts list is a stack of alternating
   coated and steel discs with coil springs and a pressure plate — which is why
   `clutch.h` models Coulomb friction with a capacity and a lever position rather
   than an rpm-dependent engagement.

### The independent check that mattered most

D10 is not a specification, and it is the strongest evidence in this section. A
driver posts that he runs 20-tooth sprockets front and rear; another karter tells
him "if you're running 20T front and back, you can't be anything like 14,000 in
G6. You should be close to 160 km/h in G5 at that RPM."

Nothing in `gearbox.h` was fitted to that. Setting both sprockets to 20 and
asking for fifth gear at 14,000 rpm returns **166.6 km/h**, and his observed
130 km/h comes back as **9,676 rpm in sixth** — which is the other half of what
he was told. Two numbers, from a chain of four independently sourced ratios and a
tire radius, landing on a stranger's arithmetic. `test_gearbox.cpp` keeps it as a
test case.

### What could not be sourced, and what was assumed instead

Three numbers. Each is flagged in the header that uses it as well as here.

1. **A dyno trace with numbers on both axes.** There is none to find. D11 states
   outright that it publishes KZ curves *without* a power scale, on the grounds
   that dynos disagree, and D8 is a thread of people agreeing that cross-dyno
   comparison is meaningless. So the intermediate points of `WOT_CURVE` are
   interpolated between hard constraints — peak power at
   `kz_reference.h`'s rpm and value, a powerband no wider than
   `kz_reference.h`'s, and a curve weak enough below 9,000 rpm to match D9 — and
   the shape between them is a two-stroke's characteristic knee and cliff. It is
   not measured data and must not be quoted as any.

2. **Crankshaft rotational inertia.** No manufacturer publishes it and no dealer
   listing gives a crank mass. `engine.h` uses **0.0055 kg·m²**, estimated
   geometrically: two full-circle steel webs treated as 110 mm discs 22 mm thick
   give 1.6 kg and 2.5e-3 kg·m² each, and the rod, piston and ignition rotor add
   the rest. The web dimensions are themselves inferred from the 54.4 mm stroke
   and a KZ crankcase, so this is an estimate resting on an estimate. It is the
   number most worth replacing with a measurement, because the drivetrain
   reflects it to the axle through the square of the total ratio — 1.19 kg·m² in
   first gear, which is 63 kg of apparent mass at the tire.

3. **Clutch torque capacity.** Not published, and not derivable from the parts
   list without a friction coefficient and a spring rate. `clutch.h` uses
   **45 N·m**, which is 1.7x the modeled peak crank torque — the usual sizing
   margin for a clutch that must not slip in service. The consequence of it being
   wrong is visible rather than hidden: it sets how much clutch travel a launch
   needs and how hard a botched clutchless upshift hits the axle.

One more thing is chosen rather than sourced, though it is calibrated against an
outcome rather than invented: the **closed-throttle drag slope** in `engine.h`,
0.00462 N·m·s, which puts engine braking at 7.9 N·m at 12,000 rpm. Two-stroke
closed-throttle losses are not published for any kart engine. It was set so that
second gear at 60 km/h decelerates the kart at 0.29 g, which is the fraction of
`kz_reference.h`'s 1.5-2.0 g braking that makes ARCHITECTURE.md §6.3's claim —
that lifting can shape corner entry more than the brakes — true rather than
decorative.

## Surfaces — issue #42

*A fragment for `docs/REFERENCES.md`. Merge it in as a top-level section; it is
kept separate only so that two agents did not write the same file at once.*

`src/core/surface.h` gives asphalt, curb, grass and dirt a grip multiplier. This
is where every one of those numbers comes from, and where the ones that came
from nowhere are named as such.

### The one assumption everything else rests on

Published friction coefficients are measured with ordinary road tires, whose own
dry-asphalt coefficient is 0.65–0.70. `tire.h` gives a kart slick **2.10**. So a
multiplier cannot simply be "the literature's grass over the literature's
asphalt" — that ratio is 0.53, and it would hand a slick 1.1 g on a lawn.

The table splits on where the shear plane is:

- **Hard surfaces** — asphalt, painted concrete. The failure is rubber against
  stone, so the compound is what differs between a road tire and a slick, and a
  *ratio between two hard surfaces* carries across. The **curb** multiplier is a
  ratio.
- **Deformable surfaces** — grass, compacted earth. The failure is inside the
  terrain. Soil shears at the soil's strength regardless of what is standing on
  it, so the published coefficient carries across as an **absolute** number and
  the multiplier is that coefficient divided by the tire's own 2.10. **Grass**
  and **dirt** are absolutes.

That split is reasoned, not measured. It is the single thing in this section most
worth disagreeing with, and disagreeing with it moves grass by a factor of three.

### The table

| Surface | Multiplier | Derived from | Lateral g the kart can hold |
| --- | --- | --- | --- |
| asphalt | 1.00 | definition — `tire.h` is already a slick on hot asphalt | 2.13 |
| curb | 0.72 | PTV 35 / PTV 49, paint over asphalt (S1) | 1.53 |
| grass | 0.18 | μ 0.37 dry rye-grass / 2.10 (S2) | 0.38 |
| dirt | 0.17 | μ 0.35 gravel-and-dirt road / 2.10 (S3) | 0.36 |

The lateral-g column is measured, not multiplied out by hand:
`tests/core/test_surface.cpp` puts a quarter of 175 kg on each of four tires,
asks `Tire::evaluate` for pure lateral force at the slip angle where the curve
peaks, and divides the sum by the weight. It is the number issue #42's "driving
onto grass loses grip immediately and obviously" is really about — a kart holding
2.13 g that puts two wheels on grass keeps 18% of what it had.

### Sources

- **S1 — road marking paint against the pavement under it.**
  Burghardt, T. E., Köck, B., Pashkevich, A., & Fasching, A. (2023). *Skid
  resistance of road markings: literature review and field test results.* Roads
  and Bridges – Drogi i Mosty, 22(2), 141–165.
  <https://doi.org/10.7409/rabdim.023.007>,
  <https://rabdim.pl/index.php/rb/article/view/v22n2p141>.
  Field experiment: the asphalt surface measured **PTV 49**; the same surface
  painted with no anti-skid additive measured **PTV 35**; with glass microbeads
  45; with microbeads plus 10% corundum 50. **35 / 49 = 0.714 → 0.72.**

- **S1b — that a kart kerb is a painted surface at all.**
  CIK-FIA, *Circuit Regulations, Part 1* (2025 preparatory text), §14.6:
  "Kerbs must be painted in two colours alternately (recommended colours: red and
  white)." <https://www.fiakarting.com/sites/default/files/2025-04/7.1_Pr%C3%A9pa%20RCIRC%20PI%20+%20II%202025.pdf>
  This is what justifies using a *paint* figure rather than a concrete figure for
  the curb: what a tire touches on a kerb is enamel, not aggregate.

- **S2 — grass.**
  Cenek, P. D., Jamieson, N. J., & McLarin, M. W., *Frictional Characteristics of
  Roadside Grass Types*, Opus International Consultants, Central Laboratories,
  New Zealand.
  <https://saferroadsconference.com/wp-content/uploads/2016/05/Peter-Cenek-Frictional-Characteristics-Roadside-Grass-Types.pdf>
  Table 4, locked-wheel braking, coefficient of braking friction, dry:
  **rye-grass (long) 0.36, rye-grass (short) 0.38**; wet 0.21 and 0.24. Mean of
  the dry pair, **0.37 / 2.10 = 0.176 → 0.18.**
  The same paper's multi-surface skid case quotes **dry chipseal at μ = 0.70**
  measured by the same team, which is where the road-car ratio of 0.53 comes from
  — the number this table deliberately does not use.
  Two caveats the authors state themselves and that matter here: their test
  ground "was very hard due to a dry spell", so the values "are likely to be at
  the lower range of what can be expected"; and their tyre-dragging test (Table 3,
  dry rye-grass 0.77) uses a very lightly loaded tyre at ≤ 3 km/h and the authors
  say those figures "should only be used for ranking purposes". The locked-wheel
  numbers are the ones used above.

- **S3 — dirt.**
  Noon, R. (1994), *Coefficients of Friction of Various Roadway Surfaces*,
  reproduced as Table 1 of S2: gravel and dirt road **0.35**, wet grassy field
  0.20, dry asphaltic concrete 0.65, dry concrete 0.75, loose moist dirt that
  allows the tyre to sink about 5 cm 0.60–0.65. **0.35 / 2.10 = 0.167 → 0.17.**
  Which of Noon's dirt rows applies is settled by the CIK-FIA regulations rather
  than by preference: *Circuit Regulations, Part 1* §7.5 requires the verge
  bordering a kart track to be "grass-covered or **compacted** ground over a
  minimum width of 1 m", and §8.2 makes a loose gravel bed a separate,
  deliberately decompacted deceleration device. The dirt this game drives onto is
  the compacted verge, so the compacted row is the right one.

- **S4 — cross-check, not used directly.**
  Wong, J. Y. (1993), *Theory of Ground Vehicles*, 2nd ed., p. 26, via
  <https://hpwizard.com/tire-friction-coefficient.html>: asphalt and concrete
  (dry) peak 0.80–0.90 / sliding 0.75; gravel 0.60 / 0.55; earth road (dry) 0.68
  / 0.65; snow 0.20; ice 0.10. Wong's dry earth road is a *road*, and its ratio
  to his asphalt (0.80) is far higher than Noon's (0.54) — the two tables are
  describing different surfaces under the same word. Recorded because that
  disagreement is the reason the CIK-FIA text was needed to decide which one this
  game's "dirt" is.

### What could not be sourced, and what was assumed instead

1. **The curb ripple's dimensions.** `surface.h` carries a 12 mm amplitude and a
   0.15 m wavelength and **neither is sourced**. The CIK-FIA circuit regulations
   specify a kerb's paint and its repair and say nothing about its profile, and
   no dimensioned drawing of a kart kerb was found. What the wavelength *is*
   anchored to is the tire rather than to a kerb: a 0.1475 m rear slick
   deflecting about 2 mm has a contact chord of 2·√(2Rd) ≈ 49 mm, and a tire
   bridges any ripple much shorter than its own patch instead of following it, so
   0.15 m is about the shortest ripple a kart can feel as individual bumps. 12 mm
   is an amplitude a kart can ride without grounding its floor tray. Both are
   candidates for replacement by a photographed kerb under the §5 item 10 rule,
   and both are pinned by a test so that changing them has to be deliberate.

2. **Whether the curb figure should be a dry number.** PTV and BPN are
   conventionally measured on a wetted surface, so 0.714 is a wet ratio and the
   dry ratio of paint to asphalt is probably higher. 0.72 is therefore
   conservative on a dry track. It was kept low on purpose — a kerb that costs
   nothing is not a kerb — but it is the first number a tuner should reach for,
   and the honest description of it is "a wet-test ratio used dry".

3. **Rolling resistance.** Not in the table at all. A kart on grass loses speed
   to more than grip: the tire ploughs, and that is a drag term, not a friction
   multiplier. No sourced rolling-resistance coefficient for a slick on turf was
   found, and inventing one and calling it data would be worse than leaving the
   column out. It is the honest lever if grass and dirt ever need to be told
   apart by feel — see the next point — and it wants a ticket rather than a
   guess.

4. **Grass and dirt come out 6% apart, and that is not a mistake.** S2 says it in
   as many words: "the coefficient of dry rye-grass is comparable to gravel." The
   two independent derivations agreeing is a reason to believe them, not a reason
   to spread them. They are told apart by what they sound like, what they throw,
   and what they look like — the §12 and M10 hooks — and not by grip.

5. **Surface temperature, rubber pickup, and marbles.** All real, all absent.
   A kart returning from a dirt verge carries the dirt on its tires for most of a
   lap, which is a memory effect this table has no room for and §5 item 7's
   "marbles off the racing line" will eventually want. Out of scope for #42.

## Engine audio — §12

`ARCHITECTURE.md` §12 decides the engine note is **synthesized, not sampled**:
"harmonic stack with fundamental driven by RPM, per-harmonic gain envelopes
shaped by load, noise layer, comb-filtered exhaust resonance." Every one of those
words hides a number. This section is where those numbers come from, and — more
often — where they could not be got.

§5 item 10 says do not model a real part from memory or from prose. That rule was
written about geometry, but a two-stroke's harmonic ladder is exactly the kind of
thing a session will otherwise invent from a plausible-sounding intuition, so it
is applied here too. Everything below was measured off a recording that is named,
licensed and hash-pinned, or it is marked as not measured.

### What was listened to

Twelve recordings, all from Wikimedia Commons, all CC0 / public-domain / CC BY /
CC BY-SA. Nothing with an ambiguous license was used. Full attribution, file
URLs and SHA-256 are in `ATTRIBUTION.md`; this table is the engineering view.

| Recording | What the engine actually is | License |
| --- | --- | --- |
| `WWS_MotorcycleTOMOSD-9` | **Tomos D-9**, 1965 50 cc two-stroke GP racer, expansion chamber, **11 hp @ 14,000 rpm**, nine-speed | CC BY 4.0 |
| `WWS_MotorcycleTOMOSD7` | **Tomos D-7**, 1962 50 cc two-stroke GP racer, expansion chamber, ~7 kW | CC BY 4.0 |
| `WWS_MotorcycleTOMOSColibrispecialD-3` | **Tomos Colibri special D-3**, 1959 50 cc two-stroke racer, 3.7 kW, three-speed | CC BY 4.0 |
| `Yamaha_RX-100_accelerates_to_top_speed` | **Yamaha RX-100**, 98.2 cc reed-valve two-stroke street single, four-speed, accelerating through all four gears | CC BY-SA 4.0 |
| `WWS_Chainsaw` | Stihl MS 150 C, 23.6 cc two-stroke, **muffler, no tuned pipe** — the negative control | CC BY 4.0 |
| `Chainsaw_1`, `Chainsaw_5` | Unnamed two-stroke chainsaws | Public domain |
| `Piaggio_Vespa_Suono_Motore` | Vespa PK 125 S, 125 cc two-stroke, **touring silencer** | CC BY-SA 4.0 |
| `Garelli_Bonanza_starten_01` | Garelli Bonanza moped, cold start and ride away | CC BY-SA 3.0 |
| `USSR_bicycle_2T_engine_D-4` | D-4 auxiliary bicycle engine, 45 cc, USSR 1956 | Public domain |
| `Wurstelprater_Wien_2024_GoKart_Honda` | Honda 200 cc **four-stroke** rental kart, governed to 30 km/h — the four-stroke control | CC BY-SA 4.0 |
| `Wurstelprater_Wien_2024_Bandito_Rennbahn_Go-Kart` | Amusement-park kart, type unstated | CC BY-SA 4.0 |

A second sweep — archive.org's `radio-aporee-maps` field-recording collection,
Freesound's public preview URLs, and Commons video — turned up recordings that are
much closer to the target, and these are the ones that matter:

| Recording | What it is | f0 measured | Implied rpm (2T) | License |
| --- | --- | --- | --- | --- |
| `commons_Patras2011_PICK_kart` | Patras International Circuit for Kart, Greece, 2011 — **a kart racing circuit** | 204.5 Hz median, 261.2 p95 | **12,270–15,670** | CC BY-SA 3.0 |
| `fs529071_Eindhoven_kartbaan` | Eindhoven kart track, Netherlands, 2020 | 157.1 Hz median, 207.1 p95 | **9,430–12,430** | **CC0** |
| `aporee_Scarborough_2012_125-400cc` | Oliver's Mount road races, described as "125-400 cc practice two stroke" | 173.0 Hz median, 214.4 p95 | **10,380–12,860** | CC BY 3.0 |
| `fs317470_GoKartRacing_Outdoors` | **Negative control** — four-stroke rental kart | 25.2 Hz | 1,510 (2T) / 3,020 (4T) | CC0 |

**So a two-stroke racing kart at KZ rpm *was* found, and the earlier conclusion
that none existed was wrong.** What does not exist under any acceptable license is
a recording *labeled* as a specific KZ engine — no TM KZ-R1, no Vortex ROK
Shifter, no IAME Screamer, no Modena. These three are identified **by
measurement, not by their labels**: a firing fundamental of 157–261 Hz can only be
a two-stroke, because read as a four-stroke it would demand 18,000–31,000 rpm,
which no kart engine of any kind reaches. The negative control confirms the split
is real rather than an artifact of the tracker — the rental kart lands at 25 Hz,
a factor of eight below, exactly where a governed four-stroke belongs.

**Patras is the single best analogue in the corpus**: an actual kart circuit,
running at 12,270–15,670 rpm, which is a KZ's working range. Eindhoven is the best
*licensed* one, being CC0 and therefore free of any obligation.

Among the older set the closest analogue is the Tomos D-9: a quarter of the
displacement, but a tuned expansion chamber and a published **14,000 rpm**
peak-power speed. Rev range and pipe design are what shape the spectrum;
displacement mostly sets how much air moves. The Vespa is the *only* 125 cc
two-stroke in the corpus and it is the **least** representative, because a touring
silencer is the opposite of a tuned pipe — its rows are marked scooter-derived
wherever they appear and none of them is used.

All four new recordings pass the subharmonic test described next (m = 1, every gap
within ±2.9 dB) and were re-measured with the same pipeline rather than trusted
from the search that found them.

### The subharmonic question, and how it was settled

An earlier pass produced harmonic ladders that peaked at h3–h8 with the
fundamental apparently sitting at the noise floor, and suspected its own pitch
tracker had locked to a subharmonic. If true, every rpm figure below would be
wrong by an integer factor. A tracker locked to f0/m and a genuinely suppressed
fundamental look **identical in a single spectrum**, and a small two-stroke with
a tuned pipe legitimately can have a weak h1, so this could not be settled by
looking harder at the same picture.

It was settled with three estimators whose failure modes point in **opposite
directions**, scored first against a synthetic signal whose answer is known
exactly (`probe_f0.py`, true f0 = 120.000 Hz):

| Estimator | Bias when it fails | Result on the probe, h1 40 dB down | Result with h1 removed entirely |
| --- | --- | --- | --- |
| A — harmonic sum (log-mean scoring) | toward **sub**multiples | 120.0 Hz | 120.0 Hz |
| B — cepstrum of the log spectrum | measures spacing, blind to which partial is loud | 118.8 Hz | 118.8 Hz |
| C — analytic-envelope autocorrelation | toward **multiples** of the period | 117.6 Hz | 117.6 Hz |

and a fourth, direct test: under the hypothesis f0_true = m·f0, the harmonic
slots not divisible by m contain no partial at all. Handed a deliberately wrong
f0, that test fires hard and unambiguously:

| f0 handed to the test | gap at m=2 | m=3 | m=4 |
| --- | --- | --- | --- |
| 120 Hz — correct | 1.4 dB | −0.1 dB | −1.3 dB |
| 60 Hz — f0/2 | **41.6 dB** | 0.6 dB | 40.8 dB |
| 40 Hz — f0/3 | 0.1 dB | **42.1 dB** | 0.4 dB |
| 30 Hz — f0/4 | 8.5 dB | −0.0 dB | **41.7 dB** |

So a real subharmonic lock shows a ~41 dB gap and a correct f0 shows under
1.4 dB. There is no middle ground to argue about.

**The test has a resolution floor, and finding it is what actually resolved the
question.** Harmonic slots sit f0/Δf bins apart and a Hanning main lobe is four
bins wide, so once f0 approaches the bin width, leakage from each real partial
fills its neighbors' slots and the empty-slot signature cannot exist whatever the
truth is. Measured (`probe_resolution.py`), feeding the test a known-wrong f0/2:

| bins per f0 | m=2 gap | verdict |
| --- | --- | --- |
| 27.3 | 42.3 dB | works |
| 8.5 | 37.3 dB | works |
| 5.1 | 11.1 dB | marginal |
| 3.4 | **0.0 dB** | blind |

At the 8192-point window originally used (Δf = 5.86 Hz at 48 kHz) the test is
therefore trustworthy above about 50 Hz and **worthless below 30 Hz** — which is
precisely where the suspicious recordings sat. Re-run at 32768 points
(Δf = 1.46 Hz), where the probe says it is sensitive down to 20 Hz, the test
reports **m = 1 on all twelve recordings**, every gap within ±2.0 dB of zero.
The tracked fundamentals also moved by at most 17% across a fourfold change of
window length and a threefold widening of the search band — no estimate moved by
anything near a factor of two.

**Conclusion: no recording was tracked to a subharmonic, and no rpm figure below
is corrupted.** The apparent "h1 at the noise floor" was a measurement artifact
of a different kind: h1 was being compared against a *global* inter-harmonic
median taken across the whole 0–24 kHz spectrum. Outdoors, wind and handling
rumble raise the floor near a 20 Hz fundamental 20–30 dB above the floor at
2 kHz, so a perfectly healthy h1 scores as buried. Against the floor **in its own
octave**, which is the honest comparison, h1 stands 14–36 dB clear on every
high-revving recording:

| Recording | h1 over global median | h1 over local floor |
| --- | --- | --- |
| `WWS_Chainsaw` | 44.1 dB | 35.6 dB |
| `WWS_MotorcycleTOMOSD7` | 33.2 dB | 20.9 dB |
| `WWS_MotorcycleTOMOSColibrispecialD-3` | 32.7 dB | 18.2 dB |
| `Yamaha_RX-100` | 41.5 dB | 15.1 dB |
| `WWS_MotorcycleTOMOSD-9` | 19.1 dB | 14.0 dB |
| `Piaggio_Vespa_Suono_Motore` | 5.5 dB | 2.4 dB |

The fundamental is genuinely weak only on the recordings whose f0 is 17–28 Hz —
idling mopeds and governed four-stroke rental karts — where it falls below the
recording chain's usable response. That is a property of those recordings, not a
property of two-stroke engines, and none of them is used for anything.

**One recording is partly corrupted, by a different mechanism.** The Tomos D-9's
f0 track contains 44 frame-to-frame jumps at simple *rational* ratios out of 2679
transitions (1.6%): 15 at 2/3, 10 at 3/2, 10 at 1/2, 9 at 2. A ratio of 2/3
recurring fifteen times is not a gearbox — a nine-speed close-ratio box steps by
0.90–0.94, which is the *other* cluster in the same data — it is the tracker
jumping between the second and third partial. The integer-m spacing test cannot
see this, because 3/2 is not an integer. The comparison files are clean:
D-7 0.2%, Colibri 0.8%, Yamaha 0.08%. **The D-9's extreme rpm readings (its
17,000–18,000 rpm peaks, against a published 14,000 rpm peak-power speed) sit on
the wrong side of these jumps and are not trustworthy.** Its ladder and its
resonance, which are computed from partial positions in absolute frequency, are
unaffected and stayed rock-stable.

### That the firing fundamental is rpm/60

A single-cylinder two-stroke fires once per revolution, so the firing
fundamental *should* be rpm/60 Hz. No recording here has a tachometer channel, so
this was verified indirectly, on the Yamaha RX-100, by the **gearbox**:

- A gearbox multiplies crank speed by a ratio the exhaust knows nothing about. If
  f0 really is the firing rate, an upshift must step f0 by exactly that ratio.
- Commons documents the recording as accelerating "through all four gears".
  A four-speed box gives **three** upshifts. Exactly **three** clean downward f0
  steps were found under power, at t = 6.0, 8.8 and 15.6 s.
- Their ratios are **0.844, 0.905, 0.921** — increasing monotonically, which is
  what a real gearbox does and what nothing else in a recording does.
- Read as a two-stroke, the three shift points are **7214, 7341 and 7733 rpm**,
  against a published peak-torque speed of **7500 rpm** (peak power on a
  two-stroke sits above peak torque, so shifting a little past 7500 is exactly
  right). Read as a four-stroke the same shifts would be at 14,400–15,500 rpm,
  which a 98 cc air-cooled street single cannot do.

So **f0 = rpm/60 holds**, to the accuracy of "a rider shifts near peak power".
It is verified on one engine, not on all of them; the four-stroke rental-kart
control was too contaminated by other karts in the same recording to serve as the
matching negative test, and that is noted below as unfinished.

### The harmonic ladder — and what the tuned pipe does to it

This is §12's "harmonic stack", and it is the measurement most worth having,
because the intuitive guess is wrong in a way that matters. Per-harmonic gains,
dB relative to h1, median over all confidently-tracked frames, and the decay
fitted against log2 of harmonic number:

| Engine | Exhaust | Decay, dB per doubling of n | h24 re h1 |
| --- | --- | --- | --- |
| Stihl MS 150 C chainsaw | muffler, **no tuned pipe** | **−6.7** | −26.4 |
| Yamaha RX-100 | mild street expansion chamber | −3.6 | −22.3 |
| Tomos Colibri D-3 (1959) | racing expansion chamber | −3.2 | −8.8 |
| Tomos D-7 (1962) | racing expansion chamber | −2.7 | −9.3 |
| Tomos D-9 (1965) | racing expansion chamber | **−0.4** | **+4.9** |

That table is the headline result of this whole section, and the D-9 is both its
strongest case and the file with the 1.6% tracking jumps, so the two could have
been the same thing. They are not: re-fitting on only the frames more than five
frames away from any single-step f0 change above 3% — which throws away **79%** of
the D-9's frames, 2837 down to 607 — moves its slope from −0.4 to −0.1 and its h24
from +4.9 to +5.5. The other three move by 0.3 dB or less. The flat ladder is not
an artifact of the tracker.

**A racing two-stroke's harmonic stack is nearly flat.** The chainsaw — the one
engine here with an ordinary muffler — rolls off at 6.7 dB per doubling, which is
almost exactly the 1/n a synth gets by default and almost exactly what somebody
would invent. The three engines with tuned pipes roll off at 0.4 to 3.2 dB per
doubling and are still within 10 dB of the fundamental at the **twenty-fourth**
harmonic. Building a KZ on a 6 dB/octave stack would produce a chainsaw, and the
measurement says so literally.

The full ladders, dB re h1 (`WWS_Chainsaw` included as the negative control):

| h | Tomos D-7 | Tomos D-9 | Colibri | Yamaha | Chainsaw |
| --- | --- | --- | --- | --- | --- |
| 1 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| 2 | −1.2 | +3.5 | −2.6 | −9.2 | −7.0 |
| 3 | −0.8 | +5.5 | −0.6 | −7.3 | −5.4 |
| 4 | −2.7 | +5.5 | −2.9 | −9.2 | −2.9 |
| 5 | −0.6 | +9.5 | −3.4 | −7.3 | −5.1 |
| 6 | −1.0 | +10.3 | −0.4 | −12.0 | −14.8 |
| 8 | −1.7 | +9.6 | +0.7 | −13.1 | −15.3 |
| 10 | −3.4 | +9.3 | −2.3 | −5.5 | −21.2 |
| 12 | −5.1 | +7.5 | −2.8 | −8.3 | −22.8 |
| 16 | −6.4 | +6.2 | −9.0 | −14.6 | −21.8 |
| 20 | −8.1 | +5.9 | −10.0 | −19.7 | −23.2 |
| 24 | −9.3 | +4.9 | −8.8 | −22.3 | −26.4 |

The ladder is not smooth and should not be smoothed. The Yamaha's h10 sits 6.5 dB
above its h9 and the Colibri's h8 sits above its h7; those bumps are the exhaust
system's fixed transfer function crossing a moving harmonic, which is the next
measurement.

How far up the stack is worth synthesizing: partials stand 10 dB or more clear of
the inter-harmonic floor **out to h24 on every usable recording**, which is where
the analysis stopped rather than where the engine did. At a KZ's 14,000 rpm the
fundamental is 233 Hz and h24 is 5.6 kHz, so a stack truncated at h24 leaves the
top two octaves of the audible band to the noise layer.

### Exhaust resonance, and the comb delay

§12 asks for a comb filter, whose delay is a real physical quantity. An engine's
partials move with rpm; its exhaust system's transfer function does not, so
plotting every partial's level against its **absolute** frequency over a rev
sweep averages out everything that moves and leaves the acoustic path. The
periodic ripple in what remains is a comb, and a cepstrum along the frequency
axis recovers its delay.

**The trap is that a ground reflection produces exactly the same ripple**, and
attributing a microphone's height above the tarmac to an expansion chamber would
be fabricating a physical quantity. These are separable: a pipe is bolted to the
engine and its delay must be constant for the whole recording, while a reflection
path changes as the vehicle moves. Splitting each file into quarters:

| Recording | τ per quarter (ms) | Spread | Verdict |
| --- | --- | --- | --- |
| **Tomos D-9** (museum, stationary) | 1.40, 1.42, 1.42, 1.42 | **1%** | **engine-fixed** |
| Colibri D-3 (museum, stationary) | 2.46, 1.67, 1.73, 1.79 | 17% | drifts |
| Yamaha RX-100 (riding past) | 2.54, 1.35, 2.38, 1.54 | 26% | drifts |
| Tomos D-7 (museum, stationary) | 5.38, 5.29, 2.81, 2.79 | 31% | drifts |
| Stihl chainsaw (handheld) | 1.75, 1.65, 2.40, 2.52 | 19% | drifts |
| Eindhoven kartbaan (trackside) | 1.31, 2.27, 1.31, 1.27 | 27% | drifts |
| Scarborough (trackside) | 1.69, 4.79, 2.90, 4.67 | 37% | drifts |
| Patras (trackside) | 9.62, 2.42, 2.38, 3.67 | 66% | drifts |

**Exactly one recording of sixteen yields a defensible comb delay: the Tomos D-9
at τ = 1.42 ms, comb spacing 704 Hz**, peak-to-median significance 7.5–8.9 across
all four quarters. Every other file's apparent resonance is dominated by where the
microphone was standing and is not usable. The three trackside kart recordings are
the worst offenders, which is exactly what should be expected — the vehicle is
moving past the microphone, so its reflection geometry changes continuously, and
their cepstral significance (2.7–3.7) barely clears the noise anyway. **A comb
delay for a kart pipe therefore remains unmeasured even though kart recordings
were finally obtained.** The measured ripple depth, which
is the depth the comb would need, is **1.6–2.6 dB RMS** — a gentle filter, not the
deep metallic comb a first guess reaches for.

Backing a length out of τ = 1.42 ms requires the speed of sound **in exhaust
gas**, not in ambient air, and that is where this stops being measured:

- at 343 m/s (20 °C air): path difference 487 mm, half-wave resonator 243 mm
- at ~557 m/s (a plausible 500 °C in-pipe figure): path difference 791 mm,
  half-wave resonator **396 mm**

The second is the right order for a 50 cc racing expansion chamber's tuned
length, but **no dimensioned drawing of a Tomos D-9 pipe was found and no in-pipe
gas temperature was measured**, so "396 mm" is consistent-with, not established.
What *is* established and directly usable is τ itself: **1.42 ms on an engine
whose peak power is at 14,000 rpm**. A KZ's pipe is longer, so its τ is longer,
and the honest scaling is given under assumptions below.

### On throttle versus off throttle — issue #39

Issue #39's engine braking currently cannot be judged at all, because nothing
says how the note is supposed to *change*. None of these recordings carries a
throttle channel, so frames were split by the sign of df0/dt — nothing but a
closed throttle makes a free-revving engine lose speed — with a ±2%/s dead band
so that steady cruising pollutes neither group.

| Recording | On-throttle frames | Off-throttle | Broadband on − off | h1 over local floor, on → off |
| --- | --- | --- | --- | --- |
| Yamaha RX-100 | 651 | 320 | +1.1 dB | 19.3 → 9.2 dB (**−10.1**) |
| Tomos Colibri D-3 | 1403 | 1367 | **+4.6 dB** | 24.0 → 14.9 dB (**−9.1**) |
| Tomos D-7 | 876 | 1991 | **+4.3 dB** | 24.7 → 19.1 dB (−5.6) |

Two effects, and the second is the interesting one:

1. **Level.** Closing the throttle costs 4–5 dB broadband on the two recordings
   where the engine stays at a fixed distance from the microphone. The Yamaha's
   +1.1 dB is not a contradiction — the bike is riding away from the mic, so its
   level change is contaminated by distance and should not be used.
2. **The ladder tilts, it does not just drop.** On the Yamaha the fundamental
   loses 10.1 dB against its local floor off-throttle while h20–h24 lose only
   4–5 dB; the ON-minus-OFF ladder difference is −5.2 dB at h2 and around
   0 dB at h11. Off-throttle the note is therefore **relatively brighter and
   thinner**, not simply quieter. On the D-7 the tilt runs the other way at the
   low end (h2 and h3 are 3 dB *stronger* on throttle) while h5–h8 are 3–4 dB
   weaker. There is no single "load" scalar that reproduces both; §12's
   "per-harmonic gain envelopes shaped by load" is the right shape of model, and
   these three columns are the only measured constraint on it.

### The rev limiter

**No recording here catches one, and that is a real gap.** A hard limiter parks
f0 on a plateau and makes it ring; a rider shifting sweeps through the top of the
range and leaves. Measured as the fraction of usable time spent within 3% of each
recording's 99th-percentile f0, and the spread of f0 while there:

| Recording | Top of range | Time within 3% of it | Spread while there |
| --- | --- | --- | --- |
| Yamaha RX-100 | 8020 rpm | 5.1% | 1.39% |
| Tomos Colibri D-3 | 9639 rpm | 5.1% | 4.74% |
| Tomos D-7 | 13,278 rpm | 2.3% | 2.27% |
| Tomos D-9 | 16,892 rpm (unreliable, see above) | 1.5% | 2.92% |

Every one of these is a rider shifting. None shows the plateau-and-ring
signature. All four engines are also carbureted period racers and road bikes with
no electronic limiter to catch — a KZ's limiter is an ignition cut, which is a
different artifact entirely and is **not represented anywhere in this corpus**.

### What could not be sourced, and what stays assumed

1. **No recording identifying a specific KZ engine was found**, under any
   license. Searches for `TM Racing kart engine`, `Vortex ROK`, `Modena kart`,
   `KZ2`, `ICC kart`, `shifter kart engine`, `Superkart` and `gearbox kart` on
   Commons return zero audio or video; Openverse returns zero for `RS125`,
   `TZ125`, `GP125` and `expansion chamber`. The karting vendor sites that would
   have video (tkart.it, mondokart, iamekarting) return 403 to everything. The
   four two-stroke racing recordings above are identified by their spectra, not
   by their captions, and **no recording in this corpus is known to be a KZ**.
   Displacement, pipe dimensions, ignition timing and exhaust-port timing are all
   unknown for every one of them.

2. **No 125 cc racing two-stroke of known specification.** The Scarborough
   recording is captioned "125-400 cc practice two stroke" and so contains
   125 cc machines, but it is a trackside recording of a mixed field and no
   individual pass can be attributed to a displacement. The only 125 cc
   two-stroke in the corpus whose model is known is the Vespa PK 125 S, which has
   a touring silencer, idles at ~1700 rpm and never exceeds ~4900 rpm. **Its
   ladder is not used and its rows are marked scooter-derived wherever they
   appear.** Its h1 sits 2.4 dB over its own local noise floor, which makes every
   "dB re h1" figure derived from it meaningless — that is why it is excluded,
   not merely deprecated.

3. **The new recordings are all field recordings of moving vehicles**, which
   costs them something the museum recordings have. Distance brings air
   absorption and ground effect, both strongly frequency-dependent, so a ladder
   measured off a drive-by is the engine's spectrum *times an unknown transfer
   function that changes as the vehicle moves*. This shows in the Patras ladder,
   where h1 sits 13–17 dB below h2–h8: its h1 is healthy against its own local
   floor (15.3 dB clear), so the fundamental is really there and really weak,
   but how much of that is the pipe and how much is 50 m of air is not separable
   from a single microphone. **Use the Patras and Eindhoven figures for rev range
   and for the fact that the ladder stays flat; do not use them for absolute
   per-harmonic gains.** The Work With Sounds museum recordings, made at a fixed
   short distance from a stationary engine, are the ones whose ladder shapes are
   trustworthy — and they are 50 cc.

4. **Freesound needed no token after all**, which contradicts the assumption this
   work started from. Its `cdn.freesound.org/previews/…-hq.mp3` URLs are public
   and Openverse indexes them directly. The catch is that those are **lossy mp3
   previews**; the original WAV/FLAC does require an account. Everything taken
   from Freesound here is a preview, so its high-frequency content is
   codec-limited and the top of every ladder derived from one should be treated
   as a lower bound. Anything that ever ships should fetch the original.

5. **The comb delay for a KZ pipe is not measured, and must not be invented.**
   What is measured is τ = 1.42 ms for a 50 cc pipe tuned for 14,000 rpm. Tuned
   length scales roughly inversely with the tuned rpm and directly with the
   in-pipe wave speed, and a KZ's pipe is physically longer than a 50 cc pipe at
   a similar peak rpm, so its τ is **longer than 1.42 ms** — but by how much is
   unknown here. Until a KZ pipe is measured or a dimensioned drawing is found,
   the comb delay is a tunable with a measured lower bound and no upper one.

6. **No in-pipe gas temperature, so no defensible length.** The 396 mm above
   rests on assuming ~500 °C and hence ~557 m/s. Neither figure was measured or
   sourced. The delay τ is the honest parameter to carry into the synth; the
   length is decoration.

7. **The rpm/60 relation is verified on one engine, and its negative control
   failed.** The Yamaha's gearbox verification is solid. The matching test — that
   the four-stroke rental kart needs rpm/120 — could not be run, because the
   Wurstelprater recordings contain several karts at once and the tracker finds a
   spurious low common divisor across them. A single-engine four-stroke recording
   would close this and none was found.

8. **No rev-limiter spectrum, no gearshift transient, no clutch.** A KZ's
   ignition-cut limiter, the momentary unloading during a shift, and the
   centrifugal clutch's slip at low rpm are all audible signatures of a shifter
   kart, and none is measured or sourced here. The shift *ratios* are measured;
   what a shift sounds like is not.

9. **Nothing was measured about tire scrub or wind.** §12 specifies both as
   filtered noise driven by slip and speed. No recording in this corpus isolates
   either — every one of them has an engine running over the top of it. Those two
   remain entirely unsourced, and §12's claim that scrub "falls straight out of
   §6 for free" is about the *modulation*, not about the filter shape, which
   nobody has measured.

10. **Almost everything here is lossy, and all of it was analyzed as mono.** Ogg
   Vorbis for the Commons files, mp3 previews for the Freesound ones; only the
   D-4 and the extracted Patras audio are uncompressed, and the Scarborough
   recording is binaural stereo folded down to mono for analysis. The ladders
   above are therefore the spectrum of *a
   recording of* an engine — including its microphone, its codec, its distance
   and its ground reflection — and only the quantities explicitly separated from
   the recording geometry (the D-9's τ, and the ladder shapes, which are stable
   across quarters) survive that. Absolute levels do not, and none is quoted.

### The scripts

The analysis lives in the session scratchpad, not in the repository, because it
consumes audio that is deliberately not committed. `engine_analysis.py` holds the
STFT, the harmonic-sum tracker and the ladder; `f0_methods.py` the cepstrum, the
analytic-envelope autocorrelation, the spacing test and the synthetic generator;
`probe_f0.py` and `probe_resolution.py` the two probes with analytic answers;
`resonance.py` the fixed-frequency profile and the cepstral comb estimator;
`final_measure.py` the throttle split and the gearshift detector. If these numbers
are ever challenged, the probes are the place to start: they are the only part
that can be checked without re-downloading anything.

## Driving HUD — issue #73

### Photographs

| Ref | Subject | Source |
| --- | --- | --- |
| H1 | **AiM MyChron 5 face-on, screen legible at full size** — the primary reference | Flickr, [`16097842490`](https://www.flickr.com/photos/16097842490), CC BY-NC-SA 2.0 |
| H2 | The same unit, three-quarter left, showing the LED strip in relief | Flickr, [`16099072149`](https://www.flickr.com/photos/16099072149), CC BY-NC-SA 2.0 |
| H3 | Two units, one reversed, showing the connector block and battery | Flickr, [`16099349507`](https://www.flickr.com/photos/16099349507), CC BY-NC-SA 2.0 |
| H4 | MyChron 4 beside MyChron 5, for what changed between generations | Flickr, [`16351804111`](https://www.flickr.com/photos/16351804111), CC BY-NC-SA 2.0 |
| H5 | Kart cockpit from the driver's seat, steering wheel and front end | Wikimedia Commons, [`Kart Steering Wheel (8662349718).jpg`](https://commons.wikimedia.org/wiki/File:Kart_Steering_Wheel_(8662349718).jpg), CC BY 2.0, Ernest Duffoo |

**H1-H4 are NC and SA and are reference only.** Nothing is traced, redistributed
or shipped. What is taken from them is the information architecture — which
figure is largest, what sits beside what, how the tach is drawn — and a layout is
not a copyrightable work. The trade dress deliberately is not taken: no maker's
mark, no product wordmark, no copy of the bezel silhouette, and no buttons.

Discovered through the Openverse API, which indexes Flickr's CC pool directly.
Wikimedia Commons has essentially nothing on kart instrumentation — searches for
`MyChron`, `kart dashboard`, `kart tachometer` and `go-kart cockpit` return
Flat-Earth maps, Mario Kart wheel accessories and 1906 farming bulletins. H5 is
the only usable Commons result and it is context rather than instrument.

### What the references settled

`scripts/ui/driving_hud.gd` was first written from memory and was wrong in five
ways, every one of them visible within a second of opening H1. Recorded because
the same mistake has now been made three times in this project under three
different headings, and §5 item 10 exists because of it:

1. **It is a positive LCD.** Dark figures on a pale grey-green transflective
   ground, not glowing white on black. That is the largest single difference,
   and it is the reason the real instrument is readable in direct sunlight —
   which is the same problem a HUD over a bright track has.
2. **Gear is top-left and huge**, occupying roughly a third of the screen height,
   with `RPM` set **vertically** in three stacked capitals immediately right of
   it. The first version centered the gear.
3. **The tachometer is a comb of many thin uniform strokes** — around sixty —
   over a numeric scale marked every 2,000 rpm, with separate full-height strokes
   standing clear of the comb as thresholds. The first version drew a dozen
   chunky segments, which reads as a battery meter.
4. **Speed is medium and centered**, sitting directly under the tach with a small
   lowercase `km/h` beside it. The first version made it a hero figure at 95 px.
5. **The lower right is a 2x2 grid** of small values, each a caps label above a
   figure — on the real unit Lambda, Pedal, Exhaust and Water.

And one thing the reference has that no amount of thinking produced: **five
square shift LEDs above the screen plus two round alarm lamps at the outer
corners**, numbered 1 and 2. Those are two channels, not one. A shift strip says
"change gear" and an alarm lamp says "something is wrong", and collapsing them
into a single ten-LED sweep — which is what the first version did — throws away
the distinction. In this project they map exactly onto `engine.h`'s two states:
the ignition cut, which is the engine doing its job, and over-rev, which is
damage.

### Where this kart's instrument necessarily differs

The real unit's 2x2 grid is Lambda, Pedal, Exhaust and Water. This project models
none of those, and `ARCHITECTURE.md` §12 and issue #138 both say the number a
driver needs is g. So the same grid carries **LONG g, TIP %, PEAK g and SUST g**,
and the slot the real unit gives its largest figure to — the running lap time —
carries **LAT g** until M6 lands lap timing (#73's timing half). The lap counter
is drawn as an em dash rather than as zero, because a zero lap counter is a claim
and an em dash is an absence.

`TIP %` has no counterpart on the reference at all. It is lateral g as a fraction
of the threshold the kart would tip at **in the direction it is currently
turning**, and it is asymmetric for the reason `chassis.h` gives: 2.4336 g left
against 2.8061 right, because 27 kg of engine, exhaust and radiator hang off the
right side and put the center of mass 41 mm right of the centerline.

### What could not be sourced

1. **No photograph of a KZ-class dash in use, at speed, from the driver's seat.**
   H5 is a stationary kart photographed from behind the seat with no instrument
   fitted at all. Whether the layout below is actually legible at 100 km/h is
   therefore a judgement made at the wheel and not a sourced fact, which is
   exactly what #138 says the instruments exist to enable.
2. **No measurement of what a driver's eye actually does.** The claim that gear
   is the figure a kart driver looks at first is inferred from it being the
   largest thing on the reference unit's screen, which is an argument about the
   manufacturer's intent rather than a measurement of a driver.
3. **The segment typeface is an interpretation, not the reference's.** The real
   unit renders a squared bitmap font on a graphic LCD; this draws true
   seven-segment glyphs, which is a different thing that reads the same way at a
   glance. Drawn rather than typed on purpose: Godot's fallback font is a
   proportional humanist sans, and shipping a segment typeface would be an asset
   for ten glyphs.
