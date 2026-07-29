"""Issue #105, respecified by #190 — the five homologated bodywork elements.

`docs/KART_SPEC.md` §50 is the design and it outranks every comment here. Art.
**4.10.1**, PDF p. 11, is the part list this module is measured against:

> According to the class, it must be made of one front fairing, one front fairing
> mounting kit, one front panel, two side bodyworks and one rear wheel protection.

Six items. This module used to build **four panels, no front panel and no
mounting kit**, and every one of the four was undersized because it had been
fitted to whichever chassis tube it happened to sit next to rather than to a
figure:

    front fairing            512 mm wide    >= 1000 required (9.5.2)   -488
    rear wheel protection    572 mm wide    >= 1340 required (9.5.5.1) -768
    side pod outer face      482 from axis  618..664 by the datum      -136..-182
    side pod length          560 mm         rear gap 82.5 <= 60 (9.5.4) illegal by 22.5

The last of those was a sixth undersize panel nobody had listed, and it is the
shape of the whole issue: `sidepod_length` was chosen so the pod fitted between
two tires, and nothing measured the gap the article actually limits.

What makes the plastics read right, in order of how much each one costs to get
wrong:

1.  **Every panel is a shell with real thickness, and every free edge is a
    returned lip.** Art. 4.10.2 sets *"the minimum radius of any angles or corners
    is 5 mm"*, and a 3.8 mm wall physically cannot carry a 5 mm edge radius -- the
    most a flat wall's cut edge can hold is half its thickness, 1.9 mm. So the rim
    is not a chamfer: the skin rolls inward through 180 degrees on a 5 mm outer
    radius, which needs a 10 mm band of material. `_loft_shell` builds that, and
    `build.bevel_object` is no longer run on any panel -- at high detail its 4 mm
    offset chamfered most of the thickness off a 3.8 mm wall.
2.  **The side pods are not parallel-sided.** Art. 9.5.4's datum plane runs
    through the outer front edge of the front wheel *and* of the rear wheel, so
    with tracks of 1240 and 1400 it tapers 4.36 degrees in plan. One constant
    cannot be right at both ends. Real pods splay outward toward the back for this
    reason, which had been read as styling.
3.  **The fairing picks up on the front bumper's UPPER bar**, through the mounting
    kit Art. 4.10.1 lists as its own homologated item. It used to hang off two
    molded pins on the lower bar, and the lower bar is capped at a 110 mm tube top
    -- there is no height at which one bar can both be legal and carry a
    1090 x 287 x 227 panel.
4.  **Panels are separate objects.** Contact displacement needs one panel to move
    without the others; livery needs one UV island set per panel. The rear
    protection is three objects for a stronger reason: Art. 9.5.5.1 requires the
    two adjustable outer parts to be *"a color that is clearly different from the
    main part"*, and two colors need two materials, which need two meshes.

Coordinates: +X right, +Y forward, +Z up. The side pods are built on the right and
mirrored; the fairing, the front panel and the rear protection are built whole,
because each crosses the centerline and a mirrored half would put a seam down the
middle of a surface the front camera looks straight at.

Interfaces published for later milestones:

    nose_fairing_pivot   the hook-clamp line; issue #105's contact displacement
                         rotates the fairing about this, which is what Art.
                         9.5.2's Appendix 9 vertical push test measures
"""

from __future__ import annotations

import math

import bmesh
import bpy
from mathutils import Vector

from . import build
from . import params as P


# --- the returned lip ------------------------------------------------------

#: Outer radius of the returned lip on every free edge of every panel.
#:
#: `sourced`: Art. 4.10.2, PDF p. 11, *"Except the wheel covers, the minimum
#: radius of any angles or corners is 5 mm."* Exactly on the limit, which is
#: legitimate here in a way it is not for a bumper's straight length -- a fold
#: radius is the *largest* radius on the corner it forms, so a modeling pass can
#: only make it rounder.
LIP_RADIUS: float = 0.005

#: How far the returned flap runs back inside the panel past the fold.
#: `estimated`: the fold itself is the 10 mm band Art. 4.10.2 forces (2 x 5), and
#: 6 mm of flange behind it is what a thermoformed return looks like. It is
#: entirely inside the cavity and no camera sees it; it is here so the shell stays
#: watertight and the winding gate can measure a signed volume.
LIP_FLAP: float = 0.006


# --- dimensions that belong in params.py -----------------------------------
#
# Every dimension of a *panel* is now in `params.py`: `nose_width`, `nose_height`,
# `nose_depth`, `nose_bottom_z`, `panel_thickness`, `front_panel_*`, `sidepod_*`
# and `rear_prot_*`. What is left here is **shape** -- the tables that say how a
# surface gets from one authored dimension to the next -- plus two frame paths
# this module is not allowed to import.

#: Top of the fairing, against |x| in meters.
#:
#: Keyed on absolute |x| rather than on a fraction of the half-width, because
#: everything it has to clear is at an absolute x. The two endpoints are the
#: parameters: 267 at the spine is `nose_bottom_z + nose_height` and 215 at the
#: tip is 85 mm of panel against 227 at the spine, read off the OTK M4 form's
#: front elevation as ~0.37 of the spine height. `_nose_top_z` substitutes the
#: parameter at the centerline and fades the correction out, so a
#: `--set nose_height=` sweep moves the spine rather than being transcribed here.
NOSE_TOP_Z: tuple[tuple[float, float], ...] = (
    (0.000, 0.267),
    (0.120, 0.264),
    (0.240, 0.257),
    (0.380, 0.240),
    (0.470, 0.228),
    (0.545, 0.215),
)

#: Bottom edge of the fairing, against |x| in meters. Flat under the spine and
#: turning up outboard -- the outer ends of a CIK fairing lift, and against the
#: falling top edge that lift is most of what reads as a fairing from the side.
#:
#: It is flat out to |x| = 0.20 for a measured reason and not for a stylistic one:
#: the front bumper's **lower** bar runs at z 75..95 (Art. 9.4.1's 70..110 tube-top
#: band) and its corner reaches x 195 inside the panel's y window, so a bottom edge
#: that had already lifted to 75 mm by then would have the bar through its skin.
#: 130 at the tip is `estimated` off the same front elevation.
NOSE_BOTTOM_Z: tuple[tuple[float, float], ...] = (
    (0.000, 0.040),
    (0.200, 0.042),
    (0.300, 0.052),
    (0.420, 0.085),
    (0.545, 0.130),
)

#: Height of the fairing's forward-most point, against |x| in meters.
#:
#: 108 at the centerline is `derived` and unchanged: the two nose-hoop tiers'
#: forward tubes occupy z 75..95 and 209..225 at y +935, and 108 is inside the gap
#: between them, which is why the face reaches its most forward point there rather
#: than at its crown -- on a real kart you see the lower bar under the fairing and
#: the upper bar above it. Outboard the apex rises with the whole section: at the
#: tip the panel only spans 130..215, so the apex has to be inside that.
NOSE_APEX_Z: tuple[tuple[float, float], ...] = (
    (0.000, 0.108),
    (0.200, 0.113),
    (0.380, 0.133),
    (0.545, 0.170),
)

#: How far the apex is swept back from the centerline apex, against |x| /
#: half-width. `estimated`: the previous curve x 1.8, which is what carries it from
#: a 256 mm half-width to 545. A fairing is arrowed in plan and a constant leading
#: edge reads as a plank; at the tip the setback is 189 mm, i.e. a tip depth of
#: 98 mm against 287 at the spine.
NOSE_APEX_SETBACK: tuple[tuple[float, float], ...] = (
    (0.00, 0.000),
    (0.25, 0.014),
    (0.50, 0.054),
    (0.75, 0.112),
    (1.00, 0.189),
)

