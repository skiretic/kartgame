"""The track's visual geometry: asphalt, paint, kerbs, verge, run-off, barriers.

One function per surface, each taking the loaded `Track` and returning
`(vertices, faces, uvs, uv2s)` in **Blender's** frame. `gentrack.py` turns those
into meshes.

## Two coordinate conversions, both here, both once

`track.json` is in Godot's frame - ADR-0046: the runtime is the consumer that must
not have a transform bug, so the runtime does not transform. Blender is not, so
this file converts on the way in:

    blender = (x_godot, -z_godot, y_godot)

and `export_yup=True` maps Blender (x, y, z) to glTF (x, z, -y), which sends it
straight back to where it started. The two conversions are inverse by
construction, which is the only reason it is safe to have two of them: getting
this wrong is invisible until the kart drives backwards, and this project has
already spent a milestone on that exact class of bug.

## UVs are in meters, and UV2 is a snake

`u` runs across the road and `v` along it, both in meters, which is what makes
Godot's `uv1_scale = extent / 4.00` the right call at the other end and gets the
§5 texel density without this file knowing what the standard is.

UV2 is the lightmap channel and has a different job: it must fit inside the unit
square and must not overlap itself. A ribbon unwraps trivially - `v` along, `u`
across - but a 1,375 m by 14 m strip mapped straight into [0,1]^2 has a 98:1
aspect ratio, so every lightmap texel is a 100:1 sliver. The lap is cut into rows
and stacked instead, which is why `snake_uv2` exists and why it takes the row
count rather than assuming one.
"""

from __future__ import annotations

import math

#: The white lines down both edges, meters wide, and how far above the asphalt
#: they sit. Part I §7.2 caps the line at 120 mm; 100 mm is inside it. The lift is
#: `track_ribbon.gd`'s: coplanar paint z-fights across the whole track, which looks
#: like a rendering bug and is one.
EDGE_LINE_WIDTH = 0.10
PAINT_LIFT = 0.004

#: The verge, meters each side. Part I §7.5's minimum, and it *continues the
#: track's transversal profile with no negative slope* - so it is built by
#: extending the road's own cross-section outward rather than as a flat apron at
#: centerline height, which would put a lip at the white line.
VERGE_WIDTH = 1.80

#: How far a kerb reaches below the surface, meters. It is not a slab lying on the
#: grass, it is the top of something buried.
KERB_DEPTH = 0.15

#: Barrier height, meters, measured up from the barrier's seated base. **estimated**
#: -- Part I §8 grades barriers by *type* and specifies their impact behaviour, not
#: their height. 1.0 m is chosen to be above a kart's centre of mass (0.23 m) by
#: enough that a glancing hit is redirected rather than launched.
BARRIER_HEIGHT = 1.0

#: How far under the road's own elevation the barrier's base sits, meters.
#:
#: This is `TrackTerrain.SHOULDER_DROP` and it has to be the same number, which is
#: why it is named rather than folded into an offset. `src/track/kart_track.cpp`
#: carries a third copy for the collider: three files, one constant, no way to
#: share it across GDScript, C++ and Python. `circuit.sh --case=agree` is what
#: proves the three agree.
BARRIER_SHOULDER_DROP = 0.25

#: How far the barrier reaches **below** its seated base, meters. Issue #244.
#:
#: A barrier whose foot is a curve and whose ground is a smoothed height field
#: cannot be flush: `track_terrain.gd`'s own docstring records an irreducible
#: **2.343 m** pinned-to-pinned step where the lap passes close to itself at two
#: different heights, and nothing the barrier does changes that. So the wall is
#: driven into the ground rather than balanced on it, far enough that the residual
#: cannot open a gap under it. 3.0 m is that 2.343 plus `BARRIER_SHOULDER_DROP`
#: plus margin; **measured** on Valdirone the worst foot still clears the terrain
#: by 0.60 m, and `circuit.sh --case=agree` fails the moment that margin reaches
#: zero rather than leaving the constant to be trusted.
BARRIER_SKIRT = 3.0

#: How often the manifest samples the barrier for `--case=agree`, in quads. The
#: barrier is 681 quads on Valdirone and a committed sidecar wants tens of rows,
#: not hundreds; every 7th quad is 98 rows and lands on all eight corners.
BARRIER_MANIFEST_STRIDE = 7


