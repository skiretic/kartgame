"""Issue #13 — seat, steering wheel, pedals, and the KZ's shifter and clutch.

This is the geometry the cockpit camera spends the whole race looking at, and
ARCHITECTURE.md §7 puts it in its own group for exactly that reason: it is an LOD
only one rig ever sees, so it goes in the `interior` collection rather than into
`chassis`, and issue #20 can then range-cull the whole node from the chase view.

What makes a cockpit read as a KZ shifter kart rather than as a rental kart, in
order of how much each one costs to get wrong:

1.  **The seat is a thin shell, not a slab.** A KZ seat is a fiberglass shell
    `seat_thickness` thick with flared wings that wrap the hips and a lip rolled
    around every free edge. Built as a lofted surface plus its own offset, so the
    4 mm rim is real geometry with an edge to catch a highlight. A zero-thickness
    seat reads as a decal on the frame. It is **wider at the shoulders than at the
    hips** -- 368 against 333 -- and one `seat_width` could not hold both: the old
    table tapered to 0.812 and built a shell 268 mm across the top.
2.  **The wheel is square to its own column, plus seven degrees.** Every part of
    the steering — the bore, the column, the wheel centre, the pivot's basis — is
    one chain derived from `params.lower_bore`, the **welded** end. Authoring the
    wheel's angle a second time is how a wheel ends up visibly skewed on its
    column, which is the single most obvious cockpit tell there is; authoring the
    *column's* free end and deriving its fixed one, which is what this module used
    to do, is how a column ends up 37 mm from the bracket that carries it.

    The seven degrees are a real part and not a liberty: the wheel plane rakes 43
    from vertical against the column's 36, OTK sells an inclined hub and an
    inclined spacer to do exactly that, and Art. 4.5 permits *"A spacer […] between
    the steering wheel and the hub."* The error is invisible from every angle
    except a true side elevation, so a turntable does not catch it.
3.  **The wheel is a flat-bottomed butterfly, not a car wheel.** Three spokes, a
    visible dished boss, and a chord across the bottom so the driver's thighs
    clear it. A circular rim makes a kart look like a toy car immediately.
    Measured on the built rim: 319.7 mm wide, 243.7 mm tall, a 39.8 mm dip
    between the horns and a 135.6 mm flat across the bottom. The dip is the
    feature that has to survive being looked at from 0.55 m, and the first
    version's 13.3 mm did not — see `WHEEL_OUTLINE`.
4.  **The shifter and the clutch lever are the whole KZ silhouette.** §6.3: hand
    shifter on the driver's right, clutch lever on the wheel. Issue #15's
    silhouette test is "reads as a shifter kart rather than a single-speed", and
    these two parts are what decide it.
5.  **The pedals are organ pedals on a bottom pivot, and the legs are nearly
    straight.** Their reach relative to the seat is one of the strongest scale
    cues on the whole kart, and this module used to say it could not fix it.

    **It is fixed, and nobody fitted it.** Criterion 1 of issue #13 measured hip
    point to pad face at 618.5 mm, folding the knee to 89 degrees where "nearly
    straight" is 850-870, and this module correctly refused to retune around it
    because the fault was in `seat_y`, `pedal_y` and `wheel_center_y`. Correcting
    the seat and the pedals **independently, each to its own source** -- Tillett's
    published size chart for one and OTK's part photographs for the other -- lands
    hip (0, -230, +36) to bar (+-85, +585, +228) at **836 mm**: 14 to 34 mm short
    of the target and well inside the 180 mm of adjustment a real pedalboard
    carries in ten holes. That is what two separately-sourced parts do when both
    are put where their sources say.

    The same arithmetic on the wheel: hip to the rim's lower edge at (0, +115,
    +429) is 525 mm against `driver_upper_arm` + `driver_forearm` = 550. It fits
    with 25 mm to spare where the old geometry put the rim 4 mm past a fully
    extended arm.

Coordinates: +X right, +Y forward, +Z up. Nothing here is mirrored wholesale —
the seat is symmetric but is built in one pass because it is a single lofted
surface, and the shifter is deliberately right-hand only.

Interfaces published for later milestones. The axes are measured on the built
scene and on the exported `.glb`, not inferred from this file — see `_steering`
for the evidence:

    name                   Blender local   Godot local   what it does
    steering_pivot         +Z              +Y            positive steers left
    pedal_throttle_pivot   +X              +X            positive presses
    pedal_brake_pivot      +X              +X            positive presses
    seat_root              --              --            hip point, CoM reference
"""

from __future__ import annotations

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

from . import build
from . import params as P


# --- dimensions that belong in params.py -----------------------------------
#
# Every one of these is a real dimension of the kart and should move into
# `KartParams` when the parameter block is next touched. They live here only
# because params.py is owned elsewhere and was being edited concurrently while
# this module was written. None of them is a free choice: each is noted with
# what constrains it.

#: Seat pan depth, hip point to the pan's front lip. `derived`, spec §40.3:
#: 395 mm of total horizontal run less the back's own 135.3. Was 0.300.
SEAT_PAN_LENGTH = 0.260

#: How far the pan's front lip rises above the base plane. `sourced`: Tillett T11
#: dimension D is 100 mm. Was 0.055 -- the lip has to hold the driver against the
#: pan under braking, so it is a real edge and not a taper.
SEAT_PAN_FRONT_RISE = 0.100

#: Length of the level stretch of pan immediately ahead of the hip point.
SEAT_PAN_FLAT = 0.120

#: Radius of the pan-to-back bend. Large, because a fiberglass shell cannot be
#: folded — the lumbar transition on a real seat is a sweeping curve.
SEAT_HIP_RADIUS = 0.100

#: Half-width of the shell as a fraction of `seat_width * 0.5`, against normalized
#: arc length along the spine (0 = pan lip, 1 = top of the back).
#:
#: **The top entry was 0.812 and it is 1.105.** A real shell is 35 mm *wider* at
#: the shoulders than at the hips -- Tillett B 360 against A 325 -- and this table
#: tapered the other way, building a shell 268 mm across the shoulders where the
#: part is 368. 1.105 is 368/333 exactly, and the two intermediate stations are
#: made monotonic so the flank does not waist and re-flare. A straight box reads
#: wrong here and so does a tapered one.
SEAT_HALF_WIDTH: tuple[tuple[float, float], ...] = (
    (0.00, 0.655),
    (0.18, 0.760),
    (0.36, 1.000),
    (0.52, 0.290),
    (0.75, 0.680),
    (1.00, 1.000),
)
"""Below the hip the entries are absolute fractions of the hip half-width; from the
hip up they are a **fraction of the way from the hip width to the shoulder width**,
so the top entry is 1.000 by construction and the shoulder number lives in
`params.seat_width_shoulders` where the source citation is. See
`_seat_half_width` -- writing 1.105 here instead would be 368/333 restated as a
literal, which is the two-copies-of-one-number failure this file has three other
notes about."""

#: How much is cut off the **right** flank's half-width, in meters, against the same
#: normalized arc length. Zero everywhere else, and the left flank never sees it.
#:
#: **This is the chain relief, and it is forced rather than styled.** Wave 3 measured
#: the driveline through the corrected shell -- 90 triangle pairs on
#: `drive_output_shaft`, 60 on `drive_chain_guard`, 50 on `drive_chain`, 48 on
#: `drive_output_sprocket` -- and the arithmetic says no lateral lane exists for the
#: chain to run in:
#:
#:     shell's right edge at the driveline's height      x 173..179
#:     `engine_clutch_cover`'s inboard face              x 182
#:     free window between them                          6.8 mm
#:     what has to fit in it: a 9 mm chain band inside
#:     a 32 mm Art. 5.9 guard                            32 mm
#:
#: So the chain cannot pass outboard of the shell whatever `chain_x` is: the engine's
#: own clutch cover caps it at 182 and the shell reaches 179. It cannot pass inboard
#: either -- that is the driver. It passes **through** the shell's right flank, which
#: is why real KZ shells are sold handed with this flank relieved, and this table is
#: that relief rather than a decision anybody here made.
#:
#: The depth is set by the innermost driveline part in the seat's own y band, which is
#: `drive_output_shaft`'s inboard end at x 100 and the guard's inboard wall at 101 --
#: not by the chain at 110.5. 92 mm of half-width leaves 8 mm to both.
#:
#: The band ends at t 0.84 on purpose: `SEAT_PAD_UPPER` is at z 300, i.e. t ~0.87, and
#: a relief that reached it would take away the fiberglass Art. 4.2.3's fourth seat
#: support lands on. Below it ends at t 0.44, which is above the hip -- the pan and
#: the hip roll are full width on both sides.
SEAT_CHAIN_RELIEF: tuple[tuple[float, float], ...] = (
    (0.00, 0.000),
    (0.44, 0.000),
    (0.50, 0.040),
    (0.56, 0.083),
    (0.76, 0.087),
    (0.84, 0.000),
    (1.00, 0.000),
)

#: How far the wing edge stands proud of the shell's spine, in meters, against
#: the same normalized arc length. Deepest just above the hip, which is where a
#: kart seat actually grips the driver, and shallow at the top.
SEAT_WING_FLARE: tuple[tuple[float, float], ...] = (
    (0.00, 0.022),
    (0.18, 0.040),
    (0.36, 0.082),
    (0.52, 0.090),
    (0.78, 0.062),
    (1.00, 0.028),
)

#: Half of one lateral cross-section of the shell, as (fraction of half-width,
#: fraction of wing flare). Nearly flat through the middle and turning hard up at
#: the edge, which is what makes the free edge a lip rather than a taper.
SEAT_SECTION: tuple[tuple[float, float], ...] = (
    (0.000, 0.000),
    (0.300, 0.006),
    (0.560, 0.042),
    (0.740, 0.135),
    (0.865, 0.300),
    (0.945, 0.520),
    (0.988, 0.760),
    (1.000, 1.000),
)

#: The four points Art. 4.2.3's *"seat with four seat supports"* has to reach, on
#: the kart's right; the left pair is mirrored. **These are published as empties**
#: (`seat_ear_upper_r` and friends) so that `frame.py` stops carrying its own
#: constants read off this loft.
#:
#: That was the whole reason the rear stays could not be closed better than
#: 17.67 mm: the shell's outer edge is a *sampled surface*, and a constant authored
#: in a second module will always miss it by a few millimeters and never say so --
#: the same failure shape as `Dictionary.get(key, default)`. The upper pad's z 300
#: is on the back's outer face at y -337.7, and x 140 is 44 mm inboard of the
#: shoulder half-width, which is where the visible discs sit in every photograph.
SEAT_PAD_UPPER: tuple[float, float, float] = (0.205, -0.338, 0.300)
SEAT_PAD_LOWER: tuple[float, float, float] = (0.196, -0.215, 0.070)

#: **Deviation from spec §40.3, and it is the loft that decides.** §40.3 publishes
#: the pads at x ±140 and ±150, and both are *inside* this shell: the corrected
#: `seat_width_shoulders` puts the external half-width at 184 at the top of the back
#: and the hip section is 166.5 plus its wing, so a stay ending at 140 ends inside
#: the fiberglass and a bracket to it is buried. The two x values above are the
#: nearest station outboard of the sampled surface, measured on the loft rather
#: than authored -- which is the same argument that moved these points out of
#: `frame.py` in the first place. `frame.SEAT_EAR_*` reads the same numbers.

#: Art. 4.8.1: reinforcement plates supporting the upper part of the seat, minimum
#: 1.5 mm thick, minimum 13 cm2, minimum Ø40. Ø45 gives 15.9 cm2 and 1.6 mm is one
#: gauge over the floor, so both minima are cleared rather than met.
SEAT_PLATE_DIAMETER = 0.045
SEAT_PLATE_THICKNESS = 0.0016

