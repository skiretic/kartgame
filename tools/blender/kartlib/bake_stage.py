"""Issue #19 — bake normals from the high-poly source onto the game mesh.

Runs after the UV stage, over the low-poly kart, reading the high-poly twin
`genkart.py` paired into `context.high_poly`. ADR-0012 named normal baking as one
of the three things Blender gives free that would be substantial C++ work for a
worse result, so this is that claim being cashed in.

## What the bake is for

The low-poly kart carries a 1.5 mm bevel and 12-sided tubes. The high-poly carries
a 4 mm bevel at four segments and 32-sided tubes. Baking the second onto the first
puts the high-poly's *shading* on the low-poly's *geometry*, so a tube edge catches
a highlight that its actual silhouette does not justify. Issue #19's first
acceptance criterion is exactly that: bevel highlights on tube edges without the
geometry cost.

Note what it cannot do, so nobody expects it to: a normal map does not change a
silhouette. At a grazing angle a 12-sided tube is still visibly 12-sided. That is
why the low-poly keeps a real 1.5 mm chamfer rather than relying on the map for
everything — see `build.Detail.low`.

## Cycles, headless, and one thing that had to be checked

Baking needs Cycles, and Cycles is an add-on. `--factory-startup` would have
disabled it, which is why `build.reset_scene` resets the scene from inside the
script instead — the scene is discarded, the add-ons stay loaded. Verified before
this was designed around: Cycles sets as the engine and bakes a normal map
headless in `--background` on this host, and the resulting PNG is byte-identical
between runs.

That last part matters. Determinism is an M2 acceptance criterion and a Monte Carlo
renderer is the obvious place to lose it, so the sample count is fixed, the seed is
fixed, denoising is off — a denoiser is the one stage most likely to be
thread-order dependent — and the device is pinned to CPU. Measured: identical
SHA-256 across runs.

## Baking every part into one shared atlas

The UV stage packs the whole kart into a single 0-1 atlas, so every object's
islands occupy a distinct region of one image. That means one bake target for the
entire kart: each pair is baked in turn with `use_clear` off after the first, and
each writes only its own region. Baking to per-object images instead would mean
dozens of textures and dozens of draw calls for an object the player sits inside.
"""

from __future__ import annotations

import os

import bpy

from . import build
from . import uv_stage

#: Fixed so the bake is reproducible. Normals are a geometric quantity, so this
#: needs far fewer samples than a lighting bake would — the ray either hits the
#: high-poly surface or it does not.
SAMPLES = 16

#: Cycles' RNG seed. Fixed for the same reason.
SEED = 0

#: How far the ray search starts outside the low-poly surface, in meters.
#:
#: Has to exceed the largest gap between the two meshes, which here is the
#: difference between a 12-sided and a 32-sided tube of 30 mm diameter — under
#: 1 mm. 5 mm is comfortable margin without being so large that a ray from one
#: tube finds the tube behind it, which is what produces the smeared bake issue
#: #19's second criterion rules out.
CAGE_EXTRUSION = 0.005

#: Ray length limit, for the same reason from the other side.
MAX_RAY_DISTANCE = 0.02

#: Pixels of bleed past each island edge, so bilinear filtering and the mip chain
#: do not sample the empty atlas next door.
MARGIN = 8

#: Tread graining: a driven slick is not perfectly smooth -- the contact band
#: carries a worked, mottled surface from abrasion, visible in
#: `refs/kart-visual/det_tonykart_401t_museum.jpg` as a matte band against the
#: sheen of the sidewall. Modeled as a procedural bump on the *high-poly* tire
#: only, attached for the duration of the bake and removed after, so it rides
#: the normal atlas like a bevel highlight: no geometry cost, no change to the
#: shipped materials or the glTF export path. Verified before building
#: (probe, this session): a selected-to-active NORMAL bake does capture the
#: source material's bump chain.
#:
#: Feature size is bounded from below by the atlas, not by realism: the tire's
#: island resolves ~2 mm per texel, so sub-texel grit would average to mush at
#: 16 samples. ~4.5 mm mottling bakes cleanly; true micro-grain stays the
#: `tire_tread` zone's roughness value. All three figures `estimated`.
TREAD_GRAIN_SCALE = 220.0
TREAD_GRAIN_DISTANCE = 0.0004
#: The grain masks in radially: zero 12 mm below the tread-edge radius, full 6 mm
#: below it, so the whole crown is worked and the band dies a hand's width down
#: the shoulder -- wear lives where the road touches.
TREAD_GRAIN_FADE = (0.012, 0.006)


