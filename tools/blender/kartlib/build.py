"""Deterministic geometry helpers shared by every kart module.

Three rules hold everywhere in this package, and all three exist to keep the
output byte-identical between runs — which the M2 acceptance gate requires and
which is what makes a generated mesh reviewable as a hash in a commit.

1.  **No randomness, no time, no iteration over an unordered set.** Vertices are
    emitted in a computed order, never in whatever order a Python `set` or a
    `bpy.data` collection happened to yield.

2.  **Prefer `bmesh` construction over `bpy.ops`.** An operator acts on the
    selection and the active object, so its result depends on state a caller set
    earlier. `bmesh` takes its inputs as arguments. Where an operator is
    genuinely the only route — UV unwrapping and baking are the two — selection
    is set explicitly and immediately before the call, never inherited.

3.  **Every object has a fixed name.** Names are the export's node names, the
    handle the vehicle solver finds a wheel by, and the tie-break the exporter
    uses when ordering siblings. A generated name with a counter in it is a hash
    that changes when a part is inserted upstream.

Coordinate convention is in params.py and is not repeated here: build toward
Blender +Y, which becomes Godot's -Z forward on export.
"""

from __future__ import annotations

import dataclasses
import fnmatch
import math
import sys
from dataclasses import dataclass
from typing import Iterable, Sequence

import bmesh
import bpy
from mathutils import Matrix, Vector

from . import params as P

Vec3 = tuple[float, float, float]


# --- detail levels ---------------------------------------------------------


@dataclass(frozen=True)
class Detail:
    """How finely to build. The kart is built twice, and this is the difference.

    Issue #19 bakes normals from a high-poly source onto the low-poly game
    mesh. That only works if both are the *same kart* built at two densities
    rather than two independently modeled objects — a cage that has to bridge
    two different shapes produces the smeared, skewed bake the issue's
    acceptance criteria calls out.

    So every geometry module takes a `Detail` and reads its segment counts and
    bevel width from it. Nothing else about the kart changes between the two.
    """

    name: str
    tube_segments: int
    bend_segments: int
    tire_segments: int
    exhaust_segments: int
    bevel_width: float
    bevel_segments: int

    @staticmethod
    def low(p: P.KartParams) -> "Detail":
        # A small bevel survives into the low-poly mesh rather than being left
        # to the normal map. A perfectly sharp edge catches no highlight at all,
        # and a normal map cannot invent the silhouette change that a real
        # chamfer gives at a grazing angle.
        return Detail(
            name="low",
            tube_segments=p.tube_segments,
            bend_segments=p.bend_segments,
            tire_segments=p.tire_segments,
            exhaust_segments=p.exhaust_segments,
            bevel_width=0.0015,
            bevel_segments=1,
        )

    @staticmethod
    def high(p: P.KartParams) -> "Detail":
        return Detail(
            name="high",
            tube_segments=p.tube_segments_high,
            bend_segments=p.bend_segments_high,
            tire_segments=p.tire_segments_high,
            exhaust_segments=p.exhaust_segments_high,
            bevel_width=0.004,
            bevel_segments=4,
        )

    @property
    def is_high(self) -> bool:
        return self.name == "high"


# --- groups ----------------------------------------------------------------

#: Part groups, in the order they are built and exported.
#:
#: `interior` is separate from `chassis` because ARCHITECTURE.md §7 makes the
#: cockpit interior an LOD only one camera rig ever sees, and issue #20 wants it
#: cullable from the chase view. That is a visibility range on a node
#: (ADR-0025), so the interior has to *be* its own node rather than being merged
#: into the body mesh.
GROUPS: tuple[str, ...] = (
    "chassis",
    "bodywork",
    "powertrain",
    "wheels",
    "interior",
    "driver",
)


# --- scene setup -----------------------------------------------------------


def reset_scene() -> bpy.types.Scene:
    """An empty scene with metric units, independent of the user's startup file.

    `--factory-startup` would also do this, but it disables add-ons, and Cycles
    is an add-on — issue #19's normal bake needs it. Resetting from inside the
    script keeps Cycles and the glTF exporter loaded while still discarding
    whatever the user's default scene contains. A stray default cube in the
    startup file would otherwise be exported as part of the kart.
    """
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.unit_settings.length_unit = "METERS"
    return scene


def group_collections(scene: bpy.types.Scene) -> dict[str, bpy.types.Collection]:
    """One collection per group, created in `GROUPS` order.

    Creation order is fixed, so the exporter's traversal order is fixed, so the
    glTF node order is fixed. That is a determinism requirement, not tidiness.
    """
    collections: dict[str, bpy.types.Collection] = {}
    for name in GROUPS:
        collection = bpy.data.collections.new(name)
        scene.collection.children.link(collection)
        collections[name] = collection
    return collections


# --- the module contract ---------------------------------------------------


@dataclass
class BuildContext:
    """Everything a geometry module is given, and the only thing it may touch.

    Each module in this package exposes exactly one entry point:

        def build_module(context: BuildContext) -> None

    and is called once per detail level. A module may create objects, empties and
    meshes; it may not create materials (they are made once, up front, and passed
    in), may not touch `bpy.context.scene` settings, may not run UV, bake, LOD or
    export operators, and may not read or write another module's objects.

    That last restriction is what makes the modules independently reviewable: a
    part is wrong if and only if its own module is wrong. The one shared thing is
    the parameter block, and it is frozen.
    """

    params: P.KartParams
    detail: Detail
    collections: dict[str, bpy.types.Collection]
    materials: dict[str, bpy.types.Material]
    output_directory: str = ""
    """Where generated siblings of the glTF go, such as the baked normal atlas.

    Set by `genkart.py` from the resolved `--out` path. Hardcoding
    `assets/generated/` here instead meant `--out=/tmp/x.glb` scattered its normal
    map into the repository, and got the relative-path arithmetic wrong on top.
    """
    high_poly: dict[str, bpy.types.Object] = dataclasses.field(default_factory=dict)
    """The high-poly twin of each object, keyed by the low-poly object's name.

    Populated by `genkart.py` before the bake stage, and empty during geometry
    building — a geometry module never sees it and must not touch it. Issue #19
    bakes normals from a high-poly source onto the low-poly game mesh, and that
    needs both in one scene at the same coordinates, which is a pairing only the
    orchestrator can establish.
    """

    pivots: dict[str, bpy.types.Object] = dataclasses.field(default_factory=dict)
    """Named pivots published for later stages and for the runtime.

    A module that creates a transform something else has to find — a wheel hub,
    the steering column, a driver arm root — registers it here under a stable
    name. Issue #14 and issue #13 both turn on this: the vehicle solver and the
    camera rig look the node up by name, so the name is an interface.
    """

    def collection(self, group: str) -> bpy.types.Collection:
        if group not in self.collections:
            raise KeyError(
                "unknown group %r; the groups are %s" % (group, ", ".join(GROUPS))
            )
        return self.collections[group]

    def material(self, name: str) -> bpy.types.Material:
        # Spec §60.3 renamed four finishes out from under modules the finishes wave
        # did not own — `tray_aluminium` is not aluminum, and `engine_alloy` was
        # four distinct finishes at once. That wave resolved the old names through a
        # `LEGACY_ALIASES` table so the split could land without a cross-file edit,
        # and reported the four one-liners instead of making them. They have since
        # been applied and **the table is gone**: an alias that outlives its
        # migration is a second name for one thing, which is how `engine_alloy`
        # came to hold 116 parts in the first place.
        if name not in self.materials:
            raise KeyError(
                "unknown material %r; the materials are %s"
                % (name, ", ".join(sorted(self.materials)))
            )
        return self.materials[name]

    def publish(self, name: str, obj: bpy.types.Object) -> bpy.types.Object:
        """Register a named pivot, refusing to overwrite one.

        A collision here means two modules claimed the same interface name,
        which is a merge mistake worth failing the build over rather than
        letting the second one win silently.
        """
        if name in self.pivots:
            raise KeyError("pivot %r already published" % name)
        self.pivots[name] = obj
        return obj


# --- object creation -------------------------------------------------------


def object_from_bmesh(
    name: str,
    bm: bmesh.types.BMesh,
    collection: bpy.types.Collection,
    *,
    material: bpy.types.Material | None = None,
    shade_smooth: bool = False,
    smooth_angle: float = math.radians(40.0),
) -> bpy.types.Object:
    """Realize a bmesh as a named object, then free the bmesh.

    `shade_smooth` sets smooth shading with an angle threshold rather than
    marking every face smooth. A tube must be smooth around its circumference
    and sharp where it meets a flat cap, and one angle threshold expresses that
    for every part on the kart without per-edge tagging.
    """
    bm.normal_update()
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    if shade_smooth:
        for polygon in mesh.polygons:
            polygon.use_smooth = True
        # Blender 5.x carries sharp-edge-by-angle as a "Smooth by Angle" node
        # group rather than the old `auto_smooth_angle` property. Tagging the
        # edges directly does the same job with no modifier and no dependency on
        # an asset library being present, which matters for a headless run.
        for edge in mesh.edges:
            edge.use_edge_sharp = False
        mesh.update()
        _mark_sharp_by_angle(mesh, smooth_angle)

    obj = bpy.data.objects.new(name, mesh)
    if material is not None:
        obj.data.materials.append(material)
    collection.objects.link(obj)
    # The single place a material is bound to a mesh on this kart, which is why the
    # per-part finish overrides, livery zones and wear tints of the materials
    # section are applied here rather than in six geometry modules that would each
    # have to carry a livery decision. Checked: `obj.data.materials.append` appears
    # exactly once in this package, on the line above.
    apply_finish(obj)
    return obj


def _mark_sharp_by_angle(mesh: bpy.types.Mesh, threshold: float) -> None:
    """Mark edges sharp where the two faces meeting them exceed `threshold`.

    Replaces the pre-5.x `auto_smooth_angle`. Edges with fewer or more than two
    faces are left alone: a boundary edge has no crease to compute and a
    non-manifold one has no single answer.
    """
    # Built from the loop table rather than from `polygon.edge_keys`, which
    # would need a vertex-pair lookup per edge; a loop already knows its edge.
    faces_per_edge: dict[int, list[int]] = {}
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            edge_index = mesh.loops[loop_index].edge_index
            faces_per_edge.setdefault(edge_index, []).append(polygon.index)

    cosine = math.cos(threshold)
    for edge_index in sorted(faces_per_edge):
        faces = faces_per_edge[edge_index]
        if len(faces) != 2:
            continue
        a = mesh.polygons[faces[0]].normal
        b = mesh.polygons[faces[1]].normal
        if a.dot(b) < cosine:
            mesh.edges[edge_index].use_edge_sharp = True


