"""Issue #13 — seat, steering wheel, pedals, and the KZ's shifter and clutch.

This is the geometry the cockpit camera spends the whole race looking at, and
ARCHITECTURE.md §7 puts it in its own group for exactly that reason: it is an LOD
only one rig ever sees, so it goes in the `interior` collection rather than into
`chassis`, and issue #20 can then range-cull the whole node from the chase view.

What makes a cockpit read as a KZ shifter kart rather than as a rental kart, in
order of how much each one costs to get wrong:

1.  **The seat is a thin shell, not a slab.** A KZ seat is a fiberglass shell
    `seat_thickness` thick with flared wings that wrap the hips and a lip rolled
    around every free edge. Built as a lofted surface plus its own offset, so the
    8 mm rim is real geometry with an edge to catch a highlight. A zero-thickness
    seat reads as a decal on the frame.
2.  **The wheel is square to its own column.** Everything about the steering
    wheel — the rim plane, the boss, the pivot's basis — is derived from the one
    vector between `params.steering_column_base` and the wheel center. Authoring
    the wheel's angle a second time is how a wheel ends up visibly skewed on its
    column, which is the single most obvious cockpit tell there is.
3.  **The wheel is a flat-bottomed butterfly, not a car wheel.** Three spokes, a
    visible dished boss, and a chord across the bottom so the driver's thighs
    clear it. A circular rim makes a kart look like a toy car immediately.
    Measured on the built rim: 319.7 mm wide, 243.7 mm tall, a 39.8 mm dip
    between the horns and a 135.6 mm flat across the bottom. The dip is the
    feature that has to survive being looked at from 0.55 m, and the first
    version's 13.3 mm did not — see `WHEEL_OUTLINE`.
4.  **The shifter and the clutch lever are the whole KZ silhouette.** §6.3: hand
    shifter on the driver's right, clutch lever on the wheel. Issue #15's
    silhouette test is "reads as a shifter kart rather than a single-speed", and
    these two parts are what decide it.
5.  **The pedals are far forward and the legs are nearly straight.** They hang
    from a cross tube ahead of the front cross member. Their reach relative to
    the seat is one of the strongest scale cues on the whole kart.

    **They are not, and this module cannot fix it.** Measured hip point to pedal
    pad face: 618.5 mm. A 50th-percentile adult leg is 432 mm of thigh plus
    450 mm from the knee to the ball of the foot, so 618.5 mm folds the knee to
    89 degrees. "Nearly straight" is 850-870 mm. The same seat-to-wheel arithmetic
    puts the rim 4 mm beyond a fully extended arm. Both distances come from
    `seat_y`, `pedal_y` and `wheel_center_y` in params.py, which this module does
    not own — the numbers and the proposed replacements are in the issue #13
    report, and criterion 1 fails until they change. Nothing here is worth
    retuning around them, because every part in this file is placed *from* those
    parameters and would only have to move back again.

Coordinates: +X right, +Y forward, +Z up. Nothing here is mirrored wholesale —
the seat is symmetric but is built in one pass because it is a single lofted
surface, and the shifter is deliberately right-hand only.

Interfaces published for later milestones. The axes are measured on the built
scene and on the exported `.glb`, not inferred from this file — see `_steering`
for the evidence:

    name                   Blender local   Godot local   what it does
    steering_pivot         +Z              +Y            positive steers left
    pedal_throttle_pivot   +X              +X            positive presses
    pedal_brake_pivot      +X              +X            positive presses
    seat_root              --              --            hip point, CoM reference
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
# Every one of these is a real dimension of the kart and should move into
# `KartParams` when the parameter block is next touched. They live here only
# because params.py is owned elsewhere and was being edited concurrently while
# this module was written. None of them is a free choice: each is noted with
# what constrains it.

#: Seat pan depth, hip point to the pan's front lip. KZ seats run 290-310 mm.
SEAT_PAN_LENGTH = 0.300

#: How far the pan's front lip rises above `seat_z`. The lip has to hold the
#: driver against the pan under braking, so it is a real edge, not a taper.
SEAT_PAN_FRONT_RISE = 0.055

#: Length of the level stretch of pan immediately ahead of the hip point.
SEAT_PAN_FLAT = 0.120

#: Radius of the pan-to-back bend. Large, because a fiberglass shell cannot be
#: folded — the lumbar transition on a real seat is a sweeping curve.
SEAT_HIP_RADIUS = 0.100

#: Half-width of the shell as a fraction of `seat_width * 0.5`, against
#: normalized arc length along the spine (0 = pan lip, 1 = top of the back).
#: Widest at the hip, and narrowing again at the top so the shoulders clear it.
SEAT_HALF_WIDTH: tuple[tuple[float, float], ...] = (
    (0.00, 0.655),
    (0.18, 0.760),
    (0.36, 1.000),
    (0.52, 0.990),
    (0.75, 0.930),
    (1.00, 0.812),
)

#: How far the wing edge stands proud of the shell's spine, in meters, against
#: the same normalized arc length. Deepest just above the hip, which is where a
#: kart seat actually grips the driver, and shallow at the top.
SEAT_WING_FLARE: tuple[tuple[float, float], ...] = (
    (0.00, 0.022),
    (0.18, 0.040),
    (0.36, 0.082),
    (0.52, 0.090),
    (0.78, 0.062),
    (1.00, 0.028),
)

#: Half of one lateral cross-section of the shell, as (fraction of half-width,
#: fraction of wing flare). Nearly flat through the middle and turning hard up at
#: the edge, which is what makes the free edge a lip rather than a taper.
SEAT_SECTION: tuple[tuple[float, float], ...] = (
    (0.000, 0.000),
    (0.300, 0.006),
    (0.560, 0.042),
    (0.740, 0.135),
    (0.865, 0.300),
    (0.945, 0.520),
    (0.988, 0.760),
    (1.000, 1.000),
)

#: Steering wheel rim centerline, in units of half the wheel's width, for the
#: right half from the top center round to the bottom center. `_wheel_outline`
#: scales it so the built rim's overall width is exactly `wheel_diameter`, which
#: makes the widest control point 1.000 by definition.
#:
#: The two features that have to read from the cockpit camera are the dip at the
#: top between the horns and the chord across the bottom. **The dip was measured
#: and it was too shallow to see**: 13.3 mm on a 293 mm rim, 4.5% of the width,
#: which subtends 1.4 degrees at the ~0.55 m the driver's eye sits from the rim
#: and is why the M2 turntable read as a plain three-spoke wheel. It is 39.8 mm
#: here, 12.5% of the width, which is what a real KZ butterfly rim runs and what
#: separates it from a round wheel at a glance.
#:
#: The horn tips reach 169.8 mm from the wheel center against a half-width of
#: 159.8, so a butterfly rim is *not* contained by a circle of `wheel_diameter`.
#: That is the shape being what it is rather than a mistake, and it is written
#: down because "diameter" invites the opposite assumption.
WHEEL_OUTLINE: tuple[tuple[float, float], ...] = (
    (0.000, 0.610),
    (0.150, 0.672),
    (0.320, 0.820),
    (0.470, 0.880),
    (0.640, 0.836),
    (0.820, 0.688),
    (0.940, 0.440),
    (1.000, 0.150),
    (0.985, -0.140),
    (0.880, -0.390),
    (0.700, -0.525),
    (0.460, -0.588),
    (0.220, -0.606),
    (0.000, -0.610),
)

#: Where the three spokes meet the rim, as an angle in the wheel plane measured
#: from the wheel's own +X (right) toward +Y (up). Two up at the shoulders of the
#: butterfly and one straight down to the flat bottom.
WHEEL_SPOKE_ANGLES: tuple[float, float, float] = (
    math.radians(25.0),
    math.radians(155.0),
    math.radians(270.0),
)

#: Spoke plate thickness, and its width at the boss and at the rim.
WHEEL_SPOKE_THICKNESS = 0.008
WHEEL_SPOKE_WIDTH_INNER = 0.032
WHEEL_SPOKE_WIDTH_OUTER = 0.020

#: How far forward of the rim plane the boss sits — the wheel's dish. A kart
#: wheel is dished away from the driver so the rim comes back to the hands.
WHEEL_DISH = 0.048

#: Boss flange radius. Has to be visible: a kart boss is a large bolted disc.
WHEEL_BOSS_RADIUS = 0.038

#: How far up the column axis the column's lower end starts, measured from
#: `params.steering_column_base`. `frame.py` puts its steering hoop's apex
#: control point at that same base, but the hoop is a *filleted* tube, so its
#: centerline actually passes 5 mm from the base point — the base sits inside the
#: 22 mm tube. The column therefore starts clear of it. See the report note.
COLUMN_LOWER_CLEAR = 0.026

#: Bearing collar on the column, just above the hoop: (start, end, diameter)
#: along the column axis from the base.
COLUMN_BEARING = (0.030, 0.062, 0.038)

#: Clutch lever, in the wheel's own plane, on the driver's left. Offset toward
#: the driver so the fingers wrap it in front of the rim.
CLUTCH_OFFSET = 0.022
CLUTCH_BLADE: tuple[tuple[float, float], ...] = (
    (-0.052, 0.036),
    (-0.070, 0.046),
    (-0.132, 0.020),
    (-0.146, -0.004),
    (-0.132, -0.016),
    (-0.072, 0.014),
    (-0.052, 0.014),
)

#: Pedal face angle from vertical. The sole of a nearly straight leg meets the
#: pad at roughly this angle, and it is also what keeps the pad's lower tip clear
#: of the front cross member and above `ground_clearance`.
PEDAL_FACE_TILT = 0.260

#: Distance from the pedal's pivot to its tip. The pad face occupies the lower
#: 65% of that, so `pedal_z` lands at 0.675 of the length from the pivot.
PEDAL_ARM_LENGTH = 0.120
PEDAL_PAD_START = 0.042

#: Pedal plate and grip-face thickness.
PEDAL_PLATE_THICKNESS = 0.008
PEDAL_GRIP_THICKNESS = 0.005

#: Pedal pivot cross tube: diameter, half-length, and where its mounting
#: brackets sit laterally (outboard of both pedals so the arms swing free).
PEDAL_TUBE_DIAMETER = 0.016
PEDAL_TUBE_HALF_SPAN = 0.140
PEDAL_MOUNT_X = 0.125

#: Clearance held between a cockpit part and the surface of any frame tube it
#: bolts to. Small but non-zero: the real bolted joint touches, and issue #13's
#: third acceptance criterion is that nothing intersects the frame.
#:
#: **Verified, at triangle level rather than by bounding box.** A BVH overlap of
#: every `interior` mesh against every `chassis` and `wheels` mesh returns zero
#: overlapping triangle pairs, so criterion 3 holds. The gaps that matter, in
#: millimeters: pedal mounts to the front cross member 5.39 (this constant plus
#: the bevel), pedal pads to the same member 6.94, seat shell to the floor tray
#: 6.00, shifter base to the floor tray 1.00, seat shell to the seat struts it
#: bolts to 17.5, steering column to the steering hoop 19.4.
#:
#: Two of those are worth knowing rather than fixing here. The 1.00 mm under the
#: shifter base is this constant measured against the *rail top*, which is where
#: a shifter clamps; the floor tray happens to lie 4 mm above that rail top and
#: gets in between. And the 17.5 mm at the seat struts is the frame reaching for
#: a seat 360 mm wide while `seat_width` builds one 330 mm wide — a parameter
#: disagreement, not a clearance choice. Both are in the report for issue #13.
#:
#: Worth recording for whoever measures next: `BVHTree.FromObject(obj, depsgraph)`
#: builds its tree in the object's **local** space in Blender 5.2, not world
#: space. Overlapping two of them directly compares two parts as if both sat at
#: the origin, which reports the seat as intersecting all four tires. Build from
#: world-space triangles with `BVHTree.FromPolygons` instead.
FRAME_CLEARANCE = 0.005

#: Hand shifter, on the driver's right. §6.3: the lever is what says KZ.
SHIFTER_BASE_X = 0.254
SHIFTER_BASE_Y = 0.040
SHIFTER_PLATE_TOP_Z = 0.086
SHIFTER_ROD_DIAMETER = 0.014
SHIFTER_KNOB_RADIUS = 0.026
SHIFTER_KNOB = (0.262, 0.104, 0.392)
"""Knob center. Sits just above the seat's top edge and well inboard of the
radiator, so the right hand drops straight off the wheel onto it."""


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("interior")

    # One root for the whole cockpit group. The glTF exporter flattens
    # collections, so a single parent is the only thing that makes the interior
    # *one node* — which is what ADR-0025's visibility range needs in order to
    # cull it from the chase camera at all.
    root = build.empty("cockpit_root", (0.0, 0.0, 0.0), collection, size=0.10)

    _seat(context, collection, root)
    _steering(context, collection, root)
    _pedals(context, collection, root)
    _shifter(context, collection, root)


# --- curve and surface helpers ---------------------------------------------
#
# `build.py` covers tubes, lathes and boxes, which is the whole of the frame and
# the powertrain. A seat shell and a butterfly rim are neither, so the two
# primitives they need live here: a spline sampler and a lofted shell.


def _catmull_rom(
    points: list[Vector],
    per_segment: int,
    *,
    closed: bool = False,
) -> list[Vector]:
    """Sample a uniform Catmull-Rom spline that passes through every point.

    Through, not near: the control points are real dimensions — `seat_z`, the top
    of the back, the rim's widest point — so a spline that only approximates them
    would quietly stop honoring the parameter block. End tangents are clamped on
    an open curve and wrapped on a closed one.
    """
    count = len(points)
    if count < 3 or per_segment < 1:
        return list(points) if not closed else list(points)

    def at(index: int) -> Vector:
        if closed:
            return points[index % count]
        return points[min(max(index, 0), count - 1)]

    last = count if closed else count - 1
    sampled: list[Vector] = []
    for segment in range(last):
        p0, p1, p2, p3 = at(segment - 1), at(segment), at(segment + 1), at(segment + 2)
        for step in range(per_segment):
            t = step / per_segment
            t2 = t * t
            t3 = t2 * t
            sampled.append(
                0.5
                * (
                    2.0 * p1
                    + (p2 - p0) * t
                    + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                    + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
                )
            )
    if not closed:
        sampled.append(points[-1])
    return sampled


def _resample(path: list[Vector], count: int) -> list[Vector]:
    """`count` points spaced evenly by arc length, with both endpoints kept.

    The seat's spine comes out of `build.fillet` with all its density in the
    bends and none along the back, and the cross-section varies over that whole
    length. Even spacing is what stops the back of the seat from being one long
    ruled quad.
    """
    lengths = [0.0]
    for index in range(1, len(path)):
        lengths.append(lengths[-1] + (path[index] - path[index - 1]).length)
    total = lengths[-1]
    if total < 1e-9 or count < 2:
        return list(path)

    resampled: list[Vector] = []
    cursor = 1
    for step in range(count):
        target = total * step / (count - 1)
        while cursor < len(path) - 1 and lengths[cursor] < target:
            cursor += 1
        span = lengths[cursor] - lengths[cursor - 1]
        fraction = 0.0 if span < 1e-12 else (target - lengths[cursor - 1]) / span
        resampled.append(path[cursor - 1].lerp(path[cursor], fraction))
    return resampled


def _table(table: tuple[tuple[float, float], ...], t: float) -> float:
    """Linear lookup into an ascending (position, value) table."""
    if t <= table[0][0]:
        return table[0][1]
    for index in range(1, len(table)):
        left_t, left_v = table[index - 1]
        right_t, right_v = table[index]
        if t <= right_t:
            span = right_t - left_t
            if span < 1e-12:
                return right_v
            return left_v + (right_v - left_v) * (t - left_t) / span
    return table[-1][1]


def _grid_normals(grid: list[list[Vector]]) -> list[list[Vector]]:
    """Surface normal at every point of a lofted grid, by central difference.

    Rows run along the spine from front to back and columns from the kart's left
    to its right, so `du x dv` points at the driver everywhere on the shell —
    including out around the rolled wing edge, where it correctly turns outboard.
    That consistent sign is what lets one offset direction thicken the whole
    shell.
    """
    rows = len(grid)
    columns = len(grid[0])
    normals: list[list[Vector]] = []
    for u in range(rows):
        row: list[Vector] = []
        for v in range(columns):
            if u == 0:
                du = grid[1][v] - grid[0][v]
            elif u == rows - 1:
                du = grid[rows - 1][v] - grid[rows - 2][v]
            else:
                du = grid[u + 1][v] - grid[u - 1][v]
            if v == 0:
                dv = grid[u][1] - grid[u][0]
            elif v == columns - 1:
                dv = grid[u][columns - 1] - grid[u][columns - 2]
            else:
                dv = grid[u][v + 1] - grid[u][v - 1]
            normal = du.cross(dv)
            row.append(normal.normalized() if normal.length > 1e-12 else Vector((0.0, 0.0, 1.0)))
        normals.append(row)
    return normals


def _shell(bm: bmesh.types.BMesh, grid: list[list[Vector]], thickness: float) -> None:
    """A lofted surface, its offset copy, and the band that closes the rim.

    The band is the visible payoff: it is `thickness` of real geometry around
    every free edge, so `build.bevel_object` has something to chamfer and the
    edge catches a highlight the way a fiberglass lip does.
    """
    rows = len(grid)
    columns = len(grid[0])
    normals = _grid_normals(grid)

    outer = [
        [bm.verts.new(grid[u][v]) for v in range(columns)]
        for u in range(rows)
    ]
    inner = [
        [bm.verts.new(grid[u][v] + normals[u][v] * thickness) for v in range(columns)]
        for u in range(rows)
    ]

    for u in range(rows - 1):
        for v in range(columns - 1):
            bm.faces.new(
                (outer[u][v], outer[u][v + 1], outer[u + 1][v + 1], outer[u + 1][v])
            )
            bm.faces.new(
                (inner[u][v], inner[u + 1][v], inner[u + 1][v + 1], inner[u][v + 1])
            )

    # Perimeter in a fixed order, so the rim band's vertex order is a function of
    # the grid size alone.
    perimeter: list[tuple[int, int]] = []
    perimeter.extend((0, v) for v in range(columns))
    perimeter.extend((u, columns - 1) for u in range(1, rows))
    perimeter.extend((rows - 1, v) for v in range(columns - 2, -1, -1))
    perimeter.extend((u, 0) for u in range(rows - 2, 0, -1))

    for index in range(len(perimeter)):
        u0, v0 = perimeter[index]
        u1, v1 = perimeter[(index + 1) % len(perimeter)]
        bm.faces.new((outer[u0][v0], outer[u1][v1], inner[u1][v1], inner[u0][v0]))

    # The shell is a closed manifold, so one recalculation gives every face the
    # outward normal and the band's winding does not have to be reasoned about.
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))


def _extruded_polygon(
    bm: bmesh.types.BMesh,
    outline: list[tuple[float, float]],
    origin: Vector,
    axis_a: Vector,
    axis_b: Vector,
    normal: Vector,
    thickness: float,
) -> None:
    """A flat plate: a closed 2D outline given thickness along `normal`.

    Every flat part in the cockpit — pedal arms, wheel spokes, the shifter's
    mounting plate, the clutch blade — is a plate cut from sheet and bent, so one
    primitive covers all of them and each is authored as an outline in the plane
    it actually lies in.
    """
    points = list(outline)
    # Fix the winding from the signed area rather than trusting the author, so
    # the front face always looks along +normal.
    area = 0.0
    for index in range(len(points)):
        a0, b0 = points[index]
        a1, b1 = points[(index + 1) % len(points)]
        area += a0 * b1 - a1 * b0
    if area < 0.0:
        points.reverse()

    half = normal * (thickness * 0.5)
    front = [
        bm.verts.new(origin + axis_a * a + axis_b * b + half) for a, b in points
    ]
    back = [
        bm.verts.new(origin + axis_a * a + axis_b * b - half) for a, b in points
    ]

    bm.faces.new(tuple(front))
    bm.faces.new(tuple(reversed(back)))
    for index in range(len(points)):
        following = (index + 1) % len(points)
        bm.faces.new((front[index], back[index], back[following], front[following]))


def _sweep_closed_planar(
    bm: bmesh.types.BMesh,
    path: list[Vector],
    plane_normal: Vector,
    radius: float,
    segments: int,
) -> None:
    """Sweep a circle along a closed *planar* path, with no seam.

    `build.sweep_tube` cannot do this: it caps both ends and its parallel
    transport gives the first and last ring slightly different frames, which on a
    closed loop shows up as a visible kink where the rim meets itself. For a
    planar curve the correct frame is exact rather than transported — the plane
    normal is one basis vector everywhere — so the loop closes on itself.
    """
    count = len(path)
    if count < 3 or segments < 3:
        return

    rings: list[list[bmesh.types.BMVert]] = []
    for index in range(count):
        tangent = (path[(index + 1) % count] - path[(index - 1) % count]).normalized()
        # `tangent x plane_normal`, not the other way round. The ring's two basis
        # vectors and the sweep direction have to form a right-handed frame, the
        # same way `build.sweep_tube` pairs its normal with `tangent.cross(normal)`
        # — otherwise the face loop below, which is copied from that function,
        # winds the whole rim inward. It did, and `genkart.check_face_winding`
        # is what caught it: the rim enclosed -0.000385 m3.
        across = tangent.cross(plane_normal).normalized()
        ring: list[bmesh.types.BMVert] = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            offset = plane_normal * (math.cos(angle) * radius) + across * (
                math.sin(angle) * radius
            )
            ring.append(bm.verts.new(path[index] + offset))
        rings.append(ring)

    for index in range(count):
        lower = rings[index]
        upper = rings[(index + 1) % count]
        for step in range(segments):
            following = (step + 1) % segments
            bm.faces.new((lower[step], lower[following], upper[following], upper[step]))


# --- the seat --------------------------------------------------------------


def _seat_spine(p: P.KartParams) -> list[tuple[float, float, float]]:
    """Control polyline of the shell's underside, in the kart's centerline plane.

    Four points, front to back, and every one of them is a parameter:

        the pan's front lip, `SEAT_PAN_FRONT_RISE` above the pan
        the front of the level stretch of pan
        the hip point, at `seat_y` and at `seat_z` exactly
        the top of the back, at `seat_z + seat_height`

    `seat_y` is read as the hip point rather than as the middle of the bounding
    box, which is what makes params.py's claim about it true: the driver's mass
    sits at the hip, and ARCHITECTURE.md §6 wants the center of mass slightly
    rearward. Moving `seat_y` moves the driver, and the physics center of mass
    with it.

    The bend at the hip is filleted at `SEAT_HIP_RADIUS`, which leaves the level
    stretch of pan tangent to the arc — so the shell's lowest point is still
    exactly `seat_z` after filleting rather than a few millimeters above it.
    """
    hip_y = p.seat_y
    back_top_y = hip_y - p.seat_height * math.tan(p.seat_back_angle)
    return [
        (0.0, hip_y + SEAT_PAN_LENGTH, p.seat_z + SEAT_PAN_FRONT_RISE),
        (0.0, hip_y + SEAT_PAN_FLAT, p.seat_z),
        (0.0, hip_y, p.seat_z),
        (0.0, back_top_y, p.seat_z + p.seat_height),
    ]


def _seat(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The fiberglass shell, lofted from a spine and a lateral cross-section."""
    p = context.params
    detail = context.detail

    # `seat_root` is at the hip point, not at the shell's bounding-box center:
    # it is the handle the physics reads for where the driver's mass sits, and a
    # pivot in the middle of a bounding box means nothing to anybody.
    seat_root = build.empty(
        "seat_root", (0.0, p.seat_y, p.seat_z), collection, size=0.08
    )
    context.publish("seat_root", seat_root)

    spine = build.fillet(_seat_spine(p), SEAT_HIP_RADIUS, detail.bend_segments)
    stations = _resample(spine, 33 if detail.is_high else 15)

    section = _catmull_rom(
        [Vector(point) for point in SEAT_SECTION],
        3 if detail.is_high else 1,
    )
    # Mirror the authored half about the centerline. Columns run left to right so
    # that `_grid_normals` gets a consistent sign out of du x dv.
    lateral = [Vector((-point.x, point.y)) for point in reversed(section[1:])]
    lateral.extend(section)

    right = Vector((1.0, 0.0, 0.0))
    grid: list[list[Vector]] = []
    for index, position in enumerate(stations):
        t = index / (len(stations) - 1)
        if index == 0:
            tangent = stations[1] - stations[0]
        elif index == len(stations) - 1:
            tangent = stations[-1] - stations[-2]
        else:
            tangent = stations[index + 1] - stations[index - 1]
        tangent.normalize()
        # Perpendicular to the spine, in the centerline plane, pointing at the
        # driver. The wings flare along this, which is what makes them wrap the
        # driver instead of just standing up vertically off the back.
        normal = tangent.cross(right).normalized()

        half_width = p.seat_width * 0.5 * _table(SEAT_HALF_WIDTH, t)
        flare = _table(SEAT_WING_FLARE, t)
        grid.append(
            [
                position + right * (half_width * point.x) + normal * (flare * point.y)
                for point in lateral
            ]
        )

    bm = bmesh.new()
    _shell(bm, grid, p.seat_thickness)
    shell = build.object_from_bmesh(
        "seat_shell",
        bm,
        collection,
        material=context.material("seat_fiberglass"),
        shade_smooth=True,
    )
    build.bevel_object(shell, detail)
    build.set_parent(shell, seat_root)
    build.set_parent(seat_root, root)


