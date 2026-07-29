"""The kart parameter block — every dimension in the generated kart, in meters.

This is the single source of truth for kart geometry. ARCHITECTURE.md §5 item 1
puts correct real-world scale above every other realism lever, and the only way
to hold that is for no module to carry its own copy of a number.

**`docs/KART_SPEC.md` is the design and it outranks every comment in this file.**
Where a docstring here disagrees with the spec, this file is what is wrong — that
is the premise of issue #190. Every field below carries the spec's provenance
vocabulary, stated once here and never re-argued per field:

    sourced      there is a citation and the citation was read, not recalled
    derived      arithmetic from sourced numbers, with the arithmetic shown
    estimated    no source exists; carries its reasoning -- what it was read off,
                 what it was checked against, what it is consistent with

`estimated` is a normal outcome and not a defect to drive to zero. The two things
that *are* defects are an unmarked number, and an estimate written in the
vocabulary of a limit. This file's own header used to be the worked example of
the second one. It read:

    Overall length          1830 mm max
    Overall width           1400 mm max
    Wheelbase               1050 mm max (KZ runs at the limit)
    Rear tire width          215 mm max
    Front tire width         135 mm max
    Rear axle                  50 mm solid

and five of those six lines were wrong about what kind of number they were:

  * Overall length is deferred by Art. 9.1.1 to a technical drawing that is not
    public. 1830 was sourced nowhere, and `frame.py` derived the whole footprint
    from it while `bodywork.py` clamped five panels to the result. It is gone as
    an input -- see `computed_figures()`, which reports 1,921 mm as a
    *derived* figure so issue #21 still has something to measure.
  * Overall width is not track width. Art. 9.1.1 says *"Track width: maximum
    1400.0 mm"*; Art. 9.5.5.1 separately lets the rear protection reach the
    overall rear width. The two coincide only in the maximum case.
  * The wheelbase range is 1010.0-1070.0 mm, so 1050 is not "max" -- 1070 is.
  * 215 and 135 are Art. 4.13.1 **wheel** ceilings, rim plus inflated tire at
    0.5 bar, not tire widths.
  * Art. 9.2's 50.0 mm is a *maximum outside diameter* with a wall-thickness
    clause, which makes the axle a tube. "50 mm solid" was a real error.

The three figures ARCHITECTURE.md §5 names explicitly — 1.05 m wheelbase, 1.4 m
track width, 0.28 m frame height — are `wheelbase`, `track_rear` and
`frame_height` below, and scripts/look/lookdev.gd holds the same three for its
reference box. They must not drift apart; issue #21 checks the wheelbase in
Godot for exactly that reason.

Four of the frozen values (`wheelbase`, `track_rear`, `track_front` and the tire
diameters and widths) are frozen because every §6.4 driving figure and every
`drive.sh` scenario is measured against them. Freezing the *value* does not
freeze the *label*, and the labels are what #190 fixed. Issue #196 carries the
values.

**A parameter that no module reads is a lie**, and this block carried four of
them: `frame_height`, `tray_width`, `tray_length` and `lod_ratios`.
`check_field_coverage()` at the bottom of this file is now a fatal build gate, in
the same family as the signed-volume winding assert and `joints.py`'s "a pattern
that matches nothing is fatal". A field that is deliberately informational is on
`FIELD_COVERAGE_EXEMPT` with a reason, one entry at a time and never a blanket
skip.

Coordinate convention, stated once because getting it wrong is invisible until
the kart drives backwards:

    Blender  +X = kart right, +Y = kart forward, +Z = up
    glTF export with export_yup=True maps (x, y, z) -> (x, z, -y)
    so Blender +Y forward becomes glTF -Z, which is Godot's forward.

Nothing in this package may use Blender's default -Y forward. Build toward +Y.

Origin: on the ground, laterally centered, midway between the axles. The front
axle is at y +0.525 and the rear at y -0.525, and every overhang in this file is
measured from those two lines, because that is where the CIK-FIA chassis
homologation form's own `G1`/`G2` dimensions are measured from.
"""

from __future__ import annotations

