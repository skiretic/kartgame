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