# --- steering --------------------------------------------------------------


def _column_frame(p: P.KartParams) -> tuple[Vector, Vector, Vector, Vector, Vector]:
    """(base, center, axis, right, up) — the one derivation everything uses.

    `params.steering_column_base` derives the column's lower end from
    `wheel_angle`, so the line from there to the wheel center *is* the column
    axis and the wheel's plane is its perpendicular. Deriving the wheel's
    orientation from this vector, rather than authoring an angle a second time,
    is the only way the rim cannot end up skewed on its own column.

    The column lies in the kart's centerline plane, so world X is exactly
    perpendicular to it and there is no near-degenerate cross product to guard
    against. `right` and `up` span the wheel's plane, and (right, up, axis) is
    right-handed with `axis` pointing up and back at the driver.
    """
    base = Vector(P.steering_column_base(p))
    center = Vector((0.0, p.wheel_center_y, p.wheel_center_z))
    axis = (center - base).normalized()
    right = Vector((1.0, 0.0, 0.0))
    up = axis.cross(right)
    return base, center, axis, right, up


def _wheel_outline(p: P.KartParams, detail: build.Detail) -> list[Vector]:
    """The rim centerline, closed, in the wheel's own 2D plane.

    Authored as a half and mirrored, then scaled so that the finished rim's
    overall **width** — the sampled outline plus the rim's own thickness — lands
    exactly on `wheel_diameter`. Scaling to fit rather than authoring absolute
    numbers means the shape and the regulated size are two separate decisions,
    and changing one does not silently change the other.

    **Width, not radius, and that distinction was worth 27 mm.** This normalized
    on the longest control *vector* first, which on a butterfly outline is a horn
    at about 60 degrees, not the horizontal extreme. The rim it produced measured
    293.3 mm across against a `wheel_diameter` of 320 — the parameter was landing
    on a dimension nothing on the finished wheel could be measured at. A kart
    wheel is sold and quoted by its width, so the width is what the parameter has
    to mean.

    The scale is measured off a fixed dense sampling rather than off the control
    points, because a Catmull-Rom segment can bulge outside the hull of its own
    control points and the control polygon therefore understates the width. It is
    deliberately *not* `detail`'s sampling: a detail-dependent scale would make
    the low-poly and the high-poly wheel slightly different shapes, and issue
    #19's normal bake needs them to be the same kart at two densities — see
    `build.Detail`. Sampling is an affine combination of the control points, so
    scaling before sampling and scaling after are the same curve.

    The consequence of measuring on a denser sampling than the one built is that
    the low-poly rim lands 0.3 mm *inside* `wheel_diameter` rather than exactly
    on it, and the high-poly a little less. That is the right direction to err
    and it is two orders of magnitude below the 27 mm this replaced.
    """
    half = [Vector(point) for point in WHEEL_OUTLINE]
    # Top center and bottom center are on the centerline and must not be doubled.
    loop = list(half)
    loop.extend(Vector((-point.x, point.y)) for point in reversed(half[1:-1]))

    widest = max(abs(point.x) for point in _catmull_rom(loop, 16, closed=True))
    scale = (p.wheel_diameter - p.wheel_rim_thickness) * 0.5 / widest

    return _catmull_rom(
        [point * scale for point in loop], 4 if detail.is_high else 2, closed=True
    )


