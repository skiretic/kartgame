"""Issue #14, respecified by #190 — everything between the asphalt and the frame.

`docs/KART_SPEC.md` §20 is the design and it outranks every comment here. Its
scope is everything the kart rolls on plus everything that slows it down, and
before #190 this module built **twelve** of the seventy-six parts in it:

    built     4 rims, 4 tires, `axle_rear`, `axle_sprocket`, 2 stub axles
    absent    4 wheel hubs, 4 axle keys, 3 bearings, 3 cassettes, 2 kingpins,
              4 eccentric pills, 2 knuckles, 2 knuckle arms, 2 tie rods, 4 rod
              ends, and the **entire brake system** -- no disc, no caliper, no
              master cylinder, no line

Three of the absent parts exist **because a rule says so**, and all three are the
kind a modeler working from photographs leaves out: Art. 4.12.4's rear-disc
protective pad, Art. 4.12.2's redundant pedal-to-pump link, and Art. 4.14.1's
bead-retention pegs.

Art. **4.2.1**, PDF p. 7, is why the missing hubs, knuckles and kingpins were a
structural omission rather than a cosmetic one:

> The chassis main parts transmit the track forces to the chassis frame through
> the tyres. They include: the wheels with hubs; the rear axle; the steering
> knuckle; and the king pin.

Four items, and this module owns all four. Art. **4.2.2** then makes the
knuckle-on-kingpin the *only* articulation the regulations allow anywhere on a
kart, which is why it is the only rotating joint here besides the wheels.

**Brakes are free in Group 1.** Art. 8.6, PDF p. 21: *"Brakes are free in Group 1,
but must comply with Articles 4.12 et seq. of the TR."* Art. 9.6's four-wheel
requirement names **KZ2 / Group 2**, and a KZ is Group 1 -- so four brakes here is
an `estimated` design choice, taken because every KZ chassis is sold that way and
none has run rear-only in decades, and not a requirement. ADR-0054. What *is*
mandatory under 4.12 is the doubled pedal link and, on this kart, the disc pad.

Four things make the running gear read right, in order of what each costs:

1.  **The rears are fat and the fronts are not**, 215 against 135 at the Art.
    4.13.1 wheel ceiling, with the rears larger in diameter as well. Those four
    figures are frozen (issue #196) and are `estimated` rather than maxima -- the
    sourced Vega XH4 tire is 274 x 207 and 260 x 130.
2.  **The tread band is flat, and it is now 179 mm wide instead of 163.** A kart
    slick is a flat band with a soft shoulder rolling into a short, stiff
    sidewall. The band used to be `half_width - lean - shoulder`, i.e. whatever
    was left after a taste constant, and it came out 16 mm narrow at the rear and
    27 mm narrow at the front against the homologation forms.
3.  **One continuous rear axle**, and it is a **tube**. ARCHITECTURE.md §6 makes
    the locked rear axle the kart's defining dynamic, and Art. 4.3's wall-thickness
    table makes it hollow: 1.9 mm minimum at 50.0 mm OD, so `axle_diameter`'s old
    *"Solid, 50 mm"* was an error worth 15 kg on a kart with a 170 kg minimum.
4.  **The front track chain closes with two parts that were missing rather than
    with any number wrong.** Spec §20.3.3, and it decomposes without a remainder:

        232.5  spindle arm, forced by front_hub_x 552.5 and kingpin_x 320
        - 25   knuckle body half-width      -- was NOT MODELLED
        - 90   stub axle exposed run        -- `stub_axle_length`, built
        -117.5 front hub length             -- was NOT MODELLED
        ------
           0.0

    So `stub_axle_length` = 90 is a correct *visible spindle* length that was being
    used as the whole spindle arm. Its twin in `src/core/steering.h` still is, and
    fixing that is a solver change with its own ticket -- **do not do it here.**

**Pivots are interfaces, not labels.** The M3b vehicle solver drives a transform
per wheel and the M4 camera rig looks one up to frame the kart, both by name, so
`wheel_fl`, `wheel_fr`, `wheel_rl`, `wheel_rr` and `rear_axle` are published
exactly as spelled here.

**Nothing here is mirrored and nothing is rotated 180 degrees about Z.** Both are
tempting for a left-hand wheel and both are wrong: a mirror flips the winding and
a Z rotation flips the handedness of the local +X spin axis, after which two of the
four wheels rotate backwards for the same solver input. Kart slicks are
non-directional and the rim is deliberately symmetric about its own center plane,
so the identical, unrotated construction is correct on all four corners.

Coordinates: +X right, +Y forward, +Z up. The tires, rims, hubs, bearings and
discs are revolutions about X -- the kart's lateral axis -- because a revolution
gives exact control of the silhouette the eye actually reads.
"""

from __future__ import annotations

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

from . import build
from . import params as P


# --- tires and rims --------------------------------------------------------

TIRE_BEAD_INSET: float = 0.014
"""Axial distance from the tire's widest point in to where it closes on the rim
flange. Sets how far the flange lip stands out of the sidewall.

Spec §20.2.3 asks for this to be re-checked in the same pass as
`tire_sidewall_bulge`, and it was: raising the widest point from radius 71.5 mm to
101 mm shortens the sidewall run from 54 mm to 24 mm on the rear and steepens the
bead turn-in to a 38 mm radial drop over this 14 mm of axial travel. That is
roughly what a real slick's lower sidewall does -- the tire's overall width is
207 against 198 of rim, so the sidewall stands 4.5 mm proud of each flange and
there is only 4.5 mm of axial room for the turn-in to happen in anyway."""

TIRE_SIDEWALL_LEAN: float = 0.004
"""How far inboard of the widest point the sidewall's upper end sits, i.e. how
much the sidewall leans outward on its way down to the bulge."""

TIRE_SHOULDER_STEPS: int = 4
TIRE_SIDEWALL_STEPS: int = 3
TIRE_BEAD_STEPS: int = 3
"""Points along each part of the profile. Independent of `tire_segments`, which is
the resolution *around* the tire: the shoulder needs several points at any
circumferential density, and one chamfer segment reads as a toy at every one."""

RIM_FLANGE_WIDTH: float = 0.005
RIM_FLANGE_TAPER: float = 0.004
RIM_WALL: float = 0.005
RIM_SEAT_CLEARANCE: float = 0.0006
"""The bead seat is built a hair under `rim_bead_diameter/2` so the rim barrel and
the tire's bore are not two coincident surfaces. The tire hides the barrel either
way, but coincident faces are what makes issue #19's normal bake produce the
speckle it lists as a failure."""

RIM_PLATE_THICKNESS: float = 0.006
RIM_PLATE_BORE: float = 0.016
RIM_PLATE_DISH: float = 0.010
"""Depth of the wheel face's cone, per side. Symmetric, see the module docstring:
the plate sits at the center of a deep barrel and reads as dished from either side,
which is what lets one construction serve both sides of the kart.

**The rim no longer carries a hub boss.** `HUB_BOSS_*` and `HUB_SLEEVE_OVERLAP`
are gone: Art. 4.17 makes the hub a separate chassis main part (Art. 4.2.1) and
spec §20.3 needs the front one to be 117.5 mm long, so `hub_f?`/`hub_r?` are real
parts and the rim keeps only its mounting plate. The plate's bore is smaller than
either hub body, so the two overlap at a declared bolted joint rather than sharing
a surface."""

BEAD_PEG_COUNT: int = 3
BEAD_PEG_DIAMETER: float = 0.007
BEAD_PEG_PROJECTION: float = 0.005
"""Art. 4.14.1, PDF pp. 13-14: *"In Groups 1 & 2, the front and rear wheels must
have some form of bead retention with at least three pegs in the outside part of
the rim."* Mandatory on all four wheels and **absent from all four rims** before
#190.

Three per flange, on **both** flanges rather than only the outboard one. That is
not over-building: the module docstring makes the rim symmetric about its own
center plane on purpose, so that one unmirrored, unrotated construction is correct
on all four corners, and putting pegs on one side only would break that rule to
satisfy a clause that says *"at least three"*. Six per rim satisfies it whichever
way round the wheel is fitted.

The diameter and projection are `estimated`: the article requires the pegs and
sizes nothing, and 7 mm is what an M6 bead-lock screw's head measures."""


# --- the front end ---------------------------------------------------------

STUB_DIAMETER: float = 0.025
"""Front spindle. A KZ stub axle is a 17 mm bolt in a cast carrier; 25 mm is the
carrier, which is what is actually visible between the kingpin and the hub."""

STUB_BORE_ENTRY: float = 0.015
"""How far the spindle runs into the hub's bore past its inboard end. The visible
run is still `stub_axle_length` = 90 mm, from the knuckle's outboard face at 345 to
the hub's inboard end at 435; this is the part inside the bore that makes
`hub_f?`/`axle_stub_f?` a `pierced` joint rather than two parts kissing."""

KNUCKLE_HALF_WIDTH: float = 0.025
"""Kingpin axis to the knuckle's outboard face. `estimated`: the casting has to
house a 17 mm spindle boss and two kingpin bushes, and 25 mm is what the CRG plan
view's knuckle mask supports at 1.1236 mm/px. It is row 4 of spec §20.3.2's lateral
chain and one of the two links that were missing."""

KNUCKLE_HEIGHT: float = 0.105
KNUCKLE_DEPTH: float = 0.085
KNUCKLE_BOTTOM_Z: float = 0.098
"""The knuckle body. Height is `estimated` off the plan-view mask -- it has to span
the two kingpin bushes. The **bottom** is `derived` and is a clearance:
`frame.chassis_kingpin_boss_?` is a Ø40 boss whose top is at z 95, so 98 keeps the
casting 3 mm clear of it and the two parts meet only through the kingpin, which is
what Art. 4.2.2 describes.

The depth was 70 and is 85 for a measured reason: the front caliper's bracket has
to reach the knuckle from y +552 without passing inside the front hub's inboard
flange, and at 70 the knuckle stopped 4.5 mm short of it -- a declared joint that
could not touch.

**The body is built axis-aligned while the kingpin through it is not.** The pin
carries 18 degrees of caster and 11 degrees of inclination; the casting's *bushes*
carry those angles and its outer form does not, which is also what keeps the
outboard face at exactly 345 and the spindle arm chain closing at 0.0. Building
the box rotated would walk that face 28 mm inboard and reopen §20.3.3."""

KINGPIN_CASTER: float = 0.31416
"""18.0 degrees, kingpin top rearward. `sourced`, and downgraded to `estimated` by
spec §99 -- the figure comes from angle-gauge measurements of real chassis posted
to a forum (a 2012 Kosmic at 19.7 and 18.0 left/right, a Tony Kart at "18.something",
a stated cross-manufacturer range of 15-20 with "10 is unheard of"), read in full
via the forum's JSON API. Nobody in this project has a manufacturer drawing, so it
is measurements of real karts rather than a specification. `src/core/steering.h`
carries the same 18.0."""

KINGPIN_INCLINATION: float = 0.19199
"""11.0 degrees, kingpin top inboard. `estimated` -- *The NatSKA Guide to Karts
and Karting* via the Kartbuilding blog gives *"generally between 10 degrees and 12
degrees"* and 11 is the middle, which spec §99 downgraded from `sourced` because a
hobby blog quoting a club handbook is not a citation anybody here has read the
primary of.

It is the angle that makes **static camber zero**: the same source says the
inclination *"to allow the wheels to stand flat on the floor, is offset by a
similar angle on the stub axle"*, so the spindle is machined at the inclination and
the wheel stands vertical. That is a statement about this casting, and it is why
the spindle below is built horizontal rather than square to the pin -- square would
give the kart 11 degrees of positive camber."""

