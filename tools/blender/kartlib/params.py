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

    steering_support_shoulder_x: float = 0.150
    """Where the upper steering support's legs stand as they pass the fuel tank's
    flank. `derived`: Art. 4.7 fixes the tank between the main tubes and its flank is
    at x +-127.5, so a Ø16 leg centered at 150 clears the tank's side by
    150 - 8 - 127.5 = **14.5 mm** and needs nothing from the tank.

    **A straight V cannot do this and the arithmetic is short.** On a straight leg
    from (foot_x, +30, 65) to the apex at (0, +262, 393), x and z are both linear, so
    the leg reaches x 137.5 at s = 1 - 137.5/foot_x and is at z = 65 + 328 s there.
    Requiring z >= 309 -- above the tank's own top at 299 -- needs foot_x >= **445 mm**,
    and the rails are at +-286 at the strut. So the legs have to stand outboard of the
    tank and converge *above* it, which is what a real column support does and what
    wave 3's #190 measurement (192 pairs against the tank, 212 against its rear strap)
    was reporting."""

    steering_support_shoulder_z: float = 0.335
    """Height of the run that crosses over the tank. `derived` from the part it has to
    clear, which is **not** the tank: `fuel_tank_strap_rear`'s crown is at z 311.6,
    12.6 mm above the tank's own top at 299, and it is the strap the old V's 212-pair
    overlap was mostly against.

    Measured on the built tube, after the 24 mm bend radius has cut the two corners:
    **15.62 mm** to the rear strap and **21.26 mm** to the tank. 325 was the first value
    and measured 5.65 to the strap, because the fillet at the outboard knee dips ~5 mm
    below the authored corner -- which is the difference between an authored offset and a
    built one, and the reason both figures here are the built ones.

    Read with `steering_support_shoulder_y`: the two legs run level at this height from
    outboard of the tank to the point where they rejoin the straight foot-to-apex line,
    which is the constraint below."""

    steering_support_shoulder_y: float = 0.150
    """Fore-aft station of the outboard knee, i.e. where each leg stops rising and
    starts running inboard. `estimated`: anywhere between the strut and the tank's rear
    strap works, and 150 keeps the leg's own y monotonic from foot to apex.

    It is bounded on one side by something real: `fuel_tank_strap_rear` occupies
    y +183.5..+208.5 out to x +-227.6, so a knee at y 150 keeps the *rising* leg -- the
    one that is still outboard and below the shoulder -- entirely out of the strap's
    fore-aft band."""

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

    seat_width: float = 0.333
    """Shell width **at the hips**, external. `sourced` + `derived`: Tillett T11
    ML dimension A is 325 internal, plus 2 x `seat_thickness`. Spec §40.3."""

    seat_width_shoulders: float = 0.368
    """Shell width **across the top of the back**, external. `sourced` +
    `derived`: Tillett dimension B, 360 internal, plus 2 x `seat_thickness`.

    A real shell is 35 mm *wider* at the top than at the hips and one
    `seat_width` cannot hold both -- `cockpit.SEAT_HALF_WIDTH` tapered to 0.812
    of the hip width and built a shell 268 mm across the shoulders."""

    seat_height: float = 0.335
    """Back-top height above the shell's base plane. `sourced`: Tillett T11 ML
    dimension E; the adult T11 range is 280-335. Was 0.290, 45 mm short."""

    seat_shell_rake: float = 0.384
    """Radians from vertical, 22 deg ±5 -- the **fiberglass chord's own line**,
    not the driver's torso recline and not the radiator's core rake.

    `derived` from Tillett C 460 and E 335: total horizontal run
    sqrt(460² - 335²) = 315, less 150-200 mm of flat pan, so the back rises 335
    over 115-165 of run, atan = 19-26 deg.

    Replaces `seat_back_angle` 0.610 (35 deg), which sat between the shell's
    chord and the driver's 40-45 deg recline and did double duty for both -- and
    which the radiator's rake was *added to*. `radiator_rake` was decoupled first
    (see its docstring) precisely so this change could land without tipping the
    core 13 deg with no gate objecting."""

    seat_y: float = -0.230
    """Hip point, rearward of the origin. `derived`, spec §40.3: the rear axle's
    front face is at y -500, Tillett's KZ "axle to driver's back" is 135 mm, so
    the shell's rearmost point -- the **top** of a reclined back, not its base --
    is at -365; the back's own horizontal run is 335 x tan(22 deg) = 135.3, which
    puts the hip at -230.

    Was -0.060, i.e. 170 mm too far forward, and that single error was most of
    why the built cockpit did not fit a driver: it is what made issue #13's
    hip-to-pedal reach 618.5 mm."""

    seat_z: float = 0.032
    """Base plane above the ground. `sourced` -> `derived`: Tillett's own seat
    positioning note says *"5 mm is usually the maximum dimension that you can
    set the base of the seat below the tubes"*, and the lowest tube is at
    `ground_clearance` 35, so the band is 30 (proud below) to 35 (flush).
    Was 0.075, 43 mm high."""

    seat_thickness: float = 0.004
    """Fiberglass seat shell. `sourced` material (Art. 4.8 permits composite),
    `estimated` thickness: 4 mm is what a Tillett shell measures. Was 0.008,
    which built a rim twice as heavy an edge as the real part."""

    # --- steering ----------------------------------------------------------
    #
    # Authored from the **welded end**. `steering_bore_y`/`_z` up in the front-end
    # block is the lower bearing's bore, the bracket is welded and cannot move, so
    # that is the datum; the column is a catalogue part of a known length at a
    # measured rake; and the wheel centre is *derived*. That ordering is what makes
    # the 37.46 mm gate-2 gap arithmetically impossible rather than merely fixed:
    # `steering_bearing`'s bore centre and the column's journal centre are the same
    # expression. Spec §40.2.

    wheel_diameter: float = 0.320
    """Steering wheel, outside diameter. `sourced` size, `derived` choice: kart
    wheels sell at 280/300/320/340 and the side-view trace scales to 306 mm,
    which picks 320 out of that list."""

    wheel_rim_thickness: float = 0.038
    """Padded grip section, across the foam. `derived`: red-grip mask 21 px of
    vertical chord corrected for the 40 deg axis lean = 44 mm raw, less a pixel
    of soft edge each side, so 38 ±6. Was 0.024, 14 mm thin -- kart grips are
    genuinely chunky."""

    column_length: float = 0.490
    """Overall column length, threaded tip to open top. `sourced`: OTK "38/50
    Steering Column 470/490/510 mm" and Birel ART "STEERING COLUMN RACING L490".
    490 is the middle of the senior range; 470 and 510 are the adjustment band a
    tunable would sweep."""

    column_rake: float = 0.628
    """Radians from vertical, 36 deg. `derived`: measured on the column tube in
    `tonykart_racer401T_product.png` and corrected for that image's 11%
    anisotropy -- 119.3 mm forward per 165.9 mm of rise, atan = 35.7 deg, ±3.

    Replaces `wheel_angle` 0.470 (27 deg): 9 deg of error, and it had
    `wheel_center_y` 133 mm too far forward."""

    column_diameter: float = 0.020
    """`derived`: a 50.0 px shaft against the 25.5 px annotated 10 mm feature in
    `birelart_kz_steering_column.jpg` is 19.6 mm, and every European support
    block on the market is bored 20. **Art. 4.5.2's 18.0 mm is a floor** and this
    field used to be built to the floor."""

    hub_stack: float = 0.025
    """Hub plus inclined spacer, measured along the column axis from the column's
    top end to the wheel's rim plane. `derived`: the side view puts the rim centre
    16.9 mm rearward and 17.5 mm above the column's top end, 24 mm back along the
    axis; OTK sells it as a stack -- hub, spacer, inclined spacer.

    Distinct from the wheel's **dish**, which is ~15 mm. `cockpit.WHEEL_DISH` was
    0.048, conflating the two, and it is subtracted from the wheel centre to find
    the column's top -- so it removed 33 mm of column as a side effect."""

    wheel_incline_delta: float = 0.122
    """Extra rake of the **wheel plane** over the column's, radians (7.0 deg).

    `derived`: the edge-on rim trace measures 42.9 deg and the column tube 35.7 in
    the same frame at the same scale. The difference is hardware, not measurement
    error -- OTK sells an "INCLINED STEERING WHEEL HUB" and an "INCLINED SPACER
    FOR STEERING" whose only purpose is to lay the wheel back further than its
    column, and Art. 4.5 permits *"A spacer […] between the steering wheel and the
    hub."*

    Added to `column_rake` rather than authoring the wheel's absolute angle a
    second time, which is how a wheel ends up skewed on its own column. The error
    is invisible from every angle except a true side elevation, so a turntable
    does not catch it."""

    # --- pedals ------------------------------------------------------------
    #
    # Organ type, **bottom pivot, transverse axis**, proved from part photographs
    # rather than judged: OTK 0014.DC (throttle) and 0015.DC/DCA (brake) are forged
    # arms with a bushed pivot eye at the *bottom* and the foot bar at the top.
    # Spec §40.5.

    pedal_pivot_y: float = 0.610
    pedal_pivot_z: float = 0.050
    """The pivot axis station, on `chassis_cross_pedal`. `estimated`: z is just
    under the frame tube, which is where OTK 0014.D3's support plate hangs its
    eye; y comes from the pedal-to-seat reach."""

    pedal_arm_length: float = 0.180
    """Pivot to foot-bar centre. `estimated`, and self-consistent at this scale:
    the part photo's foot bar reads 0.44 of the arm's height, i.e. ~80 mm, which
    is one boot. At 145 mm the bar would be 64 -- too narrow for a boot."""

    pedal_arm_rake: float = 0.140
    """Arm lean rearward from vertical, radians (8 deg). `estimated`: puts the bar
    25 mm behind the pivot so the sole meets it square with the leg raised."""

    pedal_z: float = 0.228
    """Foot-bar centre height. **`derived`, not authored**:
    `pedal_pivot_z + pedal_arm_length * cos(pedal_arm_rake)` = 50 + 180 x cos 8
    = 228.2, and this field is the rounded figure the manifest publishes.

    Was 0.090 -- 21 mm above the floor tray, which is a foot resting on the floor
    and not a pedal. 138 mm of error, and it is the single number that made the
    built pedal box a rental kart's."""

    pedal_bar_diameter: float = 0.018
    pedal_bar_length: float = 0.080
    """The transverse foot bar: Ø18 x 80 round bar on a forged arm. `estimated`
    from 0014.DC/0015.DCA proportions at the 180 mm arm scale; 80 mm is one boot.

    Replaces `pedal_width` 0.070 and `pedal_length` 0.120, which described a flat
    70 x 120 plate. **A plate is a rental-kart pedal.**"""

    pedal_separation: float = 0.300
    """Throttle to brake, center to center, so ±150. `estimated`, photogrammetric,
    issue #201 -- measured, not guessed, and the anchors are stated:

    `alessandro_giardelli_european_championship.jpg` (S3, dead-front, both boot
    soles visible on the pedals; image-left is the kart's RIGHT per §00 §2, which
    does not move a symmetric separation). Sole centers 413 px apart, read off
    the Alpinestars logos and both toe edges. Two independent scales:
    the OTK M7 front panel in the same frame, `sourced` 295 mm from form
    012-BP-41, spans 381 px -> 320 mm; the front tire centers at the estimated
    track (1240 - 135 = 1105 center-to-center) span 1570 px -> 291 mm. Both
    subjects sit within 150 mm of the front axle plane in depth, so perspective
    moves the read by under 2 percent at a telephoto distance. 300 is the middle
    of 291..320; the sole width cross-checks at 98 mm against the boot's 96.

    Caveat carried rather than hidden: the S3 kart is OK-class, not KZ -- same
    chassis family, same pedal packaging. Was 0.150, then 0.170; both put the
    feet against the steering column, and the photograph says the feet live
    nearly twice as far out."""

    # The brake master cylinder's **22 mm** bore is `wheels.MASTER_BORE`, not a field
    # here: §Running gear already sources it off three homologation forms and it is
    # the same number in the same package. `notes_controls` §5 gave 19 mm, marked it
    # `estimated` and said itself it was the figure to re-check before anything
    # depended on it -- **a homologation form beats a catalogue inference**, so 19 is
    # superseded rather than carried alongside. What changes with the bore is the
    # displacement per circuit, 2.84 -> 3.80 cm3; the 3.2:1 pedal ratio and the 10 mm
    # of piston travel are geometric and do not move.

    # --- gear lever --------------------------------------------------------
    #
    # Issue #117, unanswered for a milestone. The pivot is beside the driver's
    # **knee**, not his hip, and the reasoning is the answer: the only two shift
    # rods anybody sells are 495 and 530 mm, both `sourced`, and a hip pivot at
    # y ~ +100 needs a ~300 mm rod that does not exist. Spec §40.4.

    shift_pivot_x: float = 0.312
    shift_pivot_y: float = 0.335
    shift_pivot_z: float = 0.075
    """Lower end of the lever's own rod, in the bracket's two nylon bushes.
    `estimated`.

    §40.4 authors +330 and offers a cross-check -- *"the right main rail's centreline
    at y +330 interpolates to x 323"* -- which is **false on this chassis**: §10 waisted
    the frame, so the rail is at x 156 there and the bracket is 118 mm from it
    (`joints.py` carries the number). The check is withdrawn rather than repaired.

    +312 rather than +330 because the lever bows outboard to its kink and Art. 9.4.2's
    upper side bar has to cross that station on its way to a 500 +-5 attachment pitch:
    at +330 the two cleared by under a millimetre at low detail and *touched* at high,
    which is the worst kind of pass. 18 mm inboard buys 10 mm of real clearance and
    moves the knob 18 mm, which is inside the two-finger gap's own tolerance."""

    shift_knob_x: float = 0.200
    shift_knob_y: float = 0.300
    shift_knob_z: float = 0.450
    """Knob centre -- 414 mm above the seat pan and one two-finger gap off the
    rim's rightmost point at (+160, +187, +496). `estimated`.
    `cockpit.SHIFTER_KNOB` was (0.262, 0.104, 0.392), 200 mm rearward of this,
    i.e. beside the hip: the placement §40.4 exists to correct."""

    shift_kink: float = 0.960
    """Angle between the lever's rod and its hand tube, radians (55 deg ±3).
    `derived`: atan(150/104) over four samples along the upper axis of
    `ctl_otk_0111.B0.webp`. Out-of-plane rotation in the product shot biases it
    low and the ±3 covers it."""

    shift_rod_length: float = 0.495
    """Eye-to-eye length of the shift tie-rod. `sourced`: Righetti Ridolfi / IKP
    hexagonal, 13 mm across the flats. The alternative is OTK 0114.BA at 530 mm,
    also `sourced`; anything between the two is reachable on the turnbuckle and
    **anything under 495 is not buildable from a part anybody sells**, which is
    the constraint that placed the lever."""

    # --- fuel tank ---------------------------------------------------------

    tank_capacity: float = 0.0085
    """Cubic meters. `sourced`: OTK 0073.EA "Fuel tank, KZ, 8.5 Litre"; KG
    SER.003 and CKR also sell 8.5. Art. 9.3's minimum is **8 litres** and the kart
    had no tank at all, so this is a compliance item and not a detail."""

    tank_width: float = 0.255
    tank_depth: float = 0.250
    tank_height: float = 0.230
    """Outer bounding size. `estimated`: 255 x 250 x 230 is 14.7 L of box of which
    8.5 L is 58%, the right fraction for a body radiused on every edge and waisted
    at the bottom front -- which is what the 0073.EA photo shows."""

    tank_center_y: float = 0.225
    tank_center_z: float = 0.184
    """Centre, on the kart's centreline. **`derived`, and all three coordinates
    are forced by Art. 4.7**, which does not permit a position but *mandates*
    one: *"between the main tubes of the chassis frame, ahead of the seat and
    behind the rotation axis of the front wheels."* So x 0; front face +350
    against the front axis at +525, 175 mm clear; rear face +100 against the
    seat's lip at +30, 70 mm clear; bottom on the tray at 69, so centre 184."""

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

    cylinder_lean: float = 0.4363
    """Forward lean of the cylinder from vertical, radians (25 deg).

    **`derived`, and it is forced rather than styled.** The exhaust port axis is
    25 deg out of the plane perpendicular to the bore, tilted toward the crankcase
    (`sourced`, two independent measurements off KZ-R1 HF p. 3, 1.7 deg apart).
    With a *vertical* cylinder that points the pipe's inlet axis 25 deg **down**,
    and a 674 mm chamber cannot be packaged from there: the drop happens before the
    bend starts at s 134.7, so at the rear axle line the pipe has fallen to z 161.8
    with a Ø70 section against an axle whose top is 172.5. Measured over 0-20 deg
    of bend-plane roll the axle gap runs -62.7 to -51.1 mm -- always inside the
    axle. Clearing it needs the inlet axis within ~12 deg of horizontal, and a
    25 deg forward lean puts it at exactly 0.

    Consequences §30.4 works out and this file carries: the deck plane inclines,
    `AIRBOX_LO`/`_HI` rise 60 mm because the pipe occupies the old volume, and the
    carburettor drops 12 mm because its cap fouled the pipe by 2."""

    exhaust_developed_length: float = 0.6746
    """Developed centreline length of the expansion chamber, port face to stinger
    exit. `derived` from the 15-cone table in `powertrain.EXHAUST_CONES`, whose
    every row is `sourced` off TM homologation forms 041-EZ-75 (KZ-R1) and
    041-EZ-02 (KZ-R2) -- each carries both diameters and both slant lengths of all
    15 cones, and Art. 9.15.1 makes the HF's pipe normative.

    Replaces `exhaust_length` 0.620, which was 54 mm short."""

    exhaust_max_diameter: float = 0.1365
    """The belly, at its widest. `sourced`: cone 11's end diameter, `phi N` on both
    forms. Was 0.130."""

    exhaust_header_diameter: float = 0.0445
    """Bore at the pipe's inlet face. `sourced`, `phi A`. Replaces
    `exhaust_pipe_diameter` 0.034, which matched nothing on the real pipe."""

    exhaust_baffle_diameter: float = 0.1145
    """Where the first baffle cone ends and the long one starts, cone 13/14.
    `sourced`, and it is the station the hanger's spring cradle grips."""

    exhaust_stinger_diameter: float = 0.0263
    """The stinger's exit bore. `sourced`, `phi R`."""

    exhaust_wall: float = 0.0010
    """Sheet thickness. `derived`: the wall that makes the summed frusta reproduce
    the HFs' own stated internal volume (1.07 mm for R1, 1.29 for R2). Art. 5.10,
    PDF p. 17: *"The exhaust must be made of magnetic steel in all categories.
    Minimum sheet metal thickness is 0.75 mm if not otherwise specified in the
    HF."* So 1.0 clears the regulation floor."""

    exhaust_segments: int = 16
    exhaust_segments_high: int = 32

    chain_x: float = 0.115
    """The chain plane, and **the sign is the point.** Issue #112:
    `powertrain.CHAIN_X` and `wheels.SPROCKET_X` were the same magnitude with
    opposite signs and neither owned it. A KZ carries engine, chain and crown wheel
    on the driver's **right** and the brake disc on the left; `wheels.py`'s
    docstring reasoned to the right magnitude and then concluded *"a KZ drives the
    left rear"*, which a locked rear axle makes meaningless.

    `estimated` as a value -- it lands the crown wheel between `frame.py`'s centre
    and outer bearing hangers -- and a chain plane out at x 300-330, which
    `notes_exhaust.md` assumed when it worried about header interference, would put
    the crown wheel outboard of its own bearing hanger at 185."""

    chain_pitch: float = 0.0055626
    """219 chain. `derived`: 0.219 in x 25.4 exactly.

    **219 is not a KZ rule.** Art. 9.18.1, PDF p. 31: *"The chain and sprockets are
    free."* It is compulsory in every OK class (9.18.2) and it is the universal
    trade standard, which is a different kind of fact -- so the *choice* of 219
    here is `estimated` practice. `CHAIN_PITCH`'s old docstring read as though the
    class required it."""

    sprocket_teeth_engine: int = 12
    """Gearbox output sprocket. `estimated`: KZ fronts run 10-14, and 12 with 82
    gives 6.83:1, the middle of KZ final drive."""

    sprocket_teeth_axle: int = 82
    """Crown wheel. `derived` from the built Ø145: `p / sin(pi/82)` = **145.23**,
    so 145 *is* an 82-tooth 219 sprocket to 0.2 mm.

    Pitch diameter is `p / sin(pi/N)` and **not** `p * N / pi`. The approximation
    is harmless here (0.03%) and is 1.1% small at 12 teeth, which is exactly the
    1.9 mm by which the chain was built inside the output shaft."""

    radiator_width: float = 0.250
    """Core width **across the kart**, laterally. `sourced`: the EM-Technology
    EM-01 core is a catalogue part published at 250 x 435 x 40, and **a catalogue
    part with a published dimension outranks any photograph.**

    Was 0.265, the largest core in the KZ family (the range is 240/245/250/265/290),
    chosen because it filled the gap between the seat edge and the side bar -- a fit
    argument wearing a part dimension, which is the exact failure
    `length_overall = 1830 mm max` was written up for. Both dead-on photographs agree
    with 250: predicted outer edge 365 + 125 = 490 against 479 measured dead-front
    and 481 dead-rear, where the 265 variant predicts 497.5.

    **State the lateral extent once and read it from here:** x -240 to -490. It is
    *not* `radiator_x ± radiator_thickness/2` -- the core's own frame puts +x along
    the face normal, which points forward, so 385 is a plane 20 mm ahead of the core
    and has nothing to do with how far outboard it reaches. `joints.py`'s pod waiver
    was 105 mm short for exactly that reason.

    Read the three radiator dimensions against each other, because none of them
    is an axis-aligned extent and every earlier attempt at this block got one of
    them wrong:

        width       across the kart, from the seat shell outward
        height      up the **slant**, low edge forward, high edge rearward
        thickness   through the core, along the face's own normal

    Read the three radiator dimensions against each other, because none of them is
    an axis-aligned extent and every earlier attempt at this block got one wrong:

        width       across the kart, from the seat shell outward
        height      up the **slant**, low edge forward, high edge rearward
        thickness   through the core, along the face's own normal
    """

    radiator_height: float = 0.435
    """Core length **up the slant**, tanks included. `sourced`: EM-01 and EM-02 are
    both 435 (New-Line RS MAX 430). The old 0.432 was right for the wrong reason --
    its docstring traced it to "17 in", which nobody sourced.

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

    radiator_z: float = 0.240
    """Centre of the core, in the plane it is raked into. `derived`, and the
    derivation is two exact floors plus the raked core's true half-extent:

        vertical half-extent = (height/2) cos(rake) + (thickness/2) sin(rake)
                             = 217.5 x 0.7071 + 20 x 0.7071 = 167.9
          the second term is the one an earlier solve omitted: the core is 40 mm
          thick and raked, so its lowest point is a tank *corner*, 12 mm below its
          centreline edge.
        floors:  Art. 5.3.1 "above the chassis frame" -> rail top 65
                 chassis_floor_tray top                        69   <- binding
        radiator_z = 69 + 3 + 167.9 = 240

    Was 0.320, which put the core's top at 497 -- **3 mm** under Art. 5.3.1's 500 mm
    ceiling. At 240 the top is 407.9, with 92 mm of margin, and the low corner is
    72.1: 3.1 mm above the tray.

    Residual disagreement, recorded rather than hidden: the photogrammetric top edge
    is 375 ±20 and 408 is 13 mm above that bar. The two floors and the regulation
    win, because the photograph's vertical figure is dominated by camera elevation
    and monotone in it (371 at 24 deg, 387 at 8) while the rail and tray tops are
    arithmetic."""

    radiator_rake: float = 0.7854
    """Radians from vertical, and **its own number**.

    This replaced `radiator_rake_delta`, which was *added to `seat_back_angle`*
    on the claim that the core sits in the plane a second seat's back would
    occupy. Spec §30 measured that false: the core rakes **45° from vertical**
    (0.7854 rad) and the seat shell's chord is 22°. So the two parts were sharing
    an angle they do not share, and because **no gate measures a rake**, fixing
    the seat would have tipped the radiator 13° with nothing objecting. Decoupling
    it and correcting the seat had to land together, and this is the decoupling.

    45 deg from vertical is `derived` as the one value two ranges share: the
    height-budget solve gives 40 ±5 from vertical, and sourced kart practice tunes
    45-60 from *horizontal*, i.e. 30-45 from vertical. 45 is the top of the first
    and the bottom of the second.

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

    front_panel_bottom_z: float = 0.240
    """Front panel bottom edge. `estimated` off V8/V12: the foot tucks in just
    behind and below the fairing's rear top edge (z 267 at the spine), so the
    panel reads as the fairing's center section continuing up to the wheel --
    which is what a real nose is, one visual mass, not a signboard floating in
    daylight behind it.

    Was 0.190 with a docstring claiming the height alone satisfied Art. 9.5.3's
    feet clause. It did not: the panel stood at y 585-620, planted in the boot
    zone, 29-33 mm INTO the boots (#205, caught by gate 3 on day one). The
    penetration was fixed by moving the whole panel forward onto the fairing
    (`bodywork.FRONT_PANEL_BOTTOM_Y`), not by this z; the occlusion half of
    9.5.3 remains #205's open layout question because the pedals sit ~130 mm
    further back than V4 shows them relative to the fairing."""

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

    race_number: str = "85"
    """The racing number on the pods and the rear panel. `estimated` -- a number
    is the entrant's, not the regulation's, so any digits are as sourced as any
    others. Two digits and not three because the pods' achieved zone is 253 mm
    wide against Art. 3.7's 370 for three digits (spec §60.4.3), and the same
    number must appear on every panel. The glyphs are die-cut solids in
    Liberation Sans Bold (`assets/fonts/liberation/`, hash-pinned; Art. 3.7
    names Arial, which is not redistributable -- Liberation is the
    metric-compatible face this repo may ship, issue #187)."""

    number_glyph_height: float = 0.125
    """Cap height of the rear panel's glyphs. `estimated`, and knowingly under
    Art. 3.7's 150 minimum: the tray's aft face is 138 mm tall between its
    bottom turn-under and its top roll (z 65..203), so a regulation glyph
    cannot sit on this panel without wrapping a curve the article calls flat.
    Same geometry finding as spec §60.4.3's "the 150 mm number is what sets
    kart bodywork height" -- the built tray is 177 tall where the KG C2 form's
    is 177, so the shortfall is the reference part's, not a modeling economy."""

    number_glyph_height_pod: float = 0.095
    """Cap height on the pod flank. `estimated`, same shading as the rear: the
    flank's flat vertical band is 102-122 mm tall across the number's stations,
    so 95 is the largest glyph that stays a flat decal instead of wrapping the
    shoulder crease -- which is #189's recorded zone-wrap defect, being avoided
    rather than reproduced. The reference pods' numbers fill the flank the same
    way (V3, V13)."""

    number_decal_thickness: float = 0.0006
    """Die-cut vinyl standoff. `estimated`: real number film is ~0.1 mm; 0.6
    reads as a sticker at turntable distance without z-fighting."""

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
    # Spec §60.1.4's hard points, and §60.1.6 is the contract that fixes them:
    # issue #17's driver module, issue #200's gate 3 and issue #194's mass lumps
    # all read this block and none of them may rename a field. ADR-0055.
    #
    # **The driver was built sitting on the asphalt.** `driver_shoulder_z` was
    # 0.470 and `driver_eye_z` 0.620, 137 and 138 mm low, and their *relative*
    # gap of 150 matched the sourced 149.5 exactly -- which is what identified the
    # cause: the hip joint had been placed at z = 0 instead of on the seat pan.
    # Issue #194. Both are corrected here rather than in #194's own wave, because
    # §60.1.4 already publishes the right figures as `derived` and two conflicting
    # seated heights in one file is the drift this document exists to stop.
    # scripts/look/kartview.gd reads both off the manifest for its cockpit camera,
    # so that camera moves up 137 mm; no §6.4 figure reads either one.
    #
    # Every position is `derived` in §60.1.4 from a `sourced` chain: the Tillett
    # T11 ML seat chart via notes_radiator.md §6, and the NASA *Anthropometric
    # Source Book* 50th-percentile male segment table. The arithmetic is shown
    # there, not repeated here. Signs: x is a half-offset, so a limb is at +-x.

    driver_hip_x: float = 0.085
    driver_hip_y: float = -0.170
    driver_hip_z: float = 0.130
    """The H-point, `derived`: 95 mm above the pan at z 36, 100 mm forward of the
    back contact. §60.1.3 marks both offsets `estimated` and §60.1.4 corroborates
    the result -- hip-to-pedal-face comes out 731.1 mm against §40's independently
    measured 735, agreeing to 0.5% with no shared method."""

    driver_torso_recline_deg: float = 25.0
    """Torso axis from vertical, leaning back, so the axis is
    `(0, -sin 25, cos 25)` = `(0, -0.4226, 0.9063)`. `estimated`, bracketed by the
    seat shell chord's `sourced` 19-26 deg and taken 3 deg back of the middle
    because the spine continues above where the shell ends. `notes_radiator.md` §6
    offers 40-45 deg for the torso; §60.6 shows that angle cannot be built."""

    driver_shoulder_x: float = 0.200
    driver_shoulder_y: float = -0.393
    driver_shoulder_z: float = 0.608
    """Acromion. `derived`: sitting midshoulder height 622 less the 95 mm hip
    rise is 527 along the torso axis. Was 0.470 -- see the note above."""

    driver_eye_x: float = 0.032
    driver_eye_y: float = -0.462
    driver_eye_z: float = 0.757
    """Seated eye point. `derived`: sitting eye height 787 less 95 is 692 along the
    torso axis. Was 0.620. scripts/look/kartview.gd and scripts/look/lookdev.gd
    both read this for a camera and must not drift from it."""

    driver_helmet_y: float = -0.454
    driver_helmet_z: float = 0.738
    driver_helmet_width: float = 0.250
    driver_helmet_length: float = 0.340
    driver_helmet_height: float = 0.300
    """A helmet is an **ellipsoid, not a sphere**, which is what the old
    `driver_helmet_radius` = 0.125 got wrong: 125 is right laterally and 90 mm
    short fore-aft. Center is `derived` at 100 mm below the bare vertex along the
    head axis; the three outer dimensions are `estimated` in §60.1.5 against
    Art. 7.1's full-face requirement. The crown lands at z 860, which is 210 mm
    above Art. 9.1.1's 650 mm chassis ceiling -- as every photograph of a kart
    shows, and it is the sanity check on the whole chain."""

    driver_knee_x: float = 0.180
    """Knee lateral. `estimated` and load-bearing: the one lateral figure in the
    driver read off a photograph -- `exh_commons_buntschu_kz2.jpg`, a
    three-quarter front-left action shot where the knees are splayed clearly
    outboard of the wheel rim -- so it is a proportion against the front track,
    not a measurement, which is why it is +-180 and not a decimal. Six of gate
    3's first findings hang off it, and §60.1.6's rule is that a finding which
    cannot be adjudicated against a sourced figure gets a waiver and a ticket
    rather than a geometry change.

    The knee's y and z are **not fields**: `driver_knee()` solves the two-link
    chain from the hip and the live ankle on every read. Issue #202 is why --
    the ball of the foot was a field `sourced` from a pedal that had since been
    re-authored 138 mm higher, and the citation stayed true as a sentence while
    false as a number. A provenance tag records where a figure came from, not
    whether it still agrees with it; a derivation cannot disagree."""

    driver_thigh_link: float = 0.428
    driver_shank_link: float = 0.429
    """Hip-to-knee and knee-to-ankle joint distances. `derived`, spec §60.1.2:
    Drillis & Contini stature fractions, 0.245 x 1745 and 0.246 x 1745. They were
    spec-only until #202 made the knee a live solve; now the solve and the spec
    read the same numbers."""

    driver_foot_link: float = 0.191
    """Heel contact to ball-of-foot contact, along the sole. `estimated`:
    consistent with Drillis & Contini's foot length 0.152 x 1745 = 265 with the
    ball at ~0.72 of it, and it is the length of the old flat-foot offset
    (sqrt(190^2 + 21^2) = 191.2) so the boot the mesh already wears keeps its
    size."""

    driver_ankle_x: float = 0.150
    driver_ankle_forward: float = 0.055
    driver_ankle_rise: float = 0.068
    """Ankle joint offset from the heel contact, **in the sole's own frame**:
    55 mm along the sole toward the toe, 68 mm perpendicular to it. The rise is
    `derived` -- Drillis & Contini ankle height, 0.039 x 1745 = 68 -- and the
    55 is `estimated`. In the sole frame rather than global because the foot now
    pitches ~54 deg to reach the bar (#202), and an unrotated offset would put
    the ankle of a pitched foot in the wrong place by half its own size.

    x is `estimated` at `pedal_separation`/2 -- the foot runs straight in plan,
    which is what S3's dead-front soles show. The old 0.110 yawed the toes 35 mm
    inboard, and that yaw was an artifact of the stale ball at ±75 (#201/#202),
    not an observation. Not derived from the field because a one-piece boot may
    yet want a deliberate splay; if #201's number moves again, move this with
    it."""

    driver_heel_plane_z: float = 0.069
    """The plane the heel rests on: the floor tray's top face. `derived` from the
    tray stack. **§60.6 records that there is no floor tray under the heel** --
    the tray does not reach that station -- so the heel rests on nothing and that
    is a real finding, not a rounding. The heel's y is **not a field**: the sole
    is tangent to the live foot bar, so `driver_heel()` walks back from
    `pedal_bar_y`/`pedal_bar_z` on every read. #202."""

    driver_upper_arm: float = 0.368
    driver_elbow_to_fist: float = 0.361
    """Arm links, `sourced` from the NASA table. Not bone lengths and not mesh
    sizes: **`elbow-to-fist length` runs from the elbow to the center of the
    closed fist**, which is exactly where a wheel rim sits, and it is why §60.1.2
    chased this figure rather than a forearm.

    These replace `driver_upper_arm` = 0.290 and `driver_forearm` = 0.260, which
    summed to 550 mm -- **175.5 mm short**, and a forearm ends at the wrist so
    there was no hand in the number at all. The sourced pair sums to 729 with the
    elbow locked straight and 597.2 at a comfortable 110 deg. §60.2 measures the
    reach against the built rim and reports the residual; §60.1.6's rule is that
    the arms are built at whatever angle actually closes, because a driver with
    locked straight arms is the honest render of a cockpit that does not fit."""

    driver_shoulder_span: float = 0.400
    driver_bideltoid: float = 0.455
    driver_hip_breadth: float = 0.325
    driver_seated_shoulder_breadth: float = 0.360
    """The four breadths, and they are four numbers rather than two because they
    measure different things at different heights. `driver_shoulder_span` is
    **biacromial** -- bone to bone -- and places the joints for the reach solve;
    `driver_bideltoid` is the flesh, 27.5 mm wider per side, and sizes the mesh at
    the acromion. Both are `sourced`.

    The lower pair are `derived` from the Tillett T11 ML chart via §60.1.1: the
    shell's external widths of 333 and 368 are A and B plus 2 x 4 mm of shell, so
    the driver filling it is 325 at the hip and 360 across the shell's shoulder
    line. That the seated shoulder breadth (360) is *narrower* than the bideltoid
    (455) is not a contradiction -- the deltoids sit above where the shell ends.
    So the torso tapers 325 at the hip to 360 at the shell top to 455 at the
    acromion, and all three stations are sourced or derived rather than styled."""

    driver_overalls_thickness: float = 0.007
    driver_protector_thickness: float = 0.015
    """Art. 7.2's overalls and Art. 7.5's karting body protection as thickness.
    Both `estimated` in §60.1.5: 6-8 mm for a single close-fitting fabric layer,
    12-18 mm per side for the rigid rib shell. Between them they are the reason a
    driver fills a 325 mm hip and a 360 mm shoulder seat rather than measuring
    smaller, so they are not decoration -- they are what makes the seat fit."""

    # --- output and quality ------------------------------------------------

    texel_density: float = 512.0
    """Pixels per meter of surface, for the UV unwrap in issue #18.

    ARCHITECTURE.md §5 item 2 fixes 512 px/m for the track surface and 256 px/m
    for props. A kart is neither: the cockpit camera sits closer to it than to
    anything else in the game, so it takes the track's density rather than a
    prop's. See ADR-0024.
    """

    normal_map_size: int = 4096
    """Baked normal map resolution, issue #19. A **floor**, not the answer.

    `uv_stage.atlas_resolution()` derives the atlas the kart actually needs from
    total surface area times `texel_density` squared, divided by what a packer can
    reach, and rounds up to a power of two. This value only stops it choosing
    something smaller. `bake_stage` calls the same function, so the density
    arithmetic and the image can no longer disagree.

    It was 2048, and its comment sized that when the kart was "roughly 10 m2". The
    kart is **13.83 m2**, which at 512 px/m wants 86.4% of a 2048 atlas in island
    area -- unpackable, and it did not pack: 276 of 295 objects sat outside the
    0-1 square, the worst by 4,146 texels, while the bake reported `295/295`
    because it never checked. 4096 lands the same set at 21.6% with 3.70x of
    headroom.
    """

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
    "tank_capacity": (
        "Art. 9.3's 8 litre minimum is what makes the tank exist, and 8.5 L is the "
        "catalogue size that clears it -- but the mesh is built from tank_width/"
        "_depth/_height, which the docstring shows is 14.7 L of bounding box at 58% "
        "fill. Publishing the rated capacity in the manifest is the point; a "
        "geometry module cannot read a volume. Delete this entry if a module ever "
        "solves the shell to hit it."
    ),
    # --- the driver -------------------------------------------------------
    #
    # **Empty, and that is the point.** `tools/blender/kartlib/driver.py` landed for
    # issue #17 and reads all thirty-four fields of the driver block, so all
    # thirty-four exemptions are gone. An exemption that outlives the module reading
    # it is a field nobody reads wearing a note that says otherwise, and
    # `check_field_coverage` is fatal on it.
    #
    # That includes `driver_shoulder_z` and `driver_eye_z`, whose entries said they
    # would stay because scripts/look/kartview.gd and scripts/look/lookdev.gd read
    # them in Godot off the manifest. **They still do, and the exemptions still had
    # to go**: this gate asks whether a *Python module in this package* reads a
    # field, `write_manifest` publishes every field of `KartParams` unconditionally
    # so a Godot reader was never what kept one off the fatal list, and leaving those
    # two behind fails the build outright through `covered_but_exempt`. #17 report.
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


