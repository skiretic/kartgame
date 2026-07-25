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
from mathutils import Vector

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

CLUTCH_COVER_RADIUS: float = 0.052
CLUTCH_COVER_INBOARD_X: float = 0.205
"""The clutch is inside the cases on a KZ — a hand-operated multi-plate, not the
external centrifugal drum a TaG kart carries — so what is visible is the round
cover over it on the engine's inboard face."""

IGNITION_COVER_RADIUS: float = 0.052
IGNITION_COVER_OUTBOARD_X: float = 0.430
IGNITION_COVER_Z: float = 0.185
"""Ignition/flywheel cover on the outboard face. Held 16 mm clear of the right
side bar, which passes at x 0.432..0.452, z 0.096..0.117 alongside it."""

CYLINDER_INSET: float = 0.017
CYLINDER_TOP_Z: float = 0.330
"""Top of the barrel, i.e. the head's parting face. The barrel is inset from the
crankcase on both sides, which is what makes the two read as separate castings
rather than as one extruded lump."""

RIB_COUNT: int = 4
RIB_THICKNESS: float = 0.010
RIB_PROUD: float = 0.010
"""Casting ribs on the barrel, and two more on the head.

Deliberately *ribs*, not cooling fins. A KZ engine is water-cooled — that is what
the radiator is for — so it has no fins; what it has is a ribbed cast water
jacket, and four ribs read as an engine at a fraction of the cost of the thirty a
finned air-cooled barrel would need."""

STARTER_RADIUS: float = 0.034
STARTER_X: float = 0.283
STARTER_Z: float = 0.172
"""Onboard electric starter, on the front face of the crankcase. A KZ has to
carry one — CIK requires an onboard starter for the class — and its bulk plus the
battery is a real part of the silhouette."""

BATTERY_SIZE: tuple[float, float, float] = (0.110, 0.085, 0.070)
BATTERY_CENTER: tuple[float, float, float] = (0.230, -0.397, 0.195)
"""The starter's battery, on a bracket behind the engine. Sits above the rear
seat strut, which passes below it at z 0.120..0.148."""

CARB_LO: tuple[float, float, float] = (0.290, -0.424, 0.168)
CARB_HI: tuple[float, float, float] = (0.362, -0.352, 0.250)
CARB_BOWL_RADIUS: float = 0.026
CARB_BOWL_BOTTOM: float = 0.132
"""Dell'Orto VHSH 30 — a 30 mm flat-slide, which is what the class runs. Body
roughly 72 x 72 x 82 mm with the float bowl hanging below it, mounted to the reed
block on the rear of the crankcase and pointing rearward at the airbox."""

AIRBOX_LO: tuple[float, float, float] = (0.250, -0.545, 0.280)
AIRBOX_HI: tuple[float, float, float] = (0.420, -0.430, 0.400)
"""The airbox sits high and rearward, over the right rear tire — inboard of it at
x <= 0.420 against the tire's inner face at 0.485, and above its crown at 0.295.
It is the highest thing on the kart apart from the steering wheel and it is a
large part of what says "shifter" from behind."""

INTAKE_BOOT_DIAMETER: float = 0.056

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

RADIATOR_TANK_HEIGHT: float = 0.025
RADIATOR_TANK_PROUD: float = 0.005
"""Top and bottom tanks, standing a little proud of the core so the core reads as
a core. `radiator_height` covers the tanks *and* the core, so the core's own
height is `radiator_height - 2 * RADIATOR_TANK_HEIGHT`."""

RADIATOR_CORE_FINS: int = 5
HOSE_DIAMETER: float = 0.028
BRACKET_DIAMETER: float = 0.016
"""Radiator brackets. On a KZ the radiator is carried off the seat's right wing
rather than off the frame, and here it has to be: the exhaust belly occupies the
whole volume between the radiator's underside and the main rail."""

RADIATOR_BRACKET_LOWER: tuple[tuple[float, float, float], ...] = (
    (0.310, -0.030, 0.220),
    (0.230, -0.045, 0.200),
    (0.180, -0.055, 0.190),
)
RADIATOR_BRACKET_UPPER: tuple[tuple[float, float, float], ...] = (
    (0.310, -0.030, 0.352),
    (0.250, -0.100, 0.320),
    (0.176, -0.170, 0.285),
)
"""Both run rearward as well as inboard, which is not decoration: the shifter
lever sweeps the volume x 0.247..0.269, y 0.033..0.105 up to z 0.378, so a
bracket leaving the radiator's inboard face anywhere ahead of y = 0 goes through
the driver's gear lever."""