import dataclasses
import glob
import math
import os
import re
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
    """Front axle to rear axle. `sourced` in range, **not a maximum**.

    Art. 9.1.1, PDF p. 22: *"Wheelbase: 1010.0 - 1070.0 mm."* 1050 sits 20 mm
    inside the range with 20 mm of margin at either end; the maximum is 1070.
    This docstring said "CIK maximum" for two milestones, which is the mislabel
    §00 §3 of the spec catches by name. Frozen: issue #196.
    """

    track_rear: float = 1.400
    """Outside-to-outside rear track. `estimated` choice on a `sourced` ceiling.

    Art. 9.1.1: *"Track width: maximum 1400.0 mm"*, so the **ceiling** is
    sourced and sitting on it is a choice -- the spec's own §20.3.4 quotes a CRG
    setup guide offering 54-55 in = 1371.6-1397 mm, i.e. real karts run under the
    cap. **This is not the kart's overall width limit**; that is Art. 9.5.5.1's
    rear-protection clause and it is a different number that coincides only at
    the maximum. Frozen: `chassis.h` and the tire model are measured against it.
    """

    frame_height: float = 0.280
    """Top of the frame's highest structural tube above the ground. `estimated`.

    Art. 9.1.1 caps the chassis at 650 mm from the ground *without the seat*, so
    280 is not a limit -- it is a description, and it is 370 mm inside the cap.

    **No Blender module reads this**, and it is on `FIELD_COVERAGE_EXEMPT` with
    that stated. It survives because ARCHITECTURE.md §5 names it and
    scripts/look/lookdev.gd carries a hardcoded twin (`KART_FRAME_HEIGHT`), so
    deleting it would silently orphan the reference box. The seat struts used to
    read it and no longer do: §10.9 of the spec authors their ear points, because
    a stay that ends at a nominal frame height rather than at the seat's own
    mounting ear is a stay aimed at nothing.
    """

    # --- the footprint, and why there is no single length ------------------
    #
    # The two ends of a kart are not symmetric and never were: the front overhang
    # is 504 mm and the rear 367, and they are governed by four limits in three
    # articles. A single scalar about the origin cannot express that, and any
    # symmetric split hands each end their mean -- which is illegal at one end at
    # every possible value. `length_overall` is therefore gone as an input and
    # survives only as a computed report figure. Spec §10.2.

    overhang_front_frame: float = 0.250
    """Frontmost *frame* tube's outer surface, ahead of the front axle line.

    `sourced`: CIK-FIA chassis homologation form `04/CH/14` (CRG Road Rebel)
    section B, `G2` = 250 ±10 mm. Gillard TG16 `026-CH-99` publishes 275 ±10 on
    the same field. This is the front loop, not the bumper.
    """

    overhang_rear_frame: float = 0.210
    """Rearmost frame tube's outer surface, behind the rear axle line.

    `sourced`: `G1` = 210 ±15 mm on **both** homologation forms, which is the
    most strongly corroborated single dimension in the chassis.
    """

    overhang_front_bumper: float = 0.420
    """Front bumper's frontmost surface, ahead of the front axle line.

    `estimated`, and the reasoning is the whole content of the number: it clears
    Art. 9.4.1's *"Front overhang: 350.0 mm minimum"* by 70 mm, and it puts the
    bar 84 mm behind the fairing's front face -- 29% of the way into the
    fairing's own 287 mm depth, which is behind the molded nose radius where a
    fairing's clamp bosses are and forward of its open back. TD n°2.2 dimensions
    the mounting kit and is not obtainable, so this is judgment, not a limit.
    """

    overhang_front_fairing: float = 0.504
    """Front fairing's front face, ahead of the front axle line. `derived`.

    Photogrammetric, `tonykart_racer401T_product.png`, anchored on the wheelbase
    at 397 px = 1050 mm: (705 - 514.5) px = 504 mm, ±15 mm (the same image's rear
    tire measures 308 mm against Art. 4.13.1's 300 maximum, so it carries at
    least 3% of scale error). Under Art. 9.5.2's 680 mm ceiling by 176 mm.

    Read only by `computed_figures()` today and exempt from the coverage gate for
    exactly that reason: §50 owns the panel that will consume it.
    """

    overhang_rear_protection: float = 0.367
    """Rear wheel protection's rear face, behind the rear axle line. `derived`.

        rear tire radius      147.5
        gap to the panel      15..50   Art. 9.5.5.1
        panel depth           187      KG C2 `003-BR-48`, sourced
        =>                    349.5 .. 384.5, under the 400 mm cap

    367 is the middle of that band. Same exemption as `overhang_front_fairing`.
    """

    track_front: float = 1.240
    """Outside-to-outside front track. `derived` floor, `estimated` value.

    Art. 9.1.1's *"Track: at least 2/3 of the wheelbase"* gives a floor of
    2/3 x 1050 = 700 mm, cleared with 540 mm to spare. That the front is
    *narrower* than the rear is KZ practice rather than a regulation; the
    specific 1240 is estimated. Frozen: issue #196.
    """

    ground_clearance: float = 0.035
    """Underside of the lowest frame tube to the ground. `estimated`.

    Measured to the frame, not to the floor tray, because on a kart the rails
    *are* the lowest point — the tray bolts on top of them. Getting this
    backwards puts the whole chassis 30 mm into the asphalt. Art. 4.2.5's skid
    plates hang below this and are not built yet.
    """

    # --- frame tubes -------------------------------------------------------

    tube_main: float = 0.030
    """Main rail outside diameter. `sourced`.

    Gillard TG16 `026-CH-99` section B publishes 30 ±0.5 mm for all six of its
    counted main tubes; CRG runs 32 ±0.5 on the Road Rebel. Both are homologated,
    30 is the lighter and more common KZ figure, and nothing downstream is fitted
    to the difference. Art. 4.3's wall table applies (>28.0 mm -> free), and this
    package builds tubes as solid sweeps, so the wall is not modeled -- spec
    §99 W3 is the open item.
    """

    tube_secondary: float = 0.022
    """The rear strut, which is the one transverse tube thinner than the six
    counted mains. `derived`: the CRG plan drawing draws it below 21 mm and
    `026-CH-99` p. 3 requires the homologation marking to be on it, so it is a
    real member and not a main tube."""

    tube_bumper: float = 0.020
    """Front bumper *lower* bar, both side bumper bars, rear bumper hoop.

    `sourced` twice: Art. 9.4.1 sets the lower front bar at *"a minimum diameter
    of 20.0 mm"* and Art. 9.4.2 sets both side bars at *"a diameter of 20.0 mm"*
    exactly. The rear hoop is `estimated` by analogy -- there is no rear bumper
    article at all.
    """

    tube_bumper_upper: float = 0.016
    """Front bumper *upper* bar, and the bumper support that joins the two.

    `sourced`: Art. 9.4.1, *"an upper bar with a minimum diameter of 16.0 mm"*.
    Corroborated by the OTK M4 fairing form's `acciaio Ø16x1.5` pair at 550 mm
    spacing, which matches this article on diameter and spacing simultaneously.
    """

    tube_steering_hoop: float = 0.016
    """The lower steering support hoop. `estimated`: Art. 9.5.3 requires the
    support to exist and to carry the front panel's upper bars, and sizes
    nothing. 16 mm is the smallest tube on the reference karts."""

    tube_tray_edge: float = 0.016
    """The floor tray's lateral edging tube. `estimated`, same reasoning as
    `tube_steering_hoop`: Art. 4.6's last sentence makes the edging **mandatory**
    (*"laterally edged by a tube or a rim preventing the driver's feet from
    sliding off the floor tray"*) and gives it no size."""

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
    """Radius of a bent tube corner. `estimated`. A real chassis is mandrel-bent,
    never mitered, and a sharp corner is the single clearest tell of a toy kart.

    It is load-bearing on the regulated bumpers in a way that is not obvious: a
    fillet eats `radius / tan(theta/2)` off each leg, so a bar whose control
    polyline is 305 mm across measures far less than 305 mm of *straight* between
    its bends. `frame._corner` computes that tangent and pushes the control point
    outward, so the built straight is the regulation figure and not the polyline.
    """

    bend_segments: int = 6
    """Polyline steps per bend on the low-poly mesh."""

    bend_segments_high: int = 14

    # --- the frame in plan -------------------------------------------------
    #
    # Spec §10.3. All five stations are read off the CRG Road Rebel form's 1:10
    # plan drawing as *ratios* of its own outer rear half-width and then applied
    # to this kart's sourced `F` = 650: the drawing is 6% off in absolute scale
    # (checked four ways against section B) and dead-on in proportion.
    #
    # **The frame is widest at the rear.** `frame.py`'s docstring item 4 said the
    # opposite for two milestones and the built mesh was backwards with it --
    # front ±462.5, rear ±215, against a measured CRG of rear ±314 outer and a
    # waist of ±149 outer.

    frame_half_rear: float = 0.310
    """Rail centerline half-width from the rear extremity forward to
    `frame_rail_straight_y`. `derived`: `F` = 650 ±10 sourced, so
    650/2 - 15 = 310 at the centerline of a Ø30 tube."""

    frame_rail_straight_y: float = -0.048
    """Forward end of the rail's constant-width run. `sourced`: the CRG plan
    holds a constant outer half-width of 334 px over y -735 .. -48.

    This is the field the powertrain's whole load path depends on: the right rail
    is straight at x +310, z +50 from here back to the rear end, so the engine
    mount's two clamps have one number instead of two."""

    frame_half_strut: float = 0.286
    """Rail centerline half-width at the central strut. `derived`:
    301/314 of the rear outer half-width x 325 - 15."""

    frame_half_waist: float = 0.139
    """Rail centerline half-width at the waist -- the frame's narrowest point.
    `derived`: 149/314 x 325 - 15. Leaves 248 mm of clear gap between the rails'
    surfaces, which is where the driver's heels sit."""

    frame_waist_y: float = 0.375
    """Station of the waist. `sourced`: the CRG plan's own minimum."""

    frame_half_node: float = 0.304
    """Rail centerline half-width at the stub-axle node. `derived`:
    308/314 x 325 - 15."""

    frame_node_y: float = 0.500
    """Station of the stub-axle node, where the rail ends and the front loop
    begins. `estimated` at 25 mm behind the front axle line: the node carries the
    kingpin boss, which is on the axle line, and the rail has to stop short of
    it. The CRG plan puts the node just aft of the `E` extension lines."""

    frame_half_front: float = 0.110
    """Half-width of the front loop's frontmost segment. `derived`: the CRG plan
    measures 123 mm outer at the frontmost tube, less 15 for the wall."""

    cross_strut_y: float = 0.040
    """The **central strut** -- Art. 4.6 names it, and that is a load-bearing
    identification rather than a label, because the floor tray's rear edge is
    defined as this tube. `sourced`: CRG `B2` measures y +36.

    It was at y +230 and Ø22. The CRG's six-main-tube count includes it, so it is
    Ø30 here."""

    cross_seat_y: float = -0.417
    """The rearmost transverse *main* tube. `sourced`: CRG `B6`.

    It was at y -60, under the seat. Nothing needs a tube under the seat: Art.
    4.2.3 gives the seat four supports and they come off the rails."""

    cross_tail_y: float = -0.713
    """The rear strut, which carries the homologation marking. `derived`:
    -(525 + 210) + 22/2 = -724 for a tube whose outer surface is on the frame's
    rear extremity; -713 once the Ø22 tube is placed so its *rear* surface sits
    on -735 minus nothing. Both readings are within a tube diameter and this one
    keeps the strut clear of the bumper hoop's legs."""

    # --- the front end -----------------------------------------------------

    kingpin_x: float = 0.320
    """Kingpin axis, lateral. `derived`, and it is the number spec §10.5 is least
    sure of while mattering most.

    Two independent measurements disagree by 28 mm:

        photogrammetric kingpin flange spacing 639 ±20  ->  319.5
        `E`/2 minus a Ø40 boss radius, 367.5 - 20       ->  347.5

    320 is taken because it measures the pin directly rather than inferring it
    from a bracket of unknown thickness, and because it lands 16 mm outboard of
    the rail's own node at 304, which is what the CRG's short diagonal from node
    to boss looks like.

    **`frame.py` built these 925 mm apart** -- `front_hub_x - 0.090`, i.e. 462.5
    per side, which is 190 mm outboard of the frame's own sourced 735 mm front
    width. The residual in the front track chain is 142.5 mm per side and
    §Running gear owns it: the frame's contribution is that the kingpin is here.
    """

    kingpin_boss_diameter: float = 0.040
    """Kingpin boss outside diameter. `estimated`: two lugs plus a plate, and
    nothing publishes it. It is the figure `E`/2 - 20 = 347.5 above was derived
    against, so the two are consistent."""

    kingpin_boss_length: float = 0.060
    """Boss length along the kingpin axis. `estimated`, as the diameter.
    Positioned so its underside is flush with the rail's, at z 35, rather than
    centered on `rail_z` -- centered would make the boss the lowest thing on the
    kart and `ground_clearance` is documented to the rail."""

    steering_hoop_foot_x: float = 0.200
    """Where the lower steering support's feet land on the front loop.
    `derived`, and exactly: at y +639 the loop's leg centerline is at
    304 - (139/260) x 194 = 200, so the weld is on the tube rather than near it.

    The built feet were at x ±150 at rail height -- 60 mm inboard of the loop and
    114 mm behind the old cross member, which is the 7.55 mm gate-2 finding."""

    steering_bore_y: float = 0.477
    steering_bore_z: float = 0.097
    """Lower steering bearing bore, on the kart's centerline. `sourced`:
    `refs/kart-visual/notes_column.md`, consistent with a column of OD 20.0 mm at
    36° from vertical and 490 mm long.

    There are **two** steering supports on a real column and this project had
    collapsed them into one. This is the lower one, and it carries the bearing.
    """

    steering_support_foot_x: float = 0.200
    """Feet of the upper steering support, on the central strut. `estimated`, and
    the value is a measurement rather than a preference.

    Spec §10.6 item 2 authors ±150 and ±150 does not fit **this** kart: the built
    `seat_shell` reaches y +239.6 -- 300 mm forward of its own hip point -- and its
    front wing is 148.7 mm wide at y +50, so both legs of the V pass straight
    through it, 218 intersecting triangle pairs measured. ±240 clears the seat and
    puts the right leg through `shifter_base` (x 235..276). ±200 clears both, by
    26 mm and 27 mm respectively. The seat is the part that is wrong here -- issue
    #107 -- and this is a number chosen to survive until it is fixed.
    """

    steering_support_apex_y: float = 0.262
    steering_support_apex_z: float = 0.393
    """Apex of the upper steering support, where the column passes through a
    20 mm block. `derived` from the column: a column leaving `steering_bore_*`
    at 36° from vertical is at y = 477 - 296 x tan(36°) = 262 when it reaches
    z 393.

    This part does not exist on the built kart and Art. 9.5.3 requires it: the
    front panel's *"upper part must be securely attached to the steering column
    support with one or more independent bars"*. It leans **forward** 34.1° --
    atan((262-40)/(393-65)) -- against the column's 36° rearward, so the two are
    opposed at 70.1° included, which is what makes it a brace rather than a
    parallel tube. Apex 393 + the block is under Art. 9.1.1's 650 mm ceiling.
    """

    # --- bumpers -----------------------------------------------------------
    #
    # The most heavily `sourced` part of the kart, and none of it was in this repo
    # before #190. Art. 9.4.1 dimensions the front bumper as **two bars** with two
    # diameters, two straight lengths, two attachment spacings and two height
    # bands; Art. 9.4.2 does the same for the side bumpers. The article's text is
    # split across a PDF page break and reading only the second page is what
    # produced two wrong "corrections" in this project's history.
    #
    # Every straight length below is the length of the **built** straight run
    # between the two bends, not of a control polyline -- see `bend_radius`.

    nose_lower_straight: float = 0.305
    """Front bumper lower bar, straight run between the bends. `derived`: the
    middle of the **lower** bar's *"295.0 mm minimum and 315.0 mm maximum"* in
    Art. 9.4.1. Built at 330, which was 15 mm over the maximum."""

    nose_lower_z: float = 0.085
    """Front bumper lower bar, centerline height. `derived`: Art. 9.4.1 gives
    *"Height: 70.0 mm minimum and 110.0 mm maximum (measured to the tube top)"*,
    so a Ø20 tube's center sits at 60..100 and 85 is the middle, tube top 95.

    Built at 60, tube top 70, i.e. on the floor of the window. **Do not lift this
    bar to clear a fairing:** a tube center at 150 is a top at 160, which is
    50 mm above this bar's ceiling and 40 mm below the upper bar's floor -- the
    one height band a front bar may not occupy. The 160 mm figure that request
    was justified by is Art. 9.4.2's *side* bumper minimum."""

    nose_lower_mounts: float = 0.450
    """Front bumper lower bar, attachment spacing. `sourced`: Art. 9.4.1,
    *"two welded chassis frame attachments, which must be 450.0 mm apart and
    centred on the kart's longitudinal axis"*. Corroborated by the OTK M4 form's
    Ø20x1.5 pair at 450. Not modeled at all before #190."""

    nose_upper_straight: float = 0.385
    """Front bumper upper bar, straight run between the bends. `derived`: the
    middle of *"375.0 mm minimum and 395.0 mm maximum"*. Built at 280.5, which
    was 94.5 mm under the minimum."""

    nose_upper_z: float = 0.217
    """Front bumper upper bar, centerline height. `derived`: *"Height: 200.0 mm
    minimum and 250.0 mm maximum from the ground (measured to the tubing top)"*,
    so a Ø16 tube's center sits at 192..242 and 217 is the middle, tube top 225.
    Built at 155, tube top 165 -- 35 mm under the minimum.

    The fairing picks up on **this** bar, because the lower one is capped at a
    110 mm tube top."""

    nose_upper_mounts: float = 0.550
    """Front bumper upper bar, attachment spacing. `sourced`: Art. 9.4.1,
    *"550.0 mm apart and centred on the kart's longitudinal axis"* -- on PDF
    p. 22, which is the page a reader who starts at p. 23 never sees. The OTK M4
    form's Ø16x1.5 pair is at 550, so a form and the article agree on four
    numbers across two bars."""

    bumper_insertion: float = 0.050
    """How far a bumper bar sits inside its socket. `sourced`: Art. 9.4.1's
    lower-bar attachments must *"allow for a 50.0 mm insertion of the bar"*, and
    Art. 9.4.2 repeats it for both side bars."""

    front_bumper_support_x: float = 0.075
    """The two posts joining the front bumper's two bars. `estimated`: Art. 9.4.1
    requires the support (*"Both bars must be connected by the front bumper
    support"*) and does not place it."""

    sidebar_x_lower: float = 0.500
    """Side bumper lower bar, outermost tube midpoint from the centerline.
    `sourced` band, `derived` value: Art. 9.4.2's *"Overall width: 480.0 mm
    minimum and 520.0 mm maximum for the lower bar ... (measured to the tube
    midpoint) in relation to the longitudinal axis"*, of which 500 is the middle.

    Read as a **distance from the centerline** rather than as a total width, and
    that reading is `derived`: a total width of 480-520 would put the bar inboard
    of the frame's own 650 mm outer rear width and far inboard of the pod datum
    Art. 9.5.4 makes the bodywork occupy, while the same article requires the
    bodywork to be *"securely attached to the side bumpers"*. Built at 445, which
    is 35 mm inboard of the regulation minimum."""

    sidebar_lower_straight: float = 0.420
    """Side bumper lower bar, straight run. `derived`: Art. 9.4.2's *"Minimum
    straight length is 400.0 mm for the lower bar"* plus 20 mm of margin. The
    built bar had about 1030 mm of continuous curve and no straight at all."""

    sidebar_lower_z: float = 0.080
    """Side bumper lower bar, centerline height. `estimated`: the article gives
    no height for the lower bar. 80 puts it above the rail and below the pod's
    25-60 mm ground-clearance window."""

    sidebar_x_upper: float = 0.560
    """Side bumper upper bar, outermost tube midpoint. `estimated` inside a
    `sourced` window: Art. 9.4.2 allows 480.0-600.0 mm for the upper bar, so 560
    keeps 40 mm to the cap and stands 60 mm outboard of the lower bar, which is
    what makes a pod flare rather than sit vertical."""

    sidebar_upper_straight: float = 0.320
    """Side bumper upper bar, straight run. `derived`: *"300.0 mm for the upper
    bar"* plus 20 mm of margin."""

    sidebar_upper_z: float = 0.175
    """Side bumper upper bar, centerline height. `derived`: Art. 9.4.2's
    *"Height of the upper bar: 160.0 mm minimum from the ground (measured to the
    tube top)"*, cleared by 25 mm at a Ø20 tube's top of 185.

    **This is the article the 160 belongs to.** It was once attributed to the
    front bumper, where it is illegal at any diameter."""

    sidebar_mount_pitch: float = 0.500
    """Fore-aft spacing of each side bar's two welded attachments. `sourced`:
    Art. 9.4.2, *"two welded tube attachments that must be 500.0 ± 5 mm apart
    (measured to the tube midpoint)"*. Not modeled before #190."""

    sidebar_mount_front_y: float = 0.400
    """Station of the forward pair of side-bumper attachments. `estimated`, and
    the choice is a packaging measurement -- Art. 9.4.2 fixes the 500 mm pitch and
    nothing fixes where the pair sits.

    Both pairs land on the rail centerline, so their x follows from the rail path
    rather than being authored: 172 at the front and 310 at the rear. Everything
    else about the pair is what the *built* kart leaves free, measured:

        y +190 / -310   the obvious pair, and `engine_mount_plate` spans
                        y -305..-165 at x 230..322, so the rear socket is inside
                        the engine mount
        y +160 / -340   `engine_crankcase_lower` reaches y -340 at x 245..393
        y +400 / -100   clear of both, of the floor tray (whose edge is the rail
                        centerline from y +40 to the waist), and of the exhaust
                        silencer at x 284..377, y +218..+356

    The rear socket at y -100 still passes through the radiator's low tank on the
    kart's left -- see `joints.py`. That is not solvable by moving the pair: the
    tank reaches x -497.5 at y -96..-144 and the front tire blocks x 500 forward
    of y +385, which leaves **481 mm** of clear rail for an attachment pitch of
    500 ±5. Spec §30.7 re-places the radiator."""

    sidebar_straight_center_y: float = 0.100
    """Fore-aft center of both side bars' straight runs. `estimated`, and bounded
    at both ends by a wheel.

    A bar at x 500 is inside the front tire's disc wherever
    `(525 - y)^2 + (140 - z)^2 < 140^2`, which at z 80 means y > 398.5 -- so the
    lower bar's forward bend, tube included, has to stay behind y +388. Centering
    the 420 mm straight at +100 puts the bends at +382 and -185, i.e. 16 mm clear
    of the front tire and 203 mm clear of the rear one."""

    rear_bumper_z: float = 0.140
    """Rear bumper top bar, centerline height. `estimated`, with its reasoning:
    the KG C2 rear protection is 177 mm tall with its lower edge in Art.
    9.5.5.1's 25-60 mm window, so it spans z 40..217 and its mid-height is 128.
    140 is within 12 mm of the panel's own middle and 155 mm below the rear
    tire's top, so *"no higher than the rear wheels"* has margin at the tube as
    well as at the panel."""

    rear_bumper_half: float = 0.310
    """Rear bumper half-width. `derived`: it matches the rail ends, so the hoop's
    legs weld on without a jog."""

    # --- floor tray --------------------------------------------------------
    #
    # Art. 4.6, PDF p. 10, is normative and was not cited anywhere in this repo:
    # *"It is mandatory to have a floor tray made of rigid material stretching
    # from the central strut to the front of the chassis frame. The floor tray
    # must fit completely within the perimeter formed by the main tubes ...
    # without protruding beyond the central axis of the tubes seen from the top."*
    #
    # The built tray ran y +180 back to -580 -- 580 mm of it behind the origin,
    # under the engine bay and out past the rear axle, which is what made
    # `powertrain._engine_mount` give up its inboard clamp.

    tray_thickness: float = 0.004
    """Aluminium floor pan. `estimated`: Art. 4.6 requires only that the
    thickness be *constant*, and 4 mm is what the reference karts run."""

    tray_rear_y: float = 0.040
    """Rear edge of the tray. `sourced` through Art. 4.6 plus the identification
    of `cross_strut_y` as the central strut: the article's own enumeration --
    *"the central strut, the longitudinal tubes and the front of the chassis
    frame"* -- describes a three-sided perimeter, so the strut closes it at the
    back and the tray is entirely forward of it."""

    tray_front_y: float = 0.760
    """Front edge of the tray, i.e. the front of the chassis frame. `sourced`
    through Art. 4.6 and `G2`. It was 0.180, and with a `tray_length` of 0.760
    that ran the pan from +180 to -580.

    **`tray_width` and `tray_length` are deleted.** The tray's half-width is a
    function of y -- Art. 4.6 bounds it at the rail centerline, and the rails are
    not parallel -- so an hourglass cannot be described by two scalars. 560 mm of
    width is right only at the strut and is 141 mm per side too wide at the
    waist, where the pan would hang outboard of the tubes and fail the article.
    """

    tray_hole_diameter: float = 0.033
    """Access aperture in the pan. `derived` from a `sourced` cap: Art. 4.6 allows
    *"two holes with a maximum diameter of 35 mm ... for steering column and/or
    gear shift lever access"*, and 33 is that less a millimeter of chamfer per
    side.

    The millimeter is measured rather than guessed. `build.bevel_object` chamfers
    the aperture's lip along with every other hard edge -- 1.5 mm of offset at low
    detail and 4.0 at high -- and a hole cut at exactly 35.0 then measures **36.2
    mm across its mouth** at high detail, over a regulation maximum, while its
    through-bore is still 35.0. Sitting exactly on a limit and letting a modeling
    pass push you over it is the same class of defect as a bumper bar whose
    control polyline is the regulated length: the number that matters is the one
    on the built mesh. At 33 the bore measures 33.0 and the mouth 34.2, and a Ø20
    column still has 6.5 mm of radial clearance.

    One is built, on the centerline at `steering_bore_y`, and that is deliberate
    rather than half-finished: the shift rod crosses the rail at x 285, y +88,
    where the pan's own edge is at 266, so the rod passes 19 mm outboard of the
    pan and needs no aperture. The article permits two and this kart needs one.
    """

    # --- wheels and tires --------------------------------------------------

    rim_bead_diameter: float = 0.1262
    """Bead coupling diameter -- where the tire's bead sits. `sourced`, and a fit.

    Art. 8.7: *"In Group 1, only 5-inch rims are allowed with CIK-FIA homologated
    5-inch tyres."* So for a KZ, 5 inch is not merely typical -- it is the only
    legal size.

    Art. 4.14, PDF p. 13: *"Coupling diameter of the tyre for the rim: 126.2 mm
    with a +0/-1 mm tolerance for the diameter."* The tolerance direction makes
    126.2 a **maximum**, so the single `rim_diameter = 0.127` this replaces was
    0.8 mm over a fit rather than 9 mm under a flange. Spec §20.2.2 measured that
    and corrected `notes_running.md` on it.
    """

    rim_flange_diameter: float = 0.1362
    """Flange external diameter -- the part of the rim anyone can see. `sourced`.

    Art. 4.14: *"External diameter for 5-inch rims: 136.2 mm minimum."* Built to
    the floor. `wheels.RIM_FLANGE_LIP` is now `(flange - bead) / 2` = 5.0 mm
    rather than an authored 6.0, which is where the old build's accidentally-legal
    Ø138.9 lip came from. Spec §20.2.2.
    """

    rim_front_width: float = 0.120
    rim_rear_width: float = 0.198
    """Rim width, flange to flange. `sourced`: CIK tire homologation forms
    `047-TO-12` and `047-TO-14` p. 3, the dimensioned cross-section of the tire
    fitted to its rim, +-5 mm.

    They exist because the tire's three widths are not one number: the rear is
    207 overall on 198 of rim carrying 179 of tread, so **the sidewall stands
    4.5 mm proud of each flange and the tread band tucks 9.5 mm inboard of it**.
    The built rim used to be sized off the tire's bead inset instead, which is a
    shoulder-radius accident rather than a measurement."""

    tire_front_diameter: float = 0.280
    """`estimated`, and it is the Art. 4.13.1 **wheel** ceiling rather than a
    tire: that article's own footnote says *"maximum wheel dimensions, with a
    matching tyre fitted on the rim and an air pressure of 0.5 bar"*, and Art.
    2.3.2 defines a wheel as rim plus mounted tire. Art. 4.15 *Tyres* is one
    sentence and contains no dimension at all.

    The sourced figure is **260 mm**: CIK tire homologation form `047-TO-12`
    (Vega XH4, Groups 1 & 2) p. 3, ±5 mm. Frozen at 280 because every §6.4
    driving figure, every `drive.sh` scenario and the whole M3a/M3b tire model
    are measured against it -- a 20 mm diameter change moves the rolling radius,
    the axle heights, the gearing and the center of mass together. Issue #196.
    """

    tire_front_width: float = 0.135
    """`estimated`: the Art. 4.13.1 *wheel* ceiling. The sourced tire is 130 mm
    overall on 120 mm of rim (`047-TO-12`)."""

    tire_rear_diameter: float = 0.295
    """`estimated`, and the worst-labeled figure in this block: 295 is **neither
    the Art. 4.13.1 maximum (300) nor any real tire**. The sourced Vega XH4 rear
    is 274 mm (`047-TO-14` p. 3). Frozen, issue #196."""

    tire_rear_width: float = 0.215
    """`estimated`: the Art. 4.13.1 *wheel* ceiling of 215 mm, against a sourced
    tire of 207 mm overall on 198 mm of rim. The rears being visibly fatter than
    the fronts is a large part of reading as a kart rather than as a small car,
    and that survives the relabel."""

    tire_front_tread_width: float = 0.110
    tire_rear_tread_width: float = 0.179
    """Width of the flat tread band. `sourced`: `047-TO-12` / `047-TO-14` p. 3.

    Authored rather than left to fall out of the shoulder radius. Measured on the
    old build, the flat band was `2 x (width/2 - TIRE_SIDEWALL_LEAN -
    tire_shoulder_radius)` = **163 mm** at the rear against a sourced 179 and
    **83 mm** at the front against a sourced 110 -- so a taste constant was eating
    16 mm and 27 mm of the one surface that touches the road. Spec §20.9 item 6.
    """

    tire_sidewall_bulge: float = 0.038
    """How far the sidewall stands proud of the **bead seat**, radially.
    `derived`.

    Measured so that the tire's widest point sits at radius
    `rim_bead_diameter / 2 + tire_sidewall_bulge`, from which the profile turns
    back in to meet the bead. Stated precisely because "the bulge" has two
    plausible readings -- radial and axial -- and they produce different tires.

    Was 0.008, i.e. a widest point at radius 71.5 mm. `047-TO-14` p. 3 puts it at
    **101 mm** (`derived` in `refs/kart-visual/notes_running.md`: 63.1 + (950-632)
    px / 8.34 px/mm), so the old figure was 29.5 mm too low radially and the tire
    read as a cylinder with a chamfer. 101.0 - 63.1 = 37.9 -> 0.038. The front
    agrees: mid-sidewall on a Ø260 tire over a Ø136 flange is (130 + 68)/2 = 99,
    so one widest-point radius serves both ends. Spec §20.2.3; no physics reads
    it.
    """

    tire_shoulder_radius: float = 0.022
    """Tread-to-sidewall corner radius. `estimated`. Kart slicks have a soft
    shoulder and a square one reads as a toy.

    It no longer sets the tread band's width -- `tire_*_tread_width` does, and the
    shoulder is fitted between that band and the sidewall."""

    tire_segments: int = 32
    tire_segments_high: int = 64

    # --- rear axle ---------------------------------------------------------

    stub_axle_length: float = 0.090
    """Kingpin to front hub center. `estimated`.

    Shared: `wheels.py` needs it to place the stub. It was duplicated as a
    literal in two modules before it was hoisted here, which is precisely the
    drift this parameter block exists to prevent.

    **It no longer positions the frame.** `frame.py` used to derive the kingpin
    from `front_hub_x - 0.090`, which put the kingpins 925 mm apart; the kingpin
    is now `kingpin_x`, sourced three ways, and the 142.5 mm that this parameter
    cannot account for is spec §10.5's open item rather than a frame number.
    """

    axle_diameter: float = 0.050
    """Rear axle **outside** diameter. `estimated` value on a `sourced` ceiling.

    Art. 9.2, PDF p. 22: *"Maximum 50.0 mm outside diameter (wall thickness
    according to Article 4.3)."* A wall-thickness clause means the axle is a
    **tube**; this docstring read *"Solid, 50 mm"* for two milestones, which is a
    real error and not a wording one. Art. 4.3's table gives 1.9 mm as the floor
    at this diameter. Sitting on the 50 mm cap is a practice claim nobody has
    sourced, so the value is estimated.

    ARCHITECTURE.md §6: both rear wheels are locked to it, and that is the kart's
    defining dynamic.
    """

    axle_wall: float = 0.0025
    """Rear axle wall thickness. `estimated` on a `sourced` floor.

    Art. 4.3's table, PDF p. 9, sets **1.9 mm minimum** at 50.0 mm outside
    diameter, and its own last row (*">28.0 -> full"*) shows solid construction is
    for the small diameters only. KZ axles are sold soft/medium/hard by wall and
    2.5 is mid-range; it also leaves 0.6 mm over the floor for the four keyways,
    which the table exempts but which cannot be cut into 1.9 mm of wall without
    going through. Spec §20.5.

    A solid 50 mm steel axle 1.185 m long is 18.3 kg against 3.5 kg at this wall,
    on a kart with a 170 kg minimum (Art. 8.9) -- so the old *"Solid, 50 mm"*
    docstring was a real error and not a wording one."""

    axle_length: float = 1.185
    """`derived`: 2 x `rear_hub_x` = 2 x 592.5, so each end lands flush with its
    rim's mounting plane.

    Was 1.080, which stopped **52.5 mm short** of each wheel centerline: a 90 mm
    rear hub was supported over 37.5 mm of its bore and Art. 4.3's hub keyway sat
    20 mm from the axle's own chamfered end. Spec §20.9 item 3."""

    # --- seat --------------------------------------------------------------

    seat_width: float = 0.330
    seat_height: float = 0.290
    seat_back_angle: float = 0.610
    """Radians from vertical, ~35 deg. `estimated`. A KZ seat is reclined hard.

    **Nothing outside `cockpit.py` may read this.** The radiator's rake used to
    be `seat_back_angle + radiator_rake_delta`, on the claim that the core sits
    in the plane a second seat's back would occupy -- which spec §30 measured
    false: the core rakes 45° from vertical and the shell's chord is 22°. Two
    parts sharing one angle meant that fixing the seat would silently tip the
    radiator, and no gate measures a rake. `radiator_rake` is now its own number.
    """

    seat_y: float = -0.060
    """Seat center, rearward of the origin. This sets where the driver's mass
    sits, and ARCHITECTURE.md §6 wants the center of mass slightly rearward."""

    seat_z: float = 0.075
    seat_thickness: float = 0.008
    """Fiberglass seat shell."""

    # --- steering ----------------------------------------------------------

    wheel_diameter: float = 0.320
    """Steering wheel, outside diameter. `sourced` size, `derived` choice: kart
    wheels sell at 280/300/320/340 and the side-view trace scales to 306 mm,
    which picks 320 out of that list."""

    wheel_rim_thickness: float = 0.024
    wheel_angle: float = 0.470
    """Radians from vertical, ~27 deg. `estimated`. Kart steering columns are
    steeply laid back, and this angle is very visible from the cockpit. Spec §40
    measures the column at 36° and owns the change."""

    wheel_center_z: float = 0.480
    wheel_center_y: float = 0.320
    column_diameter: float = 0.018
    """`sourced` floor: Art. 4.5.2 sets 18.0 mm as a minimum and this is built to
    the floor. Spec §40 measures the real column at 20.0 mm."""

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
    """Overall envelope of the engine cluster: `width` across the kart, `length`
    fore-and-aft, `height` measured up from the **crankcase underside**, not from
    the ground and not from `engine_z`.

    Spelled out because it was ambiguous and a module had to guess. Read as a
    bounding-box half-height about `engine_z`, the crankcase underside lands at
    z = 0.000 — 35 mm below the frame rails, i.e. dragging on the asphalt."""

    engine_x: float = 0.319
    """Engine center, to the kart's right. A KZ carries its engine on the
    driver's right, and the resulting mass asymmetry is real — issue #15.

    Was 0.240, which put the crankcase's inboard face at x = 0.125 — 39 mm
    inside the seat shell's outer edge and through both right-hand seat struts.
    `powertrain.py` never honored it; it built the cases against the clearances
    they actually have and reported the disagreement, which is issue #111. 0.319
    is the midpoint of the faces it builds, so the parameter now describes the
    engine rather than contradicting it."""

    engine_y: float = -0.190
    engine_z: float = 0.150
    """Height of the **crankshaft axis**, not of the engine's bounding-box
    center. This is the datum the chain line is built from: the engine's output
    sprocket and the rear axle sprocket have to be coplanar and at compatible
    heights, and that is a statement about shaft centers."""

    exhaust_length: float = 0.620
    exhaust_max_diameter: float = 0.130
    """Expansion chamber, at its widest. Visually distinctive and a big part of
    a shifter kart's silhouette. Spec §30.6.2 sources the whole 15-cone table off
    homologation form `041-EZ-75` and replaces both of these; §Powertrain owns
    it."""

    exhaust_pipe_diameter: float = 0.034
    exhaust_segments: int = 16
    exhaust_segments_high: int = 32

    radiator_width: float = 0.265
    """Core width **across the kart**, laterally.

    Read the three radiator dimensions against each other, because none of them
    is an axis-aligned extent and every earlier attempt at this block got one of
    them wrong:

        width       across the kart, from the seat shell outward
        height      up the **slant**, low edge forward, high edge rearward
        thickness   through the core, along the face's own normal

    A New-Line core is 17 in by 9.5-11.4 in, so 432 mm by 241-290 mm. 265 mm is
    the short dimension and it is the lateral one. It fits the gap it has to fit
    almost exactly: the seat shell's right edge is at x 0.165 and the right side
    bar's inner face at x 0.432, which is 267 mm. Spec §30.7 sources 250 mm off
    the EM-01 core and §Powertrain owns the change."""

    radiator_height: float = 0.432
    """Core length **up the slant**, tanks included — 17 in.

    Not a vertical height: raked back, 432 mm along the slant is 354 mm of rise
    and 248 mm of fore-and-aft run at this rake. The tanks are at the two ends
    of it, low-forward and high-rearward, with the tubes running between them,
    which is why this is the tube direction and `radiator_width` is the one the
    fins repeat along."""

    radiator_thickness: float = 0.040
    """Core depth through the face — see `radiator_width`."""

    radiator_x: float = 0.365
    """Lateral center, as a **magnitude**: which side it lands on is
    `powertrain.RADIATOR_SIDE`, and the radiator is on the kart's **left**.

    Was 0.308 on the right, which is where the engine is (`engine_x` 0.319). The
    two were 11 mm apart and the radiator was built through the exhaust, through
    the gear lever and through the right sidepod — 593 intersecting triangle
    pairs in total, all one bug.

    0.365 is measured off V4, the plan-view reference, scaled against the
    steering wheel: 320 mm of `wheel_diameter` covers 310 px in that frame, so
    1.032 mm/px. The core reads 242 mm from the centerline at its inboard edge
    and 500 mm at its outboard, a 258 mm span that is `radiator_width` 265 to
    within the measurement error — the size was right and only the placement was
    wrong."""

    radiator_y: float = -0.235
    """Fore-aft center. `estimated`. The core spans y -0.076 to -0.426, beside
    the seat and well behind the kart's mid-point, which is inside Art. 5.3.1's
    window of 10 to 550 mm ahead of the rear axle. It was 0.000, i.e. 250 mm too
    far forward, which put it level with the driver's knees instead of the
    driver's hip."""

    radiator_z: float = 0.320
    """Center of the core, in the plane it is raked into. Chosen so the low edge
    clears the floor tray's top and the high edge stays forward of the engine's
    front face at y -0.145 — the radiator goes **beside the driver and ahead of
    the engine**, which is the constraint that actually places it. Spec §30.7.2
    derives 0.240 and §Powertrain owns the change."""

    radiator_rake: float = 0.610
    """Radians from vertical, and **its own number**.

    This replaced `radiator_rake_delta`, which was *added to `seat_back_angle`*
    on the claim that the core sits in the plane a second seat's back would
    occupy. Spec §30 measured that false: the core rakes **45° from vertical**
    (0.7854 rad) and the seat shell's chord is 22°. So the two parts were sharing
    an angle they do not share, and because **no gate measures a rake**, fixing
    the seat would have tipped the radiator 13° with nothing objecting. Decoupling
    it and correcting the seat had to land together, and this is the decoupling.

    The value here is the rake **as built** (0.610 = 34.95°), not §30.7's sourced
    0.7854, deliberately: §Powertrain re-places the whole core -- `radiator_z`
    0.320 -> 0.240, `radiator_width` 0.265 -> 0.250, the brackets onto the rail --
    and tipping it 10° on its own would land in none of those positions. This
    field is where that change goes, in one place, with the seat no longer in it.

    Kart practice tunes the angle with ambient temperature -- 45° to horizontal
    below 20 °C, 55 at 20-30, up to 60 above -- in 5-degree steps, so
    `--set=radiator_rake=0.698` is one step nearer vertical.
    """

    # --- bodywork ----------------------------------------------------------

    panel_thickness: float = 0.0038
    """Wall thickness of every thermoformed panel. `derived`, spec §50.6.

    Was `bodywork.PANEL_THICKNESS` = 3.0 mm, `estimated` from "CIK bodywork is
    thermoformed about 3 mm of polyethylene". Three homologation forms publish a
    mass, and mass over density times developed area is a thickness:

        t = mass / (950 kg/m3 x developed area)
        OTK M4 fairing   1500 g  1090 x ~380 arc = 0.414 m2  ->  3.81 mm
        KG 505 fairing   1600 g  1029 x ~420 arc = 0.432 m2  ->  3.90 mm
        KG C2 rear prot  1450 g  1360 x ~300 arc = 0.408 m2  ->  3.74 mm

    Three independent masses landing inside 0.16 mm of each other. The arcs are
    `estimated` off the forms' side elevations at about +-10%, which is +-0.4 mm,
    and blow-moulded wall is not uniform -- so this is the mean wall and not a
    caliper reading. It is not 3.0."""

    nose_width: float = 1.090
    """Nose fairing overall width. `sourced`: OTK M4 homologation form
    `100/CA/20` p. 2, a dimensioned drawing read at 200 dpi.

    Art. 9.5.2 sets *"Minimum width: 1.000 mm"* and caps the maximum at the
    overall rear width of the front wheel/front axle unit, i.e. `track_front` =
    1240 -- so 1090 clears the minimum by 90 and the maximum by 150.

    It was 0.680 while `bodywork.NOSE_HALF_WIDTH_LIMIT` clamped the built panel to
    a half-width of 0.256: the parameter said 680 mm, the mesh was 512 mm wide,
    and the two disagreed by 168 mm for two milestones without a gate noticing.
    The clamp is gone -- the fairing picks up on the front bumper's **upper** bar,
    which is the one in a height band that can carry it, so the lower bar's dive
    to rail height no longer bounds the panel."""

    nose_height: float = 0.227
    """Fairing overall height at the spine. `sourced`: same OTK M4 drawing, front
    elevation. Was 0.130. `nose_bottom_z` + this is 267, which is 13 mm under the
    front tire's top at 280 -- Art. 9.5.2's *"must be placed no higher than the
    front wheels"*."""

    nose_depth: float = 0.287
    """Fairing fore-aft depth at the centerline. `sourced`: same drawing, plan
    view. The KG 505 form gives 317 on the same field, and 287 is the shallower of
    the two homologated panels -- which is what keeps the front overhang at 504
    rather than 534."""

    nose_bottom_z: float = 0.040
    """Fairing bottom edge at the centerline. `estimated`: 5 mm above the rails'
    underside at 35, which are the lowest thing on the kart
    (`ground_clearance`). Was `bodywork.NOSE_BOTTOM_Z[0]` = 46; 40 buys the
    fairing's top edge 6 mm against Art. 9.5.2's front-tire-top ceiling."""

    front_panel_width: float = 0.275
    """Front panel (nassau panel) width. `derived`: the midpoint of Art. 9.5.3's
    *"Width: 250.0 mm minimum and 300.0 mm maximum"*, so +-25 mm either way.

    **The part did not exist at all.** Art. 4.10.1 lists it as one of the six
    homologated bodywork items and this kart had four panels."""

    front_panel_top_z: float = 0.500
    """Front panel top edge. `derived`, and the derivation is the regulation:
    Art. 9.5.3 forbids the panel *"above the horizontal plane defined by the top
    of the steering wheel"*. That top is z **552.5** -- `wheel_center_z` 480 plus
    `wheel_diameter`/2 x cos(`wheel_angle`) = 160 x cos(0.470) = 72.5, because a
    wheel raked 26.9° from vertical has its highest rim point leaning forward. 500
    is 52.5 mm under it."""

    front_panel_bottom_z: float = 0.190
    """Front panel bottom edge. `estimated`: above the pedal pads at `pedal_z`
    90, so Art. 9.5.3's *"must not impede the normal functioning of the pedals or
    cover any part of the feet"* is satisfied by there being no panel at foot
    height at all."""

    sidepod_length: float = 0.595
    """Fore-aft length of a side pod. `derived`: 265 - (-330).

    **Was 0.560, and that was non-compliant** -- a rear edge at -295 leaves an
    82.5 mm gap to the rear tire's forward face at -377.5, against Art. 9.5.4's
    *"Gap between the back of the side bodywork and the rear wheels: 60.0 mm
    maximum"*. Illegal by 22.5 mm, and it was a sixth undersize panel nobody had
    listed. At 595 the gap is 47.5, 12.5 under the cap."""

    sidepod_height: float = 0.180
    """Height of the pod's section at its deepest station. `estimated`, and it is
    what sets kart bodywork height generally: Art. 3.7 makes a compliant
    three-digit racing number 150 mm tall with a >=10 mm border, i.e. 170 mm, and
    Art. 9.5.4 requires *"a space for racing numbers ... on the vertical surface
    close to the rear wheels"*. 180 leaves 10 mm. The KG C2 rear protection is
    177 on the same reasoning."""

    sidepod_front_y: float = 0.265
    """Forward edge of the pod. `sourced` in range: Art. 9.5.4 caps the gap to
    the front wheels at 150 mm and the front tire's rear face is at y +385, so 265
    leaves 120. Hoisted out of `bodywork.SIDEPOD_FRONT_Y`."""

    sidepod_datum_x0: float = 0.660
    sidepod_datum_slope: float = 0.0762
    """Art. 9.5.4's datum plane, as `x_datum(y) = x0 - slope * y`. `derived`.

    The article puts it through *"the outer front edge of the front wheel and the
    outer front edge of the rear wheel (with the front wheels in the
    straight-ahead position)"*, and the pod's outer face must lie between
    `x_datum(y) - 40` and `x_datum(y)`. With `track_front` 1240 and `track_rear`
    1400 the two datum points are x 620 at y +525 and x 700 at y -525:

        x_datum(y) = 660 - (y / 1050) x 80,  plan angle atan(80/1050) = 4.36 deg

    **`sidepod_x` = 0.480 was a single constant, which this article forbids**: one
    number cannot be right at both ends of a tapering plane, and 482 was 136-182 mm
    inboard of where the face belongs. Real pods splay outward toward the back for
    this reason, which had been read as styling.

    Read the article's words literally and the datum points are the tires'
    forward-most faces at y +665 and -672.5, which gives `671.0 - 0.0767 y` --
    the same plan angle to within 0.03° and uniformly **11 mm further outboard**.
    So a face 40 mm inboard of this datum is 51 mm inboard of the literal one and
    illegal under it: **the usable inset budget is 29 mm, not 40**, and
    `sidepod_inset` plus `bodywork.SIDEPOD_TAPER` are specified against that
    narrower band."""

    sidepod_inset: float = 0.008
    """Base inset of the pod's outer face inboard of `sidepod_datum_x0`.
    `estimated` inside the 0-29 mm band above; the fore-aft taper adds up to
    14 mm more in **millimeters**, never as a fraction of the face position.

    That distinction is the trap `SIDEPOD_OUT_FRACTION` fell into: its taper was
    0.960 at the front and 0.962 at the rear, read as a fraction, and 4% of 640 is
    26 mm -- which plus any base inset walks the face out of a 40 mm band. As
    millimeters the same visual taper is bounded by construction."""

    sidepod_mouth_x: float = 0.505
    """Lateral station of both of the pod's free edges -- the mouth of the C.
    `estimated`, with a measured floor.

    The radiator's outboard extremity is at x 489 (spec §00 §5a, measured off the
    built mesh; photogrammetry on `crg_roadrebel_kz_front.webp` gives 479 and
    `radiator_width`/2 about `radiator_x` gives 497.5). 505 clears the built figure
    by 16 mm. Cross-checked photogrammetrically at 519 +-10 on the same image,
    where the pod's top lip stands visibly outboard of the radiator's outer face.

    Was `bodywork.SIDEPOD_TOP_X` = 0.372, set against a comment reasoning about a
    radiator *"at x = 0.330 with a 45 mm core"* -- a radiator that has not been
    there for two commits and was never on that side. That comment is why the
    engine crankcase was built 26.7 mm inside the right pod."""

    rear_prot_width: float = 1.390
    """Rear wheel protection, overall width across all three parts. `derived`
    from a `sourced` shape plus two `sourced` limits.

    Art. 9.5.5.1: *"Width: minimum 1.340 mm, maximum that of the overall rear
    width"*. The KG C2 form `003-BR-48` measures 1360, and 1360 on **this** kart
    cannot present a 200 mm clearance window under a rear wheel: an edge at 680
    against the tire's inner edge at 485 leaves 195, five short. 1390 gives 210
    with 10 mm still under the 1400 ceiling, and it keeps the protection *"in line
    with the outside of the rear wheels"* -- 5 mm inboard of the tire's outer plane
    at 700.

    Was `bodywork.REAR_HALF_WIDTH` x 2 = **572 mm**, i.e. 768 mm inside a
    regulation minimum."""

    rear_prot_depth: float = 0.187
    rear_prot_height: float = 0.177
    """Rear wheel protection depth and height. `sourced`: KG C2 homologation form
    `003-BR-48` p. 2 drawing. The 187 is also the depth `overhang_rear_protection`
    is derived from, so the two cannot drift."""

    rear_prot_bottom_z: float = 0.040
    """Bottom edge of the protection **inside its three clearance windows**.
    `sourced` in range: Art. 9.5.5.1 wants 25-60 mm there, and 40 is the middle.
    Between the windows the panel lifts to 95, which is what makes them
    windows."""

    # --- driver ------------------------------------------------------------
    #
    # No Blender module reads any of these yet -- issue #17's driver is not
    # written -- so all six are on `FIELD_COVERAGE_EXEMPT`. Two of them are read
    # in Godot, off the manifest, by scripts/look/kartview.gd's cockpit camera.

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
    radius, not something to discover in the viewport. Spec §60 measures them
    175 mm short of the wheel and owns the fix."""

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
    primary LOD mechanism.

    **Read by nothing**: `kartlib/lod_stage.py` does not exist, so the `lod`
    stage is skipped on every run. Exempt from the coverage gate with that named
    as the reason, and it is a fourth instance of the class spec §10.7 found
    three of."""

    def scaled(self, **overrides: float) -> "KartParams":
        """A variant with named fields replaced, for parameter sweeps."""
        return dataclasses.replace(self, **overrides)

    def as_ordered_items(self) -> list[tuple[str, object]]:
        """Fields in declaration order, for the manifest and the caption.

        Declaration order rather than sorted order, so the manifest reads like
        the kart is built rather than like an alphabetized dump.

        `computed_figures()` is appended, so a figure the kart is *measured*
        against rather than built from still reaches the manifest -- which is how
        `length_overall` stopped being an input without issue #21 losing the
        number it checks.
        """
        fields = [(f.name, getattr(self, f.name)) for f in dataclasses.fields(self)]
        return fields + sorted(computed_figures(self).items())


