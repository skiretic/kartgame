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

#: Atlas resolution the density is computed against, if `params` does not say.
#:
#: **This constant used to be the answer and it was a second copy of one.** The
#: image the bake actually writes is `params.normal_map_size`, and the density this
#: stage claims is `sqrt(uv_area / world_area) * ATLAS_RESOLUTION` — so the two
#: numbers have to agree or the reported density is a fiction, and nothing checked
#: that they did. Raising the real atlas to 4096 while this stayed at 2048 would
#: have halved the true density to 256 px/m and printed 512 anyway. Read from
#: `params` now; this value survives only as the fallback.
#:
#: The old comment claimed "one 2048 atlas holds the whole kart at 512 px/m with
#: room to spare: the kart is roughly 10 m² of surface". Measured at 295 parts the
#: kart is **13.83 m²**, which wants 86.4% of a 2048 atlas in island area alone —
#: and 86.4% is not packable with an 8 px margin and concave island shapes. It
#: overflowed: every one of the 295 objects had UVs outside 0-1. See `_report`.
ATLAS_FALLBACK_RESOLUTION = 2048

#: Packing efficiency this stage treats as achievable, as a fraction of the atlas.
#:
#: `estimated`, and it exists so the overflow is predicted before the pack rather
#: than discovered after it. Blender's `pack_islands` with `shape_method="CONCAVE"`
#: and an 8 px margin reached 86.4% requested / overflowed at 2048, and 21.6%
#: requested / packed clean at 4096, so the true ceiling is somewhere between; 0.80
#: is a conservative reading of that pair and the number is only used to decide
#: whether to print a warning.
PACKABLE_FRACTION = 0.80

#: Angle above which `smart_project` cuts a seam, in radians (~66 degrees).
#:
#: Tuned for a chassis rather than left at the default. A kart is mostly swept
#: tubes, and a threshold tight enough to seam a hard panel corner also seams
#: every facet around a 12-sided tube, producing dozens of slivers that pack badly
#: and shade worse.
SEAM_ANGLE = 1.15

#: Gap between islands, in pixels of the atlas.
#:
#: Sized so it survives the mip chain: 8 px is two texels of bleed at mip 2, which
#: is about as far down as a kart is ever sampled before the LOD takes over. A
#: tighter margin saves space and bleeds at distance. Held in pixels rather than as
#: a fraction, because the fraction depends on the resolution and the resolution is
#: no longer a constant in this file.
ISLAND_MARGIN_PIXELS = 8.0


def atlas_resolution(
    context: build.BuildContext, meshes: list[bpy.types.Object]
) -> int:
    """The atlas resolution this kart needs, in pixels, as a power of two.

    **Derived, not configured, and that is the point.** Two numbers fix it and both
    have owners: `params.texel_density` is the standard (512 px/m, ADR-0024) and the
    kart's own surface area is whatever the geometry modules built. The atlas is then
    arithmetic — `area x density^2 / packable` — and `params.normal_map_size` acts as
    the floor rather than the answer.

    It was a free parameter, and both halves of it were wrong at once. 2048 was sized
    when the kart was "roughly 10 m²"; at 295 parts it is 13.83 m², which wants 86.4%
    of a 2048 atlas in island area before any packing overhead. The unwrap overflowed
    by 1,462 texels and the bake then wrote 295 parts into a region that does not
    exist, reporting `295/295 baked`. Nothing failed, because nothing looked.

    `bake_stage` calls this too, so the image and the density arithmetic cannot
    disagree — which they silently could when each file had its own constant.
    """
    floor = int(getattr(context.params, "normal_map_size", 0) or 0)
    if floor <= 0:
        floor = ATLAS_FALLBACK_RESOLUTION

    density = float(context.params.texel_density)
    area = sum(build.surface_area(obj) for obj in meshes)
    if area <= 0.0 or density <= 0.0:
        return floor

    # Pixels of texel the kart is entitled to, divided by the fraction of an atlas
    # this packer can actually reach, then rounded up to a power of two: a
    # non-power-of-two normal map is a compression-format problem in Godot.
    wanted = area * density * density / PACKABLE_FRACTION
    resolution = floor
    while resolution * resolution < wanted:
        resolution *= 2
    return resolution


def run(context: build.BuildContext) -> None:
    """Unwrap every mesh in the kart, then report the density actually achieved."""
    meshes = _meshes(context)
    if not meshes:
        print("    nothing to unwrap")
        return

    # The finish tables are checked here because this is the first stage that sees
    # every part at once and `genkart.py`'s `STAGES` tuple is not this wave's to
    # extend. A `finish` stage of its own is the right home and is one line there.
    build.check_finish_tables(obj.name for obj in meshes)
    _report_materials(meshes)

    target_density = context.params.texel_density
    resolution = atlas_resolution(context, meshes)
    floor = int(getattr(context.params, "normal_map_size", 0) or 0)
    if resolution != floor:
        print(
            "    atlas    %d px derived from %.2f m2 at %.0f px/m; "
            "params.normal_map_size floor is %d and does not hold it"
            % (resolution, sum(build.surface_area(o) for o in meshes),
               target_density, floor)
        )

    for obj in meshes:
        _unwrap(obj)

    for obj in meshes:
        _scale_to_density(obj, target_density, resolution)

    _pack(meshes, resolution)
    _report(meshes, target_density, resolution)