#: How far up the column axis the lower bearing's bore sits from the threaded tip.
#: `derived`: the journal spans 14.5-30 mm from the tip, midpoint 22.3.
COLUMN_JOURNAL_OFFSET: float = 0.022


def column_axis(p: KartParams) -> tuple[float, float, float]:
    """Unit vector up the column, pointing up and rearward at the driver."""
    return (0.0, -math.sin(p.column_rake), math.cos(p.column_rake))


def lower_bore(p: KartParams) -> tuple[float, float, float]:
    """The lower bearing's bore centre — **the authored end of the whole chain.**

    The bracket is welded to the frame and cannot move, so this is the datum and
    everything upstream of it is derived. The previous arrangement was inverted:
    `steering_column_base` derived the column's *fixed* lower end from its *free*
    upper end through a hardcoded 402 mm, so no expression anywhere in the build
    mentioned the bracket that carries the column — which is why `joints.py`
    measured `chassis_steering_hoop`/`steering_bearing` at 37.46 mm and the hoop
    itself as touching nothing at all.

    `steering_bearing`'s bore centre *is* this point and the column's journal
    centre is the same expression, so their contact is identity and the only edit
    that can open a gap is an edit to `steering_bore_y`/`_z` — which is exactly
    the edit that should move the column.
    """
    return (0.0, p.steering_bore_y, p.steering_bore_z)


