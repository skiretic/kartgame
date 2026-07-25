"""Issue #12 — frame tubes and floor tray. The chassis itself.

A kart frame is a cage of thin bent tubes and a flat tray, and issue #12 states
the bar plainly: it reads as a kart or it does not from this alone. There is no
bodywork, no engine and no driver doing any of that work at this point.

What makes the silhouette right, in order of how much each one costs to get
wrong:

1.  **Every corner is a bend, never a miter.** Chassis rails are mandrel-bent
    from one length of tube. `build.tube` fillets the control polyline before
    sweeping for exactly this reason.
2.  **The rails are the lowest thing on the kart.** They run 35 mm off the
    ground and the tray bolts on *top* of them. A tray underneath the rails puts
    the frame into the asphalt.
3.  **The rear axle sits above the rails on hangers.** Frame rails at 50 mm,
    axle center at 148 mm. That vertical offset is why a kart jacks rather than
    rolls, so it is structural, not decorative.
4.  **The frame is widest at the front cross member, not at the rear.** The
    rails pinch inward through the seat area and stay narrow to the back. Getting
    this backwards is what makes a frame read as a toy go-kart.

Coordinates: +X right, +Y forward, +Z up. Built for the right-hand side only and
mirrored, because a chassis is symmetric and authoring both halves is two places
for a number to be wrong.
"""

from __future__ import annotations

import bmesh
import bpy

from . import build
from . import params as P


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    p = context.params
    detail = context.detail
    collection = context.collection("chassis")
    frame_material = context.material("frame_powdercoat")

    root = build.empty("chassis_root", (0.0, 0.0, 0.0), collection, size=0.12)
    context.publish("chassis_root", root)

    _rails(context, collection, frame_material, root)
    _cross_members(context, collection, frame_material, root)
    _bearing_hangers(context, collection, frame_material, root)
    _seat_struts(context, collection, frame_material, root)
    _bumpers(context, collection, frame_material, root)
    _floor_tray(context, collection, root)
    del p, detail


# --- rails -----------------------------------------------------------------


def _rail_path(p: P.KartParams) -> list[tuple[float, float, float]]:
    """Control points of the right main rail, front to rear.

    The front end sits at the kingpin, which is inboard of the front hub by the
    stub axle's length — the tube does not reach the wheel. Everything else is
    the pinch: widest at the front cross member, narrowing through the seat, and
    narrow at the rear so the bearing hangers land close to the sprocket.
    """
    z = P.rail_z(p)
    front_y = P.front_axle_y(p)
    kingpin_x = _kingpin_x(p)
    return [
        (kingpin_x, front_y, z + 0.025),
        (kingpin_x - 0.055, front_y - 0.085, z + 0.010),
        (0.300, front_y - 0.225, z),
        (0.285, -0.100, z),
        (0.245, -0.420, z),
        (0.215, -0.620, z),
    ]


def _kingpin_x(p: P.KartParams) -> float:
    """Lateral position of the kingpin.

    Front track is measured across the tires, so the hub center is inboard of
    that by half a tire width, and the kingpin is inboard of the hub again by the
    stub axle. 90 mm is a representative stub length; what matters is that the
    frame stops short of the wheel rather than running into it.
    """
    return P.front_hub_x(p) - 0.090