def run(context: build.BuildContext) -> None:
    """Bake tangent-space normals for every paired object into one atlas."""
    if not context.high_poly:
        print("    no high-poly source paired, skipping the bake")
        return

    meshes = _bakeable(context)
    if not meshes:
        print("    nothing to bake")
        return

    scene = bpy.context.scene
    _configure_cycles(scene)

    # The same resolution the UV stage laid the islands out for, computed the same
    # way from the same two owned numbers rather than read from a second constant.
    # `params.normal_map_size` alone was 2048 while the unwrap needed 4096, so the
    # bake wrote every part into an atlas region the UVs had already left.
    size = uv_stage.atlas_resolution(context, meshes)
    image = bpy.data.images.new(
        "kart_normal", size, size, alpha=False, float_buffer=False
    )
    # Non-Color is not cosmetic. A normal map read through sRGB is a wrong normal
    # map, and the error is subtle enough to survive review — surfaces look
    # slightly inflated rather than obviously broken.
    image.colorspace_settings.name = "Non-Color"

    # Every material needs the target image as its active node before it can be
    # baked into, and connecting it through a Normal Map node means the same setup
    # also gives the exported glTF its normal map. One arrangement, two purposes.
    targets = _attach_normal_map(context, image)
    grain_overrides, grain_materials = _attach_tread_grain(context)

    baked = 0
    orphans: list[str] = []
    for index, obj in enumerate(meshes):
        source = context.high_poly.get(obj.name)
        if source is None:
            # Counted and named rather than skipped in silence. An unpaired part
            # bakes nothing into its own atlas region, so it ships with whatever the
            # region held — at 295 parts that is one blurry or wrongly-shaded part
            # among 294 correct ones, which is precisely the class of defect nobody
            # finds by looking. Issue #19's pairing is the orchestrator's, so this
            # stage can only report it.
            orphans.append(obj.name)
            continue
        build.select_only([source, obj], active=obj)
        try:
            bpy.ops.object.bake(
                type="NORMAL",
                use_selected_to_active=True,
                cage_extrusion=CAGE_EXTRUSION,
                max_ray_distance=MAX_RAY_DISTANCE,
                normal_space="TANGENT",
                margin=MARGIN,
                margin_type="EXTEND",
                # Cleared once, by the first bake only. Every later pair writes its
                # own atlas region and must not wipe the ones already done.
                use_clear=(baked == 0),
                use_split_materials=False,
            )
        except RuntimeError as error:
            print("    warning: bake failed for %s: %s" % (obj.name, error))
            continue
        baked += 1
        del index

    _detach_tread_grain(grain_overrides, grain_materials)

    if baked == 0:
        print("    warning: nothing baked, leaving the normal map unattached")
        _detach_normal_map(targets)
        bpy.data.images.remove(image)
        return

    path = _write(image, context)
    print(
        "    baked %d/%d objects into %s at %dx%d, %d orphan(s)"
        % (
            baked,
            len(meshes),
            os.path.basename(path),
            size,
            size,
            len(orphans),
        )
    )
    if orphans:
        print(
            "    WARNING: %d low-poly part(s) have no high-poly twin: %s"
            % (len(orphans), ", ".join(orphans[:8]))
        )


def _bakeable(context: build.BuildContext) -> list[bpy.types.Object]:
    """Low-poly meshes with UVs, ordered by group then name.

    Ordered because the bake runs an operator per object and `use_clear` is true
    for the first one only — so which object is first has to be a function of the
    names rather than of dictionary iteration.
    """
    found: list[bpy.types.Object] = []
    for group in build.GROUPS:
        for obj in sorted(context.collections[group].objects, key=lambda o: o.name):
            if obj.type == "MESH" and obj.data.uv_layers and len(obj.data.polygons):
                found.append(obj)
    return found


def _configure_cycles(scene: bpy.types.Scene) -> None:
    """Cycles, CPU, fixed seed, no denoiser.

    Every one of those is a determinism decision rather than a quality one. GPU
    reduction order is not guaranteed stable, and a denoiser is the stage most
    likely to depend on thread scheduling — ADR-0022 already recorded this host's
    Metal driver timing out on an 11-second GPU bake, so CPU is also the safer
    choice on the same evidence.
    """
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = SAMPLES
    scene.cycles.seed = SEED
    scene.cycles.use_denoising = False
    # Bake settings live on the scene rather than on the operator for some options.
    scene.render.bake.use_selected_to_active = True
    scene.render.bake.use_cage = False


def _attach_normal_map(
    context: build.BuildContext, image: bpy.types.Image
) -> list[tuple[bpy.types.Material, bpy.types.Node]]:
    """Wire the bake target into every material as its normal map.

    Two things have to be true at once: the image must be the material's *active*
    node for `object.bake` to write into it, and it should be connected to the
    Principled BSDF's Normal input so the glTF exporter picks it up as the
    material's normal texture. A Normal Map node between them does both, because
    the exporter recognizes exactly that arrangement.

    Returns what was added, so a failed bake can undo it rather than shipping a
    material referencing an empty image.
    """
    added: list[tuple[bpy.types.Material, bpy.types.Node]] = []
    for name in sorted(context.materials):
        material = context.materials[name]
        tree = material.node_tree
        principled = tree.nodes.get("Principled BSDF")
        if principled is None:
            continue

        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = image
        texture.label = "kart_normal"
        texture.location = (-620.0, -260.0)

        normal_map = tree.nodes.new("ShaderNodeNormalMap")
        normal_map.location = (-320.0, -260.0)

        tree.links.new(texture.outputs["Color"], normal_map.inputs["Color"])
        tree.links.new(normal_map.outputs["Normal"], principled.inputs["Normal"])

        # The bake writes to the active image node, so this is not optional.
        tree.nodes.active = texture
        added.append((material, texture))
    return added


