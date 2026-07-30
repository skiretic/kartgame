"""Issue #17 — the seated driver. Eighteen parts, and he is a datum.

**`docs/KART_SPEC.md` §60.1 is the design and §60.1.6 is a committed contract.**
The part names below are fixed there and may not be renamed; the endpoints are
§60.1.4's hard points, which live in `params.py`'s driver block and are **not**
re-derived here. Where a comment in this file disagrees with §60.1, the comment is
what is wrong.

The driver exists because the kart's two gates have no opinion about the volume a
person occupies: a radiator hose crossed the lumbar spine 79.4 mm deep with both
gates green, and Art. 9.5.3 (*"must not [...] cover any part of the feet"*) and
Art. 9.5.4 (*"No part of the side bodywork may cover any part of the driver"*) are
unverifiable against nobody. So he is real, exported, materialed geometry rather
than a hidden collision proxy.

What this module owns and what it does not
------------------------------------------

*Not owned, and read rather than chosen:* every joint position, the torso
recline, the helmet's three outer dimensions, the four breadths, and the two
equipment thicknesses. All of `params.py`'s `driver_*` block.

*Owned:* the **cross-sections**. §60.1.6 deliberately does not specify them,
because they are flesh and §60.1.4 has no flesh dimension in it beyond four
breadths — and a number invented inside a contract is an estimate wearing the
authority of a constraint, which is front matter §1's second defect. So every
cross-section here carries its provenance in the block below, `derived` where the
four breadths reach and `estimated` with its reasoning where they do not.

Three chains are closed rather than merely consistent, which is the difference
between a contact that is measured lucky and one that is arithmetically true:

1.  **The pelvis bottom is the seat pan.** `2 * (driver_hip_z - pan_z)` is the
    pelvis height, so the block's underside lands *on* `seat_z + seat_thickness`
    whatever the seat does.
2.  **The torso's back is the seat's own rake.** The rear face is the plane
    through 100 mm behind the H-point at `p.seat_shell_rake`, which is the
    sourced Tillett chord. It reproduces §60.1.1's published back surface to
    1 mm and §60.1.3's "hip 100 mm forward of the back contact" exactly, and it
    gives the acromion 70 mm of forward stand-off, which is where
    `TORSO_DEPTH_ACROMION` comes from.
3.  **The grips are the built rim**, off `P.wheel_center` and `P.wheel_rake`, not
    off §60.2.1's tabulated numbers. §60.2 was written against `wheel_angle`
    0.470 and an authored `wheel_center_y` 0.320; both are **deleted** from
    `params.py`, and the reach arithmetic that follows from the live chain has a
    different answer. See §60.2.5, which this module's measurements added.

What is honest rather than pretty
---------------------------------

The arms are built at whatever elbow angle actually closes and the legs are built
where `params.py` puts them. `driver_ball_z` is 0.090 and the live foot bar is at
`P.pedal_bar_z` = 0.228 — 138 mm apart — because that field's docstring sources it
from a `pedal_y`/`pedal_z` pair that no longer exists at those values. This module
does not quietly move a sourced endpoint to make a render look better: it builds
the driver the parameter block describes and prints the divergence as a number on
every run. Same rule as the locked-straight-arm case §60.1.6 argues for.

Coordinates: +X kart right, +Y forward, +Z up; origin on the ground at
mid-wheelbase. Built on the right and mirrored, because authoring both halves is
two places for a number to be wrong.
"""

from __future__ import annotations

import math
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

from . import build
from . import params as P


# --- the switch ------------------------------------------------------------


def _enabled(argv: list[str] | None = None) -> bool:
    """Whether to build the driver at all. `--driver=false` builds the bare kart.

    Every §6.4 driving figure, every `drive.sh` scenario and every published still
    predates the driver, and each has to stay reproducible from the command that
    made it — the rule `shots/` is held to. A turntable of the kart *with* a driver
    is a new still, not a redefinition of an old one.

    **The spelling is `--driver=false`, not §60.1.6's `--set=driver=false`, and
    that is a correction rather than a preference.** `--set` is typed against
    `KartParams`' fields by `genkart.parameters_from`, which raises
    `SystemExit("no such parameter 'driver'")` before any module runs — so the
    documented spelling cannot work without a `params.py` field, and `params.py`
    is single-owner. Read off `sys.argv` instead, exactly as
    `build.selected_livery` does and for the same reason: `genkart.sh` forwards
    every unrecognized argument verbatim and `genkart.py`'s parser collects
    unknown keys without complaining, so the flag arrives intact.
    """
    source = list(sys.argv if argv is None else argv)
    for argument in source:
        if argument == "--driver":
            return True
        if argument.startswith("--driver="):
            value = argument.split("=", 1)[1].strip().lower()
            if value in ("false", "0", "no", "off"):
                return False
            if value in ("true", "1", "yes", "on"):
                return True
            raise SystemExit(
                "driver.py: --driver wants true or false, got %r" % value
            )
    return True


# --- materials -------------------------------------------------------------

#: §60.1.6 fixes six material names and this module owns their values. Five of the
#: six do not exist in `build.FIXED_FINISHES` yet and **`build.py` is not this
#: module's file**, so each falls back to the nearest existing finish until the
#: four-tuples land. The fallback is announced on every build rather than being
#: silent, because a stand-in nobody mentions is how `engine_alloy` came to hold
#: 116 parts.
#:
#: **Nothing here may take a livery role.** A suit is not bodywork: `bodywork_wrap`,
#: `bodywork_contrast`, `livery_accent`, `frame_powdercoat` and `rim_magnesium` are
#: all driven by `--livery` and none of them appears below.
#: The six four-tuples this module recommends, measured off
#: `exh_commons_buntschu_kz2.jpg` (a KZ2 driver mid-corner, the only photograph in
#: this repo of a driver in a kart) with a 16-level modal sample per region, and
#: reported to whoever owns `build.py`:
#:
#:     overalls_fabric   ("#1c2c4c", 0.060, 0.65, 0.0)
#:     protector_shell   ("#232326", 0.032, 0.50, 0.0)
#:     helmet_shell      ("#f0ece6", 0.640, 0.14, 0.0)   unchanged, confirmed
#:     visor_tint        ("#123a5c", 0.055, 0.08, 0.0)
#:     glove_leather     ("#242428", 0.030, 0.55, 0.0)
#:     boot_leather      ("#26262c", 0.034, 0.38, 0.0)
#:
#: See spec §60.1.7 for the measured samples each hex came from and for why three of
#: the luminances sit deliberately above their measured ratio.
MATERIAL_FALLBACK: tuple[tuple[str, str], ...] = (
    # A navy fabric overall. `suit_fabric` was added to `build.py` two milestones
    # ago for a driver that was never built, and it is the same surface: its
    # `#16305c` is within a hue step of the `#182848` measured off buntschu, which
    # is corroboration rather than coincidence.
    ("overalls_fabric", "suit_fabric"),
    # Art. 7.5's rigid shell to FIA 8870-2018. Moulded, not painted.
    ("protector_shell", "plastic_matte_black"),
    # Already in `build.FIXED_FINISHES`, so this row is identity and stays only so
    # the table is the whole §60.1.6 list rather than the interesting half of it.
    ("visor_tint", "carbon_twill"),
    ("helmet_shell", "helmet_shell"),
    ("glove_leather", "rubber_gloss"),
    ("boot_leather", "rubber_matte"),
)


def _materials(context: build.BuildContext) -> dict[str, bpy.types.Material]:
    """Resolve §60.1.6's six names, falling back and saying so."""
    resolved: dict[str, bpy.types.Material] = {}
    pending: list[str] = []
    for wanted, fallback in MATERIAL_FALLBACK:
        if wanted in context.materials:
            resolved[wanted] = context.material(wanted)
            continue
        resolved[wanted] = context.material(fallback)
        pending.append("%s->%s" % (wanted, fallback))
    if pending:
        print(
            "    driver   %d of spec 60.1.6's materials are not in build.py yet, "
            "standing in: %s" % (len(pending), ", ".join(pending))
        )
    return resolved


