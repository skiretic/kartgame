"""Issue #105 — CIK bodywork: nose fairing, sidepods and rear plastics.

`ROADMAP.md` M2 omitted bodywork, and a KZ without its plastics reads as a bare
rolling chassis rather than a race kart. Bodywork is also most of the kart's
visible surface area, so it carries most of the livery and most of the texel
budget that ADR-0024 sized.

What makes the plastics read right, in order of how much each one costs to get
wrong:

1.  **Every panel is a shell with real thickness, never a slab.** A CIK panel is
    thermoformed from about 3 mm of polyethylene: it has an open back, a visible
    cavity, and a cut edge all the way round. `_loft_shell` builds the outer
    surface, its offset copy, and the band that closes the rim, so
    `build.bevel_object` has a real edge to chamfer. A solid slab reads as a toy
    immediately, and so does a zero-thickness one.
2.  **The sidepods are the widest thing on the kart after the rear tires.** Seen
    from the front they, not the frame, are the kart's shoulder line. They wrap
    outboard *and downward* over the side bars, so the bar runs inside the C of
    the section rather than behind a flat plate.
3.  **The nose fairing passes between the two nose-hoop tiers.** `frame.py` puts
    both tiers' forward tube at y = 0.905, so a fairing that reached that far at
    the tubes' height would be inside them. It does not: the face is at its most
    forward in the z band between the two bars and curves back above and below,
    which is exactly what a real CIK nose looks like — you see the lower bumper
    bar under the fairing and the upper bar above it.
4.  **The fairing is mounted, not floating.** CIK's front fairing mounting kit
    (mandatory since 2013) hangs the panel off two pins on the *lower* nose bar,
    and a fairing displaced in contact is a penalty. Both pins are real geometry
    here, which is what makes the gap between panel and hoop read as a mount
    rather than as a modeling error, and gives M3 something to pivot the panel
    about when contact displaces it.
5.  **Panels are separate objects.** Contact displacement needs one panel to move
    without the others; livery needs one UV island set per panel. Both are
    impossible once the plastics are merged into a body mesh.

Coordinates: +X right, +Y forward, +Z up. The sidepods are built on the right and
mirrored; the nose fairing and the rear plastics are built whole, because both
cross the centerline and a mirrored half would put a seam down the middle of the
one surface the front camera looks straight at.

Interfaces published for later milestones:

    nose_fairing_pivot   the mount pin axis; issue #105's contact displacement
                         rotates the fairing about this, and a displaced fairing
                         is the penalty ADR-0011's race rules will want
"""

from __future__ import annotations

import bmesh
import bpy
from mathutils import Vector

from . import build
from . import params as P


# --- dimensions that belong in params.py -----------------------------------
#
# Same situation as `wheels.py` and `cockpit.py`: every one of these is a real
# dimension of the kart and should move into `KartParams` when the parameter
# block is next touched. They live here only because params.py is owned elsewhere
# and is frozen against concurrent edits. None is a free choice; each is noted
# with what constrains it.

#: Panel wall thickness. CIK bodywork is thermoformed 3 mm polyethylene, and the
#: figure has to be real geometry rather than a shading trick: it is the edge
#: `build.bevel_object` chamfers, and at a grazing angle that chamfer is the only
#: thing that says the panel is a shell.
PANEL_THICKNESS = 0.003

#: Front-to-back depth of the nose fairing. With `nose_y` as its center this puts
#: the apex at y = 0.902 and the rear lip at y = 0.618. The apex figure is the
#: constrained one: `length_overall` is measured to the *outside of the nose
#: bumper tube* at y = 0.915 (frame.py `_bumpers`), so the fairing must stay
#: inside that, and it must also clear the tube itself, whose center is at 0.905.
NOSE_DEPTH = 0.284

#: Ground clearance of the fairing's lowest edge. Below `rail_z` - tube/2 = 0.035
#: the fairing would be the lowest thing on the kart, which is wrong: on a real
#: kart the rails are, and a fairing that grounds before the frame does is a part
#: that gets destroyed on the first curb.
NOSE_BOTTOM_Z = 0.046

