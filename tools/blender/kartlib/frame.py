"""Issue #12 — frame tubes and floor tray. The chassis itself.

A kart frame is a cage of thin bent tubes and a flat tray, and issue #12 states
the bar plainly: it reads as a kart or it does not from this alone. There is no
bodywork, no engine and no driver doing any of that work at this point.

**`docs/KART_SPEC.md` §10 is the design, and this module is built to it.** Where
a comment here disagrees with the spec, this comment is what is wrong. Issue #190
rewrote the footprint against it, so the four things that make the silhouette
right now read:

1.  **Every corner is a bend, never a miter.** Chassis rails are mandrel-bent
    from one length of tube. `build.tube` fillets the control polyline before
    sweeping for exactly this reason. A fillet eats `radius / tan(theta/2)` off
    each leg, which matters on the regulated bumpers: `_corner` computes that
    tangent and pushes the control point outward so the *built* straight run is
    the figure Art. 9.4 asks for and not the control polyline's.
2.  **The rails are the lowest thing on the kart.** They run 35 mm off the
    ground and the tray bolts on *top* of them. A tray underneath the rails puts
    the frame into the asphalt.
3.  **The rear axle sits above the rails on hangers.** Frame rails at 50 mm,
    axle center at 148 mm. That vertical offset is why a kart jacks rather than
    rolls, so it is structural, not decorative.
4.  **The frame is widest at the REAR.** Rails at ±310 from the rear extremity
    forward to y -48, necking to a waist of ±139 at y +375 and flaring back to
    ±304 at the stub-axle node. This docstring said the exact opposite for two
    milestones -- *"widest at the front cross member ... the rails pinch inward
    through the seat area and stay narrow to the back"* -- and the mesh was
    backwards with it, front ±462.5 and rear ±215, against a measured CRG Road
    Rebel of rear ±314 outer and waist ±149 outer. Spec §10.1 item 2.
5.  **There is no straight cross member at the front-axle line.** The front is a
    U-shaped loop from one stub-axle node around the nose to the other, which is
    what `chassis_cross_front` is. Spec §10.1 item 3. The name is kept because
    `joints.py` matches `chassis_cross_*` against the rails and five spec
    sections name it.
6.  **The two ends of the kart are not symmetric.** The front overhang is 504 mm
    and the rear 367, which is why `length_overall` is gone: no symmetric length
    is legal at both ends at any value. Each end is now built from its own
    sourced overhang. Spec §10.2.

Coordinates: +X right, +Y forward, +Z up. Built for the right-hand side only and
mirrored where a part is symmetric, because authoring both halves is two places
for a number to be wrong. The frame is *not* laterally symmetric on the reference
chassis (its tube `B5` is an inboard rail on one side only, spec §10.10) and this
kart does not build that yet.
"""

from __future__ import annotations

import math

import bmesh
import bpy
from mathutils import Vector

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
    _front_end(context, collection, frame_material, root)
    _bearing_hangers(context, collection, frame_material, root)
    _seat_struts(context, collection, frame_material, root)
    _bumpers(context, collection, frame_material, root)
    _floor_tray(context, collection, frame_material, root)
    del p, detail


# --- shared arithmetic -----------------------------------------------------


def _tube(
    context: build.BuildContext,
    name: str,
    path: list[tuple[float, float, float]],
    diameter: float,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
    *,
    bend_radius: float | None = None,
    bevel: bool = True,
) -> bpy.types.Object:
    """One swept tube, named, parented and optionally beveled.

    Every tube in this module went through five near-identical blocks of this
    before #190 added twenty more parts; one helper is the difference between a
    file where a part is a path and a file where a part is fifteen lines.
    """
    p = context.params
    bm = bmesh.new()
    build.tube(
        bm,
        path,
        diameter,
        context.detail,
        p.bend_radius if bend_radius is None else bend_radius,
    )
    obj = build.object_from_bmesh(
        name, bm, collection, material=material, shade_smooth=True
    )
    if bevel:
        build.bevel_object(obj, context.detail)
    build.set_parent(obj, root)
    return obj


def _corner(
    tangent: Vector,
    along: Vector,
    other: Vector,
    radius: float,
    straight: float,
    *,
    steps: int = 6,
) -> Vector:
    """The control point that makes a filleted bend start exactly at `tangent`.

    `build.fillet` rounds a corner with an arc tangent to both legs, and the arc
    leaves each leg `radius / tan(theta/2)` short of the control point. So a bar
    whose control polyline is 305 mm across does **not** have 305 mm of straight
    between its bends -- at `bend_radius` 60 and a 93° corner it has 191. Art.
    9.4.1 and 9.4.2 dimension the *straight run*, so the control point has to be
    pushed outward by that tangent length, which is what this returns.

    `theta` depends on where the corner ends up, so it is solved by fixed-point
    iteration rather than in closed form. Six steps, always, because a loop that
    stops on a tolerance is a loop whose iteration count depends on floating
    point and `genkart.sh --check` compares two runs of everything.

    Args:
        tangent: where the straight run must end.
        along:   unit vector pointing outward along the straight, past `tangent`.
        other:   the next control point after the corner.
        radius:  the fillet radius the sweep will use.
        straight: length of the straight run, i.e. the other leg of this corner.
    """
    offset = radius
    for _ in range(steps):
        corner = tangent + along * offset
        leg = other - corner
        if leg.length < 1e-9:
            break
        cosine = max(-1.0, min(1.0, (-along).dot(leg.normalized())))
        angle = math.acos(cosine)
        if angle < 1e-4 or abs(angle - math.pi) < 1e-4:
            break
        offset = radius / math.tan(angle * 0.5)
    corner = tangent + along * offset
    # `build.fillet` clamps the tangent to 45% of the shorter leg, and a clamped
    # fillet silently gives back a straight run that is *not* the regulation
    # figure -- which is the whole defect this function exists to prevent, so it
    # is asserted rather than hoped for.
    shortest = min((other - corner).length, straight)
    if offset > shortest * 0.45:
        raise SystemExit(
            "frame.py: a %.0f mm fillet at %s needs a %.1f mm tangent and the "
            "shorter leg is only %.1f mm, so build.fillet will clamp it and the "
            "straight run will not be the regulated length."
            % (radius * 1000.0, tuple(round(v, 4) for v in corner), offset * 1000.0,
               shortest * 1000.0)
        )
    return corner