KINGPIN_PIVOT_Z: float = 0.100
"""Height at which the tilted kingpin axis crosses x = `kingpin_x`. `derived`, and
it is a fit rather than a choice: the frame's kingpin boss is a **vertical** Ø40
cylinder spanning z 35..95, so a pin tilted 21 degrees overall has to be pivoted
inside it or it exits the bore. At 100 the axis is 13.3 mm off the boss's own axis
at the boss's mid-height and 15.2 mm off at the spindle, both inside the 20 mm
bore. Pivoting at the spindle height instead puts it 28.4 mm off at the boss and
the pin misses the frame by 3.4 mm -- measured.

The boss being vertical is a frame fact this module has to live with; §Chassis owns
it and spec §10.5 does not carry an inclination for it."""

KINGPIN_DIAMETER: float = 0.010
KINGPIN_BOTTOM_Z: float = 0.030
KINGPIN_TOP_Z: float = 0.208
"""An M10 through-bolt is kart practice (`estimated`; nothing publishes it). The
length is `derived` from what it has to pass through rather than from spec §20.7's
95 mm, which assumed a 75 mm yoke: the built stack is the boss at z 35..95 plus the
knuckle at 98..203, so the pin spans 30..208 and is **178 mm**. A 95 mm pin on this
chassis would reach neither end of it."""

PILL_DIAMETER: float = 0.025
PILL_LENGTH: float = 0.016
PILL_OFFSET: float = 0.0015
"""The eccentric caster/camber adjusters, two per kingpin. Ø25 outer with the
Ø10 bore offset 1.5 mm, `estimated` -- the CRG Caster/Camber Chart gives three
indexed positions per pill (I / II / III) with **II top / II bottom** the factory
neutral setting and **no degrees at all**, so the positions are `sourced` and the
eccentricity is not. These four pieces are what *carry* the 18 degrees: III/III is
maximum caster, I/I minimum, and the whole roughly 3 degrees a setup screen would
expose lives in them."""

KNUCKLE_ARM_LENGTH: float = 0.108
KNUCKLE_ARM_SECTION: tuple[float, float] = (0.008, 0.030)
"""Kingpin to rod-end center, and the arm's plate section. `derived`: the CRG plan
view measures 96 px and 98 px at 1.1236 mm/px on the two sides independently, i.e.
108 and 110 mm.

**The arm points straight rearward, parallel to the centerline**, and that is
`sourced` as shape rather than chosen: both sides read their own kingpin flange's
lateral coordinate to within 1 px. It is *not* the true-Ackermann construction --
that needs the arm swept 16.95 degrees inboard at this kingpin spacing and a 240 mm
tie rod, and OTK sells 235 and 270. Spec §20.4.2: the kart's differential steer
comes from the column's 36 degree rake making the pitman a spatial linkage, and
`steering.h`'s `ackermann = 1.0` is an assumption in the vocabulary of a
construction. Its own ticket."""

TIEROD_DIAMETER: float = 0.012
TIEROD_END_DIAMETER: float = 0.022
TIEROD_END_LENGTH: float = 0.016
"""Ø12 aluminium track-rod tube, `estimated` -- not measurable at 1.12 mm/px
against a dark floor tray, and 12 is the usual size and consistent with the rods
reading 9-11 px. The rod end is an M8 rose joint with a Ø22 eye, `estimated`: Art.
4.5.3 explicitly permits *"rose joints on each end of the arm"* and sizes
nothing."""

PITMAN_EAR: tuple[float, float, float] = (0.050, 0.431, 0.160)
"""Where the inner rod end sits, right-hand side. The 50 mm lateral offset is
`sourced`: OTK's "38/50" designation means *"the centre distance of the holes of
the steering unibol from the centre of the column is 38/50 mm"* and a KZ runs the
outer hole. The y and z are `estimated` from the column's own geometry.

**`steering_pitman` does not exist**, because §Cockpit has not built it -- so the
inner rod ends are declared against `steering_column`, which is what a pitman is
clamped to, and waived at 51.7 mm in `joints.py` until §40 builds the plate. The
station itself is not a guess: rod end here to the outer end at
(320, 417, 140) measures **271 mm** against a sourced OTK *"STEERING TIE-ROD
270 mm"*, and a sourced part length agreeing with a measured geometry to 0.3% is
the strongest single result in spec §20."""

HUB_FRONT_BODY_RADIUS: float = 0.0225
HUB_FRONT_FLANGE_RADIUS: float = 0.038
HUB_FRONT_INBOARD_RADIUS: float = 0.024
HUB_FRONT_BORE_RADIUS: float = 0.0085
HUB_FRONT_INBOARD_WIDTH: float = 0.008
HUB_FRONT_FLANGE_WIDTH: float = 0.008
"""The front wheel hub. Its **length is 117.5 mm** -- `derived` as the residual of
spec §20.3.3's spindle arm, from an inboard end at x 435 to the rim's mounting plane
at `front_hub_x` = 552.5.

A real KZ front hub is 90-110, and the surplus is `track_front`'s own error, not
this part's: the CRG setup guide publishes front width as 44 to 46 inches
(1117.6-1168.4 mm) in three places, the frozen 1240 is 48.8 in, and at a sourced
45 in the same chain gives a 69 mm hub. Flag, do not change -- `chassis.h`'s
`FRONT_HALF_TRACK` and `steering.h`'s Ackermann input are both that number.

The three radii are `estimated`: the body has to house a bearing pair on a 17 mm
spindle and reads 45 mm across on the plan-view hub mask at 1.1236 mm/px; the
flange is a 3 x M8 bolt circle at Ø58 plus 9 mm of edge; the **inboard** flange is
Ø52 and carries the front disc's three drive tangs, sized to clear the caliper's
inner radial face at radius 31 by 5 mm."""

HUB_REAR_LENGTH: float = 0.090
HUB_REAR_BODY_RADIUS: float = 0.035
HUB_REAR_FLANGE_RADIUS: float = 0.038
HUB_REAR_FLANGE_WIDTH: float = 0.008
"""The rear wheel hub. 90 mm is `estimated` -- kart rear hubs are sold in lengths
and are how rear track is set, so length is the tunable, and the CRG guide's *"Rear
wheel hubs should be the shortest length (for minimum rear grip)"* is the
confirmation. 90 is mid-range.

At the old `axle_length` = 1.080 this hub would have been keyed over **37.5 mm** of
its 90 mm bore with Art. 4.3's keyway 20 mm from the axle's own chamfered end. At
1.185 the axle ends flush with the rim's mounting plane and the hub is keyed over
all 90."""


# --- the rear axle ---------------------------------------------------------

AXLE_KEY_SECTION: tuple[float, float] = (0.008, 0.004)
AXLE_KEY_LENGTH: float = 0.030
"""Art. 4.3: *"In KZ/KZ2, the rear axle must only have four keyways: one each for
the left and right hub, one for the brake disc and one for the rear axle
sprocket."* **Exactly four, and a fifth is not legal.**

8 x 4 mm is `estimated`, and the reasoning is the tag's justification: DIN 6885
would call for 14 x 9 with 5.5 mm of shaft depth on a 50 mm shaft, which
`axle_wall` = 2.5 mm cannot take. The tube is the reason the kart key is small."""

BEARING_OUTER_RADIUS: float = 0.040
BEARING_WIDTH: float = 0.016
CASSETTE_OUTER_RADIUS: float = 0.045
CASSETTE_WIDTH: float = 0.040
CASSETTE_WALL: float = 0.005
"""Three bearings and three cassettes, because a KZ carries a center bearing as
well as the outer pair and `frame.py` builds three hanger plates for them.

50 ID x 80 OD x 16 W is `estimated`: the 50 mm bore is forced by the axle, 6010 is
a real bearing of that section, and kart axle bearings are sold as self-aligning
units in this size class. No homologation form dimensions one. The cassette is
sized to house an 80 mm OD with a 5 mm wall.

**The cassettes are at x 0 and +-300, not +-185.** Spec §20.5 and §20.6.5 both
reason from hanger plates at +-185 and wave 1 moved them to `frame_half_rear` - 10
= +-300 so the weld into the rail measures 0 mm. Everything the left cassette's
outboard face fixes -- the rear disc's inboard limit above all -- moves with it."""

SPROCKET_DIAMETER: float = 0.145
"""Pitch diameter of the rear sprocket.

A KZ runs a large rear sprocket and it is very visible -- bottom edge inches off
the asphalt, outboard of the seat, in every chase-camera frame.

The figure is derived, not chosen. Karting uses #219 chain, where "219" is the
pitch in thousandths of an inch: 0.219 in, or 5.563 mm. A KZ runs roughly 78-84
teeth, so the pitch diameter is `pitch * teeth / pi` = 5.563 x 80 / pi = 142 mm.
Worth spelling out because the first version of this took the 219 for a diameter in
millimeters and then wrote it as 0.219 *meters* -- a 123-tooth sprocket, half again
too big, and close enough to plausible that it survived a render."""

SPROCKET_THICKNESS: float = 0.008
SPROCKET_HUB_RADIUS: float = 0.042
SPROCKET_HUB_HALF: float = 0.014
SPROCKET_X: float = 0.115
"""Sprocket center, on the kart's RIGHT, between the center and right bearings.

**It was on the left, and the reason given was wrong.** The comment here said "a
KZ drives the left rear", which cannot be a reason for anything: the rear axle is
one solid shaft with both wheels locked to it (ARCHITECTURE.md §6), so it drives
neither wheel preferentially and the sprocket's side is a packaging question. The
packaging answer is that the engine sits on the driver's right (`params.engine_x`),
so the chain has to reach a sprocket on the right -- a chain crossing under the seat
to the far side is not a thing any kart does. **The rear brake disc is what goes on
the left**, and Art. 4.3's four-keyway clause is the formal reason it has to: four
stations on one shaft, of which the disc and the sprocket are two, so they cannot
be coplanar. Measured separation 468 mm, on opposite sides of the center bearing."""


# --- the brake system ------------------------------------------------------
#
# The CRG **VEN BK-05-125** system, homologation form `82/FR/11`, adopted whole,
# because CRG Road Rebel is already this repo's primary chassis reference and the
# VEN system is what that chassis wears. Anything in 180-206 rear / 140-150 front
# is a real KZ, so the choice is a consistency argument and not an accuracy one.
# Cross-checked against Birel ART / FREE LINE RR `007-B4-69` and Birel Freeline
# FL RR EVO `007-BRKF-01`; **22 mm of master-cylinder bore is identical across all
# three forms and across a 2005 homologation and a 2024 one**, so treat it as
# settled.

DISC_REAR_DIAMETER: float = 0.195
DISC_REAR_THICKNESS: float = 0.0185
DISC_REAR_PAD_OUTER: float = 0.194
DISC_REAR_PAD_INNER: float = 0.136
"""Rear disc, `sourced`: `82/FR/11`, Ø195 +-1.5 and 18.5 +-1 new, pad rubbing
diameters 194 / 136. Birel runs 180 x 16 and OTK catalogs 206 for KZ, which is the
between-manufacturer spread rather than a disagreement.

*"Ventilated"* on these forms means drilled and slotted through a **single** plate,
not a two-plate vented rotor -- the drawing shows one plate, and Art. 4.12.3
permits exactly that and only as the manufacturer made it."""