#: Steering wheel rim centerline, in units of half the wheel's width, for the
#: right half from the top center round to the bottom center. `_wheel_outline`
#: scales it so the built rim's overall width is exactly `wheel_diameter`, which
#: makes the widest control point 1.000 by definition.
#:
#: The two features that have to read from the cockpit camera are the dip at the
#: top between the horns and the chord across the bottom. **The dip was measured
#: and it was too shallow to see**: 13.3 mm on a 293 mm rim, 4.5% of the width,
#: which subtends 1.4 degrees at the ~0.55 m the driver's eye sits from the rim
#: and is why the M2 turntable read as a plain three-spoke wheel. It is 39.8 mm
#: here, 12.5% of the width, which is what a real KZ butterfly rim runs and what
#: separates it from a round wheel at a glance.
#:
#: Art. 4.5.1 permits the shape: *"The upper and lower thirds of the circumference
#: may be straight or of a different radius to the rest of the wheel"*, and the rim
#: is continuous with no obtuse angles, which is the rest of that article.
#:
#: The horn tips reach 169.8 mm from the wheel center against a half-width of
#: 159.8, so a butterfly rim is *not* contained by a circle of `wheel_diameter`.
#: That is the shape being what it is rather than a mistake, and it is written
#: down because "diameter" invites the opposite assumption.
WHEEL_OUTLINE: tuple[tuple[float, float], ...] = (
    (0.000, 0.610),
    (0.150, 0.672),
    (0.320, 0.820),
    (0.470, 0.880),
    (0.640, 0.836),
    (0.820, 0.688),
    (0.940, 0.440),
    (1.000, 0.150),
    (0.985, -0.140),
    (0.880, -0.390),
    (0.700, -0.525),
    (0.460, -0.588),
    (0.220, -0.606),
    (0.000, -0.610),
)

#: Where the three spokes meet the rim, as an angle in the wheel plane measured
#: from the wheel's own +X (right) toward +Y (up). Two up at the shoulders of the
#: butterfly and one straight down to the flat bottom.
WHEEL_SPOKE_ANGLES: tuple[float, float, float] = (
    math.radians(25.0),
    math.radians(155.0),
    math.radians(270.0),
)

#: Spoke plate thickness, and its width at the boss and at the rim.
WHEEL_SPOKE_THICKNESS = 0.008
WHEEL_SPOKE_WIDTH_INNER = 0.032
WHEEL_SPOKE_WIDTH_OUTER = 0.020

#: How far forward of the rim plane the boss sits — the wheel's dish.
#:
#: **0.015, and it was 0.048.** The dish and the *hub stack* are two different
#: things and 48 conflated them: `_steering_column` computed the column's upper end
#: as `wheel_center - axis * WHEEL_DISH`, so a dish 33 mm too large silently removed
#: 33 mm of column. The stack is `params.hub_stack` = 25 and lives between the
#: column's top and the rim plane; this is the rim plane's own offset from the boss
#: face, and kart wheels are close to flat. 15 mm is what clears the hub bolt heads.
#: Not separable from the stack in a side view at 2.9 mm/px, so `estimated`.
WHEEL_DISH = 0.015

#: Boss flange radius. Has to be visible: a kart boss is a large bolted disc.
WHEEL_BOSS_RADIUS = 0.038

#: The lower bearing bush: bore, outside diameter, length along the column axis.
#: `estimated` except the length, which equals the 15 mm journal it captures. A
#: 10 mm journal in a molded bush wants 1 mm of wall, which is the least that
#: molds; the part is never visible in any photograph in the repo.
#:
#: It is a **bush in a welded bracket**, not a collar on the tube: Art. 4.5.2 wants
#: *"a bracket and an articulated joint […] a safety clip system for the lower
#: bearing restraint nut"*, and the column has to turn here.
COLUMN_BEARING = (0.012, 0.024, 0.015)

#: The upper support block. Bore Ø20.0 and length 31 are both `sourced` -- "Tony
#: Kart OTK Nylon Support for Steering Column 20mm", Birel ART "NYLON SUPPORT
#: STEERING COLUMN L31" -- and the 40 x 36 outer size is `estimated` to a 20 mm bore
#: with two bolt ears. Nylon, so a bushing and not a rolling bearing.
#:
#: **There are two column supports and this project had collapsed them into one.**
#: Art. 4.5.2 says so itself: *"two collars between the column brackets"*, plural.
#: This is the visible one, and Art. 9.5.3 makes it structural -- the front panel's
#: upper part *"must be securely attached to the steering column support with one or
#: more independent bars."*
COLUMN_UPPER_BLOCK = (0.040, 0.048, 0.031)
COLUMN_UPPER_STATION = 0.366

#: The steering hub and the inclined wedge between it and the wheel.
#:
#: Six holes is `sourced` (OTK "STEER.WHEEL HUB - 6 HOLE"), aluminium is `sourced`
#: ("AL KZ STEERING WHEEL'S HUB"), and the Ø60 flange is `estimated` -- 6 holes on a
#: Ø46 pitch circle with 7 mm of edge land. The wedge's included angle is
#: `params.wheel_incline_delta`, its taper is `60 x tan 7 = 7.36` mm across the
#: face, and 4.0 mm at the thin edge is the least that carries an M6 through-bolt.
#:
#: **The thick edge is UP.** The wheel has to lay *back*, so the extra material is
#: at the top; getting it 180 degrees wrong stands the wheel 7 degrees more upright
#: than its column, which is the same magnitude of error with the opposite sign.
HUB_FLANGE_DIAMETER = 0.060
HUB_BOLT_CIRCLE = 0.046
HUB_BOLT_COUNT = 6
WEDGE_THIN = 0.004

#: The pitman plate, clamped to the column, that `wheels.py`'s inner rod ends bolt
#: to. Its hub is 64 mm long x Ø30 with its lower face 69 mm above the threaded tip,
#: `derived` off the dark-mask profile of the Birel render (305->437 px).
#:
#: The 50 mm ear offset is `sourced` and belongs to §Running gear -- OTK's "38/50"
#: designation is *"the centre distance of the holes of the steering unibol from the
#: centre of the column"* and a KZ runs the outer hole. `wheels.PITMAN_EAR` is
#: (0.050, 0.431, 0.160), and the column's own line passes through
#: (0, 0.431, 0.1603) at that station: the ear is **on** the column's plane to
#: 0.3 mm, which is what makes this plate buildable rather than fitted.
PITMAN_HUB = (0.030, 0.069, 0.133)
PITMAN_PLATE = (0.058, 0.020, 0.008)

#: Clutch lever, **clamped to the column** rather than bolted to the spoke plate.
#:
#: Two families are sold and this kart carries the first: OTK **0113.A0KIT**
#: *"Forged clutch lever Kit, KZ"*, a two-bolt clamp round a tube plus a large closed
#: D-loop grip, whose parts list is a support, an extension, a pin, two bushes and a
#: D5 Seeger (`sourced`, kartshop's OTK gear-lever-system category). The alternative
#: is the plain 0113.00 lever mounted by the shifter. On the **left**, because the
#: right hand is busy with the gear lever and OTK's forged kit exists because that is
#: where KZ drivers put it.
#:
#: The clamp is 55 mm below the hub clamp face on the column axis, which clears
#: `steering_bearing_upper` by 18.5 mm along the axis after both half-lengths. The
#: loop sits ~70 mm behind the rim plane and that is **not** a choice: the clamp is
#: 79 mm down the column and the column is 7 degrees off the wheel's normal, so any
#: column-clamped lever is behind the rim by about that much.
CLUTCH_CLAMP_STATION = 0.413
CLUTCH_LOOP = (0.095, 0.055, 0.014)
CLUTCH_LOOP_X = -0.108

#: The pedal arm's section at the pivot boss, tapering toward the foot bar, and the
#: brake pushrod clevis's height up the arm. `estimated` from 0014.DC's proportions;
#: the clevis height is `derived` -- the slotted plate's centre reads 160 px above
#: the pivot bush on a 510 px pivot-to-bar span, so 160/510 x 180 = **56**, which
#: also *is* the brake pedal's **3.2 : 1** ratio (510/160 = 180/56).
PEDAL_ARM_SECTION = (0.022, 0.008)
PEDAL_ARM_TIP_SECTION = (0.016, 0.006)
PEDAL_CLEVIS_RISE = 0.056

#: Pedal pivot cross tube: diameter and half-length, and where its mounting plates
#: sit laterally. The plates are outboard of both pedals (±85) and inboard of the
#: tube's ends (±140) so the arms swing free; `PEDAL_MOUNT_BORE_X` is where the
#: plate's bore has to straddle the frame to touch it at 0 mm.
#:
#: **That last number is the whole content of the 104.97 mm gate-2 failure.** The
#: mounts used to aim at `(0, front_axle_y, rail_z + 0.025)`, a straight cross member
#: at the front axle line, and there is not one: spec §10.1 measures the front of a
#: CRG chassis as a U-loop plus two stub-axle fixations, so `chassis_cross_front` runs
#: y +500..+760 out at x ±110..±304 and the brackets were reaching into empty air. The
#: loop's leg centreline passes (±259, +560, +50), so a plate whose bore straddles
#: x ±259 at z +50 contacts the tube.
PEDAL_TUBE_DIAMETER = 0.016
PEDAL_TUBE_HALF_SPAN = 0.140
PEDAL_MOUNT_X = 0.125
PEDAL_MOUNT_BORE_X = 0.259
PEDAL_MOUNT_BORE_Y = 0.560

#: Clearance held between a cockpit part and the surface of any frame tube it
#: bolts to. Small but non-zero: the real bolted joint touches, and issue #13's
#: third acceptance criterion is that nothing intersects the frame.
#:
#: Worth recording for whoever measures next: `BVHTree.FromObject(obj, depsgraph)`
#: builds its tree in the object's **local** space in Blender 5.2, not world
#: space. Overlapping two of them directly compares two parts as if both sat at
#: the origin, which reports the seat as intersecting all four tires. Build from
#: world-space triangles with `BVHTree.FromPolygons` instead.
FRAME_CLEARANCE = 0.005

#: The gear lever, and **there is no shift gate.** `_shifter` used to build a
#: slotted plate "which is what says sequential rather than a stick"; a KZ lever
#: rotates about its own rod's axis in two nylon bushes and the sequential detent is
#: inside the gearbox. None of the five OTK parts that make up the assembly has a
#: gate. It was the one piece of this assembly that was invented rather than
#: measured, and it is gone.
#:
#: The mechanism comes from the part shapes: **0111.B0** is one long thin rod, a
#: shoulder, then a thicker tube kinked away, with the rod's lower end *serrated*;
#: **0111.002** is the nylon bush; **0111.B0A** is a flat forged arm with a serrated
#: collet clamp at one end and a plain joint hole at the other; **0114.BA** is the
#: 530 mm tie-rod. So the lever turns about its own rod, the kink carries the knob
#: off that axis, and the connector arm below the bracket swings the rod
#: fore-and-aft. The serrated collet is what lets the arm be set to the `sourced`
#: 90 degrees against the rod.
#:
#: The split between rod and hand tube is the photo's own length ratio, 341:301 px,
#: applied to a total the *ergonomics* fixed -- and that is the cross-check rather
#: than an input: feeding a 449 mm total back through the pixel ratios yields a
#: 13 mm rod, a 20 mm tube and a Ø28 x 47 knob, every one a sane real number for a
#: hand lever and none of them chosen. `notes_controls`' 265/235 split does not
#: close on the authored knob and pivot -- it puts the knob 443.7 mm from the rod's
#: end where the geometry needs 398.4.
SHIFT_ROD_DIAMETER = 0.013
SHIFT_TUBE_DIAMETER = 0.020
SHIFT_RATIO = 0.5312
SHIFTER_KNOB_DIAMETER = 0.028
SHIFTER_KNOB_LENGTH = 0.047
SHIFTER_BUSH = (0.013, 0.020, 0.050)
#: The bracket that clamps the gear lever's bushes to the lower side bumper.
#:
#: `SHIFT_BRACKET_PLATE_OFFSET` is where the plate's outboard face sits relative to the
#: pivot -- the same +11 mm the plate box is built at, named once instead of twice.
#: The strap is a Ø14 bar and the collar an 8 mm-walled sleeve on the bumper's own
#: Ø20, both `estimated`: no homologation form dimensions a gear-lever bracket, and
#: what the article does fix is the tube it grips. See `_shifter_clamp`.
SHIFT_BRACKET_PLATE_OFFSET = 0.011
SHIFT_BRACKET_STRAP_DIAMETER = 0.014
SHIFT_BRACKET_COLLAR_WIDTH = 0.030
SHIFT_BRACKET_COLLAR_WALL = 0.004

