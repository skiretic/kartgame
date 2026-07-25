"""Issue #14 — wheels, tires, the rear axle and the front stub axles.

Four wheels is where a kart stops looking like a generic small car, and almost
all of that comes from three things:

1.  **The rears are fat and the fronts are not.** 215 mm against 135 mm, at the
    CIK maximum, with the rears 15 mm larger in diameter as well. The contrast is
    the single strongest read in the whole silhouette, so the two ends are built
    from their own parameters and nothing is averaged between them.
2.  **The tread band is flat.** A kart slick is not crowned. It is a flat band
    with a soft shoulder rolling into a short, stiff sidewall, and a square
    shoulder is the clearest tell of toy geometry — which is why
    `tire_shoulder_radius` exists and why the shoulder here is an arc of several
    points rather than one chamfer.
3.  **One continuous rear axle.** ARCHITECTURE.md §6 makes the locked rear axle
    the kart's defining dynamic: no differential, both rear wheels turning at the
    same speed, the inside rear lifting to let the kart turn at all. So it is one
    50 mm cylinder from hub to hub, not two half-shafts with a gap in the middle
    where a diff would go.

**Pivots are interfaces, not labels.** The M3b vehicle solver drives a transform
per wheel and the M4 camera rig looks one up to frame the kart, both by name, so
`wheel_fl`, `wheel_fr`, `wheel_rl`, `wheel_rr` and `rear_axle` are published
exactly as spelled here. Each is an empty at the axle center with the tire and
rim parented under it and their own meshes centered on their own origin, so the
node the solver rotates is unambiguous and no mesh origin has to be moved off its
geometric center to get there.

**Nothing here is mirrored and nothing is rotated 180 degrees about Z.** Both are
tempting for a left-hand wheel and both are wrong: a mirror flips the winding and
a Z rotation flips the handedness of the local +X spin axis, after which two of
the four wheels rotate backwards for the same solver input and the bug shows up
as a subtle visual wrongness nobody can point at. Kart slicks are non-directional
and the rim here is deliberately symmetric about its own center plane, so the
identical, unrotated construction is correct on all four corners and every wheel
spins about its own local +X.

Coordinates: +X right, +Y forward, +Z up. The tires and rims are revolutions
about X — the kart's lateral axis — because a revolution gives exact control of
the silhouette the eye actually reads.
"""

from __future__ import annotations

import math

import bmesh
import bpy

from . import build
from . import params as P


# --- dimensions that are not in the parameter block yet --------------------
#
# Every constant below belongs in params.py, and each is here only because this
# module landed alongside the parameter block rather than after it. They are
# module-level and named rather than inline literals so that moving them is a
# mechanical edit and so that no number appears twice.
#
# The kingpin-to-hub distance is deliberately NOT here: it was duplicated between
# this module and frame.py's `_kingpin_x`, so the frame and the front wheels would
# have silently disagreed the first time one of them changed. It now lives in
# params.py as `stub_axle_length`, which both read.

STUB_DIAMETER: float = 0.025
"""Front spindle. A KZ stub axle is a 17 mm bolt in a cast carrier; 25 mm is the
carrier, which is what is actually visible between the kingpin and the hub."""

TIRE_BEAD_INSET: float = 0.014
"""Axial distance from the tire's widest point in to where it closes on the rim
flange. Sets how far the flange lip stands out of the sidewall."""

TIRE_SIDEWALL_LEAN: float = 0.004
"""How far inboard of the widest point the sidewall's upper end sits, i.e. how
much the sidewall leans outward on its way down to the bulge."""

TIRE_SHOULDER_STEPS: int = 4
TIRE_SIDEWALL_STEPS: int = 3
TIRE_BEAD_STEPS: int = 3
"""Points along each part of the profile. Independent of `tire_segments`, which
is the resolution *around* the tire: the shoulder needs several points at any
circumferential density, and one chamfer segment reads as a toy at every one."""

RIM_FLANGE_LIP: float = 0.006
"""How far the flange stands proud of the bead seat. This lip is the only part
of the rim with a hard edge on it and it is what makes a wheel read as a wheel
rather than as a black disc."""

RIM_FLANGE_WIDTH: float = 0.005
RIM_FLANGE_TAPER: float = 0.004
RIM_FLANGE_OVERHANG: float = 0.004
"""Axial distance the flange lip projects past the tire bead, so it emerges from
the sidewall instead of being buried in it."""

RIM_WALL: float = 0.005
RIM_SEAT_CLEARANCE: float = 0.0006
"""The bead seat is built a hair under `rim_diameter/2` so the rim barrel and the
tire's bore are not two coincident surfaces. The tire hides the barrel either
way, but coincident faces are what makes issue #19's normal bake produce the
speckle it lists as a failure."""