def to_blender(point: tuple[float, float, float]) -> tuple[float, float, float]:
    """Godot (x, y, z) -> Blender (x, -z, y). See the module docstring."""
    return (point[0], -point[2], point[1])


class Strip:
    """An accumulating triangle soup with two UV channels.

    A soup and not an indexed mesh, for `track_ribbon.gd`'s reason stated one
    milestone earlier: the same vertex list serves twice, once as geometry and
    once as a collider, and a few hundred shared vertices is not worth giving that
    up. Here only the first half applies - the collider is built in C++ from the
    same file - but the shapes stay the same so the two are comparable.
    """

    def __init__(self, name: str, material: str):
        self.name = name
        self.material = material
        self.vertices: list[tuple[float, float, float]] = []
        self.faces: list[tuple[int, int, int]] = []
        self.uvs: list[tuple[float, float]] = []
        self.uv2s: list[tuple[float, float]] = []

    def quad(self, corners, uvs, uv2s) -> None:
        base = len(self.vertices)
        for corner in corners:
            self.vertices.append(to_blender(corner))
        self.faces.append((base, base + 1, base + 2))
        self.faces.append((base, base + 2, base + 3))
        self.uvs.extend(uvs)
        self.uv2s.extend(uv2s)

    def triangles(self) -> int:
        return len(self.faces)

    def is_empty(self) -> bool:
        return not self.faces


def snake_uv2(distance: float, lateral: float, half_width: float,
              total: float, rows: int, gutter: float = 0.01) -> tuple[float, float]:
    """Lightmap coordinates: the lap cut into `rows` and stacked in the unit square.

    Without this the strip is 98:1 and every lightmap texel is a sliver. With ten
    rows a 1,375 m lap becomes ten 137 m pieces each about 10:1 in a cell that is
    itself 1 by 0.1, which is square-ish per texel.

    Both channels are clamped inside the gutter rather than allowed to touch the
    cell edge, because a lightmapper dilates and two adjacent rows that share an
    edge bleed into each other - the visible symptom is a horizontal seam of the
    wrong brightness every 137 m, which reads as a lighting bug.
    """
    row = min(rows - 1, int(distance / total * rows))
    along = (distance / total * rows) - row
    across = 0.5 + 0.5 * (lateral / half_width if half_width > 0.0 else 0.0)
    cell = 1.0 / rows
    v = (row + gutter + along * (1.0 - 2.0 * gutter)) * cell
    u = gutter + max(0.0, min(1.0, across)) * (1.0 - 2.0 * gutter)
    return (u, v)


def _uv_pair(near, far, near_u, far_u):
    return [
        (near_u, near.distance_m),
        (far_u, near.distance_m),
        (far_u, far.distance_m),
        (near_u, far.distance_m),
    ]


def build_road(track, line, rows: int) -> Strip:
    """The asphalt: three columns, because a crown is a roof.

    A two-column quad would join the two edges with a flat plane through the
    middle, which is the one shape a drainage camber is not. Three columns
    reproduce the cross-section exactly, because it is piecewise linear in the
    lateral offset.
    """
    strip = Strip("Asphalt", "asphalt")
    for index in range(len(line) - 1):
        near = line[index]
        far = line[index + 1]
        near_half = near.width_m * 0.5
        far_half = far.width_m * 0.5
        for column in (-1.0, 0.0):
            outer = column + 1.0
            strip.quad(
                (
                    near.surface_point(column * near_half),
                    near.surface_point(outer * near_half),
                    far.surface_point(outer * far_half),
                    far.surface_point(column * far_half),
                ),
                _uv_pair(near, far, column * near_half, outer * near_half),
                [
                    snake_uv2(near.distance_m, column * near_half, near_half, track.length_m, rows),
                    snake_uv2(near.distance_m, outer * near_half, near_half, track.length_m, rows),
                    snake_uv2(far.distance_m, outer * far_half, far_half, track.length_m, rows),
                    snake_uv2(far.distance_m, column * far_half, far_half, track.length_m, rows),
                ],
            )
    return strip


