"""Issue #15 — the KZ powertrain: engine, intake, driveline, exhaust, radiator.

The kart currently has no engine at all, and issue #15's first acceptance
criterion is that the silhouette reads as a shifter kart rather than a
single-speed. Four things carry that read, in order of how much each one costs
to get wrong:

1.  **The expansion chamber.** It is the largest single shape on the kart after
    the tires, and it is the one shape a single-speed does not have in this form.
    It is built as a swept tube of varying radius so the belly is a real bulge —
    a constant-diameter pipe with a fat box on it reads as placeholder from any
    angle. Header, diverging cone, belly, converging (baffle) cone, stinger and
    silencer are all one continuous sweep along one path.
2.  **The engine is a cluster, not a block.** Crankcase, clutch cover, ignition
    cover, cylinder with its casting ribs, head, carburetor, airbox, starter and
    battery. A single box at `engine_width x engine_length x engine_height` reads
    as a crate strapped to the frame.
3.  **The chain line is real geometry.** The engine's output sprocket, the chain
    and the axle sprocket have to be coplanar, because a chain that is visibly
    skewed is the kind of mistake a reviewer sees immediately. See the CHAIN LINE
    note below — this module does not agree with `wheels.py` about which side of
    the kart that plane is on, and the disagreement is deliberate and reported.
4.  **The mass is where the physics says it is.** ARCHITECTURE.md §6 wants a
    center of mass slightly rearward, and ADR-0011's KZ carries its engine on the
    driver's right. Everything here is behind y = -0.14 or outboard of x = +0.22
    except the radiator and the exhaust, which is what produces that bias.

CHAIN LINE — the one thing in this module that contradicts another module
-------------------------------------------------------------------------
A kart engine's crankshaft is parallel to the rear axle and its gearbox output
sprocket is on the **outboard** end of that shaft -- KZ-R1 HF `041-EZ-75` p. 1
photographs the drive side and the clutch side as opposite ends, clutch inboard
-- so the chain runs in a plane perpendicular to the axle, outboard of the
cases. That plane must contain both sprockets, and a chain cannot be skewed by
even a few millimeters without throwing itself.

**Issue #112 is closed by giving the number one owner.** `powertrain.CHAIN_X` and
`wheels.SPROCKET_X` were the same magnitude with opposite signs and neither owned
it, so the two modules disagreed about which side of the kart the chain ran on and
the disagreement was reported rather than resolved. Both now read
`params.chain_x` = **+0.445**: a KZ carries engine, chain and crown wheel on the
driver's right and the brake disc on the left, and `wheels.py`'s conclusion that
*"a KZ drives the left rear"* is not a thing a locked rear axle can do. The
magnitude moved from +0.115 to +0.445 in the corridor audit -- the chain exits
the engine's *outboard* face, not through the seat's flank; see `params.chain_x`.

The other half of #112 was the **pitch diameter formula**. It is
`p / sin(pi/N)` and not `p * N / pi`; the approximation is 0.03% small at 82 teeth
and 1.1% small at 12, which is 0.24 mm of radius — and that is exactly the 1.9 mm
by which the chain was built inside a Ø18 output shaft.

EXHAUST ROUTING — the port faces REARWARD, and this file used to say the opposite
--------------------------------------------------------------------------------
Art. **5.10**, PDF p. 17, verbatim: *"It is mandatory for the exhaust to pass
rearward and not cross the plane defined by the driver seated in the normal
driving position."* A regulation, read in the pinned text, and it settles it.

Two independent proofs had already reached the same place. The **photograph**:
in `tonykart_racer401T_p05.jpg` the intake silencer is on the engine's forward
face and the exhaust joint, its two retaining springs and the pipe are all on the
rear face — a topological reading, which is the one thing that image's projection
does not destroy. And the **arithmetic**: the sourced cone table turns through 95
degrees in total, and a single-plane 95 degree bend cannot take a forward-pointing
inlet to a rearward-pointing outlet. That needs ~180; both degenerate solutions
put the stinger 550 mm outboard of the kart or 430 mm below the ground.

The paragraph that used to stand here reasoned from a forward-facing port —
*"there is nowhere for it to go […] getting this backwards puts the pipe in the
tire and the airbox in the radiator"*. **The premise was wrong and so was the
conclusion.** Rearward is the only family that closes, and the airbox is what has
to move: the pipe now occupies the volume the intake was built in, so the box
rises 60 mm and the carburettor drops 12 (see `AIRBOX_LO` and `CARB_AXIS_Z`).

What makes it packageable at all is the cylinder's 25 degree forward lean —
`params.cylinder_lean`, which is `derived` from the sourced port angle plus the
axle clearance rather than styled. With the barrel upright the pipe's inlet axis
points 25 degrees *down* and the pipe is 51 to 63 mm inside the rear axle at
every roll of the bend plane. At 25 degrees of lean the inlet axis is horizontal.

Coordinates: +X right, +Y forward, +Z up. Everything is authored in world
coordinates and parented to `powertrain_root` at the origin, the way `frame.py`
does it, so no object here carries a transform other than the published pivots.

Interfaces published for later milestones:

    powertrain_root     the group's single node
    engine_root         crankcase center; the mass the M3b solver hangs on the
                        chassis body and the M4 audio emitter attaches to
    engine_sprocket     rotate about its own local **X**; the drivetrain's
                        output, with the sprocket mesh parented under it
    exhaust_outlet      the silencer's mouth, for the exhaust particle emitter
"""

from __future__ import annotations

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

from . import build
from . import params as P


# --- dimensions that belong in params.py -----------------------------------
#
# Same precedent as `wheels.py` and `cockpit.py`: every constant below is a real
# dimension of the kart and should move into `KartParams` when the parameter
# block is next touched. They are here, named, rather than inline, so that moving
# them is mechanical and so no number appears twice. Each says what constrains
# it, because none of them is a free choice.
#
# The seven parameters this module *does* get from params.py — `engine_width`,
# `engine_length`, `engine_height`, `engine_x/y/z`, the four `exhaust_*` and the
# seven `radiator_*` — are read where they are used and are noted there when the
# reading of them is not the obvious one.

CHAIN_HALF_WIDTH: float = 0.0045
CHAIN_HALF_HEIGHT: float = 0.0035
"""#219 chain is about 9 mm across the rollers and 8 mm tall. Modeled as a flat
band rather than a round cord: a chain seen edge-on is a flat plate, and a swept
circle reads as a bungee."""

SPROCKET_Y: float = -0.2685
"""Output sprocket station. **Solved for a whole even pitch count, not chosen.**

A chain is an even whole number of pitches. With pitch radii 10.745 and 72.615 the
closed-belt length is 790.87 mm at a centre distance of 257.01, which is 142.18
pitches; bringing the centre distance back 0.49 mm to 256.5 lands it on exactly
**142**. That moves this station from -0.268 to -0.2685, and the 0.5 mm is the
whole content of the change."""

CHAIN_GUARD_X: tuple[float, float] = (0.431, 0.463)
CHAIN_GUARD_Y: tuple[float, float] = (-0.610, -0.258)
CHAIN_GUARD_WALL: float = 0.002
"""Art. **5.9**, PDF p. **17**, verbatim: *"A chain guard is mandatory in all
classes. Chain guards may be made of composite material. […] In gearbox classes,
the chain guard must cover the sprocket and the crown wheel down to the centre of
the crown wheel axis."*

So the guard is **compulsory** and the kart did not have one. The lateral span is
`derived`: the chain band is 440.5-449.5 and both sprockets sit inside it, so
7.5 mm of clearance a side, which is the smallest gap a composite guard holds
without rubbing. The inner wall at 431 also stays 1 mm clear of the ignition
cover's face at 430, which it can only do because the cover's y span (-242..-138)
ends 16 mm before the guard's begins at -258. The fore-aft span runs from the small sprocket's front to the
crown wheel's rear plus 10 mm each end. Its **lower edge is z = rear_axle_z**,
which is the article's own words rather than a choice.

It is `pierced` by the axle and by the output shaft, and that is correct -- the
guard is cut around both. Declaring them is what stops gate 1 reading a compulsory
part as a collision."""
"""Engine output sprocket center. `SPROCKET_Z` is `engine_z` — the crankshaft and
the gearbox output sit at the same height on a kart engine, and that height is
what `engine_z` has to mean for the chain line to work at all (see `_engine`).
`SPROCKET_Y` is 257 mm ahead of the rear axle, which is a normal KZ chain run.
(It once also dodged the right rear seat strut, back when the carrier sat on the
*inboard* face at x 212; at x 393 nothing of the strut, which never passes x
300, comes near it.)"""

MOUNT_PLATE_TOP: float = 0.100
"""Top of the engine mount, and therefore the underside of the crankcase.

The main rail's top surface is at 0.065 (`rail_z` + half `tube_main`), so this
leaves 35 mm for the clamp — which is what a kart engine mount actually is, a
pair of plates bolted either side of the rail with the engine bolted on top."""

CRANKCASE_HEIGHT: float = 0.140
CRANKCASE_INBOARD_X: float = 0.240
CRANKCASE_OUTBOARD_X: float = 0.398
CRANKCASE_FRONT_Y: float = -0.145
CRANKCASE_REAR_Y: float = -0.345
"""The crankcase's own box, in absolute coordinates rather than as a center and a
size, because every face of it is set by something it has to clear rather than by
a dimension of its own:

    inboard   the right seat struts. `frame.py` mirrors both struts onto the
              right side, where a KZ has an engine; the rear one passes through
              x 0.206..0.224 at the crankcase's height. See the report.
    outboard  the right side bar, whose inboard surface is at x 0.420.
    front     the front seat strut, which ends at y = -0.129.
    rear      leaves room for the carburetor and the reed block behind it.
"""

CASE_SPLIT_Z: float = 0.150
CASE_LOWER_INSET: float = 0.005
"""The crankcase's horizontal parting line, and how far the lower half is inset.

A kart crankcase is two sand castings bolted together on a plane through the
crankshaft axis, so the split is at `engine_z` by definition rather than by
choice. The lower half is drawn narrower than the upper so the upper's bolting
flange overhangs it: that ledge and the shadow under it are the whole reason to
build two boxes instead of one, and it is what a plain box could never show.
"""

BELL_RADIUS: float = 0.060
BELL_CENTER_Y: float = -0.228
BELL_CENTER_Z: float = 0.172
BELL_PROUD: float = 0.014
"""The clutch bell housing on the inboard face.

The clutch is inside the cases on a KZ — a hand-operated multi-plate, not the
external centrifugal drum a TaG kart carries — and R2 shows the housing over it
standing well proud of the case as a round boss the height of the case itself.
The clutch owns the **inboard** face alone: KZ-R1 HF p. 1 photographs drive side
and clutch side as opposite ends, so the sprocket carrier emerges from the
*outboard* face at the case's far side (see `_driveline`), and nothing on this
face has a shaft through it.
"""

CLUTCH_COVER_RADIUS: float = 0.056
CLUTCH_COVER_INBOARD_X: float = 0.196
CLUTCH_COVER_HUB_RADIUS: float = 0.018
CLUTCH_BOLT_CIRCLE: float = 0.046
CLUTCH_BOLT_COUNT: int = 6
"""The cover bolted onto the bell.

R2's is an **openwork casting** — a lattice of webs with the clutch's pressure
plate and its eight spring bolts visible through the windows. The lattice itself
is not affordable here and is not what the eye reads at any distance the game
uses; what it reads is that the cover is a separate part with its own rim, a
raised center hub and a ring of fasteners. So: a rim, a dished face, a hub, and
six bolts. The windows are left to issue #19's normal bake.
"""

IGNITION_COVER_RADIUS: float = 0.052
IGNITION_COVER_OUTBOARD_X: float = 0.430
IGNITION_COVER_Z: float = 0.185
IGNITION_BOLT_CIRCLE: float = 0.042
IGNITION_BOLT_COUNT: int = 5
"""Ignition/flywheel cover on the outboard face. Held 16 mm clear of the right
side bar, which passes at x 0.432..0.452, z 0.096..0.117 alongside it — which is
also why this one is not on the crankshaft axis the way the clutch is. On the
axis at z = 0.150 its lower edge would be at 0.098, inside the bar's height
band, and it clears only because it stops 2 mm short of the bar in x. That is
not a clearance to rely on."""

CYLINDER_BASE_Z: float = 0.240
"""The base face, and the pivot the whole cluster leans about. `derived`:
`MOUNT_PLATE_TOP` 100 + `CRANKCASE_HEIGHT` 140."""

CYLINDER_PORT_RISE: float = 0.0499
CYLINDER_PORT_WINDOW: tuple[float, float] = (0.0441, 0.0282)
"""Port centre 49.9 mm above the base face and a 44.1 x 28.2 window, both `derived`
off KZ-R1 HF p. 3's development at 0.0601 mm/px (830 px and 733 x 470 px). The
window is 81.6% of the 54 mm bore because the form lists **three** exhaust ports,
one main plus two auxiliaries. Recorded rather than modelled: the port is a hole
this mesh does not cut, and it is what places `EXHAUST_INLET`."""

CYLINDER_AXIS_X: float = 0.319
CYLINDER_AXIS_Y: float = -0.250
"""The bore's axis. x is the crankcase's own center, because the barrel sits over
the crank; y is 5 mm ahead of the case's fore-and-aft center, because the case
extends rearward past the crank to carry the gearbox and the reed block."""

CYLINDER_BASE_TOP_Z: float = 0.258
CYLINDER_BASE_HALF: tuple[float, float] = (0.070, 0.076)
CYLINDER_TOP_Z: float = 0.348
CYLINDER_RADIUS: float = 0.064
"""The cylinder, which is **a round water jacket on a square base flange** —
not a box, and with no cooling fins on it at all.

This is the single largest correction in issue #116 and it is the one the issue
itself got wrong: #116 asks for fins that "taper, wrap the barrel's curve, and
vary in depth front to back". That describes an air-cooled 100 cc barrel — R3 is
one, and it is what the previous four flat plates were unintentionally imitating.
A KZ is water-cooled through the cylinder, head *and* crankcase, so a real one
has **no fins**; R2's Vortex is a smooth sand casting, round in plan, sitting on
a square flange that is bolted to the case by four studs. See ADR-0028.

The base flange is wider than the barrel on all four sides, which is what makes
the two read as one casting with a machined joint rather than as a taper.
"""

HEAD_RADIUS: float = 0.053
HEAD_BOLT_CIRCLE: float = 0.038
HEAD_BOLT_COUNT: int = 6
HEAD_NUT_FLATS: float = 0.013
"""The head: a disc a little smaller than the barrel, with six nuts on a bolt
circle round a spark plug boss at its center. R2 shows exactly six, each in its
own spotfaced counterbore, and the step where the head's diameter falls inside
the barrel's is clearly visible all the way round.

**The step has to be big enough to see.** At 60 mm against the barrel's 64 the
two read in a render as one drum with a lid on it, which is what the first
attempt at this produced — measured off a 1100 px close-up, the step was under
two pixels. 53 mm puts 11 mm of barrel crown on show all the way round, and
moving `CYLINDER_TOP_Z` up to 0.348 at the same time makes the head a squat
52 mm disc rather than a 70 mm drum, which is the proportion R2 has."""

PLUG_BOSS_RADIUS: float = 0.021
PLUG_BOSS_HEIGHT: float = 0.009
PLUG_HEX_FLATS: float = 0.021
PLUG_INSULATOR_RADIUS: float = 0.0080
PLUG_CAP_DIAMETER: float = 0.026
PLUG_LEAD_DIAMETER: float = 0.007
"""The spark plug, standing proud of the head on its boss, and its cap and lead.

#116 calls it "one of the most recognizable things on a two-stroke" and it is:
in R2 the white insulator and the black cap are the highest-contrast objects in
the whole engine bay. 21 mm across the flats is a B-series plug's spanner size.
The lead is included because the plug cap alone reads as a stub — what says
ignition is the lead arcing away from it down to the coil.
"""

MANIFOLD_BOLT_FLATS: float = 0.010
"""The exhaust port flange on the barrel's front face, and its two nuts.

The chamber is not welded to the engine: an elbow bolts to the port through this
flange and the pipe slips onto the elbow and is held by springs. #116 lists the
flange and the springs as the missing joint hardware, and they are the two parts
that make the pipe look bolted on rather than grown out of the cylinder.
"""

"""Two tension springs from lugs on the flange to lugs on the header. 11 mm coil
diameter and 2.2 mm wire is a standard exhaust spring; six turns over the ~55 mm
they span is what a stretched one looks like. `EXHAUST_SPRING_SAMPLES` is the
polyline resolution per turn and is fixed at both detail levels, because the
number of turns is the spring's *shape*."""

REED_BLOCK_LO: tuple[float, float, float] = (0.272, -0.368, 0.155)
REED_BLOCK_HI: tuple[float, float, float] = (0.352, -0.345, 0.222)
REED_FRAME_INSET: float = 0.009
"""The reed cage on the crankcase's rear face, between the case and the carb.

A case-reed KZ breathes through here, and R2 shows it as a ribbed grey block
with a raised bolting frame round a recessed center. It is built as the frame
plus a recessed face, which is two boxes and reads correctly; the three reed
windows themselves are normal-map detail."""

STARTER_RADIUS: float = 0.034
STARTER_X: float = 0.283
STARTER_Z: float = 0.172
"""Onboard electric starter, on the front face of the crankcase. A KZ has to
carry one — CIK requires an onboard starter for the class — and its bulk plus the
battery is a real part of the silhouette."""

BATTERY_SIZE: tuple[float, float, float] = (0.085, 0.110, 0.070)
BATTERY_CENTER: tuple[float, float, float] = (0.185, -0.400, 0.195)
"""The starter's battery, on a bracket behind the engine. Sits above the rear
seat strut, which passes below it at z 0.120..0.148.

Turned to run **fore-and-aft** rather than across the kart, and moved 8 mm
inboard. Laid across, its outboard end reached x 0.285 and the carburetor's body
— now a 60 mm cylinder about x 0.312 rather than a 72 mm box — occupies from
0.282. The two overlapped by 3 mm. Along the kart it ends at 0.265 and clears
both the carburetor by 17 mm and the seat shell's edge at 0.164 by 15 mm."""