#: Largest half-width the fairing can be built at without being inside
#: `frame.py`'s nose hoops. **`nose_width` = 0.680 does not fit this chassis.**
#:
#: Both hoop tiers converge on (±0.300, 0.545, 0.070), so the upper tier dives
#: diagonally from z = 0.155 down to 0.070 across |x| = 0.259..0.300 — straight
#: through the only volume a fairing's outer wing can occupy. Measured against
#: the built hoop mesh, the wing has to pass *under* that tube, and the tube's
#: lower surface is at:
#:
#:     |x| = 0.250   wing top may reach 0.135
#:     |x| = 0.256   wing top may reach 0.129
#:     |x| = 0.262   wing top may reach 0.107
#:     |x| = 0.272   wing top may reach 0.105
#:     |x| = 0.283   wing top may reach 0.084
#:     |x| = 0.290   wing top may reach 0.071
#:
#: while the *lower* tier's front bend forces the wing's underside above 0.078
#: from |x| = 0.290 outward. Above 0.078 and below 0.071 is empty, so the panel
#: cannot exist past |x| ≈ 0.283 at all, and past 0.256 the ceiling collapses so
#: fast that the tip becomes a sliver. Report item: either `nose_width` should be
#: 0.512, or `frame.py`'s upper nose hoop should tie into the frame at
#: steering-hoop height instead of diving to rail height at the front cross
#: member, which is what a real KZ nose bar does and would free the full 0.680.
#:
#: Taken as a clamp rather than a replacement, so a `--set nose_width=` sweep
#: below this figure is still honored.
NOSE_HALF_WIDTH_LIMIT = 0.256

#: Top of the fairing, against |x| in meters — not against a normalized fraction
#: of the half-width, because everything constraining it is at an absolute x.
#: Keyed on the fraction, a narrower fairing moved its own clearance dip outboard
#: with it and stayed just as intersected, which is how the figures above got
#: measured in the first place.
#:
#: 0.176 at the centerline is the pronounced central spine; `nose_height` is
#: exactly this minus `NOSE_BOTTOM_Z`. The fall to 0.122 at the tip is the
#: envelope above, with 2 mm in hand for the low-poly pass — which lofts 13
#: sections across the panel, so between two of them the surface is a chord, and
#: a chord across a falling curve sits above it.
#: The centerline entry is not authored twice: `_nose_top_z` replaces it with
#: `NOSE_BOTTOM_Z[0] + nose_height` and fades that correction out over
#: `NOSE_SPINE_FADE`, so the parameter is *read* by the geometry rather than
#: transcribed into a literal here. With the default kart the correction is zero,
#: which is the check that the two agree. Outboard of the fade the top edge is
#: the hoop-clearance envelope and cannot follow the parameter — that is the
#: honest limit, and `NOSE_HALF_WIDTH_LIMIT` explains it.
NOSE_SPINE_FADE = 0.160

NOSE_TOP_Z: tuple[tuple[float, float], ...] = (
    (0.000, 0.176),
    (0.070, 0.172),
    (0.120, 0.164),
    (0.160, 0.150),
    (0.200, 0.136),
    (0.230, 0.130),
    (0.256, 0.122),
)

#: Bottom edge of the fairing, against |x| in meters. Flat under the spine and
#: lifting outboard: the outer ends of a CIK fairing turn up, and against the
#: falling top edge above, the lift is most of what reads as that from the side.
NOSE_BOTTOM_Z: tuple[tuple[float, float], ...] = (
    (0.000, 0.046),
    (0.120, 0.048),
    (0.185, 0.056),
    (0.230, 0.068),
    (0.256, 0.080),
)

#: Height of the fairing's forward-most point, against |x| in meters. It has to
#: land between the two nose-hoop tiers, whose forward tubes occupy z 0.050..0.070
#: and 0.145..0.165 at y = 0.905: 0.108 is the middle of the 75 mm gap between
#: them, and it is why the fairing's face reaches its most forward point there
#: rather than at its crown. The lift back to 0.106 at the tip, against a bottom
#: edge that has risen to 0.080, is the turned-up outer end.
NOSE_APEX_Z: tuple[tuple[float, float], ...] = (
    (0.000, 0.108),
    (0.140, 0.104),
    (0.215, 0.100),
    (0.256, 0.104),
)

#: How far the apex is swept back from the centerline apex, against |x| /
#: half-width. A fairing is arrowed in plan; a constant leading edge reads as a
#: plank.
NOSE_APEX_SETBACK: tuple[tuple[float, float], ...] = (
    (0.00, 0.000),
    (0.25, 0.008),
    (0.50, 0.030),
    (0.75, 0.062),
    (1.00, 0.105),
)