DISC_REAR_X: float = -0.400
"""Rear disc friction plane, on the kart's **left**. `estimated`, and it moved.

Spec §20.6.5 places it at -260 +-25 by walking outboard from a left hanger plate at
-185: cassette outboard face at -205, then a ~55 mm carrier hub. Wave 1 moved the
hanger to **-300**, so the same walk gives a cassette face at -320 and a friction
plane at about -400. That is outside the spec's own +-25 band and it is the spec's
premise that changed, not its reasoning: *"the constraint is the bearing, not the
wheel"*, and the bearing moved 115 mm.

Checks that still hold. The left rear hub's inboard end is at -502.5, so there are
93 mm of spare axle. The disc's bottom edge is at z **50.0** -- `derived`,
147.5 - 97.5 -- which is dead level with the rails' centerline and 15 mm inside
their vertical extent, so **Art. 4.12.4's protective pad is mandatory** and not
marginally. And the sprocket at +115 is 515 mm away on the other side of the center
bearing."""

DISC_REAR_HUB_WIDTH: float = 0.055
DISC_REAR_HUB_RADIUS: float = 0.038
DISC_REAR_CARRIER_RADIUS: float = 0.050
DISC_REAR_CARRIER_THICKNESS: float = 0.014
DISC_REAR_BOBBIN_COUNT: int = 6
DISC_REAR_BOBBIN_RADIUS: float = 0.005
DISC_REAR_BOBBIN_STATION: float = 0.046
DISC_REAR_RING_INNER: float = 0.042
"""Floating two-piece construction, `sourced` as shape off `007-B4-69` p. 2's
exploded CAD at 170 dpi: a lobed star carrier whose bore clamps an axle-mounted
hub, and **6 floating bobbins on a bolt circle** so the friction ring can expand.
One bobbin position carries the Art. 4.12.3 homologation-number boss.

The hub is ~55 mm wide and clamps the 50 mm axle -- OTK sells it as *"MG DISK'S HUB
D.50mm FOR BRAKE"*."""

DISC_FRONT_DIAMETER: float = 0.150
DISC_FRONT_THICKNESS: float = 0.012
DISC_FRONT_PAD_OUTER: float = 0.149
DISC_FRONT_PAD_INNER: float = 0.092
DISC_FRONT_X: float = 0.445
DISC_FRONT_TANG_COUNT: int = 3
"""Front discs, `sourced`: `82/FR/11` and `007-B4-69` agree on Ø150 +-1.5 x 12 +-1.
One piece, no floating carrier, six curved slots, two rings of drilled holes, and
**3 integral drive tangs at 120 degrees** on the inner bore that bolt to the front
hub.

The plane at +-445 is `derived` and it is spec §20.6.6's own resolution of an
interference the measurement pass found in itself. That pass boxed the disc at +-480
between a rim inner flange at 495 and a kingpin at +-465, then reported that *"with
a 66 mm body centered at 480 it reaches 513 and fouls"*. **§20.3 dissolves the
box**: the +-465 was `stub_axle_length` used as the spindle arm, and with the
kingpin at +-320 there are 172.5 mm of clear spindle. Moving the *disc* 35 mm
inboard and keeping the caliper symmetric about it is the physical answer -- an
opposed-piston caliper has a piston on each side and cannot be offset 23 mm without
the outboard half becoming 4 mm of aluminium."""

CALIPER_REAR_LENGTH: float = 0.138
CALIPER_REAR_HEIGHT: float = 0.055
CALIPER_REAR_THICKNESS: float = 0.074
CALIPER_REAR_CLOCK: float = 0.34907
CALIPER_FRONT_LENGTH: float = 0.103
CALIPER_FRONT_HEIGHT: float = 0.062
CALIPER_FRONT_THICKNESS: float = 0.066
CALIPER_FRONT_CLOCK: float = 1.30900
"""Caliper envelopes, all `estimated` at a stated 5% and measured off `007-B4-69`
p. 2 inside a single orthographic projection: the front disc's sourced Ø150 spans
200 px -> 0.750 mm/px and the rear's sourced Ø180 spans 252 px -> 0.714 mm/px, and
**the two scales agree to 4.8%** derived from two different parts of one drawing.
That agreement is the evidence the exploded view is drawn to a single scale, and 5%
is the honest error bar. The through-thickness is not in the drawing at all and is
built up from the parts: rear 18.5 disc + 2 x 9 pad + 2 x 19 cylinder wall = 74.

Clock angles are `estimated` at +-15 degrees. The rear sits near the **top** of the
disc on a bracket bolted to the bearing cassette, tipped slightly forward of
vertical -- 20 degrees is the middle of what `crg_roadrebel_kz_detail11.webp` and
`tonykart_racer401T_p03.jpg` support. The front sits on the upright **ahead of** the
kingpin and roughly level with the axle, so 75 degrees forward of top.

Shape, from the drawing and `tonykart_racer401T_p03.jpg`: an opposed-piston
one-piece aluminium body with a waisted outline, externally finned across the top
for cooling, a banjo on each half and a bleed nipple. Not a sliding caliper."""

PAD_REAR_LENGTH: float = 0.058
PAD_FRONT_LENGTH: float = 0.038
PAD_FRONT_HEIGHT: float = 0.025
PAD_THICKNESS: float = 0.009
"""**2 pistons at 32 mm rear, 4 pistons at 26 mm front**, 2 pads per wheel either
way, all `sourced` off `82/FR/11`. Clamp area per wheel, `derived`: front
4 x pi x 13^2 = **2124 mm2**, rear 2 x pi x 16^2 = **1608 mm2**.

Birel splits the piston count the opposite way -- 2 front / 4 rear -- and
compensates with bore, landing at front 982 / rear 1963. So the two makers put the
front-to-rear clamp ratio on **opposite sides of 1.0**, which is exactly what the
balance regulator exists to trim and is worth knowing before anyone hangs a
brake-bias tunable off this."""

MASTER_BORE: float = 0.022
MASTER_BODY_DIAMETER: float = 0.032
MASTER_BODY_LENGTH: float = 0.130
MASTER_TOWER_DIAMETER: float = 0.028
MASTER_TOWER_TOP_Z: float = 0.180
MASTER_Y: float = 0.490
MASTER_Z: float = 0.101
MASTER_FRONT_X: float = -0.192
MASTER_REAR_X: float = -0.155
"""Two master cylinders on one bracket, on the kart's left at the pedal.

**22 mm of bore is the one figure identical across all three homologation forms**
and across nineteen years of them, so it is `sourced` and settled. The body is
~130 x ~110 x ~40 including its reservoir, `estimated` at 180 px x 0.72 mm/px on
`007-B4-69` p. 2 -- a different region of the drawing from the calipers, so scale
confidence is lower than the calipers'.

**CRG-style, not OTK-style**, and recording the rejection matters: OTK sells
*"BRAKE PUMP'S CONTROL ROD 490MM"* and a 525, which is a layout with the pumps back
near the seat, while `crg_roadrebel_kz_detail7.webp` shows **both red reservoir caps
right at the pedal bracket**. So the sourced 490/525 rods do not apply here and the
link is a short clevis rod -- dropping a 490 mm rod into a CRG front end would put
the cylinders behind the seat.

Art. 4.4 constrains only the **ordering**: *"The brake pedal must be placed in
front of the master cylinder."* The pedal's plate reaches y +550 and the bodies end
at +535, so it is satisfied by 15 mm and the fore-aft position is otherwise free.

The front cylinder is the **inboard** one at -148 and the rear the outboard one at
-185. That is not arbitrary: the front circuit's line arrives from the distributor
on the bracket's inboard upstand and the rear circuit's leaves rearward and
outboard, so this ordering is the one where neither hose has to cross the other
cylinder. 37 mm apart rather than spec §20.6.4's 45, because `pedal_mount_l` is a
plate on edge at x -120..-130 and a Ø28 reservoir tower at -134 clears it by 4 mm."""

MASTER_BRACKET_SIZE: tuple[float, float, float] = (0.090, 0.090, 0.004)
MASTER_BRACKET_X: float = -0.170
PEDAL_FACE_TILT: float = 0.260
MASTER_BRACKET_UPSTAND_X: float = -0.215
DISTRIBUTOR_X: float = -0.223
DISTRIBUTOR_SIZE: tuple[float, float, float] = (0.024, 0.030, 0.030)
REGULATOR_Y: float = -0.150
REGULATOR_X: float = -0.295
REGULATOR_RADIUS: float = 0.012
REGULATOR_LENGTH: float = 0.060
"""The bracket, the distributor and the balance regulator.

The bracket is a 4 mm plate on the floor tray with an inboard upstand, and **it is
on the tray rather than welded to `chassis_cross_front`** as spec §20.6.4 has it:
that member is a U-loop whose legs are at y +706 at this x, i.e. 236 mm forward of
where the cylinders have to be, so a bracket welded to it would be a plate in mid
air. Same class of miss as the 104.65 mm pedal-mount waiver. `estimated`.

The regulator is on both homologation forms and neither places it; it sits on the
left rail's straight run where a seated driver can reach it. The **distributor is
Birel-only** -- `007-B4-69` item `10.10659.00`, and the CRG form does not list one --
so it is a Birel feature grafted onto a CRG layout and is the weakest-sourced part
in this section. Recorded rather than quietly dropped."""

PUSHROD_DIAMETER: float = 0.008
PUSHROD_LINK_DIAMETER: float = 0.002
PUSHROD_Z: float = 0.112
PUSHROD_LINK_Z: float = 0.106
"""**Art. 4.12.2, and this part exists because a rule says so.** PDF p. 12:

> The brake control, i.e. the link between the pedal and the pump(s), must be
> doubled for safety and always be in conformity with the HF of the chassis it is
> homologated with. If a cable is homologated, it must have a minimum diameter of
> 1.8 mm.

This is a **mechanical redundancy** rule and not a two-circuit rule -- it is about
the pedal-to-pump link. The CRG Road Rebel form `04-CH-14` devotes its whole page 4
to it: *"PHOTO OF BRAKE CONTROL CABLE / The brake control must be separated from
chassis and show the double linkage"*. A photographed, homologated feature, and one
of the three parts here most likely to be left out.

**2.0 mm, because 1.8 is a floor and not a practice.** Writing it as "1.8 mm max"
would be the front matter's second defect exactly.

Both run at z 79 and 73, in the 18 mm channel between the floor tray's top surface
at 69 and `pedal_mount_?`'s bottom at 87. That channel is `derived` and it is the
only clear route: at pad height the rod passes through the mount plate."""

LINE_RADIUS: float = 0.003
"""Braided steel hose with banjo ends, `sourced` construction and `estimated`
route. The rear is a **single run**; the front is a **tee'd assembly** feeding both
calipers from one pump -- `007-B4-69` item 9, *"BRAKE FRONT TUBE ASSY."*, drawn as a
tee with two equal branches.

Routing from `col_crg_form_planview_1417.jpg` and `crg_roadrebel_kz_detail7.webp`:
hoses are **cable-tied along the upper surface of the chassis tubes**, the front
pair crossing the front of the chassis and turning outboard to each upright, the
rear run going back along the left rail. **Nothing crosses under the floor tray**,
which is the rule that keeps the route out of Art. 4.12.6's territory and off the
skid plates -- and it is why both runs sit a few millimeters above a tube rather
than beside it."""

