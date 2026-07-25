"""Issue #18 — UV unwrap at a fixed texel density.

Runs after every geometry module, over whatever they built. It is deliberately
generic: it reads objects out of the collections rather than knowing what a kart
is, so a new part gets unwrapped by existing to be unwrapped and nobody has to
remember to add it here.

## The density standard is the whole point

`ARCHITECTURE.md` §5 item 2 names mismatched texel density as the most common tell
in amateur work, and fixes a standard to avoid it. A standard only helps if it is
*derived* rather than dialed in per object, so this stage does the arithmetic:

    island area in UV space  =  world area x (density / atlas resolution)^2

For the kart that density is 512 px/m — the track figure, not the 256 px/m prop
figure, because the cockpit camera sits closer to the kart than to anything else
in the game. ADR-0024 has the reasoning and the check that it is affordable.

Blender's `uv.pack_islands` has a `scale` option that normalizes islands to fill
the atlas, which would undo exactly this. It is off. Islands are scaled by world
area and then packed *without* rescaling, so a 0.1 m² panel and a 1.0 m² panel end
up with proportional texel counts. That is the difference between holding a density
standard and merely having unwrapped.

## No UV2

There is deliberately no second UV channel. A kart moves, so it cannot be
lightmapped; Godot lights it from the probe grid `LightmapGI.generate_probes_subdiv`
produces, which reads no UV2. `ROADMAP.md` M2 and issue #18 both asked for one, and
both had picked it up from the track's requirements — see ADR-0025. Generating it
would cost an unwrap pass and four bytes per vertex for a channel nothing reads.
"""

from __future__ import annotations

import math

import bpy

from . import build

#: Atlas resolution the density is computed against.
#:
#: One 2048 atlas holds the whole kart at 512 px/m with room to spare: the kart is
#: roughly 10 m² of surface, which is 2.6 Mpx against the atlas's 4.2 Mpx. Checked
#: rather than assumed, because the alternative — discovering it does not fit after
#: the unwrap — means redoing every island.
ATLAS_RESOLUTION = 2048

#: Angle above which `smart_project` cuts a seam, in radians (~66 degrees).
#:
#: Tuned for a chassis rather than left at the default. A kart is mostly swept
#: tubes, and a threshold tight enough to seam a hard panel corner also seams
#: every facet around a 12-sided tube, producing dozens of slivers that pack badly
#: and shade worse.
SEAM_ANGLE = 1.15

#: Gap between islands, as a fraction of the atlas.
#:
#: Sized so it survives the mip chain: 8 px at 2048 is 0.0039, and 8 px is two
#: texels of bleed at mip 2, which is about as far down as a kart is ever sampled
#: before the LOD takes over. A tighter margin saves space and bleeds at distance.
ISLAND_MARGIN = 8.0 / ATLAS_RESOLUTION


def run(context: build.BuildContext) -> None:
    """Unwrap every mesh in the kart, then report the density actually achieved."""
    meshes = _meshes(context)
    if not meshes:
        print("    nothing to unwrap")
        return

    target_density = context.params.texel_density

    for obj in meshes:
        _unwrap(obj)

    for obj in meshes:
        _scale_to_density(obj, target_density)

    _pack(meshes)
    _report(meshes, target_density)


def _meshes(context: build.BuildContext) -> list[bpy.types.Object]:
    """Every mesh object, ordered by group then name.

    Sorted for the same reason `genkart.py` sorts its export list: the unwrap runs
    operators that act on the active object, so a stable order makes the result a
    function of the geometry rather than of dictionary iteration.
    """
    found: list[bpy.types.Object] = []
    for group in build.GROUPS:
        collection = context.collections[group]
        for obj in sorted(collection.objects, key=lambda item: item.name):
            if obj.type == "MESH" and len(obj.data.polygons) > 0:
                found.append(obj)
    return found


def _unwrap(obj: bpy.types.Object) -> None:
    """Angle-based unwrap of one object.

    `uv.smart_project` is one of the two places in this pipeline where an operator
    is unavoidable — there is no bmesh equivalent — so the selection is set
    explicitly immediately before the call, per rule 2 in `build.py`.

    `scale_to_bounds` is False and `correct_aspect` is True: the first would
    normalize this object to fill the atlas, destroying the density relationship
    with every other object, and the second keeps a non-square atlas from
    stretching islands.
    """
    if obj.data.uv_layers:
        # Rebuild rather than add. Re-running the stage on the same scene would
        # otherwise accumulate channels, and the second one would silently become
        # the lightmap channel ADR-0025 says the kart must not have.
        while obj.data.uv_layers:
            obj.data.uv_layers.remove(obj.data.uv_layers[0])
    obj.data.uv_layers.new(name="UVMap", do_init=False)

    build.select_only([obj], active=obj)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(
        angle_limit=SEAM_ANGLE,
        island_margin=0.0,
        area_weight=0.0,
        correct_aspect=True,
        scale_to_bounds=False,
    )
    bpy.ops.object.mode_set(mode="OBJECT")