SHIFTER_ARM_LENGTH = 0.055
SHIFTER_ARM_SECTION = (0.020, 0.006)
SHIFT_ROD_END_DIAMETER = 0.022
SHIFT_SELECTOR_JOINT = (0.215, -0.205, 0.100)

#: The fuel tank's mandated position is `params.tank_center_*`; these are its shape.
#:
#: **The notch is geometry, not a joint.** Art. 4.7 mandates the position -- between
#: the main tubes, ahead of the seat, behind the front wheel axis -- and that
#: position drives the steering column through the tank's top-front corner: the
#: column's height along its axis is `z(y) = 97 + 1.3763 (477 - y)`, which is 271.8
#: at the tank's front face y 350 and crosses the tank's top plane z 299 at y 330.2.
#: So the interference is 20 mm of y and 27 mm of z, and a real molding is *"waisted
#: at the bottom front to clear the steering column and the shins"* (`sourced` as a
#: shape, the 0073.EA photo). Cutting it is the honest build; declaring a joint would
#: *permit* the interpenetration gate 1 exists to catch.
TANK_NOTCH_WIDTH = 0.070
TANK_FILLER_DIAMETER = 0.060
TANK_FILLER_HEIGHT = 0.025
TANK_STRAP_Y = (0.282, 0.196)
TANK_STRAP_SECTION = (0.025, 0.002)
TANK_FITTING_DIAMETER = 0.008
FUEL_LINE_DIAMETER = 0.008


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("interior")

    # One root for the whole cockpit group. The glTF exporter flattens
    # collections, so a single parent is the only thing that makes the interior
    # *one node* — which is what ADR-0025's visibility range needs in order to
    # cull it from the chase camera at all.
    root = build.empty("cockpit_root", (0.0, 0.0, 0.0), collection, size=0.10)

    _seat(context, collection, root)
    _steering(context, collection, root)
    _pedals(context, collection, root)
    _shifter(context, collection, root)
    _fuel_tank(context, collection, root)


# --- curve and surface helpers ---------------------------------------------
#
# `build.py` covers tubes, lathes and boxes, which is the whole of the frame and
# the powertrain. A seat shell and a butterfly rim are neither, so the two
# primitives they need live here: a spline sampler and a lofted shell.


def _catmull_rom(
    points: list[Vector],
    per_segment: int,
    *,
    closed: bool = False,
) -> list[Vector]:
    """Sample a uniform Catmull-Rom spline that passes through every point.

    Through, not near: the control points are real dimensions — `seat_z`, the top
    of the back, the rim's widest point — so a spline that only approximates them
    would quietly stop honoring the parameter block. End tangents are clamped on
    an open curve and wrapped on a closed one.
    """
    count = len(points)
    if count < 3 or per_segment < 1:
        return list(points) if not closed else list(points)

    def at(index: int) -> Vector:
        if closed:
            return points[index % count]
        return points[min(max(index, 0), count - 1)]

    last = count if closed else count - 1
    sampled: list[Vector] = []
    for segment in range(last):
        p0, p1, p2, p3 = at(segment - 1), at(segment), at(segment + 1), at(segment + 2)
        for step in range(per_segment):
            t = step / per_segment
            t2 = t * t
            t3 = t2 * t
            sampled.append(
                0.5
                * (
                    2.0 * p1
                    + (p2 - p0) * t
                    + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                    + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
                )
            )
    if not closed:
        sampled.append(points[-1])
    return sampled


def _resample(path: list[Vector], count: int) -> list[Vector]:
    """`count` points spaced evenly by arc length, with both endpoints kept.

    The seat's spine comes out of `build.fillet` with all its density in the
    bends and none along the back, and the cross-section varies over that whole
    length. Even spacing is what stops the back of the seat from being one long
    ruled quad.
    """
    lengths = [0.0]
    for index in range(1, len(path)):
        lengths.append(lengths[-1] + (path[index] - path[index - 1]).length)
    total = lengths[-1]
    if total < 1e-9 or count < 2:
        return list(path)

    resampled: list[Vector] = []
    cursor = 1
    for step in range(count):
        target = total * step / (count - 1)
        while cursor < len(path) - 1 and lengths[cursor] < target:
            cursor += 1
        span = lengths[cursor] - lengths[cursor - 1]
        fraction = 0.0 if span < 1e-12 else (target - lengths[cursor - 1]) / span
        resampled.append(path[cursor - 1].lerp(path[cursor], fraction))
    return resampled


def _table(table: tuple[tuple[float, float], ...], t: float) -> float:
    """Linear lookup into an ascending (position, value) table."""
    if t <= table[0][0]:
        return table[0][1]
    for index in range(1, len(table)):
        left_t, left_v = table[index - 1]
        right_t, right_v = table[index]
        if t <= right_t:
            span = right_t - left_t
            if span < 1e-12:
                return right_v
            return left_v + (right_v - left_v) * (t - left_t) / span
    return table[-1][1]


def _grid_normals(grid: list[list[Vector]]) -> list[list[Vector]]:
    """Surface normal at every point of a lofted grid, by central difference.

    Rows run along the spine from front to back and columns from the kart's left
    to its right, so `du x dv` points at the driver everywhere on the shell —
    including out around the rolled wing edge, where it correctly turns outboard.
    That consistent sign is what lets one offset direction thicken the whole
    shell.
    """
    rows = len(grid)
    columns = len(grid[0])
    normals: list[list[Vector]] = []
    for u in range(rows):
        row: list[Vector] = []
        for v in range(columns):
            if u == 0:
                du = grid[1][v] - grid[0][v]
            elif u == rows - 1:
                du = grid[rows - 1][v] - grid[rows - 2][v]
            else:
                du = grid[u + 1][v] - grid[u - 1][v]
            if v == 0:
                dv = grid[u][1] - grid[u][0]
            elif v == columns - 1:
                dv = grid[u][columns - 1] - grid[u][columns - 2]
            else:
                dv = grid[u][v + 1] - grid[u][v - 1]
            normal = du.cross(dv)
            row.append(normal.normalized() if normal.length > 1e-12 else Vector((0.0, 0.0, 1.0)))
        normals.append(row)
    return normals


def _shell(bm: bmesh.types.BMesh, grid: list[list[Vector]], thickness: float) -> None:
    """A lofted surface, its offset copy, and the band that closes the rim.

    The band is the visible payoff: it is `thickness` of real geometry around
    every free edge, so `build.bevel_object` has something to chamfer and the
    edge catches a highlight the way a fiberglass lip does.
    """
    rows = len(grid)
    columns = len(grid[0])
    normals = _grid_normals(grid)

    outer = [
        [bm.verts.new(grid[u][v]) for v in range(columns)]
        for u in range(rows)
    ]
    inner = [
        [bm.verts.new(grid[u][v] + normals[u][v] * thickness) for v in range(columns)]
        for u in range(rows)
    ]

    for u in range(rows - 1):
        for v in range(columns - 1):
            bm.faces.new(
                (outer[u][v], outer[u][v + 1], outer[u + 1][v + 1], outer[u + 1][v])
            )
            bm.faces.new(
                (inner[u][v], inner[u + 1][v], inner[u + 1][v + 1], inner[u][v + 1])
            )

    # Perimeter in a fixed order, so the rim band's vertex order is a function of
    # the grid size alone.
    perimeter: list[tuple[int, int]] = []
    perimeter.extend((0, v) for v in range(columns))
    perimeter.extend((u, columns - 1) for u in range(1, rows))
    perimeter.extend((rows - 1, v) for v in range(columns - 2, -1, -1))
    perimeter.extend((u, 0) for u in range(rows - 2, 0, -1))

    for index in range(len(perimeter)):
        u0, v0 = perimeter[index]
        u1, v1 = perimeter[(index + 1) % len(perimeter)]
        bm.faces.new((outer[u0][v0], outer[u1][v1], inner[u1][v1], inner[u0][v0]))

    # The shell is a closed manifold, so one recalculation gives every face the
    # outward normal and the band's winding does not have to be reasoned about.
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))


def _extruded_polygon(
    bm: bmesh.types.BMesh,
    outline: list[tuple[float, float]],
    origin: Vector,
    axis_a: Vector,
    axis_b: Vector,
    normal: Vector,
    thickness: float,
) -> None:
    """A flat plate: a closed 2D outline given thickness along `normal`.

    Every flat part in the cockpit — pedal arms, wheel spokes, the shifter's
    mounting plate, the clutch blade — is a plate cut from sheet and bent, so one
    primitive covers all of them and each is authored as an outline in the plane
    it actually lies in.
    """
    points = list(outline)
    # Fix the winding from the signed area rather than trusting the author, so
    # the front face always looks along +normal.
    area = 0.0
    for index in range(len(points)):
        a0, b0 = points[index]
        a1, b1 = points[(index + 1) % len(points)]
        area += a0 * b1 - a1 * b0
    if area < 0.0:
        points.reverse()

    half = normal * (thickness * 0.5)
    front = [
        bm.verts.new(origin + axis_a * a + axis_b * b + half) for a, b in points
    ]
    back = [
        bm.verts.new(origin + axis_a * a + axis_b * b - half) for a, b in points
    ]

    bm.faces.new(tuple(front))
    bm.faces.new(tuple(reversed(back)))
    for index in range(len(points)):
        following = (index + 1) % len(points)
        bm.faces.new((front[index], back[index], back[following], front[following]))


def _sweep_closed_planar(
    bm: bmesh.types.BMesh,
    path: list[Vector],
    plane_normal: Vector,
    radius: float,
    segments: int,
) -> None:
    """Sweep a circle along a closed *planar* path, with no seam.

    `build.sweep_tube` cannot do this: it caps both ends and its parallel
    transport gives the first and last ring slightly different frames, which on a
    closed loop shows up as a visible kink where the rim meets itself. For a
    planar curve the correct frame is exact rather than transported — the plane
    normal is one basis vector everywhere — so the loop closes on itself.
    """
    count = len(path)
    if count < 3 or segments < 3:
        return

    rings: list[list[bmesh.types.BMVert]] = []
    for index in range(count):
        tangent = (path[(index + 1) % count] - path[(index - 1) % count]).normalized()
        # `tangent x plane_normal`, not the other way round. The ring's two basis
        # vectors and the sweep direction have to form a right-handed frame, the
        # same way `build.sweep_tube` pairs its normal with `tangent.cross(normal)`
        # — otherwise the face loop below, which is copied from that function,
        # winds the whole rim inward. It did, and `genkart.check_face_winding`
        # is what caught it: the rim enclosed -0.000385 m3.
        across = tangent.cross(plane_normal).normalized()
        ring: list[bmesh.types.BMVert] = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            offset = plane_normal * (math.cos(angle) * radius) + across * (
                math.sin(angle) * radius
            )
            ring.append(bm.verts.new(path[index] + offset))
        rings.append(ring)

    for index in range(count):
        lower = rings[index]
        upper = rings[(index + 1) % count]
        for step in range(segments):
            following = (step + 1) % segments
            bm.faces.new((lower[step], lower[following], upper[following], upper[step]))


# --- the seat --------------------------------------------------------------