def column_top(p: KartParams) -> tuple[float, float, float]:
    """The column's open upper end, `column_length` up the axis from the tip."""
    base = lower_bore(p)
    axis = column_axis(p)
    reach = p.column_length - COLUMN_JOURNAL_OFFSET
    return tuple(base[i] + axis[i] * reach for i in range(3))


def wheel_center(p: KartParams) -> tuple[float, float, float]:
    """Steering wheel centre — **derived, never authored.**

        axis = (0, -sin 36, +cos 36) = (0, -0.5878, +0.8090)
        wheel_center = lower_bore + axis * (490 - 22 + 25)
                     = (0, 477 - 289.8, 97 + 398.8)  ->  (0, +187, +496)

    which is the centre measured independently off the side view at
    (0, +187, +496): the chain is *closed*, not merely consistent. The upper
    support's bore, 366 mm up the same axis, lands at (0, +262, +393) against a
    measured (0, +263, +393) — 1 mm.

    `wheel_center_y` 0.320 and `wheel_center_z` 0.480 were authored fields and are
    deleted; the old pair was 133 mm too far forward.
    """
    base = lower_bore(p)
    axis = column_axis(p)
    reach = p.column_length - COLUMN_JOURNAL_OFFSET + p.hub_stack
    return tuple(base[i] + axis[i] * reach for i in range(3))