#: Fore-aft inset of the fairing's two rear free edges from the nominal rear lip
#: at `nose_y - NOSE_DEPTH / 2`. The top edge runs slightly further back than the
#: bottom one, so the panel's open back faces down and rearward rather than
#: straight back — which is what lets the pins reach the lower bar.
NOSE_BACK_TOP_INSET: tuple[tuple[float, float], ...] = (
    (0.00, -0.006),
    (0.50, 0.004),
    (1.00, 0.028),
)
NOSE_BACK_BOTTOM_INSET: tuple[tuple[float, float], ...] = (
    (0.00, 0.022),
    (0.50, 0.030),
    (1.00, 0.050),
)

#: Lateral position of the two CIK mounting pins. 250 mm apart, which is the
#: spacing of the production mounting kit and comfortably inside the lower nose
#: hoop's forward straight (x -0.165..0.165 in frame.py `_bumpers`).
NOSE_PIN_X = 0.125
NOSE_PIN_DIAMETER = 0.014

#: How far the pin stops short of the bumper tube's *surface*. The real joint
#: clamps the tube, but a mesh that touches it exactly registers as an
#: intersection once both are faceted, so the pin ends 1.5 mm out and the gap is
#: invisible at any camera distance.
MOUNT_STANDOFF = 0.0015

#: Where along the nose fairing's profile the pin picks up, as a fraction of the
#: profile's sampled length from the top rear edge. 0.88 is on the lower front
#: skin, a little behind the apex, which is where the production kit's pin
#: bosses are molded.
NOSE_PIN_PROFILE_T = 0.88

#: Forward edge of the sidepod. `sidepod_length` runs back from here, putting the
#: rear edge at y = -0.295. Both ends are constrained by a wheel, and the pod
#: sits between them rather than being centered on the kart:
#:
#:     front tire swept back to y = +0.341 at 25 deg of lock (measured, not
#:     assumed: the mesh's shoulder radius puts it 9 mm behind the arithmetic)
#:     rear tire's forward face at y = -0.378
#:
#: so the usable window is 0.719 long for a 0.560 pod, and this split leaves
#: 76 mm at the front and 83 mm at the rear.
SIDEPOD_FRONT_Y = 0.265

#: Outer surface of the pod as a fraction of `sidepod_x`, against normalized
#: station from the front edge to the rear. The taper at both ends is the shape;
#: it is also load-bearing at the front, where the side bar has already come out
#: to x = 0.432 and the pod still has to wrap *outboard* of it.
SIDEPOD_OUT_FRACTION: tuple[tuple[float, float], ...] = (
    (0.00, 0.960),
    (0.18, 0.992),
    (0.45, 1.000),
    (0.72, 0.998),
    (0.88, 0.984),
    (1.00, 0.962),
)

#: Bottom edge of the pod's C-section, against the same station. Lowest at the
#: widest station and lifting at both ends, which is half the fore-and-aft taper.
SIDEPOD_BOTTOM_Z: tuple[tuple[float, float], ...] = (
    (0.00, 0.070),
    (0.25, 0.054),
    (0.55, 0.048),
    (0.85, 0.052),
    (1.00, 0.066),
)

#: Height of the section above that bottom edge, as a fraction of
#: `sidepod_height`. A fraction rather than a second table of absolute heights so
#: that the parameter is read by the geometry: 1.0 at the deepest station means
#: the pod is exactly `sidepod_height` tall there, and a `--set sidepod_height=`
#: sweep moves the top edge. The taper to 0.76 at the front and 0.83 at the rear
#: is the other half of the fore-and-aft taper.
SIDEPOD_HEIGHT_FRACTION: tuple[tuple[float, float], ...] = (
    (0.00, 0.756),
    (0.20, 0.916),
    (0.55, 1.000),
    (0.80, 0.971),
    (1.00, 0.833),
)

#: Lateral position of the pod's two free edges. The top edge is the constrained
#: one: the radiator stands at x = 0.330 with a 45 mm core, so its outer face is
#: at 0.353, and a real kart's radiator stands proud *above and inboard* of the
#: pod rather than inside it. 0.360 at the front and 0.372 at the widest station
#: keeps the pod's mouth clear of it.
SIDEPOD_TOP_X: tuple[tuple[float, float], ...] = (
    (0.00, 0.360),
    (0.50, 0.372),
    (1.00, 0.366),
)
SIDEPOD_BOTTOM_X: tuple[tuple[float, float], ...] = (
    (0.00, 0.398),
    (0.50, 0.408),
    (1.00, 0.400),
)