PROTECTOR_SIZE: tuple[float, float, float] = (0.130, 0.080, 0.010)
PROTECTOR_TOP_Z: float = 0.045
"""**Art. 4.12.4, and this part exists because a rule says so.** PDF p. 12:

> An efficient rear brake disc protective pad (in nylon, carbon fibre, Teflon,
> Kevlar, Delrin or equivalent hard plastic) is mandatory in Groups 1, 2 & 3 if the
> brake disc protrudes below or is level with the main chassis frame tubes nearest
> to the ground. This protection must be placed laterally in relation to the disc,
> in the longitudinal axis of the chassis or under the disc.

`derived`: `rail_z` is 50 and `tube_main` is 30, so the rails occupy z 35..65, and a
Ø195 disc concentric with the rear axle at z 147.5 has its bottom edge at exactly
**50.0**. Level with the rails' centerline and 15 mm inside their vertical extent,
so *"level with"* is satisfied and the pad is mandatory. At the sourced Ø274 tire it
would be 39.5, still level.

The *"under the disc"* option is taken. Its underside is at 35, level with the
rails' lowest point, so it is the thing that grounds first -- which is the entire
point of the article. Second of the three rule-mandated parts."""


# --- corners ---------------------------------------------------------------

#: The four wheels, in a fixed order: front left, front right, rear left, rear
#: right. A tuple rather than a dict because the build order sets the object
#: creation order, which sets the exporter's node order, which sets the hash.
_CORNERS: tuple[tuple[str, float, bool], ...] = (
    ("fl", -1.0, False),
    ("fr", 1.0, False),
    ("rl", -1.0, True),
    ("rr", 1.0, True),
)


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("wheels")

    for corner, side, is_rear in _CORNERS:
        _wheel(context, collection, corner, side, is_rear)

    _rear_axle(context, collection)
    _rear_bearings(context, collection)
    _rear_hubs(context, collection)
    _front_uprights(context, collection)
    _front_hubs(context, collection)
    _steering_linkage(context, collection)
    _rear_brake(context, collection)
    _front_brakes(context, collection)
    _brake_hydraulics(context, collection)


# --- one wheel -------------------------------------------------------------


def _wheel(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    corner: str,
    side: float,
    is_rear: bool,
) -> None:
    """Pivot, tire and rim for one corner.

    The pivot is the interface; the two meshes are parented under it with their
    geometry centered on their own origins, so each mesh's exported node transform
    is the identity and the only transform in the chain is the one the solver
    drives.
    """
    p = context.params

    if is_rear:
        diameter = p.tire_rear_diameter
        width = p.tire_rear_width
        tread = p.tire_rear_tread_width
        rim_width = p.rim_rear_width
        hub_x = P.rear_hub_x(p)
        axle_y = P.rear_axle_y(p)
        axle_z = P.rear_axle_z(p)
    else:
        diameter = p.tire_front_diameter
        width = p.tire_front_width
        tread = p.tire_front_tread_width
        rim_width = p.rim_front_width
        hub_x = P.front_hub_x(p)
        axle_y = P.front_axle_y(p)
        axle_z = P.front_axle_z(p)

    center = (side * hub_x, axle_y, axle_z)

    name = "wheel_%s" % corner
    hub = build.empty(name, center, collection, size=0.06)
    context.publish(name, hub)
    # `build.set_parent` reads `parent.matrix_world`, and for an object created
    # this tick that is still the identity until the depsgraph evaluates it -- the
    # tire and rim would land at the kart's origin, one hub offset out. One
    # evaluation here rather than a hand-built parent inverse, so the parenting
    # stays the one in build.py that every module uses.
    bpy.context.view_layer.update()

    bm = bmesh.new()
    build.lathe(
        bm,
        _tire_profile(p, diameter, width, tread),
        context.detail.tire_segments,
        axis="X",
        # Closes the last profile ring back onto the first, which is the tire's
        # bore: the surface that sits on the rim. Without it the tire is an open
        # shell with a boundary edge ring inside each bead.
        close_profile=True,
    )
    tire = build.object_from_bmesh(
        "%s_tire" % name,
        bm,
        collection,
        material=context.material("tire_rubber"),
        shade_smooth=True,
    )
    # No bevel: every edge on a revolved tire is either tangent to its neighbors,
    # where a bevel does nothing, or the bead corner buried inside the rim.
    tire.location = center
    build.set_parent(tire, hub)

    bm = bmesh.new()
    _rim(context, bm, rim_width)
    rim = build.object_from_bmesh(
        "%s_rim" % name,
        bm,
        collection,
        material=context.material("rim_magnesium"),
        shade_smooth=True,
    )
    # Bevelled on the high-poly bake source only. On the low-poly the bevel doubled
    # each rim from 1,152 to 2,176 triangles -- 61% of the whole kart for four
    # flange lips -- and issue #19's normal bake exists precisely so that shading
    # detail does not have to be paid for in geometry.
    if context.detail.is_high:
        build.bevel_object(rim, context.detail)
    rim.location = center
    build.set_parent(rim, hub)


# --- tire profile ----------------------------------------------------------


def _tire_profile(
    p: P.KartParams, diameter: float, width: float, tread: float
) -> list[tuple[float, float]]:
    """(radius, x) pairs for one tire, revolved about X by `build.lathe`.

    Built as one half and mirrored, so the tire is symmetric about its own center
    plane by construction rather than by two lists agreeing. Symmetry is what makes
    the same mesh correct on both sides of the kart without a mirror or a 180 degree
    rotation -- see the module docstring for why that matters.

    The half runs from the edge of the flat tread band outward and down:

        tread edge -> shoulder arc -> sidewall -> bead turn-in -> rim seat

    **The tread band's width is now authored**, `tire_*_tread_width` off the
    homologation forms, and the shoulder is fitted between it and the sidewall. It
    used to be `half_width - lean - shoulder`, i.e. the residue after a taste
    constant, and it measured 163 mm at the rear against a sourced 179 and 83 mm at
    the front against 110. The front was worse because the same 22 mm shoulder is a
    bigger fraction of a narrower tire.

    Point order is what sets the surface orientation. `build.lathe` winds a ring
    pair so that a profile advancing in +x on the outward-facing side gives outward
    normals, so the fold at the widest point -- where x stops increasing and turns
    back inboard toward the bead -- is what makes the turn-in face outboard rather
    than inside out.
    """
    tread_radius = diameter * 0.5
    half_width = width * 0.5
    bead_radius = p.rim_bead_diameter * 0.5
    bulge_radius = bead_radius + p.tire_sidewall_bulge

    tread_x = tread * 0.5
    wall_x = half_width - TIRE_SIDEWALL_LEAN
    # The shoulder is what bridges the authored tread edge and the sidewall's upper
    # end, so it is the *gap between two sourced widths* and only falls back on
    # `tire_shoulder_radius` when that gap is wider than a kart slick's shoulder.
    # Clamped positive so a sweep onto a narrow tread cannot fold the profile
    # through its own center plane.
    shoulder = min(p.tire_shoulder_radius, max(0.001, wall_x - tread_x))
    shoulder_top = tread_radius - shoulder

    half: list[tuple[float, float]] = [(tread_radius, wall_x - shoulder)]

    # Shoulder: a quarter arc, tangent to the flat tread where it starts and purely
    # radial where it ends, so neither joint creases.
    for step in range(1, TIRE_SHOULDER_STEPS + 1):
        angle = 0.5 * math.pi * step / TIRE_SHOULDER_STEPS
        half.append(
            (
                shoulder_top + shoulder * math.cos(angle),
                wall_x - shoulder + shoulder * math.sin(angle),
            )
        )

    # Sidewall: down to the bulge, leaning outward on the way. Eased at both ends so
    # it leaves the shoulder radially and arrives at the widest point with no axial
    # slope left, which is what makes the bulge read as round.
    for step in range(1, TIRE_SIDEWALL_STEPS + 1):
        fraction = step / TIRE_SIDEWALL_STEPS
        ease = fraction * fraction * (3.0 - 2.0 * fraction)
        half.append(
            (
                shoulder_top + (bulge_radius - shoulder_top) * fraction,
                wall_x + TIRE_SIDEWALL_LEAN * ease,
            )
        )

    # Bead turn-in: an elliptical quarter from the widest point back in and down
    # onto the bead seat, tangent to the sidewall at the start and to the rim flange
    # face at the end.
    for step in range(1, TIRE_BEAD_STEPS + 1):
        angle = 0.5 * math.pi * step / TIRE_BEAD_STEPS
        half.append(
            (
                bulge_radius - (bulge_radius - bead_radius) * math.sin(angle),
                half_width - TIRE_BEAD_INSET * (1.0 - math.cos(angle)),
            )
        )

    mirrored = [(radius, -along) for radius, along in reversed(half)]
    return mirrored + half


# --- rim -------------------------------------------------------------------


def _rim(context: build.BuildContext, bm: bmesh.types.BMesh, rim_width: float) -> None:
    """Barrel with a flange at each end, a dished face plate, and bead pegs.

    Three revolutions plus six pegs into one mesh rather than several objects: they
    are one cast part on a kart, they take one material, and splitting them would
    only add nodes for the exporter to order.

    Each profile is a closed outline with real wall thickness. A rim modeled as a
    zero-thickness shell folds back on itself at the flange, and a lip with no
    thickness catches no highlight -- which is exactly the edge that has to read.

    **The width is now `rim_*_width` off the tire homologation forms** rather than
    derived from the tire's bead inset, which was a shoulder-radius accident: 198 mm
    at the rear and 120 at the front, flange to flange.
    """
    p = context.params
    segments = context.detail.tire_segments
    half_width = rim_width * 0.5

    build.lathe(bm, _rim_barrel_profile(p, half_width), segments, axis="X",
                close_profile=True)
    build.lathe(bm, _rim_plate_profile(p), segments, axis="X", close_profile=True)
    _bead_pegs(context, bm, p, half_width)


def _rim_flange_lip(p: P.KartParams) -> float:
    """How far the flange stands proud of the bead seat. `derived`, Art. 4.14.

    `(rim_flange_diameter - rim_bead_diameter) / 2` = **5.0 mm**, and it used to be
    an authored 6.0. The old build was legal by accident: 6 put the lip at radius
    69.44, i.e. Ø138.9 against the article's 136.2 minimum, so
    `refs/kart-visual/notes_running.md`'s claim that a flange drawn at 127 is 9 mm
    undersize was measured false. The real defect was the other way round and
    smaller -- the **bead seat** at Ø126.9 against a 126.2 +0/-1 fit, 0.7 mm over.
    """
    return (p.rim_flange_diameter - p.rim_bead_diameter) * 0.5


def _rim_barrel_profile(p: P.KartParams, half_width: float) -> list[tuple[float, float]]:
    """Outline of the barrel: out over the flange, along the seat, and back."""
    seat = p.rim_bead_diameter * 0.5 - RIM_SEAT_CLEARANCE
    lip = seat + _rim_flange_lip(p)
    bore = seat - RIM_WALL
    inner = half_width - RIM_FLANGE_WIDTH - RIM_FLANGE_TAPER
    return [
        (lip, -half_width),
        (lip, -half_width + RIM_FLANGE_WIDTH),
        (seat, -inner),
        (seat, inner),
        (lip, half_width - RIM_FLANGE_WIDTH),
        (lip, half_width),
        (bore, half_width),
        (bore, -half_width),
    ]