# --- cross-sections, which are this module's to choose ---------------------
#
# Every number below is one of the front matter's three tags and says which. They
# are reported to the main thread for `params.py`'s driver block rather than
# written into it here, because that file is single-owner.

#: Hip joint forward of the seat-back contact, and therefore where the back plane
#: sits. §60.1.3, `estimated` there — "roughly half the pelvis depth".
#:
#: **This is a restatement of a number that lives in a document, and there is no
#: `params.py` field for it**, which is the one thing this file would rather not be
#: doing. It is not derivable back out of the driver block either: §60.1.3 used it
#: *to build* `driver_hip_y`, so recovering it needs the same seat plane it defines,
#: and that is circular. Reported for the driver block with the rest.
HIP_FORWARD_OF_BACK: float = 0.100

#: Pelvis fore-aft depth. `derived`: 2 x `HIP_FORWARD_OF_BACK`, per §60.1.3's own
#: description of that offset as half the pelvis depth.
PELVIS_DEPTH: float = 2.0 * HIP_FORWARD_OF_BACK

#: Superellipse exponent of the pelvis ring. `estimated`: a seated pelvis in a
#: fiberglass shell is squarer than an ellipse and rounder than a box.
PELVIS_EXPONENT: float = 3.0

#: Torso depth at the chest, its deepest station. `estimated`: the `derived` pelvis
#: depth of 200 plus 7.5%. A rib protector is counted separately as its own part,
#: so this is flesh and overalls only, and a seated chest is barely deeper than the
#: pelvis it sits on once the protector is not double-counted.
TORSO_DEPTH_CHEST: float = 0.215

#: Torso depth at the acromion. `estimated`, arithmetic shown: the back plane at
#: `driver_shoulder_z` sits 70.2 mm behind `driver_shoulder_y`, and a shoulder joint
#: is about one third of the chest depth forward of the back, so 3 x 70.2 = 211.
#: The one-third is the estimate; the 70.2 is `derived` from two sourced positions.
TORSO_DEPTH_ACROMION: float = 0.210

#: Width and depth of the shoulder cap, above the acromion. `estimated`: the deltoid
#: is widest *at* the acromion and rolls over above it, so the cap comes in 25 mm
#: per the bideltoid and 20 mm in depth over 24 mm of rise.
TORSO_CAP_WIDTH: float = 0.430
TORSO_CAP_DEPTH: float = 0.190
TORSO_CAP_RISE: float = 0.024

#: Superellipse exponents of the torso, hip station and above. `estimated`: a torso
#: is close to elliptical and gets rounder as it rises out of the shell.
TORSO_EXPONENT_HIP: float = 2.6
TORSO_EXPONENT_CHEST: float = 2.3

#: Art. 7.5's body protection, as along-torso stations from the H-point.
#: §60.1.5 says "roughly z 250-450 in the torso frame"; read as distance along the
#: 25 deg torso axis, which puts it over world z 357-538 — the ribs, with the
#: acromion at 527 along. The alternative reading, world z, would put a rib
#: protector across the lumbar spine.
RIB_PROTECTOR_LOW: float = 0.250
RIB_PROTECTOR_HIGH: float = 0.450

#: How far proud of the torso surface the protector's outer face sits. `estimated`:
#: 1 mm, purely so the two parts read as two parts. The shell's thickness goes
#: *inward* from there, because `driver_hip_breadth` 325 and
#: `driver_seated_shoulder_breadth` 360 are the breadths of a driver *wearing* it —
#: that is what `driver_protector_thickness`'s docstring means by "they are what
#: makes the seat fit" — so putting 15 mm outside the torso would count it twice.
RIB_PROTECTOR_PROUD: float = 0.001

#: Bare neck diameter. `estimated`: half the helmet's `driver_helmet_width` 250 is
#: 125, and a neck is a little narrower than that. The built diameter adds
#: `driver_overalls_thickness` per side -- and only that, because Art. 7's preamble
#: bans "a scarf, muff, or any loose clothes around the neck", so there is nothing
#: else there. 106 + 2 x 7 = 120.
NECK_DIAMETER_BARE: float = 0.106

#: The neck's two endpoints. **§60.1.6 publishes these and no `params.py` field
#: carries them**, so they are the only two literal positions in this module. They
#: are 3.9 mm and 4.1 mm rearward of the 25 deg torso axis at their own heights,
#: measured — small, consistent, and reported rather than corrected, because the
#: contract's table is the contract.
NECK_BASE: tuple[float, float, float] = (0.0, -0.400, 0.615)
NECK_TOP: tuple[float, float, float] = (0.0, -0.437, 0.694)

#: Visor aperture, §60.1.5: "~95 mm tall x 200 wide", `estimated` there. The width
#: is checked against `driver_eye_x` below rather than trusted — an aperture
#: narrower than the interocular distance is a driver who cannot see out.
VISOR_APERTURE_WIDTH: float = 0.200
VISOR_APERTURE_HEIGHT: float = 0.095

#: How far the visor stands off the shell, and how thick it is. `estimated`: a
#: polycarbonate visor is 2-3 mm and sits in a rebate; 4 mm at a 1 mm standoff is
#: the same read at this scale and needs no rebate. **The helmet's eye port is not
#: cut open** — §60.1.6 specifies an ellipsoid, a closed ellipsoid is watertight and
#: therefore checkable by the winding gate, and an aperture cut at two densities
#: would not be the same shape at both, which `Detail`'s contract forbids.
VISOR_STANDOFF: float = 0.001
VISOR_THICKNESS: float = 0.004

#: Corner squareness of the visor aperture. `estimated`: 6 gives a rounded rectangle
#: with short flats top and bottom, which is what a full-face aperture looks like.
VISOR_CORNER_EXPONENT: float = 6.0

#: Upper arm, root to elbow. `estimated`, arithmetic shown: `driver_bideltoid` 455
#: less `driver_seated_shoulder_breadth` 360, halved, is the 47.5 mm the deltoid
#: stands outboard of the shell's shoulder line; doubled as a diameter is 95, plus
#: 2 x `driver_overalls_thickness` is 109. The two breadths are measured at
#: different heights, so this is a proportion and not a derivation, and it is
#: labeled accordingly.
UPPER_ARM_DIAMETER_ROOT: float = 0.109
UPPER_ARM_DIAMETER_ELBOW: float = 0.086

#: Forearm, elbow to the wrist end of the sleeve. `estimated`: a forearm is a little
#: slimmer at the elbow than the upper arm and tapers hard to the wrist.
FOREARM_DIAMETER_ELBOW: float = 0.096
FOREARM_DIAMETER_WRIST: float = 0.070

#: The closed fist, at the rim. The long axis runs **along the rim tangent**,
#: because that is the way fingers wrap a tube.
#:
#: `GLOVE_ALONG_RIM` is `derived`: the NASA table's `hand length` is 191 mm, and a
#: closed fist is about 0.55 of it — 105. The other two are `estimated`; hand
#: breadth is not in §60.1.2's table.
GLOVE_ALONG_RIM: float = 0.105
GLOVE_ACROSS: float = 0.096
GLOVE_THROUGH: float = 0.086

#: Thigh at the hip. `derived`: two thighs fill `driver_hip_breadth`, so each is
#: half of it. Computed from the field rather than written out here.
#: The knee end is `estimated` — a clothed knee measures about 128 across.
THIGH_DIAMETER_KNEE: float = 0.128

#: Shank. `estimated`: just under the knee it is a little slimmer than the knee
#: itself, and at the ankle it is the boot's shaft rather than the leg.
SHANK_DIAMETER_KNEE: float = 0.124
SHANK_DIAMETER_ANKLE: float = 0.094