def build_edge_lines(track, line, rows: int) -> Strip:
    """The two white lines, inboard of the asphalt edge rather than overhanging it.

    Inboard so the painted line is the last thing with grip on it and a wheel on
    the white is still a wheel on the track - which is also the FIA's own
    definition of the limit, all four wheels *past* the line.
    """
    strip = Strip("EdgeLines", "paint")
    for index in range(len(line) - 1):
        near = line[index]
        far = line[index + 1]
        for hand in (-1.0, 1.0):
            near_outer = hand * near.width_m * 0.5
            far_outer = hand * far.width_m * 0.5
            near_inner = near_outer - hand * EDGE_LINE_WIDTH
            far_inner = far_outer - hand * EDGE_LINE_WIDTH
            strip.quad(
                (
                    near.surface_point(near_inner, PAINT_LIFT),
                    near.surface_point(near_outer, PAINT_LIFT),
                    far.surface_point(far_outer, PAINT_LIFT),
                    far.surface_point(far_inner, PAINT_LIFT),
                ),
                _uv_pair(near, far, 0.0, EDGE_LINE_WIDTH),
                [
                    snake_uv2(near.distance_m, near_inner, near.width_m * 0.5, track.length_m, rows),
                    snake_uv2(near.distance_m, near_outer, near.width_m * 0.5, track.length_m, rows),
                    snake_uv2(far.distance_m, far_outer, far.width_m * 0.5, track.length_m, rows),
                    snake_uv2(far.distance_m, far_inner, far.width_m * 0.5, track.length_m, rows),
                ],
            )
    return strip


def build_start_line(track, rows: int) -> Strip:
    """The start line and the grid boxes - the only other paint Part I §12 allows.

    §12: *"Any paint on the circuit surfacing, other than that which delimits the
    edges of the track and determines the starting grid, is forbidden for safety
    reasons."* So corner numbers and braking boards go on free-standing structures
    behind the verge, and this function is the complete list of what may be drawn
    on the asphalt.
    """
    strip = Strip("StartLine", "paint")
    furniture = track.raw.get("furniture", {})
    start = furniture.get("start_line", {"distance_m": 0.0, "width_m": 0.30})
    width = float(start.get("width_m", 0.30))
    centre = track.sample(float(start.get("distance_m", 0.0)))
    half = centre.width_m * 0.5
    back = track.sample(track.wrap(centre.distance_m - width * 0.5))
    front = track.sample(track.wrap(centre.distance_m + width * 0.5))
    strip.quad(
        (
            back.surface_point(-half, PAINT_LIFT),
            back.surface_point(half, PAINT_LIFT),
            front.surface_point(half, PAINT_LIFT),
            front.surface_point(-half, PAINT_LIFT),
        ),
        [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)],
        [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)],
    )

    # One box per grid slot, in the forward layout only: the reverse layout's grid
    # is on the same asphalt facing the other way, and painting both would put
    # sixteen boxes on a road that seats eight.
    forward = next((l for l in track.layouts if l.get("direction") == "forward"), None)
    if forward is None:
        return strip
    for slot in forward.get("grid", {}).get("positions", []):
        station = float(slot["distance_m"])
        lateral = float(slot["lateral_m"])
        box_back = track.sample(track.wrap(station - 0.9))
        box_front = track.sample(track.wrap(station + 0.9))
        for edge in (-0.75, 0.75):
            strip.quad(
                (
                    box_back.surface_point(lateral + edge - 0.05, PAINT_LIFT),
                    box_back.surface_point(lateral + edge + 0.05, PAINT_LIFT),
                    box_front.surface_point(lateral + edge + 0.05, PAINT_LIFT),
                    box_front.surface_point(lateral + edge - 0.05, PAINT_LIFT),
                ),
                [(0.0, 0.0), (0.1, 0.0), (0.1, 1.8), (0.0, 1.8)],
                [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)],
            )
    return strip