def _report_materials(meshes: list[bpy.types.Object]) -> None:
    """Parts per material, worst first.

    Spec §60.6 item 8's headline was one material holding 75 of 146 parts — 116 of
    295 by the time this wave measured it — covering a raw sand casting, machined
    billet, gold anodize and a brazed radiator core simultaneously. A distribution
    is the only way that shows up as a number instead of as a render that looks
    like one bright blob, so it is printed on every run.
    """
    counts: dict[str, int] = {}
    for obj in meshes:
        for slot in obj.data.materials:
            if slot is None:
                continue
            counts[slot.name] = counts.get(slot.name, 0) + 1
    if not counts:
        return
    ordered = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    largest, largest_count = ordered[0]
    print(
        "    material %d material(s) over %d part(s); largest %s holds %d "
        "(%.1f%%)"
        % (len(ordered), len(meshes), largest, largest_count,
           100.0 * largest_count / len(meshes))
    )
    print(
        "    material %s"
        % ", ".join("%s %d" % (name, count) for name, count in ordered)
    )


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


def _scale_to_density(
    obj: bpy.types.Object, density: float, resolution: int
) -> None:
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
    wanted_uv_area = world_area * (density / float(resolution)) ** 2
    factor = math.sqrt(wanted_uv_area / uv_area)

    uv_layer = obj.data.uv_layers[0]
    for loop_uv in uv_layer.uv:
        loop_uv.vector = (loop_uv.vector[0] * factor, loop_uv.vector[1] * factor)

    # Then move the whole block back into the unit square, and this is a bug fix
    # rather than tidying. `pack_islands` is called with `udim_source="ORIGINAL_AABB"`,
    # which packs each island into the UDIM *tile its current bounding box sits in* —
    # so an island the scale above pushed past u = 1 is packed into tile (1, 0) and
    # stays permanently outside the atlas. Measured: at a 4096 atlas the islands need
    # 21.6% of the space and cannot be short of room, and 276 of 295 objects still
    # ended up outside 0-1, the worst by 390 texels. `smart_project` also emits
    # slightly negative coordinates, which lands islands in tile (-1, 0) the same way.
    #
    # A pure translation cannot change the density relationship this function exists
    # to impose, which is what makes it the right fix here rather than a rescale.
    lowest_u = min(loop_uv.vector[0] for loop_uv in uv_layer.uv)
    lowest_v = min(loop_uv.vector[1] for loop_uv in uv_layer.uv)
    for loop_uv in uv_layer.uv:
        loop_uv.vector = (
            loop_uv.vector[0] - lowest_u,
            loop_uv.vector[1] - lowest_v,
        )


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


def _pack(meshes: list[bpy.types.Object], resolution: int) -> None:
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
        margin=ISLAND_MARGIN_PIXELS / float(resolution),
        # ADD, not SCALED, and this was a bug worth 1,462 texels of overflow.
        # `margin_method="SCALED"` grows each island's margin in proportion to the
        # island, and with `scale=False` the packer cannot shrink anything to pay for
        # that — so it lays the whole set out into a square larger than the atlas.
        # Measured at a 4096 atlas where the islands need 21.6% of the space and
        # cannot be short of room: SCALED packed into a 1.095 square, ADD into 0.885.
        # 276 of 295 objects had UVs outside 0-1 purely because of this argument.
        margin_method="ADD",
        rotate=True,
        scale=False,
        merge_overlap=False,
        shape_method="CONCAVE",
        udim_source="ORIGINAL_AABB",
    )
    bpy.ops.object.mode_set(mode="OBJECT")


def _report(meshes: list[bpy.types.Object], target: float, resolution: int) -> None:
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
            density = math.sqrt(uv_area / world_area) * resolution
            worst.append((density, obj.name))

        uv_layer = obj.data.uv_layers[0]
        for loop_uv in uv_layer.uv:
            u, v = loop_uv.vector
            excess = max(-u, u - 1.0, -v, v - 1.0)
            if excess > 1e-4:
                outside.append((excess, obj.name))
                break

    achieved = 0.0
    if total_world > 0.0:
        achieved = math.sqrt(total_uv / total_world) * resolution

    print(
        "    %d meshes, %.2f m2 surface, %d atlas, island area %.1f%% of it, "
        "density %.0f px/m (target %.0f)"
        % (
            len(meshes),
            total_world,
            resolution,
            total_uv * 100.0,
            achieved,
            target,
        )
    )
    print(
        "    atlas    %.1f Mpx of texels wanted against %.1f Mpx available; "
        "headroom %.2fx at %.0f%% packable"
        % (
            total_world * target * target / 1.0e6,
            resolution * resolution / 1.0e6,
            (PACKABLE_FRACTION / total_uv) if total_uv > 0.0 else 0.0,
            PACKABLE_FRACTION * 100.0,
        )
    )
    if total_uv > PACKABLE_FRACTION:
        print(
            "    WARNING: island area %.1f%% exceeds the %.0f%% this packer reaches, "
            "so the atlas cannot hold the kart at %.0f px/m. Raise "
            "params.normal_map_size to %d."
            % (
                total_uv * 100.0,
                PACKABLE_FRACTION * 100.0,
                target,
                resolution * 2,
            )
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
        # Reported as a *distance* past the edge, not as a count. The old check said
        # only "295 object(s) have UVs outside 0-1", which reads as a total pack
        # failure — and at 4096, where the islands need 21.6% of the atlas and
        # cannot possibly be short of room, it still said 276. The number that
        # distinguishes "the atlas is too small" from "the packer left a hair over
        # the edge" is how far over, in texels.
        outside.sort(reverse=True)
        excess, worst = outside[0]
        print(
            "    WARNING: %d object(s) reach outside 0-1; worst %s by %.5f uv "
            "(%.1f texels at %d)"
            % (len(outside), worst, excess, excess * resolution, resolution)
        )
