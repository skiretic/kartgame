"""The kart parameter block — every dimension in the generated kart, in meters.

This is the single source of truth for kart geometry. ARCHITECTURE.md §5 item 1
puts correct real-world scale above every other realism lever, and the only way
to hold that is for no module to carry its own copy of a number.

Figures are CIK-FIA KZ regulation limits and common KZ chassis practice, not
invented proportions. Where a regulation states a maximum, the maximum is used,
because that is what a competitive chassis is built to:

    Overall length          1830 mm max
    Overall width           1400 mm max
    Wheelbase               1050 mm max (KZ runs at the limit)
    Rear tire width          215 mm max
    Front tire width         135 mm max
    Main frame rail            30 mm tube, KZ typical
    Rear axle                  50 mm solid

The three figures ARCHITECTURE.md §5 names explicitly — 1.05 m wheelbase, 1.4 m
track width, 0.28 m frame height — are WHEELBASE, TRACK_REAR and FRAME_HEIGHT
below, and scripts/look/lookdev.gd holds the same three for its reference box.
They must not drift apart; issue #21 checks the wheelbase in Godot for exactly
that reason.

Coordinate convention, stated once because getting it wrong is invisible until
the kart drives backwards:

    Blender  +X = kart right, +Y = kart forward, +Z = up
    glTF export with export_yup=True maps (x, y, z) -> (x, z, -y)
    so Blender +Y forward becomes glTF -Z, which is Godot's forward.

Nothing in this package may use Blender's default -Y forward. Build toward +Y.
"""

from __future__ import annotations

import dataclasses
import math
from dataclasses import dataclass