def build_kerbs(track, line, rows: int) -> Strip:
    """Every declared kerb span: a top face and the vertical face a wheel climbs.

    That inner face is the whole reason this is a kerb rather than a painted strip,
    and it is issue #139's target - "a wheel driven at it at 100+ km/h does not
    pass through it". Valdirone's T2 Lama is the reference: the only kerb on the
    circuit struck above 120 km/h, once a lap, 21 times in a 30 km Final.
    """
    strip = Strip("Kerbs", "kerb")
    for span in track.surfaces:
        if span.get("type") != "curb":
            continue
        side = span.get("side", "left")
        hands = (-1.0, 1.0) if side == "both" else ((-1.0,) if side == "left" else (1.0,))
        width = float(span.get("width_m", 1.0))
        height = float(span.get("height_m", 0.030))
        for hand in hands:
            for index in range(len(line) - 1):
                near = line[index]
                far = line[index + 1]
                if not track.spans_covering(span, near, far):
                    continue
                near_lift = height * track.ramp(span, near.distance_m)
                far_lift = height * track.ramp(span, far.distance_m)
                near_edge = hand * near.width_m * 0.5
                far_edge = hand * far.width_m * 0.5
                near_out = near_edge + hand * width
                far_out = far_edge + hand * width
                strip.quad(
                    (
                        near.surface_point(near_edge, near_lift),
                        near.surface_point(near_out, near_lift),
                        far.surface_point(far_out, far_lift),
                        far.surface_point(far_edge, far_lift),
                    ),
                    _uv_pair(near, far, 0.0, width),
                    [
                        snake_uv2(near.distance_m, near_edge, near.width_m, track.length_m, rows),
                        snake_uv2(near.distance_m, near_out, near.width_m, track.length_m, rows),
                        snake_uv2(far.distance_m, far_out, far.width_m, track.length_m, rows),
                        snake_uv2(far.distance_m, far_edge, far.width_m, track.length_m, rows),
                    ],
                )
                strip.quad(
                    (
                        near.surface_point(near_edge, -KERB_DEPTH),
                        near.surface_point(near_edge, near_lift),
                        far.surface_point(far_edge, far_lift),
                        far.surface_point(far_edge, -KERB_DEPTH),
                    ),
                    _uv_pair(near, far, 0.0, height),
                    [
                        snake_uv2(near.distance_m, near_edge, near.width_m, track.length_m, rows),
                        snake_uv2(near.distance_m, near_edge, near.width_m, track.length_m, rows),
                        snake_uv2(far.distance_m, far_edge, far.width_m, track.length_m, rows),
                        snake_uv2(far.distance_m, far_edge, far.width_m, track.length_m, rows),
                    ],
                )
    return strip


def build_verge(track, line, rows: int) -> Strip:
    """1.80 m of verge either side, the whole way round. Part I §7.5, and it is a
    per-segment requirement rather than corner prose - which is why it is built
    from the polyline rather than from the corner list.

    It *continues the track's transversal profile with no negative slope*, so the
    outer edge sits at the road's own cross-section extended, not at centerline
    height. Building it flat would put a lip at the white line exactly where the
    rule forbids one.
    """
    strip = Strip("Verge", "verge")
    for index in range(len(line) - 1):
        near = line[index]
        far = line[index + 1]
        for hand in (-1.0, 1.0):
            near_edge = hand * near.width_m * 0.5
            far_edge = hand * far.width_m * 0.5
            near_out = near_edge + hand * VERGE_WIDTH
            far_out = far_edge + hand * VERGE_WIDTH
            strip.quad(
                (
                    near.surface_point(near_edge, -0.002),
                    near.surface_point(near_out, -0.002),
                    far.surface_point(far_out, -0.002),
                    far.surface_point(far_edge, -0.002),
                ),
                _uv_pair(near, far, 0.0, VERGE_WIDTH),
                [
                    snake_uv2(near.distance_m, near_edge, near.width_m, track.length_m, rows),
                    snake_uv2(near.distance_m, near_out, near.width_m, track.length_m, rows),
                    snake_uv2(far.distance_m, far_out, far.width_m, track.length_m, rows),
                    snake_uv2(far.distance_m, far_edge, far.width_m, track.length_m, rows),
                ],
            )
    return strip


