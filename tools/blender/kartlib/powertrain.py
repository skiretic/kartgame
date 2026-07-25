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
sprocket is on the *inboard* end of that shaft, so the chain runs in a plane
perpendicular to the axle. That plane must contain both sprockets, and a chain
cannot be skewed by even a few millimeters without throwing itself.

`params.engine_x` is +0.240, i.e. the kart's right, which is correct: every KZ
engine — TM KZ-R1, Vortex ROK Shifter, IAME Screamer — sits on the driver's
right, with the chain guard and the axle sprocket on the right as well and the
rear brake disc on the left to balance it. `wheels.py` puts its axle sprocket at
x = **-0.115**, on the left, and its docstring says "a KZ drives the left rear".
A locked rear axle does not drive one wheel: ARCHITECTURE.md §6 makes both rears
turn together, so there is no left or right rear to drive. The magnitude 0.115 is
right — it lands the sprocket between `frame.py`'s center and outer bearing
hangers, exactly as that docstring reasons — it is only the sign that is
mirrored.

So this module puts its output sprocket and its chain at x = **+0.115**, which is
where a real KZ's chain runs and where `wheels.py`'s own reasoning puts it. The
two are one sign apart and are reported rather than silently reconciled; nothing
here reads or writes `wheels.py`'s objects.

EXHAUST ROUTING — which way the port faces
------------------------------------------
The 125cc KZ engines above are all water-cooled case-reed two-strokes with the
reed block and carburetor at the **rear** of the crankcase and the exhaust port
on the **front** face of the cylinder. So the airbox sits high and rearward, over
the right rear tire, and the header leaves the cylinder forward, drops outboard
around the right main rail, and the chamber runs forward along the right flank
inboard of the sidepod with the silencer ahead of the driver's hip.

That is also the only routing the kart has room for, which is a useful check on
the reference rather than a substitute for it: rearward of the engine, 0.62 m of
pipe would have to pass through the right rear tire, which occupies
x 0.485..0.700, y -0.673..-0.378. There is nowhere for it to go. Getting this
backwards puts the pipe in the tire and the airbox in the radiator.

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

CHAIN_X: float = 0.115
"""The chain plane. Must equal `wheels.SPROCKET_X` in magnitude *and sign*; see
the CHAIN LINE note in the module docstring for why it currently does not."""

CHAIN_PITCH: float = 0.005563
"""#219 chain pitch. "219" is thousandths of an inch: 0.219 in = 5.563 mm."""

ENGINE_SPROCKET_TEETH: int = 12
"""KZ front sprockets run 10-14 teeth. Pitch diameter is `pitch * teeth / pi`."""

CHAIN_HALF_WIDTH: float = 0.0045
CHAIN_HALF_HEIGHT: float = 0.0035
"""#219 chain is about 9 mm across the rollers and 8 mm tall. Modeled as a flat
band rather than a round cord: a chain seen edge-on is a flat plate, and a swept
circle reads as a bungee."""

SPROCKET_Y: float = -0.268
SPROCKET_Z: float = 0.150
"""Engine output sprocket center. `SPROCKET_Z` is `engine_z` — the crankshaft and
the gearbox output sit at the same height on a kart engine, and that height is
what `engine_z` has to mean for the chain line to work at all (see `_engine`).
`SPROCKET_Y` is 257 mm ahead of the rear axle, which is a normal KZ chain run,
and is as far forward as it can go: `frame.py`'s right rear seat strut dives
through z = 0.150 at y = -0.335, straight through where the sprocket carrier
would otherwise sit."""

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
It sits above and ahead of the gearbox output boss, which is why the sprocket
carrier at `SPROCKET_Y`/`SPROCKET_Z` emerges from under its rear lower edge
rather than from a flat wall.
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

EXHAUST_FLANGE_HALF: tuple[float, float] = (0.038, 0.030)
EXHAUST_FLANGE_THICKNESS: float = 0.014
EXHAUST_FLANGE_NUT_FLATS: float = 0.011
"""The exhaust port flange on the barrel's front face, and its two nuts.

The chamber is not welded to the engine: an elbow bolts to the port through this
flange and the pipe slips onto the elbow and is held by springs. #116 lists the
flange and the springs as the missing joint hardware, and they are the two parts
that make the pipe look bolted on rather than grown out of the cylinder.
"""