def _seat_spine(p: P.KartParams) -> list[tuple[float, float, float]]:
    """Control polyline of the shell's underside, in the kart's centerline plane.

    Four points, front to back, and every one of them is a parameter:

        the pan's front lip, `SEAT_PAN_FRONT_RISE` above the pan
        the front of the level stretch of pan
        the hip point, at `seat_y` and at `seat_z` exactly
        the top of the back, `params.seat_back_top`

    `seat_y` is read as the hip point rather than as the middle of the bounding
    box, which is what makes params.py's claim about it true: the driver's mass
    sits at the hip, and ARCHITECTURE.md §6 wants the center of mass slightly
    rearward. Moving `seat_y` moves the driver, and the physics center of mass
    with it.

    The rake is `seat_shell_rake` -- the fiberglass chord's own 22 degrees -- and no
    longer `seat_back_angle`, which was doing double duty for the shell and for the
    driver's torso recline and which the radiator's rake was *added to*.

    The bend at the hip is filleted at `SEAT_HIP_RADIUS`, which leaves the level
    stretch of pan tangent to the arc — so the shell's lowest point is still
    exactly `seat_z` after filleting rather than a few millimeters above it.
    """
    hip_y = p.seat_y
    return [
        (0.0, hip_y + SEAT_PAN_LENGTH, p.seat_z + SEAT_PAN_FRONT_RISE),
        (0.0, hip_y + SEAT_PAN_FLAT, p.seat_z),
        (0.0, hip_y, p.seat_z),
        P.seat_back_top(p),
    ]


def _seat(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The fiberglass shell, lofted from a spine and a lateral cross-section, plus
    the four brackets Art. 4.2.3's *"seat with four seat supports"* requires.

    The right flank carries `SEAT_CHAIN_RELIEF` and the left does not, so this is a
    **handed** shell. That is not a shortcut: see the table's own note for the 6.8 mm
    lateral window between the shell's edge and the clutch cover that makes it forced.
    """
    p = context.params
    detail = context.detail

    # `seat_root` is at the hip point, not at the shell's bounding-box center:
    # it is the handle the physics reads for where the driver's mass sits, and a
    # pivot in the middle of a bounding box means nothing to anybody.
    seat_root = build.empty(
        "seat_root", (0.0, p.seat_y, p.seat_z), collection, size=0.08
    )
    context.publish("seat_root", seat_root)

    spine = build.fillet(_seat_spine(p), SEAT_HIP_RADIUS, detail.bend_segments)
    stations = _resample(spine, 33 if detail.is_high else 15)

    section = _catmull_rom(
        [Vector(point) for point in SEAT_SECTION],
        3 if detail.is_high else 1,
    )
    # Mirror the authored half about the centerline. Columns run left to right so
    # that `_grid_normals` gets a consistent sign out of du x dv.
    lateral = [Vector((-point.x, point.y)) for point in reversed(section[1:])]
    lateral.extend(section)

    right = Vector((1.0, 0.0, 0.0))
    grid: list[list[Vector]] = []
    for index, position in enumerate(stations):
        t = index / (len(stations) - 1)
        if index == 0:
            tangent = stations[1] - stations[0]
        elif index == len(stations) - 1:
            tangent = stations[-1] - stations[-2]
        else:
            tangent = stations[index + 1] - stations[index - 1]
        tangent.normalize()
        # Perpendicular to the spine, in the centerline plane, pointing at the
        # driver. The wings flare along this, which is what makes them wrap the
        # driver instead of just standing up vertically off the back.
        normal = tangent.cross(right).normalized()

        half_width = _seat_half_width(p, t)
        relieved = max(0.030, half_width - _table(SEAT_CHAIN_RELIEF, t))
        flare = _table(SEAT_WING_FLARE, t)
        # Asymmetric on purpose: `SEAT_CHAIN_RELIEF` is the handed cutaway the chain
        # runs through and it applies to the kart's right only. `point.x` is negative
        # on the left half of the mirrored section, so the sign is the side.
        grid.append(
            [
                position
                + right * ((relieved if point.x > 0.0 else half_width) * point.x)
                + normal * (flare * point.y)
                for point in lateral
            ]
        )

    bm = bmesh.new()
    _shell(bm, grid, p.seat_thickness)
    shell = build.object_from_bmesh(
        "seat_shell",
        bm,
        collection,
        material=context.material("seat_fiberglass"),
        shade_smooth=True,
    )
    build.bevel_object(shell, detail)
    build.set_parent(shell, seat_root)

    _seat_brackets(context, collection, seat_root, grid)
    build.set_parent(seat_root, root)


def _seat_half_width(p: P.KartParams, t: float) -> float:
    """Half-width of the shell at normalized arc length `t` along the spine.

    Two regimes, because a real shell is 35 mm **wider** at the shoulders than at the
    hips and the old single table tapered the other way -- to 0.812 of the hip width,
    building a shell 268 mm across the shoulders where Tillett B puts the part at 368.

    Below the hip station the table is a plain fraction of the hip half-width. Above
    it the table interpolates from `seat_width` to `seat_width_shoulders`, so both
    sourced dimensions are read and neither is restated as a ratio.
    """
    hip = p.seat_width * 0.5
    shoulder = p.seat_width_shoulders * 0.5
    fraction = _table(SEAT_HALF_WIDTH, t)
    if t <= 0.36:
        return hip * fraction
    return hip + (shoulder - hip) * fraction


def _nearest_on_grid(grid: list[list[Vector]], target: Vector) -> Vector:
    """The sampled shell point closest to `target`.

    This is the whole point of publishing the ears from here rather than letting
    `frame.py` carry constants: the shell's flank is a *sampled surface*, so the only
    honest answer to "where does the strut land" is a search over the samples that
    were actually built. A number authored elsewhere misses by a few millimeters and
    never says so.

    Deterministic by construction -- the grid is emitted in a fixed order and ties
    resolve to the first index.
    """
    best = grid[0][0]
    best_distance = (best - target).length
    for row in grid:
        for point in row:
            distance = (point - target).length
            if distance < best_distance:
                best, best_distance = point, distance
    return best


def _seat_brackets(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    parent: bpy.types.Object,
    grid: list[list[Vector]],
) -> None:
    """Four pads, four straps, four published empties.

    Art. **4.2.3** lists *"seat with four seat supports"* among the chassis auxiliary
    parts, so four is a number and not a style choice, and Art. **4.8.1** dimensions
    the reinforcement plates: minimum 1.5 mm thick, 13 cm2 and Ø40. Art. **4.8.2**
    requires every stay bolted at each end.

    The kart had **two** stays and no brackets at all, which is why the shell's
    nearest neighbour of any kind was the floor tray at 7.31 mm and its rear stays
    were 78 mm away.
    """
    steel = context.material("frame_powdercoat")
    for label, pad in (("upper", SEAT_PAD_UPPER), ("lower", SEAT_PAD_LOWER)):
        for side, sign in (("r", 1.0), ("l", -1.0)):
            target = Vector((sign * pad[0], pad[1], pad[2]))
            anchor = _nearest_on_grid(grid, target)
            empty = build.empty(
                "seat_ear_%s_%s" % (label, side), tuple(target), collection, size=0.03
            )
            context.publish("seat_ear_%s_%s" % (label, side), empty)
            build.set_parent(empty, parent)

            bm = bmesh.new()
            # The reinforcement plate, flat against the shell, then a strap out to
            # the stay's own end point.
            plate_normal = (target - anchor)
            if plate_normal.length < 1e-6:
                plate_normal = Vector((sign, 0.0, 0.0))
            plate_normal.normalize()
            build.sweep_tube(
                bm,
                [
                    anchor - plate_normal * 0.0004,
                    anchor + plate_normal * (SEAT_PLATE_THICKNESS + 0.0004),
                ],
                SEAT_PLATE_DIAMETER * 0.5,
                context.detail.tube_segments,
            )
            build.sweep_tube(
                bm,
                [anchor, target],
                0.011,
                max(6, context.detail.tube_segments // 2),
            )
            bracket = build.object_from_bmesh(
                "seat_bracket_%s_%s" % (label, side),
                bm,
                collection,
                material=steel,
                shade_smooth=True,
            )
            build.set_parent(bracket, parent)


# --- steering --------------------------------------------------------------


def _column_frame(p: P.KartParams) -> tuple[Vector, Vector, Vector, Vector, Vector]:
    """(bore, wheel center, column axis, right, up) — the one chain everything uses.

    **Authored from the welded end.** `params.lower_bore` is the bearing bracket's
    bore, the bracket is welded to the frame and cannot move, so it is the datum;
    `column_length` is a catalogue part; `column_rake` is measured on a photograph;
    and the wheel centre is *derived*. The chain closes on an independently measured
    (0, +187, +496) and the upper support's bore lands 1 mm from its own measurement.

    This used to run the other way — `steering_column_base` derived the column's
    *fixed* lower end from its *free* upper end through a hardcoded 402 mm — and
    that inversion is the whole reason #192 measured
    `chassis_steering_hoop`/`steering_bearing` at 37.46 mm: **no expression anywhere
    in the build mentioned the bracket that carries the column.** Now
    `steering_bearing`'s bore centre and the column's journal centre are the same
    expression, so their contact is identity and the only edit that can open a gap
    is an edit to the bore.

    The column lies in the kart's centerline plane, so world X is exactly
    perpendicular to it and there is no near-degenerate cross product to guard
    against. `right` and `up` span the plane perpendicular to the *column*, and
    (right, up, axis) is right-handed with `axis` pointing up and back at the driver.
    The **wheel's** plane is 7 degrees further back than that — see `_wheel_frame`.
    """
    bore = Vector(P.lower_bore(p))
    center = Vector(P.wheel_center(p))
    axis = Vector(P.column_axis(p))
    right = Vector((1.0, 0.0, 0.0))
    up = axis.cross(right)
    return bore, center, axis, right, up


def _wheel_frame(p: P.KartParams) -> tuple[Vector, Vector, Vector, Vector]:
    """(center, axis, right, up) for the **wheel's** plane, 43 degrees from vertical.

    Seven degrees more raked than the column, and that is a real part rather than a
    modeling liberty: the edge-on rim trace measures 42.9 in the side view where the
    column tube measures 35.7, OTK sells an "INCLINED STEERING WHEEL HUB" and an
    "INCLINED SPACER FOR STEERING" whose only purpose is to lay the wheel back
    further than its column, and Art. 4.5 permits *"A spacer may be used between the
    steering wheel and the hub."*

    Derived as `column_rake + wheel_incline_delta` rather than authored, for the
    reason `_column_frame` gives about authoring an angle twice.
    """
    rake = P.wheel_rake(p)
    axis = Vector((0.0, -math.sin(rake), math.cos(rake)))
    right = Vector((1.0, 0.0, 0.0))
    return Vector(P.wheel_center(p)), axis, right, axis.cross(right)


def _wheel_outline(p: P.KartParams, detail: build.Detail) -> list[Vector]:
    """The rim centerline, closed, in the wheel's own 2D plane.

    Authored as a half and mirrored, then scaled so that the finished rim's
    overall **width** — the sampled outline plus the rim's own thickness — lands
    exactly on `wheel_diameter`. Scaling to fit rather than authoring absolute
    numbers means the shape and the regulated size are two separate decisions,
    and changing one does not silently change the other.

    **Width, not radius, and that distinction was worth 27 mm.** This normalized
    on the longest control *vector* first, which on a butterfly outline is a horn
    at about 60 degrees, not the horizontal extreme. The rim it produced measured
    293.3 mm across against a `wheel_diameter` of 320 — the parameter was landing
    on a dimension nothing on the finished wheel could be measured at. A kart
    wheel is sold and quoted by its width, so the width is what the parameter has
    to mean.

    The scale is measured off a fixed dense sampling rather than off the control
    points, because a Catmull-Rom segment can bulge outside the hull of its own
    control points and the control polygon therefore understates the width. It is
    deliberately *not* `detail`'s sampling: a detail-dependent scale would make
    the low-poly and the high-poly wheel slightly different shapes, and issue
    #19's normal bake needs them to be the same kart at two densities — see
    `build.Detail`. Sampling is an affine combination of the control points, so
    scaling before sampling and scaling after are the same curve.
    """
    half = [Vector(point) for point in WHEEL_OUTLINE]
    # Top center and bottom center are on the centerline and must not be doubled.
    loop = list(half)
    loop.extend(Vector((-point.x, point.y)) for point in reversed(half[1:-1]))

    widest = max(abs(point.x) for point in _catmull_rom(loop, 16, closed=True))
    scale = (p.wheel_diameter - p.wheel_rim_thickness) * 0.5 / widest

    return _catmull_rom(
        [point * scale for point in loop], 4 if detail.is_high else 2, closed=True
    )


def _steering(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Rim, spokes, boss, hub, wedge, column, both bearings, pitman, clutch lever.

    **The interface M4 depends on, and it is measured rather than asserted.**
    `steering_pivot` is an empty at the wheel center whose local Z *is* the wheel's
    own axis, so turning the wheel is one rotation about the pivot's local Z. The
    rim, spokes, boss, hub, wedge and clutch lever are parented under it; nothing
    else is, so everything that rotates is everything a viewer expects to rotate.

    What was checked headless, because "the code says local Z" is not evidence:

    *   `steering_pivot.matrix_world`'s third column is the wheel axis to 1e-8, and
        its basis has determinant +1 and unit scale.
    *   Rotating the rim's world-space points 30 deg about the pivot's local Z
        keeps them in a slab the rim's own thickness deep, so the wheel stays in its
        plane. The same rotation about local X or local Y swells that slab to
        133.8 mm and 171.8 mm, which is the wheel tumbling out of its plane rather
        than turning in it.
    *   A rim point 169.80 mm from the axis sweeps a chord of 14.813 mm at 5 deg
        and 240.128 mm at 90 deg, matching `2 r sin(theta / 2)` to six decimals.

    Applying `export_yup`'s (x, y, z) -> (x, z, -y) to each Blender local axis
    reproduces the exported node's basis exactly: local X stays +X, local Y becomes
    **-Z**, local Z becomes **+Y**. So the runtime axis is `Vector3.UP` in the node's
    own space, and it is the exported file saying so rather than this comment.

    **Sign.** The map is a proper rotation, so handedness and therefore the sign
    carry over. Measured on the built scene: a positive rotation moves the rim
    point at the driver's right up and inboard, which is counter-clockwise as the
    driver sees it. **Positive steers left.**
    """
    p = context.params
    detail = context.detail
    center, wheel_axis, right, up = _wheel_frame(p)

    pivot = build.empty("steering_pivot", tuple(center), collection, size=0.06)
    # Set the basis before anything is parented: `build.set_parent` reads the
    # parent's world matrix to build the child's parent inverse.
    pivot.matrix_world = Matrix.Translation(center) @ Matrix(
        (right, up, wheel_axis)
    ).transposed().to_4x4()
    context.publish("steering_pivot", pivot)

    outline = _wheel_outline(p, detail)
    rim_path = [center + right * point.x + up * point.y for point in outline]

    bm = bmesh.new()
    _sweep_closed_planar(
        bm, rim_path, wheel_axis, p.wheel_rim_thickness * 0.5, detail.tube_segments
    )
    rim = build.object_from_bmesh(
        "steering_rim",
        bm,
        collection,
        material=context.material("rubber_grip"),
        shade_smooth=True,
    )
    build.set_parent(rim, pivot)

    _wheel_spokes(context, collection, pivot, outline)
    _wheel_boss(context, collection, pivot)
    _steering_hub(context, collection, pivot)
    _clutch_lever(context, collection, pivot)
    _steering_column(context, collection, root)

    build.set_parent(pivot, root)


def _wheel_spokes(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
    outline: list[Vector],
) -> None:
    """Three flat plates from the boss out to the rim.

    Each plate's outer end is snapped to the sampled rim point nearest its target
    angle rather than to the angle itself, so a spoke always lands *on* the rim
    however the butterfly outline is retuned — including on the flat bottom,
    where the rim's radius is barely half what it is at the top.
    """
    p = context.params
    center, axis, right, up = _wheel_frame(p)
    inner_radius = WHEEL_BOSS_RADIUS * 0.85

    bm = bmesh.new()
    for angle in WHEEL_SPOKE_ANGLES:
        direction = Vector((math.cos(angle), math.sin(angle)))
        # argmin over a list in a fixed order, so ties resolve to the same index
        # on every run.
        target = min(
            range(len(outline)),
            key=lambda index: -outline[index].normalized().dot(direction),
        )
        outer = outline[target]
        radial = outer.normalized()
        length = outer.length
        # Plate axes: along the spoke, and across it in the wheel's plane.
        along = right * radial.x + up * radial.y
        across = right * (-radial.y) + up * radial.x
        _extruded_polygon(
            bm,
            [
                (inner_radius, WHEEL_SPOKE_WIDTH_INNER * 0.5),
                (length, WHEEL_SPOKE_WIDTH_OUTER * 0.5),
                (length, -WHEEL_SPOKE_WIDTH_OUTER * 0.5),
                (inner_radius, -WHEEL_SPOKE_WIDTH_INNER * 0.5),
            ],
            center,
            along,
            across,
            axis,
            WHEEL_SPOKE_THICKNESS,
        )

    spokes = build.object_from_bmesh(
        "steering_spokes", bm, collection, material=context.material("engine_alloy")
    )
    build.bevel_object(spokes, context.detail)
    build.set_parent(spokes, pivot)


def _wheel_boss(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
) -> None:
    """The dished center plate the three spokes meet, revolved about the wheel axis.

    The dish — the boss standing `WHEEL_DISH` forward of the rim plane — is what
    brings a kart wheel's rim back toward the hands, and it is very visible from the
    cockpit camera. **It is 15 mm and not 48**: see `WHEEL_DISH` for why 48 was the
    hub stack and the dish added together, and for the 33 mm of column it deleted.
    """
    p = context.params
    center, axis, right, up = _wheel_frame(p)

    # (radius, distance along the wheel axis from the wheel center). Negative is
    # forward, away from the driver.
    profile = [
        (0.0, -WHEEL_DISH),
        (0.014, -WHEEL_DISH),
        (0.014, -0.010),
        (0.019, -0.006),
        (WHEEL_BOSS_RADIUS, 0.000),
        (WHEEL_BOSS_RADIUS, 0.008),
        (0.030, 0.012),
        (0.000, 0.012),
    ]

    bm = bmesh.new()
    build.lathe(bm, profile, context.detail.tube_segments, axis="Z")
    bm.transform(
        Matrix.Translation(center) @ Matrix((right, up, axis)).transposed().to_4x4()
    )
    boss = build.object_from_bmesh(
        "steering_boss",
        bm,
        collection,
        material=context.material("engine_alloy"),
        shade_smooth=True,
    )
    build.set_parent(boss, pivot)


def _steering_hub(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
) -> None:
    """The hub that clamps the column and the inclined wedge above it.

    The wedge is the part that stops the wheel being built wrong. Its two faces are
    `params.wheel_incline_delta` apart, so the hub is square to the **column** and
    the boss is square to the **wheel** — which is the only construction in which
    the seven degrees exist as geometry rather than as two independently authored
    angles that happen to differ.

    Art. 4.5.1 requires the hub *"securely attached to the column with at least one
    M6 screw (minimum grade 8.8) and a self-locking nut"*; six holes and aluminium
    are `sourced` off OTK's own part names.
    """
    p = context.params
    top = Vector(P.column_top(p))
    column_axis = Vector(P.column_axis(p))
    center, wheel_axis, right, up = _wheel_frame(p)
    alloy = context.material("engine_alloy")

    # The hub is square to the column: a short sleeve over the column's top end,
    # `hub_stack` less the wedge's mean thickness long.
    mean_wedge = WEDGE_THIN + HUB_FLANGE_DIAMETER * math.tan(p.wheel_incline_delta) * 0.5
    hub_length = p.hub_stack - mean_wedge
    column_right = Vector((1.0, 0.0, 0.0))
    column_up = column_axis.cross(column_right)

    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (p.column_diameter * 0.5, -hub_length),
            (HUB_FLANGE_DIAMETER * 0.5, -hub_length),
            (HUB_FLANGE_DIAMETER * 0.5, 0.0),
            (p.column_diameter * 0.5, 0.0),
        ],
        context.detail.tube_segments,
        axis="Z",
    )
    bm.transform(
        Matrix.Translation(top)
        @ Matrix((column_right, column_up, column_axis)).transposed().to_4x4()
    )
    hub = build.object_from_bmesh(
        "steering_hub", bm, collection, material=alloy, shade_smooth=True
    )
    build.set_parent(hub, pivot)

    # The wedge: a disc whose two faces are `wheel_incline_delta` apart. Thick edge
    # **up** in the wheel's plane, so the wheel lays back rather than standing up.
    taper = HUB_FLANGE_DIAMETER * math.tan(p.wheel_incline_delta)
    bm = bmesh.new()
    segments = context.detail.tube_segments
    lower: list[bmesh.types.BMVert] = []
    upper: list[bmesh.types.BMVert] = []
    base = center - wheel_axis * (mean_wedge + WHEEL_DISH)
    for step in range(segments):
        angle = 2.0 * math.pi * step / segments
        offset = right * (math.cos(angle) * HUB_FLANGE_DIAMETER * 0.5) + up * (
            math.sin(angle) * HUB_FLANGE_DIAMETER * 0.5
        )
        # The lower face is square to the column, the upper to the wheel: the wedge
        # thickness therefore varies with `sin(angle)`, thickest at the top.
        thickness = WEDGE_THIN + taper * 0.5 * (1.0 + math.sin(angle))
        lower.append(bm.verts.new(base + offset))
        upper.append(bm.verts.new(base + offset + wheel_axis * thickness))
    for step in range(segments):
        following = (step + 1) % segments
        bm.faces.new((lower[step], lower[following], upper[following], upper[step]))
    bm.faces.new(list(reversed(lower)))
    bm.faces.new(upper)
    wedge = build.object_from_bmesh(
        "steering_hub_wedge", bm, collection, material=alloy
    )
    build.set_parent(wedge, pivot)