def empty(
    name: str,
    location: Vec3,
    collection: bpy.types.Collection,
    *,
    parent: bpy.types.Object | None = None,
    size: float = 0.05,
) -> bpy.types.Object:
    """A named pivot, exported as a transform-only glTF node.

    Issue #14 wants wheel origins at the axle center and a consistent rotation
    axis; issue #13 wants a findable steering pivot. Both are this: an empty at
    the right place with the mesh parented under it, so the mesh's own origin
    never has to be moved and the node the solver rotates is unambiguous.
    """
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "PLAIN_AXES"
    obj.empty_display_size = size
    obj.location = location
    collection.objects.link(obj)
    if parent is not None:
        set_parent(obj, parent)
    return obj


def set_parent(child: bpy.types.Object, parent: bpy.types.Object) -> None:
    """Parent while keeping the child's world transform.

    Set through `matrix_parent_inverse` rather than by clearing and re-setting
    the child's location, so a part's authored world position stays the number
    the parameter block gave it.

    **The `view_layer.update()` is load-bearing.** `matrix_world` is maintained by
    the depsgraph, and for an object created earlier in the same script it is still
    the identity until the depsgraph is evaluated — so the inverse computed from it
    would be the identity too, and every child would land at the kart's origin one
    hub offset away from where it belongs. `frame.py` never tripped over this only
    because its root pivot happens to sit at (0, 0, 0), where the identity is the
    right answer by accident. The wheels, whose pivots are at the four hubs, hit it
    immediately.
    """
    bpy.context.view_layer.update()
    child.parent = parent
    child.matrix_parent_inverse = parent.matrix_world.inverted()


# --- polyline and sweep ----------------------------------------------------


def fillet(
    points: Sequence[Vec3],
    radius: float,
    segments: int,
) -> list[Vector]:
    """Round every interior corner of a polyline with a tangent circular arc.

    A real chassis rail is mandrel-bent. A mitered corner between two straight
    tubes is the clearest single tell of toy kart geometry, which issue #12 calls
    out by name, so bends are geometry here rather than something a bevel
    modifier is asked to rescue afterwards.

    The arc radius is clamped per corner to what the two legs can afford, so a
    short leg between two bends tightens its bends instead of overshooting past
    the corner and folding the tube back on itself.
    """
    if len(points) < 3 or radius <= 0.0 or segments < 1:
        return [Vector(p) for p in points]

    source = [Vector(p) for p in points]
    result: list[Vector] = [source[0]]

    for index in range(1, len(source) - 1):
        previous, corner, following = source[index - 1], source[index], source[index + 1]
        to_previous = previous - corner
        to_next = following - corner
        length_previous = to_previous.length
        length_next = to_next.length
        if length_previous < 1e-9 or length_next < 1e-9:
            continue

        u = to_previous / length_previous
        v = to_next / length_next
        cosine = max(-1.0, min(1.0, u.dot(v)))
        angle = math.acos(cosine)
        # Collinear, or doubling straight back: no corner to round.
        if angle < 1e-4 or abs(angle - math.pi) < 1e-4:
            result.append(corner)
            continue

        half = angle * 0.5
        # Tangent length from the corner to where the arc touches each leg.
        tangent = radius / math.tan(half)
        # Never eat more than 45% of a leg, so two adjacent bends cannot meet.
        limit = min(length_previous, length_next) * 0.45
        effective_radius = radius
        if tangent > limit:
            tangent = limit
            effective_radius = tangent * math.tan(half)

        start = corner + u * tangent
        end = corner + v * tangent
        bisector = (u + v)
        if bisector.length < 1e-9:
            result.append(corner)
            continue
        bisector.normalize()
        center = corner + bisector * (effective_radius / math.sin(half))

        radius_start = start - center
        radius_end = end - center
        axis = radius_start.cross(radius_end)
        if axis.length < 1e-12:
            result.append(corner)
            continue
        axis.normalize()
        sweep = radius_start.angle(radius_end)

        for step in range(segments + 1):
            fraction = step / segments
            rotation = Matrix.Rotation(sweep * fraction, 3, axis)
            result.append(center + (rotation @ radius_start))

    result.append(source[-1])

    # Collapse points the fillet arithmetic placed on top of each other. A
    # duplicate would give the sweep a zero-length tangent and a degenerate ring.
    deduplicated: list[Vector] = [result[0]]
    for point in result[1:]:
        if (point - deduplicated[-1]).length > 1e-7:
            deduplicated.append(point)
    return deduplicated


def _frames(path: Sequence[Vector]) -> list[tuple[Vector, Vector, Vector]]:
    """Parallel-transported (tangent, normal, binormal) at every path point.

    Transporting the normal along the path rather than recomputing it per point
    is what keeps a swept tube from twisting where the path changes plane. The
    seam has to stay on one side of the tube; recomputed frames spin it.
    """
    count = len(path)
    tangents: list[Vector] = []
    for index in range(count):
        if index == 0:
            tangent = path[1] - path[0]
        elif index == count - 1:
            tangent = path[-1] - path[-2]
        else:
            # Averaging the two legs makes the cross-section at a joint the
            # miter plane, which is what a bent tube actually has.
            incoming = (path[index] - path[index - 1]).normalized()
            outgoing = (path[index + 1] - path[index]).normalized()
            tangent = incoming + outgoing
            if tangent.length < 1e-9:
                tangent = outgoing
        tangents.append(tangent.normalized())

    # Seed the normal with whichever world axis is least aligned with the first
    # tangent, so the choice is deterministic and never near-degenerate.
    first = tangents[0]
    seed = min(
        (Vector((1.0, 0.0, 0.0)), Vector((0.0, 1.0, 0.0)), Vector((0.0, 0.0, 1.0))),
        key=lambda axis: abs(axis.dot(first)),
    )
    normal = (seed - first * seed.dot(first)).normalized()

    frames: list[tuple[Vector, Vector, Vector]] = []
    for index, tangent in enumerate(tangents):
        if index > 0:
            previous = tangents[index - 1]
            axis = previous.cross(tangent)
            if axis.length > 1e-9:
                angle = max(-1.0, min(1.0, previous.dot(tangent)))
                rotation = Matrix.Rotation(math.acos(angle), 3, axis.normalized())
                normal = rotation @ normal
            normal = (normal - tangent * normal.dot(tangent)).normalized()
        binormal = tangent.cross(normal).normalized()
        frames.append((tangent, normal, binormal))
    return frames


def sweep_tube(
    bm: bmesh.types.BMesh,
    path: Sequence[Vec3] | Sequence[Vector],
    radius: float,
    segments: int,
    *,
    cap_start: bool = True,
    cap_end: bool = True,
) -> None:
    """Sweep a circle of `radius` along `path`, appending faces to `bm`.

    Rings are emitted in path order and vertices within a ring in increasing
    angle, which is what makes the vertex buffer reproducible.
    """
    points = [Vector(p) for p in path]
    if len(points) < 2 or segments < 3:
        return

    frames = _frames(points)
    rings: list[list[bmesh.types.BMVert]] = []
    for point, (_, normal, binormal) in zip(points, frames):
        ring: list[bmesh.types.BMVert] = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            offset = normal * (math.cos(angle) * radius) + binormal * (
                math.sin(angle) * radius
            )
            ring.append(bm.verts.new(point + offset))
        rings.append(ring)

    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        for step in range(segments):
            following = (step + 1) % segments
            bm.faces.new((lower[step], lower[following], upper[following], upper[step]))

    if cap_start:
        bm.faces.new(tuple(reversed(rings[0])))
    if cap_end:
        bm.faces.new(tuple(rings[-1]))


def tube(
    bm: bmesh.types.BMesh,
    points: Sequence[Vec3],
    diameter: float,
    detail: Detail,
    bend_radius: float,
    *,
    cap_start: bool = True,
    cap_end: bool = True,
) -> None:
    """Fillet a control polyline and sweep a tube along it. The frame builder."""
    path = fillet(points, bend_radius, detail.bend_segments)
    sweep_tube(
        bm,
        path,
        diameter * 0.5,
        detail.tube_segments,
        cap_start=cap_start,
        cap_end=cap_end,
    )


def lathe(
    bm: bmesh.types.BMesh,
    profile: Sequence[tuple[float, float]],
    segments: int,
    *,
    axis: str = "X",
    center: Vec3 = (0.0, 0.0, 0.0),
    close_profile: bool = False,
) -> None:
    """Revolve a 2D profile of (radius, along-axis) pairs into a surface.

    Tires, rims and the exhaust's expansion chamber are all revolutions, and a
    revolution has exact control of the silhouette the eye actually reads. A
    profile point at radius 0 collapses to the axis and is emitted as a fan
    rather than a ring of coincident vertices.
    """
    if len(profile) < 2 or segments < 3:
        return

    origin = Vector(center)
    axis_index = {"X": 0, "Y": 1, "Z": 2}[axis]
    # The two axes the profile radius sweeps through, in a fixed order so the
    # winding is the same for every part on the kart.
    radial = [(axis_index + 1) % 3, (axis_index + 2) % 3]

    rings: list[list[bmesh.types.BMVert] | bmesh.types.BMVert] = []
    for radius, along in profile:
        if abs(radius) < 1e-9:
            position = [0.0, 0.0, 0.0]
            position[axis_index] = along
            rings.append(bm.verts.new(origin + Vector(position)))
            continue
        ring: list[bmesh.types.BMVert] = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            position = [0.0, 0.0, 0.0]
            position[axis_index] = along
            position[radial[0]] = math.cos(angle) * radius
            position[radial[1]] = math.sin(angle) * radius
            ring.append(bm.verts.new(origin + Vector(position)))
        rings.append(ring)

    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        lower_is_point = isinstance(lower, bmesh.types.BMVert)
        upper_is_point = isinstance(upper, bmesh.types.BMVert)
        if lower_is_point and upper_is_point:
            continue
        # Both fans were wound inward while the barrel quads between them were
        # correct, so a capped cylinder came out with a signed volume of 0.260
        # against a true 0.785 — the shortfall is exactly the two cones counted
        # negative. Same root cause and same invisibility as `box` above.
        if lower_is_point:
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((lower, upper[following], upper[step]))
        elif upper_is_point:
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((lower[step], lower[following], upper))
        else:
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new(
                    (lower[step], lower[following], upper[following], upper[step])
                )

    if close_profile:
        first, last = rings[0], rings[-1]
        if not isinstance(first, bmesh.types.BMVert) and not isinstance(
            last, bmesh.types.BMVert
        ):
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((last[step], last[following], first[following], first[step]))


