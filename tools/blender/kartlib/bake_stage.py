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

    size = context.params.normal_map_size
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

    baked = 0
    for index, obj in enumerate(meshes):
        source = context.high_poly.get(obj.name)
        if source is None:
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

    if baked == 0:
        print("    warning: nothing baked, leaving the normal map unattached")
        _detach_normal_map(targets)
        bpy.data.images.remove(image)
        return

    path = _write(image, context)
    print("    baked %d/%d objects into %s" % (baked, len(meshes), os.path.basename(path)))


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