DEFAULT = KartParams()
"""The kart. Anything wanting a different one derives it with `.scaled()`."""


# --- computed report figures -----------------------------------------------


def computed_figures(p: KartParams) -> dict[str, float]:
    """Figures the kart is measured against rather than built from.

    `length_overall` is the whole reason this exists. It was an input, it was
    sourced nowhere, it was labeled "max", and both halves of it are *bodywork*
    depths -- so a frame tube placed from it was a frame dimension fitted to a
    bodywork envelope, which is the inversion issue #190 exists to stop. It is
    now arithmetic on five single-owner parameters, and issue #21's Godot check
    compares the *measured* mesh against it instead of reading a parameter back
    to itself.

        length_overall = (525 + 504) + (525 + 367) = 1,921 mm

    Spec §5 rounds that to 1,920; this reports the arithmetic rather than the
    rounding, because a report figure that has been rounded is a figure a Godot
    check has to be given a tolerance for twice.

    There is no symmetric value that works. The two ends want 504 and 367, which
    differ by 137 mm, so any symmetric split hands each end 435.5 -- and Art.
    9.5.5.1 caps the rear at 400.
    """
    half = p.wheelbase * 0.5
    return {
        "length_overall": round(
            (half + p.overhang_front_fairing) + (half + p.overhang_rear_protection), 4
        ),
        "frame_length_overall": round(
            (half + p.overhang_front_frame) + (half + p.overhang_rear_frame), 4
        ),
        "bumper_length_overall": round(
            (half + p.overhang_front_bumper) + (half + p.overhang_rear_frame), 4
        ),
    }


