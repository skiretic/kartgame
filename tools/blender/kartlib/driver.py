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
where `params.py` puts them. The foot chain is no longer a place where those two
things can differ: `driver_ball_z` was a field reading 0.090 while the live foot
bar sat at `P.pedal_bar_z` = 0.228 — 138 mm apart, a citation still true as a
sentence and false as a number — and #202 replaced the fields with
`P.driver_heel/ball/ankle/knee`, solved off the live pedal on every read. This
module still does not quietly move a sourced endpoint to make a render look
better; the change is that the endpoint now cannot be stale. Same rule as the
locked-straight-arm case §60.1.6 argues for.

Coordinates: +X kart right, +Y forward, +Z up; origin on the ground at
mid-wheelbase. Built on the right and mirrored, because authoring both halves is
two places for a number to be wrong.
"""

from __future__ import annotations

import math
import sys

import bmesh
import bpy
from mathutils import Vector

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

#: §60.1.6 fixes six material names and this module owns their values. All six
#: now live in `build.FIXED_FINISHES` (§60.1.8's group), so the fallback below is
#: a mechanism that no longer fires — kept because the next renamed or deleted
#: finish should stand in loudly rather than crash, and a stand-in nobody
#: mentions is how `engine_alloy` came to hold 116 parts. `_materials` announces
#: any row that falls back, on every build.
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

#: The waist, halfway between the H-point and the shell top. All `estimated`,
#: read off `look_lorandi.png` (S4, the one torso-in-profile frame): the suit
#: pinches visibly between the hip and the chest even with the protector worn
#: under it. Width 316 against the stations' linear 343 — 13 mm per side; depth
#: 202 against a linear 208, the smaller front pinch of a seated belly (the rear
#: face is the rake plane and never moves). Exponent between the hip's 2.6 and
#: the chest's 2.3 so the roundness ramp stays monotonic through the new station.
TORSO_WIDTH_WAIST: float = 0.316
TORSO_DEPTH_WAIST: float = 0.202
TORSO_EXPONENT_WAIST: float = 2.45

#: The lat flare, 52% of the way from the shell top to the acromion. `estimated`,
#: read off `look_giardelli.png` (S3, dead front): the torso widens well below
#: the shoulder caps rather than tapering straight from the shell to the
#: bideltoid. Width 428 against the stations' linear 409 — 9.5 mm per side, kept
#: modest because the upper arms hang at ±200 and a louder flare merges into
#: them. Depth 213 sits on the chest-to-acromion line; the flare is lateral.
TORSO_WIDTH_LAT: float = 0.428
TORSO_DEPTH_LAT: float = 0.213

#: Art. 7.5's body protection, as along-torso stations from the H-point.
#: §60.1.5 says "roughly z 250-450 in the torso frame"; read as distance along the
#: 25 deg torso axis, which puts it over world z 357-538 — the ribs, with the
#: acromion at 527 along. The alternative reading, world z, would put a rib
#: protector across the lumbar spine.
RIB_PROTECTOR_LOW: float = 0.250
RIB_PROTECTOR_HIGH: float = 0.450

#: How far *inside* the torso surface the protector's outer face sits, front and
#: sides. `estimated`: 1 mm — enough that the two surfaces cannot z-fight, small
#: enough that the part still fills the band it protects. **Recessed, not proud,
#: per §60.1.8 finding 2**: both reference frames (`exh_commons_buntschu_kz2.jpg`,
#: `exh_commons_panfilov_kz2.jpg`) show plain overalls with no external shell, so
#: the protection is worn *under* the suit and the built part never renders. It
#: stays built, materialed and watertight because it is a declared `sits_on`
#: contact and #194's mass lumps read it. The 15 mm of
#: `driver_protector_thickness` still goes inward from the outer face, because
#: `driver_hip_breadth` 325 and `driver_seated_shoulder_breadth` 360 are the
#: breadths of a driver *wearing* it — counting it outside would count it twice.
RIB_PROTECTOR_RECESS: float = 0.001

#: How far forward of the torso's rear face (the §60.1.1 rake plane) the
#: protector's rear face sits. `estimated`: 0.3 mm. It cannot be zero — the torso's
#: rear face is authored on the same plane, and two coincident faces z-fight — and
#: it cannot be the full 1 mm recess either, because the shell's back is what the
#: `driver_rib_protector`/`seat_shell` `sits_on` contact bears on: the built shell
#: sits ~0.4 mm behind the plane at the band's foot, so 0.3 keeps the measured gap
#: well inside `CONTACT_TOLERANCE`'s 2 mm. The physical story is the same one
#: ADR-0057 tells: one layer of compressed suit fabric between shell and seat.
RIB_PROTECTOR_REAR_INSET: float = 0.0003

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

#: The helmet shell's profile: horizontal rings, bottom to crown, each row
#: `(z, half_width, half_depth, center_y, exponent, recess)` in helmet-local
#: meters (z from the §60.1.4 center, `center_y` the ring's fore-aft offset from
#: it, `recess` the fraction of `VISOR_RECESS_DEPTH` active at that height).
#:
#: **The contract box is carried by four rows and the two end caps, exactly.**
#: §60.1.6 fixes 250 wide x 340 long x 300 tall about the §60.1.4 center, and
#: the extremes are authored so a vertex lands on each of them at both detail
#: densities: width 250 at the eye row (`half_width` 0.125, ring angles 0 and pi
#: are sampled at 12 and at 32 segments), rear -170 at the same row (`center_y -
#: half_depth`, angle 3pi/2 sampled likewise), front +170 at the chin-bar row
#: (`center_y + half_depth`, angle pi/2, below the recess band), and the caps at
#: z = -150 / +150. Every other row stays strictly inside all three, and the
#: recess only ever moves the surface inward, so the built bounding box is the
#: contract box to the float and not to a tolerance.
#:
#: The shape between the extremes is `estimated`, read off two photographs and
#: not from memory: the buntschu three-quarter frame
#: (`refs/kart-visual/exh_commons_buntschu_kz2.jpg`, an Arai in a KZ2 kart) for
#: the brow step over a recessed visor, the chin bar standing proud below the
#: aperture and the jaw sweeping back to the neck; `look_giardelli.png` (S3,
#: dead front) for the temple width running nearly full to the brow and the
#: chin bar's width. Row by row:
#:
#:   -150  bottom cap, 96 x 120 — the neck aperture's underside, flat and
#:         hidden by the neck and collar. A full-face shell's bottom is a rim,
#:         not a dome, and the cap plane is what puts the 300 mm height on a
#:         surface that exists.
#:    -95  jaw tuck: the shell sweeps in toward the neck below the chin bar.
#:    -55  the chin bar, front +170 — the fore-aft extreme of a full-face
#:         helmet is the chin, not the forehead. Both crops show it.
#:  -28.5  aperture bottom edge, full shell: the chin bar's top ledge. The
#:         6 mm step from the recess floor above reads as the visor's lower
#:         edge sitting on the bar.
#:  -22.5  recess floor, lower ramp knot (values interpolated on the
#:         -28.5..+19 line so the ramp is a feature of the recess alone).
#:    +19  the eye row (`driver_eye_z` - `driver_helmet_z`): width and rear
#:         extremes, and the visor aperture's own center height.
#:  +60.5  recess floor, upper ramp knot (interpolated on the +19..+66.5 line).
#:  +66.5  aperture top edge, full shell — the brow. The 6 mm step over 6 mm of
#:         rise is authored at 45 deg so `smooth_angle`'s 40 deg threshold
#:         marks it sharp and the brow line survives smooth shading (#199's
#:         sidepod trap, applied in advance).
#:    +95, +125, +140  the dome, sagging slightly inside the ellipse through
#:         the width extreme so the crown reads round rather than conic.
#:   +150  crown cap, 20 x 22 — small enough to shade as the top of a dome.
HELMET_PROFILE: tuple[tuple[float, float, float, float, float, float], ...] = (
    (-0.1500, 0.048, 0.060, -0.005, 2.00, 0.0),
    (-0.0950, 0.088, 0.119, 0.001, 2.20, 0.0),
    (-0.0550, 0.108, 0.155, 0.015, 2.30, 0.0),
    (-0.0285, 0.118, 0.152, 0.006, 2.20, 0.0),
    (-0.0225, 0.1189, 0.1533, 0.0043, 2.19, 1.0),
    (0.0190, 0.125, 0.1625, -0.0075, 2.15, 1.0),
    (0.0605, 0.1189, 0.1455, -0.0123, 2.11, 1.0),
    (0.0665, 0.118, 0.143, -0.013, 2.10, 0.0),
    (0.0950, 0.098, 0.113, -0.014, 2.05, 0.0),
    (0.1250, 0.062, 0.075, -0.014, 2.00, 0.0),
    (0.1400, 0.046, 0.055, -0.013, 2.00, 0.0),
    (0.1500, 0.010, 0.011, -0.012, 2.00, 0.0),
)

#: How deep the visor recess sinks below the surrounding shell. `estimated`:
#: 6 mm is a visor's thickness plus its rebate, read off the buntschu crop where
#: the glass clearly sits below the brow and cheek surfaces.
VISOR_RECESS_DEPTH: float = 0.006

#: The recess's angular window, degrees away from dead front in ring parameter.
#: Full depth inside 55 (the 200 mm aperture needs 52 at the eye row's width),
#: fading smoothly to nothing by 72 — short of the ring angles 0 and pi, so the
#: 250 mm width extreme is untouched by construction.
RECESS_FULL_DEG: float = 55.0
RECESS_ZERO_DEG: float = 72.0

#: The glass. `estimated`: its outer face rides 1 mm below the un-recessed
#: shell line (`VISOR_GLASS_DROP - VISOR_THICKNESS`), i.e. 5 mm proud of the
#: recess floor — a visor over its rebate — and its upper and lower edges run
#: into the recess ramps and bury themselves inside the shell, so no open rim
#: faces the camera. **The eye port is still not cut open**: the shell stays a
#: closed loft, watertight and therefore checkable by the winding gate, and an
#: aperture cut at two densities would not be the same shape at both, which
#: `Detail`'s contract forbids.
VISOR_GLASS_DROP: float = 0.005
VISOR_THICKNESS: float = 0.004

#: Corner squareness of the visor aperture. `estimated`: 6 gives a rounded rectangle
#: with short flats top and bottom, which is what a full-face aperture looks like.
VISOR_CORNER_EXPONENT: float = 6.0

#: Upper arm, root to elbow, three stations so the deltoid reads as a muscle
#: cap rather than the top of a cone. The deltoid diameter is `estimated`,
#: arithmetic shown: `driver_bideltoid` 455 less `driver_seated_shoulder_breadth`
#: 360, halved, is the 47.5 mm the deltoid stands outboard of the shell's
#: shoulder line; doubled as a diameter is 95, plus 2 x
#: `driver_overalls_thickness` is 109. The two breadths are measured at
#: different heights, so this is a proportion and not a derivation, and it is
#: labeled accordingly. The root is `estimated` slimmer — that end is the joint,
#: buried inside the torso's shoulder cap — and the bulge sits a quarter of the
#: way down the arm, where S3's dead-front frame shows the sleeve widening just
#: below the shoulder seam before the long taper to the elbow.
#: The deltoid bulge is asymmetric on purpose: its station lifts by
#: `UPPER_ARM_DELTOID_LIFT` along the loft's up direction, so the muscle sits on
#: *top* of the arm — which is where a deltoid is — and no other surface of the
#: arm moves outward at all. Gate 3 forced that discipline before the
#: photograph could justify it, twice: a symmetric 109 mm bulge a quarter of
#: the way down put the underside 0.11 mm into `engine_head_nut_3`, and even a
#: lifted 107 grazed it (0.00 mm) because starting the elbow taper lower makes
#: the mid-arm ~0.3 mm fatter — the old cone clears that nut by less than that.
#: So the deltoid diameter is held a shade *under* the old cone's own value at
#: its station (109 - 23 x 0.15 = 105.6) and the whole bulge is the 5 mm lift;
#: a regulated clearance is not spent on a styling bulge (the arm's own
#: `engine_head` waiver under #206 is the reach solve's, not this cap's).
UPPER_ARM_DIAMETER_ROOT: float = 0.098
UPPER_ARM_DIAMETER_DELTOID: float = 0.105
UPPER_ARM_DELTOID_AT: float = 0.15
UPPER_ARM_DELTOID_LIFT: float = 0.005
UPPER_ARM_DIAMETER_ELBOW: float = 0.086

#: Forearm, elbow to the wrist end of the sleeve. `estimated`: a forearm is a little
#: slimmer at the elbow than the upper arm and tapers hard to the wrist.
FOREARM_DIAMETER_ELBOW: float = 0.096
FOREARM_DIAMETER_WRIST: float = 0.070

#: The gloved hand, closed around the rim tube. Not a block: a mitt is three
#: closed solids in one mesh — the palm-and-fingers band wrapped around the
#: tube, the thumb wrapped the other way, and a cuff over the sleeve — the same
#: several-solids-one-part construction the boot uses and for the same
#: watertightness reason. Both reference frames (`look_giardelli.png`,
#: `exh_commons_buntschu_kz2.jpg`'s glove) show exactly this: fingers over the
#: far side of the tube, knuckles standing proud outboard, thumb closing the
#: near side, cuff riding over the sleeve.
#:
#: `GLOVE_ALONG_RIM` is `derived`: the NASA table's `hand length` is 191 mm and
#: four gloved fingers stack to about 0.55 of it — 105 along the rim tangent.
#: Everything else is `estimated`; hand breadth is not in §60.1.2's table.
GLOVE_ALONG_RIM: float = 0.105

#: Inner radius of both wraps around the rim tube (`wheel_rim_thickness`/2 is
#: 19). `estimated` at 4 mm *inside* the tube surface: fingers squeeze a foam
#: grip, and the overlap is also what keeps the declared `grips` contact
#: measuring zero rather than riding `CONTACT_TOLERANCE`'s 2 mm edge.
GLOVE_WRAP_INNER: float = 0.015

#: The palm-and-fingers band, as knots of `(wrap angle deg, outer radius)`.
#: Angle 0 is outboard on the rim's cross-section, positive toward the driver's
#: side of the wheel plane. Knots ascend because `_wrap_loft`'s winding proof
#: assumes the sweep runs with +phi: the band starts at the fingertips tucked
#: behind the far side (-115, where a fingertip is barely thicker than the
#: glove), comes over the outboard knuckles (+20, the thickest station — tube 19
#: plus ~23 of hand, glove and padding), and ends at the palm heel on the
#: driver's side (+100). All `estimated` against a 25-30 mm deep gloved hand.
GLOVE_FINGER_KNOTS: tuple[tuple[float, float], ...] = (
    (-115.0, 0.026),
    (-75.0, 0.032),
    (-30.0, 0.038),
    (20.0, 0.042),
    (60.0, 0.040),
    (100.0, 0.036),
)

#: The thumb, wrapping the driver's side of the tube toward the fingertips —
#: the opposite sense to the fingers, which is what "closed around" means. Its
#: knots start inside the palm heel's arc so base and heel read as one mass.
#: `half length` is along the rim tangent; the offset shifts the whole thumb
#: toward the 12 o'clock end of the hand, where a thumb lives on a 3 o'clock
#: grip. All `estimated`.
GLOVE_THUMB_KNOTS: tuple[tuple[float, float], ...] = (
    (95.0, 0.028),
    (170.0, 0.030),
    (235.0, 0.022),
)
GLOVE_THUMB_HALF_LENGTH: float = 0.017
GLOVE_THUMB_OFFSET: float = 0.033

#: The cuff: Art. 7.3 wants the wrist covered, and §60.1.6's row says the cuff
#: overlaps the sleeve. 55 mm up the forearm from the grip at radius 40 — the
#: sleeve ends at 35 — flaring 8% at the open end. `estimated`.
GLOVE_CUFF_LENGTH: float = 0.055
GLOVE_CUFF_RADIUS: float = 0.040
GLOVE_CUFF_FLARE: float = 1.08

#: Cross-section squareness of the wraps. `estimated`: a row of fingers is
#: flatter than an ellipse.
GLOVE_EXPONENT: float = 2.5

#: Thigh at the hip. `derived`: two thighs fill `driver_hip_breadth`, so each is
#: half of it — and that figure is the thigh's *width*. Computed from the field
#: rather than written out here.
#: The knee end is `estimated` — a clothed knee measures about 128 across.
THIGH_DIAMETER_KNEE: float = 0.128

#: The thigh's height as a fraction of its width. `estimated`: a seated thigh
#: carrying a driver's weight spreads against the pan — wider than tall — and
#: S4's profile shows the leg's top line running low over the seat lip rather
#: than the half-cylinder a circular section draws. Applied to both stations so
#: the taper stays straight.
THIGH_FLATTEN: float = 0.87

#: Shank, four stations: the calf is a bulge on the *rear* of the leg, not a
#: swelling of its axis. Knee and ankle ends `estimated` as before — just under
#: the knee the leg is a little slimmer than the knee itself, and at the ankle
#: it is the boot's shaft rather than the leg.
#:
#: The calf sits at 45% rather than S4's upper-third read, and the guard
#: station above it is pinned exactly on the straight knee-to-ankle line, both
#: because of one measured fact: `chassis_steering_support_upper`'s shoulder
#: knee at (±150, 150, 335) passes **67.3 mm** from the shank's axis at 19% of
#: the way down, where the old straight taper's own surface plus the Ø16 tube
#: leaves a clearance under a millimeter. Two calf attempts up there measured
#: 4.86 and then 5.33 mm inside the tube (gate 3; the rear shift moved *toward*
#: the knee of the support, which sits below-rear of the upper shank). So the
#: upper fifth of the shank is not this module's to style, the guard station
#: says so in geometry, and the bulge lives at mid-shank where the nearest
#: support segment is 136 mm out. Diameter and the 4 mm rearward shift are
#: `estimated`; the shin line stays within ~2 mm of straight, which is also
#: what S4 shows.
SHANK_DIAMETER_KNEE: float = 0.124
SHANK_GUARD_AT: float = 0.20
SHANK_DIAMETER_CALF: float = 0.120
SHANK_CALF_AT: float = 0.45
SHANK_CALF_REARWARD: float = 0.004
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

    The reach is a join between two parameter blocks that were authored in
    different waves, and §60.1.6's rule is that a finding which cannot be
    adjudicated against a sourced figure gets a waiver and a ticket rather than a
    geometry change. This is the waiver's measurement, taken on every build so it
    cannot rot: the moment somebody corrects either side, the line goes quiet.

    The foot no longer appears here. It was this function's other line -- 140.8 mm
    from `driver_ball_*` to the live bar, #202 -- and the fix was not to close the
    number but to delete the field: `P.driver_ball` is solved off the live pedal,
    so the sole's tangency is true by construction. What is still worth printing
    is the pose that solve actually lands on, because a pitch that walks toward
    vertical is a pedal drifting out of the foot's reach and the fatal check in
    `P.driver_foot_pitch` fires only at the absurd end of that walk.
    """
    ball = Vector(P.driver_ball(p))
    bar_center = Vector(
        (p.pedal_separation * 0.5, P.pedal_bar_y(p), P.pedal_bar_z(p))
    )
    print(
        "    driver   foot pitch %.1f deg; ball of foot %.2f mm off the bar "
        "surface (0 is tangent)"
        % (
            math.degrees(P.driver_foot_pitch(p)),
            ((ball - bar_center).length - p.pedal_bar_diameter * 0.5) * 1000.0,
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

    # The two intermediate stations are this module's own (§60.1.6: the shape
    # *between* the contract stations is the building module's), and both were
    # read off photographs rather than styled from memory: `look_lorandi.png`
    # (S4, the torso-in-profile frame) shows the suit pinch between the hip and
    # the chest, and both it and `look_giardelli.png` (S3, dead front) show the
    # lat flare running wide well below the shoulder line rather than a straight
    # taper from the shell top. Neither number is a contract value; the three
    # §60.1.7 breadths (325 / 360 / 455) still land exactly on their stations.
    waist_z = 0.5 * (p.driver_hip_z + shell_top_z)
    lat_z = shell_top_z + 0.52 * (p.driver_shoulder_z - shell_top_z)
    return [
        station(p.driver_hip_z, p.driver_hip_breadth, PELVIS_DEPTH, TORSO_EXPONENT_HIP),
        # The waist. `estimated`: 316 against a linear 343 at this height — a
        # 13 mm pinch per side, which is what the suit shows in S4 with the rib
        # protector worn under it, and deliberately shallower than a bare-torso
        # waist because the protector band starting 109 mm above must still fit
        # inside (its own recess check knots on this station's neighbors).
        station(waist_z, TORSO_WIDTH_WAIST, TORSO_DEPTH_WAIST, TORSO_EXPONENT_WAIST),
        station(
            shell_top_z,
            p.driver_seated_shoulder_breadth,
            TORSO_DEPTH_CHEST,
            TORSO_EXPONENT_CHEST,
        ),
        # The lats. `estimated`: 428 against a linear 409 at this height — the
        # V-taper bulging outboard of the straight hip-to-shoulder line, read
        # off S3's dead front where the torso visibly widens well below the
        # shoulder caps. 8.5 mm per side is deliberately modest: the arms hang
        # at ±200 and a louder flare merges the torso into them.
        station(lat_z, TORSO_WIDTH_LAT, TORSO_DEPTH_LAT, TORSO_EXPONENT_CHEST),
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
    """Art. 7.5's body protection, worn under the suit — a band inside the torso.

    Its outer face is the torso's surface *minus* `RIB_PROTECTOR_RECESS` on the
    front and sides (§60.1.8 finding 2: both reference frames show plain
    overalls, so the protection is under the suit and this part never renders),
    with the rear face `RIB_PROTECTOR_REAR_INSET` forward of the rake plane the
    torso's own rear face sits on. The 15 mm of `driver_protector_thickness`
    goes *inward* from there. Putting it outward would count it twice:
    `driver_hip_breadth` 325 and `driver_seated_shoulder_breadth` 360 are
    already the breadths of a driver wearing one, which is what makes him fill
    a 333 mm shell.

    The station arithmetic keeps the rear face at `rear + REAR_INSET` exactly:
    the fore-aft half-shrink is the mean of the rear inset and the front
    recess, and the center moves forward by half their difference, so both
    faces land where their constants say rather than where a symmetric shrink
    happens to put them.
    """
    p = context.params
    axis = _torso_axis(p)
    hip = _hip(p)
    low_z = (hip + axis * RIB_PROTECTOR_LOW).z
    high_z = (hip + axis * RIB_PROTECTOR_HIGH).z

    stations = _torso_stations(p)
    # Knot the band at every torso station inside its span, not just at its own
    # edges: the torso's width is piecewise linear with kinks at its stations
    # (the shell top sits inside the 357-538 band), and a band interpolated
    # straight across a kink pokes back out through the surface it is supposed
    # to hide under — measured at 0.13 mm over the shell-top station before
    # these knots existed.
    knots = sorted(
        {low_z, high_z}
        | {station[0].z for station in stations if low_z < station[0].z < high_z}
    )
    banded: list[Station] = []
    for z in knots:
        half_u, half_v, exponent = _torso_section(stations, z)
        rear = _back_plane_y(p, z)
        half_depth = half_v - 0.5 * (RIB_PROTECTOR_RECESS + RIB_PROTECTOR_REAR_INSET)
        banded.append(
            (
                Vector((0.0, rear + RIB_PROTECTOR_REAR_INSET + half_depth, z)),
                half_u - RIB_PROTECTOR_RECESS,
                half_depth,
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


def _helmet_center(p: P.KartParams) -> Vector:
    return Vector((0.0, p.driver_helmet_y, p.driver_helmet_z))


def _helmet_section(z: float) -> tuple[float, float, float, float, float]:
    """`(half_width, half_depth, center_y, exponent, recess fraction)` at a height.

    Piecewise linear over `HELMET_PROFILE`'s authored rows, clamped at the caps —
    the same lookup discipline as `_torso_section` and for the same reason: the
    shell loft and the visor both read this one function, so the glass cannot
    drift off the shell it sits in. Linear interpolation is also what keeps the
    low and the high build on one surface: every intermediate ring lies on the
    straight line between the rows that bracket it.
    """
    rows = HELMET_PROFILE
    if z <= rows[0][0]:
        return rows[0][1:]
    for index in range(len(rows) - 1):
        low, high = rows[index], rows[index + 1]
        if low[0] <= z <= high[0]:
            span = high[0] - low[0]
            t = 0.0 if span <= 1.0e-9 else (z - low[0]) / span
            return tuple(
                low[k] + (high[k] - low[k]) * t for k in range(1, 6)
            )  # type: ignore[return-value]
    return rows[-1][1:]


def _recess_weight(angle: float) -> float:
    """The recess's angular falloff, 1 dead ahead to 0 outside `RECESS_ZERO_DEG`.

    `angle` is the ring parameter in radians; distance is measured from the
    front (pi/2) the short way around. Smoothstep between the two authored
    degrees, so the recess wall in the horizontal direction is a slope rather
    than a step — the hard edges of the recess are its top and bottom ramps,
    where the visor's own edges hide them.
    """
    front = math.degrees(
        abs((angle - 0.5 * math.pi + math.pi) % (2.0 * math.pi) - math.pi)
    )
    if front <= RECESS_FULL_DEG:
        return 1.0
    if front >= RECESS_ZERO_DEG:
        return 0.0
    s = (RECESS_ZERO_DEG - front) / (RECESS_ZERO_DEG - RECESS_FULL_DEG)
    return s * s * (3.0 - 2.0 * s)


def _head(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
) -> None:
    """The shell, lofted from `HELMET_PROFILE`. Axes world-aligned, **not raked.**

    §60.1.6 spells the helmet "250 wide x 340 long x 300 tall" and `params.py`
    names the three fields `_width` / `_length` / `_height`. Those are world
    words: a helmet raked back 25 degrees is not 300 mm tall and is not 340 mm
    long, so the contract's own dimensions only mean what they say on an
    axis-aligned shell. Built that way for that reason — and §60.1.8 finding 3
    is the photographic support: both reference drivers hold the head upright
    with the face pointing where the kart is going. The two documented §60.1.4
    inconsistencies this costs (the raked crown row at (0, -511, 860), and the
    eye point landing on the shell's own fore-aft mid-plane so the driver looks
    out of the middle of his head) are §60.1.4's to settle and are reported by
    the build print below rather than patched here.

    Watertightness is the same argument as the rib protector's band: a closed
    loft with two planar caps is checkable by the winding gate, and the visor
    recess is a displacement *of* the loft's rings rather than a hole cut
    through them, so the checkable property survives the styling.
    """
    p = context.params
    detail = context.detail
    segments = detail.tube_segments
    steps = max(1, detail.bend_segments // 2)

    # The contract box lives in `params.py`'s three fields and the profile is
    # authored in meters, so the two could drift apart in silence — the exact
    # failure the parameter-coverage gate exists for. Asserted instead of
    # assumed, the same discipline as `driver_shoulder_span` against
    # `driver_shoulder_x`: the profile's own extremes must *be* the fields.
    width = 2.0 * max(row[1] for row in HELMET_PROFILE)
    length = max(row[3] + row[2] for row in HELMET_PROFILE) - min(
        row[3] - row[2] for row in HELMET_PROFILE
    )
    height = HELMET_PROFILE[-1][0] - HELMET_PROFILE[0][0]
    for label, built, field in (
        ("width", width, p.driver_helmet_width),
        ("length", length, p.driver_helmet_length),
        ("height", height, p.driver_helmet_height),
    ):
        if abs(built - field) > 1.0e-9:
            raise SystemExit(
                "driver.py: HELMET_PROFILE's %s is %.4f m against "
                "driver_helmet_%s = %.4f m. The profile's extremes are the "
                "contract box (spec 60.1.6) and may not drift from the fields "
                "that publish it."
                % (label, built, label, field)
            )

    center = _helmet_center(p)
    u = Vector((1.0, 0.0, 0.0))
    v = Vector((0.0, 1.0, 0.0))

    # Ring heights: the authored rows plus `steps` subdivisions per span, the
    # same densification rule as `_loft`'s. Every value a ring needs is linear
    # in z between rows, so both densities sample one surface.
    heights: list[float] = []
    for index in range(len(HELMET_PROFILE) - 1):
        low_z = HELMET_PROFILE[index][0]
        high_z = HELMET_PROFILE[index + 1][0]
        for step in range(steps):
            heights.append(low_z + (high_z - low_z) * step / steps)
    heights.append(HELMET_PROFILE[-1][0])

    bm = bmesh.new()
    rings: list[list[bmesh.types.BMVert]] = []
    for z in heights:
        half_u, half_v, offset_y, exponent, recess_fraction = _helmet_section(z)
        ring_center = center + Vector((0.0, offset_y, z))
        power = 2.0 / exponent
        ring: list[bmesh.types.BMVert] = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            cosine, sine = math.cos(angle), math.sin(angle)
            x = math.copysign(abs(cosine) ** power, cosine) * half_u
            y = math.copysign(abs(sine) ** power, sine) * half_v
            radial = u * x + v * y
            length = radial.length
            depth = VISOR_RECESS_DEPTH * recess_fraction * _recess_weight(angle)
            if depth > 0.0 and length > 1.0e-9:
                radial *= (length - depth) / length
            ring.append(bm.verts.new(ring_center + radial))
        rings.append(ring)

    # Quads in ring order: u x v is +Z, the loft direction, so this is exactly
    # `_loft`'s winding and the faces come out outward for the same reason.
    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        for step in range(segments):
            following = (step + 1) % segments
            bm.faces.new(
                (lower[step], lower[following], upper[following], upper[step])
            )
    bm.faces.new(tuple(reversed(rings[0])))
    bm.faces.new(tuple(rings[-1]))
    _part(context, "driver_helmet", bm, collection, materials["helmet_shell"], root)

    _visor(context, collection, materials, root, segments, steps)

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
    steps: int,
) -> None:
    """A lens in the shell's recess, closed so the winding gate can see it.

    Built as two sheets riding the helmet's **un-recessed** profile — the outer
    at `VISOR_GLASS_DROP - VISOR_THICKNESS` below it, the inner at
    `VISOR_GLASS_DROP` — joined by four rim strips. Following the smooth profile
    rather than the recessed shell keeps the glass unkinked; its top and bottom
    edges then run into the recess's 6 mm ramps and bury themselves inside the
    shell, which is what a visor disappearing under its brow looks like. Every
    one of the five windings is derived in the comments below rather than tried;
    a rim wound inward is invisible in a render and would leave the part
    enclosing a negative volume.
    """
    p = context.params
    center = _helmet_center(p)

    # The eye point in helmet-local coordinates. Only its **height** places the
    # aperture: the visor sits on the shell's *front surface* at the eye's own
    # height, and where the eye sits fore-aft along the shell is §60.1.4's business,
    # measured and reported rather than compensated for. A helmet's front face is
    # about 100 mm ahead of a real eye; this one prints 159, because §60.1.4 walks
    # sitting eye height along the torso axis and so lands the eye near the shell's
    # own fore-aft mid-plane. `driver_eye_x` is the interocular half-distance, so
    # the aperture is centred on x = 0 and merely has to be wide enough to clear
    # both.
    eye_local = Vector(
        (p.driver_eye_x, p.driver_eye_y - center.y, p.driver_eye_z - center.z)
    )
    local_eye_z = eye_local.z

    def profile_y(local_x: float, local_z: float) -> float:
        """The un-recessed shell's front surface at (x, z), helmet-local."""
        half_u, half_v, offset_y, exponent, _recess = _helmet_section(local_z)
        residual = 1.0 - abs(local_x / half_u) ** exponent
        if residual <= 0.0:
            return offset_y
        return offset_y + half_v * residual ** (1.0 / exponent)

    front_at_eye = profile_y(eye_local.x, eye_local.z)
    if front_at_eye <= eye_local.y:
        raise SystemExit(
            "driver.py: the eye point (%.0f, %.0f, %.0f) is outside the helmet "
            "shell, so there is nothing for a visor to sit in front of. Check "
            "driver_eye_* against driver_helmet_* and HELMET_PROFILE."
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
            (front_at_eye - eye_local.y) * 1000.0,
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

    rows = max(4, 2 * steps)
    columns = max(6, segments // 2)

    def surface(local_x: float, local_z: float, drop: float) -> Vector:
        """A point `drop` below the un-recessed profile, along its outward normal.

        The normal comes from central differences of `profile_y` rather than an
        analytic derivative: the profile is piecewise linear in z, so its exact
        derivative jumps at every authored row, and a face that straddles a row
        would take its normal from whichever side the float landed on. A fixed
        1 mm difference is deterministic and reads the mean slope, which is what
        a 4 mm glass sheet spanning the row actually follows.
        """
        eps = 0.001
        y = profile_y(local_x, local_z)
        dy_dx = (profile_y(local_x + eps, local_z) - profile_y(local_x - eps, local_z)) / (
            2.0 * eps
        )
        dy_dz = (profile_y(local_x, local_z + eps) - profile_y(local_x, local_z - eps)) / (
            2.0 * eps
        )
        # Tangents (1, dy/dx, 0) and (0, dy/dz, 1); their cross product with +Y
        # orientation is the outward normal of a front surface.
        normal = Vector((-dy_dx, 1.0, -dy_dz)).normalized()
        return center + Vector((local_x, y, local_z)) - normal * drop

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
                bm.verts.new(surface(local_x, local_z, VISOR_GLASS_DROP))
            )
            outer_row.append(
                bm.verts.new(
                    surface(
                        local_x, local_z, VISOR_GLASS_DROP - VISOR_THICKNESS
                    )
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
    shoulder = Vector((p.driver_shoulder_x, p.driver_shoulder_y, p.driver_shoulder_z))
    elbow = _elbow(p, +1.0)
    grip = _grip(p, +1.0)

    # Upper arm, with the deltoid cap near the root, lifted onto the arm's top
    # side (the constants' own comment holds the derivation, the photograph and
    # the gate-3 measurement that forced the lift).
    u, v = _frame(elbow - shoulder, Vector((0.0, 0.0, 1.0)))
    deltoid = shoulder.lerp(elbow, UPPER_ARM_DELTOID_AT) + v * UPPER_ARM_DELTOID_LIFT
    bm = bmesh.new()
    _loft(
        bm,
        [
            (shoulder, UPPER_ARM_DIAMETER_ROOT * 0.5, UPPER_ARM_DIAMETER_ROOT * 0.5, 2.0),
            (
                deltoid,
                UPPER_ARM_DIAMETER_DELTOID * 0.5,
                UPPER_ARM_DIAMETER_DELTOID * 0.5,
                2.0,
            ),
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

    _glove(context, collection, materials, root, segments, steps)

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


def _wrap_loft(
    bm: bmesh.types.BMesh,
    tube_center: Vector,
    radial: Vector,
    axial: Vector,
    tangent: Vector,
    knots: tuple[tuple[float, float], ...],
    half_length: float,
    t_offset: float,
    segments: int,
    steps: int,
) -> None:
    """One closed solid wrapped part-way around the rim tube.

    Stations sweep an arc around `tube_center`: at wrap angle phi the ring's
    plane is spanned by `d(phi) = cos phi * radial + sin phi * axial` (through
    the tube's cross-section) and the rim `tangent`. The ring is a superellipse
    from `GLOVE_WRAP_INNER` to that station's outer radius, so the solid is a
    curved slab hugging the tube — fingers — rather than a box near it.

    Winding: `d(phi) x tangent` is exactly `dd/dphi` (verified by expanding the
    cross products: `radial x tangent = axial` and `axial x tangent = -radial`),
    so each ring's `u x v` points along the sweep just as `_loft` requires, the
    same quad pattern winds outward, and the caps close it watertight. Ring
    parameters are linear in phi between knots and the rings sample a single
    analytic swept surface, so the two densities are one shape — the helmet's
    argument, bent around a tube.
    """
    stations: list[tuple[float, float, float]] = []
    for index in range(len(knots) - 1):
        low, high = knots[index], knots[index + 1]
        for step in range(steps):
            t = step / steps
            stations.append(
                (
                    low[0] + (high[0] - low[0]) * t,
                    low[1] + (high[1] - low[1]) * t,
                    half_length,
                )
            )
    stations.append((knots[-1][0], knots[-1][1], half_length))

    rings: list[list[bmesh.types.BMVert]] = []
    for angle_deg, outer, half_t in stations:
        angle = math.radians(angle_deg)
        direction = radial * math.cos(angle) + axial * math.sin(angle)
        mid = 0.5 * (GLOVE_WRAP_INNER + outer)
        ring_center = tube_center + direction * mid + tangent * t_offset
        rings.append(
            _ring(
                bm,
                ring_center,
                direction,
                tangent,
                0.5 * (outer - GLOVE_WRAP_INNER),
                half_t,
                GLOVE_EXPONENT,
                segments,
            )
        )
    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        for step in range(segments):
            following = (step + 1) % segments
            bm.faces.new(
                (lower[step], lower[following], upper[following], upper[step])
            )
    bm.faces.new(tuple(reversed(rings[0])))
    bm.faces.new(tuple(rings[-1]))


def _glove(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    materials: dict[str, bpy.types.Material],
    root: bpy.types.Object,
    segments: int,
    steps: int,
) -> None:
    """A mitt closed around the rim tube, thumb wrapping the other way.

    The rim tangent at 3 o'clock is the wheel plane's in-plane "up",
    `(0, cos rake, sin rake)` -- differentiate §60.2.1's `rim(phi)` and evaluate
    at 90 degrees -- and the tube's cross-section plane is spanned by the lateral
    direction and the wheel plane's own normal. So the whole hand is oriented by
    the wheel and follows a column rake change, which an axis-aligned fist would
    not.

    The wraps enclose the tube (`GLOVE_WRAP_INNER` is inside its surface), which
    is what a hand does and what `joints.DRIVER_CONTACTS`' `grips` row permits
    and requires: a fist wrapped round a rim is zero millimeters from it. The
    tube's centerline at 3 o'clock is half a `wheel_rim_thickness` inboard of
    the §60.2.1 grip point, because that point is the rim's outer extreme —
    where the *outline* is normalized to `wheel_diameter` — and the hand wraps
    the tube, not the extreme.
    """
    p = context.params
    rake = P.wheel_rake(p)
    tangent = Vector((0.0, math.cos(rake), math.sin(rake))).normalized()
    axial = Vector((0.0, -math.sin(rake), math.cos(rake))).normalized()
    radial = Vector((1.0, 0.0, 0.0))
    grip = _grip(p, +1.0)
    elbow = _elbow(p, +1.0)
    tube_center = grip - radial * (p.wheel_rim_thickness * 0.5)

    bm = bmesh.new()
    # Palm and fingers, fingertips behind the far side to the heel on the
    # driver's side.
    _wrap_loft(
        bm,
        tube_center,
        radial,
        axial,
        tangent,
        GLOVE_FINGER_KNOTS,
        GLOVE_ALONG_RIM * 0.5,
        0.0,
        segments,
        steps,
    )
    # The thumb, wrapping the opposite sense, shifted toward 12 o'clock.
    _wrap_loft(
        bm,
        tube_center,
        radial,
        axial,
        tangent,
        GLOVE_THUMB_KNOTS,
        GLOVE_THUMB_HALF_LENGTH,
        GLOVE_THUMB_OFFSET,
        segments,
        steps,
    )
    # The cuff, up the forearm's own axis so it swallows the sleeve end.
    toward_elbow = (elbow - grip).normalized()
    cuff_end = grip + toward_elbow * GLOVE_CUFF_LENGTH
    u, v = _frame(cuff_end - grip, Vector((0.0, 0.0, 1.0)))
    _loft(
        bm,
        [
            (grip, GLOVE_CUFF_RADIUS, GLOVE_CUFF_RADIUS, 2.0),
            (
                cuff_end,
                GLOVE_CUFF_RADIUS * GLOVE_CUFF_FLARE,
                GLOVE_CUFF_RADIUS * GLOVE_CUFF_FLARE,
                2.0,
            ),
        ],
        u,
        v,
        segments,
        steps,
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
    knee = Vector(P.driver_knee(p))
    ankle = Vector(P.driver_ankle(p))
    heel = Vector(P.driver_heel(p))
    ball = Vector(P.driver_ball(p))

    # Thigh. The hip end's width is `derived`: two thighs fill
    # `driver_hip_breadth`. The section is flattened by `THIGH_FLATTEN` — the
    # loft's v is the near-vertical frame direction, so the flatten lands on the
    # height and the width stays the derived figure.
    thigh_root = p.driver_hip_breadth * 0.5
    u, v = _frame(knee - hip, Vector((0.0, 0.0, 1.0)))
    bm = bmesh.new()
    _loft(
        bm,
        [
            (hip, thigh_root * 0.5, thigh_root * 0.5 * THIGH_FLATTEN, 2.3),
            (
                knee,
                THIGH_DIAMETER_KNEE * 0.5,
                THIGH_DIAMETER_KNEE * 0.5 * THIGH_FLATTEN,
                2.3,
            ),
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

    # Shank. The guard station's diameter is *computed* on the knee-to-ankle
    # line, so the upper fifth cannot grow whatever the calf constants say —
    # that band belongs to `chassis_steering_support_upper`, measured (the
    # constants' comment has the figures). The shank axis runs forward-down to
    # the pedals, so the frame's v — the up-ish perpendicular — leans forward;
    # shifting the calf station by -v puts the bulge on the back of the leg,
    # which is where a calf is.
    u, v = _frame(ankle - knee, Vector((0.0, 0.0, 1.0)))
    guard_diameter = SHANK_DIAMETER_KNEE + (
        SHANK_DIAMETER_ANKLE - SHANK_DIAMETER_KNEE
    ) * SHANK_GUARD_AT
    calf = knee.lerp(ankle, SHANK_CALF_AT) - v * SHANK_CALF_REARWARD
    bm = bmesh.new()
    _loft(
        bm,
        [
            (knee, SHANK_DIAMETER_KNEE * 0.5, SHANK_DIAMETER_KNEE * 0.5, 2.0),
            (
                knee.lerp(ankle, SHANK_GUARD_AT),
                guard_diameter * 0.5,
                guard_diameter * 0.5,
                2.0,
            ),
            (calf, SHANK_DIAMETER_CALF * 0.5, SHANK_DIAMETER_CALF * 0.5, 2.0),
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
            ("knee", Vector((side * knee.x, knee.y, knee.z))),
            ("ankle", Vector((side * ankle.x, ankle.y, ankle.z))),
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