#: The boot. Art. 7.4 wants shoes that "cover the feet and protect the ankles", so
#: the shaft rises above the ankle joint; 90 mm is `estimated` as one hand's width
#: of cuff. The shaft's diameter is `derived` rather than authored -- the cuff goes
#: *over* the overalls' leg, so it is `SHANK_DIAMETER_ANKLE` plus two
#: `driver_overalls_thickness`, 94 + 14 = 108.
#:
#: The foot's three stations are `estimated` against one neighbor that is written
#: down: `pedal_bar_length` 0.080 is documented in `params.py` as "one boot", and a
#: 50th percentile male foot breadth is about 100, so 96 across the ball sits
#: between the two figures rather than outside both.
BOOT_SHAFT_RISE: float = 0.090
BOOT_HALF_WIDTH: tuple[float, float, float] = (0.042, 0.048, 0.038)
BOOT_HALF_HEIGHT: tuple[float, float, float] = (0.048, 0.040, 0.030)
BOOT_TOE_EXTENSION: float = 0.045
BOOT_EXPONENT: float = 3.0

#: Where the elbow swings. **The elbow is not a hard point and is not invented as
#: one**: it is the two-link solution, and the only freedom left is the swivel about
#: the shoulder-to-grip line. At the reach this cockpit actually has, the elbow is
#: **212.4 mm** off that line, so unlike a locked arm the swivel is a real choice
#: and is reported as one rather than left implicit.
#:
#: `estimated`, and it was read off two photographs rather than reasoned: **down,
#: with a quarter of that outboard.** `exh_commons_buntschu_kz2.jpg` and
#: `exh_commons_panfilov_kz2.jpg` both show a KZ2 driver mid-corner with the elbows
#: *tucked*, hanging below the shoulder and barely outside the torso line, with the
#: forearms angling up and inboard to the rim. An earlier 45-degree outboard-and-down
#: guess put the elbow 132 mm outboard of the shoulder, out over the sidepod, and
#: neither photograph supports it. This ratio puts it 34 mm outboard and 259 mm down.
ELBOW_SWIVEL: tuple[float, float, float] = (0.25, 0.0, -1.0)


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    if not _enabled():
        return

    p = context.params
    collection = context.collection("driver")
    materials = _materials(context)

    root = build.empty("driver_root", (0.0, 0.0, 0.0), collection, size=0.10)
    context.publish("driver_root", root)

    _report_divergence(p)
    _torso_stack(context, collection, materials, root)
    _head(context, collection, materials, root)
    _arms(context, collection, materials, root)
    _legs(context, collection, materials, root)


# --- shared arithmetic -----------------------------------------------------


def _torso_axis(p: P.KartParams) -> Vector:
    """Unit vector up the torso, leaning back by `driver_torso_recline_deg`."""
    recline = math.radians(p.driver_torso_recline_deg)
    return Vector((0.0, -math.sin(recline), math.cos(recline)))


def _hip(p: P.KartParams) -> Vector:
    """The H-point on the kart's centerline. The x half-offset is the thigh root."""
    return Vector((0.0, p.driver_hip_y, p.driver_hip_z))


def _back_plane_y(p: P.KartParams, z: float) -> float:
    """The seat back's surface at a height, and the torso's own rear face.

    Authored on `p.seat_shell_rake` — the fiberglass chord's *sourced* 22 degrees
    from the Tillett T11 ML chart — through the point `HIP_FORWARD_OF_BACK` behind
    the H-point. That reproduces §60.1.1's published back surface to 1 mm and
    §60.1.3's hip derivation exactly, and it means the two `sits_on` contacts are
    arithmetic rather than luck: the same expression cannot drift from itself.

    Note what it is **not**: the built `seat_shell` mesh. This module may not read
    another module's objects, so the shared parameter is the join, which is the same
    discipline `P.lower_bore` uses to make the steering bearing's gap impossible.
    """
    at_hip = p.driver_hip_y - HIP_FORWARD_OF_BACK
    return at_hip + math.tan(p.seat_shell_rake) * (p.driver_hip_z - z)


def _frame(along: Vector, hint: Vector) -> tuple[Vector, Vector]:
    """An orthonormal `(u, v)` with `u.cross(v) == along`, `v` closest to `hint`.

    The cross-product identity matters and is the reason this is a function rather
    than three lines at each call site: `_loft` emits its rings counterclockwise in
    the `(u, v)` plane and its quads in ring order, so the faces come out wound
    *outward* only when `u x v` points along the loft. Get that backwards and every
    part is inside out, which no render shows because the materials export
    `doubleSided` — `genkart.check_face_winding` is the only thing that would say
    so, and only for the parts that happen to be watertight.
    """
    axis = along.normalized()
    v = hint - axis * hint.dot(axis)
    if v.length < 1.0e-6:
        # `hint` is parallel to the axis. Pick the world axis least aligned with
        # it, deterministically, so the seam lands in the same place every run.
        fallback = min(
            (Vector((1.0, 0.0, 0.0)), Vector((0.0, 1.0, 0.0)), Vector((0.0, 0.0, 1.0))),
            key=lambda candidate: abs(candidate.dot(axis)),
        )
        v = fallback - axis * fallback.dot(axis)
    v.normalize()
    return v.cross(axis), v


def _ring(
    bm: bmesh.types.BMesh,
    center: Vector,
    u: Vector,
    v: Vector,
    half_u: float,
    half_v: float,
    exponent: float,
    segments: int,
) -> list[bmesh.types.BMVert]:
    """One superellipse ring, counterclockwise in the `(u, v)` plane.

    `|x/a|^e + |y/b|^e = 1`. `e` = 2 is an ellipse and `e` = 3 is most of the way to
    a rounded box, which is what a pelvis in a shell and a boot sole both want. One
    parameter covers every cross-section in this module.
    """
    power = 2.0 / exponent
    verts: list[bmesh.types.BMVert] = []
    for step in range(segments):
        angle = 2.0 * math.pi * step / segments
        cosine, sine = math.cos(angle), math.sin(angle)
        x = math.copysign(abs(cosine) ** power, cosine) * half_u
        y = math.copysign(abs(sine) ** power, sine) * half_v
        verts.append(bm.verts.new(center + u * x + v * y))
    return verts


#: A loft station: `(center, half_u, half_v, exponent)`. The frame is shared by the
#: whole loft and computed once from its end-to-end direction.
Station = tuple[Vector, float, float, float]


def _loft(
    bm: bmesh.types.BMesh,
    stations: list[Station],
    u: Vector,
    v: Vector,
    segments: int,
    steps: int,
    *,
    cap_start: bool = True,
    cap_end: bool = True,
) -> tuple[
    list[bmesh.types.BMVert], list[bmesh.types.BMVert], list[bmesh.types.BMFace]
]:
    """Sweep superellipse rings through `stations`, `steps` rings per span.

    Interpolation between authored stations is **linear**, which is what makes the
    low and high builds the same shape rather than two shapes at two densities:
    every intermediate ring lies exactly on the straight line between the stations
    that bracket it, so subdividing a span more finely adds vertices and moves
    nothing. A smooth interpolant would not have that property and `Detail`'s
    contract requires it.

    Returns the first ring, the last ring and the faces created, because a caller
    building a hollow band has to close onto the rings and flip the wall.
    """
    rings: list[list[bmesh.types.BMVert]] = []
    for index in range(len(stations) - 1):
        low, high = stations[index], stations[index + 1]
        # The last station of a span is the first of the next, so it is emitted
        # once — by the next span, or by the tail below.
        for step in range(steps):
            t = step / steps
            center = low[0].lerp(high[0], t)
            rings.append(
                _ring(
                    bm,
                    center,
                    u,
                    v,
                    low[1] + (high[1] - low[1]) * t,
                    low[2] + (high[2] - low[2]) * t,
                    low[3] + (high[3] - low[3]) * t,
                    segments,
                )
            )
    rings.append(
        _ring(bm, stations[-1][0], u, v, stations[-1][1], stations[-1][2],
              stations[-1][3], segments)
    )

    faces: list[bmesh.types.BMFace] = []
    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        for step in range(segments):
            following = (step + 1) % segments
            faces.append(
                bm.faces.new(
                    (lower[step], lower[following], upper[following], upper[step])
                )
            )

    if cap_start:
        faces.append(bm.faces.new(tuple(reversed(rings[0]))))
    if cap_end:
        faces.append(bm.faces.new(tuple(rings[-1])))
    return rings[0], rings[-1], faces


