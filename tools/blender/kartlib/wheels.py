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

# The tire's shape table. Every figure here was read off
# `refs/kart-visual/det_tonykart_401t_museum.jpg` (gridded crops, tire-pass
# session), which mounts the same Vega 5-inch slicks the dimensions come from.
# The homologation forms' own cross-section sketch is NOT usable for shape: the
# 047-TO-12 and 047-TO-14 p. 3 curves are pixel-identical -- one template
# drawing claiming to be both 130 mm and 207 mm wide -- so only their dimension
# tables are sourced and the photograph is the shape authority.

TIRE_TREAD_CROWN_FRAC: float = 0.011
"""Radial drop from the tread center to the tread edge, as a fraction of tread
width: ~2.0 mm rear, ~1.2 mm front. `estimated` -- the museum photo shows the
tread band reading a few millimeters convex, never dead flat; the template
sketch agrees in kind (~3 mm) but is a template."""

TIRE_ROLL_HANDLE_AXIAL: float = 0.45
TIRE_ROLL_HANDLE_RADIAL: float = 0.18
"""Bezier handle lengths for the shoulder roll, as fractions of the axial gap
(tread edge to widest point) and the radial drop (tread edge to widest point).
Larger reads squarer. `estimated`: tuned so the roll spans ~30-35 mm radially on
the rear against the museum photo's soft continuous shoulder, where the old
quarter-arc spanned the 8.5-14 mm left over between two sourced widths and read
as a corner."""

TIRE_BULGE_HANDLE: float = 0.45
"""Lower-sidewall Bezier handle below the widest point, as a fraction of the
radial drop from bulge to bead. Sets how long the sidewall stays fat before
diving for the rim; the museum photo's sidewall carries its bulge well past
mid-height."""

TIRE_SEAT_TUCK: float = 0.006
"""Axial length of the concave landing onto the bead seat. The S-flip is what
makes the sidewall arrive steep at the flange lip so the gold rim edge reads
recessed into the rubber, as in the museum photo, instead of the tire ending on
a convex chamfer."""

TIRE_CROWN_STEPS: int = 3
TIRE_ROLL_STEPS: int = 7
TIRE_LOWER_STEPS: int = 7
"""Points along each piece of the profile at low detail; the caller scales them
by `tire_segments // 32` so the high-detail bake source samples the same curves
twice as densely (ADR-0059: one shape, two densities). Independent of
`tire_segments`, which is the resolution *around* the tire."""

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

VENT_COUNT: int = 5
VENT_ARC_FRACTION: float = 0.5
VENT_INNER_RADIUS: float = 0.030
VENT_OUTER_RADIUS: float = 0.050
"""Five oblong vent slots through the face plate, the visual signature of a cast
magnesium kart wheel and the recorded omission from the first build. `estimated`,
and deliberately generic per ADR-0062's design-intent rule: every reference mag
wheel is vented (the 401T museum kart's gold rims, the OTK/AMV catalogs), no
manufacturer's exact teardrop is copied. Five slots at half the period each,
spanning radii 30..50 of the plate's 16..60 dish, read as the type without being
anyone's part.

The slots are true through-holes built into the revolution grid -- no booleans:
the vent boundary radii and angles are exact grid stations at both detail levels,
so low and high are the same shape at two densities, and every hole edge is
stitched with wall quads so the plate stays watertight for the winding gate."""

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
SPROCKET_X: float = 0.445
"""Sprocket center, on the kart's RIGHT, between the right bearing hanger at
+300 and the right hub's inboard face at ~+548 -- which is where
`tonykart_racer401T_p05.jpg`'s chain guard plate (+378..+462) puts it. Must
equal `params.chain_x`; the corridor audit moved both from +0.115, a value
that ran the chain through the seat's right flank.

**It was on the left, and the reason given was wrong.** The comment here said "a
KZ drives the left rear", which cannot be a reason for anything: the rear axle is
one solid shaft with both wheels locked to it (ARCHITECTURE.md §6), so it drives
neither wheel preferentially and the sprocket's side is a packaging question. The
packaging answer is that the engine sits on the driver's right (`params.engine_x`),
so the chain has to reach a sprocket on the right -- a chain crossing under the seat
to the far side is not a thing any kart does. **The rear brake disc is what goes on
the left**, and Art. 4.3's four-keyway clause is the formal reason it has to: four
stations on one shaft, of which the disc and the sprocket are two, so they cannot
be coplanar. Separation 845 mm, on opposite sides of the center bearing."""


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
One piece, no floating carrier, nine curved slots, two rings of nine drilled holes,
and **3 integral drive tangs at 120 degrees** on the inner bore that bolt to the
front hub. The face pattern is built -- see `DISC_SLOT_COUNT` for where each count
was measured. It was **claimed and not built** until #214: this docstring said six
slots and two hole rings while the mesh was a plain ring plus three tangs at Euler
characteristic +6, which is three disjoint solids and not one hole.

The plane at +-445 is `derived` and it is spec §20.6.6's own resolution of an
interference the measurement pass found in itself. That pass boxed the disc at +-480
between a rim inner flange at 495 and a kingpin at +-465, then reported that *"with
a 66 mm body centered at 480 it reaches 513 and fouls"*. **§20.3 dissolves the
box**: the +-465 was `stub_axle_length` used as the spindle arm, and with the
kingpin at +-320 there are 172.5 mm of clear spindle. Moving the *disc* 35 mm
inboard and keeping the caliper symmetric about it is the physical answer -- an
opposed-piston caliper has a piston on each side and cannot be offset 23 mm without
the outboard half becoming 4 mm of aluminium."""

DISC_PLATE_THICKNESS: float = 0.0035
DISC_VENT_INNER_FRAC: float = 0.600
DISC_PILLAR_PER_SECTOR: int = 2
DISC_PILLAR_WIDTH: float = 0.007
DISC_GROOVE_DEPTH: float = 0.0010
DISC_RIM_CHAMFER: float = 0.0010
"""The disc is **ventilated**, which on these forms means what it says: two
friction plates with a cavity between them, not one drilled plate.

`Disque ventilé / Ventilated disc` is ticked **Oui / Yes** on all three forms in
`refs/` -- CRG `82/FR/11` (2005), Birel `007-B4-69` (2017) and Birel
`007-BRKF-01` (2024) -- and all three give the same front disc, **Ø150 x 12 +-1**.
That thickness is the one number that never moves, and it is also the one that
made the part look wrong when it was built solid: 12 mm of steel across Ø150 is a
1.26 kg chunk per corner, where 12 mm as two 3.5 mm plates around a 5 mm cavity is
about half that and is what the sourced word *ventilated* has meant the whole time.

This module's own docstring used to deny it, in these words: *"'Ventilated' on
these forms means drilled and slotted through a single plate, not a two-plate
vented rotor -- the drawing shows one plate."* That was an assertion, not a
measurement, and the drawing it leans on is a **plan view**, which cannot show a
cavity from directly above. Corroborated independently from the trade side, where
solid sprint-kart rotors run 3-6 mm and **shifter karts run 10-12 mm ventilated** --
so 12 mm is a vented number, and reading it as solid is what put three times too
much steel in the model.

`DISC_PLATE_THICKNESS` and the cavity are `estimated`: 3.5 + 5.0 + 3.5 sums to the
sourced 12.0, and 3.5 mm is about the thinnest plate that still carries a 1.0 mm
groove and a pad. The pillar count is `estimated` too, and deliberately inherits
the face pattern's own **measured** nine-fold symmetry rather than inventing a
number -- two per sector at two radii, 18 in all, standing in the gap between the
drilled rings where they foul neither the drillings nor the grooves."""

DISC_FRONT_BORE_RADIUS: float = 0.03375
DISC_FRONT_LUG_INNER: float = 0.022
DISC_FRONT_LUG_HALF_ANGLE: float = math.radians(14.0)
DISC_LUG_BOLT_FRAC: float = 0.531
DISC_LUG_BOLT_DIAMETER: float = 0.0075
"""The front disc's open center: a scalloped bore with three tabs across it.

`0.45 R` for the scallop and 0.531 R for the bolt circle are `derived` off the same
`007-BRKR-10` CAD as the face pattern -- it puts the scallop at 0.483 R and the bolt
at 0.531 R, and 0.45 buys the 0.9 mm the bolt hole's own edge needs at our smaller
bolt diameter.

**`DISC_FRONT_LUG_INNER` is `derived` from our hub and not from the form**, and it
is the one place this part is not the photograph. The form's tabs stop at 0.40 R --
Ø60 on a Ø150 disc -- because a real front hub carries a disc flange near Ø80.
`HUB_FRONT_INBOARD_RADIUS` is 24 mm, itself `estimated`, so a faithful tab would
bolt to 12 mm of fresh air. The tabs are run in to 22 mm instead, overlapping that
flange by 2 mm, which keeps `joints.py`'s bolted joint real. The tabs are therefore
longer in proportion than the form's. **The hub flange is the thing that is wrong**;
see the note in `_front_brakes`."""

DISC_SLOT_COUNT: int = 9
DISC_SLOT_INNER_FRAC: float = 0.675
DISC_SLOT_OUTER_FRAC: float = 0.950
DISC_SLOT_WIDTH: float = 0.0037
DISC_SLOT_SWEEP: float = math.radians(11.2)
DISC_HOLE_INNER_FRAC: float = 0.757
DISC_HOLE_OUTER_FRAC: float = 0.903
DISC_HOLE_CLOCK_INNER: float = math.radians(7.1)
DISC_HOLE_CLOCK_OUTER: float = math.radians(-6.8)
DISC_HOLE_DIAMETER: float = 0.0040
"""The disc face pattern, **measured off `007-BRKR-10` p. 2's exploded CAD** rather
than counted off a photograph. Counts and pitch are `sourced`; radii are `derived`
as fractions of the disc's own sourced radius; the two widths are `estimated`.