RIM_PLATE_THICKNESS: float = 0.006
RIM_PLATE_BORE: float = 0.016
"""Where the wheel face stops, inside the hub sleeve's wall. It ends *in* the
sleeve rather than on it, so the two revolutions overlap instead of sharing a
surface, and neither one closes with a triangle fan — see `HUB_BOSS_BORE`."""

RIM_PLATE_DISH: float = 0.010
"""Depth of the wheel face's cone, per side. Symmetric, see the module docstring:
the plate sits at the center of a deep barrel and reads as dished from either
side, which is what lets one construction serve both sides of the kart."""

HUB_BOSS_RADIUS: float = 0.034
HUB_BOSS_BORE: float = 0.011
"""The hub is a sleeve rather than a solid plug, for two reasons. A hub with no
bore is not a hub. And a solid cap here is a triangle fan converging on the wheel's
center, which `build.bevel_object` does not survive — it drops the fan and leaves
a hole in the middle of the wheel face. The bore keeps both caps annular, and it
is narrower than the front spindle so nothing can see down it."""

HUB_BOSS_HALF_FRONT: float = 0.055
HUB_SLEEVE_OVERLAP: float = 0.010
"""How far the rear hub sleeve reaches past the end of the axle.

The rear hubs are 1.185 m apart across a 1.08 m axle, so the axle stops 52 mm
short of each wheel's centerline and the sleeve is what closes that gap. Derived
from `axle_length` rather than authored, so a shorter axle lengthens the sleeve
instead of leaving the rear wheels floating."""

SPROCKET_DIAMETER: float = 0.145
"""Pitch diameter of the rear sprocket.

A KZ runs a large rear sprocket and it is very visible — bottom edge inches off
the asphalt, outboard of the seat, in every chase-camera frame.

The figure is derived, not chosen. Karting uses #219 chain, where "219" is the
pitch in thousandths of an inch: 0.219 in, or 5.563 mm. A KZ runs roughly 78-84
teeth, so the pitch diameter is `pitch * teeth / pi` = 5.563 x 80 / pi = 142 mm.
Worth spelling out because the first version of this took the 219 for a diameter
in millimeters and then wrote it as 0.219 *meters* — a 123-tooth sprocket, half
again too big, and close enough to plausible that it survived a render."""