def _band(
    bm: bmesh.types.BMesh,
    stations: list[Station],
    thickness: float,
    u: Vector,
    v: Vector,
    segments: int,
    steps: int,
) -> None:
    """A closed hollow band: two lofts, one inside the other, joined at both rims.

    Art. 7.5's body protection is really a front shell and a back shell strapped
    together at the sides. This builds it as one closed band instead, which is a
    simplification and is stated as one: the sides are 15 mm of strap either way,
    the arms cover them at every camera angle, and a band is **watertight**, so
    `check_face_winding` can actually check it. Two open shells could not be
    checked at all.
    """
    outer_stations = list(stations)
    inner_stations = [
        (center, half_u - thickness, half_v - thickness, exponent)
        for center, half_u, half_v, exponent in stations
    ]
    outer_low, outer_high, _outer_faces = _loft(
        bm, outer_stations, u, v, segments, steps, cap_start=False, cap_end=False
    )
    inner_low, inner_high, inner_faces = _loft(
        bm, inner_stations, u, v, segments, steps, cap_start=False, cap_end=False
    )
    # The inner wall came out of `_loft` facing *outward*, which is correct for a
    # solid and backwards for a cavity: the normal has to point into the hollow.
    # Reversed here rather than built backwards, because one call over the wall's
    # own faces cannot accidentally take the rims with it.
    bmesh.ops.reverse_faces(bm, faces=inner_faces)

    for step in range(segments):
        following = (step + 1) % segments
        # Bottom rim, facing along -loft.
        bm.faces.new(
            (
                outer_low[step],
                inner_low[step],
                inner_low[following],
                outer_low[following],
            )
        )
        # Top rim, facing along +loft.
        bm.faces.new(
            (
                outer_high[step],
                outer_high[following],
                inner_high[following],
                inner_high[step],
            )
        )


def _ellipsoid(
    bm: bmesh.types.BMesh,
    center: Vector,
    basis: Matrix,
    semi: Vector,
    segments: int,
    stacks: int,
) -> None:
    """A closed ellipsoid, `semi` along the three columns of `basis`.

    Rings run around the third basis column and poles collapse to a single vertex,
    the same way `build.lathe` handles a profile point at radius zero. `basis` must
    be a rotation — a reflection would invert every face, which is the mirrored
    sprocket failure in a different costume.
    """
    rings: list[list[bmesh.types.BMVert] | bmesh.types.BMVert] = []
    for stack in range(stacks + 1):
        elevation = -0.5 * math.pi + math.pi * stack / stacks
        if stack in (0, stacks):
            local = Vector((0.0, 0.0, semi.z * math.sin(elevation)))
            rings.append(bm.verts.new(center + basis @ local))
            continue
        ring: list[bmesh.types.BMVert] = []
        for step in range(segments):
            azimuth = 2.0 * math.pi * step / segments
            local = Vector(
                (
                    semi.x * math.cos(elevation) * math.cos(azimuth),
                    semi.y * math.cos(elevation) * math.sin(azimuth),
                    semi.z * math.sin(elevation),
                )
            )
            ring.append(bm.verts.new(center + basis @ local))
        rings.append(ring)

    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        if isinstance(lower, bmesh.types.BMVert):
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((lower, upper[following], upper[step]))
        elif isinstance(upper, bmesh.types.BMVert):
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new((lower[step], lower[following], upper))
        else:
            for step in range(segments):
                following = (step + 1) % segments
                bm.faces.new(
                    (lower[step], lower[following], upper[following], upper[step])
                )


