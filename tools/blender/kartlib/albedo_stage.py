"""The albedo stage: livery art rasterized into the UV atlas. ADR-0060.

The pipeline had a normal bake and no albedo path, so every wrap surface was a
flat base color and the driver's suit was one navy — which is exactly what a
real kart never looks like (#189). This stage paints `kart_albedo.png` in the
same atlas `uv_stage` laid out, and it paints in **world space**: a stripe is a
z-band, a chest panel is a box with a facing test, a die-cut edge is a distance
from a plane. No new UV machinery, no second UV layer — `uv_stage`'s refusal of
UV2 stands, and the art reaches the atlas by walking each face's UV triangle
with barycentric world interpolation.

numpy is deliberate and stays inside this stage (ADR-0060 §2): Blender 5.2
bundles numpy and `build.checker_image`'s pixel-list write is unusably slow at
atlas sizes. Nothing in the geometry modules may import it.

Determinism: the image is a pure function of the params, the livery tables and
the built meshes. Triangles are walked in sorted object order then loop-triangle
order, every write is a plain assignment (later writes win, and the order is
fixed), and the PNG goes through the same `image.save()` path the bake uses,
which writes no timestamp chunk.
"""

from __future__ import annotations

import os

import bpy
import numpy as np
from mathutils import Vector

from . import build
from . import uv_stage

#: Materials this stage paints, and nothing else gets a texture: everything
#: else keeps its plain base color factor, which is cheaper for Godot and true
#: to the part -- an axle has no livery.
ART_MATERIALS: tuple[str, ...] = (
    "bodywork_wrap",
    "bodywork_contrast",
    "livery_accent",
    "overalls_fabric",
)

#: The wrap's twin pinstripe, as z-bands in meters. `estimated` off
#: `tonykart_racer401T_p01.jpg`'s tray pinstriping and the V13 die-cut sheet:
#: a thick accent line with a thin contrast companion running the length of the
#: bodywork. Heights sit in the pod flank's tall band so the line runs
#: unbroken from the nose over the pods.
STRIPE_MAIN: tuple[float, float] = (0.146, 0.162)
STRIPE_THIN: tuple[float, float] = (0.168, 0.174)

#: The suit's chest panel, a world-space box over the torso with a forward
#: facing test. `derived` from §60.3.8's measurement: the suit's navy body is
#: 0.037 in luminance and its white chest panel 0.639 -- the bright thing on a
#: race driver is a panel, not the garment.
CHEST_X: float = 0.105
CHEST_Y: tuple[float, float] = (-0.560, -0.380)
CHEST_Z: tuple[float, float] = (0.380, 0.640)
CHEST_FACING: float = 0.20

#: Sleeve and shoulder stripes on the suit, z-bands over the arm parts only.
SLEEVE_BAND: tuple[float, float] = (0.560, 0.585)

#: Colors that are not livery-driven. The chest white matches the wrap white
#: family; the sleeve fluoro is §60.3.8's observed shoulder stripe, `estimated`
#: hue (fluorescent inks clip the sensor, same failure §60.5 records for the
#: factory orange).
CHEST_WHITE: tuple[float, float, float] = (0.90, 0.90, 0.88)
SLEEVE_FLUORO: tuple[float, float, float] = (0.72, 0.83, 0.05)

#: Post-paint island dilation in pixels, the albedo's analog of the bake's
#: EXTEND margin: without it, mip sampling pulls unpainted atlas into the seam.
DILATE_PIXELS: int = 8


def _srgb_to_linear_triplet(color: tuple[float, float, float]) -> np.ndarray:
    return np.array(
        [build.srgb_to_linear(c) for c in color], dtype=np.float32
    )


def _material_base(name: str) -> np.ndarray:
    color, _roughness, _metallic = build.MATERIALS[name]
    return np.array(color[:3], dtype=np.float32)