def _clutch_lever(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    pivot: bpy.types.Object,
) -> None:
    """The forged clutch lever, **clamped to the column** and turning with it.

    ARCHITECTURE.md §6.3: the clutch is used for launches and racing upshifts are
    clutchless, so the lever lives by the wheel like a motorcycle's, on the driver's
    left. Together with the hand shifter it is what makes the cockpit read as KZ.

    It used to be bolted to `steering_spokes`, and it is not a spoke part: OTK
    0113.A0KIT is a **two-bolt clamp around a tube**. So the clamp is on the column
    and `joints.py`'s `steering_clutch_lever`/`steering_spokes` row goes with it.
    """
    p = context.params
    bore, _, column_axis, _, _ = _column_frame(p)
    clamp = bore + column_axis * CLUTCH_CLAMP_STATION
    steel = context.material("frame_powdercoat")

    bm = bmesh.new()
    # The clamp, a short sleeve on the column with two bolt ears.
    column_right = Vector((1.0, 0.0, 0.0))
    column_up = column_axis.cross(column_right)
    build.sweep_tube(
        bm,
        [clamp - column_axis * 0.013, clamp + column_axis * 0.013],
        0.017,
        context.detail.tube_segments,
    )
    # The closed D-loop, out into the left hand's fingers, in the plane the fingers
    # wrap: it lies across the column, not in the wheel's plane.
    loop_center = Vector((CLUTCH_LOOP_X, clamp.y - 0.006, clamp.z + 0.006))
    ring: list[Vector] = []
    for step in range(25):
        angle = 2.0 * math.pi * step / 24.0
        ring.append(
            Vector(
                (
                    loop_center.x + math.cos(angle) * CLUTCH_LOOP[0] * 0.5,
                    loop_center.y + math.sin(angle) * CLUTCH_LOOP[1] * 0.5 * 0.35,
                    loop_center.z + math.sin(angle) * CLUTCH_LOOP[1] * 0.5,
                )
            )
        )
    build.sweep_tube(bm, ring, CLUTCH_LOOP[2] * 0.5, 8)
    # The extension between the clamp and the loop.
    build.sweep_tube(
        bm,
        [clamp - column_right * 0.014, loop_center + Vector((CLUTCH_LOOP[0] * 0.5, 0.0, 0.0))],
        0.007,
        8,
    )
    lever = build.object_from_bmesh(
        "steering_clutch_lever", bm, collection, material=steel, shade_smooth=True
    )
    build.set_parent(lever, pivot)