def box(
    bm: bmesh.types.BMesh,
    size: Vec3,
    center: Vec3 = (0.0, 0.0, 0.0),
    *,
    rotation: Matrix | None = None,
) -> list[bmesh.types.BMVert]:
    """An axis-aligned box, optionally rotated about its own center.

    Vertices are emitted in a fixed corner order rather than by `bmesh.ops`, so
    two runs produce the same buffer and a rotation is applied to the corners
    rather than to the object, keeping the object transform identity.
    """
    half = Vector(size) * 0.5
    origin = Vector(center)
    corners: list[bmesh.types.BMVert] = []
    for z in (-1, 1):
        for y in (-1, 1):
            for x in (-1, 1):
                local = Vector((half.x * x, half.y * y, half.z * z))
                if rotation is not None:
                    local = rotation @ local
                corners.append(bm.verts.new(origin + local))

    # Indices follow the emission order above: bit 0 is x, bit 1 is y, bit 2 z.
    #
    # **Every one of these was wound inward.** Measured: a unit cube built here
    # had a signed volume of -1.0, which is the exact volume with every face
    # facing in. It went unnoticed because Blender materials default to
    # `use_backface_culling = False`, so the exporter writes `doubleSided: true`
    # into the glTF and Godot renders the backfaces with the shading normal
    # flipped — the kart looked right in every render this project has taken.
    # What it would have broken is issue #19's tangent-space normal bake, which
    # reads the low-poly's normals rather than its silhouette, and anything that
    # ever turns culling on.
    #
    # The check that catches this is signed volume, not a render: sum
    # `a . (b x c) / 6` over the faces and require it positive.
    faces = (
        (0, 2, 3, 1),  # -Z
        (4, 5, 7, 6),  # +Z
        (0, 1, 5, 4),  # -Y
        (2, 6, 7, 3),  # +Y
        (0, 4, 6, 2),  # -X
        (1, 3, 7, 5),  # +X
    )
    for face in faces:
        bm.faces.new(tuple(corners[index] for index in face))
    return corners


def bevel_object(obj: bpy.types.Object, detail: Detail, *, limit_angle: float = 0.52) -> None:
    """Bevel every hard edge, then apply.

    `bmesh.ops.bevel` rather than a modifier: a modifier evaluated at export
    time depends on the depsgraph, and applying it needs an operator that reads
    the active object. Doing it in bmesh keeps the result a pure function of the
    mesh and the width.

    `limit_angle` skips edges whose faces are nearly coplanar, which is the
    modifier's Angle limit. Without it, a swept tube gets a bevel around every
    ring of its circumference and the vertex count explodes for no visual gain.
    """
    if detail.bevel_width <= 0.0:
        return
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)

    cosine = math.cos(limit_angle)
    edges = []
    for edge in bm.edges:
        if len(edge.link_faces) != 2:
            continue
        a, b = edge.link_faces
        if a.normal.dot(b.normal) < cosine:
            edges.append(edge)

    if edges:
        bmesh.ops.bevel(
            bm,
            geom=edges,
            offset=detail.bevel_width,
            offset_type="OFFSET",
            segments=detail.bevel_segments,
            profile=0.5,
            affect="EDGES",
            clamp_overlap=True,
            # bmesh.ops spells these without the modifier's MITER_ prefix.
            miter_outer="SHARP",
            miter_inner="SHARP",
        )

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    _mark_sharp_by_angle(mesh, math.radians(40.0))


def mirror_x(
    source: bpy.types.Object,
    name: str,
    collection: bpy.types.Collection,
) -> bpy.types.Object:
    """A mirrored copy across the kart's centerline, with normals corrected.

    **The copy already carries the source's material slots**, so a caller must not
    append the material again — doing so gives the mirrored half two identical
    slots, which survives into the glTF as a duplicated material reference. Four
    parts on the kart had that before it was caught in the manifest.

    Mirroring the mesh data rather than setting a negative object scale is
    deliberate: a negative scale survives into the glTF node transform, and
    Godot's importer then has an inside-out mesh with a handedness flip that
    shows up as inverted lighting on one side of the kart only.
    """
    mesh = source.data.copy()
    mesh.name = name
    bm = bmesh.new()
    bm.from_mesh(mesh)
    for vertex in bm.verts:
        vertex.co.x = -vertex.co.x
    # One call over every face: mirroring inverts the winding of all of them, and
    # reversing them one at a time would rebuild the face table per call.
    bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    obj.location = source.location.copy()
    obj.location.x = -source.location.x
    obj.rotation_euler = source.rotation_euler.copy()
    collection.objects.link(obj)
    # Re-applied rather than inherited. The copy carries the source's slots and its
    # source's vertex colors, and both are wrong for a mirrored part whose zones or
    # tint depend on x: a tire's crown is symmetric so it survives, but nothing
    # guarantees the next rule is.
    apply_finish(obj)
    return obj


# --- materials -------------------------------------------------------------

#: Finishes, per spec §60. Each entry is `(sRGB hex, target linear luminance,
#: roughness, metallic)` and the two halves of that pair carry different weight:
#:
#: * The **hex fixes the hue only.** Spec §60 measured most of these off
#:   photographs, and a hex read off a JPEG is a *rendered* value carrying that
#:   photograph's white balance and exposure. It is not an albedo.
#: * The **luminance fixes the read**, and it is anchored to the values this
#:   project has already verified render correctly under its 100,000 lx sun. The
#:   old block's comments are the evidence: `engine_alloy` at 0.255 clipped every
#:   casting feature to flat near-white and had to come down to 0.175, and a bright
#:   `tray_aluminium` swamped the frame it was bolted to. Feeding §60's photographic
#:   hexes in as base color directly would walk straight back into both.
#:
#: So the color is built as `linear(hex) scaled to Rec.709 luminance`. That keeps
#: §60's sourced hue and §60's stated *relationships* — "4x the powder coat",
#: "less than half the billet", "the darkest thing on the kart" — while every
#: absolute value stays a single reviewable number in a scale that is known to
#: work. CLAUDE.md's rule, in code: colors here are relationships, not albedo.
#:
#: Provenance per §00's vocabulary is in the comment beside each group. Where a hex
#: is `estimated` it says so; §60's own failed measurement (the Factory orange,
#: red-channel-clipped in 36.6% and 54.1% of pixels across two CRG frames) is
#: marked at the palette rather than hidden here.
Finish = tuple[str, float, float, float]