@dataclass(frozen=True)
class KartParams:
    """Every dimension of the kart, in meters and radians.

    Frozen so a geometry module cannot quietly retune the kart for itself. A
    variant is made with `dataclasses.replace`, which keeps the change visible
    at the call site.
    """

    # --- the three figures ARCHITECTURE.md §5 names ------------------------

    wheelbase: float = 1.050
    """Front axle to rear axle. CIK maximum, and what a KZ chassis runs."""

    track_rear: float = 1.400
    """Outside-to-outside rear width. Also the kart's overall width limit."""

    frame_height: float = 0.280
    """Top of the frame's highest structural tube above the ground."""

    # --- overall envelope --------------------------------------------------

    length_overall: float = 1.830
    """Nose bumper to rear bumper. CIK maximum."""

    track_front: float = 1.240
    """Front is narrower than rear on a KZ — it is what lets the front bite."""

    ground_clearance: float = 0.035
    """Underside of the lowest frame tube to the ground.

    Measured to the frame, not to the floor tray, because on a kart the rails
    *are* the lowest point — the tray bolts on top of them. Getting this
    backwards puts the whole chassis 30 mm into the asphalt.
    """

    # --- frame tubes -------------------------------------------------------

    tube_main: float = 0.030
    """Main rail outside diameter. KZ chassis are 30 or 32 mm; 30 is typical."""

    tube_secondary: float = 0.022
    """Bracing, seat struts, bumper hoops."""

    tube_bumper: float = 0.020
    """Nose and side bumper hoops, which are the thinnest tubes on the kart."""

    tube_segments: int = 12
    """Vertices around a tube's circumference.

    12 is the low-poly target. A 30 mm tube seen from the cockpit at 0.5 m
    subtends few enough pixels that 12 reads as round once the normal bake
    from issue #19 carries the shading; the high-poly source uses
    `tube_segments_high`.
    """

    tube_segments_high: int = 32
    """Circumference resolution of the high-poly bake source."""

    bend_radius: float = 0.060
    """Radius of a bent tube corner. A real chassis is mandrel-bent, never
    mitered, and a sharp corner is the single clearest tell of a toy kart."""

    bend_segments: int = 6
    """Polyline steps per bend on the low-poly mesh."""

    bend_segments_high: int = 14

    # --- floor tray --------------------------------------------------------

    tray_thickness: float = 0.004
    """Aluminium floor pan, 4 mm."""

    tray_width: float = 0.560
    tray_length: float = 0.760
    tray_front_y: float = 0.180
    """Forward edge of the tray, measured from the origin (see origin note)."""

    # --- wheels and tires --------------------------------------------------

    rim_diameter: float = 0.127
    """5 inch, which is the only rim size karting uses."""

    tire_front_diameter: float = 0.280
    tire_front_width: float = 0.135
    tire_rear_diameter: float = 0.295
    tire_rear_width: float = 0.215
    """CIK maximum rear width. The rears being visibly fatter than the fronts
    is a large part of reading as a kart rather than as a small car."""

    tire_sidewall_bulge: float = 0.008
    """How far the sidewall stands proud of the rim flange, radially.

    Measured so that the tire's widest point sits at radius
    `rim_diameter / 2 + tire_sidewall_bulge`, from which the profile turns back in
    to meet the bead. Stated precisely because "the bulge" has two plausible
    readings — radial and axial — and they produce different tires.
    """

    tire_shoulder_radius: float = 0.022
    """Tread-to-sidewall corner radius. Kart slicks have a soft shoulder and a
    square one reads as a toy."""

    tire_segments: int = 32
    tire_segments_high: int = 64

    # --- rear axle ---------------------------------------------------------

    stub_axle_length: float = 0.090
    """Kingpin to front hub center.

    Shared: `frame.py` needs it to stop the rails short of the front wheels, and
    `wheels.py` needs it to place the stub. It was duplicated as a literal in both
    before it was hoisted here, which is precisely the drift this parameter block
    exists to prevent.
    """

    axle_diameter: float = 0.050
    """Solid, 50 mm. ARCHITECTURE.md §6: both rear wheels are locked to it,
    and that is the kart's defining dynamic."""

    axle_length: float = 1.080

    # --- seat --------------------------------------------------------------

    seat_width: float = 0.330
    seat_height: float = 0.290
    seat_back_angle: float = 0.610
    """Radians from vertical, ~35 deg. A KZ seat is reclined hard."""

    seat_y: float = -0.060
    """Seat center, rearward of the origin. This sets where the driver's mass
    sits, and ARCHITECTURE.md §6 wants the center of mass slightly rearward."""

    seat_z: float = 0.075
    seat_thickness: float = 0.008
    """Fiberglass seat shell."""

    # --- steering ----------------------------------------------------------

    wheel_diameter: float = 0.320
    """Steering wheel, outside diameter."""

    wheel_rim_thickness: float = 0.024
    wheel_angle: float = 0.470
    """Radians from vertical, ~27 deg. Kart steering columns are steeply laid
    back, and this angle is very visible from the cockpit."""

    wheel_center_z: float = 0.480
    wheel_center_y: float = 0.320
    column_diameter: float = 0.018

    # --- pedals ------------------------------------------------------------

    pedal_y: float = 0.560
    pedal_z: float = 0.090
    pedal_width: float = 0.070
    pedal_length: float = 0.120
    pedal_separation: float = 0.150

    # --- engine, exhaust, radiator ----------------------------------------

    engine_width: float = 0.230
    engine_length: float = 0.260
    engine_height: float = 0.300
    engine_x: float = 0.240
    """Engine center, to the kart's right. A KZ carries its engine on the
    driver's right, and the resulting mass asymmetry is real — issue #15."""

    engine_y: float = -0.190
    engine_z: float = 0.150

    exhaust_length: float = 0.620
    exhaust_max_diameter: float = 0.130
    """Expansion chamber, at its widest. Visually distinctive and a big part of
    a shifter kart's silhouette."""

    exhaust_pipe_diameter: float = 0.034
    exhaust_segments: int = 16
    exhaust_segments_high: int = 32

    radiator_width: float = 0.260
    radiator_height: float = 0.180
    radiator_thickness: float = 0.045
    radiator_x: float = 0.330
    radiator_y: float = 0.090
    radiator_z: float = 0.290

    # --- bodywork ----------------------------------------------------------

    nose_width: float = 0.680
    nose_height: float = 0.130
    nose_y: float = 0.760
    sidepod_length: float = 0.560
    sidepod_height: float = 0.180
    sidepod_x: float = 0.480

    # --- driver ------------------------------------------------------------

    driver_eye_z: float = 0.620
    """Seated eye height. scripts/look/lookdev.gd uses the same figure for its
    look-dev camera, which is why the two must agree."""

    driver_shoulder_z: float = 0.470
    driver_shoulder_span: float = 0.400
    driver_helmet_radius: float = 0.125
    driver_upper_arm: float = 0.290
    driver_forearm: float = 0.260
    """Bone lengths, not mesh sizes. Issue #17 wants the hands to reach the
    wheel at full lock, and that is arithmetic on these two plus the wheel
    radius, not something to discover in the viewport."""

    # --- output and quality ------------------------------------------------

    texel_density: float = 512.0
    """Pixels per meter of surface, for the UV unwrap in issue #18.

    ARCHITECTURE.md §5 item 2 fixes 512 px/m for the track surface and 256 px/m
    for props. A kart is neither: the cockpit camera sits closer to it than to
    anything else in the game, so it takes the track's density rather than a
    prop's. See ADR-0024.
    """

    normal_map_size: int = 2048
    """Baked normal map resolution, issue #19."""

    lod_ratios: tuple[float, ...] = (1.0, 0.55, 0.28, 0.12)
    """Decimation ratios for the LOD chain, issue #20. Index 0 is the mesh as
    built. Godot generates its own distance LODs on import (ADR-0025), so these
    exist for the cases its automatic generator handles badly, not as the
    primary LOD mechanism."""

    def scaled(self, **overrides: float) -> "KartParams":
        """A variant with named fields replaced, for parameter sweeps."""
        return dataclasses.replace(self, **overrides)

    def as_ordered_items(self) -> list[tuple[str, object]]:
        """Fields in declaration order, for the manifest and the caption.

        Declaration order rather than sorted order, so the manifest reads like
        the kart is built rather than like an alphabetized dump.
        """
        return [(f.name, getattr(self, f.name)) for f in dataclasses.fields(self)]