def _part(
    context: build.BuildContext,
    name: str,
    bm: bmesh.types.BMesh,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> bpy.types.Object:
    """Realize one driver part, smooth-shaded and parented.

    **No bevel.** `build.bevel_object` at high detail uses a 4 mm offset, which is
    the full thickness of a 4 mm panel and would chamfer most of the rib
    protector's 15 mm wall away; and a body is smooth-shaded everywhere, so there
    are no hard edges for a bevel to earn its vertices on. The only sharp edges on
    the driver are the caps buried inside the neighboring segment.
    """
    obj = build.object_from_bmesh(
        name, bm, collection, material=material, shade_smooth=True
    )
    build.set_parent(obj, root)
    return obj


def _mirror(
    context: build.BuildContext,
    right: bpy.types.Object,
    name: str,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> bpy.types.Object:
    """The left-hand twin. `build.mirror_x` already carries the material slot."""
    left = build.mirror_x(right, name, collection)
    build.set_parent(left, root)
    return left


# --- the divergence this module refuses to hide ----------------------------


def _report_divergence(p: P.KartParams) -> None:
    """Print where the driver block and the live cockpit disagree, in millimeters.

    Both `presses` contacts and the whole reach are joins between two parameter
    blocks that were authored in different waves, and §60.1.6's rule is that a
    finding which cannot be adjudicated against a sourced figure gets a waiver and
    a ticket rather than a geometry change. This is the waiver's measurement, taken
    on every build so it cannot rot: the moment somebody corrects either side, the
    line goes quiet.

    `driver_ball_*`'s docstring calls itself `sourced` from `pedal_y` / `pedal_z`.
    Those fields are gone: the pedal box was re-authored as an organ pedal on a
    bottom pivot, `pedal_z` is now the *derived* foot-bar height 0.228, and
    `pedal_separation` moved 150 -> 170. So the boots are built where the driver
    block says and the pedals are 138 mm above them.
    """
    bar_y = P.pedal_bar_y(p)
    bar_z = P.pedal_bar_z(p)
    bar_x = p.pedal_separation * 0.5
    ball_gap = math.dist(
        (p.driver_ball_x, p.driver_ball_y, p.driver_ball_z), (bar_x, bar_y, bar_z)
    )
    if ball_gap > 0.002:
        print(
            "    driver   warning: driver_ball_* is %.1f mm from the live foot bar "
            "(dx %+.1f, dy %+.1f, dz %+.1f) -- both `presses` contacts fail by "
            "that much; #17 report"
            % (
                ball_gap * 1000.0,
                (p.driver_ball_x - bar_x) * 1000.0,
                (p.driver_ball_y - bar_y) * 1000.0,
                (p.driver_ball_z - bar_z) * 1000.0,
            )
        )

    # The reach, against the rim the cockpit actually builds rather than §60.2.1's
    # tabulated one. This one is *good* news and is printed for the same reason.
    shoulder = Vector((p.driver_shoulder_x, p.driver_shoulder_y, p.driver_shoulder_z))
    grip = _grip(p, +1.0)
    span = (grip - shoulder).length
    links = p.driver_upper_arm + p.driver_elbow_to_fist
    print(
        "    driver   shoulder to grip %.1f mm against %.1f mm of arm; elbow "
        "%.1f deg" % (span * 1000.0, links * 1000.0, math.degrees(_elbow_angle(p)))
    )


# --- the torso stack -------------------------------------------------------


def _torso_stack(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
) -> None:
    detail = context.detail
    # The ring count is the tube's: 12 low, 32 high. The station count along a part
    # is half the bend count -- 3 low and 7 high -- and because `_loft` interpolates
    # linearly, both densities lie on the same surface rather than on two surfaces.
    segments = detail.tube_segments
    steps = max(1, detail.bend_segments // 2)

    _pelvis(context, collection, materials, root, segments, steps)
    _torso(context, collection, materials, root, segments, steps)
    _rib_protector(context, collection, materials, root, segments, steps)
    _neck(context, collection, materials, root, segments, steps)


def _pelvis(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    steps: int,
) -> None:
    """The hip block about the H-point, sitting on the pan by construction.

    Height is `2 * (driver_hip_z - pan_z)`, so the underside is the pan surface
    whatever the seat's own base plane and shell thickness are. §60.1.3 puts the
    H-point 95 mm above a firm seat surface; that offset is *in* `driver_hip_z`
    already, so it is not applied twice here.
    """
    p = context.params
    pan_z = p.seat_z + p.seat_thickness
    half_height = p.driver_hip_z - pan_z
    hip = _hip(p)
    half_width = p.driver_hip_breadth * 0.5
    half_depth = PELVIS_DEPTH * 0.5
    rear = _back_plane_y(p, p.driver_hip_z)

    u, v = _frame(Vector((0.0, 0.0, 1.0)), Vector((0.0, 1.0, 0.0)))
    stations: list[Station] = [
        # Bottom, on the pan, tucked in a little: a pelvis is not a slab.
        (
            Vector((0.0, rear + half_depth, hip.z - half_height)),
            half_width * 0.88,
            half_depth * 0.90,
            PELVIS_EXPONENT,
        ),
        (
            Vector((0.0, rear + half_depth, hip.z)),
            half_width,
            half_depth,
            PELVIS_EXPONENT,
        ),
        (
            Vector(
                (
                    0.0,
                    _back_plane_y(p, hip.z + half_height) + half_depth,
                    hip.z + half_height,
                )
            ),
            half_width * 0.94,
            half_depth * 0.97,
            PELVIS_EXPONENT,
        ),
    ]
    bm = bmesh.new()
    _loft(bm, stations, u, v, segments, steps)
    _part(
        context, "driver_pelvis", bm, collection, materials["overalls_fabric"], root
    )


def _torso_stations(p: P.KartParams) -> list[Station]:
    """Hip to the top of the shoulders, as horizontal rings on the seat's rake.

    Three of the four widths are sourced or derived and none of them is styled:
    `driver_hip_breadth` 325 at the H-point, `driver_seated_shoulder_breadth` 360
    where the shell's back ends, `driver_bideltoid` 455 at the acromion. §60.1.1's
    "the torso tapers 325 -> 360 -> 455" is exactly this list.

    The rear face is `_back_plane_y`, i.e. the shell's own 22 degree chord, not the
    driver's 25 degree torso axis. That is deliberate: a back conforms to the seat
    it is pressed into, and a rigid 25 degree box would stand 33 mm proud of the
    shell's top edge -- measured -- because 3 degrees over 280 mm of shell is 15 mm
    on top of the 16 mm it starts with.

    **The section is an off-center ellipse about the spine and the breadths are never
    spent fore-aft.** Half-depths measured off the built mesh, rear then forward:
    100/100 at the hip, 85.2/129.8 at the shell top, 70.2/139.8 at the acromion. The
    rear number is `derived` from the sourced back plane rather than estimated,
    because that is the surface he leans on; only the forward number is a guess. A
    circular section would spend `driver_bideltoid` as a 227 mm radius, put a 455 mm
    chest depth through the seat back, and **pass gate 3 anyway** -- the
    `driver_torso`/`seat_shell` `sits_on` row *permits* interpenetration, so the
    check that would catch it is switched off by design. Same shape as the inverted
    winding. Built, the torso's rear face is 0.04 mm off `seat_shell`.
    """
    shell_top_z = p.seat_z + p.seat_height

    def station(z: float, width: float, depth: float, exponent: float) -> Station:
        rear = _back_plane_y(p, z)
        return (Vector((0.0, rear + depth * 0.5, z)), width * 0.5, depth * 0.5, exponent)

    return [
        station(p.driver_hip_z, p.driver_hip_breadth, PELVIS_DEPTH, TORSO_EXPONENT_HIP),
        station(
            shell_top_z,
            p.driver_seated_shoulder_breadth,
            TORSO_DEPTH_CHEST,
            TORSO_EXPONENT_CHEST,
        ),
        station(
            p.driver_shoulder_z,
            p.driver_bideltoid,
            TORSO_DEPTH_ACROMION,
            TORSO_EXPONENT_CHEST,
        ),
        station(
            p.driver_shoulder_z + TORSO_CAP_RISE,
            TORSO_CAP_WIDTH,
            TORSO_CAP_DEPTH,
            TORSO_EXPONENT_CHEST,
        ),
    ]


def _torso(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    steps: int,
) -> None:
    p = context.params
    # The shoulder joints are placed off `driver_shoulder_x`, and
    # `driver_shoulder_span` is the same number from the other direction --
    # biacromial breadth, bone to bone. Asserted rather than assumed, because two
    # fields for one distance is precisely how a renamed key draws a zero forever.
    span_half = p.driver_shoulder_span * 0.5
    if abs(span_half - p.driver_shoulder_x) > 1.0e-6:
        raise SystemExit(
            "driver.py: driver_shoulder_span/2 is %.1f mm and driver_shoulder_x is "
            "%.1f mm. They are the same distance measured twice (spec 60.1.4) and "
            "the "
            "arms are built on one of them, so they may not disagree."
            % (span_half * 1000.0, p.driver_shoulder_x * 1000.0)
        )

    u, v = _frame(Vector((0.0, 0.0, 1.0)), Vector((0.0, 1.0, 0.0)))
    bm = bmesh.new()
    _loft(bm, _torso_stations(p), u, v, segments, steps)
    _part(context, "driver_torso", bm, collection, materials["overalls_fabric"], root)


def _rib_protector(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    steps: int,
) -> None:
    """Art. 7.5's body protection, as a band over the torso's rib stations.

    Its outer face is the torso's surface plus `RIB_PROTECTOR_PROUD`, and the
    15 mm of `driver_protector_thickness` goes *inward* from there. Putting it
    outward would count it twice: `driver_hip_breadth` 325 and
    `driver_seated_shoulder_breadth` 360 are already the breadths of a driver
    wearing one, which is what makes him fill a 333 mm shell.
    """
    p = context.params
    axis = _torso_axis(p)
    hip = _hip(p)
    low_z = (hip + axis * RIB_PROTECTOR_LOW).z
    high_z = (hip + axis * RIB_PROTECTOR_HIGH).z

    stations = _torso_stations(p)
    banded: list[Station] = []
    for z in (low_z, 0.5 * (low_z + high_z), high_z):
        half_u, half_v, exponent = _torso_section(stations, z)
        rear = _back_plane_y(p, z)
        banded.append(
            (
                Vector((0.0, rear + half_v + RIB_PROTECTOR_PROUD, z)),
                half_u + RIB_PROTECTOR_PROUD,
                half_v + RIB_PROTECTOR_PROUD,
                exponent,
            )
        )

    u, v = _frame(Vector((0.0, 0.0, 1.0)), Vector((0.0, 1.0, 0.0)))
    bm = bmesh.new()
    _band(bm, banded, p.driver_protector_thickness, u, v, segments, steps)
    _part(
        context,
        "driver_rib_protector",
        bm,
        collection,
        materials["protector_shell"],
        root,
    )


def _torso_section(
    stations: list[Station], z: float
) -> tuple[float, float, float]:
    """The torso's `(half_u, half_v, exponent)` at a height, by linear lookup.

    Reads the same authored list the torso mesh is built from, so the protector
    cannot drift off the body it is worn over. Clamped at both ends rather than
    extrapolated.
    """
    if z <= stations[0][0].z:
        return stations[0][1], stations[0][2], stations[0][3]
    for index in range(len(stations) - 1):
        low, high = stations[index], stations[index + 1]
        if low[0].z <= z <= high[0].z:
            span = high[0].z - low[0].z
            t = 0.0 if span <= 1.0e-9 else (z - low[0].z) / span
            return (
                low[1] + (high[1] - low[1]) * t,
                low[2] + (high[2] - low[2]) * t,
                low[3] + (high[3] - low[3]) * t,
            )
    return stations[-1][1], stations[-1][2], stations[-1][3]


def _neck(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    steps: int,
) -> None:
    p = context.params
    base = Vector(NECK_BASE)
    top = Vector(NECK_TOP)
    # Bare neck plus the collar, and the collar is exactly the overalls: Art. 7's
    # preamble bans anything else being there.
    half = (NECK_DIAMETER_BARE + 2.0 * p.driver_overalls_thickness) * 0.5
    u, v = _frame(top - base, Vector((0.0, 1.0, 0.0)))
    stations: list[Station] = [
        # Flared at the base where the neck runs into the trapezius.
        (base, half * 1.15, half * 1.15, 2.0),
        (top, half, half, 2.0),
    ]
    bm = bmesh.new()
    _loft(bm, stations, u, v, segments, steps)
    _part(context, "driver_neck", bm, collection, materials["overalls_fabric"], root)


# --- head ------------------------------------------------------------------


def _head_basis(p: P.KartParams) -> Matrix:
    """Helmet axes: X lateral, Y the way the face points, Z up. **Not raked.**

    §60.1.6 spells the helmet "250 wide x 340 long x 300 tall" and `params.py` names
    the three fields `_width` / `_length` / `_height`. Those are world words: a
    helmet raked back 25 degrees is not 300 mm tall and is not 340 mm long, so the
    contract's own dimensions only mean what they say on an axis-aligned ellipsoid.
    Built axis-aligned for that reason, and it is the reading that puts the visor at
    the eye's own height and the face pointing where the kart is going.

    **It costs two documented inconsistencies rather than hiding them**, and both are
    §60.1.4's rather than this module's:

    * §60.1.4's helmet crown `(0, -511, 860)` is derived 135 mm from the centre
      *along the raked head axis*. An axis-aligned 300 mm ellipsoid centred at
      z 738 crowns at **888**, 28 mm higher, and its crown is at y -454 rather than
      -511. The two figures are only compatible if the shell is raked, and then the
      three outer dimensions stop being width, length and height.
    * §60.1.4 derives the eye by walking sitting eye height **along the torso
      axis**, which puts the eye point exactly on the helmet's own fore-aft
      mid-plane — so the driver looks out of the middle of his head and the visor
      lands 176 mm in front of his eyes. Real eyes sit near the *front* of the
      skull, roughly 100 mm forward of the head's axis. Fixing that moves the eye
      point, which the cockpit camera and the audio listener both read, so it is a
      §60.1.4 decision. Reported, not patched.

    `p` is taken and unused so the signature does not change if the rake question
    is ever settled the other way.
    """
    del p
    return Matrix.Identity(3)


def _helmet_center(p: P.KartParams) -> Vector:
    return Vector((0.0, p.driver_helmet_y, p.driver_helmet_z))


def _helmet_semi(p: P.KartParams) -> Vector:
    """Semi-axes in helmet-local order: lateral, fore-aft, vertical.

    `driver_helmet_length` 340 is the whole point of the field set — the deleted
    `driver_helmet_radius` 125 was right laterally and 90 mm short fore-aft, and a
    sphere is the one shape a full-face helmet is not.
    """
    return Vector(
        (
            p.driver_helmet_width * 0.5,
            p.driver_helmet_length * 0.5,
            p.driver_helmet_height * 0.5,
        )
    )


def _head(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
) -> None:
    p = context.params
    detail = context.detail
    segments = detail.tube_segments
    stacks = max(4, detail.tube_segments // 2)

    center = _helmet_center(p)
    basis = _head_basis(p)
    semi = _helmet_semi(p)

    bm = bmesh.new()
    _ellipsoid(bm, center, basis, semi, segments, stacks)
    _part(context, "driver_helmet", bm, collection, materials["helmet_shell"], root)

    _visor(context, collection, materials, root, segments, stacks)

    head = build.empty(
        "driver_head",
        (center.x, center.y, center.z),
        collection,
        parent=root,
        size=0.06,
    )
    context.publish("driver_head", head)


def _visor(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    stacks: int,
) -> None:
    """A lens over the eye port, closed so the winding gate can see it.

    Built as two sheets of the helmet's own ellipsoid — one at `VISOR_STANDOFF`
    and one `VISOR_THICKNESS` further out along the surface normal — joined by four
    rim strips. Every one of those five windings is derived in the comments below
    rather than tried; a rim wound inward is invisible in a render and would leave
    the part enclosing a negative volume.
    """
    p = context.params
    center = _helmet_center(p)
    basis = _head_basis(p)
    semi = _helmet_semi(p)

    # The eye point in helmet-local coordinates. Only its **height** places the
    # aperture: the visor sits on the shell's *front surface* at the eye's own
    # height, and where the eye sits fore-aft along the shell is §60.1.4's business,
    # measured and reported rather than compensated for. A helmet's front face is
    # about 100 mm ahead of a real eye; this one prints 178, because §60.1.4 walks
    # sitting eye height along the torso axis and so lands the eye on the shell's own
    # fore-aft mid-plane. `driver_eye_x` is the interocular half-distance, so the
    # aperture is centred on x = 0 and merely has to be wide enough to clear both.
    eye_local = Vector(
        (p.driver_eye_x, p.driver_eye_y - center.y, p.driver_eye_z - center.z)
    )
    local_eye_z = eye_local.z
    occupancy = (
        (eye_local.x / semi.x) ** 2
        + (eye_local.y / semi.y) ** 2
        + (eye_local.z / semi.z) ** 2
    )
    if occupancy >= 1.0:
        raise SystemExit(
            "driver.py: the eye point (%.0f, %.0f, %.0f) is outside the helmet "
            "shell, so there is nothing for a visor to sit in front of. Check "
            "driver_eye_* against driver_helmet_*."
            % (
                p.driver_eye_x * 1000.0,
                p.driver_eye_y * 1000.0,
                p.driver_eye_z * 1000.0,
            )
        )
    print(
        "    driver   eye is %.0f mm behind the shell's front face and %.0f mm "
        "above its centre; visor aperture %.0f x %.0f mm"
        % (
            (semi.y - eye_local.y) * 1000.0,
            local_eye_z * 1000.0,
            VISOR_APERTURE_WIDTH * 1000.0,
            VISOR_APERTURE_HEIGHT * 1000.0,
        )
    )

    half_width = max(VISOR_APERTURE_WIDTH * 0.5, p.driver_eye_x + 0.030)
    if half_width > VISOR_APERTURE_WIDTH * 0.5:
        print(
            "    driver   note: visor aperture widened to %.0f mm so both eyes at "
            "driver_eye_x %.0f mm see out of it"
            % (half_width * 2000.0, p.driver_eye_x * 1000.0)
        )
    half_height = VISOR_APERTURE_HEIGHT * 0.5

    rows = max(4, stacks)
    columns = max(6, segments // 2)

    def surface(local_x: float, local_z: float, offset: float) -> Vector:
        """A point offset along the ellipsoid's outward normal, in world space."""
        # Solve the +Y intersection of the ellipsoid at this (x, z).
        residual = 1.0 - (local_x / semi.x) ** 2 - (local_z / semi.z) ** 2
        local_y = semi.y * math.sqrt(max(0.0, residual))
        point = Vector((local_x, local_y, local_z))
        normal = Vector(
            (
                point.x / (semi.x * semi.x),
                point.y / (semi.y * semi.y),
                point.z / (semi.z * semi.z),
            )
        )
        if normal.length < 1.0e-9:
            normal = Vector((0.0, 1.0, 0.0))
        return center + basis @ (point + normal.normalized() * offset)

    inner: list[list[bmesh.types.BMVert]] = []
    outer: list[list[bmesh.types.BMVert]] = []
    bm = bmesh.new()
    for row in range(rows + 1):
        t = row / rows
        # A superellipse taper across the rows, so the aperture is a rounded
        # rectangle rather than a rectangle with four right angles on a curved
        # shell. Inset from the extremes so the last row still has width: a row of
        # zero width would be a degenerate ring, and collapsing it to a pole would
        # make the low and high builds two different shapes.
        edge = abs(2.0 * (0.03 + 0.94 * t) - 1.0)
        taper = max(0.12, (1.0 - edge ** VISOR_CORNER_EXPONENT) ** (1.0 / VISOR_CORNER_EXPONENT))
        row_half_width = half_width * taper
        local_z = local_eye_z + half_height * (2.0 * t - 1.0)
        inner_row: list[bmesh.types.BMVert] = []
        outer_row: list[bmesh.types.BMVert] = []
        for column in range(columns + 1):
            s = column / columns
            local_x = row_half_width * (2.0 * s - 1.0)
            inner_row.append(
                bm.verts.new(surface(local_x, local_z, VISOR_STANDOFF))
            )
            outer_row.append(
                bm.verts.new(
                    surface(local_x, local_z, VISOR_STANDOFF + VISOR_THICKNESS)
                )
            )
        inner.append(inner_row)
        outer.append(outer_row)

    # Outer sheet, facing +local Y. Rows run +Z and columns run +X, so the quad
    # order (row, row+1, row+1, row) walks Z before X and gives Z x X = +Y.
    for row in range(rows):
        for column in range(columns):
            bm.faces.new(
                (
                    outer[row][column],
                    outer[row + 1][column],
                    outer[row + 1][column + 1],
                    outer[row][column + 1],
                )
            )
    # Inner sheet, the same quads reversed so they face -local Y.
    for row in range(rows):
        for column in range(columns):
            bm.faces.new(
                (
                    inner[row][column],
                    inner[row][column + 1],
                    inner[row + 1][column + 1],
                    inner[row + 1][column],
                )
            )
    # Bottom rim: (b - a) is +X, (c - a) is +X - Y, so X x (X - Y) = -Z.
    for column in range(columns):
        bm.faces.new(
            (
                outer[0][column],
                outer[0][column + 1],
                inner[0][column + 1],
                inner[0][column],
            )
        )
    # Top rim: X x (X + Y) = +Z.
    for column in range(columns):
        bm.faces.new(
            (
                inner[rows][column],
                inner[rows][column + 1],
                outer[rows][column + 1],
                outer[rows][column],
            )
        )
    # Left rim: (-Y) x (-Y + Z) = -X.
    for row in range(rows):
        bm.faces.new(
            (outer[row][0], inner[row][0], inner[row + 1][0], outer[row + 1][0])
        )
    # Right rim: Z x (Z - Y) = +X.
    for row in range(rows):
        bm.faces.new(
            (
                outer[row][columns],
                outer[row + 1][columns],
                inner[row + 1][columns],
                inner[row][columns],
            )
        )

    _part(
        context,
        "driver_helmet_visor",
        bm,
        collection,
        materials["visor_tint"],
        root,
    )


# --- arms ------------------------------------------------------------------


def _grip(p: P.KartParams, side: float) -> Vector:
    """The hand's grip point on the built rim, at 3 or 9 o'clock, straight ahead.

    **Derived from the live steering chain, not from §60.2.1's table.** That
    subsection tabulates the rim off `wheel_angle` 0.470 and an authored
    `wheel_center_y` 0.320; both fields are deleted -- `column_rake` is 0.628
    (36 deg, measured on the column tube) and `P.wheel_center` derives the centre
    from the welded lower bore. The rim moved 133 mm rearward and 16 mm up, and the
    reach arithmetic that follows is a different answer, which is the point of
    reading it here rather than copying a number out of the document.

    Geometry per §60.2.1: the disc lies in the plane normal to the column axis,
    spanned by `e1 = (1, 0, 0)` and `u = (0, cos rake, sin rake)`, and 3 o'clock is
    the pure `e1` point -- so the grip is the wheel centre plus one radius laterally
    whatever the rake is.
    """
    center = Vector(P.wheel_center(p))
    return center + Vector((side * p.wheel_diameter * 0.5, 0.0, 0.0))


def _elbow_angle(p: P.KartParams) -> float:
    """Interior elbow angle that closes the two-link reach, radians.

    Clamped at pi, and the clamp is the honest output §60.1.6 asks for: when the
    grip is further away than `driver_upper_arm + driver_elbow_to_fist`, the arm is
    built locked straight and short of the rim rather than lengthened to suit. A
    sourced segment does not grow to make a render work.
    """
    shoulder = Vector((p.driver_shoulder_x, p.driver_shoulder_y, p.driver_shoulder_z))
    span = (_grip(p, +1.0) - shoulder).length
    upper = p.driver_upper_arm
    fore = p.driver_elbow_to_fist
    cosine = (upper * upper + fore * fore - span * span) / (2.0 * upper * fore)
    return math.acos(max(-1.0, min(1.0, cosine)))


def _elbow(p: P.KartParams, side: float) -> Vector:
    """Where the two-link solve puts the elbow, on the `ELBOW_SWIVEL` side.

    Standard two-link intersection: `a` along the shoulder-to-grip line and `h`
    perpendicular to it. When the reach does not close, `h` is zero and the elbow
    lands on the line -- a locked arm, pointed at a rim it cannot touch.
    """
    shoulder = Vector((side * p.driver_shoulder_x, p.driver_shoulder_y, p.driver_shoulder_z))
    grip = _grip(p, side)
    to_grip = grip - shoulder
    span = to_grip.length
    upper = p.driver_upper_arm
    fore = p.driver_elbow_to_fist
    if span < 1.0e-9:
        return shoulder
    along = to_grip / span
    reach = min(span, upper + fore)
    a = (upper * upper + reach * reach - fore * fore) / (2.0 * reach)
    h = math.sqrt(max(0.0, upper * upper - a * a))

    swivel = Vector((side * ELBOW_SWIVEL[0], ELBOW_SWIVEL[1], ELBOW_SWIVEL[2]))
    perpendicular = swivel - along * swivel.dot(along)
    if perpendicular.length < 1.0e-9:
        perpendicular = _frame(along, Vector((0.0, 0.0, 1.0)))[1]
    return shoulder + along * a + perpendicular.normalized() * h


def _arms(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
) -> None:
    p = context.params
    detail = context.detail
    segments = detail.tube_segments
    steps = max(1, detail.bend_segments // 2)
    stacks = max(4, detail.tube_segments // 2)

    shoulder = Vector((p.driver_shoulder_x, p.driver_shoulder_y, p.driver_shoulder_z))
    elbow = _elbow(p, +1.0)
    grip = _grip(p, +1.0)

    # Upper arm.
    u, v = _frame(elbow - shoulder, Vector((0.0, 0.0, 1.0)))
    bm = bmesh.new()
    _loft(
        bm,
        [
            (shoulder, UPPER_ARM_DIAMETER_ROOT * 0.5, UPPER_ARM_DIAMETER_ROOT * 0.5, 2.0),
            (elbow, UPPER_ARM_DIAMETER_ELBOW * 0.5, UPPER_ARM_DIAMETER_ELBOW * 0.5, 2.0),
        ],
        u,
        v,
        segments,
        steps,
    )
    right = _part(
        context, "driver_upper_arm_r", bm, collection, materials["overalls_fabric"], root
    )
    _mirror(context, right, "driver_upper_arm_l", collection, root)

    # Forearm, elbow to the fist centre. `driver_elbow_to_fist` runs to the middle
    # of the closed hand, which is where a rim sits, so the sleeve ends inside the
    # glove rather than at a wrist 90 mm short of it.
    u, v = _frame(grip - elbow, Vector((0.0, 0.0, 1.0)))
    bm = bmesh.new()
    _loft(
        bm,
        [
            (elbow, FOREARM_DIAMETER_ELBOW * 0.5, FOREARM_DIAMETER_ELBOW * 0.5, 2.0),
            (grip, FOREARM_DIAMETER_WRIST * 0.5, FOREARM_DIAMETER_WRIST * 0.5, 2.0),
        ],
        u,
        v,
        segments,
        steps,
    )
    right = _part(
        context, "driver_forearm_r", bm, collection, materials["overalls_fabric"], root
    )
    _mirror(context, right, "driver_forearm_l", collection, root)

    _glove(context, collection, materials, root, segments, stacks)

    for side, label in ((+1.0, "r"), (-1.0, "l")):
        for name, position in (
            ("shoulder", Vector((side * p.driver_shoulder_x, p.driver_shoulder_y, p.driver_shoulder_z))),
            ("elbow", _elbow(p, side)),
            ("grip", _grip(p, side)),
        ):
            pivot = build.empty(
                "driver_%s_%s" % (name, label),
                (position.x, position.y, position.z),
                collection,
                parent=root,
                size=0.04,
            )
            context.publish("driver_%s_%s" % (name, label), pivot)


def _glove(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    stacks: int,
) -> None:
    """The fist closed on the rim, long axis along the rim's own tangent.

    The rim tangent at 3 o'clock is the wheel plane's in-plane "up",
    `(0, cos rake, sin rake)` -- differentiate §60.2.1's `rim(phi)` and evaluate at
    90 degrees. So a fist is oriented by the wheel, which is why the glove reads
    correctly when the column rake changes and would not if it were axis-aligned.

    The grip point is the rim's *centerline*, so the glove encloses the tube. That
    is what a hand does, and `joints.DRIVER_CONTACTS` permits it: a `grips` row
    permits the overlap and requires contact, and a fist wrapped round a rim is
    zero millimeters from it.
    """
    p = context.params
    rake = P.wheel_rake(p)
    tangent = Vector((0.0, math.cos(rake), math.sin(rake))).normalized()
    grip = _grip(p, +1.0)
    elbow = _elbow(p, +1.0)

    # Local Z along the rim, local X along the forearm (projected off the rim), and
    # local Y completing a right-handed basis so the ellipsoid stays outward-wound.
    along_arm = grip - elbow
    across = along_arm - tangent * along_arm.dot(tangent)
    if across.length < 1.0e-9:
        across = Vector((1.0, 0.0, 0.0))
    across.normalize()
    third = tangent.cross(across)
    basis = Matrix((
        (across.x, third.x, tangent.x),
        (across.y, third.y, tangent.y),
        (across.z, third.z, tangent.z),
    ))

    bm = bmesh.new()
    _ellipsoid(
        bm,
        grip,
        basis,
        Vector((GLOVE_ACROSS * 0.5, GLOVE_THROUGH * 0.5, GLOVE_ALONG_RIM * 0.5)),
        segments,
        stacks,
    )
    right = _part(
        context, "driver_glove_r", bm, collection, materials["glove_leather"], root
    )
    _mirror(context, right, "driver_glove_l", collection, root)


# --- legs ------------------------------------------------------------------


def _legs(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
) -> None:
    p = context.params
    detail = context.detail
    segments = detail.tube_segments
    steps = max(1, detail.bend_segments // 2)

    hip = Vector((p.driver_hip_x, p.driver_hip_y, p.driver_hip_z))
    knee = Vector((p.driver_knee_x, p.driver_knee_y, p.driver_knee_z))
    ankle = Vector((p.driver_ankle_x, p.driver_ankle_y, p.driver_ankle_z))
    heel = Vector((p.driver_ankle_x, p.driver_heel_y, p.driver_heel_z))
    ball = Vector((p.driver_ball_x, p.driver_ball_y, p.driver_ball_z))

    # Thigh. The hip end is `derived`: two thighs fill `driver_hip_breadth`.
    thigh_root = p.driver_hip_breadth * 0.5
    u, v = _frame(knee - hip, Vector((0.0, 0.0, 1.0)))
    bm = bmesh.new()
    _loft(
        bm,
        [
            (hip, thigh_root * 0.5, thigh_root * 0.5, 2.2),
            (knee, THIGH_DIAMETER_KNEE * 0.5, THIGH_DIAMETER_KNEE * 0.5, 2.2),
        ],
        u,
        v,
        segments,
        steps,
    )
    right = _part(
        context, "driver_thigh_r", bm, collection, materials["overalls_fabric"], root
    )
    _mirror(context, right, "driver_thigh_l", collection, root)

    # Shank.
    u, v = _frame(ankle - knee, Vector((0.0, 0.0, 1.0)))
    bm = bmesh.new()
    _loft(
        bm,
        [
            (knee, SHANK_DIAMETER_KNEE * 0.5, SHANK_DIAMETER_KNEE * 0.5, 2.0),
            (ankle, SHANK_DIAMETER_ANKLE * 0.5, SHANK_DIAMETER_ANKLE * 0.5, 2.0),
        ],
        u,
        v,
        segments,
        steps,
    )
    right = _part(
        context, "driver_shank_r", bm, collection, materials["overalls_fabric"], root
    )
    _mirror(context, right, "driver_shank_l", collection, root)

    _boot(context, collection, materials, root, segments, steps, knee, ankle, heel, ball)

    for side, label in ((+1.0, "r"), (-1.0, "l")):
        for name, position in (
            ("hip", Vector((side * p.driver_hip_x, p.driver_hip_y, p.driver_hip_z))),
            ("knee", Vector((side * p.driver_knee_x, p.driver_knee_y, p.driver_knee_z))),
            ("ankle", Vector((side * p.driver_ankle_x, p.driver_ankle_y, p.driver_ankle_z))),
        ):
            pivot = build.empty(
                "driver_%s_%s" % (name, label),
                (position.x, position.y, position.z),
                collection,
                parent=root,
                size=0.04,
            )
            context.publish("driver_%s_%s" % (name, label), pivot)


def _boot(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    steps: int,
    knee: Vector,
    ankle: Vector,
    heel: Vector,
    ball: Vector,
) -> None:
    """One boot: a foot from the heel to the toe, plus an above-ankle shaft.

    Two closed lofts in one mesh rather than one loft round a 90 degree turn. A
    single sweep through that corner pinches its rings to nothing on the inside of
    the bend; two overlapping solids do not, they are both watertight so the
    winding gate checks both, and the overlap between them is intra-driver and
    skipped by §60.1.6's contract.

    **The sole passes through the heel and ball hard points**, not near them: each
    ring's centre is lifted off its contact point by that station's own half
    height, so the bottom surface is the two contacts and the `presses` contact is
    the boot's sole rather than its axis.
    """
    p = context.params
    foot_axis = ball - heel
    if foot_axis.length < 1.0e-9:
        raise SystemExit("driver.py: the heel and the ball of the foot coincide")
    toe = ball + foot_axis.normalized() * BOOT_TOE_EXTENSION

    u, v = _frame(foot_axis, Vector((0.0, 0.0, 1.0)))
    contacts = (heel, ball, toe)
    stations: list[Station] = []
    for index, contact in enumerate(contacts):
        half_v = BOOT_HALF_HEIGHT[index]
        stations.append(
            (
                contact + v * half_v,
                BOOT_HALF_WIDTH[index],
                half_v,
                BOOT_EXPONENT,
            )
        )

    bm = bmesh.new()
    _loft(bm, stations, u, v, segments, steps)

    # The shaft, up the shank's own axis so the cuff is square to the leg.
    shank_axis = (ankle - knee).normalized()
    shaft_top = ankle - shank_axis * BOOT_SHAFT_RISE
    shaft_u, shaft_v = _frame(shaft_top - ankle, Vector((0.0, 1.0, 0.0)))
    # `derived`: the cuff goes over the overalls' leg, so it is the shank plus two
    # thicknesses of fabric. 94 + 2 x 7 = 108.
    half = (SHANK_DIAMETER_ANKLE + 2.0 * p.driver_overalls_thickness) * 0.5
    _loft(
        bm,
        [
            (ankle - shank_axis * 0.010, half * 1.06, half * 1.06, 2.0),
            (shaft_top, half, half, 2.0),
        ],
        shaft_u,
        shaft_v,
        segments,
        steps,
    )

    right = _part(
        context, "driver_boot_r", bm, collection, materials["boot_leather"], root
    )
    _mirror(context, right, "driver_boot_l", collection, root)