def _rim_plate_profile(p: P.KartParams) -> list[tuple[float, float]]:
    """Outline of the wheel face: a shallow dish each side of the center bore.

    The intermediate point is what makes it a dish rather than a straight cone --
    the face flattens toward the flange and steepens toward the hub, which is how a
    cast wheel face is actually shaped.
    """
    seat = p.rim_bead_diameter * 0.5 - RIM_SEAT_CLEARANCE
    # Sunk half a wall into the barrel, so the two revolutions overlap instead of
    # leaving a hairline gap for the light to come through.
    edge = seat - RIM_WALL * 0.5
    return [
        (RIM_PLATE_BORE, -RIM_PLATE_DISH),
        (edge * 0.45, -RIM_PLATE_DISH * 0.42),
        (edge, -RIM_PLATE_THICKNESS * 0.5),
        (edge, RIM_PLATE_THICKNESS * 0.5),
        (edge * 0.45, RIM_PLATE_DISH * 0.42),
        (RIM_PLATE_BORE, RIM_PLATE_DISH),
    ]


def _bead_pegs(
    context: build.BuildContext,
    bm: bmesh.types.BMesh,
    p: P.KartParams,
    half_width: float,
) -> None:
    """Art. 4.14.1's bead retention, three pegs per flange on both flanges."""
    seat = p.rim_bead_diameter * 0.5 - RIM_SEAT_CLEARANCE
    radius = seat + _rim_flange_lip(p) * 0.5
    for side in (-1.0, 1.0):
        for index in range(BEAD_PEG_COUNT):
            angle = 2.0 * math.pi * index / BEAD_PEG_COUNT
            offset_y = radius * math.cos(angle)
            offset_z = radius * math.sin(angle)
            build.sweep_tube(
                bm,
                [
                    (side * (half_width - 0.002), offset_y, offset_z),
                    (side * (half_width + BEAD_PEG_PROJECTION), offset_y, offset_z),
                ],
                BEAD_PEG_DIAMETER * 0.5,
                max(6, context.detail.tube_segments // 2),
            )


# --- rear axle and sprocket ------------------------------------------------


def _axle_profile(p: P.KartParams) -> list[tuple[float, float]]:
    """The axle as a **tube**, with a chamfer at each end.

    Art. 4.3, PDF pp. 8-9: *"Each rear axle must have, on the inside and outside, a
    rounded edge or a chamfer with a maximum diameter corresponding to the axle
    thickness. The chamfer must not have sharp edges."* So the chamfer is
    `axle_wall` and not a free choice, and it is the only detail on the axle a
    camera ever sees.
    """
    half = p.axle_length * 0.5
    outer = p.axle_diameter * 0.5
    bore = outer - p.axle_wall
    chamfer = p.axle_wall
    # Outer surface first and in +along order, which is `_sleeve_profile`'s
    # orientation: `build.lathe` winds a ring pair so that the outward-facing run
    # has to advance along the axis, and starting on the bore instead gives a mesh
    # with a signed volume of -0.000421 m3 that no render shows.
    return [
        (outer - chamfer, -half),
        (outer, -half + chamfer),
        (outer, half - chamfer),
        (outer - chamfer, half),
        (bore, half),
        (bore, -half),
    ]


def _rear_axle(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """One continuous 50 mm tube, its four keys, and the sprocket.

    Published as `rear_axle` and given its own pivot, so the axle and sprocket can
    be spun as one assembly. The rear wheels are deliberately *not* parented to it:
    the solver drives all four wheel transforms uniformly in M3b, and hanging two of
    them off a different node would make the rear pair the special case in code that
    has no reason to have one.
    """
    p = context.params
    axle_y = P.rear_axle_y(p)
    axle_z = P.rear_axle_z(p)
    center = (0.0, axle_y, axle_z)
    material = context.material("axle_steel")

    pivot = build.empty("rear_axle", center, collection, size=0.08)
    context.publish("rear_axle", pivot)
    bpy.context.view_layer.update()  # see the note in `_wheel`

    bm = bmesh.new()
    build.lathe(
        bm, _axle_profile(p), context.detail.tube_segments, axis="X", close_profile=True
    )
    axle = build.object_from_bmesh(
        "axle_rear", bm, collection, material=material, shade_smooth=True
    )
    axle.location = center
    build.set_parent(axle, pivot)

    # Art. 4.3's four keyways, and exactly four. The disc's station is the one that
    # moved: the spec puts it at -260 against a hanger plate at -185, and wave 1's
    # hanger at -300 pushes the whole disc assembly out to -400.
    key_width, key_depth = AXLE_KEY_SECTION
    stations = (
        ("hub_l", -P.rear_hub_x(p) + HUB_REAR_LENGTH * 0.5),
        ("disc", DISC_REAR_X + 0.0225),
        ("sprocket", SPROCKET_X),
        ("hub_r", P.rear_hub_x(p) - HUB_REAR_LENGTH * 0.5),
    )
    for label, station in stations:
        bm = bmesh.new()
        build.box(
            bm,
            (AXLE_KEY_LENGTH, key_width, key_depth),
            (station, axle_y, axle_z + p.axle_diameter * 0.5),
        )
        key = build.object_from_bmesh(
            "axle_key_%s" % label, bm, collection, material=material
        )
        build.set_parent(key, pivot)

    bm = bmesh.new()
    build.lathe(
        bm,
        _cylinder_profile(SPROCKET_DIAMETER * 0.5, SPROCKET_THICKNESS * 0.5),
        context.detail.tire_segments,
        axis="X",
    )
    build.lathe(
        bm,
        _cylinder_profile(SPROCKET_HUB_RADIUS, SPROCKET_HUB_HALF),
        context.detail.tire_segments,
        axis="X",
    )
    sprocket = build.object_from_bmesh(
        "axle_sprocket", bm, collection, material=material, shade_smooth=True
    )
    # No teeth: they cannot come out of a revolution, and 100-odd of them modeled
    # would cost more triangles than the rest of the kart. Issue #19's bake.
    sprocket.location = (SPROCKET_X, axle_y, axle_z)
    build.set_parent(sprocket, pivot)


def _cylinder_profile(radius: float, half_length: float) -> list[tuple[float, float]]:
    """A capped solid cylinder about the lathe axis, as (radius, along) pairs.

    The caps are triangle fans, so this is only for parts that are not beveled.
    """
    return [
        (0.0, -half_length),
        (radius, -half_length),
        (radius, half_length),
        (0.0, half_length),
    ]


def _sleeve_profile(
    radius: float, bore: float, half_length: float
) -> list[tuple[float, float]]:
    """A thick-walled tube, as a closed outline for `close_profile=True`.

    Annular caps rather than fans, so it survives being beveled.
    """
    return [
        (radius, -half_length),
        (radius, half_length),
        (bore, half_length),
        (bore, -half_length),
    ]


def _annulus_profile(
    outer: float, inner: float, half_thickness: float
) -> list[tuple[float, float]]:
    """A flat washer -- a disc with a hole, closed so it has real thickness."""
    return [
        (inner, -half_thickness),
        (outer, -half_thickness),
        (outer, half_thickness),
        (inner, half_thickness),
    ]


# --- rear bearings, cassettes and hubs -------------------------------------


def _bearing_stations(p: P.KartParams) -> tuple[tuple[str, float], ...]:
    """Where the three hanger plates are, read off the same parameters `frame.py`
    builds them from: `frame_half_rear` less the 10 mm that puts the plate inside
    the rail's own tube."""
    outer = p.frame_half_rear - 0.010
    return (("l", -outer), ("c", 0.0), ("r", outer))


def _rear_bearings(
    context: build.BuildContext, collection: bpy.types.Collection
) -> None:
    """Three self-aligning bearings in three aluminium cassettes."""
    p = context.params
    axle_y = P.rear_axle_y(p)
    axle_z = P.rear_axle_z(p)
    bore = p.axle_diameter * 0.5
    steel = context.material("axle_steel")
    alloy = context.material("engine_alloy")

    for label, station in _bearing_stations(p):
        bm = bmesh.new()
        build.lathe(
            bm,
            _sleeve_profile(BEARING_OUTER_RADIUS, bore, BEARING_WIDTH * 0.5),
            context.detail.tire_segments,
            axis="X",
            center=(station, 0.0, 0.0),
            close_profile=True,
        )
        bearing = build.object_from_bmesh(
            "axle_bearing_%s" % label, bm, collection, material=steel, shade_smooth=True
        )
        bearing.location = (0.0, axle_y, axle_z)

        bm = bmesh.new()
        build.lathe(
            bm,
            # The bore is 0.4 mm inside the bearing's outside rather than on it: two
            # coincident revolved surfaces are what issue #19's normal bake reports as
            # speckle, and `RIM_SEAT_CLEARANCE` exists for the same reason one ring
            # further in.
            _sleeve_profile(
                CASSETTE_OUTER_RADIUS,
                BEARING_OUTER_RADIUS - 0.0004,
                CASSETTE_WIDTH * 0.5,
            ),
            context.detail.tire_segments,
            axis="X",
            center=(station, 0.0, 0.0),
            close_profile=True,
        )
        cassette = build.object_from_bmesh(
            "axle_cassette_%s" % label, bm, collection, material=alloy,
            shade_smooth=True,
        )
        cassette.location = (0.0, axle_y, axle_z)


def _rear_hubs(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """The two rear wheel hubs, keyed to the axle and bolted to the rims.

    Art. 4.17: *"The sole purpose of the wheel hub is to enable the transfer of
    forces between the rim and the chassis."* Art. 4.2.1 makes it a chassis main
    part in its own right, which is why the rim's integral `HUB_BOSS` sleeve is gone.
    """
    p = context.params
    hub_x = P.rear_hub_x(p)
    bore = p.axle_diameter * 0.5
    material = context.material("engine_alloy")

    for label, side in (("rl", -1.0), ("rr", 1.0)):
        inboard = hub_x - HUB_REAR_LENGTH
        flange_start = hub_x - HUB_REAR_FLANGE_WIDTH
        profile = [
            (bore, inboard),
            (HUB_REAR_BODY_RADIUS, inboard),
            (HUB_REAR_BODY_RADIUS, flange_start),
            (HUB_REAR_FLANGE_RADIUS, flange_start),
            (HUB_REAR_FLANGE_RADIUS, hub_x),
            (bore, hub_x),
        ]
        if side < 0.0:
            profile = [(radius, -along) for radius, along in reversed(profile)]
        bm = bmesh.new()
        build.lathe(
            bm,
            profile,
            context.detail.tire_segments,
            axis="X",
            center=(0.0, P.rear_axle_y(p), P.rear_axle_z(p)),
            close_profile=True,
        )
        build.object_from_bmesh(
            "hub_%s" % label, bm, collection, material=material, shade_smooth=True
        )


# --- the front uprights ----------------------------------------------------


def _kingpin_axis(p: P.KartParams, side: float) -> tuple[Vector, Vector]:
    """(a point on the kingpin axis, the unit vector up it) for one side.

    The point is where the axis crosses x = `kingpin_x`, at `KINGPIN_PIVOT_Z`. Up
    the axis the pin leans **rearward** by the caster and **inboard** by the
    inclination, which is why the two tangents carry opposite signs in y and x.
    """
    direction = Vector(
        (
            -side * math.tan(KINGPIN_INCLINATION),
            -math.tan(KINGPIN_CASTER),
            1.0,
        )
    ).normalized()
    anchor = Vector((side * p.kingpin_x, P.front_axle_y(p), KINGPIN_PIVOT_Z))
    return anchor, direction


def _kingpin_point(p: P.KartParams, side: float, z: float) -> Vector:
    anchor, direction = _kingpin_axis(p, side)
    return anchor + direction * ((z - anchor.z) / direction.z)


def _front_uprights(
    context: build.BuildContext, collection: bpy.types.Collection
) -> None:
    """Kingpins, eccentric pills, knuckles, knuckle arms and stub axles.

    Art. 4.2.2 makes this the only articulation the regulations permit anywhere on
    a kart: *"Articulated connections are only allowed for the steering knuckle
    (through the king pin) and the steering."* Before #190 the kart had neither the
    kingpin nor the knuckle, and `axle_stub_f?` ran from `front_hub_x - 0.090` --
    i.e. from x 462.5, 925 mm apart, 190 mm outboard of the frame's own published
    735 mm front width.
    """
    p = context.params
    axle_y = P.front_axle_y(p)
    axle_z = P.front_axle_z(p)
    hub_x = P.front_hub_x(p)
    steel = context.material("axle_steel")
    alloy = context.material("engine_alloy")

    for label, side in (("fl", -1.0), ("fr", 1.0)):
        bottom = _kingpin_point(p, side, KINGPIN_BOTTOM_Z)
        top = _kingpin_point(p, side, KINGPIN_TOP_Z)

        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [tuple(bottom), tuple(top)],
            KINGPIN_DIAMETER * 0.5,
            context.detail.tube_segments,
        )
        build.object_from_bmesh(
            "kingpin_%s" % label, bm, collection, material=steel, shade_smooth=True
        )

        # The two eccentric pills, in the boss's bore at its own top and bottom.
        # Their bore is offset from their outside, which is the whole mechanism.
        for tier, z in (
            ("lower", p.ground_clearance + PILL_LENGTH * 0.5 + 0.002),
            ("upper", p.ground_clearance + p.kingpin_boss_length - PILL_LENGTH * 0.5
             - 0.002),
        ):
            centre = _kingpin_point(p, side, z)
            bm = bmesh.new()
            build.lathe(
                bm,
                _sleeve_profile(
                    PILL_DIAMETER * 0.5, KINGPIN_DIAMETER * 0.5, PILL_LENGTH * 0.5
                ),
                context.detail.tube_segments,
                axis="Z",
                center=(
                    centre.x - side * PILL_OFFSET,
                    centre.y,
                    z,
                ),
                close_profile=True,
            )
            build.object_from_bmesh(
                "kingpin_pill_%s_%s" % (label, tier),
                bm,
                collection,
                material=steel,
                shade_smooth=True,
            )

        # The knuckle. Axis-aligned on purpose -- see KNUCKLE_HEIGHT.
        outboard = side * (p.kingpin_x + KNUCKLE_HALF_WIDTH)
        inboard = side * (p.kingpin_x - KNUCKLE_HALF_WIDTH)
        bm = bmesh.new()
        build.box(
            bm,
            (KNUCKLE_HALF_WIDTH * 2.0, KNUCKLE_DEPTH, KNUCKLE_HEIGHT),
            (
                (outboard + inboard) * 0.5,
                axle_y - KNUCKLE_DEPTH * 0.5 + 0.038,
                KNUCKLE_BOTTOM_Z + KNUCKLE_HEIGHT * 0.5,
            ),
        )
        knuckle = build.object_from_bmesh(
            "knuckle_%s" % label, bm, collection, material=alloy
        )
        build.bevel_object(knuckle, context.detail)

        # The knuckle arm, straight rearward and parallel to the centerline.
        thickness, height = KNUCKLE_ARM_SECTION
        bm = bmesh.new()
        build.box(
            bm,
            (thickness, KNUCKLE_ARM_LENGTH + 0.030, height),
            (
                side * p.kingpin_x,
                axle_y - KNUCKLE_ARM_LENGTH * 0.5 + 0.015,
                axle_z,
            ),
        )
        arm = build.object_from_bmesh(
            "knuckle_arm_%s" % label, bm, collection, material=alloy
        )
        build.bevel_object(arm, context.detail)

        # The stub axle. `stub_axle_length` is the **visible** run, from the
        # knuckle's outboard face to the hub's inboard end.
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                (side * (p.kingpin_x + 0.010), axle_y, axle_z),
                (
                    side * (
                        p.kingpin_x + KNUCKLE_HALF_WIDTH + p.stub_axle_length
                        + STUB_BORE_ENTRY
                    ),
                    axle_y,
                    axle_z,
                ),
            ],
            STUB_DIAMETER * 0.5,
            context.detail.tube_segments,
        )
        build.object_from_bmesh(
            "axle_stub_%s" % label, bm, collection, material=steel, shade_smooth=True
        )

    # Guard: the whole point of §20.3.3 is that this chain closes. Fatal, because a
    # silent 142.5 mm residual is what #190 exists to stop.
    residual = hub_x - (
        p.kingpin_x + KNUCKLE_HALF_WIDTH + p.stub_axle_length + _front_hub_length(p)
    )
    if abs(residual) > 1e-9:
        raise SystemExit(
            "wheels.py: the front lateral chain does not close -- %.4f m left over.\n"
            "           front_hub_x %.4f = kingpin_x %.4f + knuckle %.4f + stub "
            "%.4f + hub %.4f"
            % (
                residual,
                hub_x,
                p.kingpin_x,
                KNUCKLE_HALF_WIDTH,
                p.stub_axle_length,
                _front_hub_length(p),
            )
        )