CARB_AXIS_X: float = 0.312
CARB_AXIS_Z: float = 0.193
CARB_BODY_RADIUS: float = 0.030
CARB_FRONT_Y: float = -0.368
CARB_REAR_Y: float = -0.440
CARB_SPIGOT_ENGINE_RADIUS: float = 0.0175
CARB_SPIGOT_AIR_RADIUS: float = 0.032
CARB_TOP_CAP_RADIUS: float = 0.024
CARB_TOP_CAP_TOP_Z: float = 0.250
CARB_BOWL_LO: tuple[float, float, float] = (0.290, -0.434, 0.140)
CARB_BOWL_HI: tuple[float, float, float] = (0.334, -0.388, 0.172)
"""The carburettor drops **12 mm** because the pipe now runs where its cap was.

Its cap at z 262 fouled the pipe's underside at 260 by 2 mm, and the port height is
`derived` from the homologation form and does not move -- so the carburettor is what
moves. Result: 10 mm of clearance. The bowl goes to z 140-172, still 71 mm over the
tray."""
"""Dell'Orto VHSH 30 — the 30 mm flat-slide the class runs, on the crankcase's
rear face pointing back at the airbox.

**A round body with a rectangular float bowl**, which is the opposite of how
this module had it: it was a box with a turned bowl under it, and R2 shows a
turned body with a box bowl. The two spigot radii are the carburetor's own
published sizes rather than invented ones — 35 mm to the engine and 64 mm to the
air filter, so 17.5 mm and 32 mm in radius — and the top cap is a separate
cylinder standing straight up out of the body with the throttle cable entering
through it, which is the detail that stops the whole part reading as a lump.
"""

AIRBOX_LO: tuple[float, float, float] = (0.250, -0.567, 0.340)
AIRBOX_HI: tuple[float, float, float] = (0.420, -0.452, 0.460)
AIRBOX_DUCT_DIAMETER: float = 0.030
AIRBOX_DUCT_X: tuple[float, float] = (0.330, 0.390)
AIRBOX_DUCT_Z: float = 0.350
AIRBOX_DUCT_LENGTH: float = 0.045
"""The airbox sits high and rearward, over the right rear tire -- inboard of it at
x <= 0.420 against the tire's inner face at 0.485. It is the highest thing on the
kart apart from the steering wheel and a large part of what says "shifter" from
behind.

**Raised 60 mm from z 280..400, because the exhaust now occupies the volume it was
built in**: the rearward pipe passes through x 245-344 at z 238-325 there. At
340..460 it clears the pipe's crown by 15 mm, the driver's shoulder (x +-200) by 50
and the right rear tyre's inner face by 65.

The two **ducts are compulsory and were missing.** Art. **9.13.1**, PDF p. **30**:
*"They must have two ducts with a 30.0 mm maximum diameter."* Count and diameter are
`sourced` -- it is a maximum and every KZ silencer runs it -- and the positions are
`estimated`: 45 mm long, projecting forward, leaving 8 mm to the leaned head's
rearmost point."""

INTAKE_BOOT_DIAMETER: float = 0.068
"""Rubber over the carburetor's 64 mm air-filter spigot."""

EXHAUST_CONES: tuple[tuple[float, float, float, float], ...] = (
    (0.0000, 0.0677, 0.0445, 0.0470),
    (0.0677, 0.1012, 0.0470, 0.0490),
    (0.1012, 0.1347, 0.0490, 0.0508),
    (0.1347, 0.1578, 0.0508, 0.0557),
    (0.1578, 0.1809, 0.0557, 0.0610),
    (0.1809, 0.2128, 0.0610, 0.0703),
    (0.2128, 0.2446, 0.0703, 0.0798),
    (0.2446, 0.2764, 0.0798, 0.0890),
    (0.2764, 0.3083, 0.0890, 0.0983),
    (0.3083, 0.3401, 0.0983, 0.1074),
    (0.3401, 0.4090, 0.1074, 0.1365),
    (0.4090, 0.4725, 0.1365, 0.1350),
    (0.4725, 0.5131, 0.1350, 0.1145),
    (0.5131, 0.6226, 0.1145, 0.0558),
    (0.6226, 0.6746, 0.0558, 0.0263),
)
"""The whole expansion chamber, as (s start, s end, dia start, dia end) in meters.

**Every one of the fifteen rows is `sourced`**, off the TM homologation forms
KZ-R1 `041-EZ-75` and KZ-R2 `041-EZ-02`, each of which carries both diameters and
both slant lengths of all 15 cones. Art. **9.15.1**, PDF p. **30**: *"All KZ
engines must be fitted with the exhaust homologated with the engine and described
in the engine´s HF."* That makes this table normative rather than descriptive.

Two checks that the transcription is right rather than plausible: summing the 15
frusta and insetting the wall reproduces each form's own stated internal volume to
1.07 mm (R1) and 1.29 mm (R2), which is where `params.exhaust_wall` comes from;
and `outer - inner slant = theta * D_mean` summed per cone gives 95.3 degrees of
total bend, against a photograph on the facing page showing about a right angle.

**This is a shape, not a scalar, which is why it lives here** rather than in
`params.py` -- §00's single-owner rule puts scalars there and shapes in the module
that builds them. The five headline diameters are in `params.py` because other
things read them.

It replaces `EXHAUST_PROFILE`, whose nine ratio points drew a *smooth silhouette*.
A KZ chamber is fifteen straight-sided frusta with visible weld beads at the
joins, and that faceting **is** the shape -- so the pipe is built as a cone
sequence and not as a profile curve. Cone 12 is the belly and is effectively
cylindrical; 13 and 14 are the two baffle cones; 15 is the stinger."""

EXHAUST_CONE_TURN: tuple[float, ...] = (
    0.0, 0.0, 0.0, 8.93, 7.95, 8.99, 10.08, 10.05, 9.97, 10.03,
    8.13, 6.08, 6.29, 6.06, 2.79,
)
"""Degrees of bend each cone contributes, `derived` from the two slant lengths of
that cone: `theta = (outer - inner) / D_mean`. Sums to **95.35**, and the first
three are zero because the pipe is straight to s = 134.7 -- which is the sourced
fact that decides where the bend can start."""

EXHAUST_INLET: tuple[float, float, float] = (0.319, -0.328, 0.285)
"""The pipe's inlet face, on the manifold spigot. `derived`, §30.4: the port centre
is 49.9 mm up the leaned bore axis from the base face, the horizontal port axis
leaves the Ø128 jacket 70.6 mm rearward of that, and the machined face stands a
little proud."""

EXHAUST_BEND_TILT: float = 0.2094
"""Nose-down tilt of the bend *plane*, radians (12 degrees). `estimated`, and its
family is wide -- measured across it, the chamber floor / crown / stinger exit go
0 deg -> 217/353/285, 8 -> 185/330/225, **12 -> 169/318/195**, 20 -> 124/314/137.

12 is the shallowest tilt that leaves the silencer body 130 mm of ground clearance
while keeping the chamber's crown well under Art. 5.10's 450 mm ceiling. **The
crown's maximum is not a function of the tilt**: it peaks in the diffuser where the
bend has barely started, so it is set by the sourced port height and cannot be
tuned away.

The bend turns **inboard**, `derived`: outboard puts the belly in the right rear
tyre."""

MANIFOLD_LENGTH: float = 0.028
MANIFOLD_SPIGOT_DIAMETER: float = 0.043
MANIFOLD_PLATE: tuple[float, float, float] = (0.078, 0.060, 0.008)
MANIFOLD_BOLT_PATTERN: tuple[float, float] = (0.062, 0.044)
"""`exhaust_flange` is renamed `exhaust_manifold`, and the rename is the point:
**there is no flange on the pipe.** A short manifold bolts to the cylinder and the
pipe slips over its spigot on springs, which is why the chamber has to be able to
articulate and why the support downstream is a spring cradle rather than a bolt.

Length **28** is `sourced` and flagged: kartshop sells the TM KZ manifold as
"D2 28 / 29 / 30.5" and calls the number the length -- three options 2.5 mm apart
is a length shim, not a restrictor family -- but on Vortex ROK the same designation
is a bore. The spigot is `estimated` against the pipe's `sourced` 44.5 mm inlet
bore: a slip joint needs a few tenths plus room for carbon. The plate is a
62 x 44 bolt rectangle plus M6 heads and edge; the real part exists and nobody
publishes its dimensions.

Bolts: **4x M6 x 20**, `sourced` (kartshop, *"TM KZ manifold D2 28: 4x allen bolt
M6 x 20 mm"*). **The build had two nuts and needs four**, so
`exhaust_flange_nut_0..1` becomes `exhaust_manifold_bolt_0..3`."""

EXHAUST_SPRING_TURNS: int = 6
EXHAUST_SPRING_COIL_RADIUS: float = 0.006
EXHAUST_SPRING_WIRE_RADIUS: float = 0.00125
EXHAUST_SPRING_SAMPLES: int = 8
EXHAUST_SPRING_STATION: float = 0.070
"""Two bent-tab hooks side by side at s ~ 70, `measured` off KZ-R2 HF p. 13 at
500 dpi and scaled on the pipe's own sourced 46.5 mm OD there. Free length **70**
is `sourced` -- eurokart, *"TM K9/K9B/K9C KZ10/10B/KZ10C/KZ-R1 exhaust spring 70mm
KZ"*. Wire 2.5 and coil 12 are `estimated` proportions off the same drawing.

What the springs buy is ~3 degrees of cone and ~5 mm of axial play about the slip
joint, which is why the chamber is **not** rigid to the engine."""

SILENCER_LENGTH: float = 0.450
SILENCER_DIAMETER: float = 0.120
SILENCER_INLET_DIAMETER: float = 0.029
SILENCER_OUTLET_DIAMETER: float = 0.032
SILENCER_AXIS_Y: float = -0.800
SILENCER_AXIS_Z: float = 0.190
SILENCER_SPAN: tuple[float, float] = (-0.170, 0.250)
"""The can, **transverse**, and the transverse part is forced rather than styled:
the clear box behind the axle is about 365 mm deep and a 450 mm cylinder cannot lie
fore-and-aft in it. The stinger already points along -x, i.e. along the body axis.

450 x 120 with a 29 mm inlet spigot is `sourced` -- the Elto ICC/KZ silencer, whose
retaining hardware is listed as *"large jubilee clip for exhaust silencer x2,
120-140mm"*, which corroborates the 120. The outlet's **floor** is `sourced`:
Art. **5.10**, p. 17, requires an external diameter *"more than 3 cm"*, so 32 is
the smallest round tube above it; the article also requires the outlet not to
*"exceed the outer limits of the kart"* and to discharge behind the driver, and at
x 280 it is 420 mm inside the kart's 700 limit.

An alternative family exists at 89 x 349 (MC Racing KZ/ICC) and is recorded so
nobody reads 120 x 450 as the only shape. The sourced clamps fit the 120."""

CONNECTOR_DIAMETER: float = 0.030
CONNECTOR_APEX: tuple[float, float, float] = (-0.215, -0.744, 0.192)
"""The U-bend, `estimated` as a route and `sourced` as a **part**: the catalogues
sell a *"muffler bent pipe" / "exhaust with U-bend"* as a separate item, which is
exactly what turns a leftward stinger back into a rightward can."""

HANGER_CLAMP_X: float = 0.054
HANGER_CLAMP_BORE: float = 0.030
HANGER_CLAMP_OD: float = 0.046
HANGER_BOSS_TOP_Z: float = 0.088
HANGER_CRADLE_STATION: float = 0.513
HANGER_CRADLE_WIRE: float = 0.006
"""`exhaust_hanger` was **bolted to nothing** -- 60.47 mm off `chassis_side_bar_r`
-- and it could not mount where it did. The sourced mushroom clamp comes in
28/30/32 bores and `chassis_side_bar_r` is a `tube_bumper` = 20 mm bumper tube. The
30 mm tubes on this kart are `chassis_rail_*`, `chassis_cross_front` and
`chassis_cross_rear`, and of those only the rear cross member is within reach of a
rearward pipe. So the clamp goes on `chassis_cross_rear` at x +54, directly under
the grip point, and the part splits into clamp, arm and cradle.

The arm's 169 mm is `derived` from the two ends it has to join, not read off a
photograph -- `notes_exhaust.md` estimated ~150 from an image with no dimensioned
feature. The cradle is a `sourced` eurokart *"exhaust cradle spring D.12mm
L.130mm"* clipped round the **baffle cone** at s = 513, which is the only part of
the pipe both stiff enough to clamp and reachable; real installations clip the
cone, not the belly. One support, `estimated`: one arm, one clip and one clamp is
what the catalogue sells as a set, and a second would over-constrain a pipe that
must articulate at the slip joint."""

SILENCER_BRACKET_X: float = 0.230
SILENCER_BAND_X: tuple[float, float] = (-0.050, 0.160)
SILENCER_SADDLE_WIDTH: float = 0.110
SILENCER_ISOLATOR: tuple[float, float] = (0.028, 0.012)
"""The can's own mount, all `estimated` off `exh_eurokart_3.jpg` and `_5.jpg`
against the sourced 120 mm body. **Two** bands is `sourced` (*"large jubilee clip
for exhaust silencer x2"*). The bracket is 96 mm outboard of the pipe support's
clamp so the two do not collide on the same tube."""

RADIATOR_TANK_HEIGHT: float = 0.022
RADIATOR_TANK_PROUD: float = 0.007
"""Top and bottom tanks. `radiator_height` covers the tanks *and* the core, so
the core's own height is `radiator_height - 2 * RADIATOR_TANK_HEIGHT`.

**22 +-6, `estimated`**, was 30: the polished band above the fin block reads about
16 px in the dead-rear shot, foreshortened by both the rake and the camera
elevation, so the true height is larger than the reading. Cross-checked against the
CRG close-up. The fin block is therefore 435 - 2 x 22 = **391**, which is also the
travel `radiator_curtain` has to cover.

They stand proud of the core on **both** faces, which is what R2's does and what
makes the core read as a core rather than as a painted panel: the tank is a
folded box welded across the ends of the tubes, so it is necessarily thicker than
the fin pack between them."""

RADIATOR_END_PLATE: float = 0.012
"""Side channels closing the fin pack at each end of the core."""

RADIATOR_FIN_PITCH: float = 0.012
RADIATOR_FIN_THICKNESS: float = 0.005
RADIATOR_FIN_PROUD: float = 0.0015
"""The fin pack.

A real core's fins are at about 1.5 mm pitch and there is no honest way to build
them: 288 of them across a 432 mm core would cost more than the rest of the kart,
and at the distance the chase camera works from they would alias into noise. Five
fat ribs at 52 mm — what was here — went the other way and read as a garden gate.

12 mm is the pitch at which the pattern still resolves as a pattern at cockpit
range without shimmering at chase range, and issue #19's normal bake is the
designed answer for the frequency above it. The count follows from the core's
length, so it is not a free number, but it does not vary with detail level: a fin
is part of the radiator's shape, not of its resolution.

The real pitch is **1.8 +-0.5** (`estimated`; unresolvable below ~2 mm at
1.04 mm/px, checked against 12-16 fins/inch practice) and 288 fins would cost more
than the rest of the kart. With `radiator_width` 250 the modelled count is **18**,
and `RADIATOR_DIVIDER_ALONG` at -0.44 of the half-width still lands between fins 4
and 5, so `joints.py`'s two explicit rows stay correct.
"""

RADIATOR_CURTAIN_THICKNESS: float = 0.002
RADIATOR_CURTAIN_STANDOFF: float = 0.006
RADIATOR_CURTAIN_SLOT: float = 0.055
"""The adjustable blind, and it is **new**. Art. 5.3.1 permits it in as many words
-- *"a system of fairings and covers may be placed at the front or rear of the
radiator(s). This device may be adjustable, but it must not be detachable when the
kart is in motion"* -- and the baffles *"must be securely fixed to the radiator(s)
with screws. They must be one-piece and may be made of composite material."*

Its width **is** the core's: Direct-Karting sells a 250 mm curtain for the 250 mm
radiator, 290 for the 125 RS and 230 for the X30 big, and New-Line's air shield is
25 cm -- so this reads `radiator_width` rather than carrying a number. Travel is
the full 391 mm fin block, because blanking the core means spanning it. The two
55 mm slots are `sourced` off the New-Line description, for intermediate settings,
and the pulley-and-O-ring drive is what makes it adjustable without being
detachable. `estimated`: the 2 mm composite and the 3 mm standoff, which is a
faceting allowance.

It does **not** ship with the radiator: EM-01 is listed *"without shutter blind"*
and the blind is a separate line."""

RADIATOR_SIDE: float = -1.0
"""Which side of the kart the radiator stands on: -1 is the kart's **left**.

Measured off the references, not chosen. V4 is a plan view with the front at the
top, so image right is the kart's right: the engine is at image right and the
radiator at image left. V3 (dead-rear) and V8 (dead-front) both agree once each
is mirrored for its own viewpoint, which is three independent photographs of two
different manufacturers.

This was `+1` by omission -- the basis below simply assumed a right-side part --
and `engine_x` is `+0.319`, so the radiator was built 11 mm from the engine, on
top of the exhaust and through the gear lever. The audit found 167 triangle pairs
of shifter inside the core, 130 of exhaust chamber inside it and 296 of right
sidepod inside it and the engine. All three were the same bug.

`docs/REFERENCES.md`'s V3 caption says "radiator and engine between the seat and
the right rear" and is wrong for the same reason: written off a rear-view photo
without mirroring it.

At -1 the basis determinant is -1, because a part on the left genuinely is the
mirror of the part on the right. That is handled where the geometry is built
rather than by patching signs into dimensions: see `_reverse_if_mirrored`.
"""

