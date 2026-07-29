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
from mathutils import Vector
from mathutils.bvhtree import BVHTree

# Blender runs this file as a script, not as part of a package, so the package
# next to it is not importable until its parent is on the path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from kartlib import build  # noqa: E402
from kartlib import joints  # noqa: E402
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

STAGES: tuple[str, ...] = ("geometry", "finish", "uv", "bake", "lod", "export")

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


def check_parameter_coverage() -> None:
    """Fail the build if a `KartParams` field is read by no module. Issue #190.

    Spec §10.7's recommendation, and it is the same shape of check as
    `joints.py`'s "a pattern that matches nothing is fatal": a parameter that no
    mesh reads cannot be verified against anything, so it drifts away from the
    kart in silence. Four had. `frame_height` was read by nothing at all until a
    seat strut was pointed at it; `nose_width` says 680 mm while
    `bodywork.NOSE_HALF_WIDTH_LIMIT` builds 512; `tray_width`/`tray_length`
    described a rectangle Art. 4.6 forbids; `lod_ratios` is read by a stage module
    that does not exist.

    Fatal rather than a warning, and printed when it passes, because a gate that
    is silent on success is a gate nobody notices has stopped running. The
    exemption list lives in `params.py` beside the fields it excuses.
    """
    package = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kartlib")
    checked, exempt = P.check_field_coverage(package)
    print(
        "    params   %d/%d field(s) read by at least one module; %d exempt"
        % (checked - exempt, checked, exempt)
    )


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


#: How many points per part gate 2 samples, spread evenly by index.
#:
#: By index rather than by area, because index order is a property of the mesh and
#: is therefore reproducible; picking points by area would need a tie-break that
#: is not.
GAP_SAMPLES: int = 160

#: How many of those sample points are used as seeds for the refinement below, per
#: direction. The first probe is a coarse net; the refinement is what makes the
#: number mean anything.
GAP_SEEDS: int = 4

#: Alternating-projection steps per seed. Six is where the numbers stopped moving:
#: measured on `bodywork_sidepod_r` against `chassis_side_bar_r`, which reads
#: 14.08 mm from vertices alone and 1.27 mm after refinement.
GAP_REFINE: int = 6


@dataclasses.dataclass
class Part:
    """One exportable mesh, prepared in **world space** for both gates.

    `BVHTree.FromObject` builds in the object's *local* space, so overlapping two
    of them compares two parts as if both sat at the origin — which reports the
    seat as intersecting all four tires. Already in CLAUDE.md as having cost real
    time once, so the triangles are transformed by hand and handed to
    `FromPolygons`.
    """

    name: str
    tree: BVHTree
    samples: list[Vector]
    low: Vector
    high: Vector

    def bounds_gap(self, other: Part) -> float:
        """Distance between the two axis-aligned boxes; 0.0 if they overlap.

        A lower bound on the true surface gap, which is what makes it useful
        twice: gate 1 rejects a pair outright when it is positive, and gate 2
        stops walking candidates once it exceeds the best gap found so far.
        """
        squared = 0.0
        for axis in range(3):
            separation = max(
                self.low[axis] - other.high[axis],
                other.low[axis] - self.high[axis],
                0.0,
            )
            squared += separation * separation
        return squared**0.5