#: Finishes whose color does not depend on the livery.
#:
#: 34 entries against the old block's 15. §60.6 item 8 counted "15 entries for at
#: least 22 distinct finishes, and one entry covering 75 of 146 parts" — 116 of 295
#: by the time this wave measured it. The splits below are §60.3's, one group per
#: subsection, and the part-by-part assignment is `FINISHES`.
FIXED_FINISHES: dict[str, Finish] = {
    # --- §60.3.1 tube and coatings ---------------------------------------
    # Chrome bumper and nose tube, `sourced` crg_roadrebel_kz_detail7.webp: the
    # front bumper tube crossing the top of the frame is unmistakably chrome
    # against gloss-black rails. Hex `estimated` (a neutral bright).
    "tube_chrome": ("#e8ecef", 0.700, 0.08, 1.0),
    # The pedal loop in detail7 is raw polished stainless with visible bend-forming
    # marks — brighter and cooler than any coating. `sourced` finish, `estimated` hex.
    "stainless_polished": ("#dfe2e4", 0.480, 0.18, 1.0),
    # Moulded, not painted: reads flatter and browner than powder coat.
    # `sourced` exh_commons_shifter_engine.jpg (filter and airbox).
    "plastic_matte_black": ("#1d1d1f", 0.024, 0.55, 0.0),
    # --- §60.3.2 the floor tray ------------------------------------------
    # NOT anodized aluminum, which is what the old `tray_aluminium` claimed.
    # tonykart_racer401T_p01.jpg is a close-up of a granular anti-slip coating,
    # dominant mode #c8c8d2, coarse enough to resolve individual grains. So
    # metalness 1.0 -> 0.0 and roughness 0.58 -> 0.85. `derived` from p01.
    # The old comment's instinct was right and its diagnosis was wrong: it is not
    # a dark metal, it is a bright non-metal, and at 4.5x the tube it is the
    # brightest thing on the chassis rather than the darkest.
    "tray_antislip": ("#c8c8d2", 0.196, 0.85, 0.0),
    # --- §60.3.3 tires ---------------------------------------------------
    # The darkest thing on the kart, and a brown-black rather than the frame's
    # blue-black — the two must not collapse into one value. `estimated` hue.
    # Sidewall 0.72 (kept), tread 0.62: a scrubbed slick is glossier than its
    # sidewall, `sourced` exh_commons_buntschu_kz2.jpg, the only worked tire here.
    "tire_rubber": ("#1a1614", 0.0165, 0.72, 0.0),
    # The crown, at the *same albedo* as the sidewall: an earlier cooler hue
    # (#141a1c, read off buntschu's bluish sheen) drew a hard-edged painted band
    # around the tire and Anthony rejected it on sight — one rubber, one color.
    # A sheen is an illumination effect, so it lives in the roughness delta
    # (0.62 worked vs 0.72 virgin) and the baked tread grain, both of which move
    # with the light instead of sitting in the paint.
    "tire_tread": ("#1a1614", 0.0165, 0.62, 0.0),
    # --- §60.3.5 steel running gear, which was four finishes -------------
    # Kept exactly: heat-treated steel, dark satin, and the old comment's reason
    # for it (a 1,080 mm bright cylinder becomes the kart's focal point) still holds.
    "axle_steel": ("#7b7b80", 0.078, 0.42, 1.0),
    # Near-black with oil and *not* the axle's value. Roller ends and tooth flanks
    # polish bright while everything between holds black oil and dust; the split is
    # a texture job, so this is the oiled bulk. `estimated` — no in-repo photograph
    # shows a used chain, both CRG frames are new karts with the guard fitted.
    "chain_oiled": ("#17140f", 0.020, 0.45, 1.0),
    # Bright machined steel or 7075, swept polished flanks. `estimated`.
    "sprocket_steel": ("#c9c9c4", 0.230, 0.30, 1.0),
    # Bright zinc plate, cooler and brighter than the axle, faint yellow passivate
    # on the nuts. Clearly resolved in detail7, so `sourced`.
    "zinc_plated": ("#cfd2cf", 0.320, 0.28, 1.0),
    "black_oxide": ("#17171a", 0.014, 0.50, 1.0),
    # Bright spring steel, heat-tinted straw at the header end. `derived` from the
    # exhaust gradient, §60.3.9.
    "spring_steel": ("#d8c48a", 0.260, 0.25, 1.0),
    # --- §60.3.10 brakes -------------------------------------------------
    # Dark grey to near-black with a scalloped periphery, sampled #303330 in the
    # detail7 (front) and detail11 (rear) crops — NOT the bright turned steel the
    # committed list claimed. `derived`. Base sits a little above the sampled read
    # so the swept annulus in `TINTS` has somewhere to be brighter than; the
    # unswept majority multiplies back down onto #303330.
    "disc_iron": ("#303330", 0.060, 0.55, 1.0),
    "brake_pad_friction": ("#4a4640", 0.045, 0.80, 0.0),
    # The most chromatic non-livery item on the kart, red in both CRG frames.
    "brake_line_red": ("#b5201c", 0.075, 0.30, 0.0),
    # --- §60.3.8 engine and machined alloy, which was three finishes -----
    # Raw sand casting. Measured at #605944 against the machined billet's #f6f4ca
    # in one frame of exh_commons_shifter_engine.jpg, lit identically — the casting
    # is less than half the billet's value and warmer, which is what makes the
    # split a measurement rather than an assertion. Luminance and roughness kept
    # from the old `engine_alloy`, which is the one value known to read correctly.
    "engine_cast": ("#605944", 0.175, 0.66, 1.0),
    # Machined billet / clear anodize: brighter, whiter and much smoother, with
    # directional machining marks. §60's own ratio off the two samples is 2.3x the
    # casting, i.e. 0.40; held to 1.66x instead because 0.255 on a *rough* metal
    # already clipped in this project's sun and a smooth one has less headroom, not
    # more. Deliberately below the spec ratio, and this is the reason.
    "anodized_clear": ("#f6f4ca", 0.290, 0.35, 1.0),
    # Confirmed twice: the front hub collar in detail7 and the rear disc carrier in
    # detail11. Warm, saturated, darker than the number yellow.
    "anodized_gold": ("#9a7b34", 0.170, 0.30, 1.0),
    "anodized_black": ("#1c1c1e", 0.026, 0.40, 1.0),
    # Unpainted brazed core: the only cool-white metal on a KZ, and the brightest
    # object on the kart besides the driver's suit in buntschu. The 24 parts that
    # *make up* the radiator were on the sand casting's value while
    # `radiator_core` had its own; that is the bug §60.3.8 names.
    "radiator_alu": ("#d5dade", 0.240, 0.45, 1.0),
    "radiator_core": ("#b8bcc0", 0.120, 0.60, 1.0),
    # --- §60.3.9 exhaust -------------------------------------------------
    # Nickel plate at the *cool* end. A single base color cannot express this part
    # — the finish is a heat gradient along the pipe's own axis — so the base is the
    # bright silencer end and `TINTS` ramps it down to burnt grey-brown at the
    # flange. §60 records the old 0.088 as roughly the flange end, i.e. wrong for
    # the silencer by an order of magnitude in value.
    "exhaust_nickel": ("#dfe4e6", 0.400, 0.22, 1.0),
    # --- §60.3.11 rubber, hose and small parts ---------------------------
    # Orange, not black: notes_radiator.md §5 records the CRG runs as orange over a
    # protective sleeve. `sourced` via that note.
    "hose_silicone": ("#d4551a", 0.130, 0.42, 0.0),
    "rubber_grip": ("#26262a", 0.028, 0.66, 0.0),
    "rubber_gloss": ("#1f1f22", 0.024, 0.40, 0.0),
    "rubber_matte": ("#202022", 0.024, 0.70, 0.0),
    # Glazed porcelain, kept as the brightest thing on the engine and for the old
    # comment's reason: it is the highest-contrast object in every two-stroke
    # reference photograph, and that contrast is the point of modeling it.
    "plug_ceramic": ("#f2eee6", 0.710, 0.30, 0.0),
    # --- §60.3.7 the seat, which was near-black ---------------------------
    # `seat_fiberglass` was (0.055, 0.055, 0.058) — the opposite of a bare
    # gel-coated glass kart seat, which is one of the *brightest* objects on the
    # kart. Pale grey with a green cast, roughly 4-5x the powder coat, greyer and
    # greener than the tray, glossy with a slight orange peel. `derived` from
    # crg_roadrebel_kz_detail11.webp with §60.3.7's identification caveat attached:
    # all four studio chassis frames are seatless, so the *relationship* is what is
    # asserted. Nothing in that frame supports 0.055 whichever object it is.
    "seat_fiberglass": ("#cdd3c8", 0.190, 0.22, 0.0),
    # --- §60.3.12 parts the other sections add ---------------------------
    # Translucent polyethylene, natural amber-white. Translucency itself is not
    # expressible in a glTF PBR material, so this is the opaque read of it.
    "tank_polyethylene": ("#e8dcbc", 0.360, 0.30, 0.0),
    # exh_eurokart_3.jpg is a New-Line carbon exhaust support at readable weave
    # scale. The twill itself is a texture; this is the resin gloss over it.
    "carbon_twill": ("#1a1a1c", 0.020, 0.20, 0.0),
    # Unwrapped panel interiors and mounting flanges: the substrate, satin and
    # flat, normally the panel's own moulded color rather than the livery's.
    # §60.3.6 — `bodywork_plastic`'s 0.28 split the difference between this and the
    # wrap's 0.16 and got neither.
    "bodywork_substrate": ("#26262a", 0.030, 0.55, 0.0),
    # --- §60.5.1 the one genuinely sourced color in the project -----------
    # #ecd44c, `sourced`: sampled at four independent number zones of Birel ART's
    # own *vector* decal artwork (birelart_kz_graphics.jpg) and identical to the
    # byte at all four, so there is no photographic white balance to correct. It
    # supersedes the provisional #d7c354, which is 8% darker at the same hue and
    # reads as a print defect beside it. Art. 3.7 regulates this field, so it is
    # identical in all three liveries by design. Roughness 0.45: a number plate is
    # flexible opaque plastic and carries no clear laminate (Art. 3.7), and it has
    # to stay legible off-axis.
    "number_yellow": ("#ecd44c", 0.330, 0.45, 0.0),
    # The glyphs on that field. Art. 3.7: *"black numbers on a yellow
    # background"*, and cut vinyl is glossier than the printed field it sits on
    # -- slightly, not chrome. Hue near-neutral; a pure #000000 albedo returns
    # nothing to the eye and reads as a hole, same reasoning as the other
    # near-blacks in this table.
    "number_vinyl_black": ("#111113", 0.012, 0.35, 0.0),
    # --- driver package (§60.1.6; driver.py builds it, #17) ------------------
    "suit_fabric": ("#16305c", 0.055, 0.68, 0.0),
    "helmet_shell": ("#f0ece6", 0.640, 0.14, 0.0),
    # --- §60.1.8 the driver's finishes, #17 ------------------------------
    # Sampled off exh_commons_buntschu_kz2.jpg — a KZ2 driver mid-corner, the
    # only photograph in this repo of a driver in a kart — 16-level modal sample
    # per region, cross-read against exh_commons_panfilov_kz2.jpg. `helmet_shell`
    # above was confirmed by the same pass and is not restated. §60.1.8 holds the
    # measured samples and the reasoning for every luminance that sits above its
    # measured ratio.
    # Navy fabric, hex `derived` from the #182848 mode warmed toward the mean.
    # `suit_fabric` above is within a hue step and 0.005 of luminance, which is
    # corroboration. Luminance held above the measured 0.029-equivalent because a
    # textureless surface at that value is a black hole — same reasoning as
    # `engine_cast`'s 0.175.
    "overalls_fabric": ("#1c2c4c", 0.060, 0.65, 0.0),
    # Art. 7.5's rigid shell to FIA 8870-2018, moulded. All `estimated`: no
    # photograph in this repo shows a rib protector at all (§60.1.8 finding 2 —
    # it is worn under the suit). Darker than the overalls, lighter than the
    # tires, so it separates from both if it ever shows.
    "protector_shell": ("#232326", 0.032, 0.50, 0.0),
    # Hex `derived` from the #083858 visor mode. Roughness 0.08 is the smoothest
    # surface on the kart, level with `tube_chrome` — true of a real visor.
    # Metallic 0.0: tinted polycarbonate, not a mirror. Luminance below the
    # measured 0.075 on purpose; most of that read is the sky reflection a
    # roughness-0.08 material generates for itself.
    "visor_tint": ("#123a5c", 0.055, 0.08, 0.0),
    # Hex `derived` from #181818/#3b3d41. Roughness 0.55: perforated suede,
    # matte — not `rubber_gloss`'s 0.40, which is what it was standing in as.
    "glove_leather": ("#242428", 0.030, 0.55, 0.0),
    # Hex `derived` from #282838. Glossier than a glove — a karting boot is
    # smooth coated leather.
    "boot_leather": ("#26262c", 0.034, 0.38, 0.0),
}

#: The three livery variants of §60.5, as role -> (hex, luminance).
#:
#: Five materials take their color from here and nothing else does, so a livery is
#: one argument: `genkart.sh --livery=factory`. Roughness and metalness are held
#: constant across variants deliberately — a livery changes hue, not exposure and
#: not finish, so every value relationship measured in §60.3 survives the switch.
#:
#: `tube` is gloss powder coat at roughness 0.30, down from 0.42:
#: crg_roadrebel_kz_detail7.webp shows a hard specular streak running the length of
#: every tube and a 30 mm tube at 0.42 will not produce one. §60.3.1, `sourced`.
LIVERY_ROLES: tuple[str, ...] = ("tube", "rim", "wrap", "contrast", "accent")

LIVERIES: dict[str, dict[str, tuple[str, float]]] = {
    # A — Heritage. Green tubes, gold magnesium rims, white wrap, green/red
    # pinstripe. The tube green is `estimated`: tonykart_racer401T_p01.jpg samples
    # #1d3e2b on a shadowed 30 mm cylinder and #285032 as the frame's dominant
    # mode, both under a cool studio white sweep, so the true hue is warmer than
    # either. The pinstripe green and red are measured off p01's *tray* — printed
    # vinyl on a flat surface, so closer to true than the tube.
    "heritage": {
        "tube": ("#1d5c33", 0.040),
        "rim": ("#9a7b34", 0.150),
        "wrap": ("#f2f0ec", 0.420),
        "contrast": ("#c33533", 0.110),
        "accent": ("#2d5839", 0.070),
    },
    # B — Factory. Black tubes, black rims, orange/black wrap.
    #
    # **The orange is `estimated` and §60 records the measurement failing.** Across
    # crg_roadrebel_kz_detail7.webp 36.6% of the orange pixels have the red channel
    # clipped at >=253, and in detail11.webp 54.1%: a fluorescent ink under studio
    # light saturates the sensor, so the hue is unrecoverable from these frames and
    # this value is certainly wrong. Any orange sampled off a CRG photograph in this
    # repo is a guess wearing a measurement's clothes, and it is recorded as a guess.
    #
    # `contrast` is white rather than §60.5.3's mid grey, and the reason is
    # arithmetic: Art. 9.5.5.1 wants the rear protection's outer parts "clearly
    # different" from the main part, §60.4.5 makes that checkable at >=0.25 linear
    # luminance, and against an orange main part at 0.150 no grey below 0.400
    # clears it. Checked in the build log rather than judged.
    "factory": {
        "tube": ("#14161a", 0.040),
        "rim": ("#1c1c1e", 0.032),
        "wrap": ("#f4491f", 0.150),
        "contrast": ("#f2f0ec", 0.420),
        "accent": ("#9a9ea0", 0.230),
    },
    # C — Valdirone. Deep blue tubes, silver rims, white wrap, accented on the
    # circuit's panel yellow. All `estimated`; the accent is carried forward from
    # the front-end work rather than measured here, and §60.5.4's adjacency rule
    # applies — #d7c354 is never placed against a #ecd44c number field, because an
    # 8% value step in the same hue reads as a print defect.
    "valdirone": {
        "tube": ("#14284b", 0.040),
        "rim": ("#b9bcc0", 0.330),
        "wrap": ("#f4f5f7", 0.420),
        "contrast": ("#14284b", 0.040),
        "accent": ("#d7c354", 0.230),
    },
}