def _steering(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Rim, spokes, boss, column, bearing collar and clutch lever.

    **The interface M4 depends on, and it is measured rather than asserted.**
    `steering_pivot` is an empty at the wheel center whose local Z *is* the
    column axis, so turning the wheel is one rotation about the pivot's own local
    Z. The rim, spokes, boss and clutch lever are parented under it; nothing else
    is, so everything that rotates is everything a viewer expects to rotate.

    What was checked headless, because "the code says local Z" is not evidence:

    *   `steering_pivot.matrix_world`'s third column is (0, -0.452886, 0.891568),
        which is the column axis `(center - base).normalized()` to 1e-8, and its
        basis has determinant +1 and unit scale.
    *   Rotating the rim's world-space points 30 deg about the pivot's local Z
        keeps them in a 24.00 mm slab — the rim's own thickness, so the wheel
        stays in its plane. The same rotation about local X or local Y swells
        that slab to 133.8 mm and 171.8 mm, which is the wheel tumbling out of
        its plane rather than turning in it.
    *   A rim point 169.80 mm from the axis sweeps a chord of 14.813 mm at 5 deg
        and 240.128 mm at 90 deg, matching `2 r sin(theta / 2)` to six decimals.

    In the exported `.glb`, `steering_pivot` is a node under `cockpit_root`
    carrying a 26.929 deg rotation about +X — `wheel_angle` exactly — with the
    rim, spokes, boss and lever as its children. Applying `export_yup`'s
    (x, y, z) -> (x, z, -y) to each Blender local axis reproduces that node's
    basis exactly: local X stays +X, local Y becomes **-Z**, and local Z becomes
    **+Y**. So the runtime axis is `Vector3.UP` in the node's own space, and it
    is the exported file saying so rather than this comment.

    **Sign.** The map is a proper rotation, so handedness and therefore the sign
    carry over. Measured on the built scene: a positive rotation moves the rim
    point at the driver's right up and inboard, which is counter-clockwise as the
    driver sees it. **Positive steers left.**
    """
    p = context.params
    detail = context.detail
    base, center, axis, right, up = _column_frame(p)

    pivot = build.empty("steering_pivot", tuple(center), collection, size=0.06)
    # Set the basis before anything is parented: `build.set_parent` reads the
    # parent's world matrix to build the child's parent inverse.
    pivot.matrix_world = Matrix.Translation(center) @ Matrix(
        (right, up, axis)
    ).transposed().to_4x4()
    context.publish("steering_pivot", pivot)

    outline = _wheel_outline(p, detail)
    rim_path = [center + right * point.x + up * point.y for point in outline]

    bm = bmesh.new()
    _sweep_closed_planar(
        bm, rim_path, axis, p.wheel_rim_thickness * 0.5, detail.tube_segments
    )
    rim = build.object_from_bmesh(
        "steering_rim",
        bm,
        collection,
        material=context.material("rubber_grip"),
        shade_smooth=True,
    )
    build.set_parent(rim, pivot)

    _wheel_spokes(context, collection, pivot, outline)
    _wheel_boss(context, collection, pivot)
    _clutch_lever(context, collection, pivot)
    _steering_column(context, collection, root)

    build.set_parent(pivot, root)
    del base


def _wheel_spokes(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
    outline: list[Vector],
) -> None:
    """Three flat plates from the boss out to the rim.

    Each plate's outer end is snapped to the sampled rim point nearest its target
    angle rather than to the angle itself, so a spoke always lands *on* the rim
    however the butterfly outline is retuned — including on the flat bottom,
    where the rim's radius is barely half what it is at the top.
    """
    p = context.params
    _, center, axis, right, up = _column_frame(p)
    inner_radius = WHEEL_BOSS_RADIUS * 0.85

    bm = bmesh.new()
    for angle in WHEEL_SPOKE_ANGLES:
        direction = Vector((math.cos(angle), math.sin(angle)))
        # argmin over a list in a fixed order, so ties resolve to the same index
        # on every run.
        target = min(
            range(len(outline)),
            key=lambda index: -outline[index].normalized().dot(direction),
        )
        outer = outline[target]
        radial = outer.normalized()
        length = outer.length
        # Plate axes: along the spoke, and across it in the wheel's plane.
        along = right * radial.x + up * radial.y
        across = right * (-radial.y) + up * radial.x
        _extruded_polygon(
            bm,
            [
                (inner_radius, WHEEL_SPOKE_WIDTH_INNER * 0.5),
                (length, WHEEL_SPOKE_WIDTH_OUTER * 0.5),
                (length, -WHEEL_SPOKE_WIDTH_OUTER * 0.5),
                (inner_radius, -WHEEL_SPOKE_WIDTH_INNER * 0.5),
            ],
            center,
            along,
            across,
            axis,
            WHEEL_SPOKE_THICKNESS,
        )

    spokes = build.object_from_bmesh(
        "steering_spokes", bm, collection, material=context.material("engine_alloy")
    )
    build.bevel_object(spokes, context.detail)
    build.set_parent(spokes, pivot)


def _wheel_boss(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
) -> None:
    """The dished center boss, revolved about the column axis.

    The profile's innermost radius is exactly the column's, so the column's end
    cap plugs the boss's bore instead of the two parts interpenetrating. The dish
    — the boss standing `WHEEL_DISH` forward of the rim plane — is what brings a
    kart wheel's rim back toward the hands, and it is very visible from the
    cockpit camera.
    """
    p = context.params
    _, center, axis, right, up = _column_frame(p)

    # (radius, distance along the column axis from the wheel center). Negative is
    # forward, away from the driver.
    profile = [
        (p.column_diameter * 0.5, -WHEEL_DISH),
        (0.014, -WHEEL_DISH),
        (0.014, -0.030),
        (0.019, -0.026),
        (0.019, -0.006),
        (WHEEL_BOSS_RADIUS, 0.000),
        (WHEEL_BOSS_RADIUS, 0.008),
        (0.030, 0.012),
        (0.000, 0.012),
    ]

    bm = bmesh.new()
    build.lathe(bm, profile, context.detail.tube_segments, axis="Z")
    bm.transform(
        Matrix.Translation(center) @ Matrix((right, up, axis)).transposed().to_4x4()
    )
    boss = build.object_from_bmesh(
        "steering_boss",
        bm,
        collection,
        material=context.material("engine_alloy"),
        shade_smooth=True,
    )
    build.set_parent(boss, pivot)


def _clutch_lever(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
) -> None:
    """The clutch lever, on the wheel and therefore turning with it.

    ARCHITECTURE.md §6.3: the clutch is used for launches and racing upshifts are
    clutchless, so the lever lives on the wheel like a motorcycle's, on the
    driver's left. Together with the hand shifter it is what makes the cockpit
    read as KZ rather than as a single-speed rental kart.
    """
    p = context.params
    _, center, axis, right, up = _column_frame(p)

    bm = bmesh.new()
    _extruded_polygon(
        bm,
        list(CLUTCH_BLADE),
        center + axis * CLUTCH_OFFSET,
        right,
        up,
        axis,
        0.009,
    )
    # Perch: the clamp that holds the blade. It sits at 62 mm radius on the
    # 155 deg spoke, not on the rim — measured, because the comment here said
    # "rim" and the rim is at 148 mm. A lever clamped to a spoke is what a kart
    # actually carries, so the geometry is right and the sentence was wrong.
    build.box(
        bm,
        (0.022, 0.030, CLUTCH_OFFSET + 0.010),
        tuple(
            center
            + right * -0.056
            + up * 0.026
            + axis * (CLUTCH_OFFSET * 0.5 - 0.004)
        ),
    )
    lever = build.object_from_bmesh(
        "steering_clutch_lever",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
    )
    build.bevel_object(lever, context.detail)
    build.set_parent(lever, pivot)
    del p


def _steering_column(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The column and its bearing collar, along the derived axis.

    Not parented to `steering_pivot`: the column is rotationally symmetric so
    turning it would be invisible, and the bearing collar is the part that does
    *not* turn. Keeping both off the pivot means everything under the pivot is
    exactly what a viewer sees rotate.

    The lower end starts `COLUMN_LOWER_CLEAR` up the axis rather than at
    `steering_column_base`, because the frame's steering hoop is a filleted tube
    whose centerline passes within 5 mm of that base point — the base is inside
    the tube, so a column starting there would run through the frame.
    """
    p = context.params
    base, center, axis, right, up = _column_frame(p)

    lower = base + axis * COLUMN_LOWER_CLEAR
    upper = center - axis * WHEEL_DISH

    bm = bmesh.new()
    build.tube(bm, [tuple(lower), tuple(upper)], p.column_diameter, context.detail, 0.0)
    column = build.object_from_bmesh(
        "steering_column",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
        shade_smooth=True,
    )
    build.set_parent(column, root)

    start, end, diameter = COLUMN_BEARING
    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (0.0, start),
            (diameter * 0.5, start),
            (diameter * 0.5, end),
            (0.0, end),
        ],
        context.detail.tube_segments,
        axis="Z",
    )
    bm.transform(
        Matrix.Translation(base) @ Matrix((right, up, axis)).transposed().to_4x4()
    )
    bearing = build.object_from_bmesh(
        "steering_bearing",
        bm,
        collection,
        material=context.material("engine_alloy"),
        shade_smooth=True,
    )
    build.bevel_object(bearing, context.detail)
    build.set_parent(bearing, root)


