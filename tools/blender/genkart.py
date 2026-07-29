"""Generate the KZ kart mesh, headless, from the parameter block.

    blender --background --python tools/blender/genkart.py -- \
            --out assets/generated/kart.glb

Everything after the bare `--` is this script's; Blender consumes what is before
it. `tools/blender/genkart.sh` is the wrapper that finds the right Blender and
checks its version, and is the intended entry point — but this file stays
runnable exactly as ARCHITECTURE.md §11 and the M2 gate spell it out, because a
documented command that is not the real one rots.

**Determinism is an acceptance criterion, not a nicety.** The M2 gate requires
that re-running with identical parameters produces an identical mesh, so:

  * the scene is reset from factory settings, discarding whatever is in the
    user's startup file — a stray default cube would otherwise be exported as
    part of the kart;
  * no stage uses randomness, wall-clock time, or iteration over an unordered
    collection;
  * every object has a fixed name, since names order the exporter's output;
  * the output hash is printed on every run, so a change is visible without
    having to diff a binary.

Verified in practice: two runs produce a byte-identical `.glb`, and the Cycles
normal bake in `--stage bake` is byte-identical too. The one thing that *does*
move the hash is a Blender upgrade — the exporter writes its own version into
the glTF `generator` string. That is a feature. It says the toolchain moved, and
ARCHITECTURE.md §19 asks for exactly that to be a deliberate, visible event.

Stages, in order, each skippable so a geometry change does not pay for a bake:

    geometry   build the kart from parameters
    uv         unwrap, at the §5 texel density               (issue #18)
    bake       normals from the high-poly source             (issue #19)
    lod        decimated chain                               (issue #20)
    export     glTF, Y-up, -Z forward                        (issue #21)

`--watch` is the one mode that is not headless: it keeps a windowed Blender open
and rebuilds the kart in place whenever a file under `kartlib/` is saved, so
shape work is an edit-save-look loop instead of a quit-rerun-reframe one. It
exports nothing and is refused under `--background`, so no gate can reach it.
"""

from __future__ import annotations

import dataclasses
import hashlib
import importlib
import json
import os
import sys
import time
import traceback

import bpy

# Blender runs this file as a script, not as part of a package, so the package
# next to it is not importable until its parent is on the path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from kartlib import build  # noqa: E402
from kartlib import params as P  # noqa: E402

#: Geometry modules, in build order. Each exposes `build_module(context)` and
#: owns its own file — see `build.BuildContext` for what that contract allows.
#:
#: Order is fixed because it determines object creation order, which determines
#: the exporter's node order, which determines the output hash.
MODULES: tuple[tuple[str, str], ...] = (
    ("frame", "#12 frame tubes and floor tray"),
    ("wheels", "#14 wheels, tires and rear axle"),
    ("powertrain", "#15 engine, exhaust and radiator"),
    ("cockpit", "#13 seat, steering wheel and pedals"),
    ("bodywork", "#105 nose fairing, sidepods and rear plastics"),
    ("interior", "#16 cockpit interior"),
    ("driver", "#17 driver with IK-ready arms"),
)

STAGES: tuple[str, ...] = ("geometry", "uv", "bake", "lod", "export")

BLENDER_REQUIRED: tuple[int, int] = (5, 2)


# --- argument parsing ------------------------------------------------------


def script_arguments() -> list[str]:
    """Everything after the bare `--`, which is what Blender leaves for us."""
    if "--" not in sys.argv:
        return []
    return sys.argv[sys.argv.index("--") + 1 :]


def parse(argv: list[str]) -> dict[str, str]:
    """`--key=value` and `--key value` both, plus bare `--flag` as "true".

    Deliberately hand-rolled rather than `argparse`: Blender's own argv is in
    front of ours and argparse's error path calls `sys.exit`, which inside
    Blender leaves the process alive with a half-built scene.
    """
    parsed: dict[str, str] = {}
    index = 0
    while index < len(argv):
        token = argv[index]
        if not token.startswith("--"):
            index += 1
            continue
        token = token[2:]
        if "=" in token:
            key, value = token.split("=", 1)
            parsed[key] = value
        elif index + 1 < len(argv) and not argv[index + 1].startswith("--"):
            parsed[token] = argv[index + 1]
            index += 1
        else:
            parsed[token] = "true"
        index += 1
    return parsed