def _rail_knots(p: P.KartParams) -> list[tuple[float, float]]:
    """(y, x) of the right rail's centerline, front to rear. Spec §10.3.

    Read off the CRG Road Rebel form's 1:10 plan drawing as ratios of its own
    outer rear half-width and applied to this kart's sourced `F` = 650, because
    the drawing is 6% out in absolute scale and dead-on in proportion.
    """
    return [
        (p.frame_node_y, p.frame_half_node),
        (p.frame_waist_y, p.frame_half_waist),
        (p.cross_strut_y, p.frame_half_strut),
        (p.frame_rail_straight_y, p.frame_half_rear),
        (P.rail_rear_y(p), p.frame_half_rear),
    ]


def _rail_x(p: P.KartParams, y: float) -> float:
    """Right rail centerline x at a station, on the control polyline.

    Exact at every station this module uses it for -- the side bumper's two
    attachment stations and the seat stays' roots all sit on straight runs, well
    clear of the waist's arc. It is the polyline rather than the swept
    centerline, and that difference is only nonzero inside a bend.
    """
    knots = _rail_knots(p)
    if y >= knots[0][0]:
        return knots[0][1]
    for (y0, x0), (y1, x1) in zip(knots, knots[1:]):
        if y1 <= y <= y0:
            span = y0 - y1
            if span < 1e-9:
                return x1
            return x0 + (x1 - x0) * (y0 - y) / span
    return knots[-1][1]


def _loop_leg_y(p: P.KartParams, x: float) -> float:
    """Station where the front loop's leg centerline passes through `x`.

    The leg is straight from the stub-axle node to the frontmost segment, and
    that is not a simplification: spec §10.5 derives four separate pickups from
    this line -- the steering hoop's feet at x 200, the lower bumper socket at
    225, the upper at 275 and the pedal mount at 259 -- and every one of them is
    `304 - (dy/260) x 194`. The section's own path table carries an intermediate
    control point at (270, +600) which is 41 mm outboard of that line, so the
    table and the four pickups cannot both be right. The four pickups win: they
    are what three other sections are built against, and a straight diagonal from
    the node to the nose is what the CRG plan drawing shows.
    """
    dx = p.frame_half_node - p.frame_half_front
    dy = P.loop_front_y(p) - p.frame_node_y
    if abs(dx) < 1e-9:
        return p.frame_node_y
    return p.frame_node_y + (p.frame_half_node - x) / dx * dy


# --- rails -----------------------------------------------------------------


def _rail_path(p: P.KartParams) -> list[tuple[float, float, float]]:
    """Control points of the right main rail, front to rear.

    Flat at `rail_z` throughout. The built kart lifted its front end 25 mm and
    nothing sourced that: no reference puts the front of a KZ frame at a
    different height from the rear, and the lift is what let the old front cross
    member sit above the rails and the nose hoops dive to meet it.
    """
    z = P.rail_z(p)
    return [(x, y, z) for y, x in _rail_knots(p)]