def _front_hub_length(p: P.KartParams) -> float:
    """Front hub length, `derived` as spec §20.3.3's residual: 117.5 mm."""
    return P.front_hub_x(p) - p.kingpin_x - KNUCKLE_HALF_WIDTH - p.stub_axle_length


def _front_hubs(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """The two front wheel hubs: a bored body between two flanges.

    The **inboard** flange is what the front disc's three tangs bolt to, which is
    what makes the front brake a stub-axle-side assembly -- Art. 4.12.5 confirms it
    from the other end by putting rain covers *"attached to the stub axle"*.
    """
    p = context.params
    hub_x = P.front_hub_x(p)
    inboard = hub_x - _front_hub_length(p)
    material = context.material("engine_alloy")

    for label, side in (("fl", -1.0), ("fr", 1.0)):
        profile = [
            (HUB_FRONT_BORE_RADIUS, inboard),
            (HUB_FRONT_INBOARD_RADIUS, inboard),
            (HUB_FRONT_INBOARD_RADIUS, inboard + HUB_FRONT_INBOARD_WIDTH),
            (HUB_FRONT_BODY_RADIUS, inboard + HUB_FRONT_INBOARD_WIDTH),
            (HUB_FRONT_BODY_RADIUS, hub_x - HUB_FRONT_FLANGE_WIDTH),
            (HUB_FRONT_FLANGE_RADIUS, hub_x - HUB_FRONT_FLANGE_WIDTH),
            (HUB_FRONT_FLANGE_RADIUS, hub_x),
            (HUB_FRONT_BORE_RADIUS, hub_x),
        ]
        if side < 0.0:
            profile = [(radius, -along) for radius, along in reversed(profile)]
        bm = bmesh.new()
        build.lathe(
            bm,
            profile,
            context.detail.tire_segments,
            axis="X",
            center=(0.0, P.front_axle_y(p), P.front_axle_z(p)),
            close_profile=True,
        )
        build.object_from_bmesh(
            "hub_%s" % label, bm, collection, material=material, shade_smooth=True
        )


def _steering_linkage(
    context: build.BuildContext, collection: bpy.types.Collection
) -> None:
    """Two tie rods and four rod ends. Art. 4.5.3 permits the rose joints by name.

    The outer rod end shares the kingpin's lateral station, because the knuckle arm
    points straight rearward -- which is what makes the sourced 270 mm rod fit and
    is *not* the true-Ackermann construction. See `KNUCKLE_ARM_LENGTH`.
    """
    p = context.params
    axle_y = P.front_axle_y(p)
    axle_z = P.front_axle_z(p)
    alloy = context.material("engine_alloy")

    for label, side in (("l", -1.0), ("r", 1.0)):
        outer = Vector(
            (side * p.kingpin_x, axle_y - KNUCKLE_ARM_LENGTH, axle_z)
        )
        inner = Vector(
            (side * PITMAN_EAR[0], PITMAN_EAR[1], PITMAN_EAR[2])
        )
        axis = (outer - inner).normalized()

        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [tuple(inner), tuple(outer)],
            TIEROD_DIAMETER * 0.5,
            context.detail.tube_segments,
        )
        build.object_from_bmesh(
            "tierod_%s" % label, bm, collection, material=alloy, shade_smooth=True
        )

        for tag, point, direction in (
            ("inner", inner, axis),
            ("outer", outer, -axis),
        ):
            bm = bmesh.new()
            build.sweep_tube(
                bm,
                [
                    tuple(point - direction * TIEROD_END_LENGTH * 0.5),
                    tuple(point + direction * TIEROD_END_LENGTH * 0.5),
                ],
                TIEROD_END_DIAMETER * 0.5,
                context.detail.tube_segments,
            )
            build.object_from_bmesh(
                "tierod_end_%s_%s" % (label, tag),
                bm,
                collection,
                material=alloy,
                shade_smooth=True,
            )


# --- the rear brake --------------------------------------------------------


def _clocked(radius: float, clock: float) -> tuple[float, float]:
    """(y, z) offsets of a point `radius` from an axle at `clock` forward of top."""
    return radius * math.sin(clock), radius * math.cos(clock)


def _caliper_body(
    bm: bmesh.types.BMesh,
    plane: float,
    axle_y: float,
    axle_z: float,
    thickness: float,
    length: float,
    height: float,
    disc_thickness: float,
    radius: float,
    clock: float,
) -> None:
    """An opposed-piston caliper: two halves and a bridge, straddling the disc.

    **Not a solid box, and that is geometry rather than detail.** A solid body of
    the drawing's `thickness` spans the disc's own plane, so it encloses whatever
    else sits at that plane inside its radial band -- measured, the rear one
    swallowed 9 mm of `brake_disc_rear_carrier` and the front one 6.5 mm of the
    friction ring. A real caliper is a C: two halves either side of the disc, each
    carrying pistons, joined by a bridge over the disc's outer edge. Built that way
    the caliper touches the disc **only through its pads**, which is what
    `joints.py` declares.

    Each half's thickness is `derived` from the parts rather than authored:
    `(thickness - disc - 2 x pad) / 2` is 18.75 mm at the rear and 18.0 at the
    front, against the ~19 mm cylinder wall spec §20.6.7 builds the 74 out of.
    """
    half_each = (thickness - disc_thickness - 2.0 * PAD_THICKNESS) * 0.5
    offset = disc_thickness * 0.5 + PAD_THICKNESS + half_each * 0.5
    rotation = Matrix.Rotation(-clock, 4, "X")
    offset_y, offset_z = _clocked(radius, clock)
    for sign in (-1.0, 1.0):
        build.box(
            bm,
            (half_each, length, height),
            (plane + sign * offset, axle_y + offset_y, axle_z + offset_z),
            rotation=rotation,
        )
    # The bridge, just inside the halves' outer edge and clear of the disc's rim:
    # inner radius `radius + height/2 - 0.010` against a disc that reaches
    # `pad_outer/2`, which is 2.5 mm at the rear and 6 mm at the front.
    bridge_radius = radius + height * 0.5 - 0.005
    bridge_y, bridge_z = _clocked(bridge_radius, clock)
    build.box(
        bm,
        (thickness, length * 0.8, 0.010),
        (plane, axle_y + bridge_y, axle_z + bridge_z),
        rotation=rotation,
    )


