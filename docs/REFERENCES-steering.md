# Steering geometry — issue #35

> **Fragment.** Written by the M3b steering agent to be merged into
> `docs/REFERENCES.md` as a new section. It follows that file's rule: a reference
> is only listed once it has been *looked at*.

Everything in `src/core/steering.h` that is an angle, a length or a fraction is
sourced here, and the numbers that could **not** be sourced are listed too, with
what was assumed instead. `ARCHITECTURE.md` §5 item 10.

## Photographs

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

## Written sources

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

## What the references settled

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

## What could not be sourced

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