def _art_colors(
    material: str,
    part: str,
    world: np.ndarray,
    normal_y: float,
    base: np.ndarray,
    accent: np.ndarray,
    contrast: np.ndarray,
) -> np.ndarray:
    """Colors for a block of pixels, vectorized. `world` is (..., 3), meters.

    A per-pixel Python call is minutes of wall clock at a 4096 atlas; a masked
    assignment is milliseconds, and the masks *are* the art functions -- every
    rule here is a band or a box in world space.
    """
    out = np.broadcast_to(base, world.shape[:-1] + (3,)).copy()
    z = world[..., 2]
    if material == "bodywork_wrap":
        # Stripes ride the wrap alone: the contrast outers and accent trims are
        # already their own statement and a stripe across them reads as a
        # printing error (spec 60.5.4's adjacency argument).
        out[(z >= STRIPE_MAIN[0]) & (z <= STRIPE_MAIN[1])] = accent
        out[(z >= STRIPE_THIN[0]) & (z <= STRIPE_THIN[1])] = contrast
        return out
    if material in ("bodywork_contrast", "livery_accent"):
        return out
    if material == "overalls_fabric":
        if part.startswith(("driver_upper_arm", "driver_forearm")):
            band = (z >= SLEEVE_BAND[0]) & (z <= SLEEVE_BAND[1])
            out[band] = _srgb_to_linear_triplet(SLEEVE_FLUORO)
            return out
        if normal_y >= CHEST_FACING:
            chest = (
                (np.abs(world[..., 0]) <= CHEST_X)
                & (world[..., 1] >= CHEST_Y[0])
                & (world[..., 1] <= CHEST_Y[1])
                & (z >= CHEST_Z[0])
                & (z <= CHEST_Z[1])
            )
            out[chest] = _srgb_to_linear_triplet(CHEST_WHITE)
        return out
    return out


def _paint_triangle(
    pixels: np.ndarray,
    painted: np.ndarray,
    size: int,
    uvs: np.ndarray,
    worlds: np.ndarray,
    normal_y: float,
    material: str,
    part: str,
    base: np.ndarray,
    accent: np.ndarray,
    contrast: np.ndarray,
) -> None:
    """Rasterize one UV triangle, interpolating world position barycentrically."""
    xy = uvs * size
    lo = np.maximum(np.floor(xy.min(axis=0)).astype(int), 0)
    hi = np.minimum(np.ceil(xy.max(axis=0)).astype(int) + 1, size)
    if hi[0] <= lo[0] or hi[1] <= lo[1]:
        return

    xs = np.arange(lo[0], hi[0], dtype=np.float32) + 0.5
    ys = np.arange(lo[1], hi[1], dtype=np.float32) + 0.5
    grid_x, grid_y = np.meshgrid(xs, ys)

    a, b, c = xy[0], xy[1], xy[2]
    detline = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(detline) < 1e-9:
        return
    w0 = ((b[1] - c[1]) * (grid_x - c[0]) + (c[0] - b[0]) * (grid_y - c[1])) / detline
    w1 = ((c[1] - a[1]) * (grid_x - c[0]) + (a[0] - c[0]) * (grid_y - c[1])) / detline
    w2 = 1.0 - w0 - w1
    mask = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
    if not mask.any():
        return

    world = (
        w0[..., None] * worlds[0]
        + w1[..., None] * worlds[1]
        + w2[..., None] * worlds[2]
    )

    colors = _art_colors(material, part, world, normal_y, base, accent, contrast)
    block = pixels[lo[1] : hi[1], lo[0] : hi[0]]
    block[..., 0:3][mask] = colors[mask]
    block[..., 3][mask] = 1.0
    painted[lo[1] : hi[1], lo[0] : hi[0]][mask] = True


def _dilate(pixels: np.ndarray, painted: np.ndarray, steps: int) -> None:
    """Grow painted texels into unpainted neighbors, EXTEND-style."""
    for _ in range(steps):
        grown = painted.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            shifted = np.roll(painted, (dy, dx), axis=(0, 1))
            fresh = shifted & ~grown
            if not fresh.any():
                continue
            source = np.roll(pixels, (dy, dx), axis=(0, 1))
            pixels[fresh] = source[fresh]
            grown |= fresh
        if grown.all():
            painted[:] = grown
            return
        painted[:] = grown