def _rails(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    p = context.params
    right = _tube(
        context, "chassis_rail_r", _rail_path(p), p.tube_main, collection, material, root
    )
    left = build.mirror_x(right, "chassis_rail_l", collection)
    build.set_parent(left, root)


# --- cross members ---------------------------------------------------------


def _cross_members(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The four transverse tubes and the front loop. Spec §10.4.

    All four transverse members are Ø30 except the rear strut: the CRG form
    counts six main tubes at 30 ±0.5 and the drawing puts the rear strut below
    21 mm, which is also the tube `026-CH-99` p. 3 requires the homologation
    marking to be visible on.

    Two of them moved a long way. `chassis_cross_mid_front` is Art. 4.6's
    **central strut** -- a load-bearing identification, because the floor tray's
    rear edge is defined as this tube -- and goes from y +230 to y +40, where CRG
    `B2` measures y +36. `chassis_cross_seat` goes from y -60 to y -417 (CRG
    `B6`): nothing needs a tube under the seat, because Art. 4.2.3 gives the seat
    four supports and they come off the rails.
    """
    p = context.params
    z = P.rail_z(p)

    members: list[tuple[str, list[tuple[float, float, float]], float]] = [
        (
            "chassis_cross_mid_front",
            [
                (-p.frame_half_strut, p.cross_strut_y, z),
                (0.0, p.cross_strut_y, z),
                (p.frame_half_strut, p.cross_strut_y, z),
            ],
            p.tube_main,
        ),
        (
            "chassis_cross_seat",
            [
                (-p.frame_half_rear, p.cross_seat_y, z),
                (0.0, p.cross_seat_y, z),
                (p.frame_half_rear, p.cross_seat_y, z),
            ],
            p.tube_main,
        ),
        (
            "chassis_cross_rear",
            [
                (-p.frame_half_rear, P.rear_axle_y(p), z),
                (0.0, P.rear_axle_y(p), z),
                (p.frame_half_rear, P.rear_axle_y(p), z),
            ],
            p.tube_main,
        ),
        (
            "chassis_cross_tail",
            [
                (-p.frame_half_rear, p.cross_tail_y, z),
                (0.0, p.cross_tail_y, z),
                (p.frame_half_rear, p.cross_tail_y, z),
            ],
            p.tube_secondary,
        ),
    ]
    for name, path, diameter in members:
        _tube(context, name, path, diameter, collection, material, root)

    # The front loop, mirrored through the centerline in its own path rather than
    # by copying a mesh: it is one continuous tube from node to node and a
    # mirrored half would put a seam on the kart's centerline.
    loop_y = P.loop_front_y(p)
    _tube(
        context,
        "chassis_cross_front",
        [
            (-p.frame_half_node, p.frame_node_y, z),
            (-p.frame_half_front, loop_y, z),
            (p.frame_half_front, loop_y, z),
            (p.frame_half_node, p.frame_node_y, z),
        ],
        p.tube_main,
        collection,
        material,
        root,
    )


# --- the front end ---------------------------------------------------------


def _front_end(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Kingpin bosses and the two steering supports. Spec §10.5 and §10.6."""
    p = context.params
    _kingpin_bosses(context, collection, material, root)
    _steering_support_lower(context, collection, material, root)
    _steering_support_upper(context, collection, material, root)
    del p


def _kingpin_bosses(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The two bosses the kingpins pass through.

    New at #190, and the reason it had to be: the kart had kingpins in
    `wheels.py` and nothing on the frame for them to pass through, so this module
    invented a rail position from them -- `front_hub_x - 0.090`, i.e. 462.5 per
    side, 925 mm apart and 190 mm outboard of the frame's own sourced 735 mm
    front width. Art. 4.2.2 makes this the one place the frame is permitted to
    articulate, so it is a real part with a real position: x ±320, y +525.

    Vertical, and its underside is flush with the rails' at z 35 rather than
    centered on `rail_z` -- centered would make the boss the lowest thing on the
    kart, and `ground_clearance` is documented to the rail.
    """
    p = context.params
    radius = p.kingpin_boss_diameter * 0.5
    bottom = p.ground_clearance
    top = bottom + p.kingpin_boss_length
    for label, side in (("l", -1.0), ("r", 1.0)):
        bm = bmesh.new()
        build.lathe(
            bm,
            [(0.0, bottom), (radius, bottom), (radius, top), (0.0, top)],
            context.detail.tube_segments,
            axis="Z",
            center=(side * p.kingpin_x, P.front_axle_y(p), 0.0),
        )
        obj = build.object_from_bmesh(
            "chassis_kingpin_boss_%s" % label,
            bm,
            collection,
            material=material,
            shade_smooth=True,
        )
        build.bevel_object(obj, context.detail)
        build.set_parent(obj, root)


def _steering_support_lower(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The hoop that carries the steering column's lower bearing. Spec §10.6.1.

    Kept under the name `chassis_steering_hoop` deliberately: five spec sections
    and two `joints.py` entries name it, and renaming it costs more than it buys.
    It is the **lower** of two supports -- Art. 9.5.3 requires the front panel's
    upper part to be attached to the column support with *"one or more
    independent bars"*, and this project had collapsed both into this one tube.

    Its feet land on the front loop's legs at x ±200, which is where the loop's
    own arithmetic puts the leg centerline at y +639 -- so the weld is exact
    rather than nearby. The built feet were at x ±150 at rail height, 60 mm
    inboard of the loop and 114 mm behind the old cross member, which is the
    7.55 mm gate-2 finding this replaces.

    The bore is sourced off the column's reference photograph rather than derived
    from `wheel_angle`, which is the change that decouples the frame from a
    steering wheel position §Cockpit is about to move.
    """
    p = context.params
    foot_x = p.steering_hoop_foot_x
    foot_y = _loop_leg_y(p, foot_x)
    bore_y = p.steering_bore_y
    bore_z = p.steering_bore_z
    z = P.rail_z(p)
    # The arms run **level at bore height** and dive to the feet only outboard of
    # x 170, which is a clearance rather than a style: Art. 4.6's edging tube runs
    # along the pan's edge at x 137..130 through this whole y band, at z 65..81, so
    # an arm that descended on a straight line from the bore to the foot would
    # cross it -- measured, 84 intersecting triangle pairs. At bore height the arm
    # passes 8 mm above it.
    mid_x = foot_x * 0.85
    mid_y = bore_y + (foot_y - bore_y) * 0.65
    mid_z = bore_z
    _tube(
        context,
        "chassis_steering_hoop",
        [
            (-foot_x, foot_y, z),
            (-mid_x, mid_y, mid_z),
            (0.0, bore_y, bore_z),
            (mid_x, mid_y, mid_z),
            (foot_x, foot_y, z),
        ],
        p.tube_steering_hoop,
        collection,
        material,
        root,
        bend_radius=p.bend_radius * 0.7,
    )


def _steering_support_upper(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Art. 9.5.3's *"one or more independent bars"*. New at #190.

    An inverted V off the central strut, leaning **forward** 34.1° against the
    column's 36° rearward -- opposed, 70.1° included, which is what makes it a
    brace rather than a second parallel tube. Its apex carries the 20 mm block
    the column passes through, and it is what the front panel's upper edge bolts
    to once §Bodywork builds the panel.

    The feet sit 10 mm behind the strut's own station so the two legs clear the
    floor tray's rear edge instead of standing in the middle of it. They still
    weld to the strut: at 10 mm off center a Ø30 tube's surface is at z 61.2 and
    the leg's is at 57, so they overlap by 4 mm.
    """
    p = context.params
    foot_y = p.cross_strut_y - 0.010
    foot_z = P.rail_top_z(p)
    apex = (0.0, p.steering_support_apex_y, p.steering_support_apex_z)
    _tube(
        context,
        "chassis_steering_support_upper",
        [
            (-p.steering_support_foot_x, foot_y, foot_z),
            apex,
            (p.steering_support_foot_x, foot_y, foot_z),
        ],
        p.tube_steering_hoop,
        collection,
        material,
        root,
        bend_radius=p.bend_radius * 0.4,
    )


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

    The outer pair moves from x ±185 to ±300: the rail centerline at the axle
    line is ±310 and the plate is 12 mm thick, so 300 puts the plate inside the
    tube and the weld measures 0 mm instead of being 113 mm of air. Art. 9.1.2
    calls these *"the rear axle brackets"* and makes them where the extra seat
    stays start, which is the other thing the move fixes.
    """
    p = context.params
    axle_z = P.rear_axle_z(p)
    axle_y = P.rear_axle_y(p)
    rail = P.rail_z(p)

    outer = p.frame_half_rear - 0.010
    for label, x in (("l", -outer), ("c", 0.0), ("r", outer)):
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

#: Where the seat's four stays land on the shell, right-hand side. Spec §10.9.
#:
#: **They are `derived` now, and they agree with `cockpit.SEAT_PAD_*` exactly.**
#: §Cockpit publishes four `seat_ear_*` empties off the loft's own samples and builds
#: `seat_bracket_*` from the nearest sampled surface point out to these coordinates,
#: so the stay, the bracket and the shell meet at one number instead of three. The
#: old pair predated the corrected seat -- `seat_y` was 170 mm too far forward and
#: `seat_width_shoulders` did not exist -- and the note below is kept because the
#: *reason* it could not be exact from here is still the lesson.
#:
#: Previously, and still true of any constant authored in this module:
#: `cockpit.py` lofts `seat_shell` from a half-width table and a wing flare along
#: a filleted spine, so its outer edge is a sampled surface rather than a
#: constant -- these are read off that loft (half-width 165 at the widest station,
#: wing 82 mm proud at the hip, hip at (0, -60, +75), back top at (0, -263, +365))
#: and they will land within a few millimeters rather than within 2.0.
#:
#: The fix is not a better guess: §Cockpit publishes four `seat_ear_*` empties off
#: the sampled surface and this module reads them through `context`. A lofted
#: surface met by a constant authored in a second module is the same failure as
#: `Dictionary.get(key, default)` -- it drifts and nothing says so. Highest-value
#: follow-up in spec §10.
SEAT_EAR_FRONT: tuple[float, float, float] = (0.196, -0.215, 0.070)
SEAT_EAR_REAR: tuple[float, float, float] = (0.205, -0.338, 0.300)


def _seat_struts(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The four seat supports Art. 4.2.3 names, and what each one starts on.

    Art. 4.2.3 lists *"seat with four seat supports"* among the chassis auxiliary
    parts, and Art. 9.1.2 adds *"Extra seat stays are allowed between the rear
    axle brackets and the seat."* So the two pairs are different parts with
    different anchors, and the rear pair's anchor is named by a regulation:

        front pair   on the rail at the central strut's station
        rear pair    on the bearing hanger plate, not on the rail

    The rear pair used to start on the rail at y -0.400 and end 78.07 mm from the
    shell, aimed at nothing at all -- the worst gate-2 finding on the kart.

    Their root is at x ±300 rather than spec §10.9's ±185, and that is a
    correction to the spec rather than a deviation from it: §10.9 moves the
    bearing hangers to ±300 in its own table three entries later, so a stay
    rooted at ±185 would start 115 mm inboard of the bracket the article says it
    starts on. ±185 is the *old* hanger position.

    Two more numbers in that path are measurements rather than authorings, both
    against parts this module does not own:

        y -565      §10.9 roots the stay at y -525, which is the axle's own
                    station: `axle_rear` spans y -550..-500 at z 122.5..172.5 and
                    a Ø20 stay at z 130 runs straight through it. The hanger plate
                    spans y -562.5..-487.5, so -565 still welds to it, with 7.5 mm
                    of overlap, and clears the axle by 5 mm.
        z +245 at y -450   `engine_battery` occupies x 179.5..264.5,
                    y -455..-345, z 160..230. A stay that climbed linearly from
                    the hanger to the seat's ear passed through it; lifting the
                    middle control point takes the stay over its top with 15 mm to
                    spare.
    """
    p = context.params
    rail = P.rail_z(p)
    hanger_x = p.frame_half_rear - 0.010
    front_ear = Vector(SEAT_EAR_FRONT)
    rear_ear = Vector(SEAT_EAR_REAR)

    struts = [
        (
            "front",
            [
                (_rail_x(p, p.cross_strut_y) + 0.019, p.cross_strut_y, rail),
                (0.302, -0.020, 0.052),
                tuple(front_ear),
            ],
        ),
        (
            "rear",
            [
                (hanger_x, P.rear_axle_y(p) - 0.040, 0.150),
                (hanger_x, P.rear_axle_y(p) - 0.040, 0.215),
                (0.232, -0.450, 0.264),
                tuple(rear_ear),
            ],
        ),
    ]

    for label, path in struts:
        right = _tube(
            context,
            "chassis_seat_strut_%s_r" % label,
            path,
            p.tube_bumper,
            collection,
            material,
            root,
            bend_radius=p.bend_radius * 0.6,
            bevel=False,
        )
        left = build.mirror_x(right, "chassis_seat_strut_%s_l" % label, collection)
        build.set_parent(left, root)


# --- bumpers ---------------------------------------------------------------


def _bumpers(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Front bumper (two bars), side bumpers (two bars per side), rear hoop.

    Art. 9.4 makes front and side protections compulsory *"made of magnetic steel
    round tubing"* and then dimensions them in detail, and this is the most
    heavily sourced part of the kart: every figure below is in the pinned
    regulation text and none of it was in this repo before #190.

    What was built instead: one front bar at a height no front bar may occupy,
    one side bar per side where the article requires two, 55 mm inboard of the
    minimum width, with no attachment sockets anywhere and every straight run
    outside its band. The rear hoop was 180 mm too far back, because it was
    placed from `length_overall`.
    """
    _front_bumper(context, collection, material, root)
    _side_bumpers(context, collection, material, root)
    _rear_bumper(context, collection, material, root)


def _socket(
    context: build.BuildContext,
    name: str,
    station: tuple[float, float],
    bar_z: float,
    outward: Vector,
    bore: float,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """One welded bumper attachment: a riser off a frame tube plus a sleeve.

    Art. 9.4.1 and 9.4.2 both require the bar to be held by *"two welded chassis
    frame attachments"* that *"allow for a 50.0 mm insertion of the bar"*, and
    neither of the four spacings that specifies was modeled at all. The riser
    starts 10 mm inside the frame tube it welds to, so the weld measures 0 mm
    rather than being a standoff nobody chose.
    """
    p = context.params
    x, y = station
    start_z = P.rail_z(p) - 0.010
    sleeve_end = Vector((x, y, bar_z)) + outward.normalized() * p.bumper_insertion
    _tube(
        context,
        name,
        [(x, y, start_z), (x, y, bar_z), tuple(sleeve_end)],
        bore,
        collection,
        material,
        root,
        bend_radius=0.010,
    )


def _front_bumper(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Two bars, two diameters, two straight runs, two spacings, two heights.

    Art. 9.4.1, PDF pp. 22-23. The article's text is split across the page break
    and reading only p. 23 sees one bar, which is how this project twice
    "corrected" a correct 550 mm attachment spacing out of the design.

        upper bar   Ø>=16   straight 375..395   mounts 550   tube top 200..250
        lower bar   Ø>=20   straight 295..315   mounts 450   tube top  70..110

    A tube center at z 150 -- which is where a bar has to be to clear the built
    fairing -- is a tube top of 160, which is 50 mm above the lower bar's ceiling
    and 40 mm below the upper bar's floor. **The fairing picks up on the upper
    bar**, and the 160 mm figure that request came with is Art. 9.4.2's side
    bumper minimum.
    """
    p = context.params
    bars = (
        (
            "lower",
            p.tube_bumper,
            p.nose_lower_straight,
            p.nose_lower_z,
            p.nose_lower_mounts,
        ),
        (
            "upper",
            p.tube_bumper_upper,
            p.nose_upper_straight,
            p.nose_upper_z,
            p.nose_upper_mounts,
        ),
    )
    for label, diameter, straight, bar_z, mounts in bars:
        socket_x = mounts * 0.5
        socket_y = _loop_leg_y(p, socket_x)
        half = straight * 0.5
        right_socket = Vector((socket_x, socket_y, bar_z))
        corner = _corner(
            Vector((half, P.nose_y(p), bar_z)),
            Vector((1.0, 0.0, 0.0)),
            right_socket,
            p.bend_radius,
            straight,
        )
        _tube(
            context,
            "chassis_nose_hoop_%s" % label,
            [
                (-socket_x, socket_y, bar_z),
                (-corner.x, corner.y, corner.z),
                tuple(corner),
                tuple(right_socket),
            ],
            diameter,
            collection,
            material,
            root,
        )
        # The bar leaves the socket heading for the corner, so that is the
        # direction the sleeve has to lie along for the insertion to be real.
        for side, tag in ((-1.0, "l"), (1.0, "r")):
            outward = Vector((side * corner.x - side * socket_x, corner.y - socket_y, 0.0))
            _socket(
                context,
                "chassis_bumper_socket_front_%s_%s" % (label, tag),
                (side * socket_x, socket_y),
                bar_z,
                outward,
                diameter + 0.008,
                collection,
                material,
                root,
            )

    # Art. 9.4.1: *"Both bars must be connected by the front bumper support."*
    # Not optional, and it did not exist. Two posts in one part, because the
    # article names one support and `joints.py` would otherwise carry the same
    # two declarations twice.
    bm = bmesh.new()
    for side in (-1.0, 1.0):
        build.sweep_tube(
            bm,
            [
                (side * p.front_bumper_support_x, P.nose_y(p), p.nose_lower_z),
                (side * p.front_bumper_support_x, P.nose_y(p), p.nose_upper_z),
            ],
            p.tube_bumper_upper * 0.5,
            context.detail.tube_segments,
        )
    support = build.object_from_bmesh(
        "chassis_front_bumper_support",
        bm,
        collection,
        material=material,
        shade_smooth=True,
    )
    build.bevel_object(support, context.detail)
    build.set_parent(support, root)


def _side_bumpers(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Art. 9.4.2: each side is a **lower and an upper bar**, and the kart had one.

    > Each element must be composed of a lower and an upper bar. They must have a
    > diameter of 20.0 mm. Minimum straight length is 400.0 mm for the lower bar
    > and 300.0 mm for the upper bar. Overall width: 480.0 mm minimum and 520.0
    > maximum for the lower bar, 480.0 minimum and 600.0 maximum for the upper
    > bar (measured to the tube midpoint) in relation to the longitudinal axis.
    > Each bar must be fixed to two welded tube attachments that must be
    > 500.0 ± 5 mm apart.
    > Height of the upper bar: 160.0 mm minimum from the ground.

    The width figure is read as a **distance from the centerline** and that
    reading is `derived`, not sourced: a total width of 480-520 would put the bar
    inboard of the frame's own 650 mm outer rear width and far inboard of the pod
    datum Art. 9.5.4 makes the bodywork occupy, while the same article requires
    the bodywork to be *"securely attached to the side bumpers"*. The half-width
    reading is the only one under which the bar is reachable from the pod.

    The two attachment stations are on the rail centerline 500 mm apart, so their
    x is the rail's rather than authored -- 220 at the front and 310 at the rear.
    """
    p = context.params
    front_y = p.sidebar_mount_front_y
    rear_y = front_y - p.sidebar_mount_pitch
    front_x = _rail_x(p, front_y)
    rear_x = _rail_x(p, rear_y)

    bars = (
        ("", p.sidebar_x_lower, p.sidebar_lower_straight, p.sidebar_lower_z),
        ("_upper", p.sidebar_x_upper, p.sidebar_upper_straight, p.sidebar_upper_z),
    )
    for suffix, bar_x, straight, bar_z in bars:
        tangent_front = p.sidebar_straight_center_y + straight * 0.5
        tangent_rear = p.sidebar_straight_center_y - straight * 0.5
        front_socket = Vector((front_x, front_y, bar_z))
        rear_socket = Vector((rear_x, rear_y, bar_z))
        # A tighter bend than the rails': the legs back to the sockets turn
        # through 60-65 degrees, and at `bend_radius` 60 the arc's own tangent is
        # 107 mm against a 208 mm leg -- past `build.fillet`'s 45% clamp, which
        # would silently give back a straight run shorter than Art. 9.4.2's
        # minimum. `_corner` asserts that rather than letting it happen.
        bend = p.bend_radius * 0.75
        corner_front = _corner(
            Vector((bar_x, tangent_front, bar_z)),
            Vector((0.0, 1.0, 0.0)),
            front_socket,
            bend,
            straight,
        )
        corner_rear = _corner(
            Vector((bar_x, tangent_rear, bar_z)),
            Vector((0.0, -1.0, 0.0)),
            rear_socket,
            bend,
            straight,
        )
        path = [
            tuple(front_socket),
            tuple(corner_front),
            tuple(corner_rear),
            tuple(rear_socket),
        ]
        right = _tube(
            context,
            "chassis_side_bar%s_r" % suffix,
            path,
            p.tube_bumper,
            collection,
            material,
            root,
            bend_radius=bend,
        )
        left = build.mirror_x(right, "chassis_side_bar%s_l" % suffix, collection)
        build.set_parent(left, root)

        # Art. 9.4.2 wants the attachments *"parallel to the ground,
        # perpendicular to the axis of the chassis"*, so the sleeve runs straight
        # outboard rather than along the bar's first leg -- which is 2 degrees
        # off perpendicular here and would have been the only thing in this part
        # not built to the article.
        label = "lower" if suffix == "" else "upper"
        for side, tag in ((-1.0, "l"), (1.0, "r")):
            for station_name, station in (
                ("front", (side * front_x, front_y)),
                ("rear", (side * rear_x, rear_y)),
            ):
                _socket(
                    context,
                    "chassis_bumper_socket_side_%s_%s_%s" % (label, station_name, tag),
                    station,
                    bar_z,
                    Vector((side, 0.0, 0.0)),
                    p.tube_bumper + 0.008,
                    collection,
                    material,
                    root,
                )


def _rear_bumper(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The hoop the rear protection bolts to. Spec §10.8.

    **There is no rear bumper article.** Art. 9.4 makes only front and side
    protections compulsory, and Art. 9.5.5.1 governs the rear wheel *protection*,
    which is bodywork. This hoop exists to carry that panel, so its position is
    arithmetic on the panel rather than on a limit: at y -725 it sits 20 mm inside
    the protection's own 187 mm depth, which is where a panel that bolts over a
    hoop needs it. That is the 5.91 mm gate-2 finding fixed by arithmetic instead
    of by a standoff.

    It was at y -904, because it was placed from `length_overall * 0.5`, and it
    was swept at Ø22 while the parameter that positioned it assumed Ø20 -- which
    is how the kart measured 1.831 m in Godot against a parameter of 1.830 for
    two milestones.
    """
    p = context.params
    z = P.rail_z(p)
    half = p.rear_bumper_half
    _tube(
        context,
        "chassis_rear_bumper",
        [
            (-half, P.rail_rear_y(p) + 0.020, z),
            (-half, P.rear_y(p), p.rear_bumper_z),
            (0.0, P.rear_y(p), p.rear_bumper_z),
            (half, P.rear_y(p), p.rear_bumper_z),
            (half, P.rail_rear_y(p) + 0.020, z),
        ],
        p.tube_bumper,
        collection,
        material,
        root,
    )


# --- floor tray ------------------------------------------------------------

#: Half-width of the floor pan against y, in meters. Art. 4.6 bounds the pan at
#: *"the central axis of the tubes seen from the top"*, which is the rail
#: centerline at each station -- and the rails are not parallel, so this is a
#: table rather than the `tray_width` scalar it replaces.
#:
#: Aft of the waist every row **is** the rail centerline and is therefore
#: `derived` from the sourced frame widths. Forward of it the perimeter is
#: re-entrant -- the rails flare back out to ±304 at the node while the loop
#: closes to ±110 -- so following the tubes would drive the pan into a corner it
#: cannot occupy. Those three rows are `estimated` as the rounded nose a real pan
#: has, and they stay inside the perimeter everywhere by construction.
#:
#: An independent photogrammetric measurement puts the real pan at roughly y +70
#: to +720 and 382 mm wide; 382 is 2 x 191, which is this table at y +290. The
#: article is normative and the photograph agrees with it.
#: The row at y +200 is deliberately absent. It was there, at 220 mm, and that is
#: **4.2 mm outside the perimeter**: the rail's own centerline runs straight from
#: 286 at the strut to 139 at the waist, so it is at 215.8 there, and a pan wider
#: than the tube's axis fails the article. Measured off the mesh rather than
#: reasoned about -- an interpolation table with one more knot than the thing it is
#: interpolating is a table that bulges between them.
TRAY_HALF_WIDTH: tuple[tuple[float, float], ...] = (
    (0.040, 0.286),
    (0.375, 0.139),
    (0.500, 0.130),
    (0.650, 0.118),
    (0.760, 0.110),
)


def _tray_half_width(y: float) -> float:
    """Linear interpolation of `TRAY_HALF_WIDTH`, clamped at both ends."""
    if y <= TRAY_HALF_WIDTH[0][0]:
        return TRAY_HALF_WIDTH[0][1]
    for (y0, h0), (y1, h1) in zip(TRAY_HALF_WIDTH, TRAY_HALF_WIDTH[1:]):
        if y0 <= y <= y1:
            span = y1 - y0
            if span < 1e-9:
                return h1
            return h0 + (h1 - h0) * (y - y0) / span
    return TRAY_HALF_WIDTH[-1][1]


def _merge(values: list[float], tolerance: float = 1e-4) -> list[float]:
    """Sorted values with near-duplicates collapsed.

    Spec's own trap, in the circuit authoring rather than here but the same
    arithmetic: two authored stations landing on the same place, separated by
    float rounding, give every consumer a span of 0.0001 m to divide by.
    """
    out: list[float] = []
    for value in sorted(values):
        if not out or value - out[-1] > tolerance:
            out.append(value)
    return out


def _floor_tray(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Art. 4.6's floor tray, which is mandatory and was in the wrong place.

    > It is mandatory to have a floor tray made of rigid material stretching from
    > the central strut to the front of the chassis frame. The floor tray must fit
    > completely within the perimeter formed by the main tubes, i.e. the central
    > strut, the longitudinal tubes and the front of the chassis frame, without
    > protruding beyond the central axis of the tubes seen from the top. It must
    > be made of a single element, and its surfaces must be uniform, solid,
    > rigid, impenetrable, smooth, without ribs and of constant thickness. It must
    > be laterally edged by a tube or a rim preventing the driver's feet from
    > sliding off the floor tray.
    > The floor tray may be perforated, but the holes must not have a diameter of
    > more than 10 mm and they must be separated by four times their diameter as a
    > minimum. In addition, two holes with a maximum diameter of 35 mm are allowed
    > for steering column and/or gear shift lever access.

    Built extent was y **+180 back to -580**: 580 mm of it behind the origin,
    under the engine bay and out past the rear axle, which is what made
    `powertrain._engine_mount` give up its inboard clamp -- *"it covers the main
    rail through the whole engine bay and out past the rear axle"*. It is now
    y +40 (the central strut) to +760 (the front of the frame), and its width is
    a function of y rather than a constant.

    One Ø35 access aperture is built, on the centerline at the steering bore.
    The article permits two and names the gear shift lever as the other, and this
    kart does not need it: §Cockpit routes the shift rod over the rail at x 285,
    y +88, where this pan's own edge is at 266 -- so the rod passes 19 mm
    outboard of the pan. Spec §99 W1 asks for exactly that measurement.
    """
    p = context.params
    detail = context.detail
    bottom = P.tray_bottom_z(p)
    top = P.tray_top_z(p)

    hole_center = (0.0, p.steering_bore_y)
    hole_radius = p.tray_hole_diameter * 0.5

    # --- the lattice ---
    #
    # Rows in y and columns in a normalized half-width fraction, so a column
    # follows the pan's taper instead of running off its edge. Both lists carry
    # the hole's own stations, which is what lets the aperture be cut by dropping
    # cells and snapping the surviving boundary onto the circle: every boundary
    # vertex lands at exactly `hole_radius` whatever the local cell count is.
    steps = 4 if not detail.is_high else 8
    cells = 4 if not detail.is_high else 8
    rows = [p.tray_rear_y, p.tray_front_y]
    for (y0, _), (y1, _) in zip(TRAY_HALF_WIDTH, TRAY_HALF_WIDTH[1:]):
        for step in range(steps + 1):
            rows.append(y0 + (y1 - y0) * step / steps)
    reach = hole_radius * 2.0
    for step in range(cells + 1):
        rows.append(hole_center[1] - reach + 2.0 * reach * step / cells)
    rows = _merge([y for y in rows if p.tray_rear_y <= y <= p.tray_front_y])

    half_at_hole = _tray_half_width(hole_center[1])
    columns = [-1.0 + 2.0 * step / (cells * 3) for step in range(cells * 3 + 1)]
    for step in range(cells + 1):
        columns.append((hole_center[0] - reach + 2.0 * reach * step / cells) / half_at_hole)
    columns = _merge([u for u in columns if -1.0 <= u <= 1.0])

    bm = bmesh.new()
    top_verts: list[list[bmesh.types.BMVert | None]] = []
    bottom_verts: list[list[bmesh.types.BMVert | None]] = []
    positions: list[list[tuple[float, float]]] = []
    for y in rows:
        half = _tray_half_width(y)
        positions.append([(u * half, y) for u in columns])
        top_verts.append([None] * len(columns))
        bottom_verts.append([None] * len(columns))

    def inside(x: float, y: float) -> bool:
        return math.hypot(x - hole_center[0], y - hole_center[1]) < hole_radius

    # Which cells survive: a cell is dropped when its center is inside the
    # aperture. Computed before any vertex exists, so the snap below can move a
    # vertex that is shared between a dropped cell and a kept one.
    kept: list[list[bool]] = []
    for i in range(len(rows) - 1):
        row: list[bool] = []
        for j in range(len(columns) - 1):
            corners = [
                positions[i][j],
                positions[i][j + 1],
                positions[i + 1][j + 1],
                positions[i + 1][j],
            ]
            cx = sum(corner[0] for corner in corners) * 0.25
            cy = sum(corner[1] for corner in corners) * 0.25
            row.append(not inside(cx, cy))
        kept.append(row)

    used: set[tuple[int, int]] = set()
    dropped_corner: set[tuple[int, int]] = set()
    for i in range(len(rows) - 1):
        for j in range(len(columns) - 1):
            target = used if kept[i][j] else dropped_corner
            for corner in ((i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)):
                target.add(corner)

    for i, j in sorted(used & dropped_corner):
        x, y = positions[i][j]
        offset = Vector((x - hole_center[0], y - hole_center[1]))
        if offset.length < 1e-9:
            continue
        offset = offset.normalized() * hole_radius
        positions[i][j] = (hole_center[0] + offset.x, hole_center[1] + offset.y)

    for i, j in sorted(used):
        x, y = positions[i][j]
        top_verts[i][j] = bm.verts.new(Vector((x, y, top)))
        bottom_verts[i][j] = bm.verts.new(Vector((x, y, bottom)))

    # Top faces wound counter-clockwise seen from +Z and bottom faces reversed,
    # so the shell encloses a positive volume -- `genkart.py`'s signed-volume
    # assert is fatal and `build.box` was wound inward for two milestones without
    # a single render showing it.
    directed: dict[tuple[int, int], int] = {}
    for i in range(len(rows) - 1):
        for j in range(len(columns) - 1):
            if not kept[i][j]:
                continue
            ring = ((i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j))
            bm.faces.new(tuple(top_verts[a][b] for a, b in ring))
            bm.faces.new(tuple(bottom_verts[a][b] for a, b in reversed(ring)))
            for index in range(4):
                key = (
                    ring[index][0] * len(columns) + ring[index][1],
                    ring[(index + 1) % 4][0] * len(columns) + ring[(index + 1) % 4][1],
                )
                directed[key] = directed.get(key, 0) + 1

    # Every directed edge with no opposite is on a boundary -- the pan's outline
    # or the aperture -- and gets a wall. One walk covers both, so the aperture
    # is closed by the same code that closes the perimeter.
    for (a, b), _count in sorted(directed.items()):
        if (b, a) in directed:
            continue
        ai, aj = divmod(a, len(columns))
        bi, bj = divmod(b, len(columns))
        bm.faces.new(
            (
                top_verts[ai][aj],
                bottom_verts[ai][aj],
                bottom_verts[bi][bj],
                top_verts[bi][bj],
            )
        )

    obj = build.object_from_bmesh(
        "chassis_floor_tray", bm, collection, material=context.material("tray_aluminium")
    )
    build.bevel_object(obj, context.detail)
    build.set_parent(obj, root)

    _tray_edges(context, collection, material, root)


def _tray_edges(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Art. 4.6's mandatory lateral edging, which did not exist.

    > It must be laterally edged by a tube or a rim preventing the driver's feet
    > from sliding off the floor tray.

    The rails cannot serve: the rail's top is at z 65 and the pan's top is at
    z 69, so the rail stands 4 mm *below* the surface a foot would slide off.

    The tube's centerline is at z 73 rather than spec §10.7's 77, and the reason
    is a gate rather than a preference: at 77 the tube's underside is at 69, which
    leaves 4 mm of air between it and the rail's top surface at 65 -- so the part
    Art. 4.6 requires to be welded to the frame would be declared welded and
    measured 4 mm apart. At 73 the underside is flush with the rail and the tube
    still stands 12 mm proud of the pan, which is what the article asks it to do.
    Aft of the waist the pan's edge *is* the rail centerline, so that weld runs
    continuously rather than at two points.
    """
    p = context.params
    z = P.rail_top_z(p) + p.tube_tray_edge * 0.5
    path = [(half, y, z) for y, half in TRAY_HALF_WIDTH]
    right = _tube(
        context,
        "chassis_tray_edge_r",
        path,
        p.tube_tray_edge,
        collection,
        material,
        root,
        bend_radius=p.bend_radius * 0.8,
    )
    left = build.mirror_x(right, "chassis_tray_edge_l", collection)
    build.set_parent(left, root)