def parameters_from(arguments: dict[str, str]) -> P.KartParams:
    """The default kart, with `--set field=value` overrides applied.

    Overrides exist for parameter sweeps — "does a 20 mm wider rear track read
    better" is a question that should be one command, not an edit. They are typed
    against the dataclass field so a typo fails loudly instead of being ignored.
    """
    overrides: dict[str, object] = {}
    raw = arguments.get("set", "")
    if raw:
        fields = {f.name: f for f in dataclasses.fields(P.KartParams)}
        for assignment in raw.split(","):
            if not assignment:
                continue
            if "=" not in assignment:
                raise SystemExit("--set wants field=value, got %r" % assignment)
            name, value = assignment.split("=", 1)
            name = name.strip()
            if name not in fields:
                raise SystemExit(
                    "no such parameter %r; see tools/blender/kartlib/params.py" % name
                )
            overrides[name] = _coerce(fields[name].type, value.strip(), name)
    return P.KartParams(**overrides)  # type: ignore[arg-type]


def _coerce(declared: object, value: str, name: str) -> object:
    text = str(declared)
    try:
        if "int" in text and "float" not in text:
            return int(value)
        if "float" in text:
            return float(value)
    except ValueError:
        raise SystemExit("parameter %s wants a number, got %r" % (name, value))
    return value


# --- stages ----------------------------------------------------------------


def prepare_scene() -> tuple[bpy.types.Scene, dict[str, bpy.types.Material]]:
    """An empty scene and the material set, created exactly once.

    Materials are made here rather than inside a build pass because the kart may
    be built twice in one run — low-poly for the game mesh and high-poly as the
    bake source — and calling `make_materials` twice would leave two of everything
    with Blender's `.001` suffixes attached.
    """
    scene = build.reset_scene()
    return scene, build.make_materials()


def build_kart(
    parameters: P.KartParams,
    detail: build.Detail,
    scene: bpy.types.Scene,
    materials: dict[str, bpy.types.Material],
    output_directory: str,
) -> build.BuildContext:
    """Run every geometry module once, at one detail level, into a fresh set of
    collections.

    A module that is not written yet is skipped with a note rather than failing
    the run. That is deliberate for M2: the pipeline has to be end-to-end
    runnable while the geometry tickets are still landing, or the turntable
    cannot be used to review the parts that *are* done — which is the whole
    reason issue #22 is scheduled early.
    """
    collections = build.group_collections(scene)
    context = build.BuildContext(
        params=parameters,
        detail=detail,
        collections=collections,
        materials=materials,
        output_directory=output_directory,
    )

    for module_name, description in MODULES:
        try:
            module = __import__("kartlib.%s" % module_name, fromlist=["build_module"])
        except ImportError:
            print("    skip  %-11s %s (not implemented yet)" % (module_name, description))
            continue
        entry = getattr(module, "build_module", None)
        if entry is None:
            print("    skip  %-11s no build_module()" % module_name)
            continue
        entry(context)
        print("    built %-11s %s" % (module_name, description))

    return context