#: Top and bottom of the pod's widest *band*, not a single widest point. Below
#: the mid-height of the section, so the flank tucks under toward the floor
#: rather than bulging at the top — that is what makes a pod look like it is
#: shielding a wheel rather than like a box.
#:
#: A band rather than a point because the side bar has to fit inside it. The bar
#: runs at z = 0.105..0.110 with a 10 mm radius, so the pod's flank has to stay
#: outboard of x = 0.442 for the whole of z 0.095..0.115. Modeled with one
#: widest point at `SIDEPOD_BULGE_TOP_Z` the flank had already turned inboard by
#: the bar's height at both tapered ends, and 74 triangles of each pod were
#: inside the bar it is supposed to be bolted to.
SIDEPOD_BULGE_TOP_Z: tuple[tuple[float, float], ...] = (
    (0.00, 0.142),
    (0.50, 0.140),
    (1.00, 0.140),
)
SIDEPOD_BULGE_BOTTOM_Z: tuple[tuple[float, float], ...] = (
    (0.00, 0.092),
    (0.50, 0.096),
    (1.00, 0.092),
)

#: Where the pod bolts to the side bar, as y positions, and the bracket stub's
#: diameter. Two per side is what a CIK pod actually carries.
SIDEPOD_MOUNT_Y: tuple[float, ...] = (0.180, -0.200)
SIDEPOD_MOUNT_DIAMETER = 0.014

#: Where along the pod's section the mount stub picks up, as a fraction of the
#: sampled profile from the top edge. 0.5 is the outer bulge, which is the part
#: of the shell directly outboard of the bar.
SIDEPOD_MOUNT_PROFILE_T = 0.50

#: Centerline of the side bar the pods bolt to, right-hand side, copied from
#: `frame.py:_bumpers`. Duplicated rather than imported because a geometry module
#: may not read another module's objects (see `build.BuildContext`), and because
#: the mount stubs have to end on the *filleted* centerline rather than on the
#: control polyline. It belongs in params.py with the rest of the frame.
SIDE_BAR_PATH: tuple[tuple[float, float, float], ...] = (
    (0.335, 0.465, 0.065),
    (0.430, 0.300, 0.105),
    (0.445, 0.000, 0.110),
    (0.430, -0.330, 0.105),
    (0.320, -0.560, 0.065),
)

#: Half-width of the rear plastics. The rear bumper turns forward at x = ±0.310
#: on a 22 mm tube, so that corner occupies x 0.299..0.321 — and a panel that
#: reached it would have to wrap either outboard of the corner, which puts it
#: past the -0.915 length limit, or through it. It stops inboard instead, at
#: 13 mm clear of the tube, and the bumper's outer corners stay visible on both
#: sides. That is what a real rear protector looks like.
REAR_HALF_WIDTH = 0.286

#: The rear plastics' cross-section in (y, z), from the bottom front edge up over
#: the bumper and down behind it. Every point is constrained by the rear bumper
#: tube, whose rear straight is centered at y = -0.905, z = 0.140 on an 11 mm
#: radius:
#:
#:     the last point sits 8.0 mm clear of the tube surface, above and behind it
#:     the rearmost point is y = -0.911, inside the -0.915 length limit
#:
#: The panel deliberately does not wrap under the bumper: there is no room. See
#: the report note on `frame.py`'s rear bumper overrunning `length_overall`.
REAR_SECTION: tuple[tuple[float, float], ...] = (
    (-0.742, 0.118),
    (-0.790, 0.150),
    (-0.840, 0.176),
    (-0.884, 0.184),
    (-0.906, 0.172),
    (-0.911, 0.158),
)

#: How the rear panel's section closes down toward its outer ends, against
#: |x| / `REAR_HALF_WIDTH`: shifted forward, and flattened toward its own lowest
#: point. Without it the panel is an extruded ribbon with square ends.
#:
#: Forward-and-flatten rather than a scale about the section's centroid, which is
#: the obvious treatment and the wrong one: the section's centroid sits above and
#: ahead of its rear lip, so scaling toward it walks that lip straight down onto
#: the bumper tube — 150 triangles of it, measured.
REAR_END_SHIFT: tuple[tuple[float, float], ...] = (
    (0.00, 0.000),
    (0.55, 0.004),
    (0.82, 0.016),
    (1.00, 0.032),
)
REAR_END_FLATTEN: tuple[tuple[float, float], ...] = (
    (0.00, 1.000),
    (0.55, 0.985),
    (0.82, 0.930),
    (1.00, 0.860),
)


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("bodywork")
    material = context.material("bodywork_plastic")

    root = build.empty("bodywork_root", (0.0, 0.0, 0.0), collection, size=0.10)

    _nose_fairing(context, collection, material, root)
    _sidepods(context, collection, material, root)
    _rear_plastics(context, collection, material, root)