RADIATOR_CAP_RADIUS: float = 0.019
RADIATOR_CAP_HEIGHT: float = 0.041
RADIATOR_CAP_ALONG: float = -0.62
"""The filler **neck** and its cap, on the high tank, `RADIATOR_CAP_ALONG` of
the way along the core's width from its center. Negative is **outboard**, which
is where the reference photographs put it — see `_radiator_frame` for why the
local lateral axis points inboard.

#116 lists it, and it earns its place: it is the one thing on a radiator that is
unmistakably a radiator. A flat disc was the first attempt and it is not what
this is — a kart radiator's filler stands up on a welded neck tall enough to
pour a bottle into, so the shape is a base weld, a narrow neck and a wider cap
skirt over it, 41 mm in all."""

RADIATOR_DIVIDER_ALONG: float = -0.44
RADIATOR_DIVIDER_THICKNESS: float = 0.011
RADIATOR_DIVIDER_PROUD: float = 0.005
"""The dual-pass divider. Negative is outboard, as for the filler neck.

A New-Line kart radiator is double-pass: a baffle welded inside the high tank
sends water down the inboard tubes and back up an outer set, and from outside
it shows as a raised welded rib splitting the core into a wide forward section
and a narrow rear one. It is the single most recognizable thing about the face
of one of these — the photographs read as a kart radiator rather than as a
generic core largely because of it."""
BRACKET_DIAMETER: float = 0.016
BRACKET_LOWER_LOCAL: tuple[float, float, float] = (-1.0, 1.0, -0.52)
BRACKET_UPPER_LOCAL: tuple[float, float, float] = (-1.0, 1.0, 0.44)
"""Where the two brackets meet the radiator, as **fractions of the core's own
half-extents in its own frame** rather than world points, so they stay on the back
of the core when the rake or the size changes -- which is the whole reason the
radiator is built in a frame at all.

**1.15 was the bug and 1.0 is the rest of the fix.** 1.15 x 125 = 143.75 is
18.75 mm past the core's edge and the rod's radius is 8, which is the 12.3 mm
gate-2 gap almost exactly. At 1.0 the rod's axis lies in the plane of the inboard
end channel's outer face, so the rod is 8 mm engaged in a 12 mm channel: contact
0.0, and `bolted` permits the overlap. Anchoring in *fractions* was the right fix
for a bracket that started inside the fin pack; 1.15 was the wrong fraction.

**The other end anchors on `chassis_rail_l`, and that is a regulation reading
rather than a convenience.** `BRACKET_*_SEAT` used to put it on the seat's wing.
Three things say the frame: Art. **4.2.3** puts the *welded* attachment points for
*"the radiator(s)"* on the frame; Art. **4.8.2** requires seat stays to be bolted
at each end and *removed if unused*, so a stay is a removable member and not a
mounting rail; and the CRG close-up -- the only image in the repo that shows the
bracket -- shows a pair of thin vertical rods dropping to a **chassis clamp**.
Art. 5.3.1's *"radiators must be placed above the chassis frame"* points the same
way: the frame is what it stands on.

**And there must never be a `radiator_*`/`seat_shell` joint.** Art. 5.3.1: *"They
must not interfere with the seat."* That is the one place in this project where a
regulation is expressed as the **absence** of a declaration -- gate 1 makes any
overlap fatal precisely because no `Joint` is declared, so the missing row is an
assertion and not an omission. The core's inboard edge at -240 clears the shell's
outboard face at -184 by 56 mm and must keep doing so."""

HOSE_DIAMETER: float = 0.028
"""20 mm ID silicone (`sourced`, FTP kart radiator hose; 3/4 in is the trade
standard) plus 2 x 4 mm of three-ply wall. A photo reading gives 33, which is
protective sleeving and must not be modelled as bare hose. Art. 5.3.1 rates the
tubing at 150 C and 10 bar."""

HOSE_UPPER_LOCAL: tuple[float, float, float] = (0.0, 0.85, 0.95)
HOSE_LOWER_LOCAL: tuple[float, float, float] = (0.0, 0.85, -0.95)
HOSE_UPPER_ROUTE: tuple[tuple[float, float, float], ...] = (
    (-0.170, -0.410, 0.388),
    (0.220, -0.404, 0.376),
    (0.232, -0.228, 0.382),
)
"""**This route was built through the driver's chest, and the driver is the datum
that caught it.** `estimated` as a route, like the lower one.

The route was two waypoints, `(0, -400, 390)` and `(150, -300, 375)`. The first is
where it still is; the second pulled the crossing **65 mm forward of the seat
back's top edge** and the run then stayed forward all the way to the head, so the
pipe went diagonally across the lumbar spine and out over the lower right flank
about 200 mm above the hip joint. The shell it was supposed to be behind was
**1.67 mm** away, which is a graze and not a clearance.

**The driver volume is measured two ways, and only one of them means anything
behind the seat.** The hard points are
`docs/kart_spec/60-driver-and-finishes.md` §60.1.4's: hip joint (0, -170, 130) to
shoulder joint (0, -393, 608), half-breadth 162 rising to 180. A tapered capsule
on those is **circular in cross-section**, so it spends a *lateral* half-breadth
in the fore-aft direction too -- 173 mm at the height this hose crosses, i.e. a
346 mm chest -- and it reaches back past the seat: the built `seat_shell` measures
**99.3 mm inside that capsule** at (0, -362, 368). A model that puts the seat
inside the driver cannot adjudicate a hose behind the seat, so the figure to fix
against is the capsule **intersected with the half-space forward of the seat
back** -- §60.1.1's `y = -365 + 0.404*(365 - z)`, which is where his back is,
because it is what he leans on. Both, old route then new:

    capsule n forward of the seat back    -35.71 at (73, -336, 388)   ->  +18.63
    bare capsule                          -94.83 at ( 9, -380, 397)   ->  -78.81

The second row stays negative and stays negative for the seat as well; that says
the instrument is wrong, not that the hose still is. ADR-0055's **79.4 mm at
(8, -379, 394)** is this same defect on its own capsule union, and is quoted here
rather than reconciled -- three capsule models, three depths, one hose through a
man.

**Nothing here failed. That is the point.** Gates 1 and 2 have no opinion about
the volume a seated human occupies because that volume is not in the build, so
this was green for a milestone and was found by a human turning a viewport. The
sibling defect on the lower hose -- the `reversed()` in `_cooling` -- was fixed in
#190 wave 3b and **nobody re-checked the upper route afterward, because there was
nothing to check it against.** §60.1.4 publishes the hard points and §60.1.6
explains why the man is a datum; issue #200's gate 3 is what will assert this
without a viewport. Note that `HOSE_LOWER_ROUTE`'s note below says the upper
route's *"two points swapped with no change to the pipe it builds"* -- true of
that wave, and those are the two points this docstring is about.

Three waypoints now, and what each one is against:

    (-170, -410, 388)   straight back off the high tank's fitting and over the
                        core's top edge rather than around it: 3.19 mm to
                        `radiator_fin_17`'s rear-top corner, which the old route
                        cleared by 3.03, so the fin is no worse for the extra
                        29 mm of rearward set. y -410 is 8.7 mm behind
                        `radiator_tank_high`'s own rear face at -401.3
    ( 220, -404, 376)   **the crossing, and it stays behind the seat back the
                        whole way across.** The shell's rear face is at y -358 at
                        z 350 and its top edge is z 378, so the tube runs 26.4 mm
                        clear of `seat_shell` and 18.6 mm clear of the driver
                        volume above. x 220 is 36 mm outboard of the shell's
                        +-184 flank and ~49 mm outboard of the torso's own
                        half-breadth at this height, which is what lets the turn
                        forward happen here and not sooner
    ( 232, -228, 382)   up the head's **inboard** flank. `engine_cylinder` is a
                        leaned can of radius 64 whose deck is at z 361 and
                        `engine_head` is radius 53 on top of it; x 232 is 87 mm
                        inboard of their common axis at x 319, so the climb
                        happens in free air -- 14.06 mm to the cylinder against
                        the old route's 5.52 -- and the last 107 mm drop onto the
                        boss over the deck rather than through it. It also keeps
                        `engine_head_nut_2` and `_3` at 7.87 and 8.11 rather than
                        under a millimeter, which is what a straighter dive at
                        this corner costs

Measured on the built tube, surface to surface, at `Detail.high`, after the 50 mm
bend radius has had its say -- the fillet is why these are not the authored
offsets:

    seat_shell                 26.36     engine_head_nut_3           8.11
    driver, clipped capsule    18.63     engine_head_nut_2           7.87
    engine_cylinder            14.06     radiator_fin_17             3.19
    engine_plug_boss           15.24     radiator_curtain            9.75
    engine_plug_lead           67.12     chassis_seat_strut_rear_r  56.78
    engine_airbox              61.60     engine_intake_boot         51.43
    engine_airbox_duct_0       90.28     drive_chain               162.81
    axle_rear                 220.66     axle_sprocket             174.11
    cooling_pump_bracket      274.86     radiator_bracket_upper     95.13

`radiator_tank_high`, `radiator_core`, `radiator_end_inboard`, `engine_head` and
`engine_water_outlet` are the five declared `routed` joints and all five still
overlap, which is what gate 2 requires of a declared joint.

**A hose cannot cross the spinning axle plane**, so this run stays above and
behind it: 220.7 mm to `axle_rear` and 174.1 to the crown wheel."""
HOSE_LOWER_ROUTE: tuple[tuple[float, float, float], ...] = (
    (-0.213, -0.150, 0.115),
    (-0.213, -0.370, 0.100),
    (-0.020, -0.392, 0.081),
    (0.135, -0.390, 0.081),
)
"""**Both routes run radiator-first, and getting that wrong is what wave 3 measured.**

#190 wave 3b, and the finding is not the one the waiver described. `_cooling` built
each hose as `[fitting_on_the_core] + reversed(waypoints) + [fitting_on_the_engine]`,
so a list authored radiator-to-engine was *consumed* engine-to-radiator. The upper
route happened to be authored the other way round and came out right; the lower one
did not, and the hose it built went from the low tank straight across the kart to
x 0 at y -455, back **outboard** to x -224, forward to y -200, and only then
diagonally back across to the pump at (160, -386). Two of those four legs cross the
driver, which is the 101 pairs on `seat_shell` and the 48 on `seat_bracket_lower_l`.

Wave 3 read the same overlap as a packaging result -- *"the corridor is 33 mm wide and
a Ø28 hose does not fit in it with anything else"*, `seat_bracket_lower_l` at x -207
against the core's -240 -- and proposed spending the radiator's Art. 5.3.1 margin on
it. That measurement is correct and it is not what was wrong: the leg in that corridor
was never the leg touching the seat. The `reversed()` is gone and both lists are
authored in one direction, which is also why `HOSE_UPPER_ROUTE`'s two points swapped
with no change to the pipe it builds.

**The core does not move.** It has 60 mm to Art. 5.3.1's 150 mm lateral limit, but
`bodywork_sidepod_l` is 21.25 mm outboard of `radiator_tank_low` and `chassis_rail_l`
is 8.51, so only ~15 of that 60 exists before the core is inside a panel §Bodywork
owns -- and moving it would make three standing waivers worse for a corridor that was
not the fault.

The four waypoints, and what each one is against:

    (-213, -150, 115)   down the left flank. 5.9 mm inboard of
                        `radiator_bracket_lower`'s inboard end at -232.9, reached
                        before the bracket's own y band rather than across it
    (-213, -370, 100)   rearward past the seat. 15.3 mm to the shell's widest flank
                        at -183.7 and 13.0 mm to the core's inboard face at -240
    ( -20, -392,  81)   the crossing, and it is **forward of** `chassis_cross_seat`
                        (y -402.4..-431.6, top z 65) on purpose: between that tube
                        and the chain's lower strand the window is 33.8 mm, which a
                        Ø28 hose does not thread with any margin either side
    ( 135, -390,  81)   still under the chain's lower strand -- at y -390 that strand's
                        band is z 106..113 -- and it stops 7 mm short of
                        `cooling_pump_bracket` (x 156..300, z 72..88) so the climb to
                        the pump's inlet happens outboard of the bar rather than
                        through it

Measured on the built tube, surface to surface, after the 50 mm bend radius has had its
say -- the fillet is why these are not the authored offsets:

    seat_shell                21.11      cooling_pump_bracket       5.82
    seat_bracket_lower_l      19.43      chassis_cross_seat        11.42
    radiator_bracket_lower    16.90      drive_chain                4.53
    chassis_rail_l            41.97      drive_chain_guard         38.09

A hose cannot cross the spinning axle plane, so the upper goes above and behind it and
the lower stays forward of it: 100.6 mm to `axle_rear` and 61.6 to the crown wheel."""
WATER_OUTLET_LOCAL: tuple[float, float, float] = (0.299, -0.207, 0.376)
"""Hot water enters the **high** tank so the core drains downward, which New-Line's
*"curved top tank inlet designed to evenly distribute water"* confirms is the inlet
end; the cold return is the low run by construction, because the pump is at axle
height. Both `estimated` as routes.

**This docstring used to assert that the upper run crosses "behind the seat back,
which is the shorter way across from a head outlet and stays above the axle", and
for a milestone that sentence was false.** It was true of the axle and false of the
seat: the run crossed 65 mm *forward* of the shell's top edge and 94.8 mm inside the
driver's chest, and the sentence is exactly why nobody looked -- a comment that
states the constraint as satisfied reads like the check. It is now a property of
the route rather than a claim about it, and the two numbers that make it one are in
`HOSE_UPPER_ROUTE`'s own note: 26.4 mm to `seat_shell` and 18.6 mm to the torso
volume §60.1.4 defines. The lower run's geometry and every clearance on it are in
`HOSE_LOWER_ROUTE`'s note for the same reason -- a clearance quoted in two places is
a clearance that will disagree with itself.

**A hose cannot cross the spinning axle plane**, so the upper goes above and behind
it and the lower stays forward of it. That half was always true and is still
measured: 220.7 mm to `axle_rear`."""

WATER_INLET_BOSS: tuple[float, float, float] = (0.240, -0.330, 0.165)
"""A **new** cast boss on the crankcase's inboard face, low. Art. 9.10.1
water-cools *"the crankcase, cylinder and head"* -- all three -- so the coolant has
to get into the case somewhere, and there was no such part: the lower hose ended on
the clutch cover instead. 24 dia x 14 proud, `estimated`."""

PUMP_SPINDLE: tuple[float, float, float] = (0.160, -0.386, 0.110)
PUMP_BODY_DIAMETER: float = 0.060
PUMP_PULLEY_PD: float = 0.025
AXLE_PULLEY_PD: float = 0.065
BELT_WIDTH: float = 0.0079
BELT_THICKNESS: float = 0.0022
BELT_PLANE_X: float = 0.190
"""The water pump, **on the rear axle and not on the engine**, driven by a toothed
belt. Art. **5.3.2**, PDF p. 15, quoted without substitution because an earlier
paraphrase made it appear to *force* this: *"In Groups 1 & 2, the water pump must be
mechanically controlled either by the engine or by the rear wheel axle."* It
**permits either**, so the axle drive is this spec's choice and kart practice, not a
requirement. Electric pumps are what it prohibits.

The belt is what places the pump. **170XL031**, `sourced` as a part number and
`derived` as a decode: XL profile, 5.08 mm pitch, 170 = 17.0 in = **431.8 mm** pitch
length, 431.8/5.08 = **85 teeth**, 031 = 5/16 in = **7.9 mm** wide. Then
`L = 2C + (pi/2)(D1+D2) + (D1-D2)^2/(4C)` with 65 and 25 gives
`2C + 400/C = 290.43`, so **C = 143.8** -- the belt telling us where the pump sits.
The quadrant is the only one not already occupied: rearward is the exhaust chamber,
above is the airbox and the battery, and straight down is the floor tray.

The axle pulley's 65 dia is `derived` from the axle's 50 plus 6-9 mm of clamp wall
and flange a side. The pump pulley is `estimated` at 25: New Line sells a **30 mm**
pump pulley *to slow the pump down for larger tracks*, which only reads that way if
the stock one is smaller.

**The pump body stays `estimated` and that is deliberate.** It is not published on
any page reachable from here and **no reference photograph in this repo shows the
pump at all** -- every Tony Kart and CRG frame here is a studio chassis shot with
the pump absent or behind the rear panel. 60 dia +-10 is read off trade photographs
as roughly a quarter of a 250 mm core, and it has to house an impeller fed by 20 mm
spigots. This is the softest number in the assembly and it is labelled as one."""


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("powertrain")

    # One root for the whole group, as `frame.py` and `cockpit.py` do: the glTF
    # exporter flattens collections, so a single parent is the only thing that
    # makes the powertrain one node for the M3b solver to attach mass to.
    root = build.empty("powertrain_root", (0.0, 0.0, 0.0), collection, size=0.10)
    context.publish("powertrain_root", root)

    _check_cone_table(context.params)

    _engine_mount(context, collection, root)
    _engine(context, collection, root)
    _intake(context, collection, root)
    _driveline(context, collection, root)
    _exhaust(context, collection, root)
    _radiator(context, collection, root)
    _cooling(context, collection, root)


def _check_cone_table(p: P.KartParams) -> None:
    """Assert the 15-cone shape and the five sourced scalars still agree.

    `params.py` owns the headline diameters because other things read them and
    `EXHAUST_CONES` owns the shape, which is §00's single-owner rule -- and that
    leaves five numbers written down twice. Two copies of a figure with nothing
    comparing them is how `SPROCKET_Z` and `engine_z` drifted, and how the chain
    plane ended up with two signs; so this compares them, at import cost of nothing,
    and is fatal.

    It also checks the developed length against the table's own walk, which is the
    one figure a reader is most likely to change by editing a cone and forgetting
    the scalar.
    """
    checks = (
        ("exhaust_header_diameter", p.exhaust_header_diameter, EXHAUST_CONES[0][2]),
        ("exhaust_max_diameter", p.exhaust_max_diameter, EXHAUST_CONES[10][3]),
        ("exhaust_baffle_diameter", p.exhaust_baffle_diameter, EXHAUST_CONES[12][3]),
        ("exhaust_stinger_diameter", p.exhaust_stinger_diameter, EXHAUST_CONES[-1][3]),
        ("exhaust_developed_length", p.exhaust_developed_length, EXHAUST_CONES[-1][1]),
    )
    for name, scalar, table in checks:
        if abs(scalar - table) > 1e-6:
            raise SystemExit(
                "error: params.%s is %.6f and EXHAUST_CONES says %.6f.\n"
                "       The cone table is sourced off the homologation form and is "
                "the shape;\n"
                "       the scalar is what other modules read. They are the same "
                "number and\n"
                "       one of them has been edited alone." % (name, scalar, table)
            )
    if abs(p.exhaust_wall) < 1e-6:
        raise SystemExit("error: params.exhaust_wall is zero; Art. 5.10 floors it at 0.75 mm")