# --- the coverage gate -----------------------------------------------------
#
# Spec §10.7's recommendation, adopted: a field no module reads is a comment
# wearing a number's clothes, and it drifts away from the mesh in silence.
# `frame_height` was read by nothing until a seat strut was pointed at it;
# `nose_width` has disagreed with its own mesh by 168 mm for two milestones;
# `tray_width` and `tray_length` described a rectangle Art. 4.6 forbids;
# `lod_ratios` is read by a stage module that does not exist.
#
# Fatal, like the signed-volume winding assert, and for the same reason: this is
# not a defect a render can show you.

#: Fields no geometry module reads, and why each one is allowed to stay.
#:
#: One entry per field with its own reason -- never a blanket skip, and never a
#: pattern. Anything added here is a promise that the number has a consumer
#: outside this package or a ticket that will give it one.
FIELD_COVERAGE_EXEMPT: dict[str, str] = {
    "frame_height": (
        "ARCHITECTURE.md §5 names it and scripts/look/lookdev.gd carries a "
        "hardcoded twin (KART_FRAME_HEIGHT = 0.28) for its reference box. No "
        "Blender module reads it since the seat stays got their own ear points "
        "in spec §10.9. Kept so the manifest publishes the figure; the real fix "
        "is lookdev.gd reading the manifest instead of restating it."
    ),
    "lod_ratios": (
        "issue #20: kartlib/lod_stage.py does not exist, so the lod stage is "
        "skipped on every run and nothing consumes the ratios. Delete this "
        "entry when #20 lands."
    ),
    # `overhang_front_fairing` and `overhang_rear_protection` were exempt here
    # with the note "delete this entry when bodywork.py places the fairing from
    # it". `bodywork.py` now does, through `nose_apex_y()` and
    # `rear_prot_front_y()`, so both entries are gone -- an exemption that has
    # stopped being needed is the same defect as a waiver that has stopped
    # failing, and `check_field_coverage` is fatal on it.
    # The six below are the coverage gate's own findings: spec §10.7 named three
    # dead parameters and there were nine. Every one of these is *restated as a
    # literal* inside the module that should be reading it, which is worse than
    # being unread -- it is the same number in two places with nothing comparing
    # them. Waves 3 and 4 own the modules, so they are recorded here rather than
    # deleted, and the entry says what the duplicate is.
    "engine_width": (
        "powertrain.py's docstring claims to read it and does not; the crankcase "
        "is built from CRANKCASE_INBOARD_X/CRANKCASE_OUTBOARD_X instead, which "
        "is issue #111. Spec §30 owns the reconciliation."
    ),
    "engine_length": "issue #111, as engine_width: no module reads it.",
    "engine_y": (
        "issue #111: powertrain.py restates it as literals -- SPROCKET_Y and the "
        "cylinder stations are authored absolutely rather than off the engine's "
        "own datum."
    ),
    "engine_z": (
        "issue #111: powertrain.SPROCKET_Z = 0.150 is this field written out as a "
        "literal, and its own comment says so -- *\"SPROCKET_Z is engine_z\"*. "
        "Two copies of the crankshaft height with nothing comparing them."
    ),
    "exhaust_max_diameter": (
        "powertrain.py folds it into a ratio constant (*\"3.824 is "
        "exhaust_max_diameter / exhaust_pipe_diameter\"*) rather than reading it. "
        "Spec §30.6.2 replaces both with the 15-cone table off form 041-EZ-75."
    ),
    "pedal_length": (
        "cockpit.py restates it as PEDAL_ARM_LENGTH = 0.120. Spec §40.5 "
        "respecifies the whole pedal box and owns the join."
    ),
    "driver_eye_z": (
        "issue #17: no driver module exists. Read in Godot off the manifest by "
        "scripts/look/kartview.gd's cockpit camera and by lookdev.gd."
    ),
    "driver_shoulder_z": (
        "issue #17, as driver_eye_z; kartview.gd derives the cockpit camera's "
        "recline from it."
    ),
    "driver_shoulder_span": "issue #17: no driver module exists.",
    "driver_helmet_radius": "issue #17: no driver module exists.",
    "driver_upper_arm": "issue #17: no driver module exists.",
    "driver_forearm": "issue #17: no driver module exists.",
}

