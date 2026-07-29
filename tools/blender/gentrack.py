"""Generate a circuit's visual mesh from `track.json`, headless.

    blender --background --python tools/blender/gentrack.py -- \
            --track data/tracks/valdirone_nuova.track.json \
            --out assets/generated/valdirone_nuova.glb

Everything after the bare `--` is this script's; Blender consumes what is before
it. `tools/blender/gentrack.sh` is the wrapper that finds the right Blender and
checks its version, and is the intended entry point - but this file stays runnable
exactly as `ARCHITECTURE.md` §11 spells it out, because a documented command that
is not the real one rots.

## What this is the second half of

`ARCHITECTURE.md` §11: the spline is authored once and read twice, so what you see
and what you collide with cannot drift apart. `src/track/kart_track.cpp` is the
other reader and builds the collider; this one builds the mesh. They share no code
- one is C++ inside the engine and the other is Python inside Blender - so what
keeps them honest is `docs/TRACK_SCHEMA.md` stating the interpolation rules in
arithmetic, and `--manifest` writing out sampled edge points that
`tools/verify/circuit.sh` compares against the collider's own.

**That check is the point.** "Cannot drift apart" is a claim about a pipeline, and
a claim about a pipeline with nothing measuring it is a comment.

## Determinism is an acceptance criterion, not a nicety

Same rules as `genkart.py`, and `gentrack.sh --check` is the same gate:

  * the scene is reset from factory settings, so a stray default cube is not
    exported as part of the circuit;
  * no stage uses randomness, wall-clock time, or iteration over an unordered
    collection - the surface list is walked in file order and the material dict is
    walked sorted;
  * every object has a fixed name, since names order the exporter's output.

## Stages

    geometry   build the strips from track.json
    uv         the two channels, both filled during geometry; this stage checks
               them rather than computing them
    export     glTF, Y-up, -Z forward
    manifest   the diffable sidecar and the cross-check sample

**No LOD stage, and that is a decision rather than an omission.** ADR-0026: Godot
generates its own mesh LODs on import and cannot read a decimated chain out of
glTF, so emitting one would be work whose output nothing reads. The same rescope
was applied to the kart at M2.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tracklib import geometry  # noqa: E402
from tracklib import surfaces  # noqa: E402

BLENDER_REQUIRED = (5, 2)
STAGES = ("geometry", "uv", "export", "manifest")

#: Chord tolerance for the polyline, meters, and the largest gap between samples
#: on a straight. 20 mm is a quarter of the 80 mm the tire's contact patch spans,
#: which is `track_layout.gd`'s argument and holds here; 2 m on a straight is fine
#: because a straight has no sagitta at all and the spacing only has to be dense
#: enough for the lightmap and the width taper to have somewhere to vary.
SAGITTA = 0.020
MAX_SPACING = 2.0

#: How many rows the lightmap snake uses. Ten puts a 1,375 m lap into pieces of
#: about 137 m; see `surfaces.snake_uv2` for why one row is not an option.
LIGHTMAP_ROWS = 10


def script_arguments() -> list[str]:
    """Everything after the bare `--`, or everything if Blender is not running."""
    if "--" in sys.argv:
        return sys.argv[sys.argv.index("--") + 1:]
    return sys.argv[1:]


def project_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="gentrack.py", description=__doc__)
    parser.add_argument("--track", default="data/tracks/valdirone_nuova.track.json")
    parser.add_argument("--out", default="assets/generated/valdirone_nuova.glb")
    parser.add_argument("--stages", default=",".join(STAGES))
    parser.add_argument("--blend", default="")
    parser.add_argument("--manifest", default="")
    parser.add_argument("--quiet", action="store_true")
    return parser.parse_args(argv)


def build_strips(track: geometry.Track) -> list[surfaces.Strip]:
    """Every surface, in a fixed order.

    Order matters and is not cosmetic: it decides object creation order, which
    decides the exporter's node order, which decides the output hash. A set or a
    dict comprehension here would make the determinism gate flap.
    """
    line = track.polyline(SAGITTA, MAX_SPACING)
    apron, gravel, barrier = surfaces.build_runoff(track, line, LIGHTMAP_ROWS)
    ordered = [
        surfaces.build_road(track, line, LIGHTMAP_ROWS),
        surfaces.build_edge_lines(track, line, LIGHTMAP_ROWS),
        surfaces.build_start_line(track, LIGHTMAP_ROWS),
        surfaces.build_kerbs(track, line, LIGHTMAP_ROWS),
        surfaces.build_verge(track, line, LIGHTMAP_ROWS),
        apron,
        gravel,
        barrier,
    ]
    return [strip for strip in ordered if not strip.is_empty()]


# --- Blender ---------------------------------------------------------------
#
# Everything below needs bpy. Split out so the geometry above can be exercised
# from an ordinary python3, which is how the manifest cross-check runs where no
# Blender is installed.


def reset_scene(bpy) -> None:
    """Factory settings, from inside the script rather than via --factory-startup.

    `--factory-startup` disables add-ons, and this project's Blender pipeline
    needs them elsewhere; resetting from inside discards the user's startup file -
    and its default cube - without unloading anything. ADR-0012 and genkart.py's
    header carry the same note.
    """
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0


def material_for(bpy, name: str):
    material = bpy.data.materials.get(name)
    if material is not None:
        return material
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    principled = material.node_tree.nodes.get("Principled BSDF")
    if principled is not None:
        principled.inputs["Base Color"].default_value = surfaces.MATERIALS.get(
            name, (0.5, 0.5, 0.5, 1.0)
        )
        principled.inputs["Roughness"].default_value = 0.85
        principled.inputs["Metallic"].default_value = 0.0
    # Backface culling ON, and this is the trap CLAUDE.md records: Blender defaults
    # it off, the exporter then writes `doubleSided: true`, and Godot renders
    # backfaces with the shading normal flipped - so inverted winding is invisible
    # and `build.box` and `build.lathe` were wound inward for two milestones with
    # every render looking correct. With it on, a wrong-way face is a hole.
    material.use_backface_culling = True
    return material


def build_object(bpy, strip: surfaces.Strip):
    mesh = bpy.data.meshes.new(strip.name)
    mesh.from_pydata(strip.vertices, [], [list(face) for face in strip.faces])
    mesh.update()

    uv = mesh.uv_layers.new(name="UVMap")
    uv2 = mesh.uv_layers.new(name="UV2")
    # `from_pydata` keeps the vertex order it was given and the loops follow the
    # faces, so loop `i` of triangle `t` is vertex `strip.faces[t][i]` - the UV
    # arrays are per *vertex* here and are indexed through that rather than
    # assumed to be per loop, which they are not.
    for loop in mesh.loops:
        uv.data[loop.index].uv = strip.uvs[loop.vertex_index]
        uv2.data[loop.index].uv = strip.uv2s[loop.vertex_index]

    mesh.materials.append(material_for(bpy, strip.material))
    obj = bpy.data.objects.new(strip.name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def export_gltf(bpy, path: str) -> None:
    """glTF 2.0, Y-up, -Z forward, per ARCHITECTURE.md §11's import conventions.

    `export_yup=True` maps Blender (x, y, z) to glTF (x, z, -y). `track.json` is
    already in Godot's frame and `tracklib/surfaces.to_blender` converted it on the
    way in, so this converts it straight back - the two are inverse by
    construction, which is the only reason it is safe to have two conversions in
    one pipeline.
    """
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(
        filepath=path,
        export_format="GLB",
        export_yup=True,
        use_selection=True,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        # Godot generates tangents on import when a normal map needs them, and an
        # exported tangent array is four more floats per vertex for the same result.
        export_tangents=False,
        export_materials="EXPORT",
        export_cameras=False,
        export_lights=False,
        # Blender-side bookkeeping does not belong in a runtime asset, and the
        # iteration order of a Blender ID property dict is not guaranteed stable -
        # so this is a determinism risk as well as clutter.
        export_extras=False,
        export_animations=False,
        export_skins=False,
    )


def write_manifest(track, strips, gltf_path: str, manifest_path: str) -> dict:
    """The diffable sidecar, and the cross-check the pipeline's claim rests on.

    Two jobs. The first is `genkart.py`'s: a `.glb` is binary and gitignored, so a
    generated asset is not reviewable on its own, and the manifest is the half a
    reviewer reads - a geometry change shows up as a changed number with a changed
    hash beside it.

    The second is new and is the more important one. `edges` samples the road's two
    edges and its centerline every 25 m in **Godot's frame**, straight out of this
    pipeline's own interpolation. `tools/verify/circuit.sh` asks `KartTrack` for
    the same points and compares them, so "what you see and what you collide with
    cannot drift apart" becomes a number instead of a comment. It is 55 rows for a
    1,375 m lap and it is committed, so the check also survives a machine with no
    Blender on it.
    """
    digest = ""
    if os.path.exists(gltf_path):
        with open(gltf_path, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()

    edges = []
    station = 0.0
    while station < track.length_m:
        frame = track.sample(station)
        half = frame.width_m * 0.5
        edges.append(
            {
                "distance_m": round(station, 6),
                "left": [round(v, 6) for v in frame.surface_point(-half)],
                "centre": [round(v, 6) for v in frame.surface_point(0.0)],
                "right": [round(v, 6) for v in frame.surface_point(half)],
            }
        )
        station += 25.0

    manifest = {
        "generator": "tools/blender/gentrack.py",
        "track": os.path.relpath(track.path, project_root()),
        "track_name": track.name,
        "schema_version": geometry.SCHEMA_VERSION,
        "length_m": round(track.length_m, 6),
        # The .glb's SHA-256 and not its path. The path was here first and
        # `gentrack.sh --check` immediately called the manifest non-deterministic -
        # correctly, because --check writes the two passes into two scratch
        # directories and the relative path is different in each. The hash is the
        # part that carries information; the file lives beside the manifest.
        "sha256": digest,
        "sagitta_m": SAGITTA,
        "max_spacing_m": MAX_SPACING,
        "lightmap_rows": LIGHTMAP_ROWS,
        "parts": [
            {"name": strip.name, "material": strip.material,
             "vertices": len(strip.vertices), "triangles": strip.triangles()}
            for strip in strips
        ],
        # Deliberately no timing. Build duration was in genkart.py's manifest first
        # and the determinism gate immediately flagged two otherwise byte-identical
        # runs as differing - correctly, because a wall-clock number cannot be
        # reproducible.
        "edges": edges,
    }
    os.makedirs(os.path.dirname(os.path.abspath(manifest_path)), exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1, sort_keys=False)
        handle.write("\n")
    return manifest


def check_uvs(strips) -> list[str]:
    """The `uv` stage: assert the two channels rather than compute them.

    UV1 is in meters and is allowed to run off anywhere. UV2 is the lightmap
    channel and must fit inside the unit square - a lightmapper silently wraps
    anything outside it, and the visible symptom is one patch of the circuit lit
    like a different patch of the circuit.
    """
    problems = []
    for strip in strips:
        for u, v in strip.uv2s:
            if u < 0.0 or u > 1.0 or v < 0.0 or v > 1.0:
                problems.append(
                    "%s has a lightmap coordinate outside the unit square: (%.4f, %.4f)"
                    % (strip.name, u, v)
                )
                break
        if len(strip.uvs) != len(strip.vertices) or len(strip.uv2s) != len(strip.vertices):
            problems.append(
                "%s has %d vertices, %d UVs and %d UV2s"
                % (strip.name, len(strip.vertices), len(strip.uvs), len(strip.uv2s))
            )
    return problems


def main() -> int:
    arguments = parse_arguments(script_arguments())
    stages = [stage.strip() for stage in arguments.stages.split(",") if stage.strip()]
    for stage in stages:
        if stage not in STAGES:
            print("gentrack: unknown stage %r; known: %s" % (stage, ", ".join(STAGES)))
            return 2

    root = project_root()
    track_path = arguments.track
    if not os.path.isabs(track_path):
        track_path = os.path.join(root, track_path)
    track = geometry.Track(track_path)

    if not arguments.quiet:
        print("gentrack: %s, %.4f m, %d control points"
              % (track.name, track.length_m, len(track.points)))

    strips = build_strips(track)

    if "uv" in stages:
        problems = check_uvs(strips)
        for problem in problems:
            print("gentrack: %s" % problem)
        if problems:
            return 1

    try:
        import bpy  # noqa: PLC0415
    except ImportError:
        print("gentrack: no bpy, so only the geometry and manifest stages can run.")
        bpy = None

    if bpy is not None:
        if bpy.app.version[:2] != BLENDER_REQUIRED:
            print("gentrack: Blender %s found, this pipeline targets %d.%d (ADR-0012)."
                  % (".".join(str(v) for v in bpy.app.version), *BLENDER_REQUIRED))
        reset_scene(bpy)
        for strip in strips:
            build_object(bpy, strip)

    out_path = arguments.out
    if not os.path.isabs(out_path):
        out_path = os.path.join(root, out_path)

    if bpy is not None and arguments.blend:
        blend_path = arguments.blend if os.path.isabs(arguments.blend) \
            else os.path.join(root, arguments.blend)
        os.makedirs(os.path.dirname(os.path.abspath(blend_path)), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=blend_path)

    if "export" in stages and bpy is not None:
        export_gltf(bpy, out_path)

    manifest_path = arguments.manifest
    if not manifest_path:
        # Beside the **track**, not beside the .glb. `assets/generated/` is
        # gitignored and regenerable; the manifest is the half a reviewer diffs and
        # the half `tools/verify/circuit.sh` cross-checks the collider against, so
        # it has to survive a fresh clone on a machine with no Blender on it.
        manifest_path = os.path.splitext(os.path.splitext(track_path)[0])[0] \
            + ".manifest.json"
    elif not os.path.isabs(manifest_path):
        manifest_path = os.path.join(root, manifest_path)

    if "manifest" in stages:
        manifest = write_manifest(track, strips,
                                  out_path if "export" in stages else "", manifest_path)
        if not arguments.quiet:
            total = sum(part["triangles"] for part in manifest["parts"])
            for part in manifest["parts"]:
                print("  %-14s %7d tris  %-8s" % (part["name"], part["triangles"], part["material"]))
            print("  %-14s %7d tris" % ("TOTAL", total))
            if manifest["sha256"]:
                print("  sha256 %s" % manifest["sha256"])
            print("  -> %s" % os.path.relpath(manifest_path, root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