That drawing rather than `007-BRKF-01`'s because the rear form's disc is drawn
**orthographic** -- its nine slots come out at 40.0 degrees with no scatter, and
both hole rings at 40.0 -- while the front form's is an oblique exploded view whose
angles are foreshortened. The front's pattern was then confirmed to be the same
one: its two hole rings sit at the same ratio to each other (0.840 against 0.837)
and its own slots and rings land on nine sectors at 40 degrees. The photographs on
both forms show one part carrying **both** homologation numbers, so front and rear
really are one family at two diameters.

Method: threshold the render to ink, flood the background in from the border, and
every enclosed white region is a feature. Group by radius, and the angular pitch
falls out of the centroids:

    ring            n   r/R     pitch      what
    outer holes     9   0.903   40.0 deg   drilled
    slots           9   0.812   40.0 deg   curved, centroid radius
    inner holes     9   0.757   40.0 deg   drilled
    lug bolts       3   0.531   120.0 deg  one per drive tang

One 40 degree sector repeats nine times: an inner hole 7.1 degrees one side of the
slot, an outer hole 6.8 degrees the other. **Nine, not the six this module's
docstring claimed and not the eight #214 read off the photo.** Three independent
families agreeing on the same nine sectors is what makes it a measurement.

`DISC_SLOT_SWEEP` is the slot's lean: its bounding box on that drawing is 26.8 mm
radial by 15.4 mm tangential at a centroid radius of 79.2, so the slot's center
line shifts 11.2 degrees between its inner and outer ends. Applied linearly in
radius, which draws a spiral segment rather than a circular arc -- indistinguishable
at 31 mm long, and the tangent never has to be solved for.

The two widths are `estimated` because a CAD circle is an ink **outline**: its white
interior is short of the real hole by one stroke. The rear drawing's holes measure
Ø3.28 interior, and the front form's fitted photograph -- no stroke to subtract,
scaled by the outer ring's own 0.903 R -- gives Ø3.39 on a Ø150 disc. Two
measurements of two different parts landing within 0.11 mm is the evidence; Ø4.0 is
those plus about half a stroke, and a drill is a drill, so it does **not** scale
with the disc. Slot width the same way: 87.9 mm2 of interior over a 31 mm arc is
2.85 wide, so 3.7."""

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
for cooling, a banjo on each half and a bleed nipple. Not a sliding caliper.

**Two later measurements disagree with the in-plane pair and are recorded rather
than applied.** `007-BRKR-10` p. 2 item 3 -- a Freeline caliper on that form's own
sourced Ø150 disc, so a better-anchored plate than `007-B4-69` -- measures 96.6
tangential x 50.7 radial, and `007-BRKF-01` p. 4 measures 88 tangential. Against
this block's 103 x 62 that is -6%/-15% tangential and -18% radial. The axial 66 is
confirmed exactly by the p. 4 photograph. Moving an `estimated` figure onto another
`estimated` figure off an oblique photograph buys nothing, so the envelope stays
and the built shape is driven by the sourced parts it must contain -- the 49.7 x 25
pad, the 12 mm disc, the Ø25 piston -- not by these three numbers."""

CALIPER_FRONT_PISTON_COUNT: int = 2
CALIPER_FRONT_PISTON_BORE: float = 0.025
CALIPER_REAR_PISTON_COUNT: int = 2
CALIPER_REAR_PISTON_BORE: float = 0.032
"""Piston counts and bores, both `sourced` and from **two different makers** by
ADR-0067: the front off `007-BRKF-01` §B (*"Nombre de pistons 2 par etrier,
O alesage de l'etrier 25 mm"*), the rear off CRG `82/FR/11`.

Freeline's own rear form `007-BRKR-10` says 2 x O25 on a O150 disc, which is **not**
what this kart's rear is -- the rear is CRG's O195 disc and O32 bore. Both readings
are correct and they are different parts. Anyone re-deriving the rear from
`007-BRKR-10` because it is the Freeline form is reading the wrong maker's rear."""

CALIPER_FRONT_BOSS_DIAMETER: float = 0.0317
CALIPER_FRONT_BOSS_PROUD: float = 0.006
CALIPER_FRONT_CAP_DIAMETER: float = 0.028
CALIPER_FRONT_CAP_PROUD: float = 0.008
"""The cylinder boss on each half's outer face, and the anodized cap closing it.

`CALIPER_FRONT_BOSS_DIAMETER` is `derived`: `007-BRKR-10` p. 2 item 3 draws the
cylinder as a clean circle whose interior floods to 60 x 60 px at an area of 2815
px2, which is a circle to within 0.4%, and the plate's ruler is **0.5291 mm/px**
from its own §B Ø150 disc spanning 283.5 px mid-stroke. That is 31.7 mm around a
sourced Ø25 bore, so a 3.35 mm wall.

**The plate's scale is corroborated by a standard fastener rather than by a second
disc.** Its two socket screws measure 13.9 mm across the counterbore and 6.2 mm
across the hex socket; ISO 4762 M8 is 13.0 and 6.0. Two features of two different
parts agreeing to 7% is the error bar on everything measured off this plate.

The cap is `estimated` off `007-BRKF-01` p. 4 at that photograph's ~15.3 px/mm --
itself the mean of two rulers 10% apart, the 12 mm disc gap and the 49.7 pad -- and
the proud figures are the weakest numbers in this block. The cap is red anodized;
the boss and the body are black."""

CALIPER_FRONT_MOUTH_WIDTH: float = 0.033
CALIPER_FRONT_MOUTH_DEPTH: float = 0.030
"""The slot that straddles the disc, axial then radial.

Width is `derived` from what has to fit: 12 disc + 2 x 9 pad = 30, plus 3 of running
clearance. It is corroborated at 33 mm measured off `007-BRKF-01` p. 4, which is the
strongest agreement anywhere in this block because the photograph looks straight
into the mouth.

Depth is `derived` from the disc: the friction band runs 74.5 out to 46 in, so 28.5
mm of disc has to disappear into the slot, and 30 clears it."""

CALIPER_FRONT_PIN_DIAMETER: float = 0.004
CALIPER_FRONT_PIN_COLLAR_DIAMETER: float = 0.012
CALIPER_FRONT_PIN_COLLAR_LENGTH: float = 0.015
CALIPER_FRONT_NIPPLE_HEX: float = 0.0065
CALIPER_FRONT_NIPPLE_LENGTH: float = 0.015
CALIPER_FRONT_BANJO_DIAMETER: float = 0.0085
CALIPER_FRONT_BANJO_LENGTH: float = 0.015
CALIPER_FRONT_CHAMFER: float = 0.010
"""The furniture, every figure `estimated` off `007-BRKF-01` p. 4 at ~15.3 px/mm.

**The feature inventory in this block is what the photograph shows, and it is not
what the ticket's prose said.** There is **one** transverse pad pin, not two, and
there is **no bridge and no tie bolt**: a single Ø4 pin crosses the mouth at
mid-depth, protrudes past both halves, and carries a machined collar at its centre
with a hex socket in it. The body is one piece. Read as *"a bridge across the mouth
on two pad pins with a bolt head centred"*, which is the pin plus its collar seen
from the front.

The bleed nipple is at the **top left**, hex base, angled up and outboard. The
banjo is at the **bottom left**, brass against the black body. Both sit on the same
half. The top two corners are chamfered at about 10 mm; the top face is flat and
carries the `FL` badge and the homologation number."""

CALIPER_MOUNT_BOLT_DIAMETER: float = 0.008
CALIPER_MOUNT_BOLT_HEAD_DIAMETER: float = 0.013
CALIPER_MOUNT_BOLT_PITCH: float = 0.066
"""M8 socket screws through the body into the bracket, `sourced` as a **standard
fastener** -- the counterbore and socket measured off `007-BRKF-01`'s companion
plate identify ISO 4762 M8 to 7%, and a bolt is a bolt, so the head does not scale
with the caliper.

The 66 mm pitch is measured on `007-BRKR-10` p. 2's **rear** caliper and is
therefore a proportion carried across parts, not a figure for this one. It is here
so a builder has a defensible spacing rather than an invented one; the front
caliper's own bolts face inboard and are not visible in any photograph on file."""

PAD_REAR_LENGTH: float = 0.058
PAD_FRONT_LENGTH: float = 0.0497
PAD_FRONT_FRICTION_LENGTH: float = 0.0477
PAD_FRONT_HEIGHT: float = 0.025
PAD_FRONT_BACKING_HEIGHT: float = 0.045
PAD_THICKNESS: float = 0.009
"""**2 pistons at 25 mm front, 2 pistons at 32 mm rear**, 2 pads per wheel either
way. ADR-0067: the front is Freeline `007-BRKF-01` and the rear stays CRG
`82/FR/11`, so the two halves of this block cite two different forms on purpose.

Front, all `sourced` off `007-BRKF-01` §B read directly: overall pad length
**49.7**, friction length **47.7**, friction height **25**, each +-1.5. Rear
`PAD_REAR_LENGTH` is CRG's 58. Clamp area per wheel, `derived`: front
2 x pi x 12.5^2 = **982 mm2**, rear 2 x pi x 16^2 = **1608 mm2** -- the front-to-rear
ratio is 0.61 and sits **below 1.0**, which is the side Birel's own system puts it
on and the opposite side from CRG's 4 x 26 front. That is what the balance
regulator trims, and it is the number a brake-bias tunable will sit on.

**This block asserted 4 pistons at 26 mm and a 2124 mm2 front clamp for a
milestone**, sourced correctly off `82/FR/11` and describing a caliper this kart
does not have. ADR-0066's family, third case.

`PAD_FRONT_BACKING_HEIGHT` is `estimated` and is the one front pad figure §B does
not give. The p. 4 pad photograph is near dead-on and its silhouette spans 485 x
439 px; taking the horizontal as the sourced 49.7 gives 0.1025 mm/px and a
backing plate **45 mm** tall against a 25 mm friction height. The 20 mm surplus is
one edge of the plate, not a border all round: the strongly-arched edge is the
**inner** radius (tighter arc = smaller radius), so the ear that carries the
lightening holes reaches *inward* past the friction material toward the caliper's
pad pin, and the friction band sits against the outer-radius edge where the disc's
own 28.5 mm band is."""