# --- curve and surface helpers ---------------------------------------------
#
# `build.py` covers tubes, lathes and boxes. A bodywork panel is none of those:
# it is a lofted shell with an open back. `cockpit.py` has the same problem and
# solved it the same way, but its helpers are private to that module, so these
# are written here rather than reached for across a file boundary.


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


def _catmull_rom(points: list[Vector], per_segment: int) -> list[Vector]:
    """Sample an open uniform Catmull-Rom spline through every control point.

    Through, not near. A panel's control points are the dimensions that matter —
    the apex, the top and bottom free edges, the widest station — so a curve that
    only approximated them would quietly stop honoring the tables above. End
    tangents are clamped.
    """
    count = len(points)
    if count < 3 or per_segment < 1:
        return list(points)

    def at(index: int) -> Vector:
        return points[min(max(index, 0), count - 1)]

    sampled: list[Vector] = []
    for segment in range(count - 1):
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
    sampled.append(points[-1])
    return sampled


def _span_steps(detail: build.Detail) -> int:
    """Number of lofted sections across a panel.

    Read off `detail.tube_segments` rather than fixed, because the module is
    built twice and issue #19's normal bake needs the two to be the same shape at
    two densities. One more than the tube count so the centerline of a panel
    spanning both sides lands on a section rather than between two, which is what
    keeps the nose fairing's spine a ridge instead of a flat.
    """
    return detail.tube_segments + 1