def wheel_rake(p: KartParams) -> float:
    """Rake of the **wheel plane** from vertical: the column's plus the wedge's."""
    return p.column_rake + p.wheel_incline_delta


def seat_back_top(p: KartParams) -> tuple[float, float, float]:
    """Rearmost, highest point of the shell's chord. `derived`, spec §40.3.

    Checks against Tillett's published C = 460, a dimension the derivation never
    used: lip (0, +30, 132) to here is sqrt(395² + 235²) = **459.6**. A published
    figure nobody fed in coming back to 0.4 mm is the reason to believe the rest.
    """
    return (
        0.0,
        p.seat_y - p.seat_height * math.tan(p.seat_shell_rake),
        p.seat_z + p.seat_height,
    )


def pedal_bar_y(p: KartParams) -> float:
    """Foot-bar station. `derived`: pivot less the arm's rearward lean,
    610 - 180 sin 8 = 585.0."""
    return p.pedal_pivot_y - p.pedal_arm_length * math.sin(p.pedal_arm_rake)


def pedal_bar_z(p: KartParams) -> float:
    """Foot-bar height, 50 + 180 cos 8 = 228.2. `pedal_z` is this rounded."""
    return p.pedal_pivot_z + p.pedal_arm_length * math.cos(p.pedal_arm_rake)