# --- pedals ----------------------------------------------------------------


def _pedal_frame(p: P.KartParams) -> tuple[Vector, Vector, Vector]:
    """(pad up-axis, face normal, pivot offset from the pad center).

    The pad leans back by `PEDAL_FACE_TILT`, so its long axis runs up and forward
    and its face looks up and back at the driver's sole. The pivot is up that
    same axis from the pad center, which puts the cross tube ahead of the pads —
    a hanging pedal, and the reason the face leans the way it does at all.
    """
    up = Vector((0.0, math.sin(PEDAL_FACE_TILT), math.cos(PEDAL_FACE_TILT)))
    normal = Vector((0.0, -math.cos(PEDAL_FACE_TILT), math.sin(PEDAL_FACE_TILT)))
    pad_center_from_pivot = (PEDAL_PAD_START + PEDAL_ARM_LENGTH) * 0.5
    return up, normal, up * pad_center_from_pivot


def _pedals(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Throttle right, brake left, hanging from a cross tube ahead of the frame.

    On a kart the pedals sit at or ahead of the front cross member with the
    driver's legs nearly straight, which is one of the strongest scale cues the
    model has. `pedal_y` and `pedal_z` place the pad face; everything else —
    where the pivot lands, how the arm is shaped — follows from that and from
    `PEDAL_FACE_TILT`.

    **The interface M4 depends on.** Each pedal's pivot is an unrotated empty on
    the cross tube's axis, so a positive rotation about its own local **X**
    presses the pedal. That axis survives the glTF y-up conversion unchanged, so
    it is local X in Godot too.
    """
    p = context.params
    up, normal, pivot_offset = _pedal_frame(p)
    right = Vector((1.0, 0.0, 0.0))

    # (name, x, material for the grip face)
    pedals = (
        ("brake", -p.pedal_separation * 0.5),
        ("throttle", p.pedal_separation * 0.5),
    )

    pivot_y = 0.0
    pivot_z = 0.0
    for name, x in pedals:
        pad_center = Vector((x, p.pedal_y, p.pedal_z))
        pivot_point = pad_center + pivot_offset
        pivot_y, pivot_z = pivot_point.y, pivot_point.z

        pivot = build.empty(
            "pedal_%s_pivot" % name, tuple(pivot_point), collection, size=0.04
        )
        context.publish("pedal_%s_pivot" % name, pivot)

        # The plate outline in the pedal's own plane: a narrow arm at the pivot
        # widening into the pad. Distances are measured *down* the arm from the
        # pivot, so they are negative along the pad's up-axis.
        half_pad = p.pedal_width * 0.5
        outline = [
            (0.014, 0.008),
            (0.024, -PEDAL_PAD_START * 0.7),
            (half_pad, -PEDAL_PAD_START),
            (half_pad, -PEDAL_ARM_LENGTH + 0.010),
            (half_pad - 0.008, -PEDAL_ARM_LENGTH),
            (-half_pad + 0.008, -PEDAL_ARM_LENGTH),
            (-half_pad, -PEDAL_ARM_LENGTH + 0.010),
            (-half_pad, -PEDAL_PAD_START),
            (-0.024, -PEDAL_PAD_START * 0.7),
            (-0.014, 0.008),
        ]

        bm = bmesh.new()
        _extruded_polygon(
            bm, outline, pivot_point, right, up, normal, PEDAL_PLATE_THICKNESS
        )
        plate = build.object_from_bmesh(
            "pedal_%s" % name,
            bm,
            collection,
            material=context.material("frame_powdercoat"),
        )
        build.bevel_object(plate, context.detail)
        build.set_parent(plate, pivot)

        # The grip face, standing proud of the plate on the driver's side. A
        # kart pedal has a ribbed rubber or knurled face and it is the only part
        # of the pedal the cockpit camera really sees.
        grip_center = pad_center + normal * (
            (PEDAL_PLATE_THICKNESS + PEDAL_GRIP_THICKNESS) * 0.5
        )
        bm = bmesh.new()
        _extruded_polygon(
            bm,
            [
                (half_pad - 0.006, 0.030),
                (half_pad - 0.006, -0.030),
                (-half_pad + 0.006, -0.030),
                (-half_pad + 0.006, 0.030),
            ],
            grip_center,
            right,
            up,
            normal,
            PEDAL_GRIP_THICKNESS,
        )
        grip = build.object_from_bmesh(
            "pedal_%s_pad" % name,
            bm,
            collection,
            material=context.material("rubber_grip"),
        )
        build.bevel_object(grip, context.detail)
        build.set_parent(grip, pivot)

        build.set_parent(pivot, root)

    _pedal_mounts(context, collection, root, pivot_y, pivot_z)


def _pedal_mounts(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    pivot_y: float,
    pivot_z: float,
) -> None:
    """The cross tube the pedals hang from, and the two brackets carrying it.

    The brackets run back and down to the front cross member and stop
    `FRAME_CLEARANCE` short of its surface. A real bracket clamps around the
    tube; stopping just short of it keeps issue #13's "nothing intersects the
    frame" unambiguous, and at 5 mm the gap is below what the cockpit camera can
    resolve at its near plane.
    """
    p = context.params
    detail = context.detail

    bm = bmesh.new()
    build.tube(
        bm,
        [
            (-PEDAL_TUBE_HALF_SPAN, pivot_y, pivot_z),
            (0.0, pivot_y, pivot_z),
            (PEDAL_TUBE_HALF_SPAN, pivot_y, pivot_z),
        ],
        PEDAL_TUBE_DIAMETER,
        detail,
        0.0,
    )
    tube = build.object_from_bmesh(
        "pedal_cross_tube",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
        shade_smooth=True,
    )
    build.set_parent(tube, root)

    # The frame's front cross member: `frame.py` puts it on the front axle line,
    # 25 mm above the rails. Read from params rather than from frame.py's
    # objects, which this module is not allowed to touch.
    member = Vector((0.0, P.front_axle_y(p), P.rail_z(p) + 0.025))
    tube_point = Vector((0.0, pivot_y, pivot_z))
    span = member - tube_point
    reach = span.length - p.tube_main * 0.5 - FRAME_CLEARANCE
    direction = span.normalized()

    # The bracket is a plate standing on edge: its outline lies in the kart's
    # longitudinal plane and its 10 mm thickness runs across the kart.
    along = direction
    edge = Vector((0.0, -direction.z, direction.y))
    for label, x in (("l", -PEDAL_MOUNT_X), ("r", PEDAL_MOUNT_X)):
        bm = bmesh.new()
        _extruded_polygon(
            bm,
            [
                (0.0, 0.014),
                (reach, 0.011),
                (reach, -0.011),
                (0.0, -0.014),
            ],
            Vector((x, pivot_y, pivot_z)),
            along,
            edge,
            Vector((1.0, 0.0, 0.0)),
            0.010,
        )
        mount = build.object_from_bmesh(
            "pedal_mount_%s" % label,
            bm,
            collection,
            material=context.material("frame_powdercoat"),
        )
        build.bevel_object(mount, detail)
        build.set_parent(mount, root)


# --- hand shifter ----------------------------------------------------------


def _shifter(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The hand shifter, on the driver's right. This is the KZ.

    ARCHITECTURE.md §6.3: six-speed sequential, hand shifter on the right, and
    the gearbox is a real subsystem rather than a parameter. Issue #15's
    silhouette test is whether the kart reads as a shifter rather than as a
    single-speed, and this lever plus the clutch on the wheel are what decide it.

    Mounted on a plate above the right main rail, outboard of the seat's widest
    point so it sits beside the driver's hip rather than under it, and forward of
    the engine so it is not buried in the exhaust.
    """
    p = context.params
    detail = context.detail

    # Mounting plate, held clear of the rail's surface. The rail's own position
    # is `frame.py`'s, so the plate is placed from the seat and the engine
    # instead: inboard of the rail, outboard of the seat's widest point.
    bm = bmesh.new()
    build.box(
        bm,
        (0.036, 0.060, 0.016),
        (SHIFTER_BASE_X + 0.004, SHIFTER_BASE_Y, SHIFTER_PLATE_TOP_Z - 0.008),
    )
    # Shift gate: the slotted plate the lever moves in, which is what says
    # "sequential" rather than "a stick".
    build.box(
        bm,
        (0.010, 0.052, 0.034),
        (SHIFTER_BASE_X - 0.014, SHIFTER_BASE_Y + 0.004, SHIFTER_PLATE_TOP_Z + 0.017),
    )
    base = build.object_from_bmesh(
        "shifter_base", bm, collection, material=context.material("frame_powdercoat")
    )
    build.bevel_object(base, detail)
    build.set_parent(base, root)

    knob = Vector(SHIFTER_KNOB)
    bm = bmesh.new()
    build.tube(
        bm,
        [
            (SHIFTER_BASE_X, SHIFTER_BASE_Y, SHIFTER_PLATE_TOP_Z),
            (SHIFTER_BASE_X + 0.004, SHIFTER_BASE_Y + 0.015, SHIFTER_PLATE_TOP_Z + 0.144),
            (knob.x, knob.y - 0.006, knob.z - SHIFTER_KNOB_RADIUS * 0.6),
        ],
        SHIFTER_ROD_DIAMETER,
        detail,
        0.060,
    )
    lever = build.object_from_bmesh(
        "shifter_lever",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
        shade_smooth=True,
    )
    build.set_parent(lever, root)

    # Knob: a revolved teardrop rather than a sphere. It is a hand-sized object
    # right at the edge of the cockpit camera's frame and a bare ball reads as
    # placeholder geometry.
    radius = SHIFTER_KNOB_RADIUS
    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (0.000, -radius * 1.10),
            (radius * 0.34, -radius * 1.00),
            (radius * 0.78, -radius * 0.66),
            (radius * 1.00, -radius * 0.10),
            (radius * 0.94, radius * 0.46),
            (radius * 0.62, radius * 0.86),
            (radius * 0.24, radius * 1.02),
            (0.000, radius * 1.06),
        ],
        detail.tube_segments,
        axis="Z",
        center=tuple(knob),
    )
    knob_object = build.object_from_bmesh(
        "shifter_knob",
        bm,
        collection,
        material=context.material("rubber_grip"),
        shade_smooth=True,
    )
    build.set_parent(knob_object, root)
    del p