#: Fore-aft inset of the fairing's two rear free edges from `P.nose_lip_y`. The
#: top edge runs a little further back than the bottom one, so the open back faces
#: down and rearward rather than straight back -- which is what lets the mounting
#: kit reach in under it.
#:
#: The top table is nearly zero because `nose_depth` is measured to the top rear
#: edge and Art. 9.5.2 measures its 180 mm wheel gap to *"the back of the fairing"*,
#: which is that edge. The bottom table is **40 mm**, and that is a measured
#: clearance rather than shape: `chassis_cross_front` is a loop whose frontmost tube
#: reaches y +775 at its outer surface (`overhang_front_frame` = `G2` = 250 +-10 on
#: the CRG form), so a lower skin whose rear edge sat on the 742 lip would have the
#: loop straight through it -- 236 triangle pairs, which is the waiver #190 opened
#: against this panel. At +782 the skin stops 7 mm forward of the tube.
#:
#: Spec §50.8 asserts the panel is *"entirely forward of the loop"* at a 742 lip.
#: It is not: 742 < 775. The underside of a real CIK fairing is shallower than its
#: top for exactly this reason -- 247 mm against 287.
NOSE_BACK_TOP_INSET: tuple[tuple[float, float], ...] = (
    (0.00, 0.000),
    (0.50, 0.004),
    (1.00, 0.000),
)
NOSE_BACK_BOTTOM_INSET: tuple[tuple[float, float], ...] = (
    (0.00, 0.040),
    (0.50, 0.042),
    (1.00, 0.030),
)

#: Fore-aft station of the fairing's single air vent, and its diameter.
#:
#: `sourced`: Art. 9.5.2, *"Only one air vent hole is allowed, its diameter must
#: not exceed 12mm and it must be located on the rear face of the front fairing."*
#: Built at 11 so a faceted circle cannot measure over the cap. It is placed low on
#: the rear wall rather than at the crown because the same article requires the
#: fairing not to retain water or gravel, and a vent at the top drains nothing.
#:
#: Not modeled as a hole. A 11 mm aperture through a 3.8 mm wall is four rings of
#: geometry on a panel the low-poly mesh spends 13 sections on, and issue #19's
#: normal bake is where a feature that size belongs. Recorded here because the
#: compliance table has a row for it and a row with nothing behind it is worse than
#: no row.
NOSE_VENT_DIAMETER: float = 0.011
NOSE_VENT_Z: float = 0.070

#: The fairing's molded mounting bosses: (|x|, y, top z). Each is a short post from
#: the panel's inner lower skin up to the kit part it carries, in the panel's own
#: mesh so that a displaced fairing takes its mounts with it.
#:
#:     +-0.115   the two hook clamps, Art. 9.5.2's *"1 mm spacing"* pair
#:     +-0.225   the U-frame's front span, on the form's 450 mm leg spacing
#:     +-0.275   the two Ø16 struts, on the form's 550 mm spacing
#:
#: These replace the two molded Ø14 pins that used to reach the *lower* nose bar.
#: Art. 4.10.1 lists the mounting kit as a separate homologated item and Art. 9.5.2
#: specifies its clamp geometry, so it cannot be two studs inside the panel's mesh.
NOSE_BOSSES: tuple[tuple[float, float, float], ...] = (
    (0.115, 0.790, 0.163),
    (0.225, 0.955, 0.150),
    (0.275, 0.830, 0.175),
)
NOSE_BOSS_DIAMETER: float = 0.014

#: The mounting kit, all `sourced` off the OTK M4 form `100/CA/20` p. 2 except
#: where noted. The form dimensions `acciaio Ø20x1.5` at 450 mm and `acciaio
#: Ø16x1.5` at 550 mm, and the 450 matches Art. 9.4.1's own *"550.0 mm apart"* /
#: *"450.0 mm apart"* pair on the two bumper bars -- a form and the article
#: agreeing on four numbers across two bars is the strongest corroboration in the
#: whole bodywork section.
KIT_U_DIAMETER: float = 0.020
KIT_U_LEG_HALF: float = 0.225
KIT_U_CLAMP_X: float = 0.170
KIT_U_FRONT_Y: float = 0.955
KIT_U_Z: float = 0.150
KIT_STRUT_DIAMETER: float = 0.016
KIT_STRUT_HALF: float = 0.275
KIT_STRUT_Y: float = 0.830
KIT_STRUT_Z: float = 0.175
KIT_STRUT_TARGET_X: float = 0.150
"""Where each Ø16 strut clamps the upper bumper bar, in |x|.

**On the bar's straight run at y +935, not on its corner.** The corner is where
the temptation is -- it is nearest the strut's own station -- and it is 41 mm
outboard of where a straight interpolation between the article's two dimensioned
points puts it: `frame._corner` pushes the control point out to x 248.6 so that
the *built* straight measures the regulation 385, and the filleted path then runs
0.2524 -> 0.275 rather than 0.1925 -> 0.275. A strut aimed at the interpolation
missed the tube by 28 mm and would have been a declared joint measured apart.
Clamped 20 mm inside the straight's own end instead, where the tube's position is
`nose_upper_straight` and nothing else."""

#: The two support tubes of the hook clamps, and their fore-aft spacing.
#:
#: `sourced`: Art. 9.5.2 requires *"the distance of 60.1 mm minimum between the 2
#: support tubes of the clamps as well as the 1 mm spacing between the hook clamps
#: and the front fairing mounting kits"*. 65 mm is 4.9 over the floor, so a bend
#: fillet cannot pull it under. The Ø12 and the +-140 span are `estimated` -- no
#: form dimensions the release tubes; 12 mm is what an M10 hook clamp closes on and
#: 140 matches the mechanism's width in the OTK photograph.
KIT_TUBE_SPACING: float = 0.065
KIT_TUBE_DIAMETER: float = 0.012
KIT_TUBE_HALF: float = 0.140
KIT_TUBE_Y: float = 0.790

#: Lateral station of the two hook clamps, and how far they stand off the tubes.
#: The 1.0 mm is `sourced` -- Art. 9.5.2 states it -- and it is inside
#: `joints.CONTACT_TOLERANCE` = 2.0 by construction, so the joint passes without a
#: waiver. That is the whole reason the tolerance is not zero.
KIT_HOOK_X: float = 0.115
KIT_HOOK_STANDOFF: float = 0.001

#: Where along the U-frame's front span the two link stubs stand that carry the
#: release tubes. Inboard of the hooks so a hook clamp closes on bare tube.
KIT_LINK_X: float = 0.045
KIT_LINK_DIAMETER: float = 0.010

#: Front panel: the plan-view bow, and the forward lean.
#:
#: The panel leans **forward** going up, 6.4 degrees, which is what opens Art.
#: 9.5.3's hands clearance: its top edge is at y +620 and its bottom at +585, and
#: the gap from the top edge to the steering wheel's nearest rim point at
#: (0, +463, +552) is hypot(157, 52) = 166 mm against a 50 mm minimum. A vertical
#: panel at +585 would still clear 50, by 68 mm. `estimated` as shape, `derived` as
#: clearance.
FRONT_PANEL_TOP_Y: float = 0.620
FRONT_PANEL_BOTTOM_Y: float = 0.585

#: How far the panel's ends sweep rearward, against |x| / half-width. `estimated`:
#: a nassau panel is bowed in plan, and a flat one reads as a signboard.
FRONT_PANEL_SWEEP: tuple[tuple[float, float], ...] = (
    (0.00, 0.000),
    (0.55, 0.010),
    (1.00, 0.032),
)

#: Where the panel's two lower stays land on the front loop, and the one upper bar
#: on the steering column support.
#:
#: Art. 9.5.3: *"The panel's lower section must be securely attached to the front
#: part of the chassis frame, directly or indirectly. Its upper part must be
#: securely attached to the steering column support with one or more independent
#: bars."* `chassis_steering_support_upper` is that support -- wave 1 built it, and
#: before #190 this kart had no such part and no panel to hang off it.
#:
#: The stays splay outboard from x +-110 on the panel to x +-250 on the loop,
#: because that is where the loop *is*: its leg centerline passes (+-250, +572) and
#: at the panel's own x +-110 the loop is 175 mm further forward, at y +760.
FRONT_PANEL_STAY_X: float = 0.135
FRONT_PANEL_STAY_MID: tuple[float, float, float] = (0.175, 0.578, 0.145)
FRONT_PANEL_STAY_FRAME_X: float = 0.250
FRONT_PANEL_STAY_DIAMETER: float = 0.016