def _rear_brake(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """Disc, carrier, hub, six bobbins, caliper, bracket, two pads and the pad.

    On the kart's **left**, x negative, for three independent reasons: Art. 4.3's
    four keyways make the disc and the sprocket non-coplanar by construction; the
    engine is on the right so the chain has to reach a sprocket on the right and the
    brake balances it; and `crg_roadrebel_kz_detail11.webp` is the Road Rebel's left
    side with the drilled disc, its star carrier and the caliper plainly there.
    """
    p = context.params
    axle_y = P.rear_axle_y(p)
    axle_z = P.rear_axle_z(p)
    steel = context.material("axle_steel")
    alloy = context.material("engine_alloy")
    plastic = context.material("rubber_grip")
    detail = context.detail

    hub_outboard = DISC_REAR_X + DISC_REAR_HUB_WIDTH * 0.5
    hub_inboard = DISC_REAR_X - DISC_REAR_HUB_WIDTH * 0.5

    # The axle-mounted hub the carrier clamps.
    bm = bmesh.new()
    build.lathe(
        bm,
        _sleeve_profile(
            DISC_REAR_HUB_RADIUS, p.axle_diameter * 0.5, DISC_REAR_HUB_WIDTH * 0.5
        ),
        detail.tire_segments,
        axis="X",
        center=(DISC_REAR_X + 0.0225, 0.0, 0.0),
        close_profile=True,
    )
    hub = build.object_from_bmesh(
        "brake_disc_rear_hub", bm, collection, material=steel, shade_smooth=True
    )
    hub.location = (0.0, axle_y, axle_z)

    # The lobed star carrier, clamped over the hub's inboard end.
    # Inboard of the friction ring's own inboard face by 2 mm. It cannot straddle
    # the ring's plane: the carrier reaches radius 50 and the ring's inner edge is at
    # 42, so any x overlap is a real one -- and the bobbins are what bridge the gap.
    carrier_x = (
        DISC_REAR_X
        + DISC_REAR_THICKNESS * 0.5
        + DISC_REAR_CARRIER_THICKNESS * 0.5
        + 0.002
    )
    bm = bmesh.new()
    build.lathe(
        bm,
        _annulus_profile(
            DISC_REAR_CARRIER_RADIUS, 0.030, DISC_REAR_CARRIER_THICKNESS * 0.5
        ),
        detail.tire_segments,
        axis="X",
        center=(carrier_x, 0.0, 0.0),
        close_profile=True,
    )
    carrier = build.object_from_bmesh(
        "brake_disc_rear_carrier", bm, collection, material=steel, shade_smooth=True
    )
    carrier.location = (0.0, axle_y, axle_z)

    # The friction ring, floating on six bobbins.
    bm = bmesh.new()
    build.lathe(
        bm,
        _annulus_profile(
            DISC_REAR_DIAMETER * 0.5,
            DISC_REAR_RING_INNER,
            DISC_REAR_THICKNESS * 0.5,
        ),
        detail.tire_segments,
        axis="X",
        center=(DISC_REAR_X, 0.0, 0.0),
        close_profile=True,
    )
    disc = build.object_from_bmesh(
        "brake_disc_rear", bm, collection, material=steel, shade_smooth=True
    )
    disc.location = (0.0, axle_y, axle_z)

    # The bobbin circle is at radius 46 and the carrier reaches 50, both well
    # **inboard of the caliper's own inner radial face at 55** and of the pad's inner
    # rubbing radius at 68. Three separate collisions came out of putting them where
    # the pad is: at radius 68 a Ø10 bobbin clips the inner pad, and a carrier that
    # reaches the ring's inner edge overlaps either the ring itself or the caliper's
    # inboard half -- the clear x band between those two is 9 mm and the carrier is
    # 14 thick. A floating disc's bobbins live inside the swept band anyway, which is
    # what lets the ring expand without dragging.
    bobbin_radius = DISC_REAR_BOBBIN_STATION
    for index in range(DISC_REAR_BOBBIN_COUNT):
        angle = 2.0 * math.pi * index / DISC_REAR_BOBBIN_COUNT
        offset_y = bobbin_radius * math.cos(angle)
        offset_z = bobbin_radius * math.sin(angle)
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                (carrier_x + 0.004, offset_y, offset_z),
                (DISC_REAR_X - 0.004, offset_y, offset_z),
            ],
            DISC_REAR_BOBBIN_RADIUS,
            max(6, detail.tube_segments // 2),
        )
        bobbin = build.object_from_bmesh(
            "brake_disc_rear_bobbin_%d" % index,
            bm,
            collection,
            material=steel,
            shade_smooth=True,
        )
        bobbin.location = (0.0, axle_y, axle_z)

    # The caliper, straddling the disc at the pad's mean radius.
    pad_radius = (DISC_REAR_PAD_OUTER + DISC_REAR_PAD_INNER) * 0.25
    rotation = Matrix.Rotation(-CALIPER_REAR_CLOCK, 4, "X")
    bm = bmesh.new()
    _caliper_body(
        bm,
        DISC_REAR_X,
        axle_y,
        axle_z,
        CALIPER_REAR_THICKNESS,
        CALIPER_REAR_LENGTH,
        CALIPER_REAR_HEIGHT,
        DISC_REAR_THICKNESS,
        pad_radius,
        CALIPER_REAR_CLOCK,
    )
    caliper = build.object_from_bmesh(
        "brake_caliper_rear", bm, collection, material=alloy
    )
    build.bevel_object(caliper, detail)

    # Two pads, one each side of the friction plane.
    pad_offset = (DISC_REAR_THICKNESS + PAD_THICKNESS) * 0.5
    pad_y, pad_z = _clocked(pad_radius, CALIPER_REAR_CLOCK)
    for index, sign in ((0, -1.0), (1, 1.0)):
        bm = bmesh.new()
        build.box(
            bm,
            (
                PAD_THICKNESS,
                PAD_REAR_LENGTH,
                (DISC_REAR_PAD_OUTER - DISC_REAR_PAD_INNER) * 0.5,
            ),
            (DISC_REAR_X + sign * pad_offset, axle_y + pad_y, axle_z + pad_z),
            rotation=rotation,
        )
        build.object_from_bmesh(
            "brake_pad_rear_%d" % index, bm, collection, material=plastic
        )

    # The bracket: a radial plate on the cassette's outboard face, then an axial arm
    # out to the caliper at a radius that clears the carrier entirely.
    cassette_face = -(p.frame_half_rear - 0.010) - CASSETTE_WIDTH * 0.5
    arm_y, arm_z = _clocked(0.082, CALIPER_REAR_CLOCK)
    bm = bmesh.new()
    # The radial plate spans z 175..245 rather than 146..236: at 146 its inboard
    # corner is inside `axle_rear` itself -- the axle is Ø50 about z 147.5 and the
    # plate's own y band reaches 13 mm of the centerline. It still overlaps the
    # cassette over z 175..191, which is where the bolts are.
    build.box(
        bm,
        (0.014, 0.030, 0.070),
        (cassette_face - 0.002, axle_y + arm_y, axle_z + 0.0625),
    )
    build.box(
        bm,
        (0.056, 0.030, 0.024),
        (
            (cassette_face + DISC_REAR_X + CALIPER_REAR_THICKNESS * 0.5) * 0.5 - 0.006,
            axle_y + arm_y,
            axle_z + arm_z + 0.010,
        ),
    )
    bracket = build.object_from_bmesh(
        "brake_caliper_rear_bracket", bm, collection, material=alloy
    )
    build.bevel_object(bracket, detail)

    # Art. 4.12.4's protective pad, under the disc and level with the rails' lowest
    # point, so it grounds before the disc does.
    bm = bmesh.new()
    build.box(
        bm,
        PROTECTOR_SIZE,
        (
            DISC_REAR_X + PROTECTOR_SIZE[0] * 0.5 - 0.045,
            axle_y,
            PROTECTOR_TOP_Z - PROTECTOR_SIZE[2] * 0.5,
        ),
    )
    protector = build.object_from_bmesh(
        "brake_disc_protector", bm, collection, material=plastic
    )
    build.bevel_object(protector, detail)


# --- the front brakes ------------------------------------------------------