EXHAUST_SPRING_TURNS: int = 6
EXHAUST_SPRING_COIL_RADIUS: float = 0.0055
EXHAUST_SPRING_WIRE_RADIUS: float = 0.0011
EXHAUST_SPRING_SAMPLES: int = 8
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
BATTERY_CENTER: tuple[float, float, float] = (0.222, -0.400, 0.195)
"""The starter's battery, on a bracket behind the engine. Sits above the rear
seat strut, which passes below it at z 0.120..0.148.

Turned to run **fore-and-aft** rather than across the kart, and moved 8 mm
inboard. Laid across, its outboard end reached x 0.285 and the carburetor's body
— now a 60 mm cylinder about x 0.312 rather than a 72 mm box — occupies from
0.282. The two overlapped by 3 mm. Along the kart it ends at 0.265 and clears
both the carburetor by 17 mm and the seat shell's edge at 0.164 by 15 mm."""

CARB_AXIS_X: float = 0.312
CARB_AXIS_Z: float = 0.205
CARB_BODY_RADIUS: float = 0.030
CARB_FRONT_Y: float = -0.368
CARB_REAR_Y: float = -0.440
CARB_SPIGOT_ENGINE_RADIUS: float = 0.0175
CARB_SPIGOT_AIR_RADIUS: float = 0.032
CARB_TOP_CAP_RADIUS: float = 0.024
CARB_TOP_CAP_TOP_Z: float = 0.262
CARB_BOWL_LO: tuple[float, float, float] = (0.290, -0.434, 0.152)
CARB_BOWL_HI: tuple[float, float, float] = (0.334, -0.388, 0.184)
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

AIRBOX_LO: tuple[float, float, float] = (0.250, -0.567, 0.280)
AIRBOX_HI: tuple[float, float, float] = (0.420, -0.452, 0.400)
"""The airbox sits high and rearward, over the right rear tire — inboard of it at
x <= 0.420 against the tire's inner face at 0.485, and above its crown at 0.295.
It is the highest thing on the kart apart from the steering wheel and it is a
large part of what says "shifter" from behind."""

INTAKE_BOOT_DIAMETER: float = 0.068
"""Rubber over the carburetor's 64 mm air-filter spigot."""

EXHAUST_PATH: tuple[tuple[float, float, float], ...] = (
    (0.320, -0.190, 0.288),
    (0.356, -0.120, 0.205),
    (0.366, -0.040, 0.138),
    (0.356, 0.040, 0.112),
    (0.350, 0.140, 0.108),
    (0.340, 0.240, 0.116),
    (0.322, 0.330, 0.122),
    (0.290, 0.410, 0.135),
    (0.265, 0.470, 0.145),
)
"""Control polyline of the whole exhaust, from the port forward, before filleting
and before it is trimmed to `exhaust_length`.

Every point is set by a clearance rather than by taste. Leaving the port the pipe
goes outboard to x = 0.366 to clear the starter motor; it then comes back to
x ~ 0.352 because that is the middle of the only corridor the belly fits through
— the shifter's lever is at x <= 0.269 and the right side bar's inboard surface
is at x >= 0.434, and a 130 mm belly between them leaves under 20 mm a side. It
runs at z ~ 0.11 because `radiator_z` puts the radiator's bottom tank at 0.200
and the belly's crown has to pass under it. At the front it comes back inboard
and lifts, because the main rail sweeps out and up to the kingpin from y = 0.30
and would otherwise be straight through the silencer."""

EXHAUST_PROFILE: tuple[tuple[float, float], ...] = (
    (0.000, 1.000),
    (0.150, 1.000),
    (0.185, 1.294),
    (0.430, 3.824),
    (0.520, 3.824),
    (0.560, 3.706),
    (0.760, 0.941),
    (0.790, 0.647),
    (1.000, 0.647),
)
"""Chamber radius against fraction of `exhaust_length`, in units of
`exhaust_pipe_diameter / 2`.

Expressed as a ratio so the whole pipe scales with the two parameters that
describe it: 3.824 is `exhaust_max_diameter / exhaust_pipe_diameter`, so the
belly is exactly `exhaust_max_diameter` across by construction rather than by a
number that has to be kept in step by hand. The five sections are the five a real
expansion chamber has — header, diverging cone, belly, converging (baffle) cone,
stinger — and the baffle cone is deliberately steeper than the diverging cone,
which is what a tuned pipe looks like and what a symmetric "rugby ball" does
not."""