#: The upper bar is **one part with two legs**, which is how Art. 9.5.3's *"one or
#: more independent bars"* is satisfied at a part count of one. Off-center at
#: x +-60 rather than on the centerline, because a single central bar from the
#: panel's top edge to the support's apex passes 5.5 mm from the steering column's
#: surface -- measured -- and a bar through the column is worse than a bar that is
#: not there.
#:
#: The support-end station is a fraction along the support's own leg rather than a
#: point, so it tracks `steering_support_foot_x` and the apex instead of restating
#: them: 0.90 of the way from the foot to the apex.
FRONT_PANEL_BAR_X: float = 0.060
FRONT_PANEL_BAR_SUPPORT_T: float = 0.90
FRONT_PANEL_BAR_DIAMETER: float = 0.016

#: Extra inset of the pod's outer face inboard of `sidepod_datum_x0 -
#: sidepod_inset`, **in millimeters of inset and never as a fraction**, against
#: normalized station from the pod's front edge to its rear.
#:
#: That distinction is the whole content of spec §50.4. `SIDEPOD_OUT_FRACTION` --
#: which this replaces -- ran 0.960 at the front to 1.000 at the widest station,
#: read as a fraction of the face position, and 4% of 640 mm is 26 mm. Plus any
#: base inset that walks the face out of Art. 9.5.4's band, and the band is 29 mm
#: rather than 40 because the datum has two readings 11 mm apart. As millimeters
#: the same visual taper is bounded by construction: worst total inset is 22.0 mm
#: against the axis-plane datum and 32.9 mm against the literal one, both at the
#: front edge, and both inside 40.
SIDEPOD_TAPER: tuple[tuple[float, float], ...] = (
    (0.00, 0.014),
    (0.18, 0.003),
    (0.45, 0.000),
    (0.72, 0.001),
    (0.88, 0.006),
    (1.00, 0.013),
)

#: Bottom edge of the pod's C-section, against the same station. Lowest at the
#: widest station and lifting at both ends, which is half the fore-and-aft taper.
#: Art. 9.5.4 wants 25-60 mm of ground clearance and the clearance is the panel's
#: *minimum* gap, so 48 is the compliant figure and the lifted ends are shape.
#: The lifted ends are much flatter than they were -- 54 and 53 rather than 70 and
#: 66 -- and that is a clearance rather than a restyle. With the face out at the
#: article's datum the lower side bar runs *inside* the C at x 490..510, z 70..90,
#: and the mouth's bottom free edge is at x 505: an edge at z 70 is inside the bar,
#: and its returned lip curls 10 mm further up into it. At 54 the lip tops out at
#: 64 and clears the bar's underside by 6 mm.
SIDEPOD_BOTTOM_Z: tuple[tuple[float, float], ...] = (
    (0.00, 0.054),
    (0.25, 0.050),
    (0.55, 0.048),
    (0.85, 0.050),
    (1.00, 0.053),
)

#: Height of the section above that bottom edge, as a fraction of
#: `sidepod_height`. A fraction rather than a second table of absolute heights so
#: that the parameter is read by the geometry: 1.0 at the deepest station means the
#: pod is exactly `sidepod_height` tall there.
#: The ends taper much less than they did -- 0.900 and 0.910 rather than 0.756 and
#: 0.833 -- and again it is the upper side bar that forces it. At 0.756 the front
#: station's top edge is at z 190 while Art. 9.4.2's upper bar sits at z 165..185,
#: so the shell's upper run crossed the bar's own tube: measured, 66 triangle pairs
#: per side. At 0.900 the top edge is 216 everywhere and the shell passes 37 mm
#: outboard of the bar at its height, which is what *"securely attached to the side
#: bumpers"* needs geometrically -- the bar inside the C, not through its wall.
SIDEPOD_HEIGHT_FRACTION: tuple[tuple[float, float], ...] = (
    (0.00, 0.900),
    (0.20, 0.960),
    (0.55, 1.000),
    (0.80, 0.985),
    (1.00, 0.910),
)

#: Top and bottom of the pod's widest *band*, not a single widest point, so both
#: side bars fit inside the C. Art. 9.5.4 requires the side bodywork to be
#: *"securely attached to the side bumpers"*, and with the face out at the article's
#: own datum both bars now run inside the section rather than outboard of it -- the
#: lower at (500, z 80) and the upper at (560, z 175).
SIDEPOD_BULGE_TOP_Z: tuple[tuple[float, float], ...] = (
    (0.00, 0.142),
    (0.50, 0.140),
    (1.00, 0.140),
)
SIDEPOD_BULGE_BOTTOM_Z: tuple[tuple[float, float], ...] = (
    (0.00, 0.092),
    (0.50, 0.096),
    (1.00, 0.092),
)

#: Where the pods bolt to the lower side bar, and the bracket arm's diameter.
#: Two per side is what a CIK pod carries. Both stations sit on the bar's 420 mm
#: straight run, so the pickup is (0.500, y, 0.080) whatever the corner arithmetic
#: does.
SIDEPOD_MOUNT_Y: tuple[float, ...] = (0.240, -0.060)
SIDEPOD_MOUNT_DIAMETER: float = 0.014

#: Height on the pod's flank the bracket arms leave from. On the bulge, which is
#: the part of the shell directly outboard of the bar.
SIDEPOD_MOUNT_Z: float = 0.110

#: Centerline of the lower side bar the pods bolt to, right-hand side, copied from
#: `frame.py:_bumpers`. Duplicated rather than imported because a geometry module
#: may not read another module's objects (see `build.BuildContext`), and because
#: the brackets have to end on the *filleted* centerline rather than on the control
#: polyline -- near a bend those differ by most of `bend_radius`.
#: Rebuilt at #190 against what `frame._side_bumpers` actually sweeps, because the
#: old copy was two revisions stale and it showed: it put the bar's rear end at
#: y -355 where Art. 9.4.2's 500 mm socket pitch puts it at -100, so the rear
#: bracket was aimed at (500, -200) -- 45.36 mm of air, measured. The straight is
#: `sidebar_lower_straight` = 420 centered on `sidebar_straight_center_y` = 100, i.e.
#: y -110..+310 at `sidebar_x_lower` = 500, and both legs come in to sockets on the
#: rail centerline at `sidebar_mount_front_y` +-250 mm.
SIDE_BAR_PATH: tuple[tuple[float, float, float], ...] = (
    (0.172, 0.400, 0.080),
    (0.500, 0.310, 0.080),
    (0.500, -0.110, 0.080),
    (0.172, -0.100, 0.080),
)

#: The rear protection's cross-section, as (fore-aft fraction, z fraction) of the
#: box `rear_prot_front_y` / `rear_prot_depth` / bottom / `rear_prot_height` make.
#:
#: A fraction pair rather than absolute (y, z) so that the depth and height stay
#: single-owner parameters sourced off the KG C2 form. The front face is vertical
#: for the lower 0.78 of the height and only then curves over the top, which is a
#: clearance rather than a style: `chassis_rear_bumper`'s rear straight is at
#: y -725, z 130..150, and the panel's front wall at z 140 has to stay behind it.
#: Measured, the wall's inner surface is at y -708.8 and the bumper's front surface
#: at -715, so the pair is 6.2 mm clear and the joint `joints.py` used to declare
#: between them is deleted -- Art. 4.11 puts the supports on *"the two main tubes
#: of the chassis"*, and the bumper is a pair the panel must not overlap.
REAR_SECTION: tuple[tuple[float, float], ...] = (
    (0.00, 0.00),
    (0.00, 0.78),
    (0.09, 0.97),
    (0.24, 1.00),
    (0.76, 1.00),
    (0.92, 0.92),
    (1.00, 0.62),
    (0.98, 0.14),
)