def check_face_winding(context: build.BuildContext) -> None:
    """Fail the build if any watertight part is wound inside out.

    This exists because `build.box` wound all six of its faces inward and
    `build.lathe` wound both of its axis fans inward, and **every render this
    project has taken looked correct anyway**. Blender materials default to
    `use_backface_culling = False`, so the exporter writes `doubleSided: true`
    and Godot renders the backfaces with the shading normal flipped. The bug was
    found by a subagent measuring signed volumes, not by looking at a kart.

    So the invariant is asserted here rather than trusted: for a closed surface,
    `sum(a . (b x c)) / 6` over its triangles is the enclosed volume, and it is
    positive exactly when every face is wound outward. Meshes that are not
    watertight are skipped rather than guessed at — an open shell has no
    enclosed volume and the sum means nothing. That is a real gap, and it is
    stated rather than papered over: a hollow-backed sidepod is exactly the kind
    of part this cannot check.

    Fatal rather than a warning. The failure mode it guards is a mesh that looks
    right in every viewport and quietly corrupts issue #19's tangent-space bake,
    which is precisely the class of thing a warning gets scrolled past.
    """
    inverted: list[tuple[str, float]] = []
    open_parts: list[str] = []
    for obj in exportable(context):
        if obj.type != "MESH":
            continue
        mesh = obj.data
        # Watertight means every edge is shared by exactly two faces. Cheaper
        # than a full manifold test and sufficient: an open edge is the only way
        # the divergence theorem stops applying here.
        faces_per_edge: dict[int, int] = {}
        for polygon in mesh.polygons:
            for loop_index in polygon.loop_indices:
                edge_index = mesh.loops[loop_index].edge_index
                faces_per_edge[edge_index] = faces_per_edge.get(edge_index, 0) + 1
        if not faces_per_edge or any(count != 2 for count in faces_per_edge.values()):
            open_parts.append(obj.name)
            continue

        volume = 0.0
        for polygon in mesh.polygons:
            corners = [mesh.vertices[index].co for index in polygon.vertices]
            for index in range(1, len(corners) - 1):
                a, b, c = corners[0], corners[index], corners[index + 1]
                volume += a.dot(b.cross(c)) / 6.0
        if volume < 0.0:
            inverted.append((obj.name, volume))

    if inverted:
        listing = "\n".join(
            "      %-28s %+.6f m3" % (name, volume) for name, volume in inverted
        )
        raise SystemExit(
            "error: %d part(s) are wound inside out:\n%s\n"
            "       A closed mesh must enclose a positive volume. Check the face\n"
            "       tuples in whatever built these -- and note that a render will\n"
            "       not show it, because the materials export doubleSided."
            % (len(inverted), listing)
        )
    meshes = sum(1 for obj in exportable(context) if obj.type == "MESH")
    # The open parts are named rather than counted. A part that is not watertight
    # is not necessarily wrong -- a hollow shell genuinely is not -- but it is
    # unchecked, and an unchecked part appearing in this list without a reason is
    # itself the finding. Silently reporting "n skipped" would hide exactly that.
    print(
        "    winding  %d/%d watertight part(s) enclose positive volume%s"
        % (
            meshes - len(open_parts),
            meshes,
            ("; not watertight, unchecked: " + ", ".join(open_parts))
            if open_parts
            else "",
        )
    )


def suffix_pass(context: build.BuildContext, suffix: str) -> None:
    """Rename every object, mesh and collection in a pass.

    Blender object names are unique across the whole file, so building the kart
    twice would give the second pass `chassis_rail_r.001` — a name containing a
    counter, which is the one thing rule 3 in `build.py` forbids, and which would
    also make the low-poly's name depend on whether a bake happened.

    Renaming the high-poly pass out of the way first means the low-poly pass gets
    the clean names unconditionally. Done in sorted order so the rename itself
    cannot introduce an ordering dependency.
    """
    for group in build.GROUPS:
        collection = context.collections[group]
        for obj in sorted(collection.objects, key=lambda item: item.name):
            if obj.data is not None:
                obj.data.name = obj.data.name + suffix
            obj.name = obj.name + suffix
        collection.name = collection.name + suffix