#: How a module reads a parameter. `p.foo`, `params.foo`, `context.params.foo`,
#: `P.rail_z(p)` -- all of them end in an attribute access on something whose
#: name ends in `p` or `params`, so one pattern covers the package.
_READ_PATTERN = re.compile(r"\b(?:p|params|parameters)\.([A-Za-z_][A-Za-z0-9_]*)")


def _module_sources(package_directory: str) -> dict[str, str]:
    """Every module in this package except this one, keyed by file name.

    Sorted, because the failure message names the modules and `genkart.sh
    --check` compares two runs of everything this pipeline prints.
    """
    sources: dict[str, str] = {}
    for path in sorted(glob.glob(os.path.join(package_directory, "*.py"))):
        name = os.path.basename(path)
        if name == "params.py":
            continue
        with open(path, "r", encoding="utf-8") as handle:
            sources[name] = handle.read()
    return sources


#: A module-level `def` in this file and the body that follows it, so a field read
#: through a derived helper counts as read. `frame.py` never mentions `wheelbase`
#: and could not be written without it: it calls `P.front_axle_y(p)`, which is the
#: whole reason those helpers exist.
_HELPER_PATTERN = re.compile(r"^def ([a-z_][a-z0-9_]*)\(", re.MULTILINE)


def _helper_fields() -> dict[str, set[str]]:
    """Which fields each derived helper in this file reads.

    One level of indirection, not a closure: the helpers here read fields and do
    not call each other for anything the scan would miss (`tray_bottom_z` calls
    `rail_top_z`, and both are named by `frame.py` directly). Two levels would be
    a call graph, and a call graph in a build gate is a thing that fails for
    reasons nobody expects.
    """
    with open(os.path.abspath(__file__), "r", encoding="utf-8") as handle:
        source = handle.read()
    matches = list(_HELPER_PATTERN.finditer(source))
    out: dict[str, set[str]] = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(source)
        body = source[match.start() : end]
        out[match.group(1)] = set(_READ_PATTERN.findall(body))
    return out