#: Bottom edge of the rear protection against |x| in meters, over the whole
#: 1390 mm assembly. `sourced` where it is 40 and `estimated` where it is 95.
#:
#: Art. 9.5.5.1: *"Ground clearance: 25 mm minimum and 60.0 mm maximum in at least
#: three spaces of a 200.0 mm minimum width, located in the extension of the rear
#: wheels and the centreline of the chassis."* So the three windows are the
#: regulated part and the lift between them is what makes them windows:
#:
#:     centerline window   |x| <= 0.100   200 mm wide, exactly the minimum
#:     wheel windows       |x| 0.485..0.695   210 mm, the rear tire's own span
#:
#: The 210 is why the assembly is 1390 and not the KG C2's 1360: at 1360 the panel
#: edge is at 680 against the tire's inner edge at 485, which is a 195 mm window --
#: five short. `_rear_bottom_z` reads this table, and the transitions are 50 mm
#: wide so a window's own 200 mm is measured on the flat.
#: `rear_prot_bottom_z` is the window figure and `REAR_LIFT` is how far the panel
#: rises between them, so a `--set rear_prot_bottom_z=` sweep moves the regulated
#: edge and leaves the shape alone.
REAR_LIFT: float = 0.055
REAR_WINDOWS: tuple[tuple[float, float], ...] = (
    (0.000, 0.100),
    (0.485, 0.695),
)
REAR_WINDOW_RAMP: float = 0.050


def _rear_bottom_z(p: P.KartParams, x: float) -> float:
    """Bottom edge of the rear protection at lateral station `x`.

    `rear_prot_bottom_z` inside a clearance window, plus `REAR_LIFT` outside one,
    ramped over `REAR_WINDOW_RAMP` so each window's own 200 mm is measured on the
    flat rather than on a slope.
    """
    distance = abs(x)
    nearest = min(
        max(low - distance, distance - high, 0.0) for low, high in REAR_WINDOWS
    )
    fraction = min(1.0, nearest / REAR_WINDOW_RAMP)
    return p.rear_prot_bottom_z + REAR_LIFT * fraction

#: Where the main part stops and the two adjustable outer parts start, and how wide
#: the overlap of the adjustment slot is.
#:
#: `estimated`, read off the KG C2's photograph where the differently-colored ends
#: are about the outer quarter per side. The split falls between the centerline
#: window and the wheel windows so it cuts neither, which is the constraint that
#: makes it a measurement rather than a preference.
REAR_SPLIT_X: float = 0.400
REAR_SLOT_OVERLAP: float = 0.040

#: Where the two rear supports pick up. Art. 4.11: *"These supports must be mounted
#: (possibly by means of a flexible system) on the two main tubes of the chassis
#: (respecting the homologated dimension F)."*
#:
#: **The two main tubes, not the rear bumper.** `frame.frame_half_rear` is 310 and
#: the rail is straight at that station from y -48 back to the rear end, so both
#: ends of the support are exact: the rail end is on the rail's own centerline and
#: the panel end is 1 mm inside the panel's front wall.
REAR_SUPPORT_RAIL_Y: float = -0.590
REAR_SUPPORT_MID: tuple[float, float, float] = (0.270, -0.660, 0.085)
REAR_SUPPORT_PANEL_X: float = 0.260
REAR_SUPPORT_PANEL_Z: float = 0.115
REAR_SUPPORT_DIAMETER: float = 0.016
"""Each support is a dog-leg rather than a straight run, and both jogs are
measured clearances.

Inboard, because `chassis_rear_bumper`'s two legs stand at x +-310 -- the same rail
station -- and rise from y -715, z 50 to y -725, z 140: a support running straight
back at x 310 passes within 0.2 mm of one, measured. At x 260 it clears by 40 mm.

And it ends at z 115 rather than 100 because the panel's front wall only exists
above the local bottom edge, which is 95 mm between the clearance windows -- an end
at z 100 with a 8 mm tube is inside the wall by 1 mm at best and in open air below
it at worst."""


# --- entry point -----------------------------------------------------------


def build_module(context: build.BuildContext) -> None:
    """Entry point. See `build.BuildContext` for the contract."""
    collection = context.collection("bodywork")
    material = context.material("bodywork_wrap")

    root = build.empty("bodywork_root", (0.0, 0.0, 0.0), collection, size=0.10)

    _nose_fairing(context, collection, material, root)
    _fairing_kit(context, collection, root)
    _front_panel(context, collection, material, root)
    _sidepods(context, collection, material, root)
    _rear_protection(context, collection, material, root)


# --- curve and surface helpers ---------------------------------------------


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


def _loop_leg_y(p: P.KartParams, x: float) -> float:
    """Where the front loop's leg centerline crosses a given |x|.

    Duplicated from `frame.py:_loop_leg_y` for the same reason as
    `SIDE_BAR_PATH`: a geometry module may not read another module's objects. Both
    the fairing's U-frame and the front panel's stays clamp the loop, and a clamp
    aimed at a station the loop does not occupy is a bracket in mid-air -- which is
    exactly what `joints.py` waives 104.65 mm of for the pedal mounts.

    The loop runs from the rail's node at (`frame_half_node`, `frame_node_y`) to
    its frontmost segment at (`frame_half_front`, `loop_front_y`), so this is a
    straight interpolation between two parameters and not a shape choice.
    """
    span = p.frame_half_node - p.frame_half_front
    if span < 1e-9:
        return p.frame_node_y
    fraction = (p.frame_half_node - abs(x)) / span
    return p.frame_node_y + fraction * (P.loop_front_y(p) - p.frame_node_y)


def _catmull_rom(points: list[Vector], per_segment: int) -> list[Vector]:
    """Sample an open uniform Catmull-Rom spline through every control point.

    Through, not near. A panel's control points are the dimensions that matter --
    the apex, the top and bottom free edges, the widest station -- so a curve that
    only approximated them would quietly stop honoring the tables above. End
    tangents are clamped.
    """
    count = len(points)
    if count < 3 or per_segment < 1:
        return list(points)

    def at(index: int) -> Vector:
        return points[min(max(index, 0), count - 1)]

    sampled: list[Vector] = []
    for segment in range(count - 1):
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
    sampled.append(points[-1])
    return sampled


def _span_steps(detail: build.Detail) -> int:
    """Number of lofted sections across a panel.

    Read off `detail.tube_segments` rather than fixed, because the module is built
    twice and issue #19's normal bake needs the two to be the same shape at two
    densities. One more than the tube count so the centerline of a panel spanning
    both sides lands on a section rather than between two, which is what keeps the
    nose fairing's spine a ridge instead of a flat.
    """
    return detail.tube_segments + 1


