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
import math
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
        if lower_is_point:
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((lower, upper[step], upper[following]))
        elif upper_is_point:
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((lower[following], lower[step], upper))
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
    faces = (
        (0, 1, 3, 2),  # -Z
        (4, 6, 7, 5),  # +Z
        (0, 4, 5, 1),  # -Y
        (2, 3, 7, 6),  # +Y
        (0, 2, 6, 4),  # -X
        (1, 5, 7, 3),  # +X
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
    return obj


# --- materials -------------------------------------------------------------

#: Placeholder materials for M2, as (base color linear, roughness, metallic).
#:
#: M2's job is the geometry pipeline, so these are plausible values rather than
#: measured ones — ARCHITECTURE.md §5 item 3 wants photoscans, and the kart gets
#: them when it gets textures. What matters here is that a named material per
#: surface type survives the glTF round trip, which is issue #21's third
#: acceptance criterion, and that a UV checker can be swapped onto any of them.
#:
#: Values are linear, not sRGB. glTF stores base color linear and Godot reads it
#: that way, so writing an sRGB number here would come out visibly pale.
MATERIALS: dict[str, tuple[tuple[float, float, float], float, float]] = {
    "frame_powdercoat": ((0.035, 0.045, 0.060), 0.42, 0.0),
    # Anodized and scuffed, not polished. A fully bright metal at low roughness
    # renders as a white mirror slab that swamps the frame it is bolted to, and
    # the tray is the largest single surface on the kart — it sets the read.
    "tray_aluminium": ((0.145, 0.150, 0.158), 0.58, 1.0),
    "tire_rubber": ((0.016, 0.016, 0.017), 0.72, 0.0),
    "rim_magnesium": ((0.380, 0.375, 0.360), 0.30, 1.0),
    # The rear axle is heat-treated steel, not polished bar. At `engine_alloy`'s
    # brightness a 50 mm full-width cylinder under a 100,000 lx sun read as a
    # chrome pole and became the most eye-catching thing on the kart, which is not
    # what the axle is. Dark and satin instead.
    "axle_steel": ((0.075, 0.075, 0.080), 0.42, 1.0),
    "bodywork_plastic": ((0.520, 0.045, 0.035), 0.28, 0.0),
    "seat_fiberglass": ((0.055, 0.055, 0.058), 0.38, 0.0),
    "engine_alloy": ((0.290, 0.290, 0.295), 0.46, 1.0),
    "exhaust_steel": ((0.420, 0.420, 0.430), 0.24, 1.0),
    "radiator_core": ((0.120, 0.120, 0.125), 0.60, 1.0),
    "suit_fabric": ((0.045, 0.075, 0.180), 0.68, 0.0),
    "helmet_shell": ((0.640, 0.620, 0.600), 0.14, 0.0),
    "rubber_grip": ((0.028, 0.028, 0.030), 0.66, 0.0),
}


def make_materials() -> dict[str, bpy.types.Material]:
    """Create every material once, in a fixed order, and return them by name."""
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
    return created


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