def barrier_base(frame, lateral: float) -> tuple[float, float, float]:
    """Where a barrier stands, in Godot's frame. Issue #244.

    **Not** `frame.surface_point(lateral)`, and that difference is the whole bug.
    `surface_point` continues the road's crown and bank outward without limit:

        y(u) = elevation - (crown/100)|u| - (bank/100)u

    which is right for the asphalt, right for the verge, right for the apron - and
    wrong for anything standing at the outer edge of a 42 m run-off, because at
    that distance a 5% bank is 2.09 m of extrapolation and the ground out there is
    not the road's plane. It is `TrackTerrain`'s height field, which inside the
    circuit's corridor is

        h(station) = elevation(station) - SHOULDER_DROP

    with no lateral term at all. Measured before this changed: 344 of Valdirone's
    681 barrier quads had a gap of more than 0.60 m under them, worst 3.51 m, and
    a kart drove under the wall. Seated this way the mean disagreement against the
    terrain falls from 1.309 m to 0.096 m, and `BARRIER_SKIRT` covers what is
    left - which is not a tolerance to be tightened but the height field's own
    ambiguity where the lap passes over itself.

    The x and z are unchanged: the cross-section never moved them, only y.
    """
    rx, rz = frame.right()
    return (
        frame.x + rx * lateral,
        frame.elevation_m - BARRIER_SHOULDER_DROP,
        frame.z + rz * lateral,
    )


def _runoff_window(corner) -> tuple[float, float]:
    # The run-off covers the corner plus a lead-in and a lead-out: a kart leaves
    # the road under braking, before the turn-in, more often than it leaves it at
    # the apex. 30 m is about a second at the speeds these corners are approached
    # at, and it is this file's figure rather than the design's.
    return float(corner["from_m"]) - 30.0, float(corner["to_m"]) + 30.0


def build_runoff(track, line, rows: int) -> tuple[Strip, Strip, Strip]:
    """Apron, gravel bed and barrier, per corner, on the side the design names.

    Only where a corner declares one. This is deliberately not a barrier ring
    round the whole circuit: the design sizes run-off per corner from an approach
    speed, and two of Valdirone's eight are sized by the *reverse* layout - T8
    Uscita is approached at 124.3 km/h forward and 142.2 reversed, 1.31x the
    kinetic energy - which is the concrete case for run-off being a per-layout
    figure rather than shared furniture. The scene's own ground slab is what
    catches everything else.
    """
    apron = Strip("RunoffApron", "asphalt")
    gravel = Strip("Gravel", "gravel")
    barrier = Strip("Barriers", "barrier")

    for corner in track.corners:
        runoff = corner.get("runoff")
        if not runoff or float(runoff.get("apron_m", 0.0)) <= 0.0:
            continue
        side = runoff.get("side", "left")
        hands = (-1.0, 1.0) if side == "both" else ((-1.0,) if side == "left" else (1.0,))
        apron_m = float(runoff["apron_m"])
        outfield_m = float(runoff.get("outfield_m", 0.0))
        is_gravel = runoff.get("outfield") == "gravel"
        start, end = _runoff_window(corner)
        for hand in hands:
            for index in range(len(line) - 1):
                near = line[index]
                far = line[index + 1]
                if far.distance_m < start or near.distance_m > end:
                    continue
                # Outboard of the **verge**, not of the white line. Part I §7.5
                # orders them: track, then 1.80 m of verge, then the run-off area,
                # which "must grade to the verge without a negative slope". Built
                # from the road edge instead, the apron and the verge occupied the
                # same 1.80 m band - the verge vanished under the apron in every
                # render, and worse, the collider had two coplanar faces there,
                # which is the exact condition that makes a suspension raycast's
                # answer arbitrary along a whole boundary.
                near_edge = hand * (near.width_m * 0.5 + VERGE_WIDTH)
                far_edge = hand * (far.width_m * 0.5 + VERGE_WIDTH)
                apron.quad(
                    (
                        near.surface_point(near_edge),
                        near.surface_point(near_edge + hand * apron_m),
                        far.surface_point(far_edge + hand * apron_m),
                        far.surface_point(far_edge),
                    ),
                    _uv_pair(near, far, 0.0, apron_m),
                    [
                        snake_uv2(near.distance_m, near_edge, near.width_m, track.length_m, rows),
                        snake_uv2(near.distance_m, near_edge + hand * apron_m, near.width_m, track.length_m, rows),
                        snake_uv2(far.distance_m, far_edge + hand * apron_m, far.width_m, track.length_m, rows),
                        snake_uv2(far.distance_m, far_edge, far.width_m, track.length_m, rows),
                    ],
                )
                if is_gravel and outfield_m > 0.0:
                    outer = apron_m + outfield_m
                    gravel.quad(
                        (
                            near.surface_point(near_edge + hand * apron_m),
                            near.surface_point(near_edge + hand * outer),
                            far.surface_point(far_edge + hand * outer),
                            far.surface_point(far_edge + hand * apron_m),
                        ),
                        _uv_pair(near, far, 0.0, outfield_m),
                        [
                            snake_uv2(near.distance_m, near_edge + hand * apron_m, near.width_m, track.length_m, rows),
                            snake_uv2(near.distance_m, near_edge + hand * outer, near.width_m, track.length_m, rows),
                            snake_uv2(far.distance_m, far_edge + hand * outer, far.width_m, track.length_m, rows),
                            snake_uv2(far.distance_m, far_edge + hand * apron_m, far.width_m, track.length_m, rows),
                        ],
                    )
                limit = apron_m + outfield_m
                # Seated on the terrain rather than on the road's extrapolated
                # cross-section - issue #244, and `barrier_base` carries the whole
                # argument. The foot is driven `BARRIER_SKIRT` into the ground so
                # the height field's own residual cannot open a gap under it, and
                # the head is `BARRIER_HEIGHT` above the *base*, not above the
                # foot, so the wall the driver sees is still 1.0 m tall.
                base_near = barrier_base(near, near_edge + hand * limit)
                base_far = barrier_base(far, far_edge + hand * limit)
                foot_near = (base_near[0], base_near[1] - BARRIER_SKIRT, base_near[2])
                foot_far = (base_far[0], base_far[1] - BARRIER_SKIRT, base_far[2])
                head_near = (base_near[0], base_near[1] + BARRIER_HEIGHT, base_near[2])
                head_far = (base_far[0], base_far[1] + BARRIER_HEIGHT, base_far[2])
                barrier.quad(
                    (foot_near, head_near, head_far, foot_far),
                    [
                        (0.0, near.distance_m),
                        (BARRIER_HEIGHT, near.distance_m),
                        (BARRIER_HEIGHT, far.distance_m),
                        (0.0, far.distance_m),
                    ],
                    [
                        snake_uv2(near.distance_m, -1.0, 1.0, track.length_m, rows),
                        snake_uv2(near.distance_m, 1.0, 1.0, track.length_m, rows),
                        snake_uv2(far.distance_m, 1.0, 1.0, track.length_m, rows),
                        snake_uv2(far.distance_m, -1.0, 1.0, track.length_m, rows),
                    ],
                )
    return apron, gravel, barrier