LIVERY_DEFAULT = "heritage"

#: Which fixed-finish name each livery role fills.
LIVERY_MATERIALS: dict[str, str] = {
    "tube": "frame_powdercoat",
    "rim": "rim_magnesium",
    "wrap": "bodywork_wrap",
    "contrast": "bodywork_contrast",
    "accent": "livery_accent",
}

#: Roughness and metalness for the livery-driven materials, which the palette does
#: not set. §60.3.1 for the tube, §60.3.4 for the rims, §60.3.6 for the wrap.
LIVERY_SURFACE: dict[str, tuple[float, float]] = {
    "tube": (0.30, 0.0),
    # An anodize is never as glossy as a coating, so a black rim reads flatter and
    # slightly lighter than a gloss black tube. detail7 shows a gold-anodized
    # flange collar on a black-anodized hub — both finishes on one assembly.
    "rim": (0.45, 1.0),
    # The wrap's clear laminate is the glossiest thing on the bodywork.
    "wrap": (0.16, 0.0),
    "contrast": (0.16, 0.0),
    "accent": (0.16, 0.0),
}

#: Per-part finish overrides, as (glob on the object name, material name).
#:
#: **First match wins, so the order is the specification** — the specific patterns
#: precede the general ones and a reordering is a behavior change.
#:
#: This table exists because the finish of a part is not the same concern as its
#: geometry. §60.3 found four sets of parts on `frame_powdercoat` that are not
#: powder coated, 116 parts sharing one `engine_alloy` across four distinct
#: finishes, and `axle_steel` covering four more; correcting that by editing six
#: geometry modules would put a livery decision in six files. `object_from_bmesh`
#: is the single place a material is bound to a mesh on this kart — one call site,
#: checked — so the override happens there and the geometry modules keep asking for
#: whatever they asked for.
#:
#: A pattern that matches nothing is **fatal**, same rule as `joints.py`'s: object
#: names are the export's node names and are frozen by determinism rule 3, so a
#: pattern going stale means a part was renamed and its finish silently reverted to
#: the module's default. That is exactly the class of failure CLAUDE.md records for
#: `Dictionary.get(key, default)`.
FINISHES: tuple[tuple[str, str], ...] = (
    # --- §60.3.1, the four sets that are on the powder coat and should not be ---
    # Chrome, detail7. The front bumper tube is unmistakably chrome against gloss
    # black rails, and the nose hoops are the same tube.
    ("chassis_rear_bumper", "tube_chrome"),
    ("chassis_nose_hoop_*", "tube_chrome"),
    ("chassis_front_bumper_support", "tube_chrome"),
    # Plated steel column and lever; the clutch lever and shifter base are
    # anodized aluminum. crg_roadrebel_steering.webp, birelart_kz_steering_column.jpg.
    ("steering_column", "tube_chrome"),
    ("steering_bearing_upper", "tube_chrome"),
    # Polished stainless pedals, detail7. The rubber pads come first or the glob
    # below would take them.
    ("pedal_brake_pad", "rubber_grip"),
    ("pedal_throttle_pad", "rubber_grip"),
    ("pedal_*", "stainless_polished"),
    # Moulded plastic, not painted metal.
    ("engine_airbox*", "plastic_matte_black"),
    ("engine_battery", "plastic_matte_black"),
    ("radiator_curtain", "plastic_matte_black"),
    ("brake_disc_protector", "plastic_matte_black"),
    # --- §60.3.2 the tray ---
    ("chassis_floor_tray", "tray_antislip"),
    # --- §60.3.9 the exhaust, and its flange, which §60 flags on engine_alloy ---
    ("exhaust_manifold_bolt_*", "black_oxide"),
    ("exhaust_manifold", "exhaust_nickel"),
    ("exhaust_manifold_spigot", "exhaust_nickel"),
    ("exhaust_spring_*", "spring_steel"),
    ("exhaust_silencer_isolator", "rubber_matte"),
    ("exhaust_silencer_band_*", "stainless_polished"),
    ("exhaust_silencer_bracket*", "stainless_polished"),
    ("exhaust_silencer_saddle", "stainless_polished"),
    ("exhaust_hanger_arm", "carbon_twill"),
    ("exhaust_hanger_*", "stainless_polished"),
    # --- §60.3.8 the radiator's own 24 parts, which were on the casting's value ---
    ("radiator_fin_*", "radiator_alu"),
    ("radiator_tank_*", "radiator_alu"),
    ("radiator_end_*", "radiator_alu"),
    ("radiator_divider", "radiator_alu"),
    ("radiator_cap", "radiator_alu"),
    ("radiator_bracket_*", "anodized_clear"),
    ("radiator_hose_*", "hose_silicone"),
    ("cooling_hose_*", "hose_silicone"),
    ("cooling_belt", "rubber_matte"),
    ("cooling_pump_body", "engine_cast"),
    ("cooling_pump_bracket", "anodized_clear"),
    ("cooling_pump_pulley", "anodized_clear"),
    ("cooling_axle_pulley", "anodized_clear"),
    # --- §60.3.8 machined billet ---
    ("engine_mount_*", "anodized_clear"),
    ("brake_caliper_*", "anodized_clear"),
    ("brake_master_*", "anodized_clear"),
    ("brake_distributor", "anodized_clear"),
    ("brake_balance_regulator", "anodized_clear"),
    ("brake_pushrod*", "stainless_polished"),
    # Rear disc assembly: gold carrier (detail11), clear-anodized hub, stainless
    # bobbins, and only the disc itself is iron.
    ("brake_disc_rear_carrier", "anodized_gold"),
    ("brake_disc_rear_hub", "anodized_clear"),
    ("brake_disc_rear_bobbin_*", "stainless_polished"),
    ("brake_disc_*", "disc_iron"),
    ("brake_pad_*", "brake_pad_friction"),
    ("brake_line_*", "brake_line_red"),
    ("steering_boss", "anodized_clear"),
    ("steering_spokes", "anodized_clear"),
    ("steering_bearing", "anodized_clear"),
    ("steering_hub*", "anodized_clear"),
    ("steering_pitman", "anodized_clear"),
    ("steering_clutch_lever", "anodized_clear"),
    ("steering_rim", "rubber_grip"),
    ("shifter_base", "anodized_clear"),
    ("shifter_lever", "anodized_clear"),
    ("shifter_connector_arm", "anodized_clear"),
    ("shift_rod_end_*", "anodized_black"),
    ("shift_rod", "zinc_plated"),
    # Bright chrome or zinc-plated rod with black-anodized rod ends, detail7.
    ("tierod_end_*", "anodized_black"),
    ("tierod_*", "zinc_plated"),
    ("knuckle*", "anodized_clear"),
    ("axle_cassette_*", "anodized_clear"),
    ("axle_stub_*", "zinc_plated"),
    ("kingpin*", "zinc_plated"),
    # Gold-anodized hub collars, confirmed on the CRG front hub in detail7.
    ("hub_*", "anodized_gold"),
    ("drive_sprocket_carrier", "anodized_clear"),
    # --- §60.3.5 the chain and the sprockets, off the axle's value ---
    ("drive_chain", "chain_oiled"),
    ("drive_output_sprocket", "sprocket_steel"),
    ("axle_sprocket", "sprocket_steel"),
    ("engine_plug_hex", "black_oxide"),
    # --- §60.3.11 rubber, which was ten parts on one grip material ---
    ("engine_plug_lead", "rubber_gloss"),
    ("engine_plug_cap", "rubber_gloss"),
    ("engine_intake_boot", "rubber_matte"),
    ("engine_throttle_cable", "rubber_gloss"),
    ("fuel_line_*", "rubber_gloss"),
    # --- §60.3.12 the fuel tank ---
    ("fuel_tank_fitting_*", "anodized_clear"),
    ("fuel_tank_strap_*", "rubber_grip"),
    ("fuel_tank_mount_*", "zinc_plated"),
    ("fuel_tank_filler", "tank_polyethylene"),
    ("fuel_tank", "tank_polyethylene"),
    # --- §60.4.5 the rear protection's two adjustable outer parts ---
    # Art. 9.5.5.1: they must be "clearly different" in color from the main part.
    # Wave 2 had to borrow `seat_fiberglass` because `MATERIALS` had no contrasting
    # bodywork slot at all. It has one now.
    ("bodywork_rear_outer_*", "bodywork_contrast"),
    # --- §60.5's accent, on the only accent surfaces that exist as geometry ---
    # The pinstripe of §60.3.2 and §60.5.2 is a printed decal on the tray, and a
    # decal is a texture this pipeline cannot write; the tray's own edge rails are
    # the nearest thing to it that is a mesh. `shifter_knob` is a livery variable in
    # §60.3.11's own words. Both are `estimated` placements and are the reason
    # `livery_accent` is not a material with nothing on it.
    ("chassis_tray_edge_*", "livery_accent"),
    ("shifter_knob", "livery_accent"),
)