# --- geometry helpers ------------------------------------------------------
#
# `build.py` covers tubes of constant radius, revolutions about a world axis, and
# boxes, which is the whole of the frame and most of this module. An expansion
# chamber is none of the three: it is a sweep whose radius varies along a path
# that is not straight and not axis-aligned. The two primitives that needs live
# here. See the report — `build.sweep_tube` taking a per-point radius would
# remove both of them.


def _block(
    name: str,
    low: tuple[float, float, float],
    high: tuple[float, float, float],
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    *,
    bevel: bool = True,
    transform: Matrix | None = None,
) -> bpy.types.Object:
    """A box given by its two extreme corners rather than a center and a size.

    Every box in this module is positioned by what it has to clear, so its faces
    are the authored numbers and the center is the derived one. Writing it the
    other way round means every clearance is checked against arithmetic done in
    somebody's head.
    """
    size = tuple(high[axis] - low[axis] for axis in range(3))
    center = tuple((high[axis] + low[axis]) * 0.5 for axis in range(3))
    bm = bmesh.new()
    build.box(bm, size, center)
    if transform is not None:
        bm.transform(transform)
    obj = build.object_from_bmesh(name, bm, collection, material=material)
    if bevel:
        build.bevel_object(obj, context.detail)
    build.set_parent(obj, root)
    return obj


def _polyline_length(points: list[Vector]) -> float:
    return sum((points[i + 1] - points[i]).length for i in range(len(points) - 1))


def _trim_to_length(points: list[Vector], length: float) -> list[Vector]:
    """Cut a polyline off at exactly `length`, interpolating the final point.

    This is what makes `exhaust_length` govern the exhaust rather than describe
    it: the control path is authored long enough to reach past the front of the
    sidepod and the parameter decides where the silencer's mouth actually lands,
    so raising it lengthens the pipe along its own route instead of needing every
    control point re-authored.
    """
    if length <= 0.0 or len(points) < 2:
        return list(points)
    result = [points[0]]
    travelled = 0.0
    for index in range(len(points) - 1):
        step = (points[index + 1] - points[index]).length
        if step < 1e-12:
            continue
        if travelled + step >= length:
            fraction = (length - travelled) / step
            result.append(points[index].lerp(points[index + 1], fraction))
            return result
        travelled += step
        result.append(points[index + 1])
    return result


def _resample(points: list[Vector], count: int) -> list[Vector]:
    """`count` points spaced evenly along a polyline by arc length.

    A filleted control path has its points bunched in the bends and nowhere at
    all down the straights, so sweeping a varying radius along it would render
    the belly with two rings and the header with twenty. Resampling separates the
    shape of the path from the resolution of the profile drawn on it.
    """
    if count < 2 or len(points) < 2:
        return list(points)
    total = _polyline_length(points)
    if total < 1e-9:
        return list(points)

    result: list[Vector] = [points[0].copy()]
    # Walk the source once, in order, rather than searching it per sample: the
    # order is the determinism guarantee, and a per-sample search would be the
    # same answer computed len(points) times over.
    index = 0
    consumed = 0.0
    for step in range(1, count):
        target = total * step / (count - 1)
        while index < len(points) - 1:
            segment = (points[index + 1] - points[index]).length
            if consumed + segment >= target or index == len(points) - 2:
                fraction = 0.0 if segment < 1e-12 else (target - consumed) / segment
                fraction = min(max(fraction, 0.0), 1.0)
                result.append(points[index].lerp(points[index + 1], fraction))
                break
            consumed += segment
            index += 1
    return result


def _profile_at(profile: tuple[tuple[float, float], ...], t: float) -> float:
    """Linear interpolation of a (fraction, value) table, clamped at both ends."""
    if t <= profile[0][0]:
        return profile[0][1]
    for index in range(len(profile) - 1):
        left, right = profile[index], profile[index + 1]
        if t <= right[0]:
            span = right[0] - left[0]
            if span < 1e-12:
                return right[1]
            fraction = (t - left[0]) / span
            return left[1] + (right[1] - left[1]) * fraction
    return profile[-1][1]


def _sweep_varying(
    bm: bmesh.types.BMesh,
    path: list[Vector],
    radii: list[float],
    segments: int,
) -> None:
    """Sweep a circle of per-point radius along `path`, into an *empty* bmesh.

    Built on `build.sweep_tube` at unit radius and then scaled ring by ring,
    rather than on a second copy of its parallel-transport frame arithmetic. That
    is safe because `sweep_tube` documents its emission order as a determinism
    guarantee — rings in path order, vertices within a ring in increasing angle —
    so vertex `ring * segments + step` is known without recomputing anything, and
    a ring's vertices scale about their own path point.

    The bmesh must be empty on entry, because the indices are absolute.
    """
    if len(bm.verts) != 0:
        raise ValueError("_sweep_varying needs an empty bmesh")
    build.sweep_tube(bm, path, 1.0, segments)
    bm.verts.ensure_lookup_table()
    for index, point in enumerate(path):
        base = index * segments
        radius = radii[index]
        for step in range(segments):
            vertex = bm.verts[base + step]
            vertex.co = point + (vertex.co - point) * radius


def _ribbon(
    bm: bmesh.types.BMesh,
    path: list[tuple[float, float]],
    plane_x: float,
    half_width: float,
    half_height: float,
) -> None:
    """Extrude a closed rectangular section along a closed path in an X plane.

    The chain, and only the chain. A chain is a flat band lying in one plane, so
    it needs neither a general sweep nor a general frame: the section's "up" is
    the path's own 2D normal and its "across" is the kart's X axis, which is what
    keeps every link's face square to the sprockets.
    """
    count = len(path)
    if count < 3:
        return
    rings: list[list[bmesh.types.BMVert]] = []
    for index in range(count):
        y, z = path[index]
        ny, nz = path[(index + 1) % count]
        py, pz = path[(index - 1) % count]
        tangent = Vector((ny - py, nz - pz))
        if tangent.length < 1e-12:
            tangent = Vector((1.0, 0.0))
        tangent.normalize()
        # (X, normal, tangent) has to be right-handed or the face loop below —
        # which is `build.sweep_tube`'s — winds the whole chain inward. With
        # `(-tangent.y, tangent.x)` it was left-handed and the chain enclosed
        # -0.000050 m3, which `genkart.check_face_winding` rejects.
        normal = Vector((tangent.y, -tangent.x))
        ring: list[bmesh.types.BMVert] = []
        # Corner order is fixed, so the winding is fixed for every link.
        for across, along in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            ring.append(
                bm.verts.new(
                    (
                        plane_x + across * half_width,
                        y + normal.x * along * half_height,
                        z + normal.y * along * half_height,
                    )
                )
            )
        rings.append(ring)

    for index in range(count):
        lower = rings[index]
        upper = rings[(index + 1) % count]
        for corner in range(4):
            following = (corner + 1) % 4
            bm.faces.new(
                (lower[corner], lower[following], upper[following], upper[corner])
            )


def _disc_profile(radius: float, half_thickness: float) -> list[tuple[float, float]]:
    """A closed cylinder as a lathe profile of (radius, along-axis) pairs."""
    return [
        (0.0, -half_thickness),
        (radius, -half_thickness),
        (radius, half_thickness),
        (0.0, half_thickness),
    ]


def _lathe_object(
    name: str,
    profile: list[tuple[float, float]],
    center: tuple[float, float, float],
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    *,
    axis: str = "X",
    segments: int | None = None,
    shade_smooth: bool = True,
    transform: Matrix | None = None,
) -> bpy.types.Object:
    """A revolution about a world axis, parented to `root`.

    `transform` is applied to the finished vertices, and it exists for exactly one
    thing: the cylinder cluster leans 25 degrees forward (`params.cylinder_lean`)
    and every part of it is far easier to author upright and then tip. It must be a
    rotation -- a mirror would invert the winding, which is the trap
    `_reverse_if_mirrored` documents on the other side of this module.

    `segments` defaults to the detail level's, which is what a part that is meant
    to be round wants. A part whose facet count is part of its *shape* rather
    than its resolution passes an explicit number and must pass the same one at
    both detail levels — a hexagon nut with 16 sides at high detail is not the
    same object built more finely, it is a different object, and issue #19's cage
    would have to bridge the two.
    """
    bm = bmesh.new()
    build.lathe(
        bm,
        profile,
        context.detail.exhaust_segments if segments is None else segments,
        axis=axis,
        center=center,
    )
    if transform is not None:
        bm.transform(transform)
    obj = build.object_from_bmesh(
        name, bm, collection, material=material, shade_smooth=shade_smooth
    )
    build.set_parent(obj, root)
    return obj


def _hex_nut(
    name: str,
    center: tuple[float, float, float],
    across_flats: float,
    height: float,
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    *,
    axis: str = "Z",
    transform: Matrix | None = None,
) -> bpy.types.Object:
    """A hex fastener head, as a six-sided revolution.

    Fasteners are what tells the eye that a casting is an assembly of parts
    rather than one lump, and R2 shows them everywhere on a KZ: six on the head,
    four on the cylinder base, more round the covers. Six segments and flat
    shading, at both detail levels, for the reason `_lathe_object` gives.

    `across_flats` is the spanner size, so the circumscribed radius the lathe
    needs is `across_flats / sqrt(3)` — quoting the number a fastener is actually
    specified by rather than a radius nobody would recognize.
    """
    radius = across_flats / math.sqrt(3.0)
    return _lathe_object(
        name,
        [(0.0, 0.0), (radius, 0.0), (radius, height), (0.0, height)],
        center,
        context,
        collection,
        root,
        material,
        axis=axis,
        segments=6,
        shade_smooth=False,
        transform=transform,
    )


def _helix(
    start: Vector,
    end: Vector,
    coil_radius: float,
    turns: int,
    samples_per_turn: int,
) -> list[Vector]:
    """A helical polyline from `start` to `end`, for a coil spring.

    Exhaust springs are small — 50 mm long and 10 mm across — and they are one of
    the loudest "this is a two-stroke" signals there is, which is why #116 names
    them by hand. A helix is the only shape here that cannot be faked with a
    box: at a glance the eye reads the pitch, and a plain cylinder does not have
    one.

    The frame is built once from the axis rather than per sample, so the coil
    does not wander, and the seed axis is chosen the same way `build._frames`
    chooses its own — by least alignment — so it is deterministic.
    """
    axis = end - start
    length = axis.length
    if length < 1e-9 or turns < 1:
        return [start.copy(), end.copy()]
    axis = axis / length

    seed = min(
        (Vector((1.0, 0.0, 0.0)), Vector((0.0, 1.0, 0.0)), Vector((0.0, 0.0, 1.0))),
        key=lambda candidate: abs(candidate.dot(axis)),
    )
    normal = (seed - axis * seed.dot(axis)).normalized()
    binormal = axis.cross(normal).normalized()

    count = turns * samples_per_turn
    points: list[Vector] = []
    for index in range(count + 1):
        fraction = index / count
        angle = 2.0 * math.pi * turns * fraction
        points.append(
            start
            + axis * (length * fraction)
            + normal * (math.cos(angle) * coil_radius)
            + binormal * (math.sin(angle) * coil_radius)
        )
    return points