DEFAULT = KartParams()
"""The kart. Anything wanting a different one derives it with `.scaled()`."""


# --- derived geometry ------------------------------------------------------
#
# Positions that follow from the parameter block rather than being independent
# choices. They live here so that changing the wheelbase moves the axles, and
# no module has to remember to do the arithmetic the same way.
#
# The kart's origin is on the ground, laterally centered, midway between the
# axles. That is deliberate and it is what the vehicle solver in M3b will want:
# the chassis body's origin should sit near its center of mass, and a mesh whose
# origin is at the front bumper makes every wheel mount an off-by-a-meter risk.


def front_axle_y(p: KartParams) -> float:
    return p.wheelbase * 0.5


def rear_axle_y(p: KartParams) -> float:
    return -p.wheelbase * 0.5


def front_hub_x(p: KartParams) -> float:
    """Lateral center of a front wheel: track is measured outside to outside."""
    return (p.track_front - p.tire_front_width) * 0.5


def rear_hub_x(p: KartParams) -> float:
    return (p.track_rear - p.tire_rear_width) * 0.5


def front_axle_z(p: KartParams) -> float:
    return p.tire_front_diameter * 0.5


def rear_axle_z(p: KartParams) -> float:
    return p.tire_rear_diameter * 0.5


def rail_z(p: KartParams) -> float:
    """Center height of the main frame rails — the lowest tubes on the kart."""
    return p.ground_clearance + p.tube_main * 0.5


def tray_bottom_z(p: KartParams) -> float:
    """The floor tray bolts to the top of the rails, not to their underside."""
    return rail_z(p) + p.tube_main * 0.5


def tray_top_z(p: KartParams) -> float:
    return tray_bottom_z(p) + p.tray_thickness


def steering_column_base(p: KartParams) -> tuple[float, float, float]:
    """Lower end of the steering column, at the front cross member.

    Derived rather than authored so that it is consistent with `wheel_angle`:
    the line from here to the steering wheel center is the column axis, and its
    angle from vertical is `wheel_angle` by construction. Authoring both ends
    and the angle independently is how a steering wheel ends up visibly not
    square to its own column.
    """
    length = 0.402
    return (
        0.0,
        p.wheel_center_y + math.sin(p.wheel_angle) * length,
        p.wheel_center_z - math.cos(p.wheel_angle) * length,
    )