def barrier_rows(barrier: Strip, stride: int = BARRIER_MANIFEST_STRIDE) -> list[dict]:
    """The manifest's barrier cross-check rows, read off the built strip.

    Read off the strip and **not** rebuilt from the track, deliberately. A second
    walk of the corner list would be a third implementation that could agree with
    the schema while the mesh disagreed with both - which is precisely the failure
    `--case=agree` exists to catch, reintroduced inside the check itself. Quad `i`
    owns vertices `4i .. 4i+3` in the order `(foot_near, head_near, head_far,
    foot_far)`, so the rows are the mesh, sampled.

    Back in Godot's frame: `to_blender` is `(x, -z, y)`, so its inverse is
    `(bx, bz, -by)`. The two conversions are inverse by construction and this is
    the third place that matters - the module docstring's argument applies.
    """
    out = []
    quads = len(barrier.vertices) // 4
    for index in range(0, quads, max(1, stride)):
        base = index * 4
        foot = barrier.vertices[base]
        head = barrier.vertices[base + 1]
        out.append(
            {
                "distance_m": round(barrier.uvs[base][1], 6),
                "foot": [round(v, 6) for v in (foot[0], foot[2], -foot[1])],
                "head": [round(v, 6) for v in (head[0], head[2], -head[1])],
            }
        )
    return out