def driver_foot_pitch(p: KartParams) -> float:
    """Sole angle above horizontal, radians. `derived`, spec §60.1.4 (#202).

    The foot is posed by two contacts, neither of them authored: the heel rests
    on the tray-top plane (`driver_heel_plane_z`) and the sole lies tangent to
    the live foot bar's Ø18 cylinder. Heel-to-bar-center is then
    `hypot(driver_foot_link, bar_radius)` -- the tangent length is exactly the
    foot link -- and the pitch is the angle up to the bar center minus the
    tangent's own half-angle. 53.7 deg at the built pedal, which is a driver
    with the heel down and the toes up on an organ pedal, not a flat foot.
    """
    bar_radius = p.pedal_bar_diameter * 0.5
    reach = math.hypot(p.driver_foot_link, bar_radius)
    dz = pedal_bar_z(p) - p.driver_heel_plane_z
    if dz >= reach:
        raise SystemExit(
            "params.py: the foot bar is %.1f mm above the heel plane and the "
            "whole foot is only %.1f mm long -- the sole cannot reach it"
            % (dz * 1000.0, reach * 1000.0)
        )
    return math.atan2(dz, math.sqrt(reach * reach - dz * dz)) - math.asin(
        bar_radius / reach
    )


def driver_heel(p: KartParams) -> tuple[float, float, float]:
    """Heel contact point. `derived`: on the tray-top plane, walked back from
    the bar center so the sole's tangency works out; x is the ankle's, the foot
    yaws about the heel toward the pedal at `pedal_separation`/2."""
    bar_radius = p.pedal_bar_diameter * 0.5
    reach = math.hypot(p.driver_foot_link, bar_radius)
    dz = pedal_bar_z(p) - p.driver_heel_plane_z
    heel_y = pedal_bar_y(p) - math.sqrt(reach * reach - dz * dz)
    return (p.driver_ankle_x, heel_y, p.driver_heel_plane_z)