def field_readers(package_directory: str) -> dict[str, list[str]]:
    """Which modules read each field of `KartParams`, by static scan.

    Static rather than dynamic on purpose. A dynamic tracer would only see the
    fields the *current* detail level and the *current* set of implemented
    modules happened to touch, so `--watch` (one detail level) and a run with an
    unwritten module would disagree with a full build about which fields are
    dead. A source scan is a property of the tree and gives the same answer every
    time, which is what a fatal gate needs.
    """
    names = {f.name for f in dataclasses.fields(KartParams)}
    readers: dict[str, list[str]] = {name: [] for name in names}
    helpers = _helper_fields()
    for module, source in _module_sources(package_directory).items():
        read: set[str] = set(_READ_PATTERN.findall(source))
        for helper, fields in sorted(helpers.items()):
            if re.search(r"\b%s\s*\(" % helper, source):
                read |= fields
        for match in sorted(read):
            if match in readers:
                readers[match].append(module)
    return readers


def check_field_coverage(package_directory: str) -> tuple[int, int]:
    """Fail the build if a field is read by no module and is not exempt.

    Returns (fields checked, fields exempt) so the caller can print a summary
    line -- a gate that prints nothing when it passes is a gate nobody notices
    has stopped running.
    """
    readers = field_readers(package_directory)
    unread = sorted(name for name, modules in readers.items() if not modules)
    orphaned = sorted(set(FIELD_COVERAGE_EXEMPT) - set(readers))
    covered_but_exempt = sorted(
        name for name in FIELD_COVERAGE_EXEMPT if readers.get(name)
    )

    if orphaned:
        raise SystemExit(
            "error: FIELD_COVERAGE_EXEMPT names %d field(s) that no longer "
            "exist: %s\n"
            "       An exemption for a deleted field is a comment about a "
            "parameter that is\n"
            "       gone. Delete the entry." % (len(orphaned), ", ".join(orphaned))
        )

    if covered_but_exempt:
        raise SystemExit(
            "error: %d exempt field(s) are read after all: %s\n"
            "       An exemption that has stopped being needed is the same "
            "defect as a\n"
            "       joints.py waiver that has stopped failing -- it makes the "
            "list stop\n"
            "       meaning anything. Delete the entry."
            % (len(covered_but_exempt), ", ".join(covered_but_exempt))
        )

    fatal = [name for name in unread if name not in FIELD_COVERAGE_EXEMPT]
    if fatal:
        listing = "\n".join("      %s" % name for name in fatal)
        raise SystemExit(
            "error: %d parameter(s) are read by no module:\n%s\n"
            "       A parameter that no mesh reads is a comment wearing a "
            "number's clothes.\n"
            "       It cannot be checked, and it drifts away from the kart in "
            "silence --\n"
            "       frame_height did, and tray_width described a rectangle the "
            "regulations\n"
            "       forbid. Either point geometry at it, or delete it, or add it "
            "to\n"
            "       params.FIELD_COVERAGE_EXEMPT with the reason it is "
            "informational."
            % (len(fatal), listing)
        )

    return len(readers), len(FIELD_COVERAGE_EXEMPT)


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