def _front_brakes(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """Two discs, two calipers, two brackets and four pads.

    Four-wheel brakes on a KZ are an `estimated` design choice, not a requirement:
    Art. 8.6 makes brakes *free* in Group 1 and Art. 9.6's `4WP` clause names KZ2.
    They are here because every KZ chassis is sold that way. ADR-0054.
    """
    p = context.params
    axle_y = P.front_axle_y(p)
    axle_z = P.front_axle_z(p)
    steel = context.material("axle_steel")
    alloy = context.material("engine_alloy")
    plastic = context.material("rubber_grip")
    detail = context.detail

    pad_radius = (DISC_FRONT_PAD_OUTER + DISC_FRONT_PAD_INNER) * 0.25
    rotation = Matrix.Rotation(-CALIPER_FRONT_CLOCK, 4, "X")
    ring_inner = DISC_FRONT_PAD_INNER * 0.5 - 0.006
    tang_inner = 0.016

    for label, side in (("fl", -1.0), ("fr", 1.0)):
        plane = side * DISC_FRONT_X

        bm = bmesh.new()
        build.lathe(
            bm,
            _annulus_profile(
                DISC_FRONT_DIAMETER * 0.5, ring_inner, DISC_FRONT_THICKNESS * 0.5
            ),
            detail.tire_segments,
            axis="X",
            center=(plane, 0.0, 0.0),
            close_profile=True,
        )
        # Three integral drive tangs at 120 degrees, reaching in to the hub's
        # inboard flange. `sourced` as shape off `007-B4-69` p. 2.
        for index in range(DISC_FRONT_TANG_COUNT):
            angle = 2.0 * math.pi * index / DISC_FRONT_TANG_COUNT
            mid = (ring_inner + tang_inner) * 0.5
            build.box(
                bm,
                (DISC_FRONT_THICKNESS, 0.018, ring_inner - tang_inner),
                (plane, mid * math.cos(angle), mid * math.sin(angle)),
                rotation=Matrix.Rotation(angle - math.pi * 0.5, 4, "X"),
            )
        disc = build.object_from_bmesh(
            "brake_disc_%s" % label, bm, collection, material=steel, shade_smooth=True
        )
        disc.location = (0.0, axle_y, axle_z)

        bm = bmesh.new()
        _caliper_body(
            bm,
            plane,
            axle_y,
            axle_z,
            CALIPER_FRONT_THICKNESS,
            CALIPER_FRONT_LENGTH,
            CALIPER_FRONT_HEIGHT,
            DISC_FRONT_THICKNESS,
            pad_radius,
            CALIPER_FRONT_CLOCK,
        )
        caliper = build.object_from_bmesh(
            "brake_caliper_%s" % label, bm, collection, material=alloy
        )
        build.bevel_object(caliper, detail)

        pad_offset = (DISC_FRONT_THICKNESS + PAD_THICKNESS) * 0.5
        pad_y, pad_z = _clocked(pad_radius, CALIPER_FRONT_CLOCK)
        for index, sign in ((0, -1.0), (1, 1.0)):
            bm = bmesh.new()
            build.box(
                bm,
                (PAD_THICKNESS, PAD_FRONT_LENGTH, PAD_FRONT_HEIGHT),
                (plane + sign * pad_offset, axle_y + pad_y, axle_z + pad_z),
                rotation=rotation,
            )
            build.object_from_bmesh(
                "brake_pad_%s_%d" % (label, index), bm, collection, material=plastic
            )

        # The bracket, from the knuckle out to the caliper's four-bolt flange. It
        # runs at y +567 rather than spec §20.6.6's +570 for a measured reason: at
        # +570 its inboard corner is inside the front hub's Ø52 inboard flange.
        knuckle_face = side * (p.kingpin_x + KNUCKLE_HALF_WIDTH)
        # Stops 24 mm short of the disc's plane. At the plane itself the arm's outer
        # end is inside the friction ring's radial band, and at 12 it is inside the
        # inboard **pad** -- the pad sits 10.5 mm off the plane and is 9 thick, so
        # the only clear station is inboard of x 430, which is the caliper's own
        # inboard half.
        reach_to = side * (DISC_FRONT_X - 0.024)
        bm = bmesh.new()
        build.box(
            bm,
            (abs(reach_to - knuckle_face) + 0.010, 0.030, 0.024),
            (
                (knuckle_face + reach_to) * 0.5 - side * 0.0025,
                axle_y + 0.042,
                axle_z + 0.012,
            ),
        )
        bracket = build.object_from_bmesh(
            "brake_caliper_%s_bracket" % label, bm, collection, material=alloy
        )
        build.bevel_object(bracket, detail)


# --- the hydraulics --------------------------------------------------------


def _pedal_plate_y(p: P.KartParams, z: float) -> float:
    """Where the brake pedal's arm crosses a given height, in y.

    **Rewritten for the organ pedal.** §40.5 replaced the hanging plate with a
    forged arm on a *bottom* pivot at (`pedal_pivot_y`, `pedal_pivot_z`) leaning
    rearward by `pedal_arm_rake`, so the arm's line is
    `y = pedal_pivot_y - tan(rake) x (z - pedal_pivot_z)` -- and the sign of that
    term flipped, because the old pad hung *forward* of its cross tube and this arm
    stands *behind* its pivot.

    The lean is duplicated here for the same reason `SIDE_BAR_PATH` is duplicated in
    `bodywork.py` -- a geometry module may not read another module's objects -- and
    it earns the duplication: aimed at the bar's *centre* instead, the push rod
    starts 11 mm off the arm and Art. 4.12.2's mandatory link is attached to
    nothing.
    """
    return p.pedal_pivot_y - math.tan(p.pedal_arm_rake) * (z - p.pedal_pivot_z)


def _brake_hydraulics(
    context: build.BuildContext, collection: bpy.types.Collection
) -> None:
    """Two master cylinders, their bracket, the pedal link, the regulator, the
    distributor and the two lines."""
    p = context.params
    detail = context.detail
    alloy = context.material("engine_alloy")
    steel = context.material("axle_steel")

    # **The bracket sits on `chassis_tray_edge_l`, not on the pan and not on the
    # front cross member.** Art. 4.6's floor tray is only 262 mm wide at this
    # station -- `frame.TRAY_HALF_WIDTH` is 131 at y +490 -- so the pan's own edge is
    # at x -131 and everything outboard of that is open air over the rail. The
    # mandatory edging tube runs along that edge with its top at z 81, and it is the
    # only structure at the height and station a CRG-style pump pair needs. Spec
    # §20.6.4's *"welded to `chassis_cross_front`"* cannot be built: that member is a
    # U-loop whose leg is at y +706 at this x, 216 mm forward of the cylinders.
    edge_top = P.rail_top_z(p) + p.tube_tray_edge
    bracket_z = edge_top + MASTER_BRACKET_SIZE[2] * 0.5
    body_bottom = edge_top + MASTER_BRACKET_SIZE[2]

    bm = bmesh.new()
    build.box(
        bm,
        MASTER_BRACKET_SIZE,
        (MASTER_BRACKET_X, MASTER_Y, bracket_z),
    )
    build.box(
        bm,
        (0.008, MASTER_BRACKET_SIZE[1], 0.042),
        (MASTER_BRACKET_UPSTAND_X, MASTER_Y, body_bottom + 0.021),
    )
    bracket = build.object_from_bmesh(
        "brake_master_bracket", bm, collection, material=alloy
    )
    build.bevel_object(bracket, detail)

    for label, station in (("front", MASTER_FRONT_X), ("rear", MASTER_REAR_X)):
        bm = bmesh.new()
        build.lathe(
            bm,
            _cylinder_profile(MASTER_BODY_DIAMETER * 0.5, MASTER_BODY_LENGTH * 0.5),
            detail.tube_segments,
            axis="Y",
            center=(station, MASTER_Y, MASTER_Z),
        )
        build.sweep_tube(
            bm,
            [
                (station, MASTER_Y, MASTER_Z),
                (station, MASTER_Y, MASTER_TOWER_TOP_Z),
            ],
            MASTER_TOWER_DIAMETER * 0.5,
            detail.tube_segments,
        )
        master = build.object_from_bmesh(
            "brake_master_%s" % label, bm, collection, material=alloy,
            shade_smooth=True,
        )
        build.bevel_object(master, detail)

    # Art. 4.12.2's doubled link: a clevis rod and a 2.0 mm cable beside it, both
    # running from the brake pedal's own plate into both cylinders -- which is what a
    # balance bar is, and what makes one rod satisfy a two-pump layout.
    pedal_x = -p.pedal_separation * 0.5 - 0.010
    for name, diameter, z in (
        ("brake_pushrod", PUSHROD_DIAMETER, PUSHROD_Z),
        ("brake_pushrod_link", PUSHROD_LINK_DIAMETER, PUSHROD_LINK_Z),
    ):
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                # **On the arm's centreline, not 6 mm ahead of it.** The 6 mm was
                # right for a hanging plate whose face pointed rearward and is wrong
                # for a forged arm 8 mm thick: at high detail the cable ended 2.48 mm
                # short of the part it is declared to pierce, and the low-detail bevel
                # was hiding it.
                (pedal_x, _pedal_plate_y(p, z), z),
                (MASTER_FRONT_X - 0.019, MASTER_Y + 0.052, z),
            ],
            diameter * 0.5,
            max(6, detail.tube_segments // 2),
        )
        build.object_from_bmesh(
            name, bm, collection, material=steel, shade_smooth=True
        )

    # The balance regulator, on the left rail's straight run -- inboard of the rail's
    # centerline rather than on top of it, because `chassis_bumper_socket_side_*_l`
    # occupies the rail's outboard side at y -100 and the regulator has to sit where
    # the rear hose already runs.
    bm = bmesh.new()
    build.lathe(
        bm,
        _cylinder_profile(REGULATOR_RADIUS, REGULATOR_LENGTH * 0.5),
        detail.tube_segments,
        axis="Y",
        center=(REGULATOR_X, REGULATOR_Y, P.rail_z(p) + 0.020),
    )
    regulator = build.object_from_bmesh(
        "brake_balance_regulator", bm, collection, material=alloy, shade_smooth=True
    )
    build.bevel_object(regulator, detail)

    # The distributor, on the bracket's upstand. Birel-only; the weakest-sourced
    # part in the section, and recorded rather than dropped.
    bm = bmesh.new()
    build.box(
        bm, DISTRIBUTOR_SIZE, (DISTRIBUTOR_X, MASTER_Y, body_bottom + 0.017)
    )
    distributor = build.object_from_bmesh(
        "brake_distributor", bm, collection, material=alloy
    )
    build.bevel_object(distributor, detail)

    _brake_lines(context, collection)


def _brake_lines(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """The front tee'd assembly and the single rear run.

    Both are cable-tied along the **upper** surface of the chassis tubes and neither
    crosses under the floor tray, which is the rule that keeps them out of Art.
    4.12.6's territory and off the skid plates.
    """
    p = context.params
    detail = context.detail
    steel = context.material("axle_steel")
    segments = max(6, detail.tube_segments // 2)

    caliper_y = P.front_axle_y(p) + 0.075
    # Both ends land on a caliper **half** at x +-470 rather than on the disc's own
    # plane at 445, which with a C-shaped body is the gap between the halves.
    caliper_x = DISC_FRONT_X + 0.025
    # The crossing height is 105 and it is the one figure in this route that is
    # forced. Art. 9.4.1's lower bumper bar runs at z 75..95 and its filleted corner
    # reaches x 222 at y +740, so a hose crossing the nose at 78 -- above the front
    # loop's tube top at 65, which is where "cable-tied along the upper surface"
    # puts it -- is inside that bar. There are 10 mm of clear air under the bar and
    # 114 above it, so the run goes over: 105 clears the lower bar by 7 mm and the
    # upper bar's underside at 209 by 101.
    crest_z = 0.105
    front_runs = (
        [
            (-caliper_x, caliper_y, 0.150),
            (-0.380, 0.660, 0.120),
            (-0.280, 0.720, crest_z),
            (-0.110, 0.755, crest_z),
            (0.110, 0.755, crest_z),
            (0.280, 0.720, crest_z),
            (0.380, 0.660, 0.120),
            (caliper_x, caliper_y, 0.150),
        ],
        [
            (0.0, 0.755, crest_z),
            (-0.060, 0.700, 0.100),
            (-0.120, 0.640, 0.094),
            (-0.150, 0.575, 0.092),
            (-0.230, 0.545, 0.098),
            (DISTRIBUTOR_X, MASTER_Y, MASTER_Z),
        ],
        [
            (DISTRIBUTOR_X, MASTER_Y, MASTER_Z),
            (MASTER_FRONT_X, MASTER_Y - 0.008, MASTER_Z),
        ],
    )
    bm = bmesh.new()
    for run in front_runs:
        build.sweep_tube(bm, run, LINE_RADIUS, segments)
    build.object_from_bmesh(
        "brake_line_front", bm, collection, material=steel, shade_smooth=True
    )

    # Inboard of the rail's centerline for its whole length, which is the fix for
    # four separate collisions: `chassis_bumper_socket_side_*_l`'s sleeves all
    # project **outboard** from the rail at y +400 and -100, and Art. 9.4.2 fixes
    # their 500 mm pitch, so the outboard side of that rail is not available to a
    # hose. It stays above the rail's tube top at z 65 and above the pan's at 69.
    rear_run = [
        (MASTER_REAR_X, MASTER_Y - 0.055, MASTER_Z),
        (-0.140, 0.380, 0.092),
        (-0.170, 0.330, 0.078),
        (-0.210, 0.250, 0.077),
        (-0.245, 0.150, 0.076),
        (-0.270, 0.000, 0.076),
        (REGULATOR_X, REGULATOR_Y, 0.076),
        (-0.290, -0.300, 0.082),
        (-0.320, -0.450, 0.130),
        (-0.345, -0.470, 0.190),
        (-0.370, -0.480, 0.2225),
    ]
    bm = bmesh.new()
    build.sweep_tube(bm, rear_run, LINE_RADIUS, segments)
    build.object_from_bmesh(
        "brake_line_rear", bm, collection, material=steel, shade_smooth=True
    )