def pair_high_poly(
    low: build.BuildContext, high: build.BuildContext, suffix: str
) -> dict[str, bpy.types.Object]:
    """Match each low-poly object to its high-poly twin, by name.

    Both passes run the same modules over the same parameters, so the names line
    up exactly and the pairing needs no heuristics. Anything unmatched is reported
    rather than skipped silently — an unpaired low-poly object would bake a flat
    normal map and look subtly wrong rather than obviously broken.
    """
    high_by_name = {}
    for group in build.GROUPS:
        for obj in high.collections[group].objects:
            if obj.type == "MESH":
                high_by_name[obj.name] = obj

    paired: dict[str, bpy.types.Object] = {}
    unmatched: list[str] = []
    for group in build.GROUPS:
        for obj in sorted(low.collections[group].objects, key=lambda item: item.name):
            if obj.type != "MESH":
                continue
            twin = high_by_name.get(obj.name + suffix)
            if twin is None:
                unmatched.append(obj.name)
            else:
                paired[obj.name] = twin

    if unmatched:
        print(
            "    warning: %d object(s) have no high-poly twin and will bake flat: %s"
            % (len(unmatched), ", ".join(unmatched[:6]))
        )
    return paired


def discard_pass(context: build.BuildContext) -> None:
    """Delete a build pass and its collections.

    The high-poly source has done its job once the bake has read it, and leaving
    it in the file would double the memory, appear in a `--blend` dump, and invite
    somebody to export it by accident.
    """
    for group in build.GROUPS:
        collection = context.collections[group]
        for obj in list(collection.objects):
            data = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if data is not None and data.users == 0 and isinstance(data, bpy.types.Mesh):
                bpy.data.meshes.remove(data)
        bpy.data.collections.remove(collection)
    # Flush the deletions before anything iterates the view layer again. Without
    # this the export's deselect pass walks a list still holding the removed
    # objects and fails on the first one.
    bpy.context.view_layer.update()


def exportable(context: build.BuildContext) -> list[bpy.types.Object]:
    """Every object to export, ordered by group and then by name.

    Sorted rather than left in `bpy.data` order: the data-block order depends on
    creation *and* on name interning, and sorting makes the export order a
    function of the names alone.
    """
    objects: list[bpy.types.Object] = []
    for group in build.GROUPS:
        collection = context.collections[group]
        objects.extend(sorted(collection.objects, key=lambda obj: obj.name))
    return objects


def export_gltf(context: build.BuildContext, path: str) -> None:
    """glTF 2.0, Y-up, -Z forward, per ARCHITECTURE.md §11 import conventions.

    `export_yup=True` is the whole scale-and-orientation story: it maps Blender
    (x, y, z) to glTF (x, z, -y), so the kart built toward Blender +Y arrives
    facing glTF -Z, which is Godot's forward. Issue #21 checks that in Godot
    rather than trusting it here.
    """
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    build.select_only(exportable(context))
    bpy.ops.export_scene.gltf(
        filepath=path,
        export_format="GLB",
        export_yup=True,
        use_selection=True,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        # Godot generates tangents on import when a normal map needs them, and an
        # exported tangent array is four more floats per vertex for the same
        # result.
        export_tangents=False,
        export_materials="EXPORT",
        export_cameras=False,
        export_lights=False,
        # Custom properties would carry Blender-side bookkeeping into the runtime
        # asset, and this is also a determinism risk: the iteration order of a
        # Blender ID property dict is not guaranteed stable.
        export_extras=False,
        export_animations=False,
        # The driver's arms are skinned for the M4 IK rig (issue #17).
        export_skins=True,
    )