def rail_top_z(p: KartParams) -> float:
    """Top surface of a main rail. What the floor tray bolts onto, and what the
    tray's edging tube and the upper steering support's feet weld to."""
    return rail_z(p) + p.tube_main * 0.5


def nose_y(p: KartParams) -> float:
    """Fore-aft center of the front bumper's frontmost tube. Spec §10.2.

        front axle             +525
        bumper overhang        +420   `overhang_front_bumper`, >= Art. 9.4.1's 350
        bar outer surface      +945
        tube center            +945 - 10 = +935   (Ø20 lower bar)

    Derived rather than authored, and that is the shape of the whole #190 fix: the
    old `nose_y` field was 0.760 and meant the *fairing's* center, while
    `frame.py` computed +0.905 for the same bar out of `length_overall` -- one
    end of the kart described twice, 179 mm apart, and neither number sourced.
    There is now one owner (the overhang, which is what the article limits) and
    every consumer reads this.
    """
    return front_axle_y(p) + p.overhang_front_bumper - p.tube_bumper * 0.5


def rear_y(p: KartParams) -> float:
    """Fore-aft center of the rear bumper's top bar. Spec §10.2.

        rear axle              -525
        frame overhang          210   `overhang_rear_frame`, sourced G1 both forms
        rear extremity         -735
        tube center            -735 + 10 = -725   (Ø20)

    It lands 20 mm inside the rear protection's own 187 mm depth, which is where
    a panel that bolts over a hoop needs it. That is the 5.91 mm gate-2 finding
    closed by arithmetic rather than by a standoff.
    """
    return rear_axle_y(p) - p.overhang_rear_frame + p.tube_bumper * 0.5