def _profile_steps(detail: build.Detail) -> int:
    """Samples per control-point segment around a panel's section."""
    return max(2, detail.tube_segments // 4)


def _lip_steps(detail: build.Detail) -> int:
    """Arc steps in a returned lip's 180 degree fold.

    From `bevel_segments`, because the lip is what replaced the bevel: 2 at low
    detail and 5 at high. Two is enough for a 5 mm radius on the low-poly mesh --
    the fold is 10 mm of a panel that is 1090 mm wide -- and issue #19's bake
    carries the rest.
    """
    return max(2, 1 + detail.bevel_segments)


def _grid_normals(grid: list[list[Vector]]) -> list[list[Vector]]:
    """Surface normal at every point of a lofted grid, by central difference.

    Computed rather than taken from the face normals because the offset surface
    has to be built before any face exists, and because a per-vertex normal is
    what keeps the shell's wall thickness even around a tight corner -- a per-face
    offset opens gaps at the rim.
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
            row.append(
                normal.normalized() if normal.length > 1e-12 else Vector((0.0, 0.0, 1.0))
            )
        normals.append(row)
    return normals


def _outward_normals(grid: list[list[Vector]]) -> list[list[Vector]]:
    """Per-vertex normals, signed so they point *away* from the cavity.

    The sign is decided from the geometry rather than passed in: a panel section is
    a C, so its centroid lies in the cavity, and the normals that point away from
    the centroid are the outside of the shell. Deciding it here means the four
    panels can each be authored in whatever winding reads most naturally in its own
    table without one of them silently ending up inside out.
    """
    normals = _grid_normals(grid)
    centroid = Vector((0.0, 0.0, 0.0))
    count = 0
    for row in grid:
        for point in row:
            centroid += point
            count += 1
    centroid /= float(count)

    outward = 0.0
    for u, row in enumerate(grid):
        for v, point in enumerate(row):
            outward += (point - centroid).dot(normals[u][v])
    sign = 1.0 if outward > 0.0 else -1.0
    return [[normals[u][v] * sign for v in range(len(grid[u]))] for u in range(len(grid))]


def _lip_offsets(thickness: float, steps: int) -> list[tuple[float, float]]:
    """The returned lip's cross-section, as (along-surface, along-normal) offsets.

    Walked from the outer skin's free edge round to the inner skin's, in the plane
    spanned by the outward normal `n` and the in-surface direction `e` that points
    out of the panel. `R` is the **outer** fold radius, which is the one Art.
    4.10.2 regulates; the inner radius is `R - thickness`, so at 3.8 mm of wall a
    5 mm outer fold has a 1.2 mm inner one and the returned flap sits
    `2R - 2 x thickness` = 2.4 mm clear of the inner skin.

    The two endpoints are omitted: (0, 0) is the outer skin's own edge vertex and
    (0, -thickness) is the inner skin's, and both already exist in the grid.
    """
    radius = LIP_RADIUS
    inner = radius - thickness
    offsets: list[tuple[float, float]] = []
    for step in range(1, steps + 1):
        angle = math.pi * step / steps
        offsets.append(
            (radius * math.sin(angle), -radius + radius * math.cos(angle))
        )
    offsets.append((-LIP_FLAP, -2.0 * radius))
    offsets.append((-LIP_FLAP, -2.0 * radius + thickness))
    offsets.append((0.0, -radius + inner * math.cos(math.pi)))
    for step in range(steps - 1, 0, -1):
        angle = math.pi * step / steps
        offsets.append((inner * math.sin(angle), -radius + inner * math.cos(angle)))
    return offsets


def _edge_direction(grid: list[list[Vector]], u: int, v: int, normal: Vector) -> Vector:
    """In-surface direction pointing out of the panel at a perimeter vertex."""
    rows = len(grid)
    columns = len(grid[0])
    if u == 0:
        reference = grid[1][v]
    elif u == rows - 1:
        reference = grid[rows - 2][v]
    elif v == 0:
        reference = grid[u][1]
    else:
        reference = grid[u][columns - 2]
    direction = grid[u][v] - reference
    direction -= normal * direction.dot(normal)
    if direction.length < 1e-9:
        return Vector((0.0, 0.0, 0.0))
    return direction.normalized()


def _loft_shell(
    bm: bmesh.types.BMesh,
    grid: list[list[Vector]],
    thickness: float,
    lip_steps: int,
) -> None:
    """A lofted surface, its offset copy, and a **returned lip** around the rim.

    The lip is the visible payoff and the reason a panel is not modeled as a single
    surface. It used to be a flat band of `thickness` closed off with
    `build.bevel_object`, and Art. 4.10.2's *"the minimum radius of any angles or
    corners is 5 mm"* cannot be met that way at all: a flat wall's cut edge holds
    at most half its thickness, 1.9 mm on a 3.8 mm panel, and the chamfer was
    3.1 mm short of legal on every free edge of every panel. Worse, at high detail
    `build.bevel_object` runs a 4.0 mm offset on a 3.8 mm band, which chamfers away
    more than the wall.

    Vertices are emitted row by row and the perimeter is walked in a fixed order,
    so the whole thing is a function of the grid alone.
    """
    rows = len(grid)
    columns = len(grid[0])
    normals = _outward_normals(grid)
    inner_grid = [
        [grid[u][v] - normals[u][v] * thickness for v in range(columns)]
        for u in range(rows)
    ]

    outer = [[bm.verts.new(grid[u][v]) for v in range(columns)] for u in range(rows)]
    inner = [
        [bm.verts.new(inner_grid[u][v]) for v in range(columns)] for u in range(rows)
    ]

    for u in range(rows - 1):
        for v in range(columns - 1):
            bm.faces.new(
                (outer[u][v], outer[u][v + 1], outer[u + 1][v + 1], outer[u + 1][v])
            )
            bm.faces.new(
                (inner[u][v], inner[u + 1][v], inner[u + 1][v + 1], inner[u][v + 1])
            )

    perimeter: list[tuple[int, int]] = []
    perimeter.extend((0, v) for v in range(columns))
    perimeter.extend((u, columns - 1) for u in range(1, rows))
    perimeter.extend((rows - 1, v) for v in range(columns - 2, -1, -1))
    perimeter.extend((u, 0) for u in range(rows - 2, 0, -1))

    offsets = _lip_offsets(thickness, lip_steps)
    rings: list[list[bmesh.types.BMVert]] = [
        [outer[u][v] for u, v in perimeter]
    ]
    for along, across in offsets:
        ring: list[bmesh.types.BMVert] = []
        for u, v in perimeter:
            normal = normals[u][v]
            edge = _edge_direction(grid, u, v, normal)
            ring.append(bm.verts.new(grid[u][v] + edge * along + normal * across))
        rings.append(ring)
    rings.append([inner[u][v] for u, v in perimeter])

    count = len(perimeter)
    for index in range(len(rings) - 1):
        lower, upper = rings[index], rings[index + 1]
        for step in range(count):
            following = (step + 1) % count
            bm.faces.new(
                (lower[step], lower[following], upper[following], upper[step])
            )

    # Closed manifold, so one recalculation gives every face the outward normal and
    # neither the lip's winding nor the band's has to be reasoned about.
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))


def _panel(
    context: build.BuildContext,
    name: str,
    grid: list[list[Vector]],
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    extra=None,
) -> bpy.types.Object:
    """One panel: a lofted shell with a returned lip, and no bevel.

    `build.bevel_object` is deliberately not called on anything this function
    makes. The lip already carries the regulated 5 mm radius, and a 4 mm chamfer on
    a 3.8 mm wall is not a highlight, it is a hole.
    """
    bm = bmesh.new()
    _loft_shell(bm, grid, context.params.panel_thickness, _lip_steps(context.detail))
    if extra is not None:
        extra(bm)
    return build.object_from_bmesh(
        name, bm, collection, material=material, shade_smooth=True
    )


def _point_at_y(path: list[Vector], y: float) -> Vector:
    """Where a monotonic-in-y polyline crosses a given y.

    Used to land a bracket on the *filleted* centerline of a frame tube rather than
    on its control polyline -- near a bend those differ by most of `bend_radius`,
    which is enough to leave a bracket hanging in air.
    """
    for index in range(len(path) - 1):
        a, b = path[index], path[index + 1]
        if (a.y - y) * (b.y - y) <= 0.0 and abs(b.y - a.y) > 1e-9:
            return a.lerp(b, (y - a.y) / (b.y - a.y))
    return path[0] if abs(path[0].y - y) < abs(path[-1].y - y) else path[-1]


# --- the nose fairing ------------------------------------------------------


def _nose_half_width(p: P.KartParams) -> float:
    """Half the fairing's width. `nose_width` and nothing else.

    There used to be a `NOSE_HALF_WIDTH_LIMIT = 0.256` clamp here, measured against
    the old single nose bar's dive to rail height, and it is gone. The bar the
    fairing picks up on is the **upper** one, at a tube top of 225 mm inside Art.
    9.4.1's 200-250 band, and the panel's cavity spans z 43.8..263.2 at the spine --
    so both bumper bars pass through the open back without touching a skin, which
    is what a real CIK nose does.
    """
    return p.nose_width * 0.5


def _nose_top_z(p: P.KartParams, distance: float) -> float:
    """Top edge of the fairing at |x| = `distance`, honoring `nose_height`.

    The spine's height is the parameter; the shoulder's is shape. The correction
    between them fades over the first 160 mm so that a `--set nose_height=` sweep
    visibly moves the spine without walking the shoulder off the tip figure the
    form was read at. With the default kart the correction is exactly zero, which
    is the check that the parameter and the table agree.
    """
    correction = (p.nose_bottom_z + p.nose_height) - NOSE_TOP_Z[0][1]
    fade = max(0.0, 1.0 - distance / 0.160)
    return _table(NOSE_TOP_Z, distance) + correction * fade


def _nose_bottom_z(p: P.KartParams, distance: float) -> float:
    """Bottom edge at |x| = `distance`, honoring `nose_bottom_z` at the spine."""
    correction = p.nose_bottom_z - NOSE_BOTTOM_Z[0][1]
    fade = max(0.0, 1.0 - distance / 0.200)
    return _table(NOSE_BOTTOM_Z, distance) + correction * fade


def _nose_section(p: P.KartParams, s: float, steps: int) -> list[Vector]:
    """One (y, z) section of the fairing at lateral fraction `s` in [-1, 1].

    Seven control points, sampled as a spline: rear top edge, crown, upper front,
    apex, lower front, rear bottom edge. The apex is the one with a hard constraint
    on it -- it has to sit between the two nose-hoop tiers -- and everything else
    follows from the tables.
    """
    a = abs(s)
    x = s * _nose_half_width(p)

    apex_y = P.nose_apex_y(p) - _table(NOSE_APEX_SETBACK, a)
    back_y = P.nose_lip_y(p)
    apex_z = _table(NOSE_APEX_Z, abs(x))
    top_z = _nose_top_z(p, abs(x))
    bottom_z = _nose_bottom_z(p, abs(x))
    back_top_y = back_y + _table(NOSE_BACK_TOP_INSET, a)
    back_bottom_y = back_y + _table(NOSE_BACK_BOTTOM_INSET, a)

    # Fore-aft placement of the crown and of the two front curves is expressed as a
    # fraction of the section's own depth rather than in meters. The wing sections
    # are barely a third the depth of the spine, and fixed offsets there put the
    # crown behind the apex -- which a Catmull-Rom answers with a loop rather than
    # with an error.
    depth_top = apex_y - back_top_y
    depth_bottom = apex_y - back_bottom_y

    controls = [
        Vector((x, back_top_y, top_z - 0.010)),
        Vector((x, back_top_y + depth_top * 0.30, top_z)),
        Vector((x, apex_y - depth_top * 0.32, top_z - 0.004)),
        Vector(
            (
                x,
                apex_y - min(0.020, depth_top * 0.16),
                apex_z + (top_z - apex_z) * 0.45,
            )
        ),
        Vector((x, apex_y, apex_z)),
        Vector(
            (
                x,
                apex_y - min(0.028, depth_bottom * 0.22),
                bottom_z + (apex_z - bottom_z) * 0.35,
            )
        ),
        Vector((x, back_bottom_y, bottom_z)),
    ]
    return _catmull_rom(controls, steps)


def _nose_fairing(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """The CIK front fairing, 1090 x 287 x 227, held by the mounting kit.

    Built whole rather than as a mirrored half. The spine runs down the centerline
    and is the first thing the front camera and every replay still looks at; a
    mirror seam there would be a crease in the one surface that has to be
    continuous.
    """
    p = context.params
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    grid = [
        _nose_section(p, -1.0 + 2.0 * index / (spans - 1), steps)
        for index in range(spans)
    ]

    def bosses(bm: bmesh.types.BMesh) -> None:
        _nose_bosses(context, bm, steps)

    obj = _panel(
        context, "bodywork_nose_fairing", grid, collection, material, extra=bosses
    )

    # The pivot is the hook-clamp line, which is what the fairing really rotates
    # about when contact displaces it -- and it is what Art. 9.5.2's Appendix 9
    # vertical push test measures, five loads on the centerline through a
    # 200 x 450 x 10 mm plate. A pivot at the panel's centroid would displace it in
    # a way no real fairing moves.
    pivot = build.empty(
        "nose_fairing_pivot",
        (0.0, KIT_TUBE_Y, KIT_U_Z),
        collection,
        parent=root,
        size=0.06,
    )
    context.publish("nose_fairing_pivot", pivot)
    build.set_parent(obj, pivot)


def _nose_bosses(
    context: build.BuildContext, bm: bmesh.types.BMesh, steps: int
) -> None:
    """Six molded posts from the panel's inner lower skin up to the kit.

    Same object as the panel deliberately: the bosses move with the fairing when it
    is displaced, and a fairing whose mounts stayed behind would read as broken
    rather than as knocked askew. Each one starts *inside* the lower skin rather
    than on it, so gate 2 measures contact at the kit end and gate 1 sees one
    watertight panel rather than a boss tangent to its own shell.
    """
    p = context.params
    # A `--set nose_width=` sweep below the outermost boss station would put that
    # boss outside the skin it is molded into, which reads as a floating stud rather
    # than as a narrow fairing. Fatal, in the same family as the signed-volume
    # winding assert: it is not a defect a render shows you.
    half_width = _nose_half_width(p)
    if half_width <= NOSE_BOSSES[-1][0]:
        raise SystemExit(
            "bodywork.py: nose_width = %.4f m gives a half-width of %.4f m, which "
            "is inboard\n            of the mounting kit's outer strut boss at "
            "%.4f m. Art. 9.5.2's minimum\n            width is 1.000 m; the "
            "sourced OTK M4 panel is 1.090."
            % (p.nose_width, half_width, NOSE_BOSSES[-1][0])
        )
    for boss_x, boss_y, top_z in NOSE_BOSSES:
        # Starts 4 mm inside the inner lower skin, so the boss is rooted in the
        # panel rather than tangent to it -- two faceted surfaces that meet exactly
        # register as an intersection, and this is one object either way.
        start_z = _nose_bottom_z(p, boss_x) + p.panel_thickness - 0.004
        for side in (-1.0, 1.0):
            build.sweep_tube(
                bm,
                [
                    (side * boss_x, boss_y, start_z),
                    (side * boss_x, boss_y, top_z),
                ],
                NOSE_BOSS_DIAMETER * 0.5,
                context.detail.tube_segments,
            )


# --- the front fairing mounting kit ----------------------------------------


def _fairing_kit(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    root: bpy.types.Object,
) -> None:
    """Art. 4.10.1's *"one front fairing mounting kit"*, in seven parts.

    Art. 9.5.2 specifies its clamp geometry by name -- *"the distance of 60.1 mm
    minimum between the 2 support tubes of the clamps as well as the 1 mm spacing
    between the hook clamps and the front fairing mounting kits"* -- so it is a
    mechanism with dimensioned parts and not two studs buried in the panel's mesh.
    Steel, not plastic: Art. 9.4 wants *"magnetic steel round tubing"* and the OTK
    M4 form says `acciaio`.
    """
    p = context.params
    detail = context.detail
    material = context.material("axle_steel")

    clamp_y = _loop_leg_y(p, KIT_U_CLAMP_X)
    rail_z = P.rail_z(p)

    # The U-frame, plus the two link stubs that carry the release tubes. One object
    # because Art. 4.10.1 counts one kit and the stubs are welded to the frame; two
    # objects would put the same weld in `joints.py` twice.
    bm = bmesh.new()
    build.tube(
        bm,
        [
            (-KIT_U_CLAMP_X, clamp_y, rail_z),
            (-KIT_U_LEG_HALF, KIT_U_FRONT_Y, KIT_U_Z),
            (KIT_U_LEG_HALF, KIT_U_FRONT_Y, KIT_U_Z),
            (KIT_U_CLAMP_X, clamp_y, rail_z),
        ],
        KIT_U_DIAMETER,
        detail,
        p.bend_radius * 0.5,
    )
    for side in (-1.0, 1.0):
        build.sweep_tube(
            bm,
            [
                (side * KIT_LINK_X, KIT_TUBE_Y - KIT_TUBE_SPACING * 0.5, KIT_U_Z),
                (side * KIT_LINK_X, KIT_U_FRONT_Y, KIT_U_Z),
            ],
            KIT_LINK_DIAMETER * 0.5,
            detail.tube_segments,
        )
    support = build.object_from_bmesh(
        "bodywork_fairing_support_u", bm, collection, material=material,
        shade_smooth=True,
    )
    build.set_parent(support, root)

    # The two Ø16 struts, on the form's 550 mm spacing. They clamp the **upper**
    # bumper bar: the lower bar never reaches x 275, and the upper one is the only
    # bar in a height band that can carry a panel this tall. Aimed at the bar's
    # nominal centerline and run 10 mm past it, so a fillet moving the real
    # centerline by a few millimeters still leaves the clamp gripping tube.
    bar_x = min(KIT_STRUT_TARGET_X, p.nose_upper_straight * 0.5 - 0.020)
    for label, side in (("l", -1.0), ("r", 1.0)):
        start = Vector((side * KIT_STRUT_HALF, KIT_STRUT_Y, KIT_STRUT_Z))
        target = Vector((side * bar_x, P.nose_y(p), p.nose_upper_z))
        reach = (target - start).length
        end = start + (target - start) * ((reach + 0.010) / reach)
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [tuple(start), tuple(end)],
            KIT_STRUT_DIAMETER * 0.5,
            detail.tube_segments,
        )
        strut = build.object_from_bmesh(
            "bodywork_fairing_strut_%s" % label, bm, collection, material=material,
            shade_smooth=True,
        )
        build.set_parent(strut, root)

    # The two support tubes of the clamps, 65 mm apart.
    for label, offset in (
        ("fwd", KIT_TUBE_SPACING * 0.5),
        ("aft", -KIT_TUBE_SPACING * 0.5),
    ):
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                (-KIT_TUBE_HALF, KIT_TUBE_Y + offset, KIT_U_Z),
                (KIT_TUBE_HALF, KIT_TUBE_Y + offset, KIT_U_Z),
            ],
            KIT_TUBE_DIAMETER * 0.5,
            detail.tube_segments,
        )
        tube = build.object_from_bmesh(
            "bodywork_fairing_kit_tube_%s" % label, bm, collection, material=material,
            shade_smooth=True,
        )
        build.set_parent(tube, root)

    # The two hook clamps, standing 1.0 mm off both tubes. A plate over the pair
    # with a tab down each side, which is what a CIK quick-release hook is; solid
    # would put it *through* the tubes and lose the one dimension the article
    # states.
    lift = KIT_TUBE_DIAMETER * 0.5 + KIT_HOOK_STANDOFF
    for label, side in (("l", -1.0), ("r", 1.0)):
        bm = bmesh.new()
        build.box(
            bm,
            (0.016, KIT_TUBE_SPACING + 0.024, 0.010),
            (side * KIT_HOOK_X, KIT_TUBE_Y, KIT_U_Z + lift + 0.005),
        )
        for tab in (-1.0, 1.0):
            build.box(
                bm,
                (0.016, 0.008, 0.026),
                (
                    side * KIT_HOOK_X,
                    KIT_TUBE_Y + tab * (KIT_TUBE_SPACING * 0.5 + 0.010),
                    KIT_U_Z + lift - 0.008,
                ),
            )
        hook = build.object_from_bmesh(
            "bodywork_fairing_hook_%s" % label, bm, collection, material=material
        )
        build.bevel_object(hook, detail)
        build.set_parent(hook, root)


# --- the front panel -------------------------------------------------------


def _front_panel_section(p: P.KartParams, s: float, steps: int) -> list[Vector]:
    """One (y, z) section of the nassau panel at lateral fraction `s` in [-1, 1].

    Five control points from the bottom free edge up the leaning face to the top
    free edge. The lean is the regulated dimension: Art. 9.5.3 wants *"a gap of at
    least 50.0 mm between the panel and the steering wheel"*, which is a hands
    clearance and not a clearance to the front road wheel -- front matter §4 says
    so because it was misread once.
    """
    a = abs(s)
    x = s * p.front_panel_width * 0.5
    sweep = _table(FRONT_PANEL_SWEEP, a)
    bottom = Vector((x, FRONT_PANEL_BOTTOM_Y - sweep, p.front_panel_bottom_z))
    top = Vector((x, FRONT_PANEL_TOP_Y - sweep, p.front_panel_top_z))
    height = top.z - bottom.z

    controls = [
        bottom,
        Vector((x, bottom.y + (top.y - bottom.y) * 0.22 - 0.004, bottom.z + height * 0.25)),
        Vector((x, bottom.y + (top.y - bottom.y) * 0.50 - 0.006, bottom.z + height * 0.50)),
        Vector((x, bottom.y + (top.y - bottom.y) * 0.78 - 0.004, bottom.z + height * 0.75)),
        top,
    ]
    return _catmull_rom(controls, steps)


def _front_panel(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Art. 9.5.3's front panel, its two lower stays and its one upper bar.

    **The part did not exist at all**, and Art. 4.10.1 lists it as one of six
    homologated items. It is also the panel §60 found cannot carry a three-digit
    number: a compliant zone is 370 x 170 mm and this article caps the panel at
    300 mm wide, so the front zone is a two-digit zone or a class marking and the
    racing numbers live on the pods and the rear protection.
    """
    p = context.params
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    grid = [
        _front_panel_section(p, -1.0 + 2.0 * index / (spans - 1), steps)
        for index in range(spans)
    ]
    panel = _panel(context, "bodywork_front_panel", grid, collection, material)
    build.set_parent(panel, root)

    steel = context.material("axle_steel")
    rail_z = P.rail_z(p)

    for label, side in (("l", -1.0), ("r", 1.0)):
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                (
                    side * FRONT_PANEL_STAY_X,
                    FRONT_PANEL_BOTTOM_Y
                    - _table(
                        FRONT_PANEL_SWEEP,
                        FRONT_PANEL_STAY_X / (p.front_panel_width * 0.5),
                    )
                    + 0.002,
                    p.front_panel_bottom_z + 0.005,
                ),
                (
                    side * FRONT_PANEL_STAY_MID[0],
                    FRONT_PANEL_STAY_MID[1],
                    FRONT_PANEL_STAY_MID[2],
                ),
                (
                    side * FRONT_PANEL_STAY_FRAME_X,
                    _loop_leg_y(p, FRONT_PANEL_STAY_FRAME_X),
                    rail_z,
                ),
            ],
            FRONT_PANEL_STAY_DIAMETER * 0.5,
            detail.tube_segments,
        )
        stay = build.object_from_bmesh(
            "bodywork_front_panel_stay_%s" % label, bm, collection, material=steel,
            shade_smooth=True,
        )
        build.set_parent(stay, root)

    # One part, two legs. The support end is a fraction along
    # `chassis_steering_support_upper`'s own leg rather than a point, so it tracks
    # `steering_support_foot_x` and the apex instead of restating either.
    foot = Vector((p.steering_support_foot_x, p.cross_strut_y - 0.010, P.rail_top_z(p)))
    apex = Vector((0.0, p.steering_support_apex_y, p.steering_support_apex_z))
    landing = foot.lerp(apex, FRONT_PANEL_BAR_SUPPORT_T)
    bm = bmesh.new()
    for side in (-1.0, 1.0):
        build.sweep_tube(
            bm,
            [
                (side * FRONT_PANEL_BAR_X, FRONT_PANEL_TOP_Y, p.front_panel_top_z - 0.030),
                (side * landing.x, landing.y, landing.z),
            ],
            FRONT_PANEL_BAR_DIAMETER * 0.5,
            detail.tube_segments,
        )
    bar = build.object_from_bmesh(
        "bodywork_front_panel_bar", bm, collection, material=steel, shade_smooth=True
    )
    build.set_parent(bar, root)