HOSE_LOWER: tuple[tuple[float, float, float], ...] = (
    (0.313, -0.035, 0.208),
    (0.280, -0.100, 0.200),
    (0.244, -0.160, 0.190),
)
HOSE_UPPER: tuple[tuple[float, float, float], ...] = (
    (0.313, -0.035, 0.352),
    (0.290, -0.100, 0.360),
    (0.264, -0.190, 0.368),
)
"""Bottom hose to the water pump on the crankcase, top hose from the head's
outlet. Both pass inboard of the exhaust and outboard of the seat, which is the
only gap there is."""

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
) -> bpy.types.Object:
    bm = bmesh.new()
    build.lathe(
        bm, profile, context.detail.exhaust_segments, axis=axis, center=center
    )
    obj = build.object_from_bmesh(
        name, bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(obj, root)
    return obj


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

    _block(
        "engine_crankcase",
        (CRANKCASE_INBOARD_X, CRANKCASE_REAR_Y, crank_bottom),
        (CRANKCASE_OUTBOARD_X, CRANKCASE_FRONT_Y, crank_top),
        context,
        collection,
        root,
        material,
    )

    # Clutch cover, inboard. The clutch itself is inside the cases on a KZ.
    _lathe_object(
        "engine_clutch_cover",
        _disc_profile(CLUTCH_COVER_RADIUS, (CRANKCASE_INBOARD_X - CLUTCH_COVER_INBOARD_X) * 0.5),
        (
            (CRANKCASE_INBOARD_X + CLUTCH_COVER_INBOARD_X) * 0.5,
            CRANKCASE_FRONT_Y - 0.065,
            p.engine_z + 0.020,
        ),
        context,
        collection,
        root,
        material,
    )

    # Ignition / flywheel cover, outboard.
    _lathe_object(
        "engine_ignition_cover",
        _disc_profile(
            IGNITION_COVER_RADIUS,
            (IGNITION_COVER_OUTBOARD_X - CRANKCASE_OUTBOARD_X) * 0.5,
        ),
        (
            (IGNITION_COVER_OUTBOARD_X + CRANKCASE_OUTBOARD_X) * 0.5,
            center_y,
            IGNITION_COVER_Z,
        ),
        context,
        collection,
        root,
        material,
    )

    # Barrel, inset from the crankcase on every side so the two castings read
    # apart, and its casting ribs.
    barrel_low = (
        CRANKCASE_INBOARD_X + CYLINDER_INSET + 0.005,
        CRANKCASE_REAR_Y + CYLINDER_INSET,
        crank_top,
    )
    barrel_high = (
        CRANKCASE_OUTBOARD_X - CYLINDER_INSET,
        CRANKCASE_FRONT_Y - CYLINDER_INSET - 0.028,
        CYLINDER_TOP_Z,
    )
    _block(
        "engine_cylinder", barrel_low, barrel_high, context, collection, root, material
    )
    _ribs(
        context,
        collection,
        root,
        material,
        "engine_cylinder_rib_%d",
        barrel_low,
        barrel_high,
        RIB_COUNT,
    )

    head_low = (barrel_low[0] - 0.006, barrel_low[1] - 0.004, CYLINDER_TOP_Z)
    head_high = (barrel_high[0] + 0.006, barrel_high[1] + 0.004, head_top)
    _block("engine_head", head_low, head_high, context, collection, root, material)
    _ribs(
        context,
        collection,
        root,
        material,
        "engine_head_rib_%d",
        head_low,
        head_high,
        2,
    )

    # Water outlet on the head's front face, where the top hose lands.
    _lathe_object(
        "engine_water_outlet",
        _disc_profile(0.016, 0.020),
        (0.300, head_high[1] + 0.020, head_top - 0.030),
        context,
        collection,
        root,
        material,
        axis="Y",
    )

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


def _ribs(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    material: bpy.types.Material,
    name_format: str,
    low: tuple[float, float, float],
    high: tuple[float, float, float],
    count: int,
) -> None:
    """Horizontal casting ribs standing proud of a barrel or head.

    Spaced by index rather than by a fixed pitch so that the same ribs land in
    the same place whatever the casting's height is, and named with their index
    so the name is fixed — a rib named from a running counter would move when a
    part is inserted upstream, which is the failure `build.py`'s rule 3 names.
    """
    span = high[2] - low[2]
    for index in range(count):
        fraction = (index + 0.7) / (count + 0.4)
        center_z = low[2] + span * fraction
        _block(
            name_format % index,
            (low[0] - RIB_PROUD, low[1] - RIB_PROUD, center_z - RIB_THICKNESS * 0.5),
            (high[0] + RIB_PROUD, high[1] + RIB_PROUD, center_z + RIB_THICKNESS * 0.5),
            context,
            collection,
            root,
            material,
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

    _block("engine_carb", CARB_LO, CARB_HI, context, collection, root, material)

    bowl_center_x = (CARB_LO[0] + CARB_HI[0]) * 0.5
    bowl_center_y = (CARB_LO[1] + CARB_HI[1]) * 0.5
    _lathe_object(
        "engine_carb_bowl",
        _disc_profile(
            CARB_BOWL_RADIUS, (CARB_LO[2] + 0.004 - CARB_BOWL_BOTTOM) * 0.5
        ),
        (bowl_center_x, bowl_center_y, (CARB_LO[2] + 0.004 + CARB_BOWL_BOTTOM) * 0.5),
        context,
        collection,
        root,
        material,
        axis="Z",
    )

    _block(
        "engine_airbox",
        AIRBOX_LO,
        AIRBOX_HI,
        context,
        collection,
        root,
        context.material("frame_powdercoat"),
    )

    _tube_object(
        "engine_intake_boot",
        (
            (bowl_center_x, CARB_LO[1] + 0.006, CARB_HI[2] - 0.030),
            (bowl_center_x + 0.006, CARB_LO[1] - 0.046, CARB_HI[2] + 0.040),
            (bowl_center_x + 0.010, AIRBOX_LO[1] + 0.070, AIRBOX_LO[2] + 0.040),
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


def _radiator(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Core, top and bottom tanks, fins, two brackets and two hoses.

    **`radiator_width` is the fore-aft dimension, not the lateral one**, and that
    is forced rather than chosen. A KZ radiator hangs vertically on the right of
    the kart with its faces pointing left and right, and the parameters only work
    that way round: 260 mm laid across the kart would run from x = 0.200 to
    x = 0.460, straight through the shifter lever, which `cockpit.py` sweeps from
    x 0.247..0.269 up to z = 0.378. Read fore-aft it sits at x 0.305..0.355, and
    the shifter clears it by 17 mm.

    `radiator_thickness` is therefore the lateral one, and `radiator_height` runs
    from 0.200 to 0.380 — which is what puts the exhaust belly's crown, 15 mm
    below it, where it is.
    """
    p = context.params
    core_material = context.material("radiator_core")
    alloy = context.material("engine_alloy")

    half_thickness = p.radiator_thickness * 0.5
    front_y = p.radiator_y + p.radiator_width * 0.5
    rear_y = p.radiator_y - p.radiator_width * 0.5
    bottom_z = p.radiator_z - p.radiator_height * 0.5
    top_z = p.radiator_z + p.radiator_height * 0.5
    core_low_z = bottom_z + RADIATOR_TANK_HEIGHT
    core_high_z = top_z - RADIATOR_TANK_HEIGHT

    core_inboard = p.radiator_x - half_thickness + RADIATOR_TANK_PROUD
    core_outboard = p.radiator_x + half_thickness - RADIATOR_TANK_PROUD

    _block(
        "radiator_core",
        (core_inboard, rear_y, core_low_z),
        (core_outboard, front_y, core_high_z),
        context,
        collection,
        root,
        core_material,
        bevel=False,
    )

    for label, low_z, high_z in (
        ("bottom", bottom_z, core_low_z),
        ("top", core_high_z, top_z),
    ):
        _block(
            "radiator_tank_%s" % label,
            (p.radiator_x - half_thickness, rear_y, low_z),
            (p.radiator_x + half_thickness, front_y, high_z),
            context,
            collection,
            root,
            alloy,
        )

    # A few vertical ribs across the core. They are what stops a radiator reading
    # as a painted slab at the distance the chase camera sees it from, and they
    # are cheap: `RADIATOR_CORE_FINS` boxes, not a fin per millimeter.
    for index in range(RADIATOR_CORE_FINS):
        fraction = (index + 0.5) / RADIATOR_CORE_FINS
        center_y = rear_y + (front_y - rear_y) * fraction
        _block(
            "radiator_core_fin_%d" % index,
            (core_inboard - 0.004, center_y - 0.006, core_low_z),
            (core_outboard + 0.004, center_y + 0.006, core_high_z),
            context,
            collection,
            root,
            alloy,
            bevel=False,
        )

    for label, path in (
        ("lower", RADIATOR_BRACKET_LOWER),
        ("upper", RADIATOR_BRACKET_UPPER),
    ):
        _tube_object(
            "radiator_bracket_%s" % label,
            path,
            BRACKET_DIAMETER,
            context,
            collection,
            root,
            alloy,
            bend_radius=0.045,
        )

    hose_material = context.material("rubber_grip")
    for label, path in (("lower", HOSE_LOWER), ("upper", HOSE_UPPER)):
        _tube_object(
            "radiator_hose_%s" % label,
            path,
            HOSE_DIAMETER,
            context,
            collection,
            root,
            hose_material,
            bend_radius=0.050,
        )