def assembly_parts(context: build.BuildContext) -> list[Part]:
    """Every exportable mesh as world-space triangles, a BVH and a sample set.

    Built once and handed to both gates: the trees are the expensive half of gate
    1 and all of gate 2's input, and building them twice would double the only
    part of this that shows up in a `--watch` rebuild.

    Sample points are vertices *and* triangle centroids. Vertices alone are a bad
    net on a coarse part — a flat panel's nearest point to a tube is in the middle
    of a triangle, nowhere near a corner — and that showed up as the same joint
    measuring 14.08 mm at low detail and 1.72 mm at high, which would have failed
    a gate at one density and passed at the other.
    """
    parts: list[Part] = []
    for obj in exportable(context):
        if obj.type != "MESH":
            continue
        matrix = obj.matrix_world
        vertices = [matrix @ vertex.co for vertex in obj.data.vertices]
        triangles: list[tuple[int, int, int]] = []
        for polygon in obj.data.polygons:
            loop = list(polygon.vertices)
            for index in range(1, len(loop) - 1):
                triangles.append((loop[0], loop[index], loop[index + 1]))
        if not triangles:
            continue

        candidates = list(vertices)
        for a, b, c in triangles:
            candidates.append((vertices[a] + vertices[b] + vertices[c]) / 3.0)
        stride = max(1, len(candidates) // GAP_SAMPLES)

        corners = [matrix @ Vector(corner) for corner in obj.bound_box]
        parts.append(
            Part(
                name=obj.name,
                tree=BVHTree.FromPolygons(vertices, triangles, all_triangles=True),
                samples=candidates[::stride][:GAP_SAMPLES],
                low=Vector(
                    (
                        min(point.x for point in corners),
                        min(point.y for point in corners),
                        min(point.z for point in corners),
                    )
                ),
                high=Vector(
                    (
                        max(point.x for point in corners),
                        max(point.y for point in corners),
                        max(point.z for point in corners),
                    )
                ),
            )
        )
    # `exportable` already sorts, but gate output ordering is an acceptance
    # concern rather than a convenience, so this does not depend on that.
    parts.sort(key=lambda part: part.name)
    return parts


def check_interpenetration(parts: list[Part]) -> set[tuple[str, str]]:
    """Fail the build if any part is built inside another it does not join.

    The radiator was built through the gear lever, through the exhaust chamber and
    through the right sidepod for several milestones — 593 intersecting triangle
    pairs — and every render looked fine, because a part inside another part is
    hidden by the thing it is inside. It was found by a human turning a viewport,
    which is not a loop that survives an unattended run.

    `kartlib/joints.py` is the allowlist and it is not a list of name pairs: a
    declared `Joint` permits this overlap **and** obliges gate 2 to find the two
    parts touching, so one entry cannot rot into permission for a collision. Very
    roughly 200 of the ~218 overlapping pairs here are legitimate — a nut threaded
    into a casting, a tube welded to a tube, a tire bead inside a rim flange, a
    chain over a sprocket, a plug in a head, a hose entering a tank — and which
    ones took reading the code that builds each part rather than reading names.

    Returns the overlapping pairs, because gate 2 needs them: two parts that
    intersect have a surface gap of exactly zero, and sampling for it would report
    a bearing hanger 12 mm from an axle that runs straight through it.
    """
    started = time.perf_counter()
    names = [part.name for part in parts]
    declared = joints.declared(names)
    waived = joints.waived("overlap", names)

    considered = 0
    rejected = 0
    overlapping: set[tuple[str, str]] = set()
    counts: dict[tuple[str, str], int] = {}
    for index, part in enumerate(parts):
        for other in parts[index + 1 :]:
            considered += 1
            # Bounds first: 146 meshes is 10,585 pairs and the tree overlap is
            # two orders of magnitude dearer than six comparisons.
            if part.bounds_gap(other) > 0.0:
                rejected += 1
                continue
            hits = part.tree.overlap(other.tree)
            if hits:
                pair = (part.name, other.name)
                overlapping.add(pair)
                counts[pair] = len(hits)

    fatal: list[tuple[tuple[str, str], int]] = []
    excused: list[tuple[tuple[str, str], int, joints.Defect]] = []
    # What "failing" means here is *undeclared* overlap, not overlap. A waiver on
    # a pair that is also a declared joint would otherwise never be reported
    # stale, because the joint keeps the pair overlapping forever -- and that is
    # exactly the waiver that has stopped meaning anything.
    failing: set[tuple[str, str]] = set()
    for pair in sorted(overlapping):
        if pair in declared:
            continue
        failing.add(pair)
        defect = waived.get(pair)
        if defect is None:
            fatal.append((pair, counts[pair]))
        else:
            excused.append((pair, counts[pair], defect))

    if fatal:
        listing = "\n".join(
            "      %-28s %-28s %4d triangle pair(s)" % (pair[0], pair[1], count)
            for pair, count in fatal
        )
        raise SystemExit(
            "error: %d pair(s) of parts are built inside each other:\n%s\n"
            "       Either the two really do collide -- in which case move one, "
            "and note that\n"
            "       no render will show you the problem -- or they are joined, in "
            "which case\n"
            "       declare the joint in tools/blender/kartlib/joints.py with what "
            "physically\n"
            "       holds them together. A declared joint also has to *touch*, "
            "which is\n"
            "       deliberate: it is one fact about the kart, not two lists."
            % (len(fatal), listing)
        )

    stale = joints.stale_waivers("overlap", names, failing)
    if stale:
        listing = "\n".join(
            "      %-28s %-28s %s" % (defect.a, defect.b, defect.issue)
            for defect in stale
        )
        raise SystemExit(
            "error: %d overlap waiver(s) in joints.py no longer cover a failing "
            "pair:\n%s\n"
            "       This is fixed. Delete the waiver -- a waiver list that keeps "
            "entries for\n"
            "       faults that are gone stops being a list of what is broken."
            % (len(stale), listing)
        )

    for pair, count, defect in excused:
        print(
            "    warning: %s and %s intersect, %d triangle pair(s) -- known, %s"
            % (pair[0], pair[1], count, defect.issue)
        )
    print(
        "    overlap  %d/%d pair(s) intersect: %d declared, %d known-open; "
        "%d rejected on bounds, %.0f ms"
        % (
            len(overlapping),
            considered,
            len(overlapping) - len(excused),
            len(excused),
            rejected,
            (time.perf_counter() - started) * 1000.0,
        )
    )
    return overlapping


def check_attachment(parts: list[Part], overlapping: set[tuple[str, str]]) -> None:
    """Fail the build if a part touches nothing, or misses what it mounts to.

    `steering_column`'s lower bearing bottoms out 13.2 mm above the crown of
    `chassis_steering_hoop`, whose whole stated purpose in `frame.py` is *"so that
    the column has something to be mounted to rather than floating"*. Nothing
    objected, and nothing could: a 13 mm gap behind a steering wheel is invisible
    from every camera angle this project has ever rendered.

    Two forms, because the cheap one is not enough:

    **weak** — a part whose nearest neighbor over the whole kart is further than
    `joints.CONTACT_TOLERANCE` is floating. This catches a part nobody thought
    about, including one that has no joint declared at all.

    **strong** — every declared joint's two parts must be that close *to each
    other*. A sidepod resting against the engine instead of against its side bar
    passes the weak form and is still wrong, and that is not hypothetical: the
    right pod is 26.7 mm inside the crankcase and 1.3 mm off the bar it bolts to.

    The measurement is an upper bound on the true surface gap and says so: sample
    points on A, nearest surface point on B, then alternate the projection a few
    times from the best seeds. That converges on a local minimum fast and is what
    makes the figure agree between detail levels — vertices alone put one joint at
    14.08 mm low and 1.72 mm high. An upper bound is the safe direction for a
    gate that fails on *large* gaps, and refinement is what stops it crying wolf.
    """
    started = time.perf_counter()
    names = [part.name for part in parts]
    by_name = {part.name: part for part in parts}
    declared = joints.declared(names)
    waived = joints.waived("gap", names)
    tolerance = joints.CONTACT_TOLERANCE

    def surface_gap(a: Part, b: Part) -> float:
        if (a.name, b.name) in overlapping or (b.name, a.name) in overlapping:
            return 0.0
        seeds: list[tuple[float, Vector, bool]] = []
        for source, target, forward in ((a, b, True), (b, a, False)):
            hits = []
            for point in source.samples:
                location, _normal, _index, distance = target.tree.find_nearest(point)
                if location is not None:
                    hits.append((distance, point, forward))
            hits.sort(key=lambda hit: hit[0])
            seeds.extend(hits[:GAP_SEEDS])

        best = float("inf")
        for _distance, point, forward in seeds:
            here, there = (a.tree, b.tree) if forward else (b.tree, a.tree)
            current = point
            for _step in range(GAP_REFINE):
                location, _normal, _index, distance = there.find_nearest(current)
                if location is None:
                    break
                best = min(best, distance)
                current = location
                here, there = there, here
        return best

    # --- weak: is anything floating? ---
    floating: list[tuple[str, str, float]] = []
    excused_weak: list[tuple[str, str, float, joints.Defect]] = []
    for part in parts:
        best_gap = float("inf")
        best_name = ""
        for candidate in sorted(parts, key=lambda other: part.bounds_gap(other)):
            if candidate.name == part.name:
                continue
            # The bound is a lower bound on the gap, so once it exceeds the best
            # surface gap found so far, nothing further out can win.
            if part.bounds_gap(candidate) > best_gap:
                break
            gap = surface_gap(part, candidate)
            if gap < best_gap:
                best_gap, best_name = gap, candidate.name
        if best_gap > tolerance:
            defect = joints.waives_part(part.name)
            if defect is None:
                floating.append((part.name, best_name, best_gap))
            else:
                excused_weak.append((part.name, best_name, best_gap, defect))

    if floating:
        listing = "\n".join(
            "      %-28s nearest %-24s %7.2f mm" % (name, other, gap * 1000.0)
            for name, other, gap in floating
        )
        raise SystemExit(
            "error: %d part(s) touch nothing on the kart:\n%s\n"
            "       The tolerance is %.1f mm. A part that is not within that of "
            "anything is\n"
            "       floating in space, and no render will show it -- move it onto "
            "whatever\n"
            "       carries it, and declare that joint in joints.py so the next "
            "edit cannot\n"
            "       quietly move it off again."
            % (len(floating), listing, tolerance * 1000.0)
        )

    # --- strong: does every declared joint actually touch? ---
    broken: list[tuple[tuple[str, str], joints.Joint, float]] = []
    excused_strong: list[tuple[tuple[str, str], float, joints.Defect]] = []
    failing: set[tuple[str, str]] = set()
    gaps: dict[tuple[str, str], float] = {}
    for pair in sorted(declared):
        gap = surface_gap(by_name[pair[0]], by_name[pair[1]])
        gaps[pair] = gap
        if gap <= tolerance:
            continue
        failing.add(pair)
        defect = waived.get(pair)
        if defect is None:
            broken.append((pair, declared[pair], gap))
        else:
            excused_strong.append((pair, gap, defect))

    if broken:
        listing = "\n".join(
            "      %-26s %-26s %7.2f mm  (%s)"
            % (pair[0], pair[1], gap * 1000.0, joint.kind)
            for pair, joint, gap in broken
        )
        raise SystemExit(
            "error: %d declared joint(s) are not in contact:\n%s\n"
            "       joints.py says these parts are joined and the mesh says they "
            "are %.1f mm\n"
            "       or more apart. One of the two is wrong. If the joint is real, "
            "move the\n"
            "       part; if it is not, delete the entry -- but read its `why` "
            "first, because\n"
            "       it says what physically holds the two together."
            % (len(broken), listing, tolerance * 1000.0)
        )

    stale = joints.stale_waivers("gap", names, failing)
    if stale:
        listing = "\n".join(
            "      %-26s %-26s %s" % (defect.a, defect.b, defect.issue)
            for defect in stale
        )
        raise SystemExit(
            "error: %d gap waiver(s) in joints.py no longer cover a failing "
            "joint:\n%s\n"
            "       This is fixed. Delete the waiver."
            % (len(stale), listing)
        )

    for name, other, gap, defect in excused_weak:
        print(
            "    warning: %s touches nothing; nearest is %s at %.2f mm -- known, %s"
            % (name, other, gap * 1000.0, defect.issue)
        )
    for pair, gap, defect in excused_strong:
        print(
            "    warning: joint %s/%s is %.2f mm apart -- known, %s"
            % (pair[0], pair[1], gap * 1000.0, defect.issue)
        )
    widest = max(gaps.items(), key=lambda item: item[1]) if gaps else None
    print(
        "    attach   %d part(s) within %.1f mm of a neighbor (%d known-open); "
        "%d declared joint(s) in contact (%d known-open), widest %s, %.0f ms"
        % (
            len(parts) - len(excused_weak),
            tolerance * 1000.0,
            len(excused_weak),
            len(declared) - len(excused_strong),
            len(excused_strong),
            (
                "%s/%s %.2f mm" % (widest[0][0], widest[0][1], widest[1] * 1000.0)
                if widest
                else "n/a"
            ),
            (time.perf_counter() - started) * 1000.0,
        )
    )


def check_assembly(context: build.BuildContext) -> None:
    """Both #192 gates, over one shared set of world-space trees.

    Called where `check_face_winding` is called and for the same reason: these are
    invariants a render cannot show you, so they are asserted on every build
    rather than reviewed. Measured at 0.34 s for the two of them at high detail
    against a 1.7 s rebuild, which is inside the budget `--watch` has to keep, so
    neither half is skipped anywhere and there is no mode in which the kart is
    built without being checked.
    """
    started = time.perf_counter()
    parts = assembly_parts(context)
    prepared = (time.perf_counter() - started) * 1000.0
    overlapping = check_interpenetration(parts)
    check_attachment(parts, overlapping)
    print("    assembly %d part(s) prepared in %.0f ms" % (len(parts), prepared))


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


def build_totals(context: build.BuildContext) -> dict[str, int]:
    """Object, mesh, vertex and triangle counts for the scene as it stands.

    Split out of `write_manifest` so that a run which skipped the export stage can
    still report what it built. It must not report a hash — see the reasoning at
    its call site.
    """
    objects = exportable(context)
    meshes = [obj for obj in objects if obj.type == "MESH"]
    return {
        "objects": len(objects),
        "meshes": len(meshes),
        "vertices": sum(len(obj.data.vertices) for obj in meshes),
        "triangles": sum(
            max(0, len(polygon.vertices) - 2)
            for obj in meshes
            for polygon in obj.data.polygons
        ),
    }


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
    check_parameter_coverage()
    check_face_winding(context)
    check_assembly(context)

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
    check_parameter_coverage()
    check_face_winding(context)
    # Before the high-poly pass is paired in, and before any stage renames or
    # decimates anything: both gates measure the kart as it was built.
    check_assembly(context)
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

    # ...and the same is true of **any** run that skipped the export stage, which
    # the check above missed for a milestone because `--watch` was the only way
    # anybody had reached it. `--stages=geometry` builds a fresh scene, does not
    # rewrite the .glb, and then hashed whatever .glb was already on disk: three
    # different meshes of 37,768, 38,499 and 38,583 vertices all printed
    # `ffc0094e…` while `totals` came from the fresh scene. A stale hash under
    # fresh counts is worse than no hash, because `kartview.gd`'s STALE IMPORT
    # check reads this file to decide whether the mesh it is showing is current —
    # so the one guard against a stale kart was being fed a stale manifest.
    #
    # No export, no manifest. The counts still print, because they are the reason
    # to run a geometry-only build in the first place.
    if "export" not in stages:
        totals = build_totals(context)
        print(
            "==> not exported (--stages=%s)\n"
            "    %d objects, %d meshes, %s verts, %s tris in %.1f s\n"
            "    no sha256 and no manifest: %s is left describing the last\n"
            "    exported mesh rather than this one"
            % (
                arguments.get("stages", "?"),
                totals["objects"],
                totals["meshes"],
                "{:,}".format(totals["vertices"]),
                "{:,}".format(totals["triangles"]),
                elapsed,
                os.path.relpath(manifest_path, _project_root()),
            )
        )
        return

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