def _steering_column(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The column, both bearings and the pitman plate.

    Not parented to `steering_pivot`: the bearings are the parts that do *not* turn,
    and the column is rotationally symmetric so turning it would be invisible.
    Keeping all of it off the pivot means everything under the pivot is exactly what
    a viewer sees rotate.

    **Nothing here is positioned by measuring from a `build.tube` control point**,
    and that is a rule rather than a preference. `COLUMN_LOWER_CLEAR` used to lift
    the column 26 mm up-axis on the reasoning that `frame.py` puts the steering
    hoop's apex control point at the column's base and the filleted centreline
    passes within 5 mm of it. The arithmetic about an *unfilleted* tube was right and
    the conclusion was still wrong: `build.tube` cuts the apex corner and pulls the
    crown **below** the control point that was supposed to meet the column, so the
    real clearance was 19.1 mm and the lift was buying nothing while opening a gap at
    the bearing. Every position in this function is an absolute coordinate.
    """
    p = context.params
    bore, _, axis, right, up = _column_frame(p)
    frame = Matrix((right, up, axis)).transposed().to_4x4()

    tip = bore - axis * P.COLUMN_JOURNAL_OFFSET
    top = Vector(P.column_top(p))

    bm = bmesh.new()
    build.tube(bm, [tuple(tip), tuple(top)], p.column_diameter, context.detail, 0.0)
    column = build.object_from_bmesh(
        "steering_column",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
        shade_smooth=True,
    )
    build.set_parent(column, root)

    # The lower bush, **centred on the bore**. Its centre and the column's journal
    # centre are one expression, so the contact is identity.
    inner, outer, length = COLUMN_BEARING
    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (inner * 0.5, -length * 0.5),
            (outer * 0.5, -length * 0.5),
            (outer * 0.5, length * 0.5),
            (inner * 0.5, length * 0.5),
        ],
        context.detail.tube_segments,
        axis="Z",
    )
    bm.transform(Matrix.Translation(bore) @ frame)
    bearing = build.object_from_bmesh(
        "steering_bearing",
        bm,
        collection,
        material=context.material("engine_alloy"),
        shade_smooth=True,
    )
    build.set_parent(bearing, root)

    # The upper nylon block, 366 mm up the same axis: `params.steering_support_apex_*`
    # is the frame's reading of the same point and the two agree to 1 mm.
    upper = bore + axis * COLUMN_UPPER_STATION
    bm = bmesh.new()
    build.box(
        bm,
        COLUMN_UPPER_BLOCK,
        tuple(upper),
        rotation=Matrix((right, up, axis)).transposed(),
    )
    block = build.object_from_bmesh(
        "steering_bearing_upper",
        bm,
        collection,
        material=context.material("frame_powdercoat"),
    )
    build.bevel_object(block, context.detail)
    build.set_parent(block, root)

    _steering_pitman(context, collection, root, bore, axis, right, up)


def _steering_pitman(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    bore: Vector,
    axis: Vector,
    right: Vector,
    up: Vector,
) -> None:
    """The plate `wheels.py`'s inner tie-rod ends bolt to — **it did not exist.**

    That absence is the whole of `joints.py`'s 46.00 mm waiver: the rod ends were
    declared against `steering_column`, which is what a pitman is clamped to, and the
    46 mm was never slack -- it is the pitman's own reach. OTK's "38/50" designation
    puts the outer tie-rod hole 50 mm off the column axis and a KZ runs the outer
    hole, so 50 less the column's 9 mm radius less the rod end's 11 leaves 46.

    The station is not a guess either. `wheels.PITMAN_EAR` is (0.050, 0.431, 0.160)
    and the column's own line passes through (0, 0.431, 0.1603) — the ear is **on**
    the column's plane to 0.3 mm, which is what makes this plate buildable rather
    than fitted. Art. 4.5.3 permits the rose joints by name.
    """
    p = context.params
    diameter, low, high = PITMAN_HUB
    hub_low = bore - axis * P.COLUMN_JOURNAL_OFFSET + axis * low
    hub_high = bore - axis * P.COLUMN_JOURNAL_OFFSET + axis * high
    plate_center = (hub_low + hub_high) * 0.5
    half_x, half_up, thickness = PITMAN_PLATE

    bm = bmesh.new()
    build.sweep_tube(bm, [hub_low, hub_high], diameter * 0.5, context.detail.tube_segments)
    _extruded_polygon(
        bm,
        [
            (-half_x, half_up),
            (half_x, half_up),
            (half_x, -half_up),
            (-half_x, -half_up),
        ],
        plate_center,
        right,
        up,
        axis,
        thickness,
    )
    pitman = build.object_from_bmesh(
        "steering_pitman",
        bm,
        collection,
        material=context.material("engine_alloy"),
    )
    build.set_parent(pitman, root)


# --- pedals ----------------------------------------------------------------


def _pedal_frame(p: P.KartParams) -> tuple[Vector, Vector]:
    """(arm up-axis, arm face normal) for an organ pedal on a bottom pivot.

    The arm stands **up and rearward** from a pivot at the bottom, so its up-axis
    leans back by `pedal_arm_rake` and its face looks up and back at the driver's
    sole. This replaces a hanging plate whose pivot was *above* its pad, which is why
    the sign of the y term flipped in `wheels._pedal_plate_y` as well.
    """
    rake = p.pedal_arm_rake
    up = Vector((0.0, -math.sin(rake), math.cos(rake)))
    # `right x up`, so that (right, up, normal) is right-handed and
    # `_extruded_polygon` encloses a positive volume. It points rearward and down,
    # which is where the driver's sole is -- and getting the sign the other way is
    # not a modelling preference, it is an inside-out arm that no render shows.
    normal = Vector((1.0, 0.0, 0.0)).cross(up)
    return up, normal


def _pedals(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Throttle right, brake left. **Organ type, bottom pivot, transverse axis.**

    Proved from the parts rather than judged: OTK **0014.DC** (throttle) is a forged
    arm with a bushed pivot eye at the *bottom* and the foot bar at the top;
    **0014.D3** is its support plate; **0015.DC / 0015.DCA** (*"Brake pedal,
    Adjustable, KZ, New type"*) is the same family, its "adjustable" being a slotted
    plate part-way up the arm carrying the pushrod clevis at one of three heights.

    Two things about the old geometry were wrong and one of them was 138 mm:
    `pedal_z` = 0.090 put the pedal face **21 mm above the floor tray**, which is a
    foot resting on the floor and not a pedal, and `pedal_width`/`pedal_length`
    described a 70 x 120 flat plate where the part is a **Ø18 x 80 transverse round
    bar on a forged arm**. A plate is a rental-kart pedal.

    Art. 4.4 binds the assembly: pedals *"must never protrude in front of the
    chassis, including the bumper"* -- the bumper is at y >= +875 by Art. 9.4.1's
    350 mm front overhang minimum and the bar reaches y ~ +635 at full travel, so
    240 mm clear -- the accelerator needs a return spring and a mechanical link, and
    *"the brake pedal must be placed in front of the master cylinder"*, which
    `wheels.py` satisfies from the other side.

    **The interface M4 depends on.** Each pedal's pivot is an unrotated empty on the
    cross tube's axis, so a positive rotation about its own local **X** presses the
    pedal. That axis survives the glTF y-up conversion unchanged.
    """
    p = context.params
    up, normal = _pedal_frame(p)
    right = Vector((1.0, 0.0, 0.0))
    steel = context.material("frame_powdercoat")

    # `pedal_z` is published rather than authored: the bar's height is
    # `pedal_pivot_z + pedal_arm_length cos(rake)`, and the field is that rounded to
    # a millimeter for the manifest. Compared rather than trusted, because a
    # parameter that only *describes* the mesh is a parameter that drifts from it --
    # `notes_controls` stated 220 for this, which is 180 cos(19.2) and contradicts
    # its own 8 degree rake.
    if abs(P.pedal_bar_z(p) - p.pedal_z) > 0.0006:
        raise SystemExit(
            "error: params.pedal_z is %.4f and the arm puts the foot bar at %.4f.\n"
            "       pedal_z is derived from pedal_pivot_z, pedal_arm_length and\n"
            "       pedal_arm_rake; edit those, not this."
            % (p.pedal_z, P.pedal_bar_z(p))
        )

    pivot_y, pivot_z = p.pedal_pivot_y, p.pedal_pivot_z
    for name, x in (
        ("brake", -p.pedal_separation * 0.5),
        ("throttle", p.pedal_separation * 0.5),
    ):
        pivot_point = Vector((x, pivot_y, pivot_z))
        pivot = build.empty(
            "pedal_%s_pivot" % name, tuple(pivot_point), collection, size=0.04
        )
        context.publish("pedal_%s_pivot" % name, pivot)

        # The forged arm, in its own plane: a bushed eye at the bottom widening into
        # the boss and tapering to the bar. Distances run **up** the arm from the
        # pivot, which is the opposite of the hanging plate this replaces.
        root_half, tip_half = PEDAL_ARM_SECTION[0] * 0.5, PEDAL_ARM_TIP_SECTION[0] * 0.5
        length = p.pedal_arm_length
        # Wound the same way round as every other `_extruded_polygon` in this file:
        # up the *left* flank and down the right. Reversed, the arm encloses
        # -0.000027 m3 and no render shows it -- see genkart's winding gate.
        outline = [
            (-root_half, -0.012),
            (-root_half, 0.030),
            (-root_half * 0.8, 0.070),
            (-tip_half, length - 0.006),
            (tip_half, length - 0.006),
            (root_half * 0.8, 0.070),
            (root_half, 0.030),
            (root_half, -0.012),
        ]
        bm = bmesh.new()
        _extruded_polygon(
            bm, outline, pivot_point, right, up, normal, PEDAL_ARM_SECTION[1]
        )
        arm = build.object_from_bmesh(
            "pedal_%s" % name, bm, collection, material=steel
        )
        build.bevel_object(arm, context.detail)
        build.set_parent(arm, pivot)

        # The foot bar: a transverse round bar welded across the arm's top, which is
        # what `joints.py` now calls `welded` rather than `bolted`.
        bar = pivot_point + up * length
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                bar - right * p.pedal_bar_length * 0.5,
                bar + right * p.pedal_bar_length * 0.5,
            ],
            p.pedal_bar_diameter * 0.5,
            context.detail.tube_segments,
        )
        pad = build.object_from_bmesh(
            "pedal_%s_pad" % name,
            bm,
            collection,
            material=context.material("rubber_grip"),
            shade_smooth=True,
        )
        build.set_parent(pad, pivot)

        if name == "brake":
            # The slotted plate carrying the pushrod clevis, `PEDAL_CLEVIS_RISE` up
            # the arm -- which *is* the 3.2 : 1 ratio, 180/56.
            clevis = pivot_point + up * PEDAL_CLEVIS_RISE
            bm = bmesh.new()
            build.box(
                bm,
                (0.030, 0.034, 0.052),
                tuple(clevis),
                rotation=Matrix.Rotation(-p.pedal_arm_rake, 3, "X"),
            )
            plate = build.object_from_bmesh(
                "pedal_brake_clevis", bm, collection, material=steel
            )
            build.set_parent(plate, pivot)

        build.set_parent(pivot, root)

    _pedal_mounts(context, collection, root, pivot_y, pivot_z)