PAD_FRONT_HOLE_COUNT: int = 3
PAD_FRONT_HOLE_DIAMETER: float = 0.0052
PAD_FRONT_HOLE_PITCH: float = 0.0113
"""Three lightening holes in a straight row across the pad's inner ear, `sourced`
for the count off the `007-BRKF-01` p. 4 photograph and `estimated` for the two
lengths on the same 0.1025 mm/px scale as `PAD_FRONT_BACKING_HEIGHT`.

The holes measure 195-215 px against a drawn 5 mm block of 195 px, so **5.2 mm**
with the spread of the three as the error bar -- the lit interior of a hole in a
photograph is its opening plus a chamfer highlight, so this is an upper bound on
the drill. Pitch 11.3 leaves 6.1 mm of plate between holes, which is what makes the
row read as three holes rather than a slot."""

MASTER_BORE: float = 0.022
MASTER_BODY_DIAMETER: float = 0.032
MASTER_BODY_LENGTH: float = 0.130
MASTER_TOWER_DIAMETER: float = 0.028
MASTER_TOWER_TOP_Z: float = 0.180
#: Boxed in on both ends: forward of 0.490 the body's cap meets the steering
#: hoop's level arm (which #201 pulled rearward for the clevis), and rearward
#: of about 0.482 the body's tail meets the side bumper's upper-front socket
#: riser at y 386..414. So y stays put and #201's clearance came from x: the
#: whole package (both cylinders, bracket, upstand, distributor) went 20 mm
#: outboard, because the pinch against the hoop was at the rear cylinder's
#: INBOARD end -- the hoop's level arm dives away forward as |x| grows, so
#: every millimeter outboard buys about 0.6 of clearance.
MASTER_Y: float = 0.490
MASTER_Z: float = 0.101
MASTER_FRONT_X: float = -0.212
MASTER_REAR_X: float = -0.175
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