SILENCER_START: float = 0.780
SILENCER_RADIUS: float = 0.038
SILENCER_PROFILE: tuple[tuple[float, float], ...] = (
    (0.00, 0.34),
    (0.06, 0.84),
    (0.12, 1.00),
    (0.88, 1.00),
    (0.94, 0.84),
    (1.00, 0.37),
)
"""The silencer can, as a fraction of `SILENCER_RADIUS` along its own span. It
slips over the stinger and the two are coaxial and interpenetrate, which is what
the real assembly does."""

RADIATOR_TANK_HEIGHT: float = 0.030
RADIATOR_TANK_PROUD: float = 0.007
"""Top and bottom tanks. `radiator_height` covers the tanks *and* the core, so
the core's own height is `radiator_height - 2 * RADIATOR_TANK_HEIGHT`.

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
HOSE_DIAMETER: float = 0.028
BRACKET_DIAMETER: float = 0.016
"""Radiator brackets. On a KZ the radiator is carried off the seat's right wing
rather than off the frame, and here it has to be: the exhaust belly occupies the
whole volume between the radiator's underside and the main rail."""

BRACKET_LOWER_LOCAL: tuple[float, float, float] = (-1.0, 0.34, -0.52)
BRACKET_UPPER_LOCAL: tuple[float, float, float] = (-1.0, 0.34, 0.44)
BRACKET_LOWER_SEAT: tuple[float, float, float] = (0.180, -0.150, 0.135)
BRACKET_UPPER_SEAT: tuple[float, float, float] = (0.180, -0.235, 0.330)
"""Where the two brackets meet the radiator and where they meet the seat's right
wing. The radiator ends are **fractions of the radiator's own half-extents in
its own frame** rather than world points, so they stay on the back of the core
when the rake or the size changes — which is the whole reason the radiator is
built in a frame at all.

Lower and upper, because that is what they are once the core is raked into the
seat's plane: both leave the core's **back** face and run rearward and inboard
to the seat's right wing, one low and one high. They were front and rear while
the core was wrongly modeled as a long panel lying fore-and-aft."""

HOSE_UPPER_LOCAL: tuple[float, float, float] = (0.0, 0.85, 0.95)
HOSE_LOWER_LOCAL: tuple[float, float, float] = (0.0, 0.85, -0.95)
HOSE_UPPER_ENGINE: tuple[float, float, float] = (0.299, -0.182, 0.376)
HOSE_LOWER_ENGINE: tuple[float, float, float] = (0.216, -0.168, 0.194)
"""Top hose from the head's water outlet, bottom hose to the water pump on the
crankcase. Both leave the tanks on their **inboard** side, which is the side the
engine is on once the core is raked into the seat's plane.

The engine ends are the two fittings' own mouths, so a hose cannot end in mid
air — that was the state before the water pump boss existed."""