def _pedal_mounts(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    pivot_y: float,
    pivot_z: float,
) -> None:
    """The cross tube the pedals pivot on, and the two plates carrying it.

    **This is the 104.97 mm gate-2 failure, and the fix is a coordinate.** The plates
    used to run back to `(0, front_axle_y, rail_z + 0.025)` -- a straight cross member
    at the front axle line, which this chassis does not have: `chassis_cross_front` is
    a U-loop running y +500..+760 out at x ±110..±304, so the brackets were aimed at
    empty air 105 mm away. The loop's leg centreline passes **(±259, +560, +50)**, so
    each plate is bored to straddle that point and touches the tube at 0 mm.

    The plate's own shape is `sourced` from OTK 0014.D3: a bore for the frame tube at
    the top and the pivot eye 25 mm below it. Art. 4.4 makes the mounts part of the
    chassis product rather than a bolt-on -- *"pedal kits to relocate the driver's
    feet may only be used if supplied by the chassis manufacturer"* -- and Art. 4.2.3
    puts the welded attachment points for the pedals on the frame.
    """
    p = context.params
    detail = context.detail
    steel = context.material("frame_powdercoat")

    bm = bmesh.new()
    build.sweep_tube(
        bm,
        [
            (-PEDAL_TUBE_HALF_SPAN, pivot_y, pivot_z),
            (PEDAL_TUBE_HALF_SPAN, pivot_y, pivot_z),
        ],
        PEDAL_TUBE_DIAMETER * 0.5,
        detail.tube_segments,
    )
    tube = build.object_from_bmesh(
        "pedal_cross_tube", bm, collection, material=steel, shade_smooth=True
    )
    build.set_parent(tube, root)

    for label, sign in (("l", -1.0), ("r", 1.0)):
        bore = Vector((sign * PEDAL_MOUNT_BORE_X, PEDAL_MOUNT_BORE_Y, P.rail_z(p)))
        eye = Vector((sign * PEDAL_MOUNT_X, pivot_y, pivot_z))
        along = (eye - bore)
        reach = along.length
        along.normalize()
        edge = Vector((0.0, -along.z, along.y))

        bm = bmesh.new()
        _extruded_polygon(
            bm,
            [
                (-0.020, 0.019),
                (reach + 0.014, 0.013),
                (reach + 0.014, -0.013),
                (-0.020, -0.019),
            ],
            bore,
            along,
            edge,
            Vector((1.0, 0.0, 0.0)),
            0.008,
        )
        mount = build.object_from_bmesh(
            "pedal_mount_%s" % label, bm, collection, material=steel
        )
        build.bevel_object(mount, detail)
        build.set_parent(mount, root)


# --- hand shifter ----------------------------------------------------------