def write_manifest(
    context: build.BuildContext,
    gltf_path: str,
    manifest_path: str,
    elapsed: float,
) -> dict[str, object]:
    """A text sidecar describing the run: parameters, counts, and the hash.

    The `.glb` is binary and gitignored, so on its own a generated kart is not
    reviewable. The manifest is the diffable half — a parameter change shows up
    as a changed number and a changed hash next to it, which is what makes "an
    identical mesh" checkable by reading rather than by re-running.
    """
    objects = exportable(context)
    parts = []
    for obj in objects:
        if obj.type != "MESH":
            continue
        parts.append(
            {
                "name": obj.name,
                "vertices": len(obj.data.vertices),
                "triangles": sum(
                    max(0, len(polygon.vertices) - 2) for polygon in obj.data.polygons
                ),
                "area_m2": round(build.surface_area(obj), 6),
                "materials": [material.name for material in obj.data.materials],
            }
        )

    digest = ""
    if os.path.exists(gltf_path):
        with open(gltf_path, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()

    manifest = {
        "generator": "tools/blender/genkart.py",
        "blender": ".".join(str(number) for number in bpy.app.version),
        "detail": context.detail.name,
        "gltf": os.path.relpath(gltf_path, _project_root()),
        "sha256": digest,
        # Deliberately no timing here. Build duration was in this dict first, and
        # `genkart.sh --check` immediately flagged the manifests as differing
        # between two otherwise byte-identical runs -- correctly, because a
        # wall-clock number cannot be reproducible. The manifest is the diffable
        # half of a reproducible mesh, so nothing that varies run to run belongs in
        # it. The duration is still printed to stdout, where it is information
        # rather than a record.
        "totals": {
            "objects": len(objects),
            "meshes": len(parts),
            "vertices": sum(int(part["vertices"]) for part in parts),
            "triangles": sum(int(part["triangles"]) for part in parts),
        },
        "pivots": sorted(context.pivots),
        "parameters": {
            name: (list(value) if isinstance(value, tuple) else value)
            for name, value in context.params.as_ordered_items()
        },
        "parts": parts,
    }

    os.makedirs(os.path.dirname(os.path.abspath(manifest_path)), exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=False)
        handle.write("\n")
    return manifest


def _project_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


# --- entry point -----------------------------------------------------------


# --- live rebuild ----------------------------------------------------------
#
# Everything below serves `--watch` and nothing else. It is deliberately walled
# off from the build path the gates run: a determinism failure that traced back
# to a convenience loop would be a bad trade.


#: Seconds between mtime polls. Short enough that a save feels immediate, long
#: enough that the timer is not a measurable cost while nothing is changing.
WATCH_INTERVAL: float = 0.4


def watch_sources() -> list[str]:
    """Every file whose edit should trigger a rebuild.

    The geometry modules and the two files all of them import. `genkart.py`
    itself is deliberately absent: it is `__main__` here, so reloading it would
    re-enter `main()` and start a second watcher on top of the first. Editing
    this file means restarting the session, which is the honest cost of it being
    the entry point.
    """
    package = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kartlib")
    return sorted(
        os.path.join(package, name)
        for name in os.listdir(package)
        if name.endswith(".py")
    )


def source_stamp(paths: list[str]) -> dict[str, int | None]:
    """Modification times, with a missing file recorded rather than skipped.

    An editor that writes by rename makes the file briefly absent, and treating
    that as "no change" would drop the save that follows it.
    """
    stamp: dict[str, int | None] = {}
    for path in paths:
        try:
            stamp[path] = os.stat(path).st_mtime_ns
        except OSError:
            stamp[path] = None
    return stamp


def reload_modules() -> None:
    """Reload `kartlib` in dependency order.

    `importlib.reload` mutates the module object in place, so every
    `from . import build` binding inside the geometry modules keeps pointing at
    the same object and sees the new code without being reloaded itself. That
    only holds because this package imports *modules* and never names out of
    them — one `from .params import KartParams` anywhere would go stale here and
    silently keep building the old shape, which is the kind of bug that gets
    blamed on the mesh.

    `params` before `build` because `build` imports it; the geometry modules
    last because they import both.
    """
    for name in ("params", "build"):
        module = sys.modules.get("kartlib." + name)
        if module is not None:
            importlib.reload(module)
    for module_name, _ in MODULES:
        module = sys.modules.get("kartlib." + module_name)
        if module is not None:
            importlib.reload(module)


def wipe_kart() -> None:
    """Delete the built kart without touching the rest of the file.

    A normal run calls `build.reset_scene()`, which calls
    `read_factory_settings` — that discards the window layout and the viewport's
    own view matrix along with the scene. In a live loop it would snap the view
    back to the default on every save, which is exactly what the loop exists to
    avoid. So the group collections are emptied by hand instead.

    Orphans are purged because every mesh and material is created *by name*:
    leave one behind and the next build gets `chassis_rail_r.001`, the outliner
    is unreadable after four saves, and `make_materials` starts handing out
    duplicates that look like the shading changed.
    """
    for name in build.GROUPS:
        collection = bpy.data.collections.get(name)
        if collection is None:
            continue
        for obj in list(collection.all_objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        for child in list(collection.children):
            bpy.data.collections.remove(child)
        bpy.data.collections.remove(collection)
    bpy.data.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)


def rebuild(arguments: dict[str, str], out_directory: str) -> tuple[int, int]:
    """One in-place rebuild: reload the sources, wipe, build again.

    Parameters are re-read from `arguments` every time so that a `--set=` on the
    launch command still applies, and so that an edit to `params.py` is picked
    up — the dataclass is rebuilt by the reload above, and the defaults come off
    the new one.

    Returns (meshes, vertices) so the loop can print them. That is not decoration:
    a live rebuild is a fast, silent success, and a silent success is
    indistinguishable from an edit that changed nothing. A vertex count that did
    not move says the save did not reach the mesh.
    """
    reload_modules()
    parameters = parameters_from(arguments)
    detail = (
        build.Detail.high(parameters)
        if arguments.get("detail", "low") == "high"
        else build.Detail.low(parameters)
    )
    wipe_kart()
    materials = build.make_materials()
    context = build_kart(parameters, detail, bpy.context.scene, materials, out_directory)
    check_face_winding(context)

    meshes = [obj for obj in exportable(context) if obj.type == "MESH"]
    return len(meshes), sum(len(obj.data.vertices) for obj in meshes)


def watch(arguments: dict[str, str], out_directory: str) -> None:
    """Poll `kartlib/` and rebuild on change, forever.

    `main` refuses `--background` before building anything; by the time this
    runs the window is real.
    """
    sources = watch_sources()
    state = {"stamp": source_stamp(sources), "builds": 0}

    def poll() -> float:
        stamp = source_stamp(sources)
        if stamp != state["stamp"]:
            state["stamp"] = stamp
            state["builds"] += 1
            started = time.perf_counter()
            try:
                meshes, vertices = rebuild(arguments, out_directory)
            except Exception:
                # A half-written file raises SyntaxError and a wrong dimension
                # raises whatever the module raises. Neither may kill the timer:
                # fixing the file and saving again is the entire loop, and a
                # watcher that dies on the first typo is worse than no watcher,
                # because it dies silently and the next save does nothing.
                traceback.print_exc()
                print(
                    "==> rebuild %d FAILED -- still watching, save again to retry"
                    % state["builds"],
                    flush=True,
                )
            else:
                print(
                    "==> rebuild %d in %.1f s -- %d meshes, %s verts"
                    % (
                        state["builds"],
                        time.perf_counter() - started,
                        meshes,
                        "{:,}".format(vertices),
                    ),
                    flush=True,
                )
        return WATCH_INTERVAL

    bpy.app.timers.register(poll, first_interval=WATCH_INTERVAL, persistent=True)
    print(
        "==> watching %d files under kartlib/ -- save one to rebuild"
        % len(sources),
        flush=True,
    )


def main() -> None:
    arguments = parse(script_arguments())

    if bpy.app.version[:2] != BLENDER_REQUIRED:
        print(
            "warning: this script targets Blender %d.%d and bpy shifts between "
            "major versions (ADR-0012); running on %s"
            % (*BLENDER_REQUIRED, bpy.app.version_string),
            file=sys.stderr,
        )

    out = arguments.get("out", "assets/generated/kart.glb")
    if not os.path.isabs(out):
        out = os.path.join(_project_root(), out)
    manifest_path = arguments.get("manifest", os.path.splitext(out)[0] + ".json")
    if not os.path.isabs(manifest_path):
        manifest_path = os.path.join(_project_root(), manifest_path)

    # `--watch` is a shape-review loop, so it defaults to geometry only: a bake
    # on every keystroke-and-save is 20 s of Cycles nobody asked for, and an
    # export would rewrite assets/generated/ from a half-finished module. An
    # explicit --stages still wins, because someone watching a UV change has a
    # reason.
    watching = arguments.get("watch") == "true"
    # Checked before anything is built. Doing it at the point of use meant a
    # full headless build ran and *then* the mode was refused, which reads as a
    # successful run with an error stapled to the end of it.
    if watching and bpy.app.background:
        raise SystemExit(
            "--watch needs a windowed Blender: the rebuild runs off\n"
            "bpy.app.timers, and headless Blender has no event loop to fire it.\n"
            "Drop --background, or run\n"
            "    tools/blender/genkart.sh --watch\n"
            "which does it for you."
        )
    requested = arguments.get("stages", "geometry" if watching else "all")
    stages = set(STAGES) if requested == "all" else {
        stage.strip() for stage in requested.split(",") if stage.strip()
    }
    unknown = stages - set(STAGES)
    if unknown:
        raise SystemExit(
            "unknown stage(s) %s; the stages are %s"
            % (", ".join(sorted(unknown)), ", ".join(STAGES))
        )

    parameters = parameters_from(arguments)
    detail = (
        build.Detail.high(parameters)
        if arguments.get("detail", "low") == "high"
        else build.Detail.low(parameters)
    )

    out_directory = os.path.dirname(os.path.abspath(out))

    started = time.perf_counter()
    scene, materials = prepare_scene()

    # The bake needs the high-poly source in the same scene at the same
    # coordinates, so it is built first and renamed out of the way. Skipping the
    # bake skips building it at all, which is most of why `--stages` exists: the
    # high-poly pass is the expensive half of a run.
    high_suffix = "_high"
    high_context: build.BuildContext | None = None
    if "bake" in stages:
        print("==> geometry (high detail, bake source)")
        high_context = build_kart(
            parameters, build.Detail.high(parameters), scene, materials, out_directory
        )
        suffix_pass(high_context, high_suffix)

    print("==> geometry (%s detail)" % detail.name)
    context = build_kart(parameters, detail, scene, materials, out_directory)
    check_face_winding(context)
    if high_context is not None:
        context.high_poly = pair_high_poly(context, high_context, high_suffix)

    for stage in ("uv", "bake", "lod"):
        if stage in stages:
            runner = _optional_stage(stage)
            if runner is None:
                print("==> %s: not implemented yet, skipped" % stage)
            else:
                print("==> %s" % stage)
                runner(context)

    if high_context is not None:
        discard_pass(high_context)

    if "export" in stages:
        print("==> export")
        export_gltf(context, out)

    blend = arguments.get("blend")
    if blend:
        if not os.path.isabs(blend):
            blend = os.path.join(_project_root(), blend)
        os.makedirs(os.path.dirname(blend), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=blend, compress=False)
        print("    blend %s" % blend)

    # The manifest describes an exported mesh, and a watch session never exports
    # one. Writing it anyway would leave assets/generated/kart.json describing a
    # .glb that was not rebuilt with it — the same stale-caption failure the
    # SKIP_IMPORT trap produces, arriving from the other end.
    if watching:
        elapsed = time.perf_counter() - started
        print("==> first build in %.1f s" % elapsed)
        watch(arguments, out_directory)
        return

    elapsed = time.perf_counter() - started
    manifest = write_manifest(context, out, manifest_path, elapsed)
    totals = manifest["totals"]

    print(
        "==> %s\n    %d objects, %d meshes, %s verts, %s tris in %.1f s"
        % (
            os.path.relpath(out, _project_root()),
            totals["objects"],
            totals["meshes"],
            "{:,}".format(totals["vertices"]),
            "{:,}".format(totals["triangles"]),
            elapsed,
        )
    )
    print("    sha256 %s" % manifest["sha256"])


def _optional_stage(name: str):
    """Look up a stage module that may not be written yet.

    Same reasoning as the geometry modules: the pipeline stays runnable while
    issues #18 to #20 are still open, so the turntable can review geometry
    before the unwrap exists.
    """
    try:
        module = __import__("kartlib.%s_stage" % name, fromlist=["run"])
    except ImportError:
        return None
    return getattr(module, "run", None)


if __name__ == "__main__":
    main()
