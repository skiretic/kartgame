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