def _shifter(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Issue #117's gear lever, answered — and the answer is where the pivot goes.

    ARCHITECTURE.md §6.3 puts a six-speed sequential hand shifter on the right, and
    issue #15's silhouette test is whether the kart reads as a shifter rather than a
    single-speed. What #117 asked, for a milestone, was *where*.

    **Beside the knee, not the hip**, and the reasoning is the answer rather than the
    coordinate: the only two shift rods anybody sells are **530 mm** (OTK 0114.BA)
    and **495 mm** (Righetti Ridolfi / IKP hexagonal), both `sourced`, and a rod of
    that length between two ball joints reaching a selector at y ~ -200 puts the
    lever's own pivot at y ~ +330. A hip-mounted pivot at y ~ +100 needs a ~300 mm
    rod and no catalogue sells one. The rod length decides the fore-aft question
    against the intuitive answer. `SHIFTER_KNOB` was 200 mm rearward of this, beside
    the seat's top edge, i.e. beside the hip.

    An independent check nobody arranged: the right main rail's centreline at y +330
    is at x 310-323, and the estimated pivot x is +320. The bracket lands on the rail.

    The closure, in arithmetic, with the knob N and the rod's lower end R0 authored
    and the 55 degree kink measured:

        delta = N - R0 = (-130, -35, +375),  |delta| = 398.4
        |delta|^2 = P^2 (0.5312^2 + 0.4688^2 + 2 x 0.5312 x 0.4688 cos 55)
        P = 448.9 total bent path  ->  rod 238.5, tube 210.4
        rod axis 5.85 deg from vertical, leaning outboard and forward
        kink K = (+354, +341, +312);  |N - K| = 210.4 and angle(a, T) = 54.9  ok

    Which is why the lever bows out to x +354 at the kink and returns to +200 at the
    knob: the kink brings the tube back inboard over the driver's knee.
    """
    p = context.params
    detail = context.detail
    steel = context.material("frame_powdercoat")

    r0 = Vector((p.shift_pivot_x, p.shift_pivot_y, p.shift_pivot_z))
    knob = Vector((p.shift_knob_x, p.shift_knob_y, p.shift_knob_z))
    delta = knob - r0
    span = delta.length
    kink_angle = p.shift_kink
    front, back = SHIFT_RATIO, 1.0 - SHIFT_RATIO
    total = math.sqrt(
        span * span
        / (front * front + back * back + 2.0 * front * back * math.cos(kink_angle))
    )
    rod_length = front * total
    tube_length = back * total

    # The rod's own axis, in the plane of `delta` and nearer vertical of the two
    # branches. Solved rather than authored: the knob and the pivot are the inputs.
    cos_to_delta = (rod_length + tube_length * math.cos(kink_angle)) / span
    cos_to_delta = max(-1.0, min(1.0, cos_to_delta))
    offset = math.acos(cos_to_delta)
    unit = delta / span
    # In-plane perpendicular to `delta`, chosen on the side that leans the rod
    # toward vertical.
    vertical = Vector((0.0, 0.0, 1.0))
    perpendicular = (vertical - unit * unit.dot(vertical))
    if perpendicular.length < 1e-9:
        perpendicular = Vector((1.0, 0.0, 0.0))
    perpendicular.normalize()
    rod_axis = (unit * math.cos(offset) + perpendicular * math.sin(offset)).normalized()
    kink = r0 + rod_axis * rod_length

    # The bracket: two nylon bushes on a plate, and a clamp on the tube the plate
    # actually reaches. **No shift gate** -- see the SHIFT_ROD_DIAMETER block for why
    # the slotted plate that used to be here was the one invented part in the assembly.
    bm = bmesh.new()
    build.box(
        bm,
        (0.006, 0.040, 0.046),
        tuple(r0 + Vector((SHIFT_BRACKET_PLATE_OFFSET, 0.006, 0.014))),
    )
    for along in (0.0, SHIFTER_BUSH[2]):
        build.sweep_tube(
            bm,
            [
                r0 + rod_axis * (along - SHIFTER_BUSH[1] * 0.0),
                r0 + rod_axis * (along + 0.020),
            ],
            SHIFTER_BUSH[1] * 0.5,
            detail.tube_segments,
        )
    _shifter_clamp(context, bm, r0)
    base = build.object_from_bmesh(
        "shifter_base", bm, collection, material=steel, shade_smooth=True
    )
    build.set_parent(base, root)

    bm = bmesh.new()
    build.sweep_tube(
        bm, [r0, kink], SHIFT_ROD_DIAMETER * 0.5, detail.tube_segments
    )
    build.sweep_tube(
        bm,
        [kink, knob - (knob - kink).normalized() * SHIFTER_KNOB_LENGTH * 0.3],
        SHIFT_TUBE_DIAMETER * 0.5,
        detail.tube_segments,
    )
    lever = build.object_from_bmesh(
        "shifter_lever", bm, collection, material=steel, shade_smooth=True
    )
    build.set_parent(lever, root)

    # Knob: a revolved teardrop rather than a sphere. Ø28 x 47, `derived` off the
    # 0112.B0 photo -- the built radius was 0.026, i.e. Ø52, nearly twice the part.
    radius = SHIFTER_KNOB_DIAMETER * 0.5
    half = SHIFTER_KNOB_LENGTH * 0.5
    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (0.000, -half),
            (radius * 0.34, -half * 0.92),
            (radius * 0.78, -half * 0.58),
            (radius * 1.00, -half * 0.10),
            (radius * 0.94, half * 0.44),
            (radius * 0.62, half * 0.80),
            (radius * 0.24, half * 0.96),
            (0.000, half),
        ],
        detail.tube_segments,
        axis="Z",
        center=tuple(knob),
    )
    knob_object = build.object_from_bmesh(
        "shifter_knob",
        bm,
        collection,
        material=context.material("rubber_grip"),
        shade_smooth=True,
    )
    build.set_parent(knob_object, root)

    _shift_linkage(context, collection, root, r0, rod_axis)


def _shifter_clamp(
    context: build.BuildContext,
    bm: bmesh.types.BMesh,
    r0: Vector,
) -> None:
    """What the gear lever's bracket is actually bolted to. #190 wave 3b.

    **Spec §40.4's own cross-check was false and this is the correction.** It reads
    *"the right main rail's centreline at y +330 interpolates to x 323 ... The bracket
    lands on the rail"*, and §10 has since waisted the frame: `frame_half_waist` is 139
    at y +375, so at the lever's own y +335 the rail is at x **156**, measured off the
    built tube. The bracket was 106.85 mm from it -- the worst gate-2 finding on the
    kart -- and the lever's position is not what is wrong: two `sourced` shift-rod
    lengths and the two-finger gap to the rim fix it.

    What is wrong is the claim about which member is under it. The nearest structure to
    the bracket plate is `chassis_side_bar_r`'s **forward leg**, which crosses
    (325, 366, 81) on its way inboard from the straight run to its front socket --
    **5.34 mm** away, against 106.85 to the rail, 93.62 to the nearer bumper socket and
    124.91 to the tray's edging tube. So the bracket clamps the Art. 9.4.2 lower side
    bumper, which is also where a real KZ hand shifter's bracket goes: the lever stands
    beside the driver's knee, and at the knee the only frame tube out at x 320 is the
    side bumper. The main rail is 160 mm inboard of it and always was.

    The leg's two ends come from `frame.py` through `context.pivots` rather than being
    re-derived here, because `_corner` pushes the built corner 72 mm along +y off the
    authored tangent point -- so a copy of the authored polyline gets the leg's
    *direction* wrong, not just its length. §98.3.
    """
    detail = context.detail
    socket = Vector(context.pivots["side_bar_front_socket_r"].location)
    corner = Vector(context.pivots["side_bar_front_corner_r"].location)

    # Foot of the perpendicular from the plate's outboard face to the leg's axis,
    # clamped to the segment: the collar goes round the tube where the plate meets it,
    # not at an authored station that a later frame edit would move away from.
    plate = r0 + Vector((SHIFT_BRACKET_PLATE_OFFSET, 0.0, 0.014))
    span = corner - socket
    length_squared = span.length_squared
    along = 0.0 if length_squared < 1e-12 else (plate - socket).dot(span) / length_squared
    along = max(0.0, min(1.0, along))
    station = socket + span * along
    axis = span.normalized() if length_squared > 1e-12 else Vector((1.0, 0.0, 0.0))

    # The strap from the plate out to the tube, then the collar round it. The collar's
    # bore is the bumper's own diameter, so it grips rather than floats: Art. 9.4.2
    # fixes that tube at Ø20.0 and `tube_bumper` is the single owner of the number.
    build.sweep_tube(
        bm,
        [plate, station],
        SHIFT_BRACKET_STRAP_DIAMETER * 0.5,
        max(6, detail.tube_segments // 2),
    )
    build.sweep_tube(
        bm,
        [
            station - axis * (SHIFT_BRACKET_COLLAR_WIDTH * 0.5),
            station + axis * (SHIFT_BRACKET_COLLAR_WIDTH * 0.5),
        ],
        context.params.tube_bumper * 0.5 + SHIFT_BRACKET_COLLAR_WALL,
        detail.tube_segments,
    )


def _shift_linkage(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
    r0: Vector,
    rod_axis: Vector,
) -> None:
    """The connector arm and the tie-rod that reach the gearbox selector.

    OTK **0111.B0A** is a real catalogued piece and the linkage cannot exist without
    it: without the arm there is nothing for the rod to push. Its 90 degrees against
    the rod is `sourced(snippet)` and the **serrated collet** is the hardware that
    makes an arbitrary angle settable, which is corroboration from the part rather
    than from the text.

    The rod is `sourced` at 495 mm eye to eye, hexagonal, 13 mm across the flats,
    with two uniball ends on **opposing thread pitches** -- the assembly is a
    turnbuckle and adjusts without disconnecting. The far joint is placed so that the
    sourced length closes exactly; **a joint needing less than 495 mm is not
    buildable from a part anybody sells**, which is the constraint that placed the
    lever in the first place.
    """
    p = context.params
    steel = context.material("frame_powdercoat")

    # 90 degrees to the rod, in the plane that swings fore-and-aft.
    rearward = Vector((0.0, -1.0, 0.0))
    arm_direction = (rearward - rod_axis * rod_axis.dot(rearward)).normalized()
    # 75 mm **above** the rod's lower end, i.e. above both nylon bushes. Below them
    # the collet shares triangles with the bracket plate; level with them the rod then
    # runs so low that it grazes Art. 4.6's edging tube on its way aft, and dropping
    # the selector joint to clear the tube puts it inside the clutch bell. One
    # position satisfies all three, and it is above the bracket.
    collet = r0 + rod_axis * 0.075
    joint = collet + arm_direction * SHIFTER_ARM_LENGTH

    bm = bmesh.new()
    _extruded_polygon(
        bm,
        [
            (0.0, SHIFTER_ARM_SECTION[0] * 0.5),
            (SHIFTER_ARM_LENGTH, SHIFTER_ARM_SECTION[0] * 0.4),
            (SHIFTER_ARM_LENGTH, -SHIFTER_ARM_SECTION[0] * 0.4),
            (0.0, -SHIFTER_ARM_SECTION[0] * 0.5),
        ],
        collet,
        arm_direction,
        rod_axis,
        arm_direction.cross(rod_axis).normalized(),
        SHIFTER_ARM_SECTION[1],
    )
    arm = build.object_from_bmesh(
        "shifter_connector_arm", bm, collection, material=steel
    )
    build.set_parent(arm, root)

    # The far joint is **placed so the sourced rod length closes exactly**, rather
    # than authored and the rod stretched to reach it: §40.4's coordinate for the
    # gearbox selector is (215, -205, 95) and sqrt(115^2 + 481^2 + 19^2) = 494.9
    # against a `sourced` 495. If §Powertrain moves the selector shaft the rod
    # *choice* flips to the 530 mm OTK part; it does not stretch.
    target = Vector(SHIFT_SELECTOR_JOINT)
    direction = (target - joint).normalized()
    far = joint + direction * p.shift_rod_length
    # The rod runs between the two rod-end centres, so its own length is the
    # sourced 495 less both ends' half-lengths.
    bm = bmesh.new()
    build.sweep_tube(
        bm,
        [
            joint + direction * SHIFT_ROD_END_DIAMETER * 0.5,
            far - direction * SHIFT_ROD_END_DIAMETER * 0.5,
        ],
        SHIFT_ROD_DIAMETER * 0.5,
        6,
    )
    rod = build.object_from_bmesh(
        "shift_rod", bm, collection, material=context.material("engine_alloy")
    )
    build.set_parent(rod, root)

    for label, point in (("front", joint), ("rear", far)):
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                point - direction * SHIFT_ROD_END_DIAMETER * 0.5,
                point + direction * SHIFT_ROD_END_DIAMETER * 0.5,
            ],
            SHIFT_ROD_END_DIAMETER * 0.5,
            context.detail.tube_segments,
        )
        end = build.object_from_bmesh(
            "shift_rod_end_%s" % label,
            bm,
            collection,
            material=context.material("engine_alloy"),
            shade_smooth=True,
        )
        build.set_parent(end, root)


# --- fuel tank -------------------------------------------------------------


def _fuel_tank(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """The tank Art. 9.3 requires and this kart did not have.

    Art. **9.3**, PDF p. 22: **8 litres minimum** for Group 2. Art. **4.7** does not
    merely permit a position, it *mandates* one: *"It is mandatory to place the fuel
    tank between the main tubes of the chassis frame, ahead of the seat and behind
    the rotation axis of the front wheels."* All three of `tank_center_*`'s
    coordinates are forced by that sentence, which is why they are `derived`.

    Art. 4.7 also forbids any shaping *"to act as an aerodynamic device"*, requires
    flexible pipes, and permits nothing but the fuel pump to influence the tank's
    internal pressure. Art. **5.6.1** allows exactly **one** feed line plus one
    filter -- so the count is a scrutineering fact and not a styling choice. The
    **return** line is what distinguishes a KZ tank from an OK tank and is not a feed
    line, which is why there are three top-rear fittings and one feed.

    **The notch is real geometry.** The mandated position drives the steering column
    through the tank's top-front corner between y +330 and +350 above z 272; a real
    molding is waisted there and declaring a joint instead would *permit* exactly the
    interpenetration gate 1 exists to catch. See `TANK_NOTCH_WIDTH`.
    """
    p = context.params
    detail = context.detail
    plastic = context.material("frame_powdercoat")

    half_w = p.tank_width * 0.5
    half_d = p.tank_depth * 0.5
    half_h = p.tank_height * 0.5
    cy, cz = p.tank_center_y, p.tank_center_z
    front = cy + half_d
    top = cz + half_h

    # The shell, as a lofted box with the top-front corner cut away for the column
    # and the bottom front waisted for the shins.
    notch_half = TANK_NOTCH_WIDTH * 0.5
    bm = bmesh.new()
    # Body, in three blocks so the notch is an absence rather than a boolean: the
    # two flanks run full depth and the centre section stops short of the column.
    for low_x, high_x in (
        (-half_w, -notch_half),
        (notch_half, half_w),
    ):
        build.box(
            bm,
            (high_x - low_x, p.tank_depth, p.tank_height),
            ((low_x + high_x) * 0.5, cy, cz),
        )
    # The centre section: full height at the back, and cut down to clear the column
    # over the forward 20 mm of y.
    # Where the notch has to **start**, not where the column crosses the top plane.
    # The column is Ø20, so its *lower surface* is what the molding has to clear:
    # z(y) - 10 reaches the tank's top plane 299 at y 323, not at the 330 the
    # centreline gives. The 7 mm is the difference between a notch that works and a
    # notch that reports 52 intersecting triangle pairs.
    column_clear_y = 0.318
    build.box(
        bm,
        (TANK_NOTCH_WIDTH, column_clear_y - (cy - half_d), p.tank_height),
        (0.0, ((cy - half_d) + column_clear_y) * 0.5, cz),
    )
    build.box(
        bm,
        (TANK_NOTCH_WIDTH, front - column_clear_y, p.tank_height - 0.048),
        (0.0, (column_clear_y + front) * 0.5, cz - 0.024),
    )
    tank = build.object_from_bmesh(
        "fuel_tank", bm, collection, material=plastic
    )
    build.bevel_object(tank, detail)
    build.set_parent(tank, root)

    # Three fittings on the top rear: feed, return, vent. Art. 5.6.1 caps the feed
    # count at one; the return is the KZ part's distinguishing feature.
    for index, offset in enumerate((-0.055, 0.0, 0.055)):
        build.set_parent(
            build.object_from_bmesh(
                "fuel_tank_fitting_%d" % index,
                _fitting_mesh(context, offset, cy - half_d + 0.022, top),
                collection,
                material=plastic,
                shade_smooth=True,
            ),
            root,
        )

    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (0.0, top - 0.004),
            (TANK_FILLER_DIAMETER * 0.5 - 0.006, top - 0.004),
            (TANK_FILLER_DIAMETER * 0.5, top + 0.006),
            (TANK_FILLER_DIAMETER * 0.5, top + TANK_FILLER_HEIGHT - 0.004),
            (TANK_FILLER_DIAMETER * 0.5 - 0.005, top + TANK_FILLER_HEIGHT),
            (0.0, top + TANK_FILLER_HEIGHT),
        ],
        detail.tube_segments,
        axis="Z",
        center=(0.0, 0.138, 0.0),
    )
    filler = build.object_from_bmesh(
        "fuel_tank_filler", bm, collection, material=plastic, shade_smooth=True
    )
    build.set_parent(filler, root)

    # Two straps over the top, on the molded channels, down to both main rails.
    # Art. 4.7: *"A quick attachment to the chassis is strongly recommended"*, so a
    # cam-buckle strap rather than a bolted steel band.
    # **The rail's half-width at the strap's own station, and the frame waists hard
    # here.** Spec §40.6 says the rail interpolates to x 297 at y +225; on this chassis
    # it does not -- `frame_half_strut` is 286 at y +40 and `frame_half_waist` is 139 at
    # y +375, so the rail is at 217 at y +196 and 180 at y +282. A strap authored to a
    # single 291 or 330 misses by 79 mm. Duplicated here rather than read off
    # `frame.py`'s objects, for the reason `wheels._pedal_plate_y` gives.
    def rail_half(y: float) -> float:
        span = (y - p.cross_strut_y) / (p.frame_waist_y - p.cross_strut_y)
        span = min(max(span, 0.0), 1.0)
        return p.frame_half_strut + (p.frame_half_waist - p.frame_half_strut) * span

    for label, strap_y in (("front", TANK_STRAP_Y[0]), ("rear", TANK_STRAP_Y[1])):
        rail_x = rail_half(strap_y)
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                (-rail_x, strap_y, P.rail_z(p)),
                (-half_w - 0.004, strap_y, top - 0.030),
                (-half_w + 0.020, strap_y, top + 0.003),
                (half_w - 0.020, strap_y, top + 0.003),
                (half_w + 0.004, strap_y, top - 0.030),
                (rail_x, strap_y, P.rail_z(p)),
            ],
            TANK_STRAP_SECTION[0] * 0.5,
            6,
        )
        strap = build.object_from_bmesh(
            "fuel_tank_strap_%s" % label,
            bm,
            collection,
            material=context.material("rubber_grip"),
        )
        build.set_parent(strap, root)

    # One feed line and one return, out of the top-rear fittings and rearward along
    # the driver's right to the pulse pump and the carburettor. Art. 4.7 requires
    # them flexible; the route is `estimated`.
    for label, offset, lane, end in (
        ("feed", -0.055, 0.000, (0.268, -0.132, 0.132)),
        ("return", 0.055, 0.020, (0.268, -0.108, 0.132)),
    ):
        bm = bmesh.new()
        # Out of the fitting and **backwards past the tank's rear face first**, at
        # y +100: routed down the outside of the tank instead, the line runs through
        # the shell it is plumbed into. The two lines then keep separate lanes in z,
        # because a feed and a return sharing a route share their triangles.
        build.sweep_tube(
            bm,
            [
                (offset, cy - half_d + 0.022, top + 0.004),
                (offset, 0.062, top - 0.030 + lane),
                (0.170, 0.010, 0.150 + lane),
                (0.250, -0.070, 0.120 + lane),
                end,
            ],
            FUEL_LINE_DIAMETER * 0.5,
            6,
        )
        line = build.object_from_bmesh(
            "fuel_line_%s" % label,
            bm,
            collection,
            material=context.material("rubber_grip"),
            shade_smooth=True,
        )
        build.set_parent(line, root)


def _fitting_mesh(
    context: build.BuildContext, x: float, y: float, top: float
) -> bmesh.types.BMesh:
    """One Ø8 nipple standing on the tank's top face."""
    bm = bmesh.new()
    build.lathe(
        bm,
        [
            (0.0, top - 0.003),
            (TANK_FITTING_DIAMETER * 0.5 + 0.002, top - 0.003),
            (TANK_FITTING_DIAMETER * 0.5, top + 0.004),
            (TANK_FITTING_DIAMETER * 0.5, top + 0.018),
            (0.0, top + 0.018),
        ],
        context.detail.tube_segments,
        axis="Z",
        center=(x, y, 0.0),
    )
    return bm