#: Second material slots assigned per face, as (glob, material, rule).
#:
#: A slot per face is the only way to put a livery *zone* on a part whose geometry
#: another module owns. Two rules are implemented and both are geometric — no
#: texture, no UV authoring — so they are as deterministic as the mesh is:
#:
#:   `tread`     the tire's crown, by lateral position and face normal. §60.3.3
#:               wants roughness 0.62 on the tread against 0.72 on the sidewall,
#:               and roughness is not something a vertex color can carry.
#:   `number`    the vertical outboard face over the rear fraction of a panel,
#:               which is exactly where Art. 9.5.4 puts a number: "on the vertical
#:               surface close to the rear wheels".
#:
#: The **glyph is not here.** Art. 3.7 requires black Arial on the yellow field, at
#: >=150 mm cap height with a >=20 mm stroke for a short circuit — and §60.4.3's
#: arithmetic makes that Arial *Bold*, since Regular's stem is ~19.5 mm at that cap
#: height and only just clears. A glyph is a texture and this pipeline has no
#: texture stage: the bake writes one normal atlas and nothing writes an albedo.
#: So the field is placed and measured and the glyph is owed. Said plainly rather
#: than left to be discovered in a render.
ZONES: tuple[tuple[str, str, str], ...] = (
    ("wheel_*_tire", "tire_tread", "tread"),
    # §60.4.4: one large field at the rear-outboard end of each pod. Wave 2
    # measured a legal 170 mm zone fitting the pods with 10 mm and 7 mm of margin.
    ("bodywork_sidepod_l", "number_yellow", "number"),
    ("bodywork_sidepod_r", "number_yellow", "number"),
    # The main (central) part of the rear protection, 7 mm of margin per wave 2.
    # It has to be the main part: the two outer parts are committed to the
    # contrast color and are separate objects.
    ("bodywork_rear_panel", "number_yellow", "number"),
    # **Not the front panel.** §60.4.3: a three-digit zone is 370 mm wide against
    # Art. 9.5.3's 300 mm maximum panel width, so it does not fit at all, and two
    # digits at 253 mm do not fit a 250 mm panel either. Numbers go where they fit.
    #
    # §60.3.6's second finish, last so the number field wins where they overlap: the
    # unwrapped underside of a panel is the moulded substrate, satin and flat, with
    # none of the wrap's clear-coat highlight. Downward-facing faces only — a
    # mounting flange is not identifiable from a face normal, so the flanges are
    # still wrapped and that is a known gap rather than a claim.
    ("bodywork_nose_fairing", "bodywork_substrate", "underside"),
    ("bodywork_front_panel", "bodywork_substrate", "underside"),
    ("bodywork_rear_panel", "bodywork_substrate", "underside"),
    ("bodywork_sidepod_l", "bodywork_substrate", "underside"),
    ("bodywork_sidepod_r", "bodywork_substrate", "underside"),
    ("bodywork_rear_outer_*", "bodywork_substrate", "underside"),
)

#: Vertex-color tints, as (glob, rule, t_start, t_end).
#:
#: Wear that varies *across* one part cannot be a material value, and §60.3.9 says
#: so in as many words about the exhaust: "a per-part gradient texture or a vertex
#: color ramp, not a material value". A vertex color multiplies base color through
#: glTF's COLOR_0 and Godot's `vertex_color_use_as_albedo`, so it is the cheapest
#: thing here that varies within a mesh.
#:
#: **Three wear claims are implemented and they are the three §60.3.13 has a
#: photograph for.** The other six — chain, tray heel scuffs, rim brake dust, bodywork
#: chips, frame chips, and the disc's rust bloom as a distinct claim — have nothing
#: behind them, because every chassis and bodywork photograph in this repo is a
#: studio shot of a brand-new kart. §5 item 10 applies to a scuff exactly as it
#: applies to a sprocket, so they are not invented here. They are listed in the
#: wave report as a fetch job.
#:
#: `t_start`/`t_end` place a part on a ramp that spans several parts: the exhaust
#: runs manifold -> chamber -> connector -> silencer and the gradient must not
#: restart at each object. The fractions are a statement about the pipe's own
#: layout and are reviewable against it.
TINTS: tuple[tuple[str, str, float, float], ...] = (
    # §60.3.9, `partly sourced`: exh_commons_shifter_engine.jpg shows a used,
    # discolored header — the gradient's existence. Its banding *sequence* is
    # `estimated` from general steel-oxide temper colors; no photograph in this
    # repo resolves it on a kart pipe, and the header in that frame is heat-wrapped,
    # which hides the gradient rather than showing it.
    ("exhaust_manifold", "heat", 0.00, 0.04),
    ("exhaust_manifold_spigot", "heat", 0.02, 0.07),
    ("exhaust_chamber", "heat", 0.07, 0.62),
    ("exhaust_connector", "heat", 0.62, 0.80),
    ("exhaust_silencer", "heat", 0.80, 1.00),
    # §60.3.10, and §60 flags this as the weakest evidence in the section: both
    # reference discs are brand new and show no swept band at all. What is
    # implemented is only the geometry of it — a swept annulus inside the pad track
    # against a darker, warmer bloom outside — and it is `estimated`.
    ("brake_disc_fl", "swept", 0.0, 1.0),
    ("brake_disc_fr", "swept", 0.0, 1.0),
    ("brake_disc_rear", "swept", 0.0, 1.0),
    # **No tire entry, and the reason is measured.** §60.3.3's worked band and its
    # marble-picking shoulders are `sourced` off exh_commons_buntschu_kz2.jpg, and a
    # lateral vertex ramp cannot express either: the tire lathe puts vertices only at
    # the ends of its profile, so every vertex of the tread sits at the shoulder and
    # a rule keyed on lateral position returned a uniform 1.0 over all 862 of them.
    # The `worked` rule below is kept because it is correct and one interior vertex
    # ring would make it work. What carries the claim instead is the `tread` zone —
    # a second material slot, which is per *face* and does land — so the tread's
    # roughness and its cooler cast are on `tire_tread` and the shoulders' warmth is
    # a texture that is owed.
)

#: The heat ramp of §60.3.9, as (t, multiplier hex) over `exhaust_nickel`.
#:
#: t = 0 is the flange, hottest, plating burnt off. t = 1 is the silencer, coolest,
#: bright nickel and therefore no tint at all. The multiplier is what turns one
#: base color into the four-band sequence the spec tabulates.
HEAT_RAMP: tuple[tuple[float, str], ...] = (
    (0.00, "#6b5a4e"),  # matte grey-brown, the darkest metal on the kart
    (0.20, "#5a5a86"),  # blue-purple oxide band
    (0.45, "#b8933f"),  # straw and gold
    (0.70, "#d8cfa8"),  # pale straw
    (1.00, "#ffffff"),  # bright nickel, near-chrome
)

#: Where the pad track sits on a disc, as a fraction of its outer radius.
#: `estimated`: a kart pad is roughly a third of the disc's radius, set in from the
#: edge. The bloom outside it and the darker hub area inside are the same estimate.
DISC_SWEPT_BAND: tuple[float, float] = (0.60, 0.94)

#: Fraction of a panel's fore-aft extent, from the rear, that a number may occupy.
#:
#: §60.4.4 reads the Birel ART `CL FL AERO` pod decal as a trapezoid over "the
#: rearmost ~35% of its length". 0.45 rather than 0.35 because 35% of this kart's
#: 605 mm pod is 212 mm and §60.4.3's two-digit field is 253 mm wide — at 35% the
#: pods cannot carry a legal number at all. Widening the *search* does not widen the
#: field: the crop below still cuts it to 370 x 170.
NUMBER_ZONE_REAR_FRACTION = 0.45

#: The number field, in meters, from §60.4.3: 350 mm of Arial Bold three-digit glyph
#: block plus Art. 3.7's 10 mm of yellow all round, by 150 mm of cap height plus the
#: same border. `derived`, and the derivation is in that subsection.
NUMBER_FIELD_SIZE: tuple[float, float] = (0.370, 0.170)

#: Fraction of the tire's half width that is crown rather than sidewall.
#: `derived`: the tread reaches every surface that touches the road, so the band
#: is the sourced tread width over the overall width -- 179/215 = 0.83 rear,
#: 110/135 = 0.81 front. 0.80 selects exactly the crown faces at both detail
#: levels under the face-center rule below (last rear crown face center sits at
#: 0.70 low / 0.76 high, first shoulder face at 0.84; front 0.68/0.78 vs 0.83)
#: with >=0.02 margin on all eight cases. Replaces an `estimated` 0.62 that cut
#: the outer tread face ring at low detail and drew the worked band 25% narrow.
TIRE_TREAD_FRACTION = 0.80

#: What a livery role's material is, for the default livery, in the old
#: `(color linear, roughness, metallic)` shape. Kept because the name is part of
#: this module's surface; `make_materials` builds from the tables above.
MATERIALS: dict[str, tuple[tuple[float, float, float], float, float]] = {}

#: The material set for this run, so `object_from_bmesh` can reach it.
#:
#: A per-run singleton, and that is not a shortcut: `genkart.py.prepare_scene`
#: creates the materials exactly once per process — its docstring says why, the
#: kart is built twice and calling `make_materials` twice would leave two of
#: everything with Blender's `.001` suffixes attached — so there is exactly one
#: table to point at. The alternative was threading a `BuildContext` through
#: `object_from_bmesh`, which every geometry module calls and none of them owns.
_MATERIALS: dict[str, bpy.types.Material] = {}
_LIVERY: str = LIVERY_DEFAULT

#: What each zone assignment actually covered, as
#: `(part, material, faces, area m2, (dx, dy, dz))`.
#:
#: Measured rather than intended. Art. 3.7 needs 370 x 170 mm for a three-digit
#: number and 253 x 170 for two digits, and a face-normal rule gives whatever the
#: mesh gives — so the size the rule *achieved* is printed and compared against
#: those two figures instead of being assumed to have worked.
ZONE_REPORT: list[tuple[str, str, int, float, tuple[float, float, float]]] = []


def srgb_to_linear(value: float) -> float:
    """One sRGB channel in 0-1 to linear. The IEC 61966-2-1 transfer function."""
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


def linear_from_hex(text: str) -> tuple[float, float, float]:
    """`#rrggbb` to a linear triple, unnormalized."""
    digits = text.lstrip("#")
    if len(digits) != 6:
        raise ValueError("expected #rrggbb, got %r" % text)
    return tuple(  # type: ignore[return-value]
        srgb_to_linear(int(digits[index : index + 2], 16) / 255.0)
        for index in (0, 2, 4)
    )


def luminance(color: tuple[float, float, float]) -> float:
    """Rec. 709 relative luminance of a linear triple."""
    return 0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2]


def base_color(hex_srgb: str, target: float) -> tuple[float, float, float]:
    """A linear base color with `hex_srgb`'s hue and `target`'s luminance.

    The whole point of the split described at the top of this section: the hue is
    §60's measurement off a photograph, the luminance is this project's own
    verified scale. A hex whose luminance is zero cannot be scaled, so pure black
    is returned as the target on every channel — a neutral of that value.
    """
    color = linear_from_hex(hex_srgb)
    present = luminance(color)
    if present <= 0.0:
        return (target, target, target)
    factor = target / present
    return (color[0] * factor, color[1] * factor, color[2] * factor)


def livery_table(livery: str) -> dict[str, Finish]:
    """Every finish for one livery, fixed and livery-driven together."""
    if livery not in LIVERIES:
        raise KeyError(
            "unknown livery %r; the liveries are %s"
            % (livery, ", ".join(sorted(LIVERIES)))
        )
    table = dict(FIXED_FINISHES)
    palette = LIVERIES[livery]
    for role in LIVERY_ROLES:
        hex_srgb, value = palette[role]
        roughness, metallic = LIVERY_SURFACE[role]
        table[LIVERY_MATERIALS[role]] = (hex_srgb, value, roughness, metallic)
    return table