SPROCKET_THICKNESS: float = 0.008
SPROCKET_HUB_RADIUS: float = 0.042
SPROCKET_HUB_HALF: float = 0.014
SPROCKET_X: float = 0.115
"""Sprocket center, on the kart's RIGHT, between the center and right bearings —
frame.py puts hangers at 0 and 0.185, so this lands cleanly between them.

**It was on the left, and the reason given was wrong.** The comment here said "a
KZ drives the left rear", which cannot be a reason for anything: the rear axle is
one solid shaft with both wheels locked to it (ARCHITECTURE.md §6), so it drives
neither wheel preferentially and the sprocket's side is a packaging question, not
a drivetrain one. The packaging answer is that the engine sits on the driver's
right (`params.engine_x`), so the chain has to reach a sprocket on the right —
a chain crossing under the seat to the far side is not a thing any kart does. The
rear brake disc is what goes on the left, to balance it.

Caught by `powertrain.py` building its chain line from the engine and finding the
two sprockets 230 mm apart in a plane they both have to lie in. The magnitude was
right all along; only the sign was mirrored."""


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
    _front_stub_axles(context, collection)


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
    geometry centered on their own origins, so each mesh's exported node
    transform is the identity and the only transform in the chain is the one the
    solver drives.
    """
    p = context.params

    if is_rear:
        diameter = p.tire_rear_diameter
        width = p.tire_rear_width
        hub_x = P.rear_hub_x(p)
        axle_y = P.rear_axle_y(p)
        axle_z = P.rear_axle_z(p)
        # Long enough to reach over the end of the axle. See HUB_SLEEVE_OVERLAP.
        boss_half = hub_x - p.axle_length * 0.5 + HUB_SLEEVE_OVERLAP
    else:
        diameter = p.tire_front_diameter
        width = p.tire_front_width
        hub_x = P.front_hub_x(p)
        axle_y = P.front_axle_y(p)
        axle_z = P.front_axle_z(p)
        boss_half = HUB_BOSS_HALF_FRONT

    center = (side * hub_x, axle_y, axle_z)

    name = "wheel_%s" % corner
    hub = build.empty(name, center, collection, size=0.06)
    context.publish(name, hub)
    # `build.set_parent` reads `parent.matrix_world`, and for an object created
    # this tick that is still the identity until the depsgraph evaluates it — the
    # tire and rim would land at the kart's origin, one hub offset out. One
    # evaluation here rather than a hand-built parent inverse, so the parenting
    # stays the one in build.py that every module uses.
    bpy.context.view_layer.update()

    bm = bmesh.new()
    build.lathe(
        bm,
        _tire_profile(p, diameter, width),
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
    # where a bevel does nothing, or the bead corner buried inside the rim. A
    # bevel on the tread would round the silhouette the profile was written to
    # control.
    tire.location = center
    build.set_parent(tire, hub)

    bm = bmesh.new()
    _rim(context, bm, width, boss_half)
    rim = build.object_from_bmesh(
        "%s_rim" % name,
        bm,
        collection,
        material=context.material("rim_magnesium"),
        shade_smooth=True,
    )
    # The rim is the one part of the wheel with hard edges the camera sees — the
    # flange lip and the hub's outer face — so it takes the bevel.
    # Bevelled on the high-poly bake source only. On the low-poly the bevel
    # doubled each rim from 1,152 to 2,176 triangles -- 61% of the whole kart for
    # four flange lips -- and issue #19's normal bake exists precisely so that
    # shading detail does not have to be paid for in geometry. The silhouette of a
    # 5 inch rim at any distance the player sees it does not need the extra loop.
    if context.detail.is_high:
        build.bevel_object(rim, context.detail)
    rim.location = center
    build.set_parent(rim, hub)


# --- tire profile ----------------------------------------------------------


def _tire_profile(
    p: P.KartParams, diameter: float, width: float
) -> list[tuple[float, float]]:
    """(radius, x) pairs for one tire, revolved about X by `build.lathe`.

    Built as one half and mirrored, so the tire is symmetric about its own center
    plane by construction rather than by two lists agreeing. Symmetry is what
    makes the same mesh correct on both sides of the kart without a mirror or a
    180 degree rotation — see the module docstring for why that matters.

    The half runs from the edge of the flat tread band outward and down:

        tread edge -> shoulder arc -> sidewall -> bead turn-in -> rim seat

    Point order is what sets the surface orientation. `build.lathe` winds a ring
    pair so that a profile advancing in +x on the outward-facing side gives
    outward normals, so the fold at the widest point — where x stops increasing
    and turns back inboard toward the bead — is what makes the turn-in face
    outboard rather than inside out.
    """
    tread_radius = diameter * 0.5
    half_width = width * 0.5
    rim_radius = p.rim_diameter * 0.5
    bulge_radius = rim_radius + p.tire_sidewall_bulge

    # Clamped so that a parameter sweep onto a narrow tire tightens the shoulder
    # instead of folding the profile through its own center plane.
    shoulder = min(p.tire_shoulder_radius, (half_width - TIRE_SIDEWALL_LEAN) * 0.6)

    wall_x = half_width - TIRE_SIDEWALL_LEAN
    tread_x = wall_x - shoulder
    shoulder_top = tread_radius - shoulder

    half: list[tuple[float, float]] = [(tread_radius, tread_x)]

    # Shoulder: a quarter arc of `shoulder`, tangent to the flat tread where it
    # starts and purely radial where it ends, so neither joint creases.
    for step in range(1, TIRE_SHOULDER_STEPS + 1):
        angle = 0.5 * math.pi * step / TIRE_SHOULDER_STEPS
        half.append(
            (
                shoulder_top + shoulder * math.cos(angle),
                tread_x + shoulder * math.sin(angle),
            )
        )

    # Sidewall: down to the bulge, leaning outward on the way. Eased at both ends
    # so it leaves the shoulder radially and arrives at the widest point with no
    # axial slope left, which is what makes the bulge read as round.
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
    # onto the rim seat, tangent to the sidewall at the start and to the rim
    # flange face at the end.
    for step in range(1, TIRE_BEAD_STEPS + 1):
        angle = 0.5 * math.pi * step / TIRE_BEAD_STEPS
        half.append(
            (
                bulge_radius - (bulge_radius - rim_radius) * math.sin(angle),
                half_width - TIRE_BEAD_INSET * (1.0 - math.cos(angle)),
            )
        )

    mirrored = [(radius, -along) for radius, along in reversed(half)]
    return mirrored + half


# --- rim -------------------------------------------------------------------


def _rim(
    context: build.BuildContext,
    bm: bmesh.types.BMesh,
    tire_width: float,
    boss_half: float,
) -> None:
    """Barrel with a flange at each end, a dished face plate, and the hub boss.

    Three revolutions into one mesh rather than three objects: they are one cast
    part on a kart and they take one material, and splitting them would only add
    nodes for the exporter to order.

    Each profile is a closed outline with real wall thickness. A rim modeled as a
    zero-thickness shell folds back on itself at the flange, and a lip with no
    thickness catches no highlight — which is exactly the edge that has to read.
    """
    p = context.params
    segments = context.detail.tire_segments
    # The rim is as wide as the tire's beads, plus enough for the flange lip to
    # emerge from the sidewall rather than being swallowed by it.
    half_width = tire_width * 0.5 - TIRE_BEAD_INSET + RIM_FLANGE_OVERHANG

    barrel = _rim_barrel_profile(p, half_width)
    build.lathe(bm, barrel, segments, axis="X", close_profile=True)
    build.lathe(bm, _rim_plate_profile(p), segments, axis="X", close_profile=True)
    build.lathe(
        bm,
        _sleeve_profile(HUB_BOSS_RADIUS, HUB_BOSS_BORE, boss_half),
        segments,
        axis="X",
        close_profile=True,
    )


def _rim_barrel_profile(
    p: P.KartParams, half_width: float
) -> list[tuple[float, float]]:
    """Outline of the barrel: out over the flange, along the seat, and back."""
    seat = p.rim_diameter * 0.5 - RIM_SEAT_CLEARANCE
    lip = seat + RIM_FLANGE_LIP
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

    The intermediate point is what makes it a dish rather than a straight cone —
    the face flattens toward the flange and steepens toward the hub, which is how
    a cast wheel face is actually shaped.
    """
    seat = p.rim_diameter * 0.5 - RIM_SEAT_CLEARANCE
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