def build_pit(track, rows: int, max_spacing: float) -> Strip:
    """The pit lane: one parallel band shared by both layouts, plus one gore each.

    ## Why it is sampled on its own stations rather than off the shared polyline

    The polyline is subdivided to a chord tolerance, so its samples land wherever
    the sagitta rule puts them. A gore is 7.92 m long; snapped to that grid its tip
    would start most of a meter past its own junction, which draws a wedge of
    asphalt beginning in the middle of the verge. Stepping from the junction to the
    outboard station puts both ends exactly where `docs/TRACK_SCHEMA.md` says, here
    and in `src/track/kart_track.cpp`, which is what makes `--case=agree` a
    measurement rather than a coincidence.

    ## Why the gore is a wedge and not a ribbon

    A stub is the asphalt **between the white line and the pit lane's inner edge** -
    zero wide at the junction, exactly `separation_m` wide where it meets the lane.
    Laid instead as a 3.5 m ribbon it would occupy the same band as the lane for the
    length of the taper, and two coplanar collider faces along a whole boundary are
    what makes a suspension raycast's answer arbitrary. Gore inboard, lane outboard,
    and they cannot overlap because the gore's offset never exceeds the lane's.
    """
    strip = Strip("PitLane", "asphalt")
    lane = track.pit_lane
    if not lane:
        return strip
    hand = -1.0 if lane["side"] == "left" else 1.0
    separation = float(lane["separation_m"])
    width = float(lane["width_m"])
    from_m = float(lane["from_m"])

    run = (float(lane["to_m"]) - from_m) % track.length_m
    steps = max(1, int(math.ceil(run / max_spacing)))
    for step in range(steps):
        near = track.sample(from_m + run * step / steps)
        far = track.sample(from_m + run * (step + 1) / steps)
        near_in = hand * (near.width_m * 0.5 + separation)
        far_in = hand * (far.width_m * 0.5 + separation)
        strip.quad(
            (
                near.surface_point(near_in),
                near.surface_point(near_in + hand * width),
                far.surface_point(far_in + hand * width),
                far.surface_point(far_in),
            ),
            _uv_pair(near, far, 0.0, width),
            [
                snake_uv2(near.distance_m, near_in, near.width_m, track.length_m, rows),
                snake_uv2(near.distance_m, near_in + hand * width, near.width_m, track.length_m, rows),
                snake_uv2(far.distance_m, far_in + hand * width, far.width_m, track.length_m, rows),
                snake_uv2(far.distance_m, far_in, far.width_m, track.length_m, rows),
            ],
        )

    for stub in track.pit_stubs():
        reach = track.signed_gap(stub["junction_m"], stub["outboard_m"])
        gore_steps = max(1, int(math.ceil(abs(reach) / max_spacing)))
        gore_hand = stub["hand"]
        for step in range(gore_steps):
            near_t = step / gore_steps
            far_t = (step + 1) / gore_steps
            near = track.sample(stub["junction_m"] + reach * near_t)
            far = track.sample(stub["junction_m"] + reach * far_t)
            near_edge = gore_hand * near.width_m * 0.5
            far_edge = gore_hand * far.width_m * 0.5
            near_out = near_edge + gore_hand * separation * near_t
            far_out = far_edge + gore_hand * separation * far_t
            strip.quad(
                (
                    near.surface_point(near_edge),
                    near.surface_point(near_out),
                    far.surface_point(far_out),
                    far.surface_point(far_edge),
                ),
                _uv_pair(near, far, 0.0, separation),
                [
                    snake_uv2(near.distance_m, near_edge, near.width_m, track.length_m, rows),
                    snake_uv2(near.distance_m, near_out, near.width_m, track.length_m, rows),
                    snake_uv2(far.distance_m, far_out, far.width_m, track.length_m, rows),
                    snake_uv2(far.distance_m, far_edge, far.width_m, track.length_m, rows),
                ],
            )
    return strip


#: Base colours, so a still reads without a material library. Deliberately drab -
#: `docs/ROADMAP.md` M1 settled that the measured asphalt albedo of 0.15 renders
#: pale and is kept, because a track surface is darkened by *use* and that arrives
#: with the racing line, not with a hand-picked colour.
MATERIALS = {
    "asphalt": (0.150, 0.150, 0.152, 1.0),
    "paint": (0.860, 0.860, 0.840, 1.0),
    "kerb": (0.620, 0.090, 0.090, 1.0),
    "gravel": (0.480, 0.430, 0.360, 1.0),
    "verge": (0.130, 0.320, 0.085, 1.0),
    "barrier": (0.090, 0.090, 0.095, 1.0),
}