# --- the side pods ---------------------------------------------------------


def _sidepod_face_x(p: P.KartParams, y: float, t: float) -> float:
    """Outer face of the pod at station `y`, normalized station `t`.

        x_face(y) = sidepod_datum_x0 - sidepod_datum_slope * y
                    - sidepod_inset - SIDEPOD_TAPER(t)

    Art. 9.5.4's datum, less a base inset and a fore-aft taper **in millimeters**.
    At the default kart: 618 at the front edge, 652 at the widest station, 664 at
    the rear -- against a single `sidepod_x` = 480 that was 136-182 mm inboard of
    where the face belongs, per side.
    """
    datum = p.sidepod_datum_x0 - p.sidepod_datum_slope * y
    return datum - p.sidepod_inset - _table(SIDEPOD_TAPER, t)


def _sidepod_section(p: P.KartParams, t: float, steps: int) -> list[Vector]:
    """One (x, z) C-section of the right side pod at normalized station `t`.

    Seven control points from the top free edge, outboard and down around the
    flank, to the bottom free edge. The C is the whole point: with the face out at
    Art. 9.5.4's datum, **both** side bars now run inside it -- the lower at
    (500, z 80) and the upper at (560, z 175) -- which is what *"securely attached
    to the side bumpers"* means geometrically.
    """
    y = p.sidepod_front_y - t * p.sidepod_length

    out_x = _sidepod_face_x(p, y, t)
    mouth_x = p.sidepod_mouth_x
    bottom_z = _table(SIDEPOD_BOTTOM_Z, t)
    top_z = bottom_z + p.sidepod_height * _table(SIDEPOD_HEIGHT_FRACTION, t)
    bulge_top_z = _table(SIDEPOD_BULGE_TOP_Z, t)
    bulge_bottom_z = _table(SIDEPOD_BULGE_BOTTOM_Z, t)

    controls = [
        Vector((mouth_x, y, top_z)),
        Vector((mouth_x + 0.052, y, top_z + 0.003)),
        Vector((out_x - 0.014, y, top_z - 0.032)),
        Vector((out_x, y, bulge_top_z)),
        Vector((out_x - 0.002, y, bulge_bottom_z)),
        Vector((mouth_x + 0.038, y, bottom_z + 0.007)),
        Vector((mouth_x, y, bottom_z)),
    ]
    return _catmull_rom(controls, steps)