def rail_rear_y(p: KartParams) -> float:
    """Rear end of a main rail's centerline. `derived` from the sourced `G1`.

    **No half-diameter inset here, and the difference is measurable.** A swept
    tube's end cap is perpendicular to its path and sits *on* the last control
    point, so the rearmost surface of the frame is this station itself -- unlike
    the front, where the loop's frontmost segment runs across the kart and its
    outer surface is half a diameter ahead of its centerline. Setting this to
    `- overhang + tube/2` made the built frame measure a 195 mm rear overhang
    against a form figure of 210 ±15: inside the tolerance, at the bottom edge of
    it, and wrong for a reason that has nothing to do with the tolerance.
    """
    return rear_axle_y(p) - p.overhang_rear_frame


def loop_front_y(p: KartParams) -> float:
    """Frontmost point of the front loop's centerline, by the same argument as
    `rail_rear_y` and against the sourced `G2`."""
    return front_axle_y(p) + p.overhang_front_frame - p.tube_main * 0.5


def nose_apex_y(p: KartParams) -> float:
    """Frontmost plane of the nose fairing, on the centerline. Spec §50.8.

        front axle              +525
        fairing overhang        +504   `overhang_front_fairing`, <= Art. 9.5.2's 680
        apex                   +1029

    Derived rather than authored so the fairing's front face and the number issue
    #21 measures the kart against are one figure. `bodywork.FAIRING_CENTER_Y` used
    to author +760 independently, which is where the built panel's 236-triangle
    overlap with the front loop came from.
    """
    return front_axle_y(p) + p.overhang_front_fairing


def nose_lip_y(p: KartParams) -> float:
    """Rear lip of the nose fairing, constant across the span. `derived`:
    apex - `nose_depth`. Constant so Art. 9.5.2's 180 mm wheel-to-fairing gap is
    one number rather than a function of x."""
    return nose_apex_y(p) - p.nose_depth


def rear_prot_front_y(p: KartParams) -> float:
    """Front face of the rear wheel protection. Spec §50.11.

        rear axle               -525
        protection overhang      367   `overhang_rear_protection`, <= 400 (9.5.5.1)
        rear face               -892
        depth                    187   `rear_prot_depth`, sourced KG C2
        front face              -705

    Derived from the overhang rather than from a half-length, because 1,920 mm of
    overall length is **not symmetric about the origin** -- the two ends want 504
    and 367 -- and placing either end from half of it is 35 mm over the rear cap.
    The gap to the rear tire's rearmost surface at -672.5 is then 32.5 mm, the
    middle of Art. 9.5.5.1's 15-50 band.
    """
    return rear_axle_y(p) - p.overhang_rear_protection + p.rear_prot_depth


def tray_bottom_z(p: KartParams) -> float:
    """The floor tray bolts to the top of the rails, not to their underside."""
    return rail_top_z(p)


def tray_top_z(p: KartParams) -> float:
    return tray_bottom_z(p) + p.tray_thickness


def steering_column_base(p: KartParams) -> tuple[float, float, float]:
    """Lower end of the steering column, at the front cross member.

    Derived rather than authored so that it is consistent with `wheel_angle`:
    the line from here to the steering wheel center is the column axis, and its
    angle from vertical is `wheel_angle` by construction. Authoring both ends
    and the angle independently is how a steering wheel ends up visibly not
    square to its own column.

    **The frame no longer reads this.** The lower steering support's bore is
    `steering_bore_y`/`_z`, sourced off the column's own reference photograph,
    and spec §40 moves the column onto it -- so this function describes the
    column as built and the frame describes the support as specified. They are
    99 mm apart today and that is `joints.py`'s waived gap, not a hidden one.
    """
    length = 0.402
    return (
        0.0,
        p.wheel_center_y + math.sin(p.wheel_angle) * length,
        p.wheel_center_z - math.cos(p.wheel_angle) * length,
    )