def _tube_object(
    name: str,
    path: tuple[tuple[float, float, float], ...],
    diameter: float,
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    *,
    bend_radius: float = 0.030,
) -> bpy.types.Object:
    bm = bmesh.new()
    build.tube(bm, list(path), diameter, context.detail, bend_radius)
    obj = build.object_from_bmesh(
        name, bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(obj, root)
    return obj


# --- engine mount ----------------------------------------------------------


def _engine_mount(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The clamp assembly bolting the engine to the right main rail.

    A kart engine mount is a table bolted down onto the rail with clamp plates
    either side of the tube. Here it is a top plate plus two **outboard** clamps,
    and the asymmetry is not a simplification — there is no room for an inboard
    clamp. `frame.py`'s floor tray is `tray_length` = 0.760 long and runs from
    y = 0.180 back to y = -0.580 at x = +-0.280, so it covers the main rail
    through the whole engine bay and out past the rear axle. Anything reaching
    down the rail's inboard side goes through it. See the report: a real kart's
    floor pan stops at the back of the footwell, and this one does not.

    The rail's path is `frame.py:_rail_path`, read from there rather than
    guessed: between y = -0.305 and y = -0.165 its centerline runs from x = 0.259
    to x = 0.277, so the clamps sit at x 0.303..0.317, about 11 mm outboard of
    the 30 mm tube's surface.
    """
    p = context.params
    material = context.material("engine_cast")
    front_y = -0.165
    rear_y = -0.305
    top = MOUNT_PLATE_TOP
    # The clamps start just above the rail's lowest point rather than below it:
    # `ground_clearance` is measured to the underside of the rail and nothing on
    # the kart may hang below it.
    bottom = p.ground_clearance + 0.003

    for label, low_y, high_y in (
        ("front", front_y - 0.048, front_y - 0.005),
        ("rear", rear_y + 0.005, rear_y + 0.048),
    ):
        _block(
            "engine_mount_clamp_%s" % label,
            (0.303, low_y, bottom),
            (0.317, high_y, top - 0.028),
            context,
            collection,
            root,
            material,
        )

    # The table sits on top of the tray rather than beside it, 3 mm clear.
    _block(
        "engine_mount_plate",
        (0.230, rear_y, P.tray_top_z(p) + 0.003),
        (0.322, front_y, top),
        context,
        collection,
        root,
        material,
    )


# --- engine ----------------------------------------------------------------


def _engine(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Crankcase, covers, barrel and head — one casting cluster.

    **How the three engine parameters are read**, because two of them have a
    second plausible reading that puts the engine through the asphalt:

    `engine_z` = 0.150 is the **crankshaft axis**, not the center of a bounding
    box. It has to be: the gearbox output sits on that axis, the chain runs from
    it to a rear axle whose center is at 0.1475, and a chain line is the one
    thing here that cannot be fudged. Read instead as the center of a box
    `engine_height` = 0.300 tall, it would put the crankcase's underside at
    z = 0.000 — 35 mm below the frame rails and on the ground.

    `engine_height` = 0.300 is therefore measured from the crankcase's underside,
    which the mount fixes at `MOUNT_PLATE_TOP`, to the top of the head: 0.100 to
    0.400. That is a tall engine, and a KZ is a tall engine — the head sits at
    about the driver's elbow.

    `engine_x` = 0.240 and `engine_width` = 0.230 are **not** both honored, and
    that is the one deviation in this module that is not forced by another
    module's geometry being wrong. Centered at 0.240 the crankcase's inboard face
    lands at x = 0.125, which is 39 mm *inside* the seat shell's outer edge at
    0.164. The cluster is built at its true width against the clearances it has,
    and `engine_x` should be about 0.320. See the report.
    """
    p = context.params
    material = context.material("engine_cast")

    crank_bottom = MOUNT_PLATE_TOP
    crank_top = crank_bottom + CRANKCASE_HEIGHT
    # The head's top is `engine_height` above the crankcase's underside, so the
    # parameter still sets how tall the engine is even though it does not set
    # where its center is.
    head_top = crank_bottom + p.engine_height

    center_x = (CRANKCASE_INBOARD_X + CRANKCASE_OUTBOARD_X) * 0.5
    center_y = (CRANKCASE_FRONT_Y + CRANKCASE_REAR_Y) * 0.5

    pivot = build.empty(
        "engine_root",
        (center_x, center_y, (crank_bottom + head_top) * 0.5),
        collection,
        size=0.10,
    )
    context.publish("engine_root", pivot)
    build.set_parent(pivot, root)

    _crankcase(context, collection, root, material, crank_bottom, crank_top)
    _cylinder(context, collection, root, material, crank_top)
    _head(context, collection, root, material, head_top)

    _lathe_object(
        "engine_starter",
        _disc_profile(STARTER_RADIUS, 0.045),
        (STARTER_X, CRANKCASE_FRONT_Y + 0.045, STARTER_Z),
        context,
        collection,
        root,
        material,
        axis="Y",
    )

    _block(
        "engine_battery",
        tuple(
            BATTERY_CENTER[axis] - BATTERY_SIZE[axis] * 0.5 for axis in range(3)
        ),
        tuple(
            BATTERY_CENTER[axis] + BATTERY_SIZE[axis] * 0.5 for axis in range(3)
        ),
        context,
        collection,
        root,
        context.material("frame_powdercoat"),
    )


def _crankcase(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    crank_bottom: float,
    crank_top: float,
) -> None:
    """Two case halves, the clutch bell and cover, and the ignition cover.

    #116's complaint about the crankcase is that it is "a plain box" where a real
    one is "an organic casting: a bell housing round the clutch, cast ribbing, a
    raised bearing boss, a sump profile, visible case-half split line and bolt
    bosses". Of that list, the two that carry the read at the distances this game
    uses are the **split line** and the **bell**, and both are silhouette rather
    than surface — a normal bake cannot supply either. Cast ribbing and bolt
    bosses are surface, and are left to issue #19.
    """
    # Upper and lower halves, parted on the crankshaft axis, with the lower one
    # inset so the upper's bolting flange overhangs it. Two boxes, one shadow
    # line, and the engine stops being extruded.
    _block(
        "engine_crankcase_upper",
        (CRANKCASE_INBOARD_X, CRANKCASE_REAR_Y, CASE_SPLIT_Z),
        (CRANKCASE_OUTBOARD_X, CRANKCASE_FRONT_Y, crank_top),
        context,
        collection,
        root,
        material,
    )
    _block(
        "engine_crankcase_lower",
        (
            CRANKCASE_INBOARD_X + CASE_LOWER_INSET,
            CRANKCASE_REAR_Y + CASE_LOWER_INSET,
            crank_bottom,
        ),
        (
            CRANKCASE_OUTBOARD_X - CASE_LOWER_INSET,
            CRANKCASE_FRONT_Y - CASE_LOWER_INSET,
            CASE_SPLIT_Z,
        ),
        context,
        collection,
        root,
        material,
    )

    # The bell, standing proud of the inboard wall, and the cover bolted to it.
    bell_inboard = CRANKCASE_INBOARD_X - BELL_PROUD
    _lathe_object(
        "engine_clutch_bell",
        _disc_profile(BELL_RADIUS, BELL_PROUD * 0.5),
        (bell_inboard + BELL_PROUD * 0.5, BELL_CENTER_Y, BELL_CENTER_Z),
        context,
        collection,
        root,
        material,
    )

    # The cover is dished: a rim at full radius, a face set back from it, and a
    # hub in the middle. Three profile steps, and it stops reading as a decal.
    cover_thickness = bell_inboard - CLUTCH_COVER_INBOARD_X
    _lathe_object(
        "engine_clutch_cover",
        # Written inboard face first, so the profile's along-axis coordinate only
        # ever increases. `build.lathe` winds from that order, and a profile that
        # doubles back emits one ring of inverted faces — which the signed-volume
        # gate only catches if the whole part comes out negative.
        [
            (0.0, -0.014),
            (CLUTCH_COVER_HUB_RADIUS, -0.014),
            (CLUTCH_COVER_HUB_RADIUS, -0.008),
            (CLUTCH_COVER_RADIUS - 0.008, -0.008),
            (CLUTCH_COVER_RADIUS, -0.003),
            (CLUTCH_COVER_RADIUS, cover_thickness),
            (0.0, cover_thickness),
        ],
        (CLUTCH_COVER_INBOARD_X, BELL_CENTER_Y, BELL_CENTER_Z),
        context,
        collection,
        root,
        material,
    )
    _cover_bolts(
        context,
        collection,
        root,
        material,
        "engine_clutch_bolt_%d",
        (CLUTCH_COVER_INBOARD_X - 0.008, BELL_CENTER_Y, BELL_CENTER_Z),
        CLUTCH_BOLT_CIRCLE,
        CLUTCH_BOLT_COUNT,
        inward=True,
    )

    # Ignition / flywheel cover, outboard, with the same treatment.
    ignition_thickness = IGNITION_COVER_OUTBOARD_X - CRANKCASE_OUTBOARD_X
    _lathe_object(
        "engine_ignition_cover",
        # Every step is inside `IGNITION_COVER_OUTBOARD_X`. The first version put
        # the hub 6 mm past it, at x 0.436, which is inside the right side bar's
        # x 0.432..0.452 — a limit this constant exists to hold and that a
        # profile written outward-last quietly broke.
        [
            (0.0, 0.0),
            (IGNITION_COVER_RADIUS, 0.0),
            (IGNITION_COVER_RADIUS, ignition_thickness - 0.011),
            (IGNITION_COVER_RADIUS - 0.007, ignition_thickness - 0.006),
            (0.022, ignition_thickness - 0.006),
            (0.018, ignition_thickness),
            (0.0, ignition_thickness),
        ],
        (CRANKCASE_OUTBOARD_X, (CRANKCASE_FRONT_Y + CRANKCASE_REAR_Y) * 0.5, IGNITION_COVER_Z),
        context,
        collection,
        root,
        material,
    )
    _cover_bolts(
        context,
        collection,
        root,
        material,
        "engine_ignition_bolt_%d",
        (
            IGNITION_COVER_OUTBOARD_X,
            (CRANKCASE_FRONT_Y + CRANKCASE_REAR_Y) * 0.5,
            IGNITION_COVER_Z,
        ),
        IGNITION_BOLT_CIRCLE,
        IGNITION_BOLT_COUNT,
        inward=False,
        proud=0.007,
        # Half a pitch: the output shaft's bore sits at ring angle ~214 deg and
        # the unrotated #3 bolt lands at 216 -- inside a rotating shaft. At +36
        # the nearest bolts are 34 and 38 deg off the bore against a ~15 deg
        # exclusion. ADR-0061.
        phase=math.radians(36.0),
    )


def _cover_bolts(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    name_format: str,
    center: tuple[float, float, float],
    circle_radius: float,
    count: int,
    *,
    inward: bool,
    proud: float = 0.004,
    phase: float = 0.0,
) -> None:
    """A ring of hex bolt heads on a cover face normal to X.

    Indexed names rather than a running counter, for `build.py`'s rule 3: a bolt
    named from a counter moves when a part is inserted upstream, and the name is
    the glTF node name.

    The first bolt is at angle zero — straight up — rather than at half a pitch,
    so that a cover with an even count has one at top and one at bottom, which is
    what the eye checks a bolt circle against. `phase` rotates the whole ring for
    the one cover where a position is physically occupied: the ignition ring's
    Ø84 circle passes within 0.2 mm of the output shaft's axis, so its unrotated
    #3 bolt sat inside the output bore.
    """
    height = 0.005
    # A lathe about X grows from its center along +X, so a bolt head standing
    # proud of an *inboard* face has to be started `height` back from it.
    base_x = center[0] - height if inward else center[0] - proud
    for index in range(count):
        angle = 2.0 * math.pi * index / count + phase
        _hex_nut(
            name_format % index,
            (
                base_x,
                center[1] + math.sin(angle) * circle_radius,
                center[2] + math.cos(angle) * circle_radius,
            ),
            0.011,
            height,
            context,
            collection,
            root,
            material,
            axis="X",
        )


def _cylinder(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    crank_top: float,
) -> None:
    """Square base flange, round water jacket, exhaust manifold and springs.

    The shape is one correction and the **25 degree forward lean** is the other.
    See `CYLINDER_RADIUS`'s docstring for why a KZ barrel carries no fins, and
    `params.cylinder_lean` for why the barrel is not upright: with a vertical
    cylinder the sourced 25 degree port angle points the pipe's inlet axis 25
    degrees *down*, and the pipe is then 51 to 63 mm inside the rear axle at every
    roll of the bend plane. At 25 degrees of lean the inlet axis is horizontal.

    The cluster is authored upright and tipped once, about the lateral axis through
    the base-face centre. Authoring fifteen leaned parts by hand is how one of them
    ends up at a different angle from the rest.

    **What the lean does not do is re-cut the crankcase.** §30.4's inclined deck
    plane is expressed here by the *flange* leaning on a case whose top stays flat
    at z 240, so the flange's forward corner dips into the casting. That pair is a
    declared `bolted` joint and gate 1 permits it; a prismatic deck is a crankcase
    change and this wave did not make one. Recorded so nobody reads the flat top as
    an oversight.
    """
    axis = (CYLINDER_AXIS_X, CYLINDER_AXIS_Y)
    lean = _lean(context.params)

    _block(
        "engine_cylinder_base",
        (axis[0] - CYLINDER_BASE_HALF[0], axis[1] - CYLINDER_BASE_HALF[1], crank_top),
        (axis[0] + CYLINDER_BASE_HALF[0], axis[1] + CYLINDER_BASE_HALF[1], CYLINDER_BASE_TOP_Z),
        context,
        collection,
        root,
        material,
        transform=lean,
    )

    # Four base studs, one near each corner of the flange, inset far enough that
    # the nut's own hexagon sits inside the flange rather than overhanging it.
    for index, (sign_x, sign_y) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        _hex_nut(
            "engine_cylinder_base_nut_%d" % index,
            (
                axis[0] + sign_x * (CYLINDER_BASE_HALF[0] - 0.014),
                axis[1] + sign_y * (CYLINDER_BASE_HALF[1] - 0.014),
                CYLINDER_BASE_TOP_Z,
            ),
            0.013,
            0.008,
            context,
            collection,
            root,
            material,
            transform=lean,
        )

    # The jacket. Slightly barrelled — widest just above the flange and drawn in
    # again at the top — because a sand casting has draft on it and a perfect
    # cylinder reads as turned bar.
    _lathe_object(
        "engine_cylinder",
        [
            (0.0, CYLINDER_BASE_TOP_Z - 0.004),
            (CYLINDER_RADIUS - 0.004, CYLINDER_BASE_TOP_Z - 0.004),
            (CYLINDER_RADIUS, CYLINDER_BASE_TOP_Z + 0.006),
            (CYLINDER_RADIUS, CYLINDER_TOP_Z - 0.020),
            (CYLINDER_RADIUS - 0.003, CYLINDER_TOP_Z - 0.006),
            (HEAD_RADIUS + 0.002, CYLINDER_TOP_Z),
            (0.0, CYLINDER_TOP_Z),
        ],
        (axis[0], axis[1], 0.0),
        context,
        collection,
        root,
        material,
        axis="Z",
        transform=lean,
    )

    _manifold(context, collection, root, material)


def _lean(p: P.KartParams) -> Matrix:
    """The cylinder cluster's 25 degree forward tip, about the base-face centre.

    Forward means the top moves toward **+Y**, so it is a rotation about +X by
    *minus* `cylinder_lean`: `Matrix.Rotation(+a, 'X')` sends +Z to (0, -sin, cos),
    which leans the barrel *backwards* over the axle. Getting this sign wrong is
    invisible in plan and obvious in a side elevation, which is the same class of
    error as the inclined steering hub in §40.
    """
    pivot = Vector((CYLINDER_AXIS_X, CYLINDER_AXIS_Y, CYLINDER_BASE_Z))
    return (
        Matrix.Translation(pivot)
        @ Matrix.Rotation(-p.cylinder_lean, 4, "X")
        @ Matrix.Translation(-pivot)
    )


def _manifold(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
) -> None:
    """`exhaust_manifold` — renamed from `exhaust_flange`, because there is no
    flange on the pipe.

    `notes_exhaust.md` §1 is unambiguous: a short manifold bolts to the cylinder and
    the pipe **slips over its spigot on springs**. So the part is a plate plus a
    spigot rather than a loose ring, and the slip joint is what lets the chamber
    articulate -- which is why the support downstream is a spring cradle and not a
    bolt.

    Four M6 bolts, `sourced` (kartshop, *"TM KZ manifold D2 28: 4x allen bolt M6 x
    20 mm"*) on a 62 x 44 pattern measured off HF p. 3's base view. **The build had
    two.** The port face is *derived* rather than authored: it is where the
    horizontal port axis leaves the leaned Ø128 jacket, and `EXHAUST_INLET` is
    `MANIFOLD_LENGTH` further down the same axis.
    """
    inlet = Vector(EXHAUST_INLET)
    face_y = inlet.y + MANIFOLD_LENGTH + 0.004
    plate_rear = face_y - MANIFOLD_PLATE[2]

    _block(
        "exhaust_manifold",
        (inlet.x - MANIFOLD_PLATE[0] * 0.5, plate_rear, inlet.z - MANIFOLD_PLATE[1] * 0.5),
        (inlet.x + MANIFOLD_PLATE[0] * 0.5, face_y, inlet.z + MANIFOLD_PLATE[1] * 0.5),
        context,
        collection,
        root,
        material,
    )
    # The spigot the pipe slips over, from the plate's rear face to the inlet.
    _lathe_object(
        "exhaust_manifold_spigot",
        [
            (0.0, inlet.y),
            (MANIFOLD_SPIGOT_DIAMETER * 0.5, inlet.y),
            (MANIFOLD_SPIGOT_DIAMETER * 0.5, plate_rear),
            (0.0, plate_rear),
        ],
        (inlet.x, 0.0, inlet.z),
        context,
        collection,
        root,
        material,
        axis="Y",
    )

    half = (MANIFOLD_BOLT_PATTERN[0] * 0.5, MANIFOLD_BOLT_PATTERN[1] * 0.5)
    for index, (sx, sz) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        _hex_nut(
            "exhaust_manifold_bolt_%d" % index,
            (inlet.x + sx * half[0], face_y, inlet.z + sz * half[1]),
            MANIFOLD_BOLT_FLATS,
            0.007,
            context,
            collection,
            root,
            material,
            axis="Y",
        )

    # Two springs, from the plate back to the bent tabs at s ~ 70. Their station is
    # `measured` off KZ-R2 HF p. 13; the hook geometry is a helix because the pitch
    # is what the eye reads and a plain cylinder does not have one.
    centreline, radii = _exhaust_centerline(context)
    tab = _station(centreline, EXHAUST_SPRING_STATION)
    tab_radius = _station_radius(centreline, radii, EXHAUST_SPRING_STATION)
    for index, sign in enumerate((-1, 1)):
        start = Vector((inlet.x + sign * half[0], face_y + 0.0035, inlet.z))
        end = tab + Vector((sign * (tab_radius + 0.004), 0.0, 0.0))
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            _helix(
                start,
                end,
                EXHAUST_SPRING_COIL_RADIUS,
                EXHAUST_SPRING_TURNS,
                EXHAUST_SPRING_SAMPLES,
            ),
            EXHAUST_SPRING_WIRE_RADIUS,
            6,
        )
        spring = build.object_from_bmesh(
            "exhaust_spring_%d" % index,
            bm,
            collection,
            material=context.material("axle_steel"),
            shade_smooth=True,
        )
        build.set_parent(spring, root)


def _head(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    head_top: float,
) -> None:
    """The head casting, its six nuts, the spark plug, and the water outlet.

    All of it is **carried by the cylinder's 25 degree lean**, which is why every
    part here takes the same `transform`: the head bolts to the barrel, so it cannot
    have an orientation of its own.
    """
    axis = (CYLINDER_AXIS_X, CYLINDER_AXIS_Y)
    lean = _lean(context.params)

    _lathe_object(
        "engine_head",
        [
            (0.0, CYLINDER_TOP_Z),
            (HEAD_RADIUS, CYLINDER_TOP_Z),
            (HEAD_RADIUS, head_top - 0.010),
            (HEAD_RADIUS - 0.007, head_top),
            (0.0, head_top),
        ],
        (axis[0], axis[1], 0.0),
        context,
        collection,
        root,
        material,
        axis="Z",
        transform=lean,
    )

    for index in range(HEAD_BOLT_COUNT):
        angle = 2.0 * math.pi * index / HEAD_BOLT_COUNT
        _hex_nut(
            "engine_head_nut_%d" % index,
            (
                axis[0] + math.cos(angle) * HEAD_BOLT_CIRCLE,
                axis[1] + math.sin(angle) * HEAD_BOLT_CIRCLE,
                head_top - 0.002,
            ),
            HEAD_NUT_FLATS,
            0.009,
            context,
            collection,
            root,
            material,
            transform=lean,
        )

    _spark_plug(context, collection, root, head_top)

    # Water outlet on the head's front face, inboard of the bore, where the top
    # hose lands. Sharing the front face with the exhaust port is not a conflict:
    # the port is on the *cylinder* at z 0.288 and this is on the head 88 mm
    # above it. `HOSE_UPPER`'s last control point is this boss's mouth.
    _lathe_object(
        "engine_water_outlet",
        _disc_profile(0.015, 0.018),
        WATER_OUTLET_LOCAL,
        context,
        collection,
        root,
        material,
        axis="Y",
        transform=lean,
    )

    # `engine_water_pump` used to be a boss on this casting and it is **gone**:
    # Art. 5.3.2 permits the pump to be driven by the engine *or* by the rear wheel
    # axle, the KZ trade sells it as "KZ water pump with HTD axle pulley and tooth
    # belt", and `_cooling` builds it on the axle as `cooling_pump_body`. What the
    # crankcase needs instead is somewhere for the coolant to *enter*, because
    # Art. 9.10.1 water-cools the case as well as the barrel and the head -- there
    # was no such part, which is why the bottom hose ended on the clutch cover.
    _lathe_object(
        "engine_water_inlet",
        [
            (0.0, -0.014),
            (0.010, -0.014),
            (0.012, -0.010),
            (0.012, 0.0),
            (0.0, 0.0),
        ],
        WATER_INLET_BOSS,
        context,
        collection,
        root,
        material,
    )


def _spark_plug(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    head_top: float,
) -> None:
    """Boss, plug body, insulator, cap and lead, on the bore axis, leaning with it.

    The plug is on the bore axis and nowhere else — it fires into the middle of
    the combustion chamber — so it is placed from `CYLINDER_AXIS_*` rather than
    given coordinates of its own. That also means it cannot drift off center if
    the cylinder moves.
    """
    axis_x, axis_y = CYLINDER_AXIS_X, CYLINDER_AXIS_Y
    alloy = context.material("engine_cast")
    lean = _lean(context.params)

    boss_top = head_top + PLUG_BOSS_HEIGHT
    _lathe_object(
        "engine_plug_boss",
        [
            (0.0, head_top - 0.004),
            (PLUG_BOSS_RADIUS, head_top - 0.004),
            (PLUG_BOSS_RADIUS, boss_top - 0.003),
            (PLUG_BOSS_RADIUS - 0.004, boss_top),
            (0.0, boss_top),
        ],
        (axis_x, axis_y, 0.0),
        context,
        collection,
        root,
        alloy,
        axis="Z",
        transform=lean,
    )

    hex_top = boss_top + 0.010
    _hex_nut(
        "engine_plug_hex",
        (axis_x, axis_y, boss_top),
        PLUG_HEX_FLATS,
        0.010,
        context,
        collection,
        root,
        context.material("axle_steel"),
        transform=lean,
    )

    # The insulator has to stand clear of the cap, not disappear into it. At the
    # first attempt the cap started 14 mm above the hex and covered all but a
    # sliver, so the one bright white object on the engine — the whole reason to
    # model a plug at all — was invisible in the render. 26 mm of porcelain
    # shows now, which is about what R2 has.
    insulator_top = hex_top + 0.038
    _lathe_object(
        "engine_plug_insulator",
        [
            (0.0, hex_top),
            (PLUG_INSULATOR_RADIUS + 0.002, hex_top),
            (PLUG_INSULATOR_RADIUS, hex_top + 0.007),
            (PLUG_INSULATOR_RADIUS, insulator_top - 0.006),
            (PLUG_INSULATOR_RADIUS - 0.002, insulator_top),
            (0.0, insulator_top),
        ],
        (axis_x, axis_y, 0.0),
        context,
        collection,
        root,
        context.material("plug_ceramic"),
        axis="Z",
        transform=lean,
    )

    # The cap slips over the insulator and leans back off the bore axis, and the
    # lead runs from it down the back of the engine. Two swept tubes: the cap is
    # short and fat, the lead is long and thin, and it is the lead that reads at
    # a distance.
    cap_base = lean @ Vector((axis_x, axis_y, hex_top + 0.026))
    cap_top = lean @ Vector((axis_x - 0.004, axis_y - 0.028, hex_top + 0.058))
    rubber = context.material("rubber_grip")
    bm = bmesh.new()
    build.sweep_tube(
        bm,
        [cap_base, cap_base.lerp(cap_top, 0.55), cap_top],
        PLUG_CAP_DIAMETER * 0.5,
        context.detail.tube_segments,
    )
    cap = build.object_from_bmesh(
        "engine_plug_cap", bm, collection, material=rubber, shade_smooth=True
    )
    build.set_parent(cap, root)

    _tube_object(
        "engine_plug_lead",
        (
            (cap_top.x, cap_top.y, cap_top.z - 0.004),
            (0.382, -0.250, 0.318),
            (0.406, -0.300, 0.286),
            (0.404, -0.340, 0.216),
        ),
        PLUG_LEAD_DIAMETER,
        context,
        collection,
        root,
        rubber,
        bend_radius=0.030,
    )


# --- intake ----------------------------------------------------------------


def _intake(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Carburetor, float bowl, boot and airbox, all behind the engine.

    The direction matters more than the shapes do: a case-reed KZ breathes
    through the *rear* of the crankcase, which is what puts the exhaust port on
    the front of the barrel and settles the whole exhaust routing. Building the
    intake first and the exhaust after it is the order that keeps the two from
    being decided independently and ending up on the same side.
    """
    material = context.material("engine_cast")

    # The reed cage: a raised bolting frame round a recessed face, which is the
    # cheapest pair of boxes that reads as a casting bolted to another casting.
    _block(
        "engine_reed_block",
        REED_BLOCK_LO,
        REED_BLOCK_HI,
        context,
        collection,
        root,
        material,
    )
    _block(
        "engine_reed_face",
        (
            REED_BLOCK_LO[0] + REED_FRAME_INSET,
            REED_BLOCK_LO[1] - 0.006,
            REED_BLOCK_LO[2] + REED_FRAME_INSET,
        ),
        (
            REED_BLOCK_HI[0] - REED_FRAME_INSET,
            REED_BLOCK_LO[1] + 0.004,
            REED_BLOCK_HI[2] - REED_FRAME_INSET,
        ),
        context,
        collection,
        root,
        material,
    )

    # The carburetor body, turned about Y with a spigot at each end: 35 mm into
    # the reed block, 64 mm out to the air boot. Both are the VHSH's published
    # sizes — see `CARB_AXIS_X`'s docstring.
    _lathe_object(
        "engine_carb",
        # Rear end first: +Y is forward, so the air side is the low end of the
        # profile's axis coordinate. Getting this backwards builds the whole
        # carburetor inside out, which is what the winding gate caught.
        [
            (0.0, CARB_REAR_Y),
            (CARB_SPIGOT_AIR_RADIUS, CARB_REAR_Y),
            (CARB_SPIGOT_AIR_RADIUS, CARB_REAR_Y + 0.010),
            (CARB_BODY_RADIUS, CARB_REAR_Y + 0.014),
            (CARB_BODY_RADIUS, CARB_FRONT_Y - 0.005),
            (CARB_SPIGOT_ENGINE_RADIUS, CARB_FRONT_Y),
            (CARB_SPIGOT_ENGINE_RADIUS, CARB_FRONT_Y + 0.014),
            (0.0, CARB_FRONT_Y + 0.014),
        ],
        (CARB_AXIS_X, 0.0, CARB_AXIS_Z),
        context,
        collection,
        root,
        material,
        axis="Y",
    )

    # Wrong way round before: a box body with a turned bowl. R2 has a turned
    # body with a **box** bowl hanging off its underside, forward of center,
    # with a drain plug in the bottom.
    _block(
        "engine_carb_bowl",
        CARB_BOWL_LO,
        CARB_BOWL_HI,
        context,
        collection,
        root,
        material,
    )

    # The top cap, standing straight up out of the body with the throttle cable
    # entering it. Two turned steps and a rubber sleeve; it is small, and it is
    # most of what stops the carburetor reading as a pipe.
    cap_y = (CARB_FRONT_Y + CARB_REAR_Y) * 0.5 - 0.004
    _lathe_object(
        "engine_carb_cap",
        [
            (0.0, CARB_AXIS_Z),
            (CARB_TOP_CAP_RADIUS, CARB_AXIS_Z),
            (CARB_TOP_CAP_RADIUS, CARB_TOP_CAP_TOP_Z - 0.006),
            (CARB_TOP_CAP_RADIUS - 0.005, CARB_TOP_CAP_TOP_Z),
            (0.0, CARB_TOP_CAP_TOP_Z),
        ],
        (CARB_AXIS_X, cap_y, 0.0),
        context,
        collection,
        root,
        material,
        axis="Z",
    )
    _tube_object(
        "engine_throttle_cable",
        (
            (CARB_AXIS_X, cap_y, CARB_TOP_CAP_TOP_Z - 0.004),
            (CARB_AXIS_X - 0.024, cap_y - 0.006, CARB_TOP_CAP_TOP_Z + 0.028),
            (CARB_AXIS_X - 0.056, cap_y + 0.050, CARB_TOP_CAP_TOP_Z + 0.044),
        ),
        0.008,
        context,
        collection,
        root,
        context.material("rubber_grip"),
        bend_radius=0.025,
    )

    # The airbox, as a molded body and a clipped-on lid rather than one brick.
    plastic = context.material("frame_powdercoat")
    lid_z = AIRBOX_HI[2] - 0.026
    _block(
        "engine_airbox",
        AIRBOX_LO,
        (AIRBOX_HI[0], AIRBOX_HI[1], lid_z),
        context,
        collection,
        root,
        plastic,
    )
    _block(
        "engine_airbox_lid",
        (AIRBOX_LO[0] + 0.006, AIRBOX_LO[1] + 0.006, lid_z),
        (AIRBOX_HI[0] - 0.006, AIRBOX_HI[1] - 0.006, AIRBOX_HI[2]),
        context,
        collection,
        root,
        plastic,
    )

    # The boot has to cross the pipe's z band now, and it can only do it **inboard
    # of the pipe**: at x 250 +- 34 it clears the pipe's inboard face at 294 by 9 mm
    # and the seat shell at 184 by 32. A boot that went straight up from the
    # carburettor to a box 60 mm higher would pass through the chamber.
    _tube_object(
        "engine_intake_boot",
        (
            (CARB_AXIS_X, CARB_REAR_Y + 0.004, CARB_AXIS_Z),
            (0.268, -0.448, CARB_AXIS_Z),
            (0.252, -0.478, 0.330),
            (0.268, -0.470, 0.372),
        ),
        INTAKE_BOOT_DIAMETER,
        context,
        collection,
        root,
        context.material("rubber_grip"),
        bend_radius=0.040,
    )

    # Art. 9.13.1's two ducts, Ø30.0 maximum, projecting forward out of the box's
    # front wall. Compulsory, and absent until now.
    for index, duct_x in enumerate(AIRBOX_DUCT_X):
        _lathe_object(
            "engine_airbox_duct_%d" % index,
            [
                (0.0, AIRBOX_HI[1]),
                (AIRBOX_DUCT_DIAMETER * 0.5, AIRBOX_HI[1]),
                (AIRBOX_DUCT_DIAMETER * 0.5, AIRBOX_HI[1] + AIRBOX_DUCT_LENGTH),
                (AIRBOX_DUCT_DIAMETER * 0.5 - 0.003, AIRBOX_HI[1] + AIRBOX_DUCT_LENGTH),
                (0.0, AIRBOX_HI[1] + AIRBOX_DUCT_LENGTH),
            ],
            (duct_x, 0.0, AIRBOX_DUCT_Z),
            context,
            collection,
            root,
            plastic,
            axis="Y",
        )


# --- driveline -------------------------------------------------------------


def _driveline(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Sprocket carrier, output shaft, output sprocket, chain and the guard.

    The chain is built from the two sprockets' pitch circles rather than drawn:
    the two straight runs are the circles' external tangents and the two arcs are
    the wraps between the tangent points, so the wrap angles are what the radii
    and the center distance make them. A hand-drawn loop is the version that
    looks subtly wrong at the small sprocket, where the wrap is only 152 degrees
    and any error in it is the whole shape.

    **The pitch radius is `p / (2 sin(pi/N))` and this used to be `p*N/(2*pi)`.**
    The approximation is 0.03% small at 82 teeth and 1.1% small at 12, i.e. 0.24 mm
    of radius -- and that 0.24 mm is what put the chain's inner strand 1.9 mm inside
    a Ø18 output shaft.

    **The shaft is Ø12 outboard of x 430 and the margins that once read 1.0 and 2.7 mm
    were measured to the wrong circle.** They were taken to the *pitch* circle at
    10.745 mm, and the chain is a 9 x 8 mm band straddling the tooth, so its inner
    edge is `CHAIN_HALF_HEIGHT` further in at **7.245 mm** from the axis. Against that:

        Ø18   9.000   1.755 mm INSIDE the band
        Ø16   8.000   0.755 mm INSIDE the band
        Ø12   6.000   1.245 mm of clearance          <- built

    13 teeth is the other answer and it is a worse one: `p / (2 sin(pi/13))` is 11.622,
    so the band's inner edge moves out to 8.122 and Ø16 clears it by **0.122 mm**. A
    tenth of a millimetre is not a clearance, and `sprocket_teeth_engine` is
    `estimated` inside a real 10-14 range -- bending a ratio to rescue a shaft diameter
    would put a gear ratio at the mercy of a lathe operation. Ø12 is the shaft's own
    retaining nose, which is what a real gearbox output has there anyway: the sprocket
    is on the end of the shaft, not partway along it.

    No teeth. They cannot come out of a revolution and a hundred of them would
    cost more than the rest of this module; `wheels.py` reaches the same
    conclusion about the axle sprocket for the same reason. The chain sits on the
    pitch circle and therefore interpenetrates both sprocket discs, which is what
    a roller chain straddling a tooth actually does.
    """
    p = context.params
    material = context.material("axle_steel")

    pitch_radius = P.sprocket_pitch_radius(p.chain_pitch, p.sprocket_teeth_engine)

    pivot = build.empty(
        "engine_sprocket",
        (p.chain_x, SPROCKET_Y, p.engine_z),
        collection,
        size=0.05,
    )
    context.publish("engine_sprocket", pivot)
    build.set_parent(pivot, root)
    # `build.set_parent` reads `parent.matrix_world`, which for an empty created
    # this tick is still the identity until the depsgraph is evaluated — the same
    # trap `wheels.py` documents at its hubs. One evaluation here.
    bpy.context.view_layer.update()

    # Sprocket carrier: the boss the output shaft comes out of, on the DRIVE
    # (outboard) face -- KZ-R1 HF p. 1; the clutch owns the inboard face. Spans
    # 393..428: 5 mm into the case's outboard face at 398 so the joint is metal
    # rather than a kiss, 30 mm proud, and stops 2 mm short of the ignition
    # cover's face plane at 430 -- their y bands (-300..-236 vs -242..-138) are
    # what actually keeps them apart. The old inboard boss protruded 60 because
    # chain_x 0.115 was 125 mm from the case face; the real stack is short.
    _lathe_object(
        "drive_sprocket_carrier",
        _disc_profile(0.032, 0.0175),
        (0.4105, SPROCKET_Y, p.engine_z),
        context,
        collection,
        root,
        context.material("engine_cast"),
    )

    # Stepped: Ø18 where it seats in the carrier (423..430, 5 mm inside the
    # boss's outboard face and clear of the cases -- the carrier bridges to the
    # crankcase, the shaft only ever touches the carrier and the cover it
    # pierces), Ø12 nose 430..460 where the chain wraps it. Sprocket plane at
    # `chain_x` 445 is mid-nose.
    _lathe_object(
        "drive_output_shaft",
        [
            (0.0, 0.423),
            (0.009, 0.423),
            (0.009, 0.430),
            (0.006, 0.430),
            (0.006, 0.460),
            (0.0, 0.460),
        ],
        (0.0, SPROCKET_Y, p.engine_z),
        context,
        collection,
        root,
        material,
    )

    bm = bmesh.new()
    build.lathe(
        bm,
        _disc_profile(pitch_radius, 0.0035),
        context.detail.exhaust_segments,
        axis="X",
        center=(p.chain_x, SPROCKET_Y, p.engine_z),
    )
    sprocket = build.object_from_bmesh(
        "drive_output_sprocket", bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(sprocket, pivot)

    large_radius = P.sprocket_pitch_radius(p.chain_pitch, p.sprocket_teeth_axle)
    _chain(context, collection, root, material, pitch_radius, large_radius)
    _chain_guard(context, collection, root, large_radius)


def _belt_path(
    small: Vector,
    small_radius: float,
    large: Vector,
    large_radius: float,
    steps: int,
) -> list[tuple[float, float]]:
    """The closed external-tangent loop round two circles, in one plane.

    Two consumers: the drive chain and the water pump's toothed belt. The geometry
    is identical and writing it twice is how the two end up with different wrap
    conventions.
    """
    delta = large - small
    distance = delta.length
    alpha = math.atan2(delta.y, delta.x)
    cosine = max(-1.0, min(1.0, (large_radius - small_radius) / distance))
    beta = math.acos(cosine)

    # Tangent points share an angle on both circles, because on an external
    # tangent the two radii to the contact points are parallel.
    theta_a = alpha + math.pi - beta
    theta_b = alpha + math.pi + beta

    def arc(
        center: Vector, radius: float, start: float, end: float, count: int
    ) -> list[tuple[float, float]]:
        points: list[tuple[float, float]] = []
        for index in range(count):
            angle = start + (end - start) * index / count
            points.append(
                (
                    center.x + math.cos(angle) * radius,
                    center.y + math.sin(angle) * radius,
                )
            )
        return points

    path: list[tuple[float, float]] = []
    # Wrap on the small pulley, 2 * beta, on the side facing away from the axle.
    path.extend(arc(small, small_radius, theta_a, theta_b, steps))
    # Wrap on the large one, 2 * pi - 2 * beta, the long way round.
    path.extend(arc(large, large_radius, theta_b, theta_a + 2.0 * math.pi, steps * 3))
    return path


def _chain(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    small_radius: float,
    large_radius: float,
) -> None:
    """The closed belt path around the two sprockets, in the plane x = chain_x.

    `large_radius` is `p/(2 sin(pi/82))` = **72.615**, which reproduces the Ø145
    `wheels.py` builds to 0.2 mm -- so 145 *is* an 82-tooth 219 sprocket and the
    number now has a tooth count behind it rather than being two literals that
    happen to agree.
    """
    p = context.params
    small = Vector((SPROCKET_Y, p.engine_z))
    large = Vector((P.rear_axle_y(p), P.rear_axle_z(p)))
    steps = max(6, context.detail.exhaust_segments)

    bm = bmesh.new()
    _ribbon(
        bm,
        _belt_path(small, small_radius, large, large_radius, steps),
        p.chain_x,
        CHAIN_HALF_WIDTH,
        CHAIN_HALF_HEIGHT,
    )
    obj = build.object_from_bmesh("drive_chain", bm, collection, material=material)
    build.set_parent(obj, root)


def _slab(
    bm: bmesh.types.BMesh,
    a: tuple[float, float],
    b: tuple[float, float],
    x_low: float,
    x_high: float,
    depth: float,
) -> None:
    """One closed facet of a sheet part: a (y, z) segment given width in x and
    `depth` through its own in-plane normal.

    Built with `build.box` and a rotation about X rather than by emitting eight
    vertices by hand, because `build.box`'s winding is known-good and hand-wound
    boxes in this repo were inside out for two milestones without any render
    showing it.
    """
    direction = Vector((b[0] - a[0], b[1] - a[1]))
    length = direction.length
    if length < 1e-9:
        return
    angle = math.atan2(direction.y, direction.x)
    center = (
        (x_low + x_high) * 0.5,
        (a[0] + b[0]) * 0.5,
        (a[1] + b[1]) * 0.5,
    )
    build.box(
        bm,
        (x_high - x_low, length, depth),
        center,
        rotation=Matrix.Rotation(angle, 3, "X"),
    )


def _chain_guard(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    large_radius: float,
) -> None:
    """Art. 5.9's compulsory guard. See `CHAIN_GUARD_X` for the article.

    Built as a **cover** rather than as a solid: two thin side walls plus the crown
    facets over the top, all in one mesh. A solid prism spanning the guard's lateral
    band would swallow the chain and both sprockets, which is the shape the name
    invites and the opposite of the part.

    The lower edge is `rear_axle_z` because the article says *"down to the centre of
    the crown wheel axis"* and that is where the axis is. It is `pierced` by the axle
    and by the output shaft and declared as such, which is what stops gate 1 reading
    a compulsory part as a collision.
    """
    p = context.params
    lower = P.rear_axle_z(p)
    axle_y = P.rear_axle_y(p)
    small_crown = p.engine_z + 0.0107 + 0.022

    # (y, z) outline of the guard's edge, front (nearest the engine) to rear.
    #
    # **The rear half is a semicircle about the axle, and that is the article rather
    # than a style.** Art. 5.9 requires the guard *"down to the centre of the crown
    # wheel axis"*, and a crown that runs flat across the top of the crown wheel and
    # then stops -- which is what this outline used to do -- covers it to z 204 and
    # leaves 56 mm of open sprocket below. Following the crown wheel's own circle down
    # to `lower` at both ends is the only shape that satisfies the sentence.
    radius = large_radius + 0.010
    outline = [
        (CHAIN_GUARD_Y[1], lower),
        (CHAIN_GUARD_Y[1], small_crown),
        (SPROCKET_Y, small_crown),
    ]
    for step in range(9):
        angle = math.radians(24.0 + (180.0 - 24.0) * step / 8.0)
        outline.append((axle_y + math.cos(angle) * radius, lower + math.sin(angle) * radius))

    bm = bmesh.new()
    for wall_low in (CHAIN_GUARD_X[0], CHAIN_GUARD_X[1] - CHAIN_GUARD_WALL):
        for index in range(len(outline) - 1):
            _slab(
                bm,
                outline[index],
                outline[index + 1],
                wall_low,
                wall_low + CHAIN_GUARD_WALL,
                CHAIN_GUARD_WALL,
            )
    # The crown, spanning both walls along the outline's upper edge.
    for index in range(1, len(outline) - 1):
        _slab(
            bm,
            outline[index],
            outline[index + 1],
            CHAIN_GUARD_X[0],
            CHAIN_GUARD_X[1],
            CHAIN_GUARD_WALL,
        )

    guard = build.object_from_bmesh(
        "drive_chain_guard",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
    )
    build.set_parent(guard, root)

    # The mounting stay, onto the right bearing hanger's outboard face. The old
    # anchor was `chassis_cross_rear`, but that member spans +-310 and the
    # guard's walls now sit at 431..463: a leg dropped straight down from the
    # wall lands 117 mm outboard of any metal, and a diagonal back to the
    # member's top runs straight through the hanger plate (294..306) on the
    # way. So the stay bolts to the plate itself -- which is where a real
    # guard's bracket lands, beside the cassette -- running from its outboard
    # face at (306, -540, 100) up-out-rearward to the guard's rear-lower
    # corner, over the rail (crosses x 310 at z ~101 against a rail top of 66)
    # and under the axle (z ~106 at y -550 against an axle underside of 115).
    _tube_object(
        "drive_chain_guard_flange",
        (
            (0.306, axle_y - 0.015, 0.100),
            # Ends ON the inner wall's outboard face, not 4 mm shy of it: the
            # stay runs mostly along x now, so the tube's end cap has almost no
            # radial reach in x and an endpoint short of the sheet never
            # touches the part it is welded to.
            (CHAIN_GUARD_X[0] + 0.002, axle_y - radius + 0.004, lower),
        ),
        0.010,
        context,
        collection,
        root,
        context.material("frame_powdercoat"),
        bend_radius=0.020,
    )


# --- exhaust ---------------------------------------------------------------


def _exhaust_centerline(
    context: build.BuildContext,
) -> tuple[list[Vector], list[float]]:
    """The chamber's centreline and its outside radius at every sample.

    **A construction, and it says so.** No accessible photograph anywhere shows a KZ
    expansion chamber fitted to a kart -- every manufacturer display kart in `refs/`
    is shot without one -- so this is sourced part geometry plus regulation
    constraints, not a measurement. What is sourced is the cone table; what is
    constructed is where it points.

    Three placement rules, and nothing else:

        inlet face     EXHAUST_INLET, derived off the leaned port
        inlet axis     (0, -1, 0).  Plan 0 because the port is square in plan and
                       the crank must be parallel to the rear axle for the chain to
                       run at all, so the engine is square on its mount and there is
                       no yaw to inherit.  Elevation 0 by the 25 degree lean.
        bend           in a plane tilted EXHAUST_BEND_TILT nose-down, turning
                       inboard, by EXHAUST_CONE_TURN degrees per cone

    The walk is arc-length exact by construction: each cone contributes its own
    sourced slant length and its own derived turn, so the developed length is the
    table's 674.6 mm however finely it is sampled. Sampling density comes from
    `context.detail`, so low and high are the same pipe at two densities.

    **Where this disagrees with spec §30.6.3's centreline table, the table is the one
    that is wrong, and the disagreement is one station wide.** Measured against all
    eight of the table's own rows, the built walk is within **0.4 mm** on every axis at
    s = 0, 135, 213, 409, 472, 513 and 675 -- and **-15.0 mm in x at s 623**, with y and
    z still inside 0.1. That is exactly where the table contradicts itself: its stations
    at s 513 and 623 are 95.2 mm apart in space --
    `sqrt(92.4^2 + 2.4^2 + 22.7^2)` -- across cone 14, whose two `sourced` slant lengths
    make it **109.5 mm** long. A row cannot be 109.5 mm of pipe and 95.2 mm of chord at a
    3.6 degree turn. The cone lengths come off the homologation forms and the station
    list was derived from them, so the sourced figures win and the table's s-623 row is
    the one to correct. Nothing else on the pipe moves.
    """
    tilt = EXHAUST_BEND_TILT
    forward = Vector((0.0, -1.0, 0.0))
    inward = Vector((-math.cos(tilt), 0.0, -math.sin(tilt)))

    steps_per_cone = max(2, context.detail.exhaust_segments // 4)
    point = Vector(EXHAUST_INLET)
    turned = 0.0
    path: list[Vector] = [point.copy()]
    radii: list[float] = [EXHAUST_CONES[0][2] * 0.5]

    for (start, end, dia_start, dia_end), turn in zip(EXHAUST_CONES, EXHAUST_CONE_TURN):
        span = end - start
        delta = math.radians(turn) / steps_per_cone
        for step in range(steps_per_cone):
            direction = forward * math.cos(turned) + inward * math.sin(turned)
            point = point + direction * (span / steps_per_cone)
            turned += delta
            fraction = (step + 1) / steps_per_cone
            path.append(point.copy())
            radii.append((dia_start + (dia_end - dia_start) * fraction) * 0.5)
    return path, radii


def _station(path: list[Vector], target: float) -> Vector:
    """The point at developed length `target` along a polyline."""
    travelled = 0.0
    for index in range(len(path) - 1):
        step = (path[index + 1] - path[index]).length
        if travelled + step >= target:
            fraction = 0.0 if step < 1e-12 else (target - travelled) / step
            return path[index].lerp(path[index + 1], fraction)
        travelled += step
    return path[-1].copy()


def _station_radius(path: list[Vector], radii: list[float], target: float) -> float:
    """The outside radius at developed length `target`."""
    travelled = 0.0
    for index in range(len(path) - 1):
        step = (path[index + 1] - path[index]).length
        if travelled + step >= target:
            return radii[index]
        travelled += step
    return radii[-1]


def _exhaust(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The chamber, the U-bend, the silencer and every mount either of them needs.

    Art. **5.10**, PDF p. 17, is the article this whole assembly answers: magnetic
    steel, sheet at least 0.75 mm, *"mandatory for the exhaust to pass rearward"*,
    the outlet's external diameter *"more than 3 cm"* and not past the kart's outer
    limits, and the system must discharge behind the driver. Art. **9.15.1** makes
    the HF's own pipe compulsory, which is what `EXHAUST_CONES` is.
    """
    material = context.material("exhaust_nickel")
    path, radii = _exhaust_centerline(context)

    bm = bmesh.new()
    _sweep_varying(bm, path, radii, context.detail.exhaust_segments)
    chamber = build.object_from_bmesh(
        "exhaust_chamber", bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(chamber, root)

    _exhaust_hanger(context, collection, root, path, radii)
    _silencer(context, collection, root, material, path[-1], radii[-1])


def _exhaust_hanger(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    path: list[Vector],
    radii: list[float],
) -> None:
    """Clamp, arm and cradle — replacing an `exhaust_hanger` bolted to nothing.

    See `HANGER_CLAMP_X` for why the anchor cannot be `chassis_side_bar_r`: that bar
    is a Ø20 bumper tube and the sourced mushroom clamp is bored 28, 30 or 32.
    """
    p = context.params
    steel = context.material("frame_powdercoat")
    cross_y = P.rear_axle_y(p)
    clamp_z = P.rail_z(p)

    # The split clamp body, bored on the cross member's own centreline so contact is
    # 0.0 by construction and `clamped` permits the facet overlap.
    _lathe_object(
        "exhaust_hanger_clamp",
        [
            (HANGER_CLAMP_BORE * 0.5, -0.015),
            (HANGER_CLAMP_OD * 0.5, -0.015),
            (HANGER_CLAMP_OD * 0.5, 0.015),
            (HANGER_CLAMP_BORE * 0.5, 0.015),
        ],
        (HANGER_CLAMP_X, cross_y, clamp_z),
        context,
        collection,
        root,
        steel,
        axis="X",
    )
    _block(
        "exhaust_hanger_boss",
        (HANGER_CLAMP_X - 0.010, cross_y - 0.010, clamp_z),
        (HANGER_CLAMP_X + 0.010, cross_y + 0.010, HANGER_BOSS_TOP_Z),
        context,
        collection,
        root,
        steel,
    )

    grip = _station(path, HANGER_CRADLE_STATION)
    grip_radius = _station_radius(path, radii, HANGER_CRADLE_STATION)

    # The arm's length is `derived` from the two ends it joins -- 169 mm -- rather
    # than read off a photograph with no dimensioned feature in it. It runs
    # **rearward at boss height first and rises afterwards**, because
    # `chassis_cross_rear` and `axle_rear` share a station: a straight chord from the
    # boss to the pipe passes through the axle, whose underside is at z 122.5 against
    # a boss top at 88. Held flat to y -0.600 it clears the axle by 22 mm.
    _tube_object(
        "exhaust_hanger_arm",
        (
            (HANGER_CLAMP_X, cross_y, HANGER_BOSS_TOP_Z - 0.004),
            (HANGER_CLAMP_X, -0.600, HANGER_BOSS_TOP_Z),
            (grip.x, grip.y, grip.z - grip_radius - 0.004),
        ),
        0.014,
        context,
        collection,
        root,
        steel,
        bend_radius=0.040,
    )

    # The cradle: a C of Ø6 spring wire round the baffle cone, open at the bottom
    # where the arm comes up to it.
    bm = bmesh.new()
    ring: list[Vector] = []
    for index in range(19):
        # 40 to 320 degrees, so the **bottom** of the ring is closed and the gap is
        # at the top where the pipe drops in. Opened the other way round the arm comes
        # up to a hole and the cradle grips nothing, which gate 2 read as 36 mm of air.
        angle = math.radians(40.0 + 280.0 * index / 18.0)
        ring.append(
            Vector(
                (
                    grip.x + math.sin(angle) * (grip_radius + HANGER_CRADLE_WIRE * 0.5),
                    grip.y,
                    grip.z + math.cos(angle) * (grip_radius + HANGER_CRADLE_WIRE * 0.5),
                )
            )
        )
    build.sweep_tube(bm, ring, HANGER_CRADLE_WIRE * 0.5, 6)
    cradle = build.object_from_bmesh(
        "exhaust_hanger_cradle",
        bm,
        collection,
        material=context.material("axle_steel"),
        shade_smooth=True,
    )
    build.set_parent(cradle, root)


def _silencer(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    stinger: Vector,
    stinger_radius: float,
) -> None:
    """The U-bend, the can, its saddle and both bands.

    The can lies **across** the kart, which is forced: the clear box behind the axle
    is about 365 mm deep and a 450 mm cylinder does not fit in it fore-and-aft. The
    stinger already points along -x, so the U-bend is what turns a leftward stinger
    back into a rightward body -- and that part is `sourced` as a catalogue item even
    though its route is `estimated`.
    """
    p = context.params
    steel = context.material("frame_powdercoat")
    low, high = SILENCER_SPAN
    body_low = low + 0.030
    body_high = high - 0.030
    axis_y, axis_z = SILENCER_AXIS_Y, SILENCER_AXIS_Z

    _tube_object(
        "exhaust_connector",
        (
            tuple(stinger),
            CONNECTOR_APEX,
            (low, axis_y, axis_z),
        ),
        CONNECTOR_DIAMETER,
        context,
        collection,
        root,
        material,
        bend_radius=0.045,
    )
    del stinger_radius

    radius = SILENCER_DIAMETER * 0.5
    _lathe_object(
        "exhaust_silencer",
        [
            (0.0, low),
            (SILENCER_INLET_DIAMETER * 0.5, low),
            (SILENCER_INLET_DIAMETER * 0.5, body_low - 0.004),
            (radius, body_low),
            (radius, body_high),
            (SILENCER_OUTLET_DIAMETER * 0.5, body_high + 0.004),
            (SILENCER_OUTLET_DIAMETER * 0.5, high),
            (0.0, high),
        ],
        (0.0, axis_y, axis_z),
        context,
        collection,
        root,
        material,
        axis="X",
    )

    outlet = build.empty(
        "exhaust_outlet", (high, axis_y, axis_z), collection, size=0.04
    )
    context.publish("exhaust_outlet", outlet)
    build.set_parent(outlet, root)

    # Bracket down from the cross member, a rubber isolator, then the saddle.
    cross_y = P.rear_axle_y(p)
    _lathe_object(
        "exhaust_silencer_bracket_clamp",
        [
            (HANGER_CLAMP_BORE * 0.5, -0.014),
            (HANGER_CLAMP_OD * 0.5, -0.014),
            (HANGER_CLAMP_OD * 0.5, 0.014),
            (HANGER_CLAMP_BORE * 0.5, 0.014),
        ],
        (SILENCER_BRACKET_X, cross_y, P.rail_z(p)),
        context,
        collection,
        root,
        steel,
        axis="X",
    )

    saddle_top = axis_z - radius + 0.012
    # Same problem as the pipe support's arm and the same answer: rearward at rail
    # height first, then up. A straight run from the cross member to the saddle passes
    # through `axle_rear`, and at x 230 it would also cross the water pump's belt
    # plane -- which is why the bracket is 80 mm outboard of where §30.6 put it.
    _tube_object(
        "exhaust_silencer_bracket",
        (
            (SILENCER_BRACKET_X, cross_y, P.rail_z(p) + 0.004),
            (SILENCER_BRACKET_X, -0.660, 0.062),
            (SILENCER_BRACKET_X, axis_y, saddle_top - 0.012),
        ),
        0.016,
        context,
        collection,
        root,
        steel,
        bend_radius=0.040,
    )
    _lathe_object(
        "exhaust_silencer_isolator",
        _disc_profile(SILENCER_ISOLATOR[0] * 0.5, SILENCER_ISOLATOR[1] * 0.5),
        (SILENCER_BRACKET_X, axis_y, saddle_top - 0.008 - SILENCER_ISOLATOR[1] * 0.5),
        context,
        collection,
        root,
        context.material("rubber_grip"),
        axis="Z",
    )
    _block(
        "exhaust_silencer_saddle",
        (
            SILENCER_BAND_X[0] - 0.012,
            axis_y - SILENCER_SADDLE_WIDTH * 0.5,
            saddle_top - 0.008,
        ),
        (SILENCER_BRACKET_X + 0.014, axis_y + SILENCER_SADDLE_WIDTH * 0.5, saddle_top),
        context,
        collection,
        root,
        steel,
    )

    # Two jubilee clips, `sourced` as a pair, threaded through the saddle's slots.
    for index, band_x in enumerate(SILENCER_BAND_X):
        bm = bmesh.new()
        ring: list[Vector] = []
        for step in range(33):
            angle = 2.0 * math.pi * step / 32.0
            ring.append(
                Vector(
                    (
                        band_x,
                        axis_y + math.sin(angle) * (radius + 0.0015),
                        axis_z + math.cos(angle) * (radius + 0.0015),
                    )
                )
            )
        build.sweep_tube(bm, ring, 0.0025, 6)
        band = build.object_from_bmesh(
            "exhaust_silencer_band_%d" % index,
            bm,
            collection,
            material=context.material("axle_steel"),
            shade_smooth=True,
        )
        build.set_parent(band, root)


# --- radiator --------------------------------------------------------------
#
# The radiator is the one assembly on the kart that is not axis-aligned, so it is
# authored in a frame of its own and transformed into the world once. Building it
# in world coordinates would mean every one of its ~45 parts carrying the same
# two rotations by hand, and any part that got them wrong would be wrong in a way
# no reader could see.


def _radiator_frame(p: P.KartParams) -> tuple[Matrix, Vector]:
    """The radiator's local-to-world rotation and its center.

    **The rake is about the kart's lateral axis and the fin face points forward,
    the way the driver does.** Two earlier versions of this function reclined the
    core about the kart's *fore-and-aft* axis instead, which tips it sideways out
    over the sidepod and leaves the fin face pointing outboard.

    It used to be explained as sitting "where a second seat's back would sit", and
    that analogy is retired with the parameter it justified: the core rakes 45
    degrees from vertical and the shell's chord is 22, so they are not the same
    plane and `radiator_rake` is its own authored number. Yaw in plan is **0 ±3**,
    `derived`: in the dead-rear frame the core's inboard and outboard edges are
    parallel to within 7 px over 178.

    Local axes:

        local +x   the face normal — forward and up
        local +y   **inboard**, across the kart
        local +z   up the slant — rearward and up

    `+y` is inboard rather than outboard, which reads backwards for a part that
    lives on the kart's right, and is not a free choice: with `+y` outboard the
    three axes are left-handed, the determinant is -1, and every mesh pushed
    through the matrix comes out with its faces inverted. `genkart`'s signed
    volume gate catches that, but the fix is a right-handed basis rather than a
    sign patched in somewhere downstream.

    The rake is `radiator_rake` and nothing else. It used to be
    `seat_back_angle + radiator_rake_delta`, on the claim that the core sits in
    the plane a second seat's back would occupy -- which spec §30 measured false:
    the core rakes 45 degrees from vertical and the seat shell's chord is 22. So
    the two parts were sharing an angle they do not share, and because **no gate
    measures a rake**, correcting the seat would have tipped the radiator 13
    degrees with nothing objecting. The value is unchanged at 0.610; only its
    owner is. Issue #190.
    """
    rake = p.radiator_rake
    sin_rake, cos_rake = math.sin(rake), math.cos(rake)

    # Columns are where local +x, +y and +z land. At rake 0 this is a vertical
    # panel facing straight up the track; the rake tips its top rearward.
    normal = Vector((0.0, cos_rake, sin_rake))
    inboard = Vector((-RADIATOR_SIDE, 0.0, 0.0))
    up_slant = Vector((0.0, -sin_rake, cos_rake))

    basis = Matrix((
        (normal.x, inboard.x, up_slant.x),
        (normal.y, inboard.y, up_slant.y),
        (normal.z, inboard.z, up_slant.z),
    ))
    center = Vector((RADIATOR_SIDE * abs(p.radiator_x), p.radiator_y, p.radiator_z))
    return basis, center


def _radiator_world(
    basis: Matrix, center: Vector, local: tuple[float, float, float]
) -> Vector:
    """A point given in the radiator's frame, in world coordinates."""
    return center + basis @ Vector(local)


def _reverse_if_mirrored(bm: bmesh.types.BMesh, basis: Matrix) -> None:
    """Undo the winding flip a mirrored basis introduces.

    `RADIATOR_SIDE = -1` makes the frame a mirror rather than a rotation, so its
    determinant is -1 and every face pushed through it comes out inside out. That
    is invisible in a render -- Blender's materials do not backface cull and the
    exporter writes `doubleSided: true`, which is the trap `genkart.check_face_
    winding` exists for and which hid inverted `build.box` output for two
    milestones. So it is corrected here, at the one place the mirror is
    introduced, rather than being left for the gate to complain about.

    One call over every face: `bmesh.ops.reverse_faces` rebuilds the face table
    per call, so reversing them one at a time is quadratic for no reason.
    """
    if basis.determinant() < 0.0:
        bmesh.ops.reverse_faces(bm, faces=list(bm.faces))


def _radiator_block(
    name: str,
    low: tuple[float, float, float],
    high: tuple[float, float, float],
    basis: Matrix,
    center: Vector,
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    *,
    bevel: bool = True,
) -> bpy.types.Object:
    """`_block`, but with its corners given in the radiator's frame.

    `build.box` applies the rotation to the corner offsets rather than to the
    object, so the object transform stays identity and the vertex buffer is the
    same pure function of the parameters that everything else here is.
    """
    size = tuple(high[axis] - low[axis] for axis in range(3))
    local_center = Vector(tuple((high[axis] + low[axis]) * 0.5 for axis in range(3)))
    bm = bmesh.new()
    build.box(bm, size, center + basis @ local_center, rotation=basis)
    _reverse_if_mirrored(bm, basis)
    obj = build.object_from_bmesh(name, bm, collection, material=material)
    if bevel:
        build.bevel_object(obj, context.detail)
    build.set_parent(obj, root)
    return obj


def _radiator_cap(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    basis: Matrix,
    center: Vector,
    half_width: float,
    half_height: float,
) -> None:
    """The filler cap on the top tank.

    Built at the origin about local +z and then moved, because `build.lathe`
    revolves about a *world* axis and the top tank's normal is not one. Same
    determinant argument as `_radiator_frame`: the transform is a rotation, so
    the winding survives it.
    """
    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (0.0, 0.0),
            (RADIATOR_CAP_RADIUS - 0.003, 0.0),
            (RADIATOR_CAP_RADIUS - 0.003, 0.006),
            (RADIATOR_CAP_RADIUS - 0.006, 0.008),
            (RADIATOR_CAP_RADIUS - 0.006, RADIATOR_CAP_HEIGHT - 0.017),
            (RADIATOR_CAP_RADIUS, RADIATOR_CAP_HEIGHT - 0.015),
            (RADIATOR_CAP_RADIUS, RADIATOR_CAP_HEIGHT - 0.003),
            (RADIATOR_CAP_RADIUS - 0.004, RADIATOR_CAP_HEIGHT),
            (0.0, RADIATOR_CAP_HEIGHT),
        ],
        context.detail.exhaust_segments,
        axis="Z",
    )
    origin = _radiator_world(
        basis,
        center,
        (0.0, half_width * RADIATOR_CAP_ALONG, half_height - 0.004),
    )
    for vertex in bm.verts:
        vertex.co = origin + basis @ vertex.co
    _reverse_if_mirrored(bm, basis)
    cap = build.object_from_bmesh(
        "radiator_cap", bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(cap, root)


def _radiator(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Core, fin pack, dual-pass divider, end channels, tanks, neck, brackets,
    hoses — all built in the radiator's own frame. See `_radiator_frame`: the
    core sits where a second seat's back would sit, next to the driver's.

    Every dimension here is read against that frame and not against a world axis,
    because none of them is axis-aligned. `radiator_width` is across the kart,
    `radiator_height` is up the slant, `radiator_thickness` is through the face.
    Three earlier versions of this function each got one of those wrong; the
    parameter docstrings now say which is which and this one does not repeat
    them.
    """
    p = context.params
    core_material = context.material("radiator_core")
    alloy = context.material("engine_cast")

    basis, center = _radiator_frame(p)

    half_thickness = p.radiator_thickness * 0.5
    half_width = p.radiator_width * 0.5
    half_height = p.radiator_height * 0.5
    core_half_height = half_height - RADIATOR_TANK_HEIGHT
    core_half_thickness = half_thickness - RADIATOR_TANK_PROUD
    core_half_width = half_width - RADIATOR_END_PLATE

    _radiator_block(
        "radiator_core",
        (-core_half_thickness, -core_half_width, -core_half_height),
        (core_half_thickness, core_half_width, core_half_height),
        basis,
        center,
        context,
        collection,
        root,
        core_material,
        bevel=False,
    )

    # Low-forward and high-rearward, not bottom and top: the tanks are at the
    # two ends of the raked tube run, which is what `radiator_height` measures.
    for label, low_v, high_v in (
        ("low", -half_height, -core_half_height),
        ("high", core_half_height, half_height),
    ):
        _radiator_block(
            "radiator_tank_%s" % label,
            (-half_thickness, -half_width, low_v),
            (half_thickness, half_width, high_v),
            basis,
            center,
            context,
            collection,
            root,
            alloy,
        )

    # Side channels closing the ends of the fin pack.
    for label, sign in (("inboard", 1.0), ("outboard", -1.0)):
        _radiator_block(
            "radiator_end_%s" % label,
            (
                -half_thickness,
                sign * half_width - (RADIATOR_END_PLATE if sign > 0 else 0.0),
                -core_half_height,
            ),
            (
                half_thickness,
                sign * half_width + (RADIATOR_END_PLATE if sign < 0 else 0.0),
                core_half_height,
            ),
            basis,
            center,
            context,
            collection,
            root,
            alloy,
        )

    # The fin pack. Count follows from the core's length so it is not a free
    # number, and it is the same at both detail levels — see RADIATOR_FIN_PITCH.
    fin_count = max(1, int(round(2.0 * core_half_width / RADIATOR_FIN_PITCH)) - 1)
    for index in range(fin_count):
        along = -core_half_width + (index + 1) * (
            2.0 * core_half_width / (fin_count + 1)
        )
        _radiator_block(
            "radiator_fin_%d" % index,
            (
                -core_half_thickness - RADIATOR_FIN_PROUD,
                along - RADIATOR_FIN_THICKNESS * 0.5,
                -core_half_height,
            ),
            (
                core_half_thickness + RADIATOR_FIN_PROUD,
                along + RADIATOR_FIN_THICKNESS * 0.5,
                core_half_height,
            ),
            basis,
            center,
            context,
            collection,
            root,
            alloy,
            bevel=False,
        )

    _radiator_block(
        "radiator_divider",
        (
            -core_half_thickness - RADIATOR_DIVIDER_PROUD,
            core_half_width * RADIATOR_DIVIDER_ALONG - RADIATOR_DIVIDER_THICKNESS * 0.5,
            -core_half_height,
        ),
        (
            core_half_thickness + RADIATOR_DIVIDER_PROUD,
            core_half_width * RADIATOR_DIVIDER_ALONG + RADIATOR_DIVIDER_THICKNESS * 0.5,
            core_half_height,
        ),
        basis,
        center,
        context,
        collection,
        root,
        alloy,
    )

    _radiator_cap(context, collection, root, alloy, basis, center, half_width, half_height)

    halves = (half_thickness, half_width, half_height)

    def attach(fractions: tuple[float, float, float]) -> Vector:
        return _radiator_world(
            basis,
            center,
            tuple(fractions[axis] * halves[axis] for axis in range(3)),
        )

    _radiator_curtain(context, collection, root, basis, center, half_width, half_height)

    # Both brackets clamp `chassis_rail_l`, whose straight run is at
    # x = RADIATOR_SIDE * frame_half_rear and z = rail_z. Reading the rail's own two
    # parameters rather than an authored world point is what makes the brackets
    # follow the rail if §Chassis moves it again -- the property that was missing
    # when `BRACKET_*_SEAT` was a literal.
    rail = (RADIATOR_SIDE * p.frame_half_rear, P.rail_z(p))
    for label, local in (
        ("lower", BRACKET_LOWER_LOCAL),
        ("upper", BRACKET_UPPER_LOCAL),
    ):
        start = attach(local)
        end = Vector((rail[0], start.y, rail[1]))
        _tube_object(
            "radiator_bracket_%s" % label,
            (
                tuple(start),
                # A kart's radiator bracket is a bent rod, not a straight one.
                # The mid point is dropped below the chord so the bend is the
                # right way up — a bracket bowing upward looks sprung.
                tuple(start.lerp(end, 0.5) - Vector((0.0, 0.0, 0.018))),
                tuple(end),
            ),
            BRACKET_DIAMETER,
            context,
            collection,
            root,
            alloy,
            bend_radius=0.045,
        )
        # The Ø30 clamp that grips the rail, bored on the rail's own centreline so
        # contact is 0.0 by construction.
        _lathe_object(
            "radiator_bracket_%s_clamp" % label,
            [
                (0.015, start.y - 0.014),
                (0.023, start.y - 0.014),
                (0.023, start.y + 0.014),
                (0.015, start.y + 0.014),
            ],
            (rail[0], 0.0, rail[1]),
            context,
            collection,
            root,
            alloy,
            axis="Y",
        )

    # The radiator is on the left and the engine is on the right, so both hoses
    # cross the kart. A straight chord does it through the driver -- 142 triangle
    # pairs, measured -- so both routes are authored waypoint lists rather than
    # interpolated: the upper goes **behind the seat back and above the axle**, the
    # lower stays forward of the axle and inboard of both brackets. A hose cannot
    # cross the spinning axle plane, and that is the constraint, not styling.
    hose_material = context.material("rubber_grip")
    # **Derived through the lean, not authored.** The outlet elbow is a boss on a head
    # that `params.cylinder_lean` tips 25 degrees forward, so its mouth is wherever the
    # rotation puts it -- and a literal for the hose's engine end drifts 10 mm the
    # moment the lean changes, which is what gate 2 measured.
    upper_engine = _lean(p) @ Vector(WATER_OUTLET_LOCAL)
    for label, local, waypoints, fitting in (
        ("upper", HOSE_UPPER_LOCAL, HOSE_UPPER_ROUTE, upper_engine),
        ("lower", HOSE_LOWER_LOCAL, HOSE_LOWER_ROUTE, Vector(PUMP_SPINDLE) + Vector((0.0, 0.0, 0.030))),
    ):
        start = attach(local)
        route = [tuple(start)]
        # **Not `reversed()`.** Both lists are authored radiator-first, from the core's
        # own fitting toward the engine, which is the order the tube is swept in. It was
        # `reversed()` here, and `HOSE_LOWER_ROUTE`'s note has the four legs that built.
        route.extend(tuple(point) for point in waypoints)
        route.append(tuple(fitting))
        _tube_object(
            "radiator_hose_%s" % label,
            tuple(route),
            HOSE_DIAMETER,
            context,
            collection,
            root,
            hose_material,
            bend_radius=0.050,
        )


def _radiator_curtain(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    basis: Matrix,
    center: Vector,
    half_width: float,
    half_height: float,
) -> None:
    """The adjustable blind, standing proud of the core's forward face.

    Art. 5.3.1 permits it explicitly and constrains it: adjustable but *"not
    detachable when the kart is in motion"*, *"securely fixed to the radiator(s) with
    screws"*, one-piece, composite permitted. So it is threaded to the two end
    channels and nothing else, and its width **is** the core's -- see
    `RADIATOR_CURTAIN_THICKNESS` for why that is sourced rather than convenient.
    """
    # Two millimetres inside the fin block at each end, because the tanks stand
    # `RADIATOR_TANK_PROUD` forward of the core and a curtain flush with the block's
    # ends shares triangles with both of them. The 391 mm of *travel* is still the
    # full block: the blind slides.
    fin_half = half_height - RADIATOR_TANK_HEIGHT - 0.002
    face = half_height * 0.0 + RADIATOR_CURTAIN_STANDOFF
    del face
    outer = (
        context.params.radiator_thickness * 0.5
        - RADIATOR_TANK_PROUD
        + RADIATOR_CURTAIN_STANDOFF
    )
    _radiator_block(
        "radiator_curtain",
        (outer, -half_width, -fin_half),
        (outer + RADIATOR_CURTAIN_THICKNESS, half_width, fin_half),
        basis,
        center,
        context,
        collection,
        root,
        context.material("frame_powdercoat"),
        bevel=False,
    )


# --- cooling: the pump is on the axle, not on the engine --------------------


def _cooling(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Pump body, both pulleys, the toothed belt, the bracket and the short hose.

    `engine_water_pump` was a boss on the clutch cover and it is gone. Art. **5.3.2**
    permits the pump to be driven *"either by the engine or by the rear wheel axle"*
    -- it **permits**, it does not require, and this spec chooses the axle because
    that is what the parts the trade sells are built for: *"KZ water pump with HTD
    axle pulley and tooth belt"*. Three joints on the engine castings go with it.

    The belt is what places the pump: see `PUMP_SPINDLE`. Nothing here is a free
    position except the belt plane.
    """
    p = context.params
    alloy = context.material("engine_cast")
    spindle = Vector(PUMP_SPINDLE)
    axle = Vector((P.rear_axle_y(p), P.rear_axle_z(p)))

    _lathe_object(
        "cooling_pump_body",
        [
            (0.0, BELT_PLANE_X - 0.042),
            (PUMP_BODY_DIAMETER * 0.5 - 0.008, BELT_PLANE_X - 0.042),
            (PUMP_BODY_DIAMETER * 0.5, BELT_PLANE_X - 0.034),
            (PUMP_BODY_DIAMETER * 0.5, BELT_PLANE_X - 0.014),
            (PUMP_BODY_DIAMETER * 0.5 - 0.012, BELT_PLANE_X - 0.005),
            (0.0, BELT_PLANE_X - 0.005),
        ],
        (0.0, spindle.y, spindle.z),
        context,
        collection,
        root,
        alloy,
        axis="X",
    )
    _lathe_object(
        "cooling_pump_pulley",
        [
            (0.0, BELT_PLANE_X - BELT_WIDTH * 0.5 - 0.002),
            (PUMP_PULLEY_PD * 0.5 + 0.003, BELT_PLANE_X - BELT_WIDTH * 0.5 - 0.002),
            (PUMP_PULLEY_PD * 0.5, BELT_PLANE_X - BELT_WIDTH * 0.5),
            (PUMP_PULLEY_PD * 0.5, BELT_PLANE_X + BELT_WIDTH * 0.5),
            (PUMP_PULLEY_PD * 0.5 + 0.003, BELT_PLANE_X + BELT_WIDTH * 0.5 + 0.002),
            (0.0, BELT_PLANE_X + BELT_WIDTH * 0.5 + 0.002),
        ],
        (0.0, spindle.y, spindle.z),
        context,
        collection,
        root,
        alloy,
        axis="X",
    )
    _lathe_object(
        "cooling_axle_pulley",
        [
            (p.axle_diameter * 0.5, BELT_PLANE_X - BELT_WIDTH * 0.5 - 0.004),
            (AXLE_PULLEY_PD * 0.5 + 0.003, BELT_PLANE_X - BELT_WIDTH * 0.5 - 0.004),
            (AXLE_PULLEY_PD * 0.5, BELT_PLANE_X - BELT_WIDTH * 0.5),
            (AXLE_PULLEY_PD * 0.5, BELT_PLANE_X + BELT_WIDTH * 0.5),
            (AXLE_PULLEY_PD * 0.5 + 0.003, BELT_PLANE_X + BELT_WIDTH * 0.5 + 0.004),
            (p.axle_diameter * 0.5, BELT_PLANE_X + BELT_WIDTH * 0.5 + 0.004),
        ],
        (0.0, axle.x, axle.y),
        context,
        collection,
        root,
        alloy,
        axis="X",
    )

    steps = max(6, context.detail.exhaust_segments)
    bm = bmesh.new()
    _ribbon(
        bm,
        _belt_path(
            Vector((spindle.y, spindle.z)),
            PUMP_PULLEY_PD * 0.5,
            axle,
            AXLE_PULLEY_PD * 0.5,
            steps,
        ),
        BELT_PLANE_X,
        BELT_WIDTH * 0.5,
        BELT_THICKNESS * 0.5,
    )
    belt = build.object_from_bmesh(
        "cooling_belt", bm, collection, material=context.material("rubber_grip")
    )
    build.set_parent(belt, root)

    # The bracket back to the right bearing hanger, which is the nearest
    # axle-adjacent structure Art. 4.2.3 already contemplates a weld on.
    hanger_x = p.frame_half_rear - 0.010
    # **Under** the belt loop, not across it. Straight outboard from the spindle the
    # bracket would be inside the pump pulley and inside both belt strands; the loop's
    # lowest point near the pump is `spindle.z - 12.5`, so a bracket whose top is at
    # `spindle.z - 22` clears it by 9.5 mm and still overlaps the Ø60 body it bolts to.
    _block(
        "cooling_pump_bracket",
        (BELT_PLANE_X - 0.034, spindle.y - 0.011, spindle.z - 0.038),
        (hanger_x, spindle.y + 0.011, spindle.z - 0.022),
        context,
        collection,
        root,
        context.material("frame_powdercoat"),
    )

    # Out of the pump's *forward* face, because its top is where the bottom radiator
    # hose comes in and two hoses cannot share one port.
    _tube_object(
        "cooling_hose_pump_engine",
        (
            (BELT_PLANE_X - 0.026, spindle.y + 0.026, spindle.z),
            (0.190, -0.348, 0.140),
            WATER_INLET_BOSS,
        ),
        HOSE_DIAMETER,
        context,
        collection,
        root,
        context.material("rubber_grip"),
        bend_radius=0.040,
    )