def driver_ball(p: KartParams) -> tuple[float, float, float]:
    """Ball-of-foot contact, ON the bar's surface rather than at its center.
    `derived`: heel plus the foot link along the pitched sole. The lateral is
    `pedal_separation`/2 -- throttle x + is the right foot, brake x - the left --
    so #201 moving the pedals moves the feet with them. #202."""
    pitch = driver_foot_pitch(p)
    _, heel_y, heel_z = driver_heel(p)
    return (
        p.pedal_separation * 0.5,
        heel_y + p.driver_foot_link * math.cos(pitch),
        heel_z + p.driver_foot_link * math.sin(pitch),
    )


def driver_ankle(p: KartParams) -> tuple[float, float, float]:
    """Ankle joint. `derived`: the (`driver_ankle_forward`, `driver_ankle_rise`)
    offset applied in the sole's frame, rotated with the foot's pitch."""
    pitch = driver_foot_pitch(p)
    _, heel_y, heel_z = driver_heel(p)
    cos_p, sin_p = math.cos(pitch), math.sin(pitch)
    return (
        p.driver_ankle_x,
        heel_y + p.driver_ankle_forward * cos_p - p.driver_ankle_rise * sin_p,
        heel_z + p.driver_ankle_forward * sin_p + p.driver_ankle_rise * cos_p,
    )