def _scale_to_density(obj: bpy.types.Object, density: float) -> None:
    """Scale this object's UVs so one meter of surface spans `density` pixels.

    `smart_project` produces islands normalized to roughly the 0-1 square with no
    relationship to world size, so this is where the density standard is actually
    imposed. The scale factor is the ratio of the UV area the object *should* own
    to the UV area it currently owns, square-rooted because area scales as the
    square of a linear factor.

    Scaled about the UV origin rather than about each island's own center, so
    islands keep their relative arrangement and the pack step has something
    sensible to work with.
    """
    world_area = build.surface_area(obj)
    uv_area = _uv_area(obj)
    if world_area <= 0.0 or uv_area <= 0.0:
        return

    # Pixels this object is entitled to, expressed as a fraction of the atlas.
    wanted_uv_area = world_area * (density / float(ATLAS_RESOLUTION)) ** 2
    factor = math.sqrt(wanted_uv_area / uv_area)

    uv_layer = obj.data.uv_layers[0]
    for loop_uv in uv_layer.uv:
        loop_uv.vector = (loop_uv.vector[0] * factor, loop_uv.vector[1] * factor)


def _uv_area(obj: bpy.types.Object) -> float:
    """Total UV-space area of every polygon, by the shoelace formula.

    Summed per polygon rather than per island because islands do not need to be
    identified for this: the density relationship is about total area, and two
    islands or twenty make no difference to it.
    """
    uv_layer = obj.data.uv_layers[0]
    total = 0.0
    for polygon in obj.data.polygons:
        indices = list(polygon.loop_indices)
        if len(indices) < 3:
            continue
        area = 0.0
        for position, loop_index in enumerate(indices):
            current = uv_layer.uv[loop_index].vector
            following = uv_layer.uv[indices[(position + 1) % len(indices)]].vector
            area += current[0] * following[1] - following[0] * current[1]
        total += abs(area) * 0.5
    return total


def _pack(meshes: list[bpy.types.Object]) -> None:
    """Pack every object's islands into one shared atlas, without rescaling them.

    All objects are packed together in a single edit session so they share the
    atlas rather than each filling their own. `scale=False` is the load-bearing
    argument: with it True, `pack_islands` grows the islands to fill the space and
    the density work above is thrown away — which is the trap that makes an
    "unwrapped at a fixed density" claim quietly false.

    `rotate=True` is allowed because a rotated island wastes less atlas and the
    kart has no anisotropic detail texture whose direction has to be preserved.
    """
    build.select_only(meshes, active=meshes[0])
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(
        margin=ISLAND_MARGIN,
        margin_method="SCALED",
        rotate=True,
        scale=False,
        merge_overlap=False,
        shape_method="CONCAVE",
        udim_source="ORIGINAL_AABB",
    )
    bpy.ops.object.mode_set(mode="OBJECT")


def _report(meshes: list[bpy.types.Object], target: float) -> None:
    """Print the density actually achieved, and flag anything outside the atlas.

    Reported rather than assumed because the pack step can still refuse to fit
    everything, and the failure is invisible in geometry — it shows up later as one
    part of the kart being blurrier than the rest, which is precisely the tell §5
    exists to prevent. Printing the worst offenders makes it a number in the build
    log instead of something to notice in a render.
    """
    total_world = 0.0
    total_uv = 0.0
    outside = []
    worst: list[tuple[float, str]] = []

    for obj in meshes:
        world_area = build.surface_area(obj)
        uv_area = _uv_area(obj)
        total_world += world_area
        total_uv += uv_area
        if world_area > 0.0 and uv_area > 0.0:
            density = math.sqrt(uv_area / world_area) * ATLAS_RESOLUTION
            worst.append((density, obj.name))

        uv_layer = obj.data.uv_layers[0]
        for loop_uv in uv_layer.uv:
            u, v = loop_uv.vector
            if u < -1e-4 or u > 1.0 + 1e-4 or v < -1e-4 or v > 1.0 + 1e-4:
                outside.append(obj.name)
                break

    achieved = 0.0
    if total_world > 0.0:
        achieved = math.sqrt(total_uv / total_world) * ATLAS_RESOLUTION

    print(
        "    %d meshes, %.2f m2 surface, atlas fill %.1f%%, density %.0f px/m (target %.0f)"
        % (len(meshes), total_world, total_uv * 100.0, achieved, target)
    )

    worst.sort()
    if worst:
        low = worst[0]
        high = worst[-1]
        spread = high[0] / low[0] if low[0] > 0.0 else 0.0
        print(
            "    density spread %.2fx: %s at %.0f px/m, %s at %.0f px/m"
            % (spread, low[1], low[0], high[1], high[0])
        )

    if outside:
        print(
            "    WARNING: %d object(s) have UVs outside 0-1, so the atlas overflowed: %s"
            % (len(outside), ", ".join(sorted(set(outside))[:6]))
        )