def _sidepods(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Right pod plus its mirror, and four named brackets.

    Art. 9.5 requires *"the two side pods must be used together as a set"*, which
    is what makes the mirror correct rather than convenient.

    The brackets are parts now rather than stubs inside the pod's mesh, because a
    140 mm reach with a bushed clamp is a bracket and gate 2 cannot see a stub
    buried in a panel. They are also much longer than they were: the pod's mouth is
    outboard of the bar it bolts to, so the arm runs *inboard* from the flank
    rather than outboard from the mouth.
    """
    p = context.params
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    grid = [_sidepod_section(p, index / (spans - 1), steps) for index in range(spans)]

    right = _panel(context, "bodywork_sidepod_r", grid, collection, material)
    build.set_parent(right, root)

    # `build.mirror_x` copies the source's material slots, so the material is not
    # appended again here.
    left = build.mirror_x(right, "bodywork_sidepod_l", collection)
    build.set_parent(left, root)

    path = build.fillet(SIDE_BAR_PATH, p.bend_radius, detail.bend_segments)
    radius = p.tube_bumper * 0.5
    steel = context.material("axle_steel")

    for tag, mount_y in (("f", SIDEPOD_MOUNT_Y[0]), ("r", SIDEPOD_MOUNT_Y[1])):
        t = (p.sidepod_front_y - mount_y) / p.sidepod_length
        face = _sidepod_face_x(p, mount_y, t)
        target = _point_at_y(path, mount_y)
        for side_label, side in (("r", 1.0), ("l", -1.0)):
            # Started 4 mm **outboard** of the nominal face rather than 6 mm inside
            # it. The face is the widest point of the section and the bracket picks
            # up 30 mm below it, where the flank has already turned in -- and the
            # loft samples the section at 13 stations, so between two of them the
            # surface is a chord and sits inboard again. Measured, a start at
            # `face - 0.006` left the arm 3.04 mm off its own pod: a declared joint
            # that does not touch. Outboard, the bolt head reads as a bolt head.
            start = Vector((side * (face + 0.004), mount_y, SIDEPOD_MOUNT_Z))
            centre = Vector((side * target.x, target.y, target.z))
            direction = centre - start
            end = centre - direction.normalized() * (radius + 0.0015)
            bm = bmesh.new()
            build.sweep_tube(
                bm,
                [tuple(start), tuple(end)],
                SIDEPOD_MOUNT_DIAMETER * 0.5,
                detail.tube_segments,
            )
            bracket = build.object_from_bmesh(
                "bodywork_sidepod_bracket_%s%s" % (side_label, tag),
                bm,
                collection,
                material=steel,
                shade_smooth=True,
            )
            build.set_parent(bracket, root)


# --- the rear wheel protection ---------------------------------------------


def _rear_section(p: P.KartParams, x: float, steps: int) -> list[Vector]:
    """One (y, z) section of the rear protection at lateral station `x`.

    `REAR_SECTION` is in fractions of the box `rear_prot_depth` and
    `rear_prot_height` define, so the two sourced KG C2 dimensions stay
    single-owner and the three ground-clearance windows are a bottom-edge table
    rather than a second section.
    """
    front_y = P.rear_prot_front_y(p)
    bottom_z = _rear_bottom_z(p, x)
    top_z = bottom_z + p.rear_prot_height
    controls = [
        Vector(
            (
                x,
                front_y - along * p.rear_prot_depth,
                bottom_z + up * (top_z - bottom_z),
            )
        )
        for along, up in REAR_SECTION
    ]
    return _catmull_rom(controls, steps)


def _rear_grid(
    p: P.KartParams, x0: float, x1: float, spans: int, steps: int
) -> list[list[Vector]]:
    return [
        _rear_section(p, x0 + (x1 - x0) * index / (spans - 1), steps)
        for index in range(spans)
    ]


def _rear_protection(
    context: build.BuildContext,
    collection: bpy.types.Collection,
    material: bpy.types.Material,
    root: bpy.types.Object,
) -> None:
    """Three panels and two supports, 1390 mm across.

    Three panels is a **geometry** requirement before it is a livery one: Art.
    9.5.5.1 requires the two adjustable outer parts to be *"a color that is clearly
    different from the main part"* and Art. 4.11 repeats it, so two colors need two
    materials, which need two meshes.

    **The outer parts take `bodywork_contrast`.** They borrowed `seat_fiberglass`
    for one wave, because `build.MATERIALS` had no contrasting bodywork slot and
    `build.py` belonged to another agent — the render was right and the material's
    *name* was wrong, which is the kind of thing that survives a milestone if it is
    not written down. The slot exists now and the gate checks numerically that its
    color differs from the main part's by at least 0.250; the three liveries measure
    0.310, 0.270 and 0.380.
    """
    p = context.params
    detail = context.detail
    spans = _span_steps(detail)
    steps = _profile_steps(detail)

    main = _panel(
        context,
        "bodywork_rear_panel",
        _rear_grid(p, -REAR_SPLIT_X, REAR_SPLIT_X, spans, steps),
        collection,
        material,
    )
    build.set_parent(main, root)

    contrast = context.material("bodywork_contrast")
    inner = REAR_SPLIT_X - REAR_SLOT_OVERLAP
    for label, side in (("l", -1.0), ("r", 1.0)):
        outer = _panel(
            context,
            "bodywork_rear_outer_%s" % label,
            _rear_grid(p, side * inner, side * p.rear_prot_width * 0.5, spans, steps),
            collection,
            contrast,
        )
        build.set_parent(outer, root)

    steel = context.material("axle_steel")
    front_y = P.rear_prot_front_y(p)
    for label, side in (("l", -1.0), ("r", 1.0)):
        bm = bmesh.new()
        build.sweep_tube(
            bm,
            [
                (side * p.frame_half_rear, REAR_SUPPORT_RAIL_Y, P.rail_z(p)),
                (
                    side * REAR_SUPPORT_MID[0],
                    REAR_SUPPORT_MID[1],
                    REAR_SUPPORT_MID[2],
                ),
                (side * REAR_SUPPORT_PANEL_X, front_y - 0.001, REAR_SUPPORT_PANEL_Z),
            ],
            REAR_SUPPORT_DIAMETER * 0.5,
            detail.tube_segments,
        )
        support = build.object_from_bmesh(
            "bodywork_rear_support_%s" % label, bm, collection, material=steel,
            shade_smooth=True,
        )
        build.set_parent(support, root)