def _cylinder_profile(radius: float, half_length: float) -> list[tuple[float, float]]:
    """A capped solid cylinder about the lathe axis, as (radius, along) pairs.

    The caps are triangle fans, so this is only for parts that are not beveled —
    see `HUB_BOSS_BORE` for what a bevel does to a fan.
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


# --- rear axle and sprocket ------------------------------------------------


def _rear_axle(context: build.BuildContext, collection: bpy.types.Collection) -> None:
    """One continuous 50 mm shaft, plus the sprocket that drives it.

    Published as `rear_axle` and given its own pivot, so the axle and sprocket
    can be spun as one assembly. The rear wheels are deliberately *not* parented
    to it: the solver drives all four wheel transforms uniformly in M3b, and
    hanging two of them off a different node would make the rear pair the special
    case in code that has no reason to have one. That both rear wheels and this
    shaft turn together is a constraint the solver enforces — no differential,
    per ARCHITECTURE.md §6 — not something the mesh hierarchy should assert.
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
    build.sweep_tube(
        bm,
        [(-p.axle_length * 0.5, 0.0, 0.0), (p.axle_length * 0.5, 0.0, 0.0)],
        p.axle_diameter * 0.5,
        context.detail.tube_segments,
    )
    axle = build.object_from_bmesh(
        "axle_rear", bm, collection, material=material, shade_smooth=True
    )
    # No bevel: the only hard edges are the two end caps, and both are inside a
    # hub sleeve.
    axle.location = center
    build.set_parent(axle, pivot)

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
    # No bevel: an 8 mm disc against the high-poly bevel width of 4 mm is inside
    # the range where clamping decides the result, and the sprocket's silhouette
    # is the thing being read here, not its edge highlight.
    #
    # No teeth either. They cannot come out of a revolution, and 100-odd of them
    # modeled would cost more triangles than the rest of the kart — they belong
    # to the normal bake in issue #19 or to an alpha-cut ring later.
    sprocket.location = (SPROCKET_X, axle_y, axle_z)
    build.set_parent(sprocket, pivot)


# --- front stub axles ------------------------------------------------------


def _front_stub_axles(
    context: build.BuildContext, collection: bpy.types.Collection
) -> None:
    """Short spindles from each kingpin out to the front hubs.

    Without them the front wheels float: the main rail stops at the kingpin,
    which is `stub_axle_length` inboard of the hub center, and that gap is visible
    from the chase camera.

    These are not parented to the wheel pivots and must not be — a stub axle does
    not spin with its wheel. They belong under a steering knuckle, which does not
    exist until the solver builds the steering geometry in M3b, so for now they
    sit in world space like the frame tubes do.
    """
    p = context.params
    axle_y = P.front_axle_y(p)
    axle_z = P.front_axle_z(p)
    hub_x = P.front_hub_x(p)
    kingpin_x = hub_x - p.stub_axle_length
    material = context.material("axle_steel")

    for corner, side in (("fl", -1.0), ("fr", 1.0)):
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [(side * kingpin_x, axle_y, axle_z), (side * hub_x, axle_y, axle_z)],
            STUB_DIAMETER * 0.5,
            context.detail.tube_segments,
        )
        build.object_from_bmesh(
            "axle_stub_%s" % corner,
            bm,
            collection,
            material=material,
            shade_smooth=True,
        )