def _profile_steps(detail: build.Detail) -> int:
    """Samples per control-point segment around a panel's section."""
    return max(2, detail.tube_segments // 4)


def _grid_normals(grid: list[list[Vector]]) -> list[list[Vector]]:
    """Surface normal at every point of a lofted grid, by central difference.

    Computed rather than taken from the face normals because the offset surface
    has to be built before any face exists, and because a per-vertex normal is
    what keeps the shell's wall thickness even around a tight corner — a
    per-face offset opens gaps at the rim.
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
            row.append(
                normal.normalized() if normal.length > 1e-12 else Vector((0.0, 0.0, 1.0))
            )
        normals.append(row)
    return normals


def _offset_grid(grid: list[list[Vector]], thickness: float) -> list[list[Vector]]:
    """The grid pushed `thickness` toward its own cavity.

    The sign is decided from the geometry rather than passed in: a panel section
    is a C, so its centroid lies in the cavity, and the normals that point *away*
    from the centroid are the outside of the shell. Deciding it here means the
    three panels can each be authored in whatever winding reads most naturally in
    its own table without one of them silently ending up inside out.
    """
    normals = _grid_normals(grid)
    centroid = Vector((0.0, 0.0, 0.0))
    count = 0
    for row in grid:
        for point in row:
            centroid += point
            count += 1
    centroid /= float(count)

    outward = 0.0
    for u, row in enumerate(grid):
        for v, point in enumerate(row):
            outward += (point - centroid).dot(normals[u][v])
    sign = -1.0 if outward > 0.0 else 1.0

    return [
        [grid[u][v] + normals[u][v] * (thickness * sign) for v in range(len(grid[u]))]
        for u in range(len(grid))
    ]


def _loft_shell(
    bm: bmesh.types.BMesh, grid: list[list[Vector]], thickness: float
) -> None:
    """A lofted surface, its offset copy, and the band that closes the rim.

    The band is the visible payoff and the reason a panel is not modeled as a
    single surface: it is `thickness` of real geometry around every free edge, so
    the open back of a sidepod has a wall the eye can read and
    `build.bevel_object` has something to chamfer. Vertices are emitted row by
    row and the perimeter is walked in a fixed order, so the whole thing is a
    function of the grid alone.
    """
    rows = len(grid)
    columns = len(grid[0])
    inner_grid = _offset_grid(grid, thickness)

    outer = [[bm.verts.new(grid[u][v]) for v in range(columns)] for u in range(rows)]
    inner = [
        [bm.verts.new(inner_grid[u][v]) for v in range(columns)] for u in range(rows)
    ]

    for u in range(rows - 1):
        for v in range(columns - 1):
            bm.faces.new(
                (outer[u][v], outer[u][v + 1], outer[u + 1][v + 1], outer[u + 1][v])
            )
            bm.faces.new(
                (inner[u][v], inner[u + 1][v], inner[u + 1][v + 1], inner[u][v + 1])
            )

    perimeter: list[tuple[int, int]] = []
    perimeter.extend((0, v) for v in range(columns))
    perimeter.extend((u, columns - 1) for u in range(1, rows))
    perimeter.extend((rows - 1, v) for v in range(columns - 2, -1, -1))
    perimeter.extend((u, 0) for u in range(rows - 2, 0, -1))

    for index in range(len(perimeter)):
        u0, v0 = perimeter[index]
        u1, v1 = perimeter[(index + 1) % len(perimeter)]
        bm.faces.new((outer[u0][v0], outer[u1][v1], inner[u1][v1], inner[u0][v0]))

    # Closed manifold, so one recalculation gives every face the outward normal
    # and the band's winding never has to be reasoned about.
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))


def _point_at_y(path: list[Vector], y: float) -> Vector:
    """Where a monotonic-in-y polyline crosses a given y.

    Used to land a mounting stub on the *filleted* centerline of a frame tube
    rather than on its control polyline — near a bend those differ by most of
    `bend_radius`, which is enough to leave a bracket hanging in air.
    """
    for index in range(len(path) - 1):
        a, b = path[index], path[index + 1]
        if (a.y - y) * (b.y - y) <= 0.0 and abs(b.y - a.y) > 1e-9:
            return a.lerp(b, (y - a.y) / (b.y - a.y))
    return path[0] if abs(path[0].y - y) < abs(path[-1].y - y) else path[-1]


def _mount_stub(
    bm: bmesh.types.BMesh,
    detail: build.Detail,
    start: Vector,
    tube_center: Vector,
    tube_radius: float,
    diameter: float,
) -> None:
    """A short bracket from a panel's inner skin to the surface of a frame tube.

    Stops `MOUNT_STANDOFF` short of the tube rather than touching it. The real
    joint clamps the tube, but two faceted meshes that meet exactly register as
    an intersection in the BVH check, and a 1.5 mm gap is invisible at any camera
    distance the kart is ever seen from.
    """
    direction = tube_center - start
    if direction.length < 1e-6:
        return
    end = tube_center - direction.normalized() * (tube_radius + MOUNT_STANDOFF)
    build.sweep_tube(bm, [start, end], diameter * 0.5, detail.tube_segments)


# --- the nose fairing ------------------------------------------------------


def _nose_top_z(p: P.KartParams, distance: float) -> float:
    """Top edge of the fairing at |x| = `distance`, honoring `nose_height`.

    The spine's height is the parameter; the shoulder's is the hoop-clearance
    envelope. The correction between them fades over `NOSE_SPINE_FADE` so that a
    `--set nose_height=` sweep visibly moves the spine without walking the
    shoulder into the tube it was measured against.
    """
    correction = (NOSE_BOTTOM_Z[0][1] + p.nose_height) - NOSE_TOP_Z[0][1]
    fade = max(0.0, 1.0 - distance / NOSE_SPINE_FADE)
    return _table(NOSE_TOP_Z, distance) + correction * fade


def _nose_half_width(p: P.KartParams) -> float:
    """Half-width the fairing is actually built at. See `NOSE_HALF_WIDTH_LIMIT`."""
    return min(p.nose_width * 0.5, NOSE_HALF_WIDTH_LIMIT)


def _nose_section(p: P.KartParams, s: float, steps: int) -> list[Vector]:
    """One (y, z) section of the fairing at lateral fraction `s` in [-1, 1].

    Seven control points, sampled as a spline: rear top edge, crown, upper front,
    apex, lower front, rear bottom edge. The apex is the one with a hard
    constraint on it — it has to sit in the 75 mm gap between the two nose-hoop
    tiers — and everything else follows from the tables.
    """
    a = abs(s)
    x = s * _nose_half_width(p)

    front_y = p.nose_y + NOSE_DEPTH * 0.5
    back_y = p.nose_y - NOSE_DEPTH * 0.5

    # The three z tables are keyed on absolute |x| because the hoops that
    # constrain them are; the plan-view tables are keyed on the section fraction
    # because they are shape, not clearance.
    apex_y = front_y - _table(NOSE_APEX_SETBACK, a)
    apex_z = _table(NOSE_APEX_Z, abs(x))
    top_z = _nose_top_z(p, abs(x))
    bottom_z = _table(NOSE_BOTTOM_Z, abs(x))
    back_top_y = back_y + _table(NOSE_BACK_TOP_INSET, a)
    back_bottom_y = back_y + _table(NOSE_BACK_BOTTOM_INSET, a)

    # Fore-aft placement of the crown and of the two front curves is expressed as
    # a fraction of the section's own depth rather than in meters. The wing
    # sections are barely half the depth of the spine, and fixed offsets there
    # put the crown behind the apex — which a Catmull-Rom answers with a loop
    # rather than with an error.
    depth_top = apex_y - back_top_y
    depth_bottom = apex_y - back_bottom_y

    controls = [
        Vector((x, back_top_y, top_z - 0.010)),
        Vector((x, back_top_y + depth_top * 0.30, top_z)),
        Vector((x, apex_y - depth_top * 0.32, top_z - 0.004)),
        Vector(
            (
                x,
                apex_y - min(0.020, depth_top * 0.16),
                apex_z + (top_z - apex_z) * 0.45,
            )
        ),
        Vector((x, apex_y, apex_z)),
        Vector(
            (
                x,
                apex_y - min(0.028, depth_bottom * 0.22),
                bottom_z + (apex_z - bottom_z) * 0.35,
            )
        ),
        Vector((x, back_bottom_y, bottom_z)),
    ]
    return _catmull_rom(controls, steps)


def _nose_fairing(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The CIK front fairing, hung off the lower nose hoop on its two pins.

    Built whole rather than as a mirrored half. The spine runs down the
    centerline and is the first thing the front camera and every replay still
    looks at; a mirror seam there would be a crease in the one surface that has
    to be continuous.
    """
    p = context.params
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    grid = [
        _nose_section(p, -1.0 + 2.0 * index / (spans - 1), steps)
        for index in range(spans)
    ]

    bm = bmesh.new()
    _loft_shell(bm, grid, PANEL_THICKNESS)
    _nose_pins(context, bm, grid, steps)

    obj = build.object_from_bmesh(
        "bodywork_nose_fairing", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(obj, detail)

    # The pivot is the mount pin axis, which is what the fairing really rotates
    # about when contact displaces it — the CIK mounting kit's whole point. A
    # pivot at the panel's centroid would displace it in a way no real fairing
    # moves.
    pivot = build.empty(
        "nose_fairing_pivot",
        (0.0, _lower_hoop_y(p), _lower_hoop_z(p)),
        collection,
        parent=root,
        size=0.06,
    )
    context.publish("nose_fairing_pivot", pivot)
    build.set_parent(obj, pivot)


def _lower_hoop_y(p: P.KartParams) -> float:
    """Forward tube center of the nose hoop, as `frame.py:_bumpers` derives it."""
    return p.length_overall * 0.5 - p.tube_bumper * 0.5


def _lower_hoop_z(p: P.KartParams) -> float:
    """Height of the lower nose hoop tier — `rail_z` + 10 mm in `frame.py`."""
    return P.rail_z(p) + 0.010


def _nose_pins(
    context: build.BuildContext,
    bm: bmesh.types.BMesh,
    grid: list[list[Vector]],
    steps: int,
) -> None:
    """The two CIK mounting pins, into the same mesh as the panel they carry.

    Same object deliberately: the pins move with the fairing when it is
    displaced, and a fairing whose mounts stayed behind would read as broken
    rather than as knocked askew.
    """
    p = context.params
    half_width = _nose_half_width(p)
    columns = len(grid[0])
    profile_index = min(columns - 1, int(round(NOSE_PIN_PROFILE_T * (columns - 1))))
    radius = p.tube_bumper * 0.5

    for side in (-1.0, 1.0):
        s = side * NOSE_PIN_X / half_width
        section = _nose_section(p, s, steps)
        skin = section[profile_index]
        # Start inside the shell rather than on it, so the pin is visibly rooted
        # in the panel instead of tangent to it.
        start = Vector((side * NOSE_PIN_X, skin.y - 0.006, skin.z + 0.010))
        _mount_stub(
            bm,
            context.detail,
            start,
            Vector((side * NOSE_PIN_X, _lower_hoop_y(p), _lower_hoop_z(p))),
            radius,
            NOSE_PIN_DIAMETER,
        )


# --- the sidepods ----------------------------------------------------------


def _sidepod_section(p: P.KartParams, t: float, steps: int) -> list[Vector]:
    """One (x, z) C-section of the right sidepod at normalized station `t`.

    Seven control points from the top free edge, outboard and down around the
    flank, to the bottom free edge. The C is the whole point: the side bar runs
    *inside* it, so the pod wraps the bar the way a real one does rather than
    hanging off its outboard face like a plate.
    """
    y = SIDEPOD_FRONT_Y - t * p.sidepod_length

    out_x = p.sidepod_x * _table(SIDEPOD_OUT_FRACTION, t)
    top_x = _table(SIDEPOD_TOP_X, t)
    bottom_x = _table(SIDEPOD_BOTTOM_X, t)
    bottom_z = _table(SIDEPOD_BOTTOM_Z, t)
    top_z = bottom_z + p.sidepod_height * _table(SIDEPOD_HEIGHT_FRACTION, t)
    bulge_top_z = _table(SIDEPOD_BULGE_TOP_Z, t)
    bulge_bottom_z = _table(SIDEPOD_BULGE_BOTTOM_Z, t)

    controls = [
        Vector((top_x, y, top_z)),
        Vector((top_x + 0.038, y, top_z + 0.003)),
        Vector((out_x - 0.014, y, top_z - 0.032)),
        Vector((out_x, y, bulge_top_z)),
        Vector((out_x - 0.002, y, bulge_bottom_z)),
        Vector((bottom_x + 0.024, y, bottom_z + 0.007)),
        Vector((bottom_x, y, bottom_z)),
    ]
    return _catmull_rom(controls, steps)


def _sidepods(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Right pod plus its mirror. The widest bodywork on the kart.

    Deep, hollow-backed and tapered at both ends, which between them are what
    stop a pod reading as a slab: the open inboard face and the 3 mm rim are
    visible from the chase camera on every corner entry.
    """
    p = context.params
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    grid = [_sidepod_section(p, index / (spans - 1), steps) for index in range(spans)]

    bm = bmesh.new()
    _loft_shell(bm, grid, PANEL_THICKNESS)
    _sidepod_mounts(context, bm, steps)

    right = build.object_from_bmesh(
        "bodywork_sidepod_r", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(right, detail)
    build.set_parent(right, root)

    # `build.mirror_x` copies the source's material slots, so the material is not
    # appended again here.
    left = build.mirror_x(right, "bodywork_sidepod_l", collection)
    build.set_parent(left, root)


def _sidepod_mounts(
    context: build.BuildContext, bm: bmesh.types.BMesh, steps: int
) -> None:
    """Two brackets per pod, onto the side bar `frame.py` already builds.

    Landed on the filleted centerline rather than on the control polyline: the
    bar's bends eat up to `bend_radius`, and a bracket aimed at the polyline near
    one of them ends in air.
    """
    p = context.params
    path = build.fillet(
        SIDE_BAR_PATH,
        p.bend_radius,
        context.detail.bend_segments,
    )
    radius = p.tube_bumper * 0.5

    for mount_y in SIDEPOD_MOUNT_Y:
        t = (SIDEPOD_FRONT_Y - mount_y) / p.sidepod_length
        section = _sidepod_section(p, t, steps)
        index = min(
            len(section) - 1, int(round(SIDEPOD_MOUNT_PROFILE_T * (len(section) - 1)))
        )
        skin = section[index]
        start = Vector((skin.x - 0.006, mount_y, skin.z))
        _mount_stub(
            bm,
            context.detail,
            start,
            _point_at_y(path, mount_y),
            radius,
            SIDEPOD_MOUNT_DIAMETER,
        )


# --- the rear plastics -----------------------------------------------------


def _rear_section(s: float, steps: int) -> list[Vector]:
    """One (y, z) section of the rear panel at lateral fraction `s` in [-1, 1].

    Shifted forward and flattened toward the section's own lowest point as it
    goes outboard, so the panel closes down at both sides instead of stopping as
    a square-cut ribbon — and so its rear lip moves *away* from the bumper tube
    rather than onto it.
    """
    a = abs(s)
    x = s * REAR_HALF_WIDTH
    shift = _table(REAR_END_SHIFT, a)
    flatten = _table(REAR_END_FLATTEN, a)
    base_z = min(point[1] for point in REAR_SECTION)

    return _catmull_rom(
        [
            Vector((x, y + shift, base_z + (z - base_z) * flatten))
            for y, z in REAR_SECTION
        ],
        steps,
    )


def _rear_plastics(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The rear protector over the bumper hoop. Much smaller than a sidepod.

    Built whole for the same reason as the nose: it spans the centerline and a
    mirrored half would seam it. It sits over and behind the bumper's rear
    straight without wrapping under it, because there is no room to — the
    bumper's own outer surface is already at the rear length limit.
    """
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    grid = [
        _rear_section(-1.0 + 2.0 * index / (spans - 1), steps) for index in range(spans)
    ]

    bm = bmesh.new()
    _loft_shell(bm, grid, PANEL_THICKNESS)
    obj = build.object_from_bmesh(
        "bodywork_rear_panel", bm, collection, material=material, shade_smooth=True
    )
    build.bevel_object(obj, detail)
    build.set_parent(obj, root)