def selected_livery(argv: Sequence[str] | None = None) -> str:
    """The livery named by `--livery=`, or the default.

    Read off `sys.argv` rather than taken as an argument because `make_materials`
    is called by `genkart.py`, which this wave does not own. `genkart.sh` forwards
    every unrecognized argument verbatim and `genkart.py`'s parser collects unknown
    keys without complaint, so `--livery=factory` reaches here intact and a still
    of a variant stays reproducible from its command.
    """
    source = list(sys.argv if argv is None else argv)
    chosen = LIVERY_DEFAULT
    for argument in source:
        if argument.startswith("--livery="):
            chosen = argument.split("=", 1)[1].strip()
    if chosen not in LIVERIES:
        raise SystemExit(
            "unknown livery %r; the liveries are %s"
            % (chosen, ", ".join(sorted(LIVERIES)))
        )
    return chosen


def _rebuild_materials_view(livery: str) -> None:
    """Refresh the module-level `MATERIALS` mirror for `livery`."""
    MATERIALS.clear()
    for name, (hex_srgb, value, roughness, metallic) in livery_table(livery).items():
        MATERIALS[name] = (base_color(hex_srgb, value), roughness, metallic)


_rebuild_materials_view(LIVERY_DEFAULT)


def make_materials(livery: str | None = None) -> dict[str, bpy.types.Material]:
    """Create every material once, in a fixed order, and return them by name.

    Also reports the livery and the Art. 9.5.5.1 contrast margin, because that
    margin is the only regulation constraint in this file that a palette edit can
    break and §60.4.5 asked for it as a number rather than a judgment.
    """
    global _LIVERY
    _LIVERY = selected_livery() if livery is None else livery
    _rebuild_materials_view(_LIVERY)
    _MATERIALS.clear()

    created: dict[str, bpy.types.Material] = {}
    for name in sorted(MATERIALS):
        color, roughness, metallic = MATERIALS[name]
        material = bpy.data.materials.new(name)
        material.use_nodes = True
        principled = material.node_tree.nodes["Principled BSDF"]
        principled.inputs["Base Color"].default_value = (*color, 1.0)
        principled.inputs["Roughness"].default_value = roughness
        principled.inputs["Metallic"].default_value = metallic
        created[name] = material
    _MATERIALS.update(created)

    for name in TINT_MATERIALS:
        _attach_vertex_tint(created[name])

    main = luminance(MATERIALS["bodywork_wrap"][0])
    outer = luminance(MATERIALS["bodywork_contrast"][0])
    margin = abs(main - outer)
    print(
        "    livery   %s: %d material(s); Art. 9.5.5.1 rear outer contrast "
        "%.3f vs main %.3f, margin %.3f (>= 0.250 %s)"
        % (
            _LIVERY,
            len(created),
            outer,
            main,
            margin,
            "ok" if margin >= 0.250 else "FAIL",
        )
    )
    return created


#: Materials a `TINTS` rule lands on, and therefore the materials that need the
#: Color Attribute node. Listed rather than derived: resolving a glob of part names
#: through `FINISHES` and `ZONES` to work it out would be a
#: second copy of the resolution order, and the two copies would disagree.
#: `_check_tables` asserts this list covers every tinted part instead.
TINT_MATERIALS: tuple[str, ...] = (
    "disc_iron",
    "exhaust_nickel",
)


def _attach_vertex_tint(material: bpy.types.Material) -> None:
    """Multiply the material's base color by the `Col` vertex color.

    The exporter recognizes exactly this arrangement — a Color Attribute through a
    MULTIPLY mix into Base Color — and writes COLOR_0 plus a base-color factor,
    which Godot's glTF importer turns into `vertex_color_use_as_albedo`. A mesh with
    no `Col` layer renders unchanged: glTF's default vertex color is opaque white
    and white is the identity of a multiply.
    """
    tree = material.node_tree
    principled = tree.nodes.get("Principled BSDF")
    if principled is None:
        return
    color = principled.inputs["Base Color"].default_value
    base = (color[0], color[1], color[2], 1.0)

    attribute = tree.nodes.new("ShaderNodeVertexColor")
    attribute.layer_name = VERTEX_TINT_LAYER
    attribute.location = (-620.0, 260.0)

    mix = tree.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.location = (-320.0, 260.0)
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs[6].default_value = base
    tree.links.new(attribute.outputs["Color"], mix.inputs[7])
    tree.links.new(mix.outputs[2], principled.inputs["Base Color"])


#: Name of the vertex color layer the tints write and the materials read.
VERTEX_TINT_LAYER = "Col"


# --- applying a finish to one object ---------------------------------------


def finish_override(name: str) -> str | None:
    """The material `FINISHES` assigns to this object name, if any. First match."""
    for pattern, material in FINISHES:
        if fnmatch.fnmatchcase(name, pattern):
            return material
    return None


def apply_finish(obj: bpy.types.Object) -> None:
    """Override the object's material, add its zone slots, write its tint.

    Called from the two places an object comes into existence — `object_from_bmesh`
    and `mirror_x` — so a mirrored part gets its own lateral zones and its own tint
    rather than inheriting the ones computed for the other side of the kart.
    Idempotent, because both paths may run over the same mesh data.
    """
    if not _MATERIALS or obj.type != "MESH":
        return

    override = finish_override(obj.name)
    if override is not None:
        material = _MATERIALS[override]
        if obj.data.materials:
            obj.data.materials[0] = material
        else:
            obj.data.materials.append(material)

    _apply_zones(obj)
    _apply_tint(obj)


def _bounds(mesh: bpy.types.Mesh) -> tuple[Vector, Vector]:
    """Local-space min and max corner of a mesh's vertices."""
    low = Vector((float("inf"),) * 3)
    high = Vector((float("-inf"),) * 3)
    for vertex in mesh.vertices:
        for axis in range(3):
            low[axis] = min(low[axis], vertex.co[axis])
            high[axis] = max(high[axis], vertex.co[axis])
    return low, high


def _apply_zones(obj: bpy.types.Object) -> None:
    """Give matching faces a second material slot, per `ZONES`.

    A face already claimed by an earlier rule is left alone, so `ZONES` order is a
    priority: the number field is regulated and the substrate is not, so the number
    field is listed first and keeps its faces.
    """
    mesh = obj.data
    for pattern, material_name, rule in ZONES:
        if not fnmatch.fnmatchcase(obj.name, pattern):
            continue
        material = _MATERIALS[material_name]
        if material.name in {slot.name for slot in mesh.materials if slot}:
            continue
        selected = [
            face
            for face in _zone_faces(mesh, rule)
            if mesh.polygons[face].material_index == 0
        ]
        if not selected:
            continue
        mesh.materials.append(material)
        index = len(mesh.materials) - 1
        area = 0.0
        low = Vector((float("inf"),) * 3)
        high = Vector((float("-inf"),) * 3)
        for face in selected:
            polygon = mesh.polygons[face]
            polygon.material_index = index
            area += polygon.area
            for loop_index in polygon.loop_indices:
                position = mesh.vertices[mesh.loops[loop_index].vertex_index].co
                for axis in range(3):
                    low[axis] = min(low[axis], position[axis])
                    high[axis] = max(high[axis], position[axis])
        ZONE_REPORT.append(
            (
                obj.name,
                material_name,
                len(selected),
                area,
                (high.x - low.x, high.y - low.y, high.z - low.z),
            )
        )


def _zone_faces(mesh: bpy.types.Mesh, rule: str) -> list[int]:
    """Face indices the zone rule selects, in ascending order."""
    low, high = _bounds(mesh)
    chosen: list[int] = []

    if rule == "tread":
        axis = revolution_axis(low, high)
        center = (low[axis] + high[axis]) * 0.5
        half = max(1e-9, (high[axis] - low[axis]) * 0.5)
        for polygon in mesh.polygons:
            offset = abs(polygon.center[axis] - center) / half
            # The crown, and only the faces that actually face outward from the
            # axle: a tire's inner bead faces are inside the rim and never touch
            # the road, so they keep the sidewall's roughness.
            if offset <= TIRE_TREAD_FRACTION and abs(polygon.normal[axis]) < 0.5:
                chosen.append(polygon.index)
        return chosen

    if rule == "underside":
        for polygon in mesh.polygons:
            if polygon.normal.z < -0.35:
                chosen.append(polygon.index)
        return chosen

    if rule == "number":
        # Art. 9.5.4: "the vertical surface close to the rear wheels", and Art. 3.7
        # wants it "clearly visible to Timekeepers and Officials", so: the rear
        # `NUMBER_ZONE_REAR_FRACTION` of the part's own fore-aft extent, and only
        # faces standing close enough to vertical to read off-axis. Blender +Y is
        # forward, so the rear of a part is its minimum y.
        #
        # Then **cropped to a field of the regulation size**, centered on what that
        # leaves. Without the crop the rule took the rear panel's entire 810 mm
        # vertical face — a whole panel painted yellow, not a number field. The
        # field is a rectangle, so it is cropped as one: the two largest extents of
        # the candidate region get the 370 x 170 mm window of §60.4.3, clamped to
        # whatever the panel actually offers.
        span = max(1e-9, high.y - low.y)
        cut = low.y + span * NUMBER_ZONE_REAR_FRACTION
        candidates = [
            polygon.index
            for polygon in mesh.polygons
            if polygon.center.y <= cut and abs(polygon.normal.z) <= 0.5
        ]
        if not candidates:
            return []
        return _crop_to_field(mesh, candidates)

    raise KeyError("unknown zone rule %r" % rule)


def _crop_to_field(mesh: bpy.types.Mesh, candidates: list[int]) -> list[int]:
    """Keep the candidate faces inside a centered field of the regulation size."""
    low = Vector((float("inf"),) * 3)
    high = Vector((float("-inf"),) * 3)
    for index in candidates:
        center = mesh.polygons[index].center
        for axis in range(3):
            low[axis] = min(low[axis], center[axis])
            high[axis] = max(high[axis], center[axis])

    extents = [(high[axis] - low[axis], axis) for axis in range(3)]
    extents.sort(reverse=True)
    wanted = {
        extents[0][1]: NUMBER_FIELD_SIZE[0],
        extents[1][1]: NUMBER_FIELD_SIZE[1],
    }

    limits: dict[int, tuple[float, float]] = {}
    for axis, size in sorted(wanted.items()):
        middle = (low[axis] + high[axis]) * 0.5
        half = min(size, high[axis] - low[axis]) * 0.5
        limits[axis] = (middle - half, middle + half)

    kept: list[int] = []
    for index in candidates:
        center = mesh.polygons[index].center
        inside = True
        for axis, (lower, upper) in limits.items():
            # A half-texel of slack, so a face whose center lands exactly on the
            # boundary does not fall in or out on a float comparison.
            if center[axis] < lower - 1e-6 or center[axis] > upper + 1e-6:
                inside = False
                break
        if inside:
            kept.append(index)
    return kept