The rear cylinder is the **inboard** one at -175 and the front the outboard one
at -212, 37 mm apart rather than spec §20.6.4's 45. Both went 20 mm outboard with
#201 -- the pedals moved to +-150 and the steering hoop's level arm then pinched
against the rear cylinder's inboard end, and outboard is the direction in which
the hoop dives away. (An earlier version of this paragraph said the *front*
cylinder was the inboard one at -148; the constants below have said otherwise
since before #201, and the pushrod reaches the outboard cylinder either way.)"""

#: 120 wide, was 90: the plate bolts to the tray edging at x -131 and the
#: cylinders moved 20 mm outboard (#201), so a 90 plate could no longer span
#: from the edging to under the front cylinder at -212. Same part, longer.
MASTER_BRACKET_SIZE: tuple[float, float, float] = (0.120, 0.090, 0.004)
MASTER_BRACKET_X: float = -0.190
PEDAL_FACE_TILT: float = 0.260
MASTER_BRACKET_UPSTAND_X: float = -0.235
DISTRIBUTOR_X: float = -0.243
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
        _tire_profile(
            p, diameter, width, tread, max(1, context.detail.tire_segments // 32)
        ),
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


def _cubic_samples(
    p0: tuple[float, float],
    c0: tuple[float, float],
    c1: tuple[float, float],
    p1: tuple[float, float],
    steps: int,
) -> list[tuple[float, float]]:
    """`steps` samples of a cubic Bezier at t = 1/steps .. 1, excluding t = 0.

    The caller has already emitted p0 as the previous piece's last point, so
    including t = 0 would double a ring and hand `lathe` a zero-height quad band.
    """
    out: list[tuple[float, float]] = []
    for step in range(1, steps + 1):
        t = step / steps
        u = 1.0 - t
        out.append(
            (
                u * u * u * p0[0]
                + 3.0 * u * u * t * c0[0]
                + 3.0 * u * t * t * c1[0]
                + t * t * t * p1[0],
                u * u * u * p0[1]
                + 3.0 * u * u * t * c0[1]
                + 3.0 * u * t * t * c1[1]
                + t * t * t * p1[1],
            )
        )
    return out


def _tire_profile(
    p: P.KartParams, diameter: float, width: float, tread: float, scale: int
) -> list[tuple[float, float]]:
    """(radius, x) pairs for one tire, revolved about X by `build.lathe`.

    Built as one half and mirrored, so the tire is symmetric about its own center
    plane by construction rather than by two lists agreeing. Symmetry is what makes
    the same mesh correct on both sides of the kart without a mirror or a 180 degree
    rotation -- see the module docstring for why that matters.

    The half runs from the tread center outward and down, three pieces:

        crowned tread -> shoulder roll -> lower sidewall S onto the bead seat

    There is deliberately no straight "sidewall" segment left: the museum photo
    (see the shape table above) shows one continuous convex roll from the tread
    edge to the widest point, so the shoulder is a single tangent-matched cubic
    covering that whole span rather than a corner radius plus a flat. The old
    quarter-arc construction clamped to the 8.5-14 mm left between two sourced
    widths and read as a machined chamfer on a drum.

    The lower piece is one cubic with an inflection: convex while it carries the
    bulge down from the widest point, concave as it lands on the bead seat, which
    is what tucks the rubber in behind the rim flange lip. It crosses the flange
    lip radius ~1.5 mm proud of the rim face and dives inside, so the visible seam
    is the flange edge pressing into the sidewall, same overlap discipline as the
    old profile.

    `tire_*_tread_width` stays authored off the homologation forms; the crown drops
    the tread edge by `TIRE_TREAD_CROWN_FRAC` and the roll starts tangent to that
    slope, so the tread/shoulder joint never creases.

    Point order is what sets the surface orientation. `build.lathe` winds a ring
    pair so that a profile advancing in +x on the outward-facing side gives outward
    normals, so the fold at the widest point -- where x stops increasing and turns
    back inboard toward the bead -- is what makes the turn-in face outboard rather
    than inside out.

    `scale` multiplies the per-piece step counts (1 at low detail, 2 at high) so
    both details sample identical curves and the high mesh is the same shape at
    twice the profile density.
    """
    tread_radius = diameter * 0.5
    half_width = width * 0.5
    bead_radius = p.rim_bead_diameter * 0.5
    bulge_radius = bead_radius + p.tire_sidewall_bulge

    tread_x = tread * 0.5
    crown = tread * TIRE_TREAD_CROWN_FRAC
    edge_radius = tread_radius - crown
    # dr/dx of the crown parabola at the tread edge; the roll's first handle leaves
    # along this slope so the joint is tangent-continuous.
    crown_slope = 2.0 * crown / tread_x

    half: list[tuple[float, float]] = []

    # Tread: a shallow parabola from center to edge. Starts at step 1 -- the
    # center ring at x = 0 would mirror onto itself and give lathe a zero-height
    # band; the chord across the first two samples sags by well under a tenth of
    # a millimeter.
    crown_steps = TIRE_CROWN_STEPS * scale
    for step in range(1, crown_steps + 1):
        x = tread_x * step / crown_steps
        half.append((tread_radius - crown * (x / tread_x) ** 2, x))

    # Shoulder roll: one cubic from the tread edge to the widest point, leaving
    # along the crown's slope and arriving purely radial.
    axial_gap = half_width - tread_x
    radial_drop = edge_radius - bulge_radius
    roll_h0 = TIRE_ROLL_HANDLE_AXIAL * axial_gap
    half.extend(
        _cubic_samples(
            (edge_radius, tread_x),
            (edge_radius - crown_slope * roll_h0, tread_x + roll_h0),
            (bulge_radius + TIRE_ROLL_HANDLE_RADIAL * radial_drop, half_width),
            (bulge_radius, half_width),
            TIRE_ROLL_STEPS * scale,
        )
    )

    # Lower sidewall: one cubic from the widest point onto the bead seat. The
    # second handle stands TIRE_SEAT_TUCK proud of the seat, which is what puts
    # the inflection -- convex bulge above, concave tuck below -- into the curve.
    seat_x = half_width - TIRE_BEAD_INSET
    half.extend(
        _cubic_samples(
            (bulge_radius, half_width),
            (
                bulge_radius - TIRE_BULGE_HANDLE * (bulge_radius - bead_radius),
                half_width,
            ),
            (bead_radius, seat_x + TIRE_SEAT_TUCK),
            (bead_radius, seat_x),
            TIRE_LOWER_STEPS * scale,
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
    _rim_plate_vented(context, bm, p)
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


def _plate_front_axial(p: P.KartParams, radius: float) -> float:
    """Front-face axial position of the dish at `radius`, off the same 3-point
    outline the plain lathe used to revolve: bore, knee at 0.45 of the edge,
    edge at half the plate thickness. The back face is its mirror.

    A function of radius rather than a point list because the vented plate needs
    the surface evaluated at the vent boundary radii exactly -- the hole edges
    are grid stations, not intersections.
    """
    seat = p.rim_bead_diameter * 0.5 - RIM_SEAT_CLEARANCE
    edge = seat - RIM_WALL * 0.5
    knee_r = edge * 0.45
    knee_x = -RIM_PLATE_DISH * 0.42
    if radius <= knee_r:
        t = (radius - RIM_PLATE_BORE) / (knee_r - RIM_PLATE_BORE)
        return -RIM_PLATE_DISH + (knee_x + RIM_PLATE_DISH) * t
    t = (radius - knee_r) / (edge - knee_r)
    return knee_x + (-RIM_PLATE_THICKNESS * 0.5 - knee_x) * t


def _rim_plate_vented(
    context: build.BuildContext, bm: bmesh.types.BMesh, p: P.KartParams
) -> None:
    """The wheel face as a closed revolution grid with `VENT_COUNT` through-slots.

    The plain plate was one `build.lathe` of a closed outline. A vent hole cannot
    come out of a lathe, and a boolean is banned by the determinism rules, so this
    builds the same torus of quads by hand and simply does not emit the front and
    back sheet faces inside a vent window -- then stitches the four edges of each
    window (inner arc, outer arc, two radial sides) with wall quads connecting the
    front sheet to the back sheet. Every edge ends up with exactly two faces, so
    the winding gate's watertightness check covers the vents rather than skipping
    them.

    The angular grid is built per vent period -- `spoke_steps` stations across the
    solid spoke, `vent_steps` across the window -- so the window boundaries land on
    exact stations at any density and low/high detail are the same shape. Radial
    stations are the same short list at both details: the surface between them is a
    straight polyline, so extra stations would change nothing.
    """
    seat = p.rim_bead_diameter * 0.5 - RIM_SEAT_CLEARANCE
    edge = seat - RIM_WALL * 0.5
    knee_r = edge * 0.45
    mid_r = (VENT_INNER_RADIUS + VENT_OUTER_RADIUS) * 0.5

    # Front outline radii, bore to edge; the vent band's three stations are exact.
    radii = [
        RIM_PLATE_BORE,
        knee_r,
        VENT_INNER_RADIUS,
        mid_r,
        VENT_OUTER_RADIUS,
        (VENT_OUTER_RADIUS + edge) * 0.5,
        edge,
    ]
    r_in_i, r_out_i = 2, 4
    count = len(radii)

    # Closed outline loop: front sheet bore->edge, edge wall, back sheet
    # edge->bore, bore wall (implicit in the loop closure). Outline index k:
    # 0..count-1 front, count..2*count-1 back (reversed radii).
    outline: list[tuple[float, float]] = []
    for radius in radii:
        outline.append((radius, _plate_front_axial(p, radius)))
    for radius in reversed(radii):
        outline.append((radius, -_plate_front_axial(p, radius)))
    loop = len(outline)

    def back_index(front_index: int) -> int:
        return loop - 1 - front_index

    # Angular stations: per period, spoke first then vent window.
    detail_scale = max(1, context.detail.tire_segments // 32)
    spoke_steps = 3 * detail_scale
    vent_steps = 4 * detail_scale
    period_steps = spoke_steps + vent_steps
    total = VENT_COUNT * period_steps
    period = 2.0 * math.pi / VENT_COUNT
    spoke_arc = period * (1.0 - VENT_ARC_FRACTION)
    angles: list[float] = []
    for vent in range(VENT_COUNT):
        base = vent * period
        for step in range(spoke_steps):
            angles.append(base + spoke_arc * step / spoke_steps)
        for step in range(vent_steps):
            angles.append(
                base + spoke_arc + (period - spoke_arc) * step / vent_steps
            )

    def in_vent(column: int) -> bool:
        return column % period_steps >= spoke_steps

    rings: list[list[bmesh.types.BMVert]] = []
    for radius, along in outline:
        ring = []
        for angle in angles:
            ring.append(
                bm.verts.new(
                    Vector((along, math.cos(angle) * radius, math.sin(angle) * radius))
                )
            )
        rings.append(ring)

    def quad(a: bmesh.types.BMVert, b, c, d) -> None:
        bm.faces.new((a, b, c, d))

    # Sheet faces, matching build.lathe's winding: (lower[s], lower[s+1],
    # upper[s+1], upper[s]) with "upper" the next outline point.
    for k in range(loop):
        k_next = (k + 1) % loop
        front_band = r_in_i <= k < r_out_i
        back_band = back_index(r_out_i) <= k < back_index(r_in_i)
        for a in range(total):
            a_next = (a + 1) % total
            if (front_band or back_band) and in_vent(a):
                continue
            quad(rings[k][a], rings[k][a_next], rings[k_next][a_next], rings[k_next][a])

    # Vent walls. Inner arc at r_in, outer arc at r_out, and the two radial side
    # strips per window; windings chosen so each wall faces into its window.
    f_in, f_out = r_in_i, r_out_i
    b_in, b_out = back_index(r_in_i), back_index(r_out_i)
    for column in range(total):
        if not in_vent(column):
            continue
        a_next = (column + 1) % total
        quad(rings[f_in][a_next], rings[f_in][column], rings[b_in][column], rings[b_in][a_next])
        quad(rings[f_out][column], rings[f_out][a_next], rings[b_out][a_next], rings[b_out][column])
        first = column % period_steps == spoke_steps
        last = (column + 1) % period_steps == 0
        if first:
            for k in range(f_in, f_out):
                quad(
                    rings[k][column],
                    rings[k + 1][column],
                    rings[back_index(k + 1)][column],
                    rings[back_index(k)][column],
                )
        if last:
            for k in range(f_in, f_out):
                quad(
                    rings[k + 1][a_next],
                    rings[k][a_next],
                    rings[back_index(k)][a_next],
                    rings[back_index(k + 1)][a_next],
                )

    # Rings strictly inside the vent band (the mid station, front and back) have
    # no faces at window-interior stations -- the sheet is skipped there and the
    # side walls only touch the window's two boundary stations. Left in place
    # they export as loose vertices, so they go.
    orphans = [
        rings[k][a]
        for k in (f_in + 1, back_index(f_in + 1))
        for a in range(total)
        if a % period_steps > spoke_steps
    ]
    bmesh.ops.delete(bm, geom=orphans, context="VERTS")


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


def _disc_face(
    context: build.BuildContext,
    bm: bmesh.types.BMesh,
    plane: float,
    outer_radius: float,
    thickness: float,
    *,
    bore_radius: float,
    lug_count: int = 0,
    lug_inner_radius: float = 0.0,
    lug_half_angle: float = 0.0,
    lug_bolt_radius: float = 0.0,
    lug_bolt_diameter: float = 0.0,
) -> None:
    """A ventilated brake disc as one closed shell: two plates, a cavity, pillars.

    Same construction as `_rim_plate_vented` and for the same two reasons -- a
    through-hole cannot come out of a lathe, and a boolean is banned by the
    determinism rules. A closed outline in (radius, x) is revolved into a grid of
    quads, faces are simply not emitted where a feature removes material, and the
    resulting openings are stitched with walls. Every edge ends with exactly two
    faces, so the winding gate's watertightness check covers the features instead
    of skipping them.

    The outline is a **C**, not a rectangle, because the disc is ventilated:

        bore   +6 ------------------------------------------ rim, chamfered
                                          +2.5 ----------- cavity mouth, open
                  (solid to 0.60 R)       -2.5 -----------
        bore   -6 ------------------------------------------

    so the plate is 12 mm overall -- sourced, three forms -- while being two 3.5 mm
    friction plates around a 5 mm cavity that is open at the rim and closed inboard
    of the pads. See `DISC_PLATE_THICKNESS` for why the old solid reading was wrong.

    **Four feature families, and one mechanism serves all of them.** A window is a
    set of grid cells removed from *two* facing sheets at once, plus walls joining
    those sheets around its boundary:

        drilling   removes outer face and plate inner face -> a tunnel to the cavity
        bolt hole  removes front face and back face        -> through the solid hub
        pillar     removes the two CAVITY faces            -> leaves a column standing

    The pillar is the same operation read backwards, which is why it costs no extra
    code: taking the cavity's own two surfaces away and joining them leaves material
    exactly where the cavity used to be.

    **What makes the pattern affordable.** Four families sit at four radii and
    overlap in angle -- the groove leans 11.2 degrees across its length and passes
    between the two drilled rings, so at some radii a drilling and the groove want
    the same station. A single fixed angular grid cannot express that, and it does
    not have to: each radius places its own station angles, because the faces are
    flat annuli and moving a vertex around its own ring changes no surface at all,
    only where a boundary lands. At the two radii bounding a drilling its stations
    sit exactly on that hole's edge; everywhere else the same stations are filler
    and get pushed clear. The quads that result are skewed and coplanar, which is
    invisible, and every feature edge is an exact station at both detail levels.

    The lugs and the grooves come out of the same freedom in the other two
    coordinates. The bore ring's *radius* varies by station, so the bore is
    scalloped with three tabs. The front and back faces' *x* varies by station, so
    a groove is a blind channel milled into the face -- which is what the
    photographs show, a lit floor and a tapered end, and **not** a slot through the
    plate. Building them as through-slots was this pass's own first mistake.

    #212 lost two features to being shaded smooth into invisibility, so the walls
    here are checked by dihedral rather than by eye.
    """
    detail = context.detail
    before = (len(bm.verts), len(bm.edges), len(bm.faces))
    half = thickness * 0.5
    cavity = half - DISC_PLATE_THICKNESS
    chamfer = DISC_RIM_CHAMFER
    hole_a = DISC_HOLE_DIAMETER * 0.5
    bolt_a = lug_bolt_diameter * 0.5
    groove_half = DISC_SLOT_WIDTH * 0.5
    assert cavity > 0.0, "plates thicker than the disc"

    # **A disc with no tabs carries none of the tab's machinery.** The rear ring
    # floats on bobbins and takes its torque through the carrier, so it has no drive
    # lugs and no bolt holes -- and the two radii that bracket a bolt hole and the
    # three stations that place a tab edge then bound nothing there. They were built
    # anyway, as filler: 4 outline points and 3 stations per period, 1,440 vertices
    # of surface that no feature sits on, on a part that with its two mates is a
    # fifth of the kart's whole vertex count. Dropped when `lug_count` is zero. The
    # shape does not move -- every feature edge is still its own locked station and
    # the annuli between them are still flat.
    radius_of = {
        "bore": lug_inner_radius if lug_count else bore_radius,
        "vent": DISC_VENT_INNER_FRAC * outer_radius,
        "g_in": DISC_SLOT_INNER_FRAC * outer_radius,
        "hin_lo": DISC_HOLE_INNER_FRAC * outer_radius - hole_a,
        "hin_hi": DISC_HOLE_INNER_FRAC * outer_radius + hole_a,
        "hout_lo": DISC_HOLE_OUTER_FRAC * outer_radius - hole_a,
        "hout_hi": DISC_HOLE_OUTER_FRAC * outer_radius + hole_a,
        "g_out": DISC_SLOT_OUTER_FRAC * outer_radius,
        "rim_in": outer_radius - chamfer,
        "rim": outer_radius,
    }
    if lug_count:
        radius_of["bolt_lo"] = lug_bolt_radius - bolt_a
        radius_of["bolt_hi"] = lug_bolt_radius + bolt_a

    order = ["bore"]
    if lug_count:
        order += ["bolt_lo", "bolt_hi"]
    order += ["vent", "g_in", "hin_lo", "hin_hi",
              "hout_lo", "hout_hi", "g_out", "rim_in", "rim"]
    for lo, hi in zip(order, order[1:]):
        assert radius_of[lo] < radius_of[hi], (
            "disc radii must increase: %s %.4f then %s %.4f"
            % (lo, radius_of[lo], hi, radius_of[hi])
        )
    if lug_count:
        assert bore_radius < radius_of["bolt_lo"], (
            "the scalloped bore at %.4f reaches past the bolt band at %.4f"
            % (bore_radius, radius_of["bolt_lo"])
        )

    # --- angular stations --------------------------------------------------
    # Sixteen per period carry a feature edge, in this order -- thirteen with no
    # tabs. Fillers scale with detail and go in the gaps; feature stations are
    # identical at both densities because the surface between them is flat.
    # `lug_a_out` stays whatever the disc is: it is station 0, the period's own
    # anchor, and on a lugged disc it also happens to be the outer edge of the tab's
    # side wall.
    ROLES = ["lug_a_out"]
    if lug_count:
        ROLES.append("lug_a")
    ROLES += ["hout_l", "hout_m", "hout_h",
              "groove_l", "groove_l2", "groove_h2", "groove_h",
              "hin_l", "hin_m", "hin_h"]
    if lug_count:
        ROLES += ["lug_b", "lug_b_out"]
    ROLES += ["pillar_l", "pillar_h"]
    ROLES = tuple(ROLES)
    ROLE = {name: index for index, name in enumerate(ROLES)}
    # A gap is a station that fillers go *after*. The one opening the period is
    # `lug_a` where there is a tab and station 0 itself where there is not, so the
    # leading gap keeps its filler either way and high detail stays even.
    GAP_NAMES = ["hout_h", "groove_h", "hin_h", "pillar_h",
                 "lug_a" if lug_count else "lug_a_out"]
    if lug_count:
        GAP_NAMES.append("lug_b_out")
    GAPS = frozenset(ROLE[name] for name in GAP_NAMES)
    fillers = max(0, detail.tire_segments // 32 - 1)
    per_period = len(ROLES) + len(GAPS) * fillers
    period_angle = 2.0 * math.pi / DISC_SLOT_COUNT
    lug_period = DISC_SLOT_COUNT // lug_count if lug_count else 0
    total = DISC_SLOT_COUNT * per_period
    if not lug_count:
        lug_half_angle = period_angle * 0.30
    wall = period_angle * 0.030
    offsets: dict[str, int] = {}
    cursor = 0
    for index, name in enumerate(ROLES):
        offsets[name] = cursor
        cursor += 1 + (fillers if index in GAPS else 0)

    def is_lug_period(period: int) -> bool:
        return bool(lug_count) and period % lug_period == 0

    # Which stations a given radius genuinely bounds. Everything else there is
    # filler and gets spread evenly between the locked ones, which is what keeps a
    # single ascending order at every radius without moving a feature edge. The
    # alternative -- computing all sixteen from their own geometry -- puts a
    # drilling's hexagon 0.3 rad wide at the bore, where it means nothing, and the
    # order collapses.
    #
    # **The bore is the exception, and it is the whole reason the two bolt bands
    # lock the lug stations too.** That freedom rests on the ring being a flat
    # annulus, and the bore's radius *varies by station* -- it is at the tab on a
    # lug period and at the scallop everywhere else. A ring that steps radially is
    # not flat, so its neighbor in the outline has to carry the same station angles
    # across the step or the ribbon between them folds over itself. It did: with
    # `bolt_lo` free, its `lug_b` sat 2.4 degrees ahead of the bore's while the
    # stations there are 1.2 degrees apart, and the quad on the tab's trailing edge
    # came out a **bowtie** -- self-intersecting, signed area negative, normal
    # reversed, a 180.0 degree dihedral against both its neighbors. Eighteen of
    # them, three tabs by three edges by two plates, and a render shows nothing
    # because the disc is watertight and every other face is right.
    GROOVE_STATIONS = ("groove_l", "groove_l2", "groove_h2", "groove_h")
    LUG_STATIONS = ("lug_a", "lug_b", "lug_b_out") if lug_count else ()
    LOCKS = {
        "bore": LUG_STATIONS,
        "vent": (),
        "g_in": GROOVE_STATIONS,
        "hin_lo": GROOVE_STATIONS + ("hin_l", "hin_m", "hin_h", "pillar_l", "pillar_h"),
        "hin_hi": GROOVE_STATIONS + ("hin_l", "hin_m", "hin_h", "pillar_l", "pillar_h"),
        "hout_lo": GROOVE_STATIONS + ("hout_l", "hout_m", "hout_h", "pillar_l", "pillar_h"),
        "hout_hi": GROOVE_STATIONS + ("hout_l", "hout_m", "hout_h",
                                     "pillar_l", "pillar_h"),
        "g_out": GROOVE_STATIONS,
        "rim_in": (),
        "rim": (),
    }
    if lug_count:
        LOCKS["bolt_lo"] = LUG_STATIONS + ("groove_l2", "groove_h2")
        LOCKS["bolt_hi"] = LUG_STATIONS + ("groove_l2", "groove_h2")

    def feature_angles(radius: float, bolt_band: bool) -> list[float]:
        """Where each active station wants to be, relative to the period center, if
        this radius were the one that bounds it.

        Keyed by name and projected onto `ROLES` at the end, so a station the disc
        does not have simply is not asked for -- the list this returns is positional
        and a silent shift in it would move every feature at once."""
        if bolt_band:
            # These two radii bound a lug's bolt hole, so the middle group is that
            # hole rather than the groove -- the groove has not started this far in.
            core, core_w, lip = 0.0, bolt_a / radius, 0.0
        else:
            span = (radius - radius_of["g_in"]) / (
                radius_of["g_out"] - radius_of["g_in"]
            )
            core = DISC_SLOT_SWEEP * (min(1.0, max(0.0, span)) - 0.5)
            core_w = groove_half / radius
            lip = core_w * 0.26
        hex_w = 0.866 * hole_a / radius
        gap = period_angle * 0.012
        pillar_lo = lug_half_angle + wall + gap * 2.0
        want = {
            "lug_a_out": -(lug_half_angle + wall),
            "lug_a": -lug_half_angle,
            "hout_l": DISC_HOLE_CLOCK_OUTER - hex_w,
            "hout_m": DISC_HOLE_CLOCK_OUTER,
            "hout_h": DISC_HOLE_CLOCK_OUTER + hex_w,
            "groove_l": core - core_w - lip,
            "groove_l2": core - core_w,
            "groove_h2": core + core_w,
            "groove_h": core + core_w + lip,
            "hin_l": DISC_HOLE_CLOCK_INNER - hex_w,
            "hin_m": DISC_HOLE_CLOCK_INNER,
            "hin_h": DISC_HOLE_CLOCK_INNER + hex_w,
            "lug_b": lug_half_angle,
            "lug_b_out": lug_half_angle + wall,
            "pillar_l": pillar_lo,
            "pillar_h": pillar_lo + DISC_PILLAR_WIDTH / radius,
        }
        return [want[name] for name in ROLES]

    def ring_stations(key: str) -> list[tuple[float, bool]]:
        """(angle, on a lug tab) for every station at one radius, in order."""
        radius = radius_of[key]
        bolt_band = bool(lug_count) and key in ("bolt_lo", "bolt_hi")
        want = feature_angles(radius, bolt_band)
        lower = want[0]
        upper = lower + period_angle
        # Station 0 anchors the period; the rest are anchors only where this radius
        # bounds them.
        anchors = [(0, lower)]
        for name in LOCKS[key]:
            anchors.append((ROLE[name], want[ROLE[name]]))
        anchors.sort()
        for (i0, v0), (i1, v1) in zip(anchors, anchors[1:]):
            assert v0 < v1, (
                "locked stations collide at %s: %s %.5f then %s %.5f"
                % (key, ROLES[i0], v0, ROLES[i1], v1)
            )
        assert anchors[-1][1] < upper, "locked station past the period at %s" % key

        placed = [0.0] * len(ROLES)
        bounded = anchors + [(len(ROLES), upper)]
        for (i0, v0), (i1, v1) in zip(bounded, bounded[1:]):
            placed[i0] = v0
            free = i1 - i0 - 1
            for step in range(free):
                placed[i0 + 1 + step] = v0 + (v1 - v0) * (step + 1) / (free + 1)

        out: list[tuple[float, bool]] = []
        for period in range(DISC_SLOT_COUNT):
            center = (period + 0.5) * period_angle
            lug_here = is_lug_period(period)
            after = center + period_angle + lower
            for index, angle in enumerate(placed):
                # lug_a through lug_b inclusive stand on the tab; the two _out
                # stations sit just off it, which is what puts the tab's side face
                # on a real edge instead of a ramp.
                on_tab = lug_here and ROLE["lug_a"] <= index <= ROLE["lug_b"]
                out.append((center + angle, on_tab))
                if index in GAPS:
                    end = (center + placed[index + 1]) if index + 1 < len(placed) else after
                    for step in range(fillers):
                        share = (step + 1) / (fillers + 1)
                        out.append(
                            (center + angle + (end - center - angle) * share, on_tab)
                        )
        assert len(out) == total, "%d stations, expected %d" % (len(out), total)
        for index in range(total - 1):
            assert out[index][0] < out[index + 1][0], (
                "stations out of order at %s, station %d: %.5f then %.5f"
                % (key, index, out[index][0], out[index + 1][0])
            )
        return out

    stations = {key: ring_stations(key) for key in radius_of}

    # The guard for the fold above, written against the property rather than the
    # symptom: wherever the bore's radius steps between two neighboring stations,
    # the ring outboard of it must sit at those same two angles. A degenerate or
    # reversed quad is not something the winding gate can see -- the shell stays
    # watertight and its total signed volume stays positive -- so it is checked here.
    if lug_count:
        bore_ring, next_ring = stations["bore"], stations["bolt_lo"]
        for a in range(total):
            b = (a + 1) % total
            if bore_ring[a][1] == bore_ring[b][1]:
                continue  # no step across this cell
            for index in (a, b):
                assert abs(bore_ring[index][0] - next_ring[index][0]) < 1e-12, (
                    "the bore steps radius at station %d and bolt_lo is %.5f rad "
                    "away, so the quad between them folds"
                    % (index, bore_ring[index][0] - next_ring[index][0])
                )

    # --- the outline -------------------------------------------------------
    # (radius key, x, groove side, pass). Groove side +1/-1 marks a face the blind
    # grooves are milled into; 0 leaves x alone. The pass -- front face, cavity
    # front, cavity back, back face -- exists because four radii appear on all four
    # of them and the windows below have to name one: a segment index is what a
    # window is addressed by, and hardcoded integers were a landmine the moment two
    # points left the list for the rear disc.
    outline: list[tuple[str, float, int, str]] = [("bore", half, 0, "f")]
    if lug_count:
        outline += [("bolt_lo", half, 0, "f"), ("bolt_hi", half, 0, "f")]
    outline += [
        ("vent", half, 0, "f"), ("g_in", half, 0, "f"),
        ("hin_lo", half, 1, "f"), ("hin_hi", half, 1, "f"),
        ("hout_lo", half, 1, "f"), ("hout_hi", half, 1, "f"),
        ("g_out", half, 0, "f"), ("rim_in", half, 0, "f"),
        ("rim", half - chamfer, 0, "f"),
        ("rim", cavity, 0, "cf"),
        ("hout_hi", cavity, 0, "cf"), ("hout_lo", cavity, 0, "cf"),
        ("hin_hi", cavity, 0, "cf"), ("hin_lo", cavity, 0, "cf"),
        ("vent", cavity, 0, "cf"),
        ("vent", -cavity, 0, "cb"),
        ("hin_lo", -cavity, 0, "cb"), ("hin_hi", -cavity, 0, "cb"),
        ("hout_lo", -cavity, 0, "cb"), ("hout_hi", -cavity, 0, "cb"),
        ("rim", -cavity, 0, "cb"),
        ("rim", -half + chamfer, 0, "b"), ("rim_in", -half, 0, "b"),
        ("g_out", -half, 0, "b"),
        ("hout_hi", -half, -1, "b"), ("hout_lo", -half, -1, "b"),
        ("hin_hi", -half, -1, "b"), ("hin_lo", -half, -1, "b"),
        ("g_in", -half, 0, "b"), ("vent", -half, 0, "b"),
    ]
    if lug_count:
        outline += [("bolt_hi", -half, 0, "b"), ("bolt_lo", -half, 0, "b")]
    outline += [("bore", -half, 0, "b")]
    loop = len(outline)

    # Where each (radius, pass) landed. A window names its segments through this
    # rather than by number, so dropping the bolt bands shifts nothing.
    at: dict[tuple[str, str], int] = {}
    for index, (key, _x, _g, tag) in enumerate(outline):
        assert (key, tag) not in at, (
            "outline has %s twice on pass %s, so a window addressed by name would "
            "take the second one silently" % (key, tag)
        )
        at[(key, tag)] = index

    def groove_floor(a: int) -> bool:
        """Is this station between a groove's two inner edges?"""
        return offsets["groove_l2"] <= a % per_period < offsets["groove_h2"]

    verts: list[list[bmesh.types.BMVert]] = []
    for key, along, groove, _tag in outline:
        ring: list[bmesh.types.BMVert] = []
        for a, (angle, on_tab) in enumerate(stations[key]):
            radius = radius_of[key]
            if key == "bore" and lug_count and not on_tab:
                radius = bore_radius
            x = along
            if groove and groove_floor(a):
                x -= groove * DISC_GROOVE_DEPTH
            ring.append(
                bm.verts.new(
                    Vector((plane + x, radius * math.cos(angle), radius * math.sin(angle)))
                )
            )
        verts.append(ring)

    # --- features ----------------------------------------------------------
    skip: set[tuple[int, int]] = set()
    walls: list[tuple] = []

    def window(seg_a: int, seg_b: int, lo_a: int, lo_b: int, hi_a: int, hi_b: int,
               cells: list[int]) -> None:
        """Remove `cells` from two facing sheets and stitch the opening shut.

        `seg_a`/`seg_b` are the outline segments carrying those sheets; `lo_*` and
        `hi_*` are the outline points at the feature's two radii on each sheet.
        """
        for a in cells:
            skip.add((seg_a, a))
            skip.add((seg_b, a))
        for a in cells:
            b = (a + 1) % total
            walls.append((verts[lo_a][a], verts[lo_b][a], verts[lo_b][b], verts[lo_a][b]))
            walls.append((verts[hi_a][b], verts[hi_b][b], verts[hi_b][a], verts[hi_a][a]))
        first, last = cells[0], (cells[-1] + 1) % total
        walls.append((verts[lo_a][first], verts[hi_a][first],
                      verts[hi_b][first], verts[lo_b][first]))
        walls.append((verts[lo_b][last], verts[hi_b][last],
                      verts[hi_a][last], verts[lo_a][last]))

    # The outline is a closed loop, so two of its four passes run inward to outward
    # and two run back. A feature's segment on a given pass is therefore named by
    # whichever of its two radii that pass reaches first, and every window pairs one
    # pass of each direction.
    INWARD_OUT = ("f", "cb")

    def bore_through(lo: str, hi: str, sheet: str, floor: str,
                     cells: list[int]) -> None:
        """One feature between the `sheet` pass and the `floor` pass, at the two
        radii `lo` and `hi`."""

        def first_on(tag: str) -> str:
            return lo if tag in INWARD_OUT else hi

        assert (sheet in INWARD_OUT) != (floor in INWARD_OUT), (
            "a window needs one pass of each direction; %s and %s run the same way"
            % (sheet, floor)
        )
        window(at[(first_on(sheet), sheet)], at[(first_on(floor), floor)],
               at[(lo, sheet)], at[(lo, floor)],
               at[(hi, sheet)], at[(hi, floor)], cells)

    for period in range(DISC_SLOT_COUNT):
        base = period * per_period
        hin_cells = [base + offsets["hin_l"], base + offsets["hin_m"]]
        hout_cells = [base + offsets["hout_l"], base + offsets["hout_m"]]
        pillar_cells = [base + offsets["pillar_l"]]
        # drillings: outer face -> plate inner face, both plates
        bore_through("hin_lo", "hin_hi", "f", "cf", hin_cells)
        bore_through("hout_lo", "hout_hi", "f", "cf", hout_cells)
        bore_through("hin_lo", "hin_hi", "b", "cb", hin_cells)
        bore_through("hout_lo", "hout_hi", "b", "cb", hout_cells)
        # pillars: the cavity's own two faces, so material is left standing
        bore_through("hin_lo", "hin_hi", "cf", "cb", pillar_cells)
        if DISC_PILLAR_PER_SECTOR > 1:
            bore_through("hout_lo", "hout_hi", "cf", "cb", pillar_cells)
        # one bolt hole per tab, straight through the solid inner section
        if is_lug_period(period):
            bolt_cells = [base + offsets["groove_l2"]]
            bore_through("bolt_lo", "bolt_hi", "f", "b", bolt_cells)

    # --- faces -------------------------------------------------------------
    for o in range(loop):
        o_next = (o + 1) % loop
        for a in range(total):
            b = (a + 1) % total
            if (o, a) in skip:
                continue
            bm.faces.new((verts[o][a], verts[o_next][a], verts[o_next][b], verts[o][b]))
    for quad in walls:
        bm.faces.new(quad)

    # --- the part counts its own holes -------------------------------------
    # #214's acceptance line, and the answer to `DISC_FRONT_TANG_COUNT`'s old
    # docstring: a claim about the pattern that the mesh does not carry is caught
    # here rather than believed. Euler characteristic over a closed shell is
    # `2 - 2*genus`, and every tunnel through the shell is one handle.
    #
    # **A drilling is two handles, not one, and that is the honest count.** Each of
    # the eighteen drilled positions is bored through the *front* plate into the
    # cavity and again through the *back* plate, and the cavity is open at the rim,
    # so outside and cavity are one connected region and each bore is its own
    # tunnel. Reading the two rings of nine as eighteen handles is what predicted
    # genus 40 against a measured 58 -- off by exactly the eighteen back-plate
    # bores. The pillars are the same operation inverted and each still costs a
    # handle: taking the cavity's two faces away and joining them leaves a column,
    # and a column standing in a cavity is a handle in the shell around it.
    handles = (
        1                                          # the bore
        + DISC_SLOT_COUNT * 2 * 2                  # two drilled rings, both plates
        + DISC_SLOT_COUNT * DISC_PILLAR_PER_SECTOR  # vent pillars
        + lug_count                                # one bolt hole per tab
    )
    grew = (
        len(bm.verts) - before[0],
        len(bm.edges) - before[1],
        len(bm.faces) - before[2],
    )
    chi = grew[0] - grew[1] + grew[2]
    assert chi == 2 - 2 * handles, (
        "the disc built Euler characteristic %+d, which is genus %.1f, against the "
        "%d holes this builder claims -- a feature is doubled, missing, or the "
        "claim is wrong" % (chi, (2 - chi) / 2.0, handles)
    )


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
    alloy = context.material("engine_cast")

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
    material = context.material("engine_cast")

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
    alloy = context.material("engine_cast")

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
    material = context.material("engine_cast")

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
    alloy = context.material("engine_cast")

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
    *,
    piston_bore: float = CALIPER_REAR_PISTON_BORE,
    detail: build.Detail | None = None,
) -> None:
    """The **rear** caliper: two waisted finned halves, a bridge, bosses, fittings.

    **This builder is the rear's alone.** The front is `_caliper_front_body`, built
    against a different maker's form -- ADR-0067 puts a Freeline front on a CRG
    rear, so nothing here is shared by accident. Until that split lands,
    `_front_brakes` still calls this, which is why every dimension below comes off
    the arguments and only the piston bore has a rear default.

    **Not a solid box, and that is geometry rather than detail.** A solid body of
    the drawing's `thickness` spans the disc's own plane, so it encloses whatever
    else sits at that plane inside its radial band -- measured, the rear one
    swallowed 9 mm of `brake_disc_rear_carrier` and the front one 6.5 mm of the
    friction ring. A real caliper is a C: two halves either side of the disc, each
    carrying a piston, joined by a bridge over the disc's outer edge. Built that
    way the caliper touches the disc **only through its pads**, which is what
    `joints.py` declares, and the bridge is what leaves that true.

    Each half's thickness is `derived` from the parts rather than authored:
    `(thickness - disc - 2 x pad) / 2` is 18.75 mm at the rear, against the ~19 mm
    cylinder wall spec §20.6.7 builds the 74 out of. That 18.75 is now spent
    **12.75 of slab plus a 6 mm cylinder boss**, so the wall is still 18.75 where
    the piston is and the envelope's 74 is still the envelope: nothing on this part
    stands proud of `CALIPER_REAR_THICKNESS` except the two fittings, which are
    plumbing and are not body.

    Shape, and what it was read off. Two references, in this order:

    * `82/FR/11` p. 4's *Etriers* photograph is **this caliper**, dead-on, at its
      own homologation number -- the silver one; the black one beside it is the
      4-piston front this kart does not have. Its outline is an hourglass: full
      radial height at the two ends, pinched in the middle on both edges, with
      fastener ears at all four corners and the mouth's jaws scalloped around the
      pistons. Ratios only, and coarse ones: the part is 140 px tall in a 227 x 174
      thumbnail, measured by thresholding the JPEG `pdfimages` pulls out of the
      form, which gives a core body of 1.69 tangential-to-radial and a waist that
      takes 25% out of the middle.
    * `007-BRKR-10` p. 2 item 3 is a *Freeline* rear on a Ø150 disc, so its
      **absolute dimensions belong to another part** and only its proportions and
      its feature inventory are usable here -- 0.5291 mm/px, 7% error bar. What it
      gives: two M8 socket screws on a 66 mm pitch lying on the body's own
      centre-line at +-33 mm, one cylinder circle per half rather than two, and a
      mouth 33 mm deep whose closed end sits within 1.3 mm of the piston's centre.
      That last agreement is what identifies the big circle as the **cylinder**
      boss and not the central through-boss `joints.py` reads it as.

    The waist is 4.0 mm on the outer edge and 3.5 on the inner, both `estimated`,
    and it is one smooth pinch rather than the photograph's scalloped jaws: the
    photograph's own pinch is 25% of the body height and this envelope cannot have
    it. `CALIPER_REAR_HEIGHT` is 55 against a Ø38.7 cylinder boss, so the boss
    already eats 70% of the height where the real part's eats 48%, and a 25% waist
    would leave 1.2 mm of aluminium around the bore. 14% is what fits, and the
    assert below is what will say so if either number moves. **The 55 is the
    figure to doubt, not the waist** -- read off `82/FR/11` p. 4 the body is 1.69
    long-to-radial and this envelope is 2.51.

    Fins, `estimated` at 8 blades: the caliper is externally finned across the top
    for cooling -- `tonykart_racer401T_p03.jpg` is the clearest of them, a comb of
    blades at roughly 10 mm pitch standing off the body. They are cut *into* the
    envelope rather than added onto it: the bridge's own outer 6 mm is the fin
    band, so the top of the caliper is still `radius + height/2` and the comb is
    real geometry rather than a groove that shades away. #212's cast webs at 5 mm
    are the reason the blades are 7 mm thick and 6 proud -- `bevel_object` spends
    4 mm at high detail, and a feature thinner than twice that comes back rounded
    to nothing and reads as the plain part it replaced.
    """
    steps = max(4, (detail.bend_segments if detail is not None else 6) // 2)
    rotation = Matrix.Rotation(-clock, 4, "X")
    # Full `tube_segments` rather than the half the bobbins get: at 6 the Ø38.7
    # cylinder boss is a **hexagon**, and it is the largest single feature on the
    # face -- rendered, it read as a machined hex plate rather than a bore.
    segments = max(12, detail.tube_segments if detail is not None else 12)

    # --- the axial stack, from the disc's plane outward --------------------
    half_each = (thickness - disc_thickness - 2.0 * PAD_THICKNESS) * 0.5
    mouth = disc_thickness * 0.5 + PAD_THICKNESS   # each half's inner face
    face = mouth + half_each                       # and its outer, = the envelope
    proud = CALIPER_FRONT_BOSS_PROUD
    slab = face - proud
    assert slab - mouth > 0.005, (
        "the slab is %.1f mm thick once the boss takes its %.1f -- there is no "
        "cylinder wall left" % ((slab - mouth) * 1000.0, proud * 1000.0)
    )

    # The boss's wall is `derived` from the two front constants that were measured
    # together on one plate: a Ø31.7 boss around a Ø25 bore is 3.35 mm of aluminium,
    # and a wall does not scale with the bore it surrounds -- it is what the
    # casting needs. Ø32 + 2 x 3.35 = 38.7 at the rear.
    wall = (CALIPER_FRONT_BOSS_DIAMETER - CALIPER_FRONT_PISTON_BORE) * 0.5
    boss_radius = piston_bore * 0.5 + wall

    # --- the outline, in (tangential, radial) about the pad's mean radius ---
    a = length * 0.5
    h = height * 0.5
    cap = length * 0.25          # the elliptical ends' tangential semi-axis
    straight = a - cap
    waist_outer = height * 0.073
    waist_inner = height * 0.064
    assert h - waist_outer - boss_radius > 0.003, (
        "the waist leaves %.1f mm of body around a Ø%.1f cylinder boss"
        % ((h - waist_outer - boss_radius) * 1000.0, boss_radius * 2000.0)
    )

    def to_world(u: float, v: float, w: float) -> tuple[float, float, float]:
        """(axial, tangential, radial) about the caliper's own centre -> world.

        The part is clocked, so **no measurement of it may be taken along a world
        axis**: `max(z)` over a body tipped 20 degrees is a corner, not the top.
        Everything below is authored in this frame and mapped once.
        """
        return (
            plane + u,
            axle_y + (radius + w) * math.sin(clock) + v * math.cos(clock),
            axle_z + (radius + w) * math.cos(clock) - v * math.sin(clock),
        )

    def bump(v: float) -> float:
        """1 at mid-length, 0 where the end caps begin, and C1 at both."""
        if abs(v) >= straight:
            return 0.0
        return 0.5 * (1.0 + math.cos(math.pi * v / straight))

    # Walked counter-clockwise in (v, w) from the leading tip, which is what makes
    # the prism's side quads face outward -- see `prism`.
    outline: list[tuple[float, float]] = []
    for step in range(steps):
        angle = 0.5 * math.pi * step / steps
        outline.append((straight + cap * math.cos(angle), h * math.sin(angle)))
    for step in range(2 * steps + 1):
        v = straight * (1.0 - step / steps)
        outline.append((v, h - waist_outer * bump(v)))
    for step in range(1, 2 * steps):
        angle = math.pi * (0.5 + step / (2 * steps))
        outline.append((-straight + cap * math.cos(angle), h * math.sin(angle)))
    for step in range(2 * steps + 1):
        v = straight * (step / steps - 1.0)
        outline.append((v, -(h - waist_inner * bump(v))))
    for step in range(1, steps):
        angle = 1.5 * math.pi + 0.5 * math.pi * step / steps
        outline.append((straight + cap * math.cos(angle), h * math.sin(angle)))

    # The two fan hubs sit on the axis, so the outline has to be star-shaped about
    # it or a cap triangle folds. It is, by construction -- the waist is a fraction
    # of the half-height and the caps are ellipses about the same centre -- and
    # this is the assert that says so if the waist is ever authored past it. Same
    # family as #214's bowtie: a fold has positive area from one side and is
    # invisible to every other check.
    for (v0, w0), (v1, w1) in zip(outline, outline[1:] + outline[:1]):
        assert v0 * w1 - v1 * w0 > 0.0, (
            "the caliper outline folds between (%.4f, %.4f) and (%.4f, %.4f)"
            % (v0, w0, v1, w1)
        )

    def prism(u0: float, u1: float) -> None:
        """Extrude the outline between two axial stations, capped by fans.

        `u1 > u0` in world x and the outline is counter-clockwise in (v, w), which
        together fix the winding: the side quad's normal comes out as the outline
        edge's own outward 2D normal, and the caps take the two axial directions.
        """
        assert u1 > u0, "prism stations out of order"
        low = [bm.verts.new(to_world(u0, v, w)) for v, w in outline]
        high = [bm.verts.new(to_world(u1, v, w)) for v, w in outline]
        count = len(outline)
        for index in range(count):
            following = (index + 1) % count
            bm.faces.new((low[index], low[following], high[following], high[index]))
        hub_low = bm.verts.new(to_world(u0, 0.0, 0.0))
        hub_high = bm.verts.new(to_world(u1, 0.0, 0.0))
        for index in range(count):
            following = (index + 1) % count
            bm.faces.new((hub_low, low[following], low[index]))
            bm.faces.new((hub_high, high[index], high[following]))

    def stud(u0: float, u1: float, v: float, w: float, diameter: float,
             sides: int = 0) -> None:
        """A boss on the axis through (v, w). `sides=6` is a hex across flats."""
        count = sides if sides else segments
        # A swept circle's radius is its circumradius, so a hex quoted across the
        # flats is 2/sqrt(3) bigger than the flat spacing suggests.
        outer = diameter * 0.5 * (2.0 / math.sqrt(3.0) if sides == 6 else 1.0)
        build.sweep_tube(bm, [to_world(u0, v, w), to_world(u1, v, w)], outer, count)

    # --- the two halves, and the cylinder boss on each ----------------------
    # The slab is the waisted prism; the boss is a cylinder standing on it out to
    # the envelope's own face, centred on the piston -- **one per half**, because
    # `82/FR/11` reads 2 pistons per wheel and this is an opposed caliper. Two
    # circles per half is the 4-piston front, which is a different part.
    for sign in (-1.0, 1.0):
        prism(*sorted((sign * mouth, sign * slab)))
        stud(*sorted((sign * slab, sign * face)), 0.0, 0.0, boss_radius * 2.0)

    # --- the bridge over the disc's rim, and the fins on top of it ----------
    # Inner face at `radius + height/2 - 0.010`, which is 2.5 mm clear of a rear
    # disc that reaches `pad_outer/2` -- measured on the built mesh at 2.50 mm, and
    # it is the number that keeps `joints.py`'s pads-only claim true. The outer
    # 6 mm of the envelope is the fin band, so the bridge is the 4 mm between them.
    bridge_lo = h - 0.010
    bridge_hi = h - 0.006
    build.box(
        bm,
        (2.0 * slab, length * 0.8, bridge_hi - bridge_lo),
        to_world(0.0, 0.0, (bridge_lo + bridge_hi) * 0.5),
        rotation=rotation,
    )
    fin_count = 8
    fin_span = length * 0.75
    fin_pitch = fin_span / fin_count
    fin_thickness = 0.007
    assert fin_thickness < fin_pitch, "the fins meet and there is no comb"
    for index in range(fin_count):
        v = fin_span * ((index + 0.5) / fin_count - 0.5)
        build.box(
            bm,
            (2.0 * slab, fin_thickness, h - bridge_hi),
            to_world(0.0, v, (bridge_hi + h) * 0.5),
            rotation=rotation,
        )

    # --- the two mounting bolts --------------------------------------------
    # M8 socket screws at the sourced 66 mm pitch, but carried **out to the
    # bridge**. `007-BRKR-10` draws them on the body's own centre-line, which on
    # this envelope is r 82.5 -- and a bolt there would have to cross the disc,
    # which reaches to within 12.5 mm of the outer edge. At the bridge's r 102 the
    # bolt is 4.5 mm outboard of the rim; its Ø13 head still dips 2 mm below it,
    # which is nothing, because the head sits 22 mm off the disc's plane. The
    # bracket's arm reaches r 108, so the joint is real at that radius.
    bolt_w = (bridge_lo + bridge_hi) * 0.5
    for sign in (-1.0, 1.0):
        for side in (-1.0, 1.0):
            stud(*sorted((side * slab, side * face)),
                 sign * CALIPER_MOUNT_BOLT_PITCH * 0.5, bolt_w,
                 CALIPER_MOUNT_BOLT_HEAD_DIAMETER)

    # --- the plumbing, both on the inboard half -----------------------------
    # `007-BRKF-01` p. 4 has the nipple and the banjo on the same half, and this is
    # the half the hose can reach: `brake_line_rear` arrives from **inboard** along
    # the left rail and ends at local (v +16.6, w +3.4), which is inside the
    # cylinder boss's own footprint. The banjo is set just clear of it at v +25.
    # The nipple goes at v -40 because the caliper is tipped 20 degrees forward, so
    # the trailing end is the high one and that is where the air collects.
    inboard = 1.0 if plane < 0.0 else -1.0
    banjo_v, banjo_w = 0.025, 0.006
    stud(*sorted((inboard * slab, inboard * (slab + CALIPER_FRONT_BANJO_LENGTH))),
         banjo_v, banjo_w, CALIPER_FRONT_BANJO_DIAMETER)
    stud(*sorted((inboard * slab, inboard * (slab + 0.003))),
         banjo_v, banjo_w, CALIPER_MOUNT_BOLT_HEAD_DIAMETER)
    nipple_v, nipple_w = -0.040, 0.014
    hex_length = CALIPER_FRONT_NIPPLE_LENGTH * (2.0 / 3.0)
    stud(*sorted((inboard * slab, inboard * (slab + hex_length))),
         nipple_v, nipple_w, CALIPER_FRONT_NIPPLE_HEX, sides=6)
    stud(*sorted((inboard * (slab + hex_length),
                  inboard * (slab + CALIPER_FRONT_NIPPLE_LENGTH))),
         nipple_v, nipple_w, CALIPER_FRONT_PIN_DIAMETER)


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
    alloy = context.material("engine_cast")
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

    # The friction ring, floating on six bobbins, and carrying the same face
    # pattern as the front: `007-BRKR-10`'s own drawing is where the nine slots and
    # the two rings of nine holes were measured, and its photograph shows one part
    # stamped with both this form's number and the front's. It keeps its floating
    # two-piece construction -- that is `007-B4-69`'s rear and the front form's is a
    # one-piece -- so it takes no drive lugs: the carrier and the bobbins are what
    # the torque goes through. Before #214 it was a plain washer at chi 0.
    bm = bmesh.new()
    _disc_face(
        context,
        bm,
        DISC_REAR_X,
        DISC_REAR_DIAMETER * 0.5,
        DISC_REAR_THICKNESS,
        bore_radius=DISC_REAR_RING_INNER,
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

    # The caliper, straddling the disc at the pad's mean radius. It is centred on
    # that radius rather than on the disc's, which is what puts the piston over the
    # middle of the friction band -- 82.5 mm, so the body spans r 55..110 and the
    # disc's own rim at 97.5 is 12.5 mm inside the caliper's outer edge. That is
    # the fact the bridge is built around.
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
        piston_bore=CALIPER_REAR_PISTON_BORE,
        detail=detail,
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
    alloy = context.material("engine_cast")
    plastic = context.material("rubber_grip")
    detail = context.detail

    pad_radius = (DISC_FRONT_PAD_OUTER + DISC_FRONT_PAD_INNER) * 0.25
    rotation = Matrix.Rotation(-CALIPER_FRONT_CLOCK, 4, "X")

    for label, side in (("fl", -1.0), ("fr", 1.0)):
        plane = side * DISC_FRONT_X

        # One piece: nine curved slots, two rings of nine drilled holes, and three
        # integral drive tangs at 120 degrees reaching in to the hub's inboard
        # flange, each with its own bolt hole. Until #214 this was a plain lathed
        # ring plus three boxes -- Euler characteristic +6, which is three disjoint
        # solids and not one drilling, under a docstring that claimed the pattern.
        bm = bmesh.new()
        _disc_face(
            context,
            bm,
            plane,
            DISC_FRONT_DIAMETER * 0.5,
            DISC_FRONT_THICKNESS,
            bore_radius=DISC_FRONT_BORE_RADIUS,
            lug_count=DISC_FRONT_TANG_COUNT,
            lug_inner_radius=DISC_FRONT_LUG_INNER,
            lug_half_angle=DISC_FRONT_LUG_HALF_ANGLE,
            lug_bolt_radius=DISC_LUG_BOLT_FRAC * DISC_FRONT_DIAMETER * 0.5,
            lug_bolt_diameter=DISC_LUG_BOLT_DIAMETER,
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
    alloy = context.material("engine_cast")
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
            # The old route dove to z 92 and crossed the steering hoop's y band
            # at x -150 -- fine while the brake pedal was at -85, inside the arm
            # and the clevis once #201 moved it to -150. And there is no low
            # road anymore: the hoop's level arm at z 89..105 and the pan
            # edging at z 65..81 leave an 8 mm slot, so the line now crosses
            # the hoop *high*, inboard at x -100 where the arm is furthest
            # forward, then runs over both master bodies (tops at z 117) and
            # outside both towers before dropping onto the distributor.
            (0.0, 0.755, crest_z),
            (-0.060, 0.690, 0.106),
            (-0.100, 0.600, 0.107),
            (-0.100, 0.535, 0.108),
            (-0.200, 0.512, 0.128),
            (-0.230, 0.508, 0.124),
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
        # -0.132, was -0.140: with the rear cylinder at -175 (#201) the first
        # leg crossed y 400 at x -159, inside the side bumper socket's riser at
        # -158..-186. Bending to -132 crosses at -150, 8 mm inboard of it.
        (-0.132, 0.380, 0.092),
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