def driver_knee(p: KartParams) -> tuple[float, float, float]:
    """Knee joint, the §60.1.4 two-link solve, live. `derived` from the hip, the
    ankle and the Drillis & Contini thigh/shank links; the knee is the elbow of
    that chain and bends up, above the hip-to-ankle line. Fatal if the chain
    cannot close -- a pedal moved out of leg reach is a cockpit that does not
    fit, not a knee to fudge."""
    _, ankle_y, ankle_z = driver_ankle(p)
    dy, dz = ankle_y - p.driver_hip_y, ankle_z - p.driver_hip_z
    d = math.hypot(dy, dz)
    thigh, shank = p.driver_thigh_link, p.driver_shank_link
    if not (abs(thigh - shank) < d < thigh + shank):
        raise SystemExit(
            "params.py: hip-to-ankle is %.1f mm and the leg links are %.0f + "
            "%.0f -- the two-link chain cannot close"
            % (d * 1000.0, thigh * 1000.0, shank * 1000.0)
        )
    along = (thigh * thigh + d * d - shank * shank) / (2.0 * d)
    lift = math.sqrt(thigh * thigh - along * along)
    unit_y, unit_z = dy / d, dz / d
    return (
        p.driver_knee_x,
        p.driver_hip_y + along * unit_y - lift * unit_z,
        p.driver_hip_z + along * unit_z + lift * unit_y,
    )


def sprocket_pitch_radius(pitch: float, teeth: int) -> float:
    """`p / (2 sin(pi/N))`, and the exact form matters at low tooth counts.

    The approximation `p*N/(2*pi)` is 0.03% small at 82 teeth and **1.1% small at
    12**, which is 0.24 mm of radius — and that is the 1.9 mm by which the chain
    was built inside a Ø18 output shaft. Not a field, so it takes its arguments
    rather than the parameter block.
    """
    return pitch / (2.0 * math.sin(math.pi / teeth))