def _apply_tint(obj: bpy.types.Object) -> None:
    """Write the `Col` vertex color layer, per `TINTS`."""
    mesh = obj.data
    for pattern, rule, start, end in TINTS:
        if not fnmatch.fnmatchcase(obj.name, pattern):
            continue
        layer = mesh.color_attributes.get(VERTEX_TINT_LAYER)
        if layer is None:
            layer = mesh.color_attributes.new(
                name=VERTEX_TINT_LAYER, type="FLOAT_COLOR", domain="POINT"
            )
        low, high = _bounds(mesh)
        for index, vertex in enumerate(mesh.vertices):
            color = _tint_color(rule, vertex.co, low, high, start, end)
            layer.data[index].color = (*color, 1.0)
        return


def _tint_color(
    rule: str,
    position: Vector,
    low: Vector,
    high: Vector,
    start: float,
    end: float,
) -> tuple[float, float, float]:
    """The multiplier at one vertex."""
    if rule == "heat":
        # Along the pipe. The chamber's inlet axis is (0, -1, 0) — powertrain's own
        # `_exhaust_centerline` — so the flange end of every exhaust part is its
        # maximum y and the pipe runs toward minimum y. Hot at max y.
        span = max(1e-9, high.y - low.y)
        local = (high.y - position.y) / span
        return _ramp(HEAT_RAMP, start + (end - start) * min(1.0, max(0.0, local)))

    if rule == "swept":
        # Radial, in the plane normal to the disc's own axis of revolution.
        #
        # **A ramp, not the band §60.3.10 describes, and the reason is the mesh.**
        # A brake disc here is a lathe with vertices at two radii only — the carrier
        # bore and the outer edge — so a vertex color has no vertex anywhere inside
        # the pad track to be bright at. Measured: the band rule produced exactly two
        # distinct colors and neither was the swept one. The band needs a texture or a
        # third vertex ring, which is `bodywork.py`'s to add; see the wave report.
        # What is here is the part of the claim two rings *can* carry — dark toward
        # the hub, bright toward the swept outer face.
        axis = revolution_axis(low, high)
        plane = [index for index in range(3) if index != axis]
        center = [(low[index] + high[index]) * 0.5 for index in plane]
        outer = max(
            1e-9,
            max(high[plane[0]] - center[0], high[plane[1]] - center[1]),
        )
        radius = min(
            1.0,
            math.hypot(
                position[plane[0]] - center[0], position[plane[1]] - center[1]
            )
            / outer,
        )
        inner, _outer_edge = DISC_SWEPT_BAND
        weight = min(1.0, max(0.0, (radius - inner) / max(1e-9, 1.0 - inner)))
        dark = (0.46, 0.46, 0.47)
        bright = (1.0, 1.0, 1.0)
        return tuple(  # type: ignore[return-value]
            dark[index] + (bright[index] - dark[index]) * weight for index in range(3)
        )

    if rule == "worked":
        # Across the tread. The crown's worked band carries a bluish sheen; the
        # shoulders pick up marbles and read warmer and darker. buntschu, §60.3.3.
        axis = revolution_axis(low, high)
        center = (low[axis] + high[axis]) * 0.5
        half = max(1e-9, (high[axis] - low[axis]) * 0.5)
        offset = abs(position[axis] - center) / half
        if offset <= TIRE_TREAD_FRACTION * 0.75:
            return (0.94, 0.97, 1.00)
        if offset <= TIRE_TREAD_FRACTION:
            return (1.00, 0.92, 0.84)
        return (1.0, 1.0, 1.0)

    raise KeyError("unknown tint rule %r" % rule)


def revolution_axis(low: Vector, high: Vector) -> int:
    """Which local axis a solid of revolution turns about: its shortest extent.

    Derived from the mesh rather than assumed, because it cannot be assumed. A tire
    and a brake disc are both built as a lathe and *then* rotated onto the hub, and
    `apply_finish` runs at creation, before the rotation — so at tint time a wheel's
    axis is wherever its module happened to lathe it. Reading the axis off the
    bounding box costs nothing and is right in every orientation: a tire is
    width x D x D and a disc is thickness x D x D, so in both the axis is the one
    extent that is not the diameter.
    """
    extents = [(high[index] - low[index], index) for index in range(3)]
    extents.sort()
    return extents[0][1]


def _ramp(stops: tuple[tuple[float, str], ...], t: float) -> tuple[float, float, float]:
    """Linear interpolation between hex stops, in linear space."""
    if t <= stops[0][0]:
        return linear_from_hex(stops[0][1])
    for index in range(1, len(stops)):
        upper_t, upper_hex = stops[index]
        if t <= upper_t:
            lower_t, lower_hex = stops[index - 1]
            weight = (t - lower_t) / max(1e-9, upper_t - lower_t)
            a = linear_from_hex(lower_hex)
            b = linear_from_hex(upper_hex)
            return tuple(  # type: ignore[return-value]
                a[axis] + (b[axis] - a[axis]) * weight for axis in range(3)
            )
    return linear_from_hex(stops[-1][1])


def check_finish_tables(names: Iterable[str]) -> None:
    """Fail the build if a table entry matches no part, or points at no material.

    Same rule as `joints.py`'s "a pattern that matches nothing is fatal", and for
    the same reason: object names are frozen by determinism rule 3 and are the
    export's node names, so a stale pattern does not error — the part quietly keeps
    whatever finish its geometry module happened to ask for, and the finish work
    silently un-does itself one rename at a time. Exactly the shape of the
    `Dictionary.get(key, default)` failure CLAUDE.md records.
    """
    part_names = sorted(names)
    problems: list[str] = []

    for pattern, material in FINISHES:
        if material not in MATERIALS:
            problems.append("FINISHES %r names unknown material %r" % (pattern, material))
        if not any(fnmatch.fnmatchcase(name, pattern) for name in part_names):
            problems.append("FINISHES pattern %r matches no part" % pattern)

    for pattern, material, rule in ZONES:
        if material not in MATERIALS:
            problems.append("ZONES %r names unknown material %r" % (pattern, material))
        if not any(fnmatch.fnmatchcase(name, pattern) for name in part_names):
            problems.append("ZONES pattern %r matches no part" % pattern)
        del rule

    tinted: list[str] = []
    for pattern, rule, _start, _end in TINTS:
        matched = [name for name in part_names if fnmatch.fnmatchcase(name, pattern)]
        if not matched:
            problems.append("TINTS pattern %r matches no part" % pattern)
        tinted.extend(matched)
        del rule

    for name in TINT_MATERIALS:
        if name not in MATERIALS:
            problems.append("TINT_MATERIALS names unknown material %r" % name)

    if problems:
        raise SystemExit(
            "finish tables are stale:\n    " + "\n    ".join(problems)
        )

    print(
        "    finish   %d override(s), %d zone rule(s), %d tinted part(s); "
        "every pattern matches"
        % (len(FINISHES), len(ZONES), len(set(tinted)))
    )
    report_zones()


def report_zones() -> None:
    """What each zone covered, in millimeters, against Art. 3.7's two figures.

    Deduped on (part, material) keeping the last entry, because the kart is built
    twice per run — high-poly bake source first, low-poly second — and the numbers
    that matter are the game mesh's.
    """
    latest: dict[tuple[str, str], tuple[int, float, tuple[float, float, float]]] = {}
    for part, material, faces, area, extent in ZONE_REPORT:
        latest[(part, material)] = (faces, area, extent)

    for key in sorted(latest):
        part, material = key
        faces, area, extent = latest[key]
        dx, dy, dz = (value * 1000.0 for value in extent)
        note = ""
        if material == "number_yellow":
            # Art. 3.7 short circuit: >=150 mm cap height, >=20 mm stroke, >=10 mm
            # of yellow all round. §60.4.3 derives 370 x 170 for three digits and
            # 253 x 170 for two. Compared against the field's two largest extents,
            # because which pair of axes the field spans is the panel's business.
            spans = sorted((dx, dy, dz), reverse=True)
            # 1 mm of slack. The measured span is a vertex bounding box over faces
            # selected by their centers, so it lands a fraction of a face short of
            # the window it was cropped to and a hard >= turns a legal field into a
            # failure on rounding.
            if spans[0] >= 369.0 and spans[1] >= 169.0:
                note = "  fits 3 digits (370x170)"
            elif spans[0] >= 252.0 and spans[1] >= 169.0:
                note = "  fits 2 digits (253x170), not 3"
            else:
                note = "  BELOW Art. 3.7 minimum (%.0f x %.0f)" % (spans[0], spans[1])
        print(
            "    zone     %-24s %-18s %3d face(s), %6.1f cm2, "
            "%.0f x %.0f x %.0f mm%s"
            % (part, material, faces, area * 1.0e4, dx, dy, dz, note)
        )


def checker_image(name: str, size: int, cell_pixels: int) -> bpy.types.Image:
    """A checkerboard for reading UV texel density off a render.

    Issue #18 asks for density "checked against a checker pattern", and the
    check only works if the pattern's world size is known: at the §5 density of
    512 px/m, a `cell_pixels`-wide cell covers `cell_pixels / 512` meters. Cells
    that are visibly different sizes on two parts of the kart are a density
    mismatch, which is the tell §5 item 2 names as the most common in amateur
    work.
    """
    image = bpy.data.images.new(name, size, size, alpha=False, float_buffer=False)
    pixels = [0.0] * (size * size * 4)
    for y in range(size):
        for x in range(size):
            dark = ((x // cell_pixels) + (y // cell_pixels)) % 2 == 0
            value = 0.055 if dark else 0.520
            index = (y * size + x) * 4
            pixels[index] = value
            pixels[index + 1] = value
            pixels[index + 2] = value
            pixels[index + 3] = 1.0
    image.pixels = pixels
    return image


# --- selection -------------------------------------------------------------


def select_only(objects: Iterable[bpy.types.Object], active: bpy.types.Object | None = None) -> None:
    """Deselect everything, then select exactly `objects`.

    Rule 2 in this module's docstring: the operators that cannot be avoided —
    UV unwrap, bake, glTF export with `use_selection` — read the selection, so
    it is always set immediately before the call and never inherited from
    whatever ran previously.
    """
    # Guarded against None: deleting objects leaves stale entries in the view
    # layer's list until the depsgraph catches up, and iterating it straight after
    # a deletion yields them. The bake's high-poly source is discarded right before
    # the export selects everything, which is exactly that window.
    for obj in bpy.context.view_layer.objects:
        if obj is None:
            continue
        obj.select_set(False)
    chosen = list(objects)
    for obj in chosen:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = active if active is not None else (
        chosen[0] if chosen else None
    )


def surface_area(obj: bpy.types.Object) -> float:
    """Total polygon area in square meters, for texel-density arithmetic."""
    return float(sum(polygon.area for polygon in obj.data.polygons))
