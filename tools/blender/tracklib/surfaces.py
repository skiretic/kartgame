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

#: Barrier height, meters. Not sourced: Part I §8 grades barriers by *type* and
#: specifies their impact behaviour, not their height. 1.0 m is chosen to be above
#: a kart's centre of mass (0.23 m) by enough that a glancing hit is redirected
#: rather than launched, and it is recorded here rather than in
#: `circuit_reference.h` because nothing but this mesh reads it.
BARRIER_HEIGHT = 1.0


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
                foot_near = near.surface_point(near_edge + hand * limit)
                foot_far = far.surface_point(far_edge + hand * limit)
                head_near = (foot_near[0], foot_near[1] + BARRIER_HEIGHT, foot_near[2])
                head_far = (foot_far[0], foot_far[1] + BARRIER_HEIGHT, foot_far[2])
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