def _rails(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    p = context.params
    bm = bmesh.new()
    build.tube(
        bm,
        _rail_path(p),
        p.tube_main,
        context.detail,
        p.bend_radius,
    )
    right = build.object_from_bmesh(
        "chassis_rail_r", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(right, context.detail)
    build.set_parent(right, root)

    left = build.mirror_x(right, "chassis_rail_l", collection)
    build.set_parent(left, root)


# --- cross members ---------------------------------------------------------


def _cross_members(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The four transverse tubes, plus the steering support hoop.

    The front one carries the kingpins and is therefore at the front axle line,
    not somewhere near it: `front_axle_y` is where the steering geometry the
    solver builds in M3b expects to find its pivots.
    """
    p = context.params
    z = P.rail_z(p)
    kingpin_x = _kingpin_x(p)

    # (name, path, diameter)
    members: list[tuple[str, list[tuple[float, float, float]], float]] = [
        (
            "chassis_cross_front",
            [
                (-kingpin_x, P.front_axle_y(p), z + 0.025),
                (0.0, P.front_axle_y(p), z + 0.025),
                (kingpin_x, P.front_axle_y(p), z + 0.025),
            ],
            p.tube_main,
        ),
        (
            "chassis_cross_mid_front",
            [(-0.300, 0.230, z), (0.0, 0.230, z), (0.300, 0.230, z)],
            p.tube_secondary,
        ),
        (
            "chassis_cross_seat",
            [(-0.280, -0.060, z), (0.0, -0.060, z), (0.280, -0.060, z)],
            p.tube_secondary,
        ),
        (
            "chassis_cross_rear",
            [
                (-0.220, P.rear_axle_y(p), z),
                (0.0, P.rear_axle_y(p), z),
                (0.220, P.rear_axle_y(p), z),
            ],
            p.tube_main,
        ),
        (
            "chassis_cross_tail",
            [(-0.215, -0.620, z), (0.0, -0.620, z), (0.215, -0.620, z)],
            p.tube_secondary,
        ),
    ]

    for name, path, diameter in members:
        bm = bmesh.new()
        build.tube(bm, path, diameter, context.detail, p.bend_radius)
        obj = build.object_from_bmesh(
            name, bm, collection, material=material, shade_smooth=True
        )
        build.bevel_object(obj, context.detail)
        build.set_parent(obj, root)

    _steering_support(context, collection, material, root)


def _steering_support(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The hoop that carries the steering column bearing.

    The column and the wheel itself belong to issue #13; this is only the frame
    that holds them, and it is here so that the column has something to be
    mounted to rather than floating. Its top is at the column's lower bearing,
    which `params.steering_column_base` derives from the wheel angle.
    """
    p = context.params
    base = P.steering_column_base(p)
    z = P.rail_z(p)
    path = [
        (0.150, base[1] - 0.020, z),
        (0.110, base[1] - 0.010, base[2] * 0.55),
        (0.0, base[1], base[2]),
        (-0.110, base[1] - 0.010, base[2] * 0.55),
        (-0.150, base[1] - 0.020, z),
    ]
    bm = bmesh.new()
    build.tube(bm, path, p.tube_secondary, context.detail, p.bend_radius * 0.7)
    obj = build.object_from_bmesh(
        "chassis_steering_hoop", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(obj, context.detail)
    build.set_parent(obj, root)


# --- rear axle bearing hangers ---------------------------------------------


def _bearing_hangers(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Three plates lifting the axle line above the rails.

    Three because a KZ carries a center bearing as well as the outer pair, and
    the middle one is visible between the seat and the sprocket. The axle itself
    is issue #14's; these are the frame's.
    """
    p = context.params
    axle_z = P.rear_axle_z(p)
    axle_y = P.rear_axle_y(p)
    rail = P.rail_z(p)

    for label, x in (("l", -0.185), ("c", 0.0), ("r", 0.185)):
        bm = bmesh.new()
        build.box(
            bm,
            (0.012, 0.075, axle_z - rail + 0.030),
            (x, axle_y, (rail + axle_z) * 0.5 + 0.005),
        )
        obj = build.object_from_bmesh(
            "chassis_bearing_hanger_%s" % label, bm, collection, material=material
        )
        build.bevel_object(obj, context.detail)
        build.set_parent(obj, root)


# --- seat struts -----------------------------------------------------------


def _seat_struts(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Diagonal stays from the rails up to the seat's mounting ears.

    Two per side. They are thin, they are in the chase camera's view of the
    kart's flank, and their absence is one of the things that makes a frame look
    like a box rather than a structure.
    """
    p = context.params
    rail = P.rail_z(p)
    # `frame_height` is documented as the top of the frame's highest structural
    # tube, and these struts are it — so they terminate there rather than at a
    # fraction of the seat's height, which is what they did first.
    #
    # Worth naming as a smell: `frame_height` is one of the three dimensions
    # ARCHITECTURE.md §5 calls out by name, and until this line it was used by
    # nothing at all. A parameter that no geometry reads is not a parameter, it is
    # a comment — and it would have drifted away from the mesh silently, which is
    # exactly the failure §5 item 1 is about. The seat back rises above this and
    # should: a seat is not a structural tube.
    top = p.frame_height

    struts = [
        ("front", [(0.272, -0.010, rail), (0.215, -0.075, top * 0.62), (0.178, -0.120, top)]),
        ("rear", [(0.243, -0.400, rail), (0.205, -0.330, top * 0.58), (0.180, -0.250, top * 0.94)]),
    ]

    for label, path in struts:
        bm = bmesh.new()
        build.tube(bm, path, p.tube_bumper, context.detail, p.bend_radius * 0.6)
        right = build.object_from_bmesh(
            "chassis_seat_strut_%s_r" % label,
            bm,
            collection,
            material=material,
            shade_smooth=True,
        )
        build.set_parent(right, root)
        left = build.mirror_x(right, "chassis_seat_strut_%s_l" % label, collection)
        build.set_parent(left, root)


# --- bumpers ---------------------------------------------------------------


def _bumpers(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Nose hoop, side bars and rear bumper — the kart's outline in plan view.

    These are the thinnest tubes on the kart and they define almost the whole
    footprint: the nose hoop is what `length_overall` actually measures to. CIK
    requires them, every kart has them, and a kart without them reads as a
    stripped frame rather than as a race kart.
    """
    p = context.params
    z = P.rail_z(p)
    # `length_overall` is measured to the outside of the bumper tube, so the tube
    # *center* sits half a diameter inboard of it. Subtracting a round 30 mm
    # instead — which is what this did first — left the kart 39 mm short of its
    # own parameter, and issue #21 checks that number in Godot.
    #
    # Each end uses the diameter of the tube *that end is actually swept with*.
    # The rear used `tube_bumper` while the rear bumper is swept at
    # `tube_secondary`, 22 mm against 20, so the tube's outer surface landed
    # 1 mm past the CIK maximum and the kart measured 1.831 m in Godot against a
    # parameter of 1.830. That 1 mm sat in every turntable caption for two
    # milestones, close enough to right to read as rounding.
    nose_y = p.length_overall * 0.5 - p.tube_bumper * 0.5
    rear_y = -p.length_overall * 0.5 + p.tube_secondary * 0.5

    # Nose hoop: two tiers, which is what a kart actually carries.
    for label, height, half_width in (("lower", z + 0.010, 0.300), ("upper", z + 0.105, 0.255)):
        path = [
            (-0.300, P.front_axle_y(p) + 0.020, z + 0.020),
            (-half_width, nose_y - 0.110, height),
            (-half_width * 0.55, nose_y, height),
            (half_width * 0.55, nose_y, height),
            (half_width, nose_y - 0.110, height),
            (0.300, P.front_axle_y(p) + 0.020, z + 0.020),
        ]
        bm = bmesh.new()
        build.tube(bm, path, p.tube_bumper, context.detail, p.bend_radius)
        obj = build.object_from_bmesh(
            "chassis_nose_hoop_%s" % label,
            bm,
            collection,
            material=material,
            shade_smooth=True,
        )
        build.set_parent(obj, root)

    # Rear bumper, wider than the nose and carrying the tail bodywork.
    rear_path = [
        (-0.215, -0.600, z),
        (-0.310, rear_y + 0.060, z + 0.060),
        (-0.310, rear_y, z + 0.090),
        (0.310, rear_y, z + 0.090),
        (0.310, rear_y + 0.060, z + 0.060),
        (0.215, -0.600, z),
    ]
    bm = bmesh.new()
    build.tube(bm, rear_path, p.tube_secondary, context.detail, p.bend_radius)
    obj = build.object_from_bmesh(
        "chassis_rear_bumper", bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(obj, root)

    # Side bars, running the length of the kart outboard of the rails. These are
    # what a sidepod bolts to.
    side_path = [
        (0.335, P.front_axle_y(p) - 0.060, z + 0.015),
        (0.430, 0.300, z + 0.055),
        (0.445, 0.000, z + 0.060),
        (0.430, -0.330, z + 0.055),
        (0.320, -0.560, z + 0.015),
    ]
    bm = bmesh.new()
    build.tube(bm, side_path, p.tube_bumper, context.detail, p.bend_radius)
    right = build.object_from_bmesh(
        "chassis_side_bar_r", bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(right, root)
    left = build.mirror_x(right, "chassis_side_bar_l", collection)
    build.set_parent(left, root)


# --- floor tray ------------------------------------------------------------


def _floor_tray(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The aluminium pan the driver's heels rest on, bolted to the rail tops.

    4 mm thick, and the thickness matters: a tray modeled as a zero-thickness
    plane has no edge to catch a highlight and reads as a decal on the frame.
    """
    p = context.params
    center_y = p.tray_front_y - p.tray_length * 0.5
    bottom = P.tray_bottom_z(p)

    bm = bmesh.new()
    build.box(
        bm,
        (p.tray_width, p.tray_length, p.tray_thickness),
        (0.0, center_y, bottom + p.tray_thickness * 0.5),
    )
    obj = build.object_from_bmesh(
        "chassis_floor_tray", bm, collection, material=context.material("tray_aluminium")
    )
    build.bevel_object(obj, context.detail)
    build.set_parent(obj, root)