EXHAUST_HANGER_Y: float = 0.300
"""Where the silencer is strapped to the right side bar. A real pipe hangs on
springs at the header joint and a strap at the can; the strap is the one that is
visible."""


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("powertrain")

    # One root for the whole group, as `frame.py` and `cockpit.py` do: the glTF
    # exporter flattens collections, so a single parent is the only thing that
    # makes the powertrain one node for the M3b solver to attach mass to.
    root = build.empty("powertrain_root", (0.0, 0.0, 0.0), collection, size=0.10)
    context.publish("powertrain_root", root)

    _engine_mount(context, collection, root)
    _engine(context, collection, root)
    _intake(context, collection, root)
    _driveline(context, collection, root)
    _exhaust(context, collection, root)
    _radiator(context, collection, root)


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
) -> bpy.types.Object:
    """A revolution about a world axis, parented to `root`.

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
    material = context.material("engine_alloy")
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
    material = context.material("engine_alloy")

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
) -> None:
    """A ring of hex bolt heads on a cover face normal to X.

    Indexed names rather than a running counter, for `build.py`'s rule 3: a bolt
    named from a counter moves when a part is inserted upstream, and the name is
    the glTF node name.

    The first bolt is at angle zero — straight up — rather than at half a pitch,
    so that a cover with an even count has one at top and one at bottom, which is
    what the eye checks a bolt circle against.
    """
    height = 0.005
    # A lathe about X grows from its center along +X, so a bolt head standing
    # proud of an *inboard* face has to be started `height` back from it.
    base_x = center[0] - height if inward else center[0] - proud
    for index in range(count):
        angle = 2.0 * math.pi * index / count
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
    """Square base flange, round water jacket, exhaust port flange and springs.

    The shape is the correction. See `CYLINDER_RADIUS`'s docstring for why a KZ
    barrel carries no fins and why the four that were here read as a 100 cc
    air-cooled engine instead.
    """
    axis = (CYLINDER_AXIS_X, CYLINDER_AXIS_Y)

    _block(
        "engine_cylinder_base",
        (axis[0] - CYLINDER_BASE_HALF[0], axis[1] - CYLINDER_BASE_HALF[1], crank_top),
        (axis[0] + CYLINDER_BASE_HALF[0], axis[1] + CYLINDER_BASE_HALF[1], CYLINDER_BASE_TOP_Z),
        context,
        collection,
        root,
        material,
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
    )

    _exhaust_flange(context, collection, root, material)


def _exhaust_flange(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
) -> None:
    """The port flange on the barrel's front face, its nuts, and two springs.

    The flange is placed off the sampled exhaust path rather than off a literal,
    so it stays on the port when the pipe is re-routed: the path's first point is
    the port by construction (`EXHAUST_PATH`), and the header's direction there
    is what the flange is square to. Authoring the two independently is how a
    flange ends up visibly not perpendicular to its own pipe.
    """
    samples = _exhaust_path(context)
    port = samples[0]
    forward = (samples[1] - samples[0]).normalized()

    face_y = CYLINDER_AXIS_Y + CYLINDER_RADIUS
    _block(
        "exhaust_flange",
        (
            port.x - EXHAUST_FLANGE_HALF[0],
            face_y - 0.004,
            port.z - EXHAUST_FLANGE_HALF[1],
        ),
        (
            port.x + EXHAUST_FLANGE_HALF[0],
            face_y + EXHAUST_FLANGE_THICKNESS,
            port.z + EXHAUST_FLANGE_HALF[1],
        ),
        context,
        collection,
        root,
        material,
    )

    # Two nuts, left and right of the pipe on the flange's face, and two springs
    # hooked from just outboard of them back to lugs on the header.
    for index, sign in enumerate((-1, 1)):
        anchor_x = port.x + sign * (EXHAUST_FLANGE_HALF[0] - 0.009)
        _hex_nut(
            "exhaust_flange_nut_%d" % index,
            (anchor_x, face_y + EXHAUST_FLANGE_THICKNESS, port.z),
            EXHAUST_FLANGE_NUT_FLATS,
            0.007,
            context,
            collection,
            root,
            material,
            axis="Y",
        )

        # The far end sits on the header a little way down the pipe. Found by
        # walking the sampled path so the spring stretches with the pipe rather
        # than floating when `exhaust_length` changes.
        far = samples[min(2, len(samples) - 1)]
        start = Vector((anchor_x, face_y + EXHAUST_FLANGE_THICKNESS + 0.004, port.z))
        end = far + (Vector((sign, 0.0, 0.0)) * 0.020) - forward * 0.004
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
    """The head casting, its six nuts, the spark plug, and the water outlet."""
    axis = (CYLINDER_AXIS_X, CYLINDER_AXIS_Y)

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
        )

    _spark_plug(context, collection, root, head_top)

    # Water outlet on the head's front face, inboard of the bore, where the top
    # hose lands. Sharing the front face with the exhaust port is not a conflict:
    # the port is on the *cylinder* at z 0.288 and this is on the head 88 mm
    # above it. `HOSE_UPPER`'s last control point is this boss's mouth.
    _lathe_object(
        "engine_water_outlet",
        _disc_profile(0.015, 0.018),
        (axis[0] - 0.020, axis[1] + HEAD_RADIUS - 0.010, head_top - 0.024),
        context,
        collection,
        root,
        material,
        axis="Y",
    )

    # The water pump's boss on the crankcase's inboard wall, so the bottom hose
    # lands on a fitting rather than on a blank face.
    _lathe_object(
        "engine_water_pump",
        [
            (0.0, -0.030),
            (0.014, -0.030),
            (0.014, -0.018),
            (0.026, -0.012),
            (0.026, 0.0),
            (0.0, 0.0),
        ],
        (CRANKCASE_INBOARD_X, -0.168, 0.194),
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
    """Boss, plug body, insulator, cap and lead, on the bore axis.

    The plug is on the bore axis and nowhere else — it fires into the middle of
    the combustion chamber — so it is placed from `CYLINDER_AXIS_*` rather than
    given coordinates of its own. That also means it cannot drift off center if
    the cylinder moves.
    """
    axis_x, axis_y = CYLINDER_AXIS_X, CYLINDER_AXIS_Y
    alloy = context.material("engine_alloy")

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
    )

    # The cap slips over the insulator and leans back off the bore axis, and the
    # lead runs from it down the back of the engine. Two swept tubes: the cap is
    # short and fat, the lead is long and thin, and it is the lead that reads at
    # a distance.
    cap_base = Vector((axis_x, axis_y, hex_top + 0.026))
    cap_top = Vector((axis_x - 0.004, axis_y - 0.028, hex_top + 0.058))
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
            (0.352, -0.330, hex_top + 0.020),
            (0.404, -0.352, 0.300),
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
    material = context.material("engine_alloy")

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
            (CARB_AXIS_X - 0.010, cap_y + 0.010, CARB_TOP_CAP_TOP_Z + 0.030),
            (CARB_AXIS_X - 0.030, cap_y + 0.090, CARB_TOP_CAP_TOP_Z + 0.024),
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

    _tube_object(
        "engine_intake_boot",
        (
            (CARB_AXIS_X, CARB_REAR_Y + 0.006, CARB_AXIS_Z),
            (CARB_AXIS_X + 0.006, CARB_REAR_Y - 0.046, CARB_AXIS_Z + 0.040),
            (CARB_AXIS_X + 0.010, AIRBOX_LO[1] + 0.070, AIRBOX_LO[2] + 0.040),
        ),
        INTAKE_BOOT_DIAMETER,
        context,
        collection,
        root,
        context.material("rubber_grip"),
        bend_radius=0.040,
    )


# --- driveline -------------------------------------------------------------


def _driveline(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Sprocket carrier, output shaft, output sprocket and the chain.

    The chain is built from the two sprockets' pitch circles rather than drawn:
    the two straight runs are the circles' external tangents and the two arcs are
    the wraps between the tangent points, so the wrap angles are what the radii
    and the center distance make them. A hand-drawn loop is the version that
    looks subtly wrong at the small sprocket, where the wrap is only 145 degrees
    and any error in it is the whole shape.

    No teeth. They cannot come out of a revolution and a hundred of them would
    cost more than the rest of this module; `wheels.py` reaches the same
    conclusion about the axle sprocket for the same reason. The chain sits on the
    pitch circle and therefore interpenetrates both sprocket discs, which is what
    a roller chain straddling a tooth actually does.
    """
    material = context.material("axle_steel")

    pitch_radius = CHAIN_PITCH * ENGINE_SPROCKET_TEETH / (2.0 * math.pi)

    pivot = build.empty(
        "engine_sprocket", (CHAIN_X, SPROCKET_Y, SPROCKET_Z), collection, size=0.05
    )
    context.publish("engine_sprocket", pivot)
    build.set_parent(pivot, root)
    # `build.set_parent` reads `parent.matrix_world`, which for an empty created
    # this tick is still the identity until the depsgraph is evaluated — the same
    # trap `wheels.py` documents at its hubs. One evaluation here.
    bpy.context.view_layer.update()

    # Sprocket carrier: the boss the output shaft comes out of. Held to a 32 mm
    # radius because the right rear seat strut passes 50 mm above its axis.
    _lathe_object(
        "drive_sprocket_carrier",
        _disc_profile(0.032, 0.0325),
        (0.2125, SPROCKET_Y, SPROCKET_Z),
        context,
        collection,
        root,
        context.material("engine_alloy"),
    )

    _lathe_object(
        "drive_output_shaft",
        _disc_profile(0.009, 0.0425),
        (0.1425, SPROCKET_Y, SPROCKET_Z),
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
        center=(CHAIN_X, SPROCKET_Y, SPROCKET_Z),
    )
    sprocket = build.object_from_bmesh(
        "drive_output_sprocket", bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(sprocket, pivot)

    _chain(context, collection, root, material, pitch_radius)


def _chain(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    small_radius: float,
) -> None:
    """The closed belt path around the two sprockets, in the plane x = CHAIN_X."""
    p = context.params
    # `wheels.SPROCKET_DIAMETER` is 0.145 and this has to match it. It is not
    # imported: the contract makes the parameter block the only shared thing, and
    # importing a sibling module's constant would make this file depend on a file
    # being edited concurrently. Both belong in params.py — see the report.
    large_radius = 0.1450 * 0.5

    small = Vector((SPROCKET_Y, SPROCKET_Z))
    large = Vector((P.rear_axle_y(p), P.rear_axle_z(p)))

    delta = large - small
    distance = delta.length
    alpha = math.atan2(delta.y, delta.x)
    cosine = max(-1.0, min(1.0, (large_radius - small_radius) / distance))
    beta = math.acos(cosine)

    # Tangent points share an angle on both circles, because on an external
    # tangent the two radii to the contact points are parallel.
    theta_a = alpha + math.pi - beta
    theta_b = alpha + math.pi + beta

    steps = max(6, context.detail.exhaust_segments)

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
    # Wrap on the small sprocket, 2 * beta, on the side facing away from the axle.
    path.extend(arc(small, small_radius, theta_a, theta_b, steps))
    # Wrap on the axle sprocket, 2 * pi - 2 * beta, the long way round.
    path.extend(
        arc(large, large_radius, theta_b, theta_a + 2.0 * math.pi, steps * 3)
    )

    bm = bmesh.new()
    _ribbon(bm, path, CHAIN_X, CHAIN_HALF_WIDTH, CHAIN_HALF_HEIGHT)
    obj = build.object_from_bmesh("drive_chain", bm, collection, material=material)
    build.set_parent(obj, root)


# --- exhaust ---------------------------------------------------------------


def _exhaust_path(context: build.BuildContext) -> list[Vector]:
    """The sampled centerline: filleted, trimmed to `exhaust_length`, resampled.

    The sample count comes from `context.detail.exhaust_segments`, so the low and
    high builds are the same pipe at two densities rather than two pipes — which
    is what issue #19's normal bake needs and what a hardcoded count would break.
    """
    p = context.params
    filleted = build.fillet(
        list(EXHAUST_PATH), p.bend_radius * 0.8, context.detail.bend_segments
    )
    trimmed = _trim_to_length(filleted, p.exhaust_length)
    return _resample(trimmed, context.detail.exhaust_segments * 2 + 1)


def _exhaust(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Header, cones, belly, stinger, silencer and the strap that holds it up."""
    p = context.params
    material = context.material("exhaust_steel")
    samples = _exhaust_path(context)
    count = len(samples)
    unit = p.exhaust_pipe_diameter * 0.5

    # The chamber runs to just inside the silencer's mouth rather than to the end
    # of the path, so the stinger disappears into the can the way it does on a
    # real pipe instead of stopping at an open ring in mid-air.
    chamber_last = max(2, int(round((SILENCER_START + 0.02) * (count - 1))))
    chamber_path = samples[: chamber_last + 1]
    chamber_radii = [
        _profile_at(EXHAUST_PROFILE, index / (count - 1)) * unit
        for index in range(chamber_last + 1)
    ]
    bm = bmesh.new()
    _sweep_varying(bm, chamber_path, chamber_radii, context.detail.exhaust_segments)
    chamber = build.object_from_bmesh(
        "exhaust_chamber", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(chamber, context.detail)
    build.set_parent(chamber, root)

    silencer_first = int(round(SILENCER_START * (count - 1)))
    silencer_path = samples[silencer_first:]
    span = len(silencer_path) - 1
    silencer_radii = [
        _profile_at(SILENCER_PROFILE, index / span) * SILENCER_RADIUS
        for index in range(span + 1)
    ]
    bm = bmesh.new()
    _sweep_varying(bm, silencer_path, silencer_radii, context.detail.exhaust_segments)
    silencer = build.object_from_bmesh(
        "exhaust_silencer", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(silencer, context.detail)
    build.set_parent(silencer, root)

    outlet = build.empty(
        "exhaust_outlet", tuple(samples[-1]), collection, size=0.04
    )
    context.publish("exhaust_outlet", outlet)
    build.set_parent(outlet, root)

    # Strap from the can out to the right side bar. Found by walking the sampled
    # path rather than authored, so it stays on the pipe when the pipe moves.
    anchor = min(samples, key=lambda point: abs(point.y - EXHAUST_HANGER_Y))
    _block(
        "exhaust_hanger",
        (anchor.x + SILENCER_RADIUS - 0.004, anchor.y - 0.007, anchor.z - 0.006),
        (0.405, anchor.y + 0.007, anchor.z + 0.006),
        context,
        collection,
        root,
        context.material("frame_powdercoat"),
    )


# --- radiator --------------------------------------------------------------
#
# The radiator is the one assembly on the kart that is not axis-aligned, so it is
# authored in a frame of its own and transformed into the world once. Building it
# in world coordinates would mean every one of its ~45 parts carrying the same
# two rotations by hand, and any part that got them wrong would be wrong in a way
# no reader could see.


def _radiator_frame(p: P.KartParams) -> tuple[Matrix, Vector]:
    """The radiator's local-to-world rotation and its center.

    **The core sits where a second seat's back would sit.** Immediately outboard
    of the driver's, reclined by the same angle, big fin face pointing forward
    the way the driver does. That single sentence fixes the whole orientation,
    and two earlier versions of this function were wrong because they did not
    have it: both reclined the core about the kart's *fore-and-aft* axis, which
    tips it sideways out over the sidepod and leaves the fin face pointing
    outboard. The rake is about the kart's **lateral** axis.

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

    The rake is `seat_back_angle` plus `radiator_rake_delta`, so the radiator
    cannot drift away from the seat it is derived from.
    """
    rake = p.seat_back_angle + p.radiator_rake_delta
    sin_rake, cos_rake = math.sin(rake), math.cos(rake)

    # Columns are where local +x, +y and +z land. At rake 0 this is a vertical
    # panel facing straight up the track; the rake tips its top rearward.
    normal = Vector((0.0, cos_rake, sin_rake))
    inboard = Vector((-1.0, 0.0, 0.0))
    up_slant = Vector((0.0, -sin_rake, cos_rake))

    basis = Matrix((
        (normal.x, inboard.x, up_slant.x),
        (normal.y, inboard.y, up_slant.y),
        (normal.z, inboard.z, up_slant.z),
    ))
    return basis, Vector((p.radiator_x, p.radiator_y, p.radiator_z))


def _radiator_world(
    basis: Matrix, center: Vector, local: tuple[float, float, float]
) -> Vector:
    """A point given in the radiator's frame, in world coordinates."""
    return center + basis @ Vector(local)


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
    alloy = context.material("engine_alloy")

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

    for label, local, seat in (
        ("lower", BRACKET_LOWER_LOCAL, BRACKET_LOWER_SEAT),
        ("upper", BRACKET_UPPER_LOCAL, BRACKET_UPPER_SEAT),
    ):
        start = attach(local)
        end = Vector(seat)
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

    hose_material = context.material("rubber_grip")
    for label, local, fitting in (
        ("upper", HOSE_UPPER_LOCAL, HOSE_UPPER_ENGINE),
        ("lower", HOSE_LOWER_LOCAL, HOSE_LOWER_ENGINE),
    ):
        start = attach(local)
        end = Vector(fitting)
        _tube_object(
            "radiator_hose_%s" % label,
            (
                tuple(start),
                tuple(start.lerp(end, 0.5) + Vector((0.0, 0.012, 0.010))),
                tuple(end),
            ),
            HOSE_DIAMETER,
            context,
            collection,
            root,
            hose_material,
            bend_radius=0.050,
        )