def _attach_albedo(
    context: build.BuildContext, image: bpy.types.Image
) -> int:
    """Wire the atlas into the art materials' Base Color.

    The base color factor moves to white for these materials, because glTF
    multiplies factor by texture and the base color is painted *into* the
    texture -- leaving the factor in place would square it.
    """
    attached = 0
    for name in ART_MATERIALS:
        material = context.materials.get(name)
        if material is None:
            continue
        tree = material.node_tree
        principled = tree.nodes.get("Principled BSDF")
        if principled is None:
            continue
        texture = tree.nodes.new("ShaderNodeTexImage")
        texture.image = image
        texture.label = "kart_albedo"
        texture.location = (-620.0, 220.0)
        tree.links.new(
            texture.outputs["Color"], principled.inputs["Base Color"]
        )
        principled.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        attached += 1
    return attached


def run(context: build.BuildContext) -> None:
    """Paint the atlas and attach it. See the module docstring."""
    art = set(ART_MATERIALS)
    meshes = [
        obj
        for obj in _uv_meshes(context)
        if any(slot.material and slot.material.name in art for slot in obj.material_slots)
    ]
    if not meshes:
        print("    no art surfaces, skipping the albedo")
        return

    size = uv_stage.atlas_resolution(context, _uv_meshes(context))
    pixels = np.zeros((size, size, 4), dtype=np.float32)
    painted = np.zeros((size, size), dtype=bool)

    accent = _material_base("livery_accent")
    contrast = _material_base("bodywork_contrast")

    triangles = 0
    for obj in sorted(meshes, key=lambda o: o.name):
        mesh = obj.data
        if not mesh.uv_layers:
            continue
        uv_layer = mesh.uv_layers.active.data
        mesh.calc_loop_triangles()
        matrix = obj.matrix_world
        slots = obj.material_slots
        for tri in mesh.loop_triangles:
            material = slots[tri.material_index].material
            if material is None or material.name not in art:
                continue
            base = _material_base(material.name)
            uvs = np.array(
                [uv_layer[loop].uv[:] for loop in tri.loops], dtype=np.float32
            )
            worlds = np.array(
                [
                    (matrix @ mesh.vertices[v].co)[:]
                    for v in tri.vertices
                ],
                dtype=np.float32,
            )
            normal = matrix.to_3x3() @ Vector(tri.normal)
            _paint_triangle(
                pixels,
                painted,
                size,
                uvs,
                worlds,
                normal.y,
                material.name,
                obj.name,
                base,
                accent,
                contrast,
            )
            triangles += 1

    _dilate(pixels, painted, DILATE_PIXELS)

    image = bpy.data.images.new(
        "kart_albedo", size, size, alpha=False, float_buffer=False
    )
    image.colorspace_settings.name = "sRGB"
    # The art tables are linear (they feed Principled base colors), but a PNG's
    # bytes are sRGB-encoded and `image.pixels` stores what the file will hold.
    # Writing linear values raw shipped an atlas one gamma dark -- the whole
    # kart rendered grey, measured on the first livery turntable -- so the
    # buffer is encoded here, vectorized, before the write.
    rgb = pixels[..., 0:3]
    encoded = np.where(
        rgb <= 0.0031308,
        rgb * 12.92,
        1.055 * np.power(np.maximum(rgb, 0.0), 1.0 / 2.4) - 0.055,
    )
    pixels[..., 0:3] = encoded
    # Blender wants a flat float list bottom-up; numpy is already row-major
    # bottom-up in this addressing, so the reshape is the write.
    flat = pixels.reshape(-1)
    image.pixels.foreach_set(flat)

    path = os.path.join(context.output_directory, "kart_albedo.png")
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()

    attached = _attach_albedo(context, image)
    print(
        "    painted %d triangle(s) into kart_albedo.png at %dx%d, %d material(s) textured"
        % (triangles, size, size, attached)
    )


def _uv_meshes(context: build.BuildContext) -> list[bpy.types.Object]:
    """The same mesh set the uv stage laid out, via its own enumerator."""
    return uv_stage._meshes(context)