def _grain_material(name: str, tread_radius: float) -> bpy.types.Material:
    """One bake-only material: noise bump masked to the tread band by radius.

    The mask reads the mesh's own object-space coordinates -- the tire is lathed
    about local X with its origin at the wheel center, so radial distance is the
    length of (0, y, z) and no per-object transform is involved.
    """
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    tree = material.node_tree
    principled = tree.nodes["Principled BSDF"]

    coords = tree.nodes.new("ShaderNodeTexCoord")
    separate = tree.nodes.new("ShaderNodeSeparateXYZ")
    tree.links.new(coords.outputs["Object"], separate.inputs["Vector"])
    plane = tree.nodes.new("ShaderNodeCombineXYZ")
    plane.inputs["X"].default_value = 0.0
    tree.links.new(separate.outputs["Y"], plane.inputs["Y"])
    tree.links.new(separate.outputs["Z"], plane.inputs["Z"])
    radius = tree.nodes.new("ShaderNodeVectorMath")
    radius.operation = "LENGTH"
    tree.links.new(plane.outputs["Vector"], radius.inputs[0])

    fade_out, fade_full = TREAD_GRAIN_FADE
    mask = tree.nodes.new("ShaderNodeMapRange")
    mask.inputs["From Min"].default_value = tread_radius - fade_out
    mask.inputs["From Max"].default_value = tread_radius - fade_full
    tree.links.new(radius.outputs["Value"], mask.inputs["Value"])

    noise = tree.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = TREAD_GRAIN_SCALE
    noise.inputs["Detail"].default_value = 3.0
    tree.links.new(coords.outputs["Object"], noise.inputs["Vector"])

    bump = tree.nodes.new("ShaderNodeBump")
    bump.inputs["Distance"].default_value = TREAD_GRAIN_DISTANCE
    tree.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    tree.links.new(mask.outputs["Result"], bump.inputs["Strength"])
    tree.links.new(bump.outputs["Normal"], principled.inputs["Normal"])
    return material


def _attach_tread_grain(
    context: build.BuildContext,
) -> tuple[
    list[tuple[bpy.types.MaterialSlot, str, bpy.types.Material]],
    list[bpy.types.Material],
]:
    """Swap the high-poly tires onto grain materials, as object-level overrides.

    Object-level (`slot.link = "OBJECT"`) so the shared mesh materials are never
    touched -- the shipped low-poly reads the same datablocks throughout, and
    restoring is clearing the override. Front and rear get their own material
    because the mask is keyed to each end's tread radius.
    """
    overrides: list[tuple[bpy.types.MaterialSlot, str, bpy.types.Material]] = []
    materials: dict[str, bpy.types.Material] = {}
    p = context.params
    radii = {"f": p.tire_front_diameter * 0.5, "r": p.tire_rear_diameter * 0.5}
    for name in sorted(context.high_poly):
        if not (name.startswith("wheel_") and name.endswith("_tire")):
            continue
        end = name.split("_")[1][0]
        if end not in materials:
            materials[end] = _grain_material("bake_tread_grain_%s" % end, radii[end])
        for slot in context.high_poly[name].material_slots:
            overrides.append((slot, slot.link, slot.material))
            slot.link = "OBJECT"
            slot.material = materials[end]
    return overrides, [materials[end] for end in sorted(materials)]


def _detach_tread_grain(
    overrides: list[tuple[bpy.types.MaterialSlot, str, bpy.types.Material]],
    materials: list[bpy.types.Material],
) -> None:
    for slot, link, material in overrides:
        slot.material = None
        slot.link = link
        if link == "OBJECT":
            slot.material = material
    for material in materials:
        bpy.data.materials.remove(material)


def _detach_normal_map(
    targets: list[tuple[bpy.types.Material, bpy.types.Node]]
) -> None:
    for material, node in targets:
        material.node_tree.nodes.remove(node)


def _write(image: bpy.types.Image, context: build.BuildContext) -> str:
    """Save the atlas next to the glTF, as PNG.

    PNG rather than a compressed format: this is the source texture, Godot
    compresses to VRAM formats on import (ARCHITECTURE.md §11), and compressing
    twice is how a normal map acquires blocky artifacts. Blender writes no
    timestamp chunk, so the file is byte-identical between runs — checked, because
    a timestamp here would have quietly broken the determinism gate.
    """
    path = os.path.join(context.output_directory, "kart_normal.png")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    return path
