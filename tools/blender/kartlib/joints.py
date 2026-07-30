"""Issue #192 — which parts of the kart touch which, and why.

Two invariants about a built kart are invisible in a render and were unchecked
until this file existed:

1.  **No part is built inside another.** The radiator was built through the gear
    lever, through the exhaust chamber and through the right sidepod for several
    milestones — 593 intersecting triangle pairs — and nothing objected. It was
    found by a human turning a viewport, which is not a loop that scales to an
    unattended overnight run.
2.  **Every part touches what it mounts to.** `steering_column`'s lower end
    stops 19.4 mm short of `chassis_steering_hoop`, whose entire stated purpose
    in `frame.py` is *"so that the column has something to be mounted to rather
    than floating"*.

Both are one fact per pair, so both read the same table. **A `Joint` does two
things at once**: it permits the pair to interpenetrate, and it *requires* the
pair to be in contact within `CONTACT_TOLERANCE`. That is deliberate, and it is
the property that makes the table worth writing rather than being two allowlists
that drift apart. A sidepod resting against the engine instead of against its
side bar passes any "is this part floating" test and fails the joint.

The contract, in full:

    * A pair that overlaps and is **not** declared is fatal.
    * A pair that is declared and is **further apart** than CONTACT_TOLERANCE is
      fatal.
    * A part whose nearest neighbor over the whole kart is further than
      CONTACT_TOLERANCE is fatal — the weak form, which catches a part that was
      never given a joint at all.
    * `kind` comes from `KINDS` and nothing else. An unknown kind fails at
      **import**, before a single mesh is built.
    * Patterns are `fnmatch` globs so forty bolts are one line. **A pattern that
      matches nothing is fatal**, because otherwise renaming a part silently
      deletes its declaration — the same failure shape as
      `Dictionary.get(key, default)` in CLAUDE.md, and as a `.gitignore` pattern
      that stopped matching.
    * Anything in `OPEN_DEFECTS` is downgraded to a printed warning naming its
      issue, and an entry there that **no longer fails** is fatal. A waiver list
      that cannot rot is the only kind worth having: the alternative is a red
      build that blocks a night's work, or a green build that has quietly
      stopped checking.

`why` is not decoration. Every entry says what physically joins the two parts,
because the next person to read this has to be able to tell a joint from a
collision, and no name pair carries that. Where a `why` cites a number, the
number was measured off the built mesh or read out of the module that builds it.
"""

from __future__ import annotations

import dataclasses
import fnmatch

#: Surface gap, in meters, inside which two parts count as touching. 2 mm.
#:
#: Not zero, and not a fudge: a joint that shares a surface exactly registers as
#: an *intersection* once both sides are faceted, so several modules deliberately
#: stand their parts off. The clearest case is now a **regulation** figure rather
#: than a modeling allowance: Art. 9.5.2 states *"the 1 mm spacing between the hook
#: clamps and the front fairing mounting kits"*, so
#: `bodywork_fairing_kit_tube_*`/`bodywork_fairing_hook_?` passes at exactly 1.0 mm
#: because the article says 1.0 mm. The engine mount table, by contrast, is 3 mm
#: clear of the floor tray. 2 mm passes the first and fails the second, which is the
#: correct outcome both times.
#:
#: `bodywork.MOUNT_STANDOFF` used to be cited here at 1.5 mm and is gone with the
#: two molded fairing pins it existed for -- Art. 4.10.1 lists the mounting kit as
#: its own homologated item, so the panel does not reach a frame tube at all.
CONTACT_TOLERANCE: float = 0.002

#: The closed vocabulary. Every one of these was drawn from a real joint on this
#: kart rather than invented for completeness, and adding a tenth means finding a
#: tenth kind of joint on a real kart first.
KINDS: frozenset[str] = frozenset(
    {
        # One continuous piece of material, separate meshes only because it was
        # easier to author that way: a tube welded to a tube, a fin brazed into a
        # core, a boss cast into a head.
        "welded",
        # Fastened with hardware through a flange or a bracket. A small standoff
        # is normal here and expected.
        "bolted",
        # A nut or a stud screwed into the other part, so the thread's flanks are
        # genuinely inside it.
        "threaded",
        # Interference fit: one part driven into the other's bore.
        "pressed",
        # One part sits in or on a seat in the other and is held there by load or
        # by a slip fit — a tire bead on a rim flange, a silencer over a pipe.
        "seated",
        # Held by a clamp, a spring or a hook that grips a surface rather than
        # passing through it.
        "clamped",
        # Chain over sprocket teeth. Nothing else on the kart meshes.
        "meshed",
        # A hose, cable or lead entering a fitting or passing over a boss.
        "routed",
        # One part passes through an opening in the other: an axle through a
        # bearing hanger, a tube through a pedal boss, a tray cut around a strut.
        "pierced",
    }
)


@dataclasses.dataclass(frozen=True)
class Joint:
    """One declared joint. `a` and `b` are `fnmatch` globs and are unordered."""

    a: str
    b: str
    kind: str
    why: str


@dataclasses.dataclass(frozen=True)
class Defect:
    """A known-failing pair, waived to a warning until its issue is closed.

    `gate` is `"overlap"`, `"gap"` or `"driver"`; `measured` is intersecting
    triangle pairs for the first and millimeters for the other two -- the
    **worst** figure the entry covers, measured at high detail when the waiver
    was written. For `"driver"` the millimeters are penetration depth into the
    driver volume (torso findings measured against the §60.1.1 rake-plane clip,
    ADR-0057), or the contact gap for a declared contact that does not touch;
    an occlusion-only finding carries the count of occluded sample points and
    its `why` says so. The number is here so that a waiver whose fault got
    *worse* is visible in a diff rather than being covered by the same one line.
    """

    a: str
    b: str
    gate: str
    measured: float
    issue: str
    why: str


# --- the joints ------------------------------------------------------------
#
# Grouped by the module that builds them. Within a group, ordered structure
# first and hardware last, which is roughly assembly order.

JOINTS: tuple[Joint, ...] = (
    # --- frame.py: the weldment ---------------------------------------------
    Joint(
        a="chassis_cross_*",
        b="chassis_rail_?",
        kind="welded",
        why="every cross member is welded through both main rails; the rails and "
        "the cross members are one weldment and a kart frame has no bolted "
        "joint anywhere in it",
    ),
    Joint(
        a="chassis_cross_tail",
        b="chassis_rear_bumper",
        kind="welded",
        why="the tail cross member and the rear bumper hoop meet at the same "
        "corner of the frame",
    ),
    Joint(
        a="chassis_rail_?",
        b="chassis_rear_bumper",
        kind="welded",
        why="the bumper hoop's two legs run forward and weld to the rail ends",
    ),
    Joint(
        a="chassis_rail_l",
        b="chassis_seat_strut_front_l",
        kind="welded",
        why="the FRONT seat stay starts on the rail, at the central strut's "
        "station. Same side only: a strut welded to the opposite rail would be a "
        "mirroring bug and this is where it would surface. **The rear stay is no "
        "longer here** -- Art. 9.1.2 puts the extra seat stays *between the rear "
        "axle brackets and the seat*, so the rear pair roots on the bearing "
        "hanger and has its own entry. It used to start on the rail at y -400 "
        "and end 78.07 mm from the shell, aimed at nothing",
    ),
    Joint(
        a="chassis_rail_r",
        b="chassis_seat_strut_front_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_bearing_hanger_l",
        b="chassis_seat_strut_rear_l",
        kind="welded",
        why="Art. 9.1.2: *\"Extra seat stays are allowed between the rear axle "
        "brackets and the seat.\"* The bracket is this hanger, and the stay welds "
        "to its plate 40 mm behind the axle line -- 40 rather than 0 because "
        "`axle_rear` occupies y -550..-500 at z 122.5..172.5 and a stay rooted on "
        "the axle line runs straight through it",
    ),
    Joint(
        a="chassis_bearing_hanger_r",
        b="chassis_seat_strut_rear_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_bearing_hanger_l",
        b="chassis_rail_l",
        kind="welded",
        why="the outer hanger plates moved from x +-185 to +-300 (spec §10.9), "
        "which is inside the rail's own tube at the axle line -- so they weld to "
        "the rail as well as to the rear cross member, and the axle line is "
        "carried by the two members that actually run under it",
    ),
    Joint(
        a="chassis_bearing_hanger_r",
        b="chassis_rail_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    # Art. 9.4.2 puts each side bar in **two welded tube attachments 500 +-5 mm
    # apart**, so the bar no longer touches the rail at all: it is seated in the
    # sockets and the sockets are welded to the rail. The old
    # `chassis_rail_?`/`chassis_side_bar_?` entries are deleted rather than
    # waived, because a bar that runs into its own rail is not what the article
    # describes -- and the built bar had no sockets at all.
    Joint(
        a="chassis_bumper_socket_side_*_l",
        b="chassis_rail_l",
        kind="welded",
        why="all four sockets on the left stand on the rail centerline, 10 mm "
        "inside the tube so the weld measures 0 mm rather than being a standoff "
        "nobody chose. Art. 9.4.2's *\"two welded tube attachments\"*",
    ),
    Joint(
        a="chassis_bumper_socket_side_*_r",
        b="chassis_rail_r",
        kind="welded",
        why="the right-hand four of the same joint",
    ),
    Joint(
        a="chassis_bumper_socket_side_lower_*_l",
        b="chassis_side_bar_l",
        kind="seated",
        why="Art. 9.4.2: the attachments must *\"allow for a 50.0 mm insertion of "
        "the bar\"*, so the sleeve is 50 mm long and the bar's end is inside it",
    ),
    Joint(
        a="chassis_bumper_socket_side_lower_*_r",
        b="chassis_side_bar_r",
        kind="seated",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_bumper_socket_side_upper_*_l",
        b="chassis_side_bar_upper_l",
        kind="seated",
        why="the upper bar's 50 mm insertion, same article",
    ),
    Joint(
        a="chassis_bumper_socket_side_upper_*_r",
        b="chassis_side_bar_upper_r",
        kind="seated",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_bumper_socket_side_upper_*_l",
        b="chassis_side_bar_l",
        kind="welded",
        why="the upper socket is a 135 mm post and the lower bar crosses it on "
        "the way out, so the two are welded where they meet. That is also what "
        "makes the pair one bracket rather than two",
    ),
    Joint(
        a="chassis_bumper_socket_side_upper_*_r",
        b="chassis_side_bar_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_bumper_socket_side_lower_front_l",
        b="chassis_bumper_socket_side_upper_front_l",
        kind="welded",
        why="the two posts at one station are 40 mm apart in y and share their "
        "root on the rail. Written per station rather than as a glob because the "
        "front and rear pairs are 500 mm apart and a glob would demand they touch",
    ),
    Joint(
        a="chassis_bumper_socket_side_lower_front_r",
        b="chassis_bumper_socket_side_upper_front_r",
        kind="welded",
        why="the right-hand front station",
    ),
    Joint(
        a="chassis_bumper_socket_side_lower_rear_l",
        b="chassis_bumper_socket_side_upper_rear_l",
        kind="welded",
        why="the left rear station",
    ),
    Joint(
        a="chassis_bumper_socket_side_lower_rear_r",
        b="chassis_bumper_socket_side_upper_rear_r",
        kind="welded",
        why="the right rear station",
    ),
    # The two front bumper bars are **not** welded to each other. Art. 9.4.1
    # keeps them 132 mm apart in two different height bands and joins them with
    # the front bumper support, which is a part rather than a joint -- *"Both bars
    # must be connected by the front bumper support."* The old entry said they
    # *"share their rearmost control point at the front cross member"*, which was
    # true of a kart with one bar at a height no bar may occupy.
    Joint(
        a="chassis_front_bumper_support",
        b="chassis_nose_hoop_lower",
        kind="welded",
        why="the two posts stand on the lower bar's straight run at x +-75",
    ),
    Joint(
        a="chassis_front_bumper_support",
        b="chassis_nose_hoop_upper",
        kind="welded",
        why="and reach the upper bar's straight run 132 mm above it",
    ),
    Joint(
        a="chassis_bumper_socket_front_*",
        b="chassis_cross_front",
        kind="welded",
        why="Art. 9.4.1's *\"two welded chassis frame attachments\"* per bar. All "
        "four land on the front loop's legs, and the spacing is the article's: "
        "450 mm for the lower bar and 550 for the upper. None of this was modeled "
        "before #190",
    ),
    Joint(
        a="chassis_bumper_socket_front_lower_?",
        b="chassis_nose_hoop_lower",
        kind="seated",
        why="*\"allow for a 50.0 mm insertion of the bar\"*, and the riser is what "
        "lifts the socket from the loop at z 50 to the bar at z 85 -- the bar is "
        "planar and horizontal because the 70..110 window is stated for the bar, "
        "not for its front straight",
    ),
    Joint(
        a="chassis_bumper_socket_front_upper_?",
        b="chassis_nose_hoop_upper",
        kind="seated",
        why="the same insertion on a 167 mm post, which is what a 200..250 mm "
        "height band forces",
    ),
    Joint(
        a="chassis_cross_front",
        b="chassis_kingpin_boss_?",
        kind="welded",
        why="the boss sits on the front loop's leg at the front axle line. Art. "
        "4.2.2 allows an articulated connection *\"only for the steering knuckle "
        "(through the king pin) and the steering\"*, so this is the one place the "
        "frame is permitted to articulate -- and there was nothing here at all "
        "before #190, which is why `frame.py` invented a rail position from the "
        "front hub and built the kingpins 925 mm apart",
    ),
    Joint(
        a="chassis_cross_mid_front",
        b="chassis_steering_support_upper",
        kind="welded",
        why="Art. 9.5.3's *\"one or more independent bars\"*: an inverted V off the "
        "central strut's top surface, leaning forward against the column",
    ),
    Joint(
        a="chassis_bearing_hanger_?",
        b="chassis_cross_rear",
        kind="welded",
        why="all three hanger plates are welded to the rear cross member; that "
        "member is what lifts the axle line 98 mm above the rails, which "
        "frame.py's docstring calls structural rather than decorative",
    ),
    Joint(
        a="chassis_steering_hoop",
        b="chassis_cross_front",
        kind="welded",
        why="the hoop's two feet come down to the frame beside the front cross "
        "member. This is the joint that makes the hoop part of the frame "
        "rather than a tube hanging in the footwell",
    ),
    Joint(
        a="chassis_floor_tray",
        b="chassis_rail_?",
        kind="bolted",
        why="the tray bolts on **top** of the rails -- frame.py item 2, and a "
        "tray under the rails puts the frame into the asphalt",
    ),
    # **Three tray joints are deleted rather than waived**, and that is the point
    # of Art. 4.6: the tray *"stretch[es] from the central strut to the front of
    # the chassis frame"*, so it runs y +40..+760 and the hangers, the rear cross
    # member and all four seat stays are between 460 and 1,285 mm away from it. A
    # declared joint whose two parts are half a meter apart is a statement about a
    # kart that no longer exists, and `_expand_or_die` is what would have caught
    # it. Deleted: `chassis_floor_tray`/`chassis_cross_rear`,
    # `chassis_floor_tray`/`chassis_bearing_hanger_?`,
    # `chassis_floor_tray`/`chassis_seat_strut_*`.
    Joint(
        a="chassis_floor_tray",
        b="chassis_cross_mid_front",
        kind="bolted",
        why="the central strut is the tray's rear edge, by Art. 4.6's own "
        "enumeration of the perimeter -- *\"the central strut, the longitudinal "
        "tubes and the front of the chassis frame\"*. The pan's underside is flush "
        "with the tube's top surface",
    ),
    Joint(
        a="chassis_floor_tray",
        b="chassis_cross_front",
        kind="bolted",
        why="and the loop's frontmost segment is its front edge, which is what "
        "*\"the front of the chassis frame\"* means. Both edges land on a tube "
        "rather than in mid-air, which is how the article is scrutineered",
    ),
    Joint(
        a="chassis_floor_tray",
        b="chassis_tray_edge_?",
        kind="welded",
        why="Art. 4.6's last sentence is mandatory and this part did not exist: "
        "*\"It must be laterally edged by a tube or a rim preventing the driver's "
        "feet from sliding off the floor tray.\"* The rails cannot serve -- their "
        "top is at z 65 and the pan's is at 69, so the rail stands 4 mm *below* "
        "the surface a foot slides off",
    ),
    Joint(
        a="chassis_floor_tray",
        b="chassis_steering_support_upper",
        kind="pierced",
        why="the pan is notched at its rear edge around the support's two feet. "
        "**Art. 4.6's two-hole allowance is not what covers this** -- that is for "
        "the steering column and the gear shift lever, and this is an edge notch "
        "rather than a hole, which is the reading spec §99 W1 asks for and the "
        "one every kart in the reference set is built to",
    ),
    Joint(
        a="chassis_tray_edge_l",
        b="chassis_rail_l",
        kind="welded",
        why="aft of the waist the pan's edge *is* the rail centerline, so the "
        "edging tube runs directly above the rail and welds to it continuously. "
        "Its centerline is at z 73 rather than spec §10.7's 77 for exactly this "
        "reason: at 77 the tube's underside is 4 mm clear of the rail's top and "
        "this weld would be declared and measured apart",
    ),
    Joint(
        a="chassis_tray_edge_r",
        b="chassis_rail_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    # --- wheels.py -----------------------------------------------------------
    Joint(
        a="wheel_fl_rim",
        b="wheel_fl_tire",
        kind="seated",
        why="the tire's bead seats on the rim flange, so the bead sits inside "
        "the rim's swept profile. Written out per corner rather than as "
        "wheel_??_rim x wheel_??_tire, because that glob's cross terms would "
        "demand the front-left tire touch the front-right rim",
    ),
    Joint(a="wheel_fr_rim", b="wheel_fr_tire", kind="seated", why="as front left"),
    Joint(a="wheel_rl_rim", b="wheel_rl_tire", kind="seated", why="as front left"),
    Joint(a="wheel_rr_rim", b="wheel_rr_tire", kind="seated", why="as front left"),
    Joint(
        a="axle_rear",
        b="wheel_r?_rim",
        kind="pierced",
        why="**the second clause of this entry used to be wrong and it mattered.** "
        "It read *\"on a kart the rear wheels are keyed to it, not to hubs\"*, and "
        "Art. 4.2.1 and Art. 4.3 both say otherwise: the hub is a chassis main part "
        "with a keyway of its own, so the axle is keyed to `hub_r?` and the hub is "
        "bolted to the rim. Spec §20.9 item 8 asked for this joint to be deleted on "
        "those grounds. It stays, because the geometry is still real: `axle_length` "
        "= 2 x `rear_hub_x`, so the axle's end and the rim's mounting plane are the "
        "same plane and the axle passes through the plate's Ø32 bore on its way to "
        "the wheel nuts. What is deleted is the reasoning",
    ),
    Joint(
        a="axle_rear",
        b="hub_r?",
        kind="pressed",
        why="Art. 4.17: the hub's whole purpose is *\"to enable the transfer of "
        "forces between the rim and the chassis\"*, and it does it by being bored "
        "onto the axle over all 90 mm of its length -- 37.5 mm at the old "
        "`axle_length` of 1.080",
    ),
    Joint(
        a="hub_rl",
        b="wheel_rl_rim",
        kind="bolted",
        why="3x M8 self-locking through the hub's outboard flange into the rim's "
        "plate. Art. 4.13 names the M8; the count is kart practice",
    ),
    Joint(
        a="hub_rr",
        b="wheel_rr_rim",
        kind="bolted",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="axle_key_hub_l",
        b="hub_rl",
        kind="seated",
        why="one of Art. 4.3's four keyways, and only one -- *\"one each for the "
        "left and right hub, one for the brake disc and one for the rear axle "
        "sprocket\"*. Written per side because the left key does not reach the right "
        "hub",
    ),
    Joint(a="axle_key_hub_r", b="hub_rr", kind="seated", why="the right-hand pair"),
    Joint(
        a="axle_key_disc",
        b="brake_disc_rear_hub",
        kind="seated",
        why="the disc's keyway. Its station moved with the bearing: spec §20.5 puts "
        "it at x -260 against a hanger plate at -185, and wave 1's plate at -300 "
        "carries the whole disc assembly out to -400",
    ),
    Joint(
        a="axle_key_sprocket",
        b="axle_sprocket",
        kind="seated",
        why="the fourth and last keyway. A fifth is not legal",
    ),
    Joint(
        a="axle_key_*",
        b="axle_rear",
        kind="seated",
        why="all four keys sit in the axle's own keyways, half in the shaft and "
        "half in whatever they drive. Art. 4.3 exempts the keyways from the wall "
        "table, which is why a 2.5 mm wall can carry an 8 x 4 key at all",
    ),
    Joint(
        a="axle_stub_fl",
        b="hub_fl",
        kind="pierced",
        why="the spindle runs into the hub's bore. **This used to be declared "
        "against `wheel_fl_rim`** and could not be after #190: the visible spindle "
        "run is `stub_axle_length` = 90 mm from the knuckle's face at 345 to the "
        "hub's inboard end at 435, and the rim's plate is 117.5 mm further out. The "
        "old joint only held because the stub was built from "
        "`front_hub_x - 0.090`, which put the kingpins 925 mm apart",
    ),
    Joint(a="axle_stub_fr", b="hub_fr", kind="pierced", why="as front left"),
    Joint(
        a="axle_stub_fl",
        b="knuckle_fl",
        kind="pressed",
        why="the spindle is a bolt through the knuckle's boss, and the boss is "
        "machined at `KINGPIN_INCLINATION` off the kingpin's normal so the wheel "
        "stands vertical -- which is what makes static camber 0 degrees",
    ),
    Joint(a="axle_stub_fr", b="knuckle_fr", kind="pressed", why="as front left"),
    Joint(
        a="hub_fl",
        b="wheel_fl_rim",
        kind="bolted",
        why="3x M8 through the hub's outboard flange, as the rears",
    ),
    Joint(
        a="hub_fr",
        b="wheel_fr_rim",
        kind="bolted",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="axle_rear",
        b="chassis_bearing_hanger_?",
        kind="pierced",
        why="the axle runs through a bearing in each of the three hangers. The "
        "hanger is the frame's part and the axle is the driveline's, which is "
        "the one place those two modules meet",
    ),
    Joint(
        a="axle_bearing_?",
        b="axle_rear",
        kind="pierced",
        why="a 50 mm bore, forced by the axle. Three of them, because a KZ carries "
        "a center bearing as well as the outer pair and `frame.py` builds three "
        "hanger plates for it",
    ),
    Joint(
        a="axle_bearing_l",
        b="axle_cassette_l",
        kind="pressed",
        why="the bearing's 80 mm outside diameter is pressed into the cassette's "
        "bore. Per station rather than as a glob: the left bearing is 300 mm from "
        "the center cassette",
    ),
    Joint(a="axle_bearing_c", b="axle_cassette_c", kind="pressed", why="the center pair"),
    Joint(a="axle_bearing_r", b="axle_cassette_r", kind="pressed", why="the right pair"),
    Joint(
        a="axle_bearing_l",
        b="chassis_bearing_hanger_l",
        kind="pierced",
        why="the hanger plate is 12 mm thick and bored for the axle, and a 16 mm "
        "bearing in a 40 mm cassette straddles that bore -- so the bearing is inside "
        "the plate as well as inside its cassette. Per station, as above",
    ),
    Joint(
        a="axle_bearing_c",
        b="chassis_bearing_hanger_c",
        kind="pierced",
        why="the center station",
    ),
    Joint(
        a="axle_bearing_r",
        b="chassis_bearing_hanger_r",
        kind="pierced",
        why="the right station",
    ),
    Joint(
        a="axle_cassette_l",
        b="chassis_bearing_hanger_l",
        kind="bolted",
        why="4x M8 into the plate. The cassette is what Art. 9.1.2 calls a rear "
        "axle bracket's bearing carrier, and its **outboard face at x -320 is what "
        "fixes the rear disc's inboard limit** -- spec §20.6.5's *\"the constraint is "
        "the bearing, not the wheel\"*, recomputed after wave 1 moved the plate from "
        "-185 to -300",
    ),
    Joint(
        a="axle_cassette_c",
        b="chassis_bearing_hanger_c",
        kind="bolted",
        why="the center station",
    ),
    Joint(
        a="axle_cassette_r",
        b="chassis_bearing_hanger_r",
        kind="bolted",
        why="the right station",
    ),
    Joint(
        a="axle_bearing_l",
        b="chassis_seat_strut_rear_l",
        kind="bolted",
        why="Art. 9.1.2 starts the extra seat stays *\"between the rear axle "
        "brackets and the seat\"*, so the stay's root and the bearing's carrier are "
        "on the same plate at the same node -- measured, the stay's root at "
        "(-300, -485, +167) is 44.7 mm from the axle centerline against a Ø80 "
        "bearing. Not a joint anybody would draw, and it is the one the gate finds",
    ),
    Joint(
        a="axle_bearing_r",
        b="chassis_seat_strut_rear_r",
        kind="bolted",
        why="the right-hand pair of the same node",
    ),
    Joint(
        a="axle_cassette_l",
        b="chassis_seat_strut_rear_l",
        kind="bolted",
        why="the cassette half of the same node",
    ),
    Joint(
        a="axle_cassette_r",
        b="chassis_seat_strut_rear_r",
        kind="bolted",
        why="the right-hand pair",
    ),
    Joint(
        a="axle_rear",
        b="axle_sprocket",
        kind="clamped",
        why="the rear sprocket's carrier clamps around the axle tube, so its "
        "bore is inside the axle's surface",
    ),
    # --- wheels.py: the front uprights --------------------------------------
    Joint(
        a="chassis_kingpin_boss_l",
        b="kingpin_fl",
        kind="pierced",
        why="Art. 4.2.2: *\"Articulated connections are only allowed for the "
        "steering knuckle (through the king pin) and the steering.\"* This is that "
        "one place, and the frame had nothing here before #190. The pin is tilted "
        "18 degrees of caster and 11 of inclination about z 100 rather than about "
        "the spindle, because the boss is a **vertical** Ø40 cylinder: pivoted at "
        "the spindle the axis is 28.4 mm off the bore at the boss's mid-height and "
        "the pin misses the frame by 3.4 mm",
    ),
    Joint(
        a="chassis_kingpin_boss_r",
        b="kingpin_fr",
        kind="pierced",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_kingpin_boss_l",
        b="kingpin_pill_fl_lower",
        kind="seated",
        why="the lower eccentric pill sits in the yoke's bore. Only the lower one: "
        "the boss is 60 mm tall and the upper pill is at its top, where the tilted "
        "axis has already walked the pill's outside past the bore's lip -- which is "
        "a fact about a vertical boss carrying an inclined pin and belongs in a "
        "§Chassis ticket rather than in a fudge here",
    ),
    Joint(
        a="chassis_kingpin_boss_r",
        b="kingpin_pill_fr_lower",
        kind="seated",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="kingpin_fl",
        b="kingpin_pill_fl_lower",
        kind="pierced",
        why="the pin passes through both pills' offset bores. That offset is the "
        "whole mechanism: the CRG Caster/Camber Chart's III/III is maximum caster, "
        "I/I minimum and II/II the factory neutral setting, and it publishes "
        "positions and no degrees",
    ),
    Joint(
        a="kingpin_fr",
        b="kingpin_pill_fr_lower",
        kind="pierced",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="kingpin_fl",
        b="kingpin_pill_fl_upper",
        kind="pierced",
        why="the upper pill of the same pair",
    ),
    Joint(
        a="kingpin_fr",
        b="kingpin_pill_fr_upper",
        kind="pierced",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="kingpin_fl",
        b="knuckle_fl",
        kind="pierced",
        why="the knuckle swings on the pin between the yoke's two lugs. Art. 4.2.1 "
        "makes both of them chassis main parts and this kart had neither",
    ),
    Joint(
        a="kingpin_fr",
        b="knuckle_fr",
        kind="pierced",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="knuckle_arm_fl",
        b="knuckle_fl",
        kind="welded",
        why="one casting; separate meshes so the arm can carry its own section. Art. "
        "4.5.3 wants it *\"made of aluminium or steel and securely attached with "
        "self-locking nuts and bolts\"*",
    ),
    Joint(
        a="knuckle_arm_fr",
        b="knuckle_fr",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="kingpin_fl",
        b="knuckle_arm_fl",
        kind="pierced",
        why="the arm's root straddles the pin, which is what makes it an arm about "
        "the kingpin axis rather than a bracket beside it",
    ),
    Joint(
        a="kingpin_fr",
        b="knuckle_arm_fr",
        kind="pierced",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="knuckle_arm_fl",
        b="tierod_end_l_outer",
        kind="bolted",
        why="Art. 4.5.3 permits *\"rose joints on each end of the arm\"* by name. The "
        "outer eye shares the kingpin's lateral station, because the arm points "
        "straight rearward -- measured on both sides of the CRG plan view to within "
        "1 px, and it is what makes the sourced 270 mm tie rod fit",
    ),
    Joint(
        a="knuckle_arm_fr",
        b="tierod_end_r_outer",
        kind="bolted",
        why="the right-hand pair",
    ),
    Joint(
        a="knuckle_arm_fl",
        b="tierod_l",
        kind="bolted",
        why="the rod's threaded end reaches into the same eye",
    ),
    Joint(a="knuckle_arm_fr", b="tierod_r", kind="bolted", why="the right-hand pair"),
    Joint(
        a="tierod_l",
        b="tierod_end_l_*",
        kind="threaded",
        why="both rose joints screw into the rod's own ends, which is what makes the "
        "270 mm eye-to-eye adjustable at all. **The measured length is 271 mm** "
        "against a sourced OTK *\"STEERING TIE-ROD 270 mm\"* -- a part length and a "
        "geometry agreeing to 0.3%, and the third leg of spec §20.3.1's kingpin "
        "derivation",
    ),
    Joint(a="tierod_r", b="tierod_end_r_*", kind="threaded", why="the right-hand rod"),
    # --- wheels.py: the brake system ----------------------------------------
    #
    # Absent in its entirety before #190: no disc, no caliper, no master cylinder,
    # no line. Art. 8.6 makes brakes **free** in Group 1, so four wheels is an
    # `estimated` design choice and not a requirement (ADR-0054); what Art. 4.12 does
    # make mandatory is the doubled pedal link and, on this kart, the disc pad.
    Joint(
        a="axle_rear",
        b="brake_disc_rear_hub",
        kind="clamped",
        why="OTK's *\"MG DISK'S HUB D.50mm FOR BRAKE\"* clamps the 50 mm axle on the "
        "kart's **left**, opposite the sprocket at +115. Art. 4.3's four-keyway "
        "clause is the formal reason it has to be opposite: four stations on one "
        "shaft, of which the disc and the sprocket are two, so they cannot be "
        "coplanar. Measured separation 515 mm, on opposite sides of the center "
        "bearing",
    ),
    Joint(
        a="brake_disc_rear_carrier",
        b="brake_disc_rear_hub",
        kind="clamped",
        why="the lobed star carrier's bore clamps the axle-mounted hub. Two pieces "
        "rather than one because the friction ring floats",
    ),
    Joint(
        a="brake_disc_rear_bobbin_*",
        b="brake_disc_rear_carrier",
        kind="pierced",
        why="**6 bobbins on a bolt circle**, `sourced` as shape off `007-B4-69` "
        "p. 2's exploded CAD at 170 dpi. One position carries the Art. 4.12.3 "
        "homologation-number boss",
    ),
    Joint(
        a="brake_disc_rear",
        b="brake_disc_rear_bobbin_*",
        kind="pierced",
        why="the friction ring floats on them, which is the point of the "
        "construction -- it expands and the carrier does not",
    ),
    Joint(
        a="brake_disc_rear",
        b="brake_pad_rear_?",
        kind="seated",
        why="2 pads at 58 mm overall against 2 pistons at 32 mm bore, `sourced` off "
        "`82/FR/11`. Clamp area 1608 mm2 -- and note that Birel's 4-piston rear at "
        "25 mm gives 1963 while its front gives 982, so the two makers put the "
        "front-to-rear clamp ratio on opposite sides of 1.0",
    ),
    Joint(
        a="brake_caliper_rear",
        b="brake_pad_rear_?",
        kind="seated",
        why="each pad sits in its own half of the opposed body. The body is built as "
        "a **C** rather than a solid block for this reason: solid, at the drawing's "
        "74 mm thickness, it enclosed 9 mm of the disc's carrier",
    ),
    Joint(
        a="brake_caliper_rear",
        b="brake_caliper_rear_bracket",
        kind="bolted",
        why="2 lugs at the ends of the long axis plus a large central through-boss, "
        "`sourced` off the drawing",
    ),
    Joint(
        a="axle_cassette_l",
        b="brake_caliper_rear_bracket",
        kind="bolted",
        why="Art. 4.12.5 confirms this end from the other side by attaching rain "
        "covers *\"to the stub axle\"* -- the caliper hangs off the chassis's side of "
        "the joint, not the wheel's. The bracket is an L: a radial plate on the "
        "cassette's outboard face, then an axial arm out to the caliper at a radius "
        "**above 70 mm**, which is what clears the disc's carrier entirely",
    ),
    Joint(
        a="brake_disc_protector",
        b="chassis_rail_l",
        kind="bolted",
        why="**Art. 4.12.4 makes this part mandatory and it did not exist.** "
        "`derived`: `rail_z` 50 and `tube_main` 30 put the rails at z 35..65, and a "
        "Ø195 disc concentric with the axle at z 147.5 has its bottom edge at "
        "exactly **50.0** -- dead level with the rails' centerline. *\"Level with\"* is "
        "satisfied, so the pad is not marginal. Its underside is at 35, level with "
        "the rails' lowest point, so it grounds before the disc does",
    ),
    Joint(
        a="brake_disc_fl",
        b="hub_fl",
        kind="bolted",
        why="**3 integral drive tangs at 120 degrees** on the disc's inner bore, into "
        "the hub's Ø48 inboard flange. `sourced` as shape off `007-B4-69` p. 2: the "
        "front disc is one piece with no floating carrier, unlike the rear",
    ),
    Joint(
        a="brake_disc_fr",
        b="hub_fr",
        kind="bolted",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="brake_disc_fl",
        b="brake_pad_fl_?",
        kind="seated",
        why="**4 pistons at 26 mm bore and 2 pads at 38 mm** per wheel, `sourced` off "
        "`82/FR/11` and `007-BRKF-01`. Clamp area 2124 mm2. Per corner rather than "
        "as a glob, because the left pads do not reach the right disc",
    ),
    Joint(a="brake_disc_fr", b="brake_pad_fr_?", kind="seated", why="the right corner"),
    Joint(
        a="brake_caliper_fl",
        b="brake_pad_fl_?",
        kind="seated",
        why="a pad in each half of the opposed body",
    ),
    Joint(a="brake_caliper_fr", b="brake_pad_fr_?", kind="seated", why="the right corner"),
    Joint(
        a="brake_caliper_fl",
        b="brake_caliper_fl_bracket",
        kind="bolted",
        why="4 bolt holes in a flange, 2 top 2 bottom, `sourced` off the drawing. "
        "**And this is the pair spec §20.6.6 says to watch**: the caliper's outboard "
        "face is 7.0 mm from the tire's inner face at x 485, which is the binding "
        "clearance in the whole front assembly",
    ),
    Joint(
        a="brake_caliper_fr",
        b="brake_caliper_fr_bracket",
        kind="bolted",
        why="the right corner",
    ),
    Joint(
        a="brake_caliper_fl_bracket",
        b="knuckle_fl",
        kind="bolted",
        why="the bracket's other end. It reaches from the knuckle's outboard face at "
        "345 out to 424 -- stopping 21 mm short of the disc's plane, because at the "
        "plane itself its outer end is inside the friction ring's own radial band",
    ),
    Joint(
        a="brake_caliper_fr_bracket",
        b="knuckle_fr",
        kind="bolted",
        why="the right corner",
    ),
    Joint(
        a="brake_master_bracket",
        b="chassis_tray_edge_l",
        kind="bolted",
        why="**not `chassis_cross_front`, which spec §20.6.4 asks for and which "
        "cannot carry it.** `frame.TRAY_HALF_WIDTH` is 131 mm at y +490, so the "
        "floor pan's own edge is at x -131 and the loop's leg at this x is at y +706 "
        "-- 216 mm forward of where Art. 4.4's ordering puts the cylinders. Art. "
        "4.6's mandatory edging tube runs along the pan's edge with its top at z 81 "
        "and is the only structure at the right station and height",
    ),
    Joint(
        a="brake_master_rear",
        b="brake_master_bracket",
        kind="bolted",
        why="the right-hand pair of the same joint -- written per cylinder because "
        "the glob `brake_master_*` also matches `brake_master_bracket` itself",
    ),
    Joint(
        a="brake_master_front",
        b="brake_master_bracket",
        kind="bolted",
        why="both bodies bolt down onto the plate. **The front cylinder is the "
        "inboard one**, which is not arbitrary: the front circuit's hose arrives at "
        "the distributor on the bracket's outboard upstand and the rear circuit's "
        "leaves rearward and inboard, and this is the ordering where neither hose has "
        "to cross the other cylinder",
    ),
    Joint(
        a="brake_distributor",
        b="brake_master_bracket",
        kind="bolted",
        why="on the bracket's upstand. **Birel-only** -- `007-B4-69` item "
        "`10.10659.00`, and the CRG form does not list one -- so this is a Birel "
        "feature grafted onto a CRG layout and is the weakest-sourced part in the "
        "section. Recorded rather than quietly dropped",
    ),
    Joint(
        a="brake_balance_regulator",
        b="chassis_rail_l",
        kind="bolted",
        why="on the left rail's straight run, where a seated driver can reach it. "
        "Both homologation forms list a balance regulator and neither places one. "
        "Inboard of the rail's centerline rather than on top of it, because Art. "
        "9.4.2's side-bumper sockets own the rail's outboard side at y -100",
    ),
    Joint(
        a="brake_pushrod",
        b="pedal_brake",
        kind="pierced",
        why="the rod's clevis eye is on the pedal's plate. Aimed at the plate's own "
        "plane -- `pedal_y + tan(PEDAL_FACE_TILT) x (z - pedal_z)` -- rather than at "
        "the pad's center, which is 11 mm off it and would have left Art. 4.12.2's "
        "mandatory link attached to nothing",
    ),
    Joint(
        a="brake_pushrod_link",
        b="pedal_brake",
        kind="pierced",
        why="**Art. 4.12.2, and this part exists because a rule says so:** *\"the "
        "link between the pedal and the pump(s) must be doubled for safety\"*. A "
        "mechanical redundancy rule, not a two-circuit rule. 2.0 mm of cable, one "
        "step over the article's 1.8 mm floor -- and 1.8 is a floor and not a "
        "practice, which is why this is not written as \"1.8 max\". The CRG chassis "
        "form devotes a whole page to photographing it",
    ),
    Joint(
        a="brake_master_front",
        b="brake_pushrod",
        kind="pierced",
        why="one rod through **both** cylinders, which is what a balance bar is and "
        "what makes a single Art. 4.4-compliant link serve a two-pump layout. "
        "Measured 126 mm against spec §20.6.4's 135 estimate",
    ),
    Joint(
        a="brake_master_rear",
        b="brake_pushrod",
        kind="pierced",
        why="the rear cylinder of the same balance bar",
    ),
    Joint(
        a="brake_master_front",
        b="brake_pushrod_link",
        kind="pierced",
        why="the cable runs alongside it into the same two bodies",
    ),
    Joint(
        a="brake_master_rear",
        b="brake_pushrod_link",
        kind="pierced",
        why="the rear cylinder of the same balance bar",
    ),
    Joint(
        a="brake_pushrod",
        b="pedal_mount_l",
        kind="pierced",
        why="the rod passes through a slot in the left pedal bracket, which is what "
        "a brake pushrod on a hanging pedal does. Declared against the left mount "
        "alone: the right one is 250 mm away and a glob would demand the rod reach "
        "it. There is no clear station -- the bracket spans z 85..168 at x 120..130 "
        "and the cylinders' axis is at 116, so a rod that missed the bracket would "
        "have to miss the pumps too",
    ),
    Joint(
        a="brake_pushrod",
        b="brake_pushrod_link",
        kind="clamped",
        why="the two halves of one doubled control, banded together. 6 mm apart in z, "
        "which is inside both radii",
    ),
    Joint(
        a="brake_line_front",
        b="brake_caliper_f?",
        kind="routed",
        why="a **tee'd assembly** feeding both calipers from one pump: `007-B4-69` "
        "item 9, *\"BRAKE FRONT TUBE ASSY.\"*, drawn as a tee with two equal "
        "branches. Both banjos land on a caliper **half** at x +-470 rather than on "
        "the disc's plane at 445, which with a C-shaped body is the gap between the "
        "halves",
    ),
    Joint(
        a="brake_line_front",
        b="brake_master_front",
        kind="routed",
        why="the pump end of the front circuit",
    ),
    Joint(
        a="brake_line_front",
        b="brake_distributor",
        kind="routed",
        why="through the distributor on the way",
    ),
    Joint(
        a="brake_line_front",
        b="brake_master_bracket",
        kind="routed",
        why="cable-tied to the bracket's upstand where it turns down into the "
        "distributor",
    ),
    Joint(
        a="brake_line_front",
        b="chassis_steering_hoop",
        kind="routed",
        why="cable-tied along the **upper** surface of the chassis tubes, which is "
        "the route `col_crg_form_planview_1417.jpg` and "
        "`crg_roadrebel_kz_detail7.webp` both show. This is the tie that keeps the "
        "front pair off the floor tray -- Art. 4.12.6's rule of thumb, applied to the "
        "hoses as well as to the cooling tube it names",
    ),
    Joint(
        a="brake_line_rear",
        b="brake_master_rear",
        kind="routed",
        why="a **single run**, unlike the front's tee",
    ),
    Joint(
        a="brake_line_rear",
        b="brake_balance_regulator",
        kind="routed",
        why="inline, which is why the regulator sits on the route rather than beside "
        "it",
    ),
    Joint(
        a="brake_line_rear",
        b="brake_caliper_rear",
        kind="routed",
        why="the banjo end, at a radius inside the caliper's own 55..110 mm band -- "
        "an end at the bracket's height is 47 mm out and touches nothing",
    ),
    Joint(
        a="brake_line_rear",
        b="chassis_tray_edge_l",
        kind="routed",
        why="cable-tied along the edging tube for most of its length. The run is "
        "**inboard** of the rail's centerline throughout, and that is the fix for "
        "four separate collisions: Art. 9.4.2 fixes the side-bumper sockets' 500 mm "
        "pitch and all four of their sleeves project outboard from that rail, so its "
        "outboard side is not available to a hose",
    ),
    # --- powertrain.py: driveline -------------------------------------------
    Joint(
        a="axle_sprocket",
        b="drive_chain",
        kind="meshed",
        why="the chain wraps the rear sprocket's teeth; the roller band is "
        "inside the tooth profile by design",
    ),
    # Art. **5.9**, PDF p. 17: *"A chain guard is mandatory in all classes […] In
    # gearbox classes, the chain guard must cover the sprocket and the crown wheel
    # down to the centre of the crown wheel axis."* The kart did not have one, which
    # is a compliance failure and not a detail.
    Joint(
        a="drive_chain_guard",
        b="drive_chain_guard_flange",
        kind="welded",
        why="the inboard mounting flange off the guard's own wall",
    ),
    Joint(
        a="drive_chain_guard_flange",
        b="chassis_cross_rear",
        kind="bolted",
        why="Art. 5.9's compulsory guard needs an anchor, and Art. 4.2.3 already "
        "contemplates a welded attachment point on this member",
    ),
    Joint(
        a="drive_chain_guard",
        b="drive_output_shaft",
        kind="pierced",
        why="the same, at the other end",
    ),
    Joint(
        a="drive_chain",
        b="drive_output_sprocket",
        kind="meshed",
        why="the other end of the same chain run",
    ),
    Joint(
        a="drive_output_shaft",
        b="drive_output_sprocket",
        kind="pressed",
        why="the sprocket is on the gearbox output shaft",
    ),
    Joint(
        a="drive_output_shaft",
        b="drive_sprocket_carrier",
        kind="pressed",
        why="the carrier is bored onto the same shaft, inboard of the sprocket",
    ),
    Joint(
        a="drive_sprocket_carrier",
        b="engine_clutch_cover",
        kind="pierced",
        why="the output shaft and its carrier come out through the clutch "
        "cover; a gearbox output that did not pass through its own cover "
        "would be inside a sealed casing",
    ),
    Joint(
        a="drive_sprocket_carrier",
        b="engine_clutch_bell",
        kind="pierced",
        why="the carrier reaches back inside the bell housing to the gearbox",
    ),
    Joint(
        a="drive_sprocket_carrier",
        b="engine_crankcase_*",
        kind="pierced",
        why="the output boss emerges from the crankcase's own bore, which is "
        "split across the upper and lower halves",
    ),
    # --- powertrain.py: engine castings -------------------------------------
    Joint(
        a="engine_crankcase_lower",
        b="engine_crankcase_upper",
        kind="bolted",
        why="a split crankcase, bolted on its parting line",
    ),
    Joint(
        a="engine_crankcase_lower",
        b="engine_mount_plate",
        kind="bolted",
        why="the engine bolts down onto the mount's table -- MOUNT_PLATE_TOP is "
        "documented as 'the underside of the crankcase' for this reason",
    ),
    Joint(
        a="engine_mount_plate",
        b="engine_mount_clamp_*",
        kind="bolted",
        why="the two outboard clamp plates hang off the table",
    ),
    Joint(
        a="engine_mount_clamp_*",
        b="chassis_rail_r",
        kind="clamped",
        why="this is what an engine mount **is**: plates clamped either side of "
        "the right main rail with the engine bolted on top. It is also the "
        "only thing attaching the entire powertrain to the chassis, so if "
        "this pair is not in contact the engine is resting on nothing",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_cylinder_base",
        kind="bolted",
        why="the barrel's base flange lands on the crankcase deck",
    ),
    Joint(
        a="engine_cylinder",
        b="engine_cylinder_base",
        kind="welded",
        why="one casting: the barrel and its base flange are separate meshes so "
        "the flange can carry its own bevel",
    ),
    Joint(
        a="engine_cylinder",
        b="engine_head",
        kind="bolted",
        why="the head lands on the barrel's top deck",
    ),
    Joint(
        a="engine_cylinder_base",
        b="engine_cylinder_base_nut_?",
        kind="threaded",
        why="four base nuts, threaded onto the case studs",
    ),
    Joint(
        a="engine_head",
        b="engine_head_nut_?",
        kind="threaded",
        why="six head nuts on the same studs, one row further up",
    ),
    Joint(
        a="engine_head",
        b="engine_plug_boss",
        kind="welded",
        why="the plug boss is cast into the head",
    ),
    Joint(
        a="engine_head",
        b="engine_water_outlet",
        kind="bolted",
        why="the outlet elbow bolts to the head's water jacket",
    ),
    Joint(
        a="engine_plug_boss",
        b="engine_plug_hex",
        kind="threaded",
        why="the plug screws into the boss",
    ),
    Joint(
        a="engine_plug_hex",
        b="engine_plug_insulator",
        kind="pressed",
        why="the ceramic is pressed into the plug's steel shell",
    ),
    Joint(
        a="engine_plug_cap",
        b="engine_plug_insulator",
        kind="seated",
        why="the cap pushes down over the insulator onto the terminal",
    ),
    Joint(
        a="engine_plug_cap",
        b="engine_plug_lead",
        kind="routed",
        why="the lead enters the cap",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_ignition_cover",
        kind="bolted",
        why="the cover closes the ignition side of the case",
    ),
    Joint(
        a="engine_ignition_bolt_?",
        b="engine_ignition_cover",
        kind="threaded",
        why="five cover bolts, heads sunk into the cover's counterbores",
    ),
    Joint(
        a="engine_clutch_bolt_?",
        b="engine_clutch_cover",
        kind="threaded",
        why="six cover bolts on the clutch side, the same way",
    ),
    Joint(
        a="engine_clutch_bell",
        b="engine_clutch_cover",
        kind="bolted",
        why="the cover's lip closes over the bell housing's rim",
    ),
    Joint(
        a="engine_clutch_bell",
        b="engine_crankcase_upper",
        kind="welded",
        why="the bell housing is part of the case casting",
    ),
    # The three `engine_water_pump` rows that were here are **deleted, not
    # renamed**: Art. 5.3.2 permits the pump to be driven by the engine *or* by the
    # rear wheel axle, the KZ trade sells it as "KZ water pump with HTD axle pulley
    # and tooth belt", and §30.7 puts it on the axle as `cooling_pump_body`. A joint
    # to a casting the part no longer touches is not an outstanding defect, it is a
    # statement about a kart that does not exist.
    Joint(
        a="engine_crankcase_upper",
        b="engine_water_inlet",
        kind="welded",
        why="Art. 9.10.1 water-cools *\"the crankcase, cylinder and head\"* -- all "
        "three -- so the coolant has to get into the case somewhere. This is the "
        "cast boss it enters through, and there was no such part: the lower hose "
        "used to end on the clutch cover",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_starter",
        kind="bolted",
        why="the starter motor bolts to the case",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_battery",
        kind="clamped",
        why="the starter's battery is strapped to a bracket on the case behind "
        "the engine",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_reed_block",
        kind="bolted",
        why="the reed block bolts to the case's inlet face",
    ),
    Joint(
        a="engine_reed_block",
        b="engine_reed_face",
        kind="seated",
        why="the reed petals' sealing face sits in the block",
    ),
    Joint(
        a="engine_carb",
        b="engine_reed_block",
        kind="bolted",
        why="the carburetor bolts to the reed block, which is what a case-reed "
        "two-stroke's inlet tract is",
    ),
    Joint(
        a="engine_carb",
        b="engine_reed_face",
        kind="seated",
        why="the carb's spigot lands against the same face",
    ),
    Joint(a="engine_carb", b="engine_carb_bowl", kind="bolted", why="float bowl on the body"),
    Joint(a="engine_carb", b="engine_carb_cap", kind="bolted", why="top cap on the body"),
    Joint(
        a="engine_carb_cap",
        b="engine_throttle_cable",
        kind="routed",
        why="the throttle cable enters the cap and pulls the slide",
    ),
    Joint(
        a="engine_carb",
        b="engine_intake_boot",
        kind="routed",
        why="the rubber boot clamps over the carburetor's mouth",
    ),
    Joint(
        a="engine_carb_cap",
        b="engine_intake_boot",
        kind="routed",
        why="the boot's clamp band reaches up past the cap",
    ),
    Joint(
        a="engine_airbox",
        b="engine_intake_boot",
        kind="routed",
        why="the other end of the boot enters the airbox",
    ),
    Joint(
        a="engine_airbox",
        b="engine_airbox_duct_?",
        kind="welded",
        why="Art. **9.13.1**, PDF p. 30: *\"They must have two ducts with a 30.0 mm "
        "maximum diameter.\"* Two, and Ø30.0 -- a maximum every KZ silencer runs. "
        "Both were missing",
    ),
    Joint(
        a="engine_airbox",
        b="engine_airbox_lid",
        kind="bolted",
        why="the lid closes the box",
    ),
    # --- powertrain.py: exhaust ---------------------------------------------
    Joint(
        a="engine_cylinder",
        b="exhaust_manifold",
        kind="bolted",
        why="4x M6 on a 62 x 44 pattern into the barrel's exhaust port face. "
        "**Renamed from `exhaust_flange`, and the rename is the fact**: there is no "
        "flange on the pipe, a short manifold bolts to the cylinder and the pipe "
        "slips over its spigot on springs",
    ),
    Joint(
        a="engine_cylinder_base",
        b="exhaust_manifold",
        kind="bolted",
        why="the plate is tall enough to reach the base casting below the port",
    ),
    Joint(
        a="exhaust_manifold",
        b="exhaust_manifold_spigot",
        kind="welded",
        why="the spigot the pipe slips over is part of the manifold casting; two "
        "meshes because a plate and a tube are two primitives",
    ),
    Joint(
        a="exhaust_chamber",
        b="exhaust_manifold_spigot",
        kind="seated",
        why="**the slip joint**, and it is what lets the chamber articulate: ~3 "
        "degrees of cone and ~5 mm of axial play on 70 mm springs. A pipe rigid to "
        "the engine cracks at the header",
    ),

    Joint(
        a="exhaust_chamber",
        b="exhaust_spring_?",
        kind="clamped",
        why="two springs pull the chamber onto the flange. The hooks wrap the "
        "pipe's lugs, so hook and lug share volume",
    ),
    Joint(
        a="exhaust_manifold",
        b="exhaust_spring_?",
        kind="clamped",
        why="the other hook of each spring, over the manifold's bolt heads",
    ),
    Joint(
        a="exhaust_manifold",
        b="exhaust_manifold_bolt_?",
        kind="threaded",
        why="**four** M6 x 20, `sourced` off kartshop's own listing for the TM KZ "
        "manifold. The build had two, and `exhaust_flange_nut_0..1` is renamed with "
        "the count corrected",
    ),
    Joint(
        a="exhaust_manifold_bolt_0",
        b="exhaust_spring_0",
        kind="clamped",
        why="the spring hooks over the bolt under its head, so it wraps the hex. "
        "Written per side because spring 0 does not reach bolt 1",
    ),
    Joint(
        a="exhaust_manifold_bolt_1",
        b="exhaust_spring_1",
        kind="clamped",
        why="as spring 0",
    ),
    # -- the hanger: one part bolted to nothing becomes four bolted to a 30 mm tube
    #
    # `exhaust_hanger` measured **60.47 mm** off `chassis_side_bar_r` and could not
    # mount there at any distance: that bar is swept at `tube_bumper` = 20 mm and the
    # sourced mushroom clamp family is bored 28/30/32. The 30 mm tubes on this kart
    # are the two rails and the two cross members, and only `chassis_cross_rear` is
    # within reach of a rearward pipe.
    Joint(
        a="exhaust_hanger_clamp",
        b="chassis_cross_rear",
        kind="clamped",
        why="Ø30.0 bore on the rear cross member at x +54, directly under the grip "
        "point, so contact is 0.0 by construction. Art. 4.2.5 lists *\"exhaust and "
        "exhaust silencer holder\"* as a chassis component and 4.2.3 puts its welded "
        "attachment point on the frame",
    ),
    Joint(
        a="exhaust_hanger_clamp",
        b="exhaust_hanger_boss",
        kind="welded",
        why="the Ø20 mushroom boss standing proud of the clamp body",
    ),
    Joint(
        a="exhaust_hanger_boss",
        b="exhaust_hanger_arm",
        kind="bolted",
        why="through the arm's two 30 mm slots, which is what makes the height "
        "adjustable",
    ),
    Joint(
        a="exhaust_hanger_arm",
        b="exhaust_hanger_cradle",
        kind="bolted",
        why="the cradle spring's tails bolt to the arm's upper end",
    ),
    Joint(
        a="exhaust_hanger_cradle",
        b="exhaust_chamber",
        kind="clamped",
        why="a `sourced` Ø12 x 130 cradle spring round the **baffle cone** at "
        "s = 513. That cone is the only part of the pipe both stiff enough to clamp "
        "and reachable; real installations clip the cone, not the belly",
    ),
    # -- the U-bend, the can and its own mount
    Joint(
        a="exhaust_chamber",
        b="exhaust_connector",
        kind="seated",
        why="the U-bend slips over the stinger. `sourced` as a part -- the "
        "catalogues sell a *\"muffler bent pipe\"* separately -- which is what turns "
        "a leftward stinger back into a rightward can",
    ),
    Joint(
        a="exhaust_connector",
        b="exhaust_silencer",
        kind="seated",
        why="and into the silencer's Ø29 inlet spigot",
    ),
    Joint(
        a="exhaust_silencer_bracket_clamp",
        b="chassis_cross_rear",
        kind="clamped",
        why="Ø30 bore at x +150, 96 mm outboard of the pipe support's clamp so the "
        "two do not collide on the same tube",
    ),
    Joint(
        a="exhaust_silencer_bracket_clamp",
        b="exhaust_silencer_bracket",
        kind="welded",
        why="the bracket plate off the clamp body",
    ),
    Joint(
        a="exhaust_silencer_bracket",
        b="exhaust_silencer_isolator",
        kind="bolted",
        why="M8 through a rubber bush, which is what stops the can shaking itself "
        "off a rigid mount",
    ),
    Joint(
        a="exhaust_silencer_isolator",
        b="exhaust_silencer_saddle",
        kind="bolted",
        why="the saddle on the other side of the same M8",
    ),
    Joint(
        a="exhaust_silencer_saddle",
        b="exhaust_silencer",
        kind="seated",
        why="the can sits in the saddle's cradle",
    ),
    Joint(
        a="exhaust_silencer_band_?",
        b="exhaust_silencer",
        kind="clamped",
        why="two jubilee clips, `sourced` as a pair: *\"large jubilee clip for "
        "exhaust silencer x2, 120-140mm\"*, which is also what corroborates the "
        "120 mm body",
    ),
    Joint(
        a="exhaust_silencer_band_?",
        b="exhaust_silencer_saddle",
        kind="pierced",
        why="each band threads through the saddle's slot",
    ),
    # --- powertrain.py: cooling ---------------------------------------------
    Joint(
        a="radiator_core",
        b="radiator_fin_*",
        kind="welded",
        why="the fin pack is brazed into the core; every fin stands 1.5 mm "
        "proud of the core's face (RADIATOR_FIN_PROUD) and is inside it for "
        "the rest of its depth",
    ),
    Joint(
        a="radiator_core",
        b="radiator_tank_*",
        kind="welded",
        why="upper and lower tanks are welded to the core's ends",
    ),
    Joint(
        a="radiator_core",
        b="radiator_end_*",
        kind="welded",
        why="the two end channels close the core's sides",
    ),
    Joint(
        a="radiator_core",
        b="radiator_divider",
        kind="welded",
        why="the dual-pass divider crosses the core face",
    ),
    Joint(
        a="radiator_divider",
        b="radiator_tank_*",
        kind="welded",
        why="the divider runs from tank to tank",
    ),
    Joint(
        a="radiator_divider",
        b="radiator_fin_4",
        kind="welded",
        why="the divider sits at RADIATOR_DIVIDER_ALONG = -0.44 of the core's "
        "width, which lands between fins 4 and 5. Named explicitly rather "
        "than as radiator_fin_*, because the divider crosses two fins and a "
        "glob would demand it touch all nineteen",
    ),
    Joint(a="radiator_divider", b="radiator_fin_5", kind="welded", why="the other of that pair"),
    Joint(
        a="radiator_fin_*",
        b="radiator_tank_high",
        kind="welded",
        why="every fin runs the full height of the core and lands on both tanks",
    ),
    Joint(a="radiator_fin_*", b="radiator_tank_low", kind="welded", why="the lower end of the same"),
    Joint(
        a="radiator_end_*",
        b="radiator_tank_*",
        kind="welded",
        why="the end channels close onto both tanks -- four corners of the same "
        "brazed frame",
    ),
    Joint(
        a="radiator_cap",
        b="radiator_tank_high",
        kind="threaded",
        why="the filler neck and cap screw into the high tank",
    ),
    Joint(
        a="radiator_hose_upper",
        b="radiator_tank_high",
        kind="routed",
        why="the top hose enters the high tank's outlet",
    ),
    Joint(
        a="radiator_hose_lower",
        b="radiator_tank_low",
        kind="routed",
        why="the bottom hose enters the low tank's inlet",
    ),
    Joint(
        a="radiator_hose_lower",
        b="radiator_end_inboard",
        kind="routed",
        why="that inlet is in the corner, so the hose also passes the inboard "
        "end channel",
    ),
    Joint(
        a="radiator_core",
        b="radiator_hose_lower",
        kind="routed",
        why="and the tube pack itself, for the same reason: the low tank's inlet is in "
        "the core's own footprint, so a hose reaching it is inside the core. Was a #190 "
        "overlap waiver reading *\"the outboard half of the same 33 mm problem\"*; with "
        "`HOSE_LOWER_ROUTE`'s waypoint order fixed the 20 remaining pairs are all at the "
        "inlet, which is a joint and not a collision",
    ),
    Joint(
        a="engine_water_outlet",
        b="radiator_hose_upper",
        kind="routed",
        why="the other end of the top hose, on the head's outlet elbow",
    ),
    Joint(
        a="cooling_pump_body",
        b="radiator_hose_lower",
        kind="routed",
        why="the other end of the bottom hose, on the pump's inlet. Renamed from "
        "`engine_water_pump`, and it is a different place on the kart: the pump is on "
        "the rear axle now, not on the clutch cover",
    ),
    Joint(
        a="radiator_curtain",
        b="radiator_end_*",
        kind="threaded",
        why="Art. 5.3.1: the baffles *\"must be securely fixed to the radiator(s) "
        "with screws. They must be one-piece\"* -- so it screws to the two end "
        "channels and to nothing else, and it must not be *\"detachable when the kart "
        "is in motion\"*",
    ),
    Joint(
        a="radiator_bracket_lower",
        b="radiator_end_inboard",
        kind="bolted",
        why="both brackets pick up on the core's inboard end channel. "
        "`BRACKET_*_LOCAL` anchors them at **1.0** of the core's own half-width, so "
        "the rod's axis lies in the plane of that channel's outer face and the rod is "
        "8 mm engaged in a 12 mm channel: contact 0.0. It was 1.15 -- 18.75 mm past "
        "the edge against a rod radius of 8, which is the 12.3 mm gap almost exactly. "
        "Anchoring in *fractions* was the right fix for a bracket that started inside "
        "the fin pack; 1.15 was the wrong fraction",
    ),
    Joint(
        a="radiator_bracket_upper",
        b="radiator_end_inboard",
        kind="bolted",
        why="as the lower bracket",
    ),
    Joint(
        a="radiator_bracket_*_clamp",
        b="chassis_rail_l",
        kind="clamped",
        why="**the radiator's entire attachment to the kart, and it is the frame.** "
        "`BRACKET_*_SEAT` used to put it on the seat's wing and left both brackets "
        "44 and 69 mm from anything. Three things say the rail: Art. **4.2.3** puts "
        "the welded attachment points for *\"the radiator(s)\"* on the frame; "
        "Art. **4.8.2** requires seat stays bolted at each end and *removed if "
        "unused*, so a stay is a removable member and not a mounting rail; and the "
        "only image in the repo that shows the bracket shows thin vertical rods "
        "dropping to a chassis clamp. Ø30.0 bore, so contact is 0.0",
    ),
    Joint(
        a="radiator_bracket_lower_clamp",
        b="radiator_bracket_lower",
        kind="welded",
        why="the clamp body on the end of its own rod",
    ),
    Joint(
        a="radiator_bracket_upper_clamp",
        b="radiator_bracket_upper",
        kind="welded",
        why="the same, on the upper rod",
    ),
    # **There is deliberately no `radiator_*`/`seat_shell` row, and its absence is an
    # assertion.** Art. 5.3.1, PDF p. 15: *"They must not interfere with the seat."*
    # This is the one place in this project where a regulation is expressed as the
    # *absence* of a declaration -- gate 1 makes any overlap fatal precisely because
    # nothing here permits it. The row that used to be here declared
    # `radiator_bracket_*`/`seat_shell` as `bolted`, which is a regulation violation
    # written into the build table. The core's inboard edge at -240 clears the shell's
    # outboard face at -184 by 56 mm and must keep doing so.
    # --- powertrain.py: the axle-driven pump --------------------------------
    #
    # Art. **5.3.2**, PDF p. 15: *"In Groups 1 & 2, the water pump must be
    # mechanically controlled either by the engine or by the rear wheel axle."* It
    # **permits either**; the axle drive is §30.7's choice and kart practice, not a
    # requirement, and the comment says so because an earlier paraphrase of that
    # article made it read as a mandate.
    Joint(
        a="cooling_pump_body",
        b="cooling_pump_pulley",
        kind="pressed",
        why="the pulley on the pump's spindle",
    ),
    Joint(
        a="cooling_pump_body",
        b="cooling_pump_bracket",
        kind="bolted",
        why="the pump hangs off its own bracket, not off the engine",
    ),
    Joint(
        a="cooling_pump_bracket",
        b="chassis_bearing_hanger_r",
        kind="bolted",
        why="the nearest axle-adjacent structure, and Art. 4.2.3 contemplates a "
        "welded attachment point here",
    ),
    Joint(
        a="cooling_axle_pulley",
        b="axle_rear",
        kind="clamped",
        why="**clamped, not keyed**: Art. 4.3's fourth keyway is the sprocket's. A "
        "PD 65 pulley round a Ø50 axle is 6-9 mm of clamp wall and flange a side",
    ),
    Joint(
        a="cooling_belt",
        b="cooling_pump_pulley",
        kind="meshed",
        why="170XL031 -- 85 teeth, 431.8 mm, 7.9 mm wide, `sourced` as a part number. "
        "The belt is what *places* the pump: `2C + 400/C = 290.43` solves to "
        "C = 143.8 mm",
    ),
    Joint(
        a="cooling_belt",
        b="cooling_axle_pulley",
        kind="meshed",
        why="the other wrap of the same belt",
    ),
    Joint(
        a="cooling_pump_body",
        b="cooling_hose_pump_engine",
        kind="routed",
        why="the pump's outlet",
    ),
    Joint(
        a="engine_water_inlet",
        b="cooling_hose_pump_engine",
        kind="routed",
        why="and the crankcase's inlet boss at the other end. Short by design",
    ),
    # --- cockpit.py ---------------------------------------------------------
    # Art. **4.2.3** lists *"seat with four seat supports"*, so four is a number and
    # not a style choice, and Art. **4.8.1** dimensions the reinforcement plates:
    # 1.5 mm, 13 cm2 and Ø40 minimum. The kart had **two** stays and no brackets, so
    # `seat_shell`/`chassis_seat_strut_*` was one row doing the work of eight and it
    # measured 17.67 mm at the rear pair.
    #
    # **The four pads are published as empties now** (`seat_ear_*`), off the loft's
    # own samples. That is the fix rather than a better constant: the shell's flank is
    # a *sampled surface*, so a number authored in `frame.py` misses it by a few
    # millimeters and never says so -- the same failure shape as
    # `Dictionary.get(key, default)`.
    Joint(
        a="seat_shell",
        b="seat_bracket_*",
        kind="bolted",
        why="M8 through an Art. 4.8.1 reinforcement plate, and the bracket's shell "
        "end is snapped to the **nearest sampled point on the loft** rather than to "
        "an authored coordinate",
    ),
    Joint(
        a="seat_bracket_upper_r",
        b="chassis_seat_strut_rear_r",
        kind="bolted",
        why="the right of the same pair",
    ),
    Joint(
        a="seat_bracket_lower_r",
        b="chassis_seat_strut_front_r",
        kind="bolted",
        why="the right of the front pair",
    ),
    Joint(
        a="seat_bracket_upper_l",
        b="chassis_seat_strut_rear_l",
        kind="bolted",
        why="Art. 4.8.2: bolted at each end. The rear stays start on the bearing "
        "hanger, where Art. 9.1.2 puts them",
    ),
    Joint(
        a="seat_bracket_lower_l",
        b="chassis_seat_strut_front_l",
        kind="bolted",
        why="the front pair, on the rail at the central strut's station",
    ),
    Joint(
        a="steering_bearing",
        b="steering_column",
        kind="pierced",
        why="the journal turns in the bush; the bush is the part that does not turn. "
        "**Contact here is now identity**: `params.lower_bore` is the bush's bore "
        "centre *and* the column's journal centre, one expression, so the only edit "
        "that can open a gap is the edit that should move the column",
    ),
    Joint(
        a="steering_bearing",
        b="chassis_steering_hoop",
        kind="pressed",
        why="the bush is carried by the frame's lower steering bracket, which exists "
        "for nothing else. This was issue #192's headline at 37.46 mm, and the frame "
        "was the half that was right -- its bore is sourced off the column's own "
        "reference photograph and §40.2 moved the column onto it",
    ),
    Joint(
        a="steering_bearing_upper",
        b="steering_column",
        kind="pierced",
        why="**there are two column supports and this project had collapsed them into "
        "one.** Art. 4.5.2 says so itself: *\"two collars between the column "
        "brackets\"*, plural. A Ø20 nylon block, `sourced` -- \"NYLON SUPPORT STEERING "
        "COLUMN L31\" -- 366 mm up the axis from the bore",
    ),
    Joint(
        a="steering_bearing_upper",
        b="chassis_steering_support_upper",
        kind="bolted",
        why="Art. 9.5.3 makes this structural: the front panel's upper part *\"must be "
        "securely attached to the steering column support with one or more "
        "independent bars\"*",
    ),
    Joint(
        a="steering_hub",
        b="steering_column",
        kind="clamped",
        why="Art. 4.5.1: *\"securely attached to the column with at least one M6 "
        "screw (minimum grade 8.8) and a self-locking nut\"*. Replaces "
        "`steering_boss`/`steering_column` -- the boss is on the far side of the "
        "wedge and does not touch the column at all",
    ),
    Joint(
        a="steering_hub",
        b="steering_hub_wedge",
        kind="bolted",
        why="6x M6, and **the wedge is the part that stops the wheel being built "
        "wrong**: its two faces are 7 degrees apart, so the hub is square to the "
        "column and the boss is square to the wheel. Art. 4.5 permits it in as many "
        "words -- *\"A spacer may be used between the steering wheel and the hub\"* -- "
        "and OTK sells the inclined hub and the inclined spacer as catalogue items",
    ),
    Joint(
        a="steering_hub_wedge",
        b="steering_boss",
        kind="bolted",
        why="the other side of the same six bolts",
    ),
    Joint(
        a="steering_pitman",
        b="steering_column",
        kind="clamped",
        why="**this part did not exist**, which is the whole of the 46.00 mm waiver "
        "on `steering_column`/`tierod_end_?_inner`: the rod ends were declared "
        "against the column because a pitman is what they really bolt to. The 46 mm "
        "was never slack -- it is the pitman's reach, `sourced` off OTK's \"38/50\" "
        "designation with a KZ on the outer hole",
    ),
    Joint(
        a="steering_pitman",
        b="tierod_end_?_inner",
        kind="bolted",
        why="Art. 4.5.3 permits the rose joints by name. `wheels.PITMAN_EAR` is "
        "(0.050, 0.431, 0.160) and the column's own line passes through "
        "(0, 0.431, 0.1603) -- the ear is **on** the column's plane to 0.3 mm, which "
        "is what makes this plate buildable rather than fitted",
    ),
    Joint(
        a="steering_boss",
        b="steering_spokes",
        kind="welded",
        why="boss and spokes are one machined part",
    ),
    Joint(
        a="steering_rim",
        b="steering_spokes",
        kind="welded",
        why="the spokes are welded into the rim tube",
    ),
    Joint(
        a="steering_clutch_lever",
        b="steering_column",
        kind="clamped",
        why="OTK **0113.A0KIT** *\"Forged clutch lever Kit, KZ\"* is a **two-bolt clamp "
        "around a tube**, so it clamps the column and not the spoke plate -- which is "
        "what the row here used to say. On the left, because the right hand is busy "
        "with the gear lever and OTK's forged kit exists because that is where KZ "
        "drivers put it",
    ),
    Joint(
        a="pedal_cross_tube",
        b="pedal_mount_?",
        kind="pierced",
        why="the cross tube runs through both mount plates",
    ),
    Joint(
        a="pedal_mount_?",
        b="chassis_cross_front",
        kind="clamped",
        why="**this is the 104.97 mm gate-2 failure and the fix is one coordinate.** "
        "The plates used to aim at `(0, front_axle_y, rail_z + 0.025)` -- a straight "
        "cross member at the front axle line, which this chassis does not have: "
        "`chassis_cross_front` is a U-loop running y +500..+760 out at x ±110..±304, "
        "so the brackets were reaching into empty air. The loop's leg centreline "
        "passes (±259, +560, +50), so each plate's bore straddles that point and "
        "touches the tube. Art. 4.2.3 puts the pedals' welded attachment points on "
        "the frame and Art. 4.4 makes the kit the manufacturer's",
    ),
    Joint(
        a="pedal_brake",
        b="pedal_cross_tube",
        kind="pierced",
        why="the arm's bushed eye swings on the cross tube -- **at the bottom of the "
        "arm**, not above the pad: OTK 0014.DC and 0015.DCA are organ pedals",
    ),
    Joint(a="pedal_throttle", b="pedal_cross_tube", kind="pierced", why="as the brake pedal"),
    Joint(
        a="pedal_brake",
        b="pedal_brake_pad",
        kind="welded",
        why="**`welded`, was `bolted` (\"the rubber pad on the plate\")**: the part is a "
        "Ø18 x 80 transverse round bar welded across a forged arm, not a rubber pad "
        "on a 70 x 120 plate. A plate is a rental-kart pedal",
    ),
    Joint(a="pedal_throttle", b="pedal_throttle_pad", kind="welded", why="as the brake pedal"),
    Joint(
        a="pedal_brake",
        b="pedal_brake_clevis",
        kind="welded",
        why="the slotted plate carrying the pushrod clevis, 56 mm up the arm -- which "
        "*is* the brake pedal's 3.2 : 1 ratio, 180/56. Its \"adjustable\" in 0015.DCA's "
        "own name is three clevis heights in that slot",
    ),
    Joint(
        a="pedal_brake_clevis",
        b="brake_pushrod_link",
        kind="bolted",
        why="Art. 4.12.2 requires the pedal-to-pump link **doubled for safety**, with a "
        "homologated cable at 1.8 mm minimum. This is the second link, and it lands on "
        "the same slotted plate as the rod",
    ),
    Joint(
        a="pedal_brake_clevis",
        b="brake_pushrod",
        kind="bolted",
        why="Art. 4.4: *\"The brake pedal must be placed in front of the master "
        "cylinder.\"* The clevis is at (-85, +602, +105) and a `sourced` 68 mm OTK "
        "0119.01 push rod puts the cylinder's mouth at y ~ +534 -- entirely behind the "
        "pedal, which is the whole content of the article",
    ),
    Joint(
        a="shifter_base",
        b="chassis_side_bar_r",
        kind="clamped",
        why="**replaces `shifter_base`/`chassis_rail_r`, which was 106.85 mm apart -- the "
        "worst gate-2 finding on the kart.** Spec §40.4's own cross-check is false on "
        "this chassis: it reads *\"the right main rail's centreline at y +330 "
        "interpolates to x 323 ... The bracket lands on the rail\"*, and §10 has since "
        "waisted the frame -- `frame_half_waist` is 139 at y +375, so at the lever's "
        "y +335 the rail is at x **156**. The lever's own position is not in doubt: two "
        "`sourced` shift-rod lengths and the two-finger gap to the rim fix it. What was "
        "wrong is the claim about which member is under it. Measured, the four "
        "candidates are `chassis_side_bar_r` **5.34 mm**, "
        "`chassis_bumper_socket_side_lower_front_r` 93.62, `chassis_rail_r` 106.85 and "
        "`chassis_tray_edge_r` 124.91 -- so the bracket clamps Art. 9.4.2's lower side "
        "bumper, whose forward leg crosses (325, 366, 81) on its way inboard to its "
        "front socket. That is also where a real KZ's gear-lever bracket goes: the lever "
        "stands beside the knee and at the knee the only tube out at x 320 is the side "
        "bumper. `cockpit._shifter_clamp` builds the strap and the Ø20 collar, and it "
        "reads the leg's two ends off `frame.py`'s published empties rather than "
        "re-deriving them -- `_corner` pushes the built corner 72 mm along +y, so a copy "
        "of the authored polyline gets the leg's *direction* wrong",
    ),
    Joint(
        a="shifter_base",
        b="shifter_lever",
        kind="pierced",
        why="the lever turns in **two nylon bushes** about its own rod's axis. OTK "
        "0111.002, sold with no dimensions; two of them is `sourced` as a shape from "
        "the part set. **There is no shift gate** -- the slotted plate that used to be "
        "here was the one invented part in this assembly, and a KZ's sequential detent "
        "is inside the gearbox",
    ),
    Joint(a="shifter_knob", b="shifter_lever", kind="pressed", why="the knob is on the lever"),
    Joint(
        a="shifter_lever",
        b="shifter_connector_arm",
        kind="clamped",
        why="a **serrated collet**, which is the hardware that lets the arm be set to "
        "the `sourced` 90 degrees against the rod. OTK 0111.B0A, and the linkage "
        "cannot exist without it: without the arm there is nothing for the rod to push",
    ),
    Joint(
        a="shifter_connector_arm",
        b="shift_rod_end_front",
        kind="bolted",
        why="a uniball joint on the arm's plain hole",
    ),
    Joint(
        a="shift_rod",
        b="shift_rod_end_*",
        kind="threaded",
        why="M8 on **opposing pitches** -- the assembly is a turnbuckle and adjusts "
        "with a wrench on the rod's own 13 mm flats without disconnecting",
    ),
    # `shift_rod_end_rear`/`engine_selector_arm` is **not** declared, because
    # §Powertrain has not built the selector arm. The rear end is left touching only
    # the rod, and the weak form of gate 2 catches it -- which is the correct outcome:
    # the arm is a real missing part and not a tolerance.
    # --- cockpit.py: the fuel tank Art. 9.3 requires -------------------------
    #
    # Art. **9.3**, PDF p. 22: **8 litres minimum**, and the kart had no tank at all.
    # Art. **4.7** *mandates* the position rather than permitting one, which is why
    # all three of `tank_center_*`'s coordinates are `derived`.
    Joint(
        a="fuel_tank",
        b="chassis_floor_tray",
        kind="seated",
        why="it sits on the pan, which is where Art. 4.6 now puts the pan -- forward "
        "of the central strut, under the driver's feet and the tank rather than under "
        "his backside",
    ),
    Joint(
        a="fuel_tank",
        b="fuel_tank_strap_*",
        kind="clamped",
        why="two straps over the molded channels; the count follows the channels. "
        "Art. 4.7: *\"A quick attachment to the chassis is strongly recommended\"*, so "
        "a cam buckle rather than a bolted steel band",
    ),
    Joint(
        a="fuel_tank_strap_*",
        b="chassis_rail_?",
        kind="clamped",
        why="both ends of both straps, down to the two main tubes Art. 4.7 puts the "
        "tank between",
    ),
    Joint(
        a="fuel_tank",
        b="fuel_tank_filler",
        kind="threaded",
        why="the cap screws into the tank's top-rear third, reached between the legs "
        "past the steering wheel",
    ),
    Joint(
        a="fuel_tank",
        b="fuel_tank_fitting_?",
        kind="welded",
        why="**three**, and the third is the point: the KZ tank differs from the OK "
        "tank by an extra fitting for a **return** line. Molded bosses on this mesh, "
        "which is a mesh-count decision and not a claim about the real part",
    ),
    Joint(
        a="fuel_tank_fitting_0",
        b="fuel_line_feed",
        kind="routed",
        why="Art. **5.6.1**: *\"Only one fuel line from the tank to the "
        "carburettor/fuel pump is allowed.\"* **One**, and it is a scrutineering fact "
        "rather than a styling choice. The return line is not a feed line and does "
        "not count against it",
    ),
    Joint(
        a="fuel_tank_fitting_2",
        b="fuel_line_return",
        kind="routed",
        why="the return, which is what the KZ tank has and the OK tank does not",
    ),
    # --- wave 3 (#190): joints the new powertrain and cockpit parts need -----
    Joint(
        a="chassis_cross_rear",
        b="exhaust_hanger_boss",
        kind="clamped",
        why="the mushroom boss stands out of a clamp bored on this tube's own "
        "centreline, so it reaches the tube",
    ),
    Joint(
        a="chassis_cross_rear",
        b="exhaust_silencer_bracket",
        kind="clamped",
        why="the same, at x +230",
    ),
    Joint(
        a="chassis_rail_l",
        b="radiator_bracket_upper",
        kind="clamped",
        why="as the lower rod",
    ),
    Joint(
        a="chassis_rail_l",
        b="radiator_bracket_lower",
        kind="clamped",
        why="each rod runs to the rail's own centreline inside its clamp, which is "
        "what makes the contact 0.0 rather than a tolerance",
    ),
    Joint(
        a="chassis_steering_hoop",
        b="steering_column",
        kind="pierced",
        why="the column passes **through** the lower bracket's bore. It used to be "
        "lifted 26 mm clear of it by `COLUMN_LOWER_CLEAR` on the reasoning that the "
        "filleted hoop's crown came up to meet it -- `build.tube` pulls that crown "
        "*below* the control point, so the lift bought 19 mm of air at the one joint "
        "the bracket exists for",
    ),
    Joint(
        a="steering_pitman",
        b="tierod_r",
        kind="bolted",
        why="the other side",
    ),
    Joint(
        a="steering_pitman",
        b="tierod_l",
        kind="bolted",
        why="the rod's own end reaches the plate alongside its rod end, because a "
        "uniball is a ball in the rod's eye and not a separate link",
    ),
    Joint(
        a="radiator_hose_upper",
        b="radiator_core",
        kind="routed",
        why="both hoses enter their tank in the corner, and a Ø28 hose on a 40 mm "
        "core cannot reach a tank without crossing the core's own face. Declared "
        "rather than dodged: moving the fitting off the corner is not what a real "
        "core does",
    ),
    Joint(
        a="radiator_hose_upper",
        b="radiator_end_inboard",
        kind="routed",
        why="the same, against the inboard end channel",
    ),
    Joint(
        a="radiator_hose_lower",
        b="radiator_fin_17",
        kind="routed",
        why="and against the outermost fin, which is the one beside that corner",
    ),
    Joint(
        a="engine_cylinder_base",
        b="exhaust_manifold_spigot",
        kind="pierced",
        why="the port is bored through the base casting as well as the barrel, so "
        "the manifold's spigot is inside both",
    ),
    Joint(
        a="engine_cylinder_base",
        b="exhaust_manifold_bolt_[01]",
        kind="threaded",
        why="the lower pair of the four M6 threads into the base casting -- the "
        "62 x 44 pattern straddles the parting between barrel and base",
    ),
    Joint(
        a="engine_cylinder_base",
        b="exhaust_spring_?",
        kind="clamped",
        why="each spring's forward hook wraps a bolt head that stands on the base "
        "casting's own face",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_cylinder",
        kind="bolted",
        why="**new with the 25 degree lean.** The deck plane inclines but the case's "
        "top stays flat at z 240, so the barrel's forward skirt dips into the "
        "casting. A prismatic deck is a crankcase change and this wave did not make "
        "one; the pair is a real bolted joint either way",
    ),
    Joint(
        a="engine_head",
        b="engine_plug_lead",
        kind="routed",
        why="the lead leaves the cap and lies against the head's rear quadrant on "
        "its way down the back of the engine",
    ),
    Joint(
        a="engine_head",
        b="radiator_hose_upper",
        kind="routed",
        why="the top hose meets the outlet elbow, which is a boss on this casting, "
        "so the hose's **last** bend is against the head. This said *first* and "
        "pointed a reader at the wrong end of the pipe: both routes are authored "
        "radiator-first and have been consumed that way since #190 wave 3b, so the "
        "head is where the run ends",
    ),
    Joint(
        a="fuel_tank",
        b="fuel_line_*",
        kind="routed",
        why="each line leaves its own fitting on the tank's top-rear face, so its "
        "first 20 mm is against the molding",
    ),
    Joint(
        a="fuel_tank_filler",
        b="fuel_tank_fitting_1",
        kind="welded",
        why="the vent nipple is molded into the filler's own neck boss",
    ),
    Joint(
        a="engine_carb_bowl",
        b="engine_intake_boot",
        kind="routed",
        why="the boot goes over the carburettor's 64 mm air spigot and the float bowl "
        "hangs off the same body 2 mm forward of it, so the rubber lies against the "
        "bowl's rear corner",
    ),
    Joint(
        a="cooling_hose_pump_engine",
        b="engine_crankcase_upper",
        kind="routed",
        why="the hose's last 40 mm lies along the case's inboard face on its way into "
        "`engine_water_inlet`, which is a boss on that face",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="exhaust_chamber",
        kind="pierced",
        why="§30.6 states this as a number rather than a hope: the rear protection's "
        "inner clear volume must *contain* the chamber at x -117..346, y -652..-734, "
        "z 170..318, and its own front face lands at y -687..-722 per Art. 9.5.5.1's "
        "15-50 mm gap. So both parts are inside its fore-aft band by construction and "
        "**the shell is cut for them rather than stopping in front of them**",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="exhaust_connector",
        kind="pierced",
        why="the U-bend crosses the same shell",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="exhaust_silencer",
        kind="pierced",
        why="and the can, which Art. 5.10 requires to discharge behind the driver and "
        "not past the kart's outer limits -- at x 280 the outlet is 420 mm inside the "
        "700 limit, so it is inside the protection and not beyond it",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="exhaust_silencer_band_0",
        kind="pierced",
        why="the two jubilee clips are on the body, inside the same cut",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="exhaust_silencer_bracket",
        kind="pierced",
        why="the bracket reaches the saddle through the same cut",
    ),
    Joint(
        a="drive_chain",
        b="drive_chain_guard",
        kind="pierced",
        why="**a guard that clears the chain everywhere is not covering it.** Art. 5.9 "
        "requires the guard down to the crown wheel's axis, which is below the chain's "
        "lower strand for the whole rear wrap, so the strand passes inside the cover's "
        "own outline. The pair that must *not* touch is the guard and the two sprockets, "
        "and 7.5 mm a side is what holds there",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_cylinder_base_nut_[23]",
        kind="threaded",
        why="the 25 degree lean drops the flange's forward corner into the casting, so "
        "the two forward base studs thread through the case as well as the flange. Only "
        "those two -- nuts 0 and 1 are the rear pair and stay clear",
    ),
    Joint(
        a="exhaust_silencer_bracket",
        b="exhaust_silencer_saddle",
        kind="bolted",
        why="the bracket's upper end reaches the saddle's underside beside the rubber "
        "isolator it bolts through",
    ),
    Joint(
        a="fuel_tank_strap_*",
        b="chassis_floor_tray",
        kind="pierced",
        why="each strap's ends come down past the pan's edge to reach the rail beneath "
        "it, so the pan is cut for two 25 mm straps. Art. 4.6 permits holes up to 10 mm "
        "and two 35 mm ones for the column and the shift lever, so a molded slot is "
        "outside its list -- **the honest reading is that the strap goes round the pan's "
        "edge**, and this declaration is what the built geometry says instead. Recorded "
        "as a joint and flagged in the report rather than made to look clean",
    ),
    Joint(
        a="fuel_tank_strap_*",
        b="chassis_tray_edge_?",
        kind="clamped",
        why="and over the pan's edging tube, which is the structure the buckle actually "
        "pulls against at that station",
    ),
    Joint(
        a="exhaust_silencer",
        b="exhaust_silencer_isolator",
        kind="seated",
        why="the rubber bush sits directly under the can's own barrel where the saddle "
        "cradles it, 8 mm below the saddle's top face",
    ),
    # --- bodywork.py: the front fairing and its mounting kit -----------------
    #
    # **`bodywork_nose_fairing`/`chassis_nose_hoop_lower` is deleted.** Art. 4.10.1
    # lists *"one front fairing mounting kit"* as its own homologated item and Art.
    # 9.5.2 dimensions its clamps, so the panel does not reach the frame at all: it
    # reaches the kit and the kit reaches the frame. Panel-to-hoop is now a pair
    # that must **not** overlap -- both bumper bars pass through the panel's open
    # back with the cavity clear above and below them, which is what a real CIK nose
    # looks like. Same shape as front matter §5a's radiator-and-seat rule.
    Joint(
        a="bodywork_nose_fairing",
        b="bodywork_fairing_support_u",
        kind="bolted",
        why="2x M8 through the panel's two molded bosses at x +-225, which is the "
        "OTK M4 form's Ø20x1.5 leg spacing of 450 mm. The bosses are in the "
        "panel's own mesh so a displaced fairing takes its mounts with it",
    ),
    Joint(
        a="bodywork_nose_fairing",
        b="bodywork_fairing_strut_?",
        kind="bolted",
        why="1x M8 each through the bosses at x +-275, the form's Ø16x1.5 pair at "
        "550 mm. Outboard and above the U-frame's legs, matching the form's photo "
        "where the thin struts stand outside the thick ones",
    ),
    Joint(
        a="bodywork_nose_fairing",
        b="bodywork_fairing_hook_?",
        kind="bolted",
        why="the two hook clamps bolt to the panel through the bosses at x +-115; "
        "they are the release mechanism, and `nose_fairing_pivot` is their line",
    ),
    Joint(
        a="bodywork_fairing_support_u",
        b="chassis_cross_front",
        kind="bolted",
        why="the U-frame's two legs clamp the front loop's legs at x +-225, where "
        "`_loop_leg_y` puts the leg centerline at y +606. Aimed at the station the "
        "loop actually occupies rather than at spec §50.8's (+-225, +545), which is "
        "61 mm of air on this chassis -- the same class of miss as the 104.65 mm "
        "pedal-mount waiver below",
    ),
    Joint(
        a="bodywork_fairing_support_u",
        b="bodywork_fairing_kit_tube_*",
        kind="welded",
        why="Art. 9.5.2's *\"2 support tubes of the clamps\"* stand on two stubs off "
        "the U-frame's front span at x +-45, in the U-frame's own mesh. Both tubes "
        "cross both stubs",
    ),
    Joint(
        a="bodywork_fairing_kit_tube_*",
        b="bodywork_fairing_hook_?",
        kind="clamped",
        why="Art. 9.5.2: *\"the 1 mm spacing between the hook clamps and the front "
        "fairing mounting kits\"*. Built at exactly 1.0 mm, which is inside "
        "CONTACT_TOLERANCE by construction -- so this joint passes with a stated "
        "regulation dimension rather than with a modeling standoff nobody chose",
    ),
    Joint(
        a="bodywork_fairing_strut_?",
        b="chassis_nose_hoop_upper",
        kind="clamped",
        why="**the upper bar, not the lower one.** Art. 9.4.1 puts the lower bar's "
        "tube top in a 70..110 mm band and the upper bar's in 200..250, and a "
        "1090 x 287 x 227 panel cannot be carried from a 110 mm ceiling. Each strut "
        "is run 10 mm past the bar's nominal centerline so a bend fillet moving the "
        "real centerline still leaves the clamp gripping tube",
    ),
    # --- bodywork.py: the front panel ---------------------------------------
    Joint(
        a="bodywork_front_panel",
        b="bodywork_front_panel_stay_?",
        kind="bolted",
        why="Art. 9.5.3: *\"The panel's lower section must be securely attached to "
        "the front part of the chassis frame, directly or indirectly.\"* These two "
        "stays are the indirectly",
    ),
    Joint(
        a="bodywork_front_panel",
        b="bodywork_front_panel_bar",
        kind="bolted",
        why="and *\"Its upper part must be securely attached to the steering column "
        "support with one or more independent bars\"*. One part, two legs at x +-60 "
        "-- a single central bar passes 5.5 mm from the steering column's surface",
    ),
    Joint(
        a="bodywork_front_panel_stay_*",
        b="chassis_cross_front",
        kind="clamped",
        why="both stays land on the front loop's leg centerline at x +-250, y +572. "
        "They splay outboard from the panel's own x +-110 because at that x the loop "
        "is 175 mm further forward, at y +760",
    ),
    Joint(
        a="bodywork_front_panel_bar",
        b="chassis_steering_support_upper",
        kind="bolted",
        why="Art. 9.5.3's *\"steering column support\"* is this part, which wave 1 "
        "built and #190 required. The landing is 0.90 of the way from the support's "
        "own foot to its own apex rather than a point, so it tracks "
        "`steering_support_foot_x` instead of restating it",
    ),
    # --- bodywork.py: the side pods -----------------------------------------
    #
    # **`bodywork_sidepod_?`/`chassis_side_bar_?` is deleted.** With the outer face
    # out at Art. 9.5.4's tapered datum the pod's mouth is at x 505 and the lower
    # bar's surface at 510, so the bar is *inside* the C and the pod's shell no
    # longer reaches it -- a joint that cannot touch is a gate-2 failure by
    # construction. The four brackets are what attach the pod, which is also what
    # makes *"securely attached to the side bumpers"* a part rather than a claim.
    Joint(
        a="bodywork_sidepod_r",
        b="bodywork_sidepod_bracket_r?",
        kind="bolted",
        why="two brackets per side is what a CIK pod carries, at y +180 and -200 -- "
        "both on the lower bar's 420 mm straight run. Each arm starts 6 mm inside "
        "the flank's skin, so contact is measured at the clamp",
    ),
    Joint(
        a="bodywork_sidepod_l",
        b="bodywork_sidepod_bracket_l?",
        kind="bolted",
        why="the left-hand pair of the same joint",
    ),
    Joint(
        a="bodywork_sidepod_bracket_r?",
        b="chassis_side_bar_r",
        kind="clamped",
        why="M10 through a rubber bush on the bar, 1.5 mm off its surface -- KG C2 "
        "form p. 3's `RC.182.x`/`RC.228.x` bush pair, whose suffix is the tube "
        "diameter. The arm reaches ~140 mm **inboard** from the flank rather than "
        "50 mm outboard from the mouth, because the pod's mouth is now outboard of "
        "the bar it bolts to. Spec §50.10's 50 mm reach is measured from a mouth at "
        "505 to a bar at 455, and Art. 9.4.2 moved that bar to 500",
    ),
    Joint(
        a="bodywork_sidepod_bracket_l?",
        b="chassis_side_bar_l",
        kind="clamped",
        why="the left-hand pair of the same joint",
    ),
    # --- bodywork.py: the rear wheel protection -----------------------------
    #
    # **`bodywork_rear_panel`/`chassis_rear_bumper` is deleted.** Art. 4.11 puts the
    # supports *"on the two main tubes of the chassis"* and not on the hoop, so the
    # bumper is a pair the panel must not overlap: the panel's front wall is
    # vertical to 0.78 of its height for that reason and clears the hoop's front
    # surface by 6.2 mm.
    Joint(
        a="bodywork_rear_panel",
        b="bodywork_rear_outer_?",
        kind="bolted",
        why="Art. 9.5.5.1's *\"two adjustable outer parts\"*, bolted through a 40 mm "
        "adjustment slot at |x| 360..400. The split falls between the centerline "
        "clearance window and the two wheel windows so it cuts neither",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="bodywork_rear_support_?",
        kind="bolted",
        why="Art. 4.11: *\"fastened to the homologated chassis by at least two "
        "points using supports homologated with the protection\"*. Both land 2 mm "
        "inside the panel's front wall",
    ),
    Joint(
        a="bodywork_rear_support_l",
        b="chassis_rail_l",
        kind="clamped",
        why="*\"These supports must be mounted ... on the two main tubes of the "
        "chassis (respecting the homologated dimension F)\"* -- so the rail, at its "
        "own `frame_half_rear` = 310 rather than spec §50.11's stale +-215, which "
        "was read off a rail path from before wave 1 moved it. M10 through a rubber "
        "bush on a 30 mm tube: KG C2 form p. 3's `RC.182.30`/`RC.228.30`, and "
        "`tube_main` is 30, which is the `.30` variant",
    ),
    Joint(
        a="bodywork_rear_support_r",
        b="chassis_rail_r",
        kind="clamped",
        why="the right-hand pair of the same joint",
    ),
)


# --- the driver, and why he is not in the table above ----------------------
#
# Spec §60.1.6 is the contract; ADR-0055 is the decision. Issue #200 is the gate
# that reads this, issue #17 is the module that builds the parts.
#
# `KINDS` is closed on purpose and every one of its nine kinds was drawn from a
# real joint on a kart. **A driver is none of them.** He is not fastened to the
# chassis, he carries no load through a fastener, and a hand closed around a rim
# is not a clamp -- widening `KINDS` to fit him would destroy the property that
# makes that vocabulary worth having, which is that a tenth kind means finding a
# tenth kind of joint on a real kart first. So the driver gets his own table and
# his own three words.
#
# What the table is *for*: gates 1 and 2 have never had an opinion about the
# volume a seated human occupies, because that volume did not exist in the build.
# `radiator_hose_upper` crossed the driver's lumbar spine 79.4 mm deep, measured,
# while both gates stayed green -- and the only thing that caught it was a human
# turning a viewport, which is the loop #192 exists to replace. Two clauses of the
# regulation are *about the driver* and are unverifiable without him: Art. 9.5.3's
# *"must not impede the normal functioning of the pedals or cover any part of the
# feet"* and Art. 9.5.4's *"No part of the side bodywork may cover any part of the
# driver seated in the normal driving position."*

#: The driver's contact vocabulary. Three words, and the same closed-set rule
#: applies: a fourth means finding a fourth way a driver touches a kart.
CONTACT_KINDS: frozenset[str] = frozenset(
    {
        # Weight passes through this surface into the chassis.
        "sits_on",
        # A hand closes around it.
        "grips",
        # A foot bears on it.
        "presses",
    }
)


@dataclasses.dataclass(frozen=True)
class Contact:
    """One declared driver contact. Same two-way contract as a `Joint`.

    It **permits** the pair to interpenetrate and it **requires** the pair to be
    in contact within `CONTACT_TOLERANCE`, so a glove 40 mm off the rim fails as
    loudly as a hose through the spine. `a` is always the `driver_*` side.
    """

    a: str
    b: str
    kind: str
    why: str


DRIVER_CONTACTS: tuple[Contact, ...] = (
    Contact(
        a="driver_pelvis",
        b="seat_shell",
        kind="sits_on",
        why="the H-point sits 95 mm above the pan at z 36; this pair is where 78 kg "
        "of the kart's 170 enters the chassis, and it is the pair issue #194's "
        "mass lumps are placed against",
    ),
    Contact(
        a="driver_torso",
        b="seat_shell",
        kind="sits_on",
        why="the back bears on the shell at the 22 deg rake spec §60.1.1 sources "
        "from the Tillett T11 ML chart. The driver's torso is at 25 deg and the "
        "shell at 22, which is deliberate -- the spine continues above where the "
        "shell ends -- so the two touch at the shell's top edge",
    ),
    Contact(
        a="driver_rib_protector",
        b="seat_shell",
        kind="sits_on",
        why="ADR-0057: the protector is worn *between* torso and shell over its "
        "z 250-450 band, so it -- not the torso -- is what bears on the shell "
        "there. The torso's own row above is the contact at the shell's top "
        "edge, where the 25 deg torso leaves the 22 deg shell; both contacts "
        "are physically real and they touch in different places. Until #17's "
        "surface pass re-insets the torso by the protector's thickness, the "
        "protector overlaps the shell by roughly its own 12-18 mm, and this "
        "row is what makes that overlap declared rather than waived",
    ),
    Contact(
        a="driver_thigh_?",
        b="seat_shell",
        kind="sits_on",
        why="the pan's front lip at y -50 carries the thighs. §60.2.4 records that "
        "the *built* lip stands about 115 mm forward of a real T11 ML's, so this "
        "pair is also where that error will surface",
    ),
    Contact(
        a="driver_glove_?",
        b="steering_rim",
        kind="grips",
        why="§60.2.1's rim, hands at the 3 and 9 o'clock positions with the wheel "
        "straight ahead. §60.2.2 measures the reach and it does not close at a "
        "comfortable elbow angle, so this contact is the assertion that catches an "
        "arm quietly lengthened to reach",
    ),
    Contact(
        a="driver_boot_l",
        b="pedal_brake_pad",
        kind="presses",
        why="the brake is the left foot, at -`pedal_separation`/2 because #202 "
        "made the ball of the foot a derivation off the live pedal rather than a "
        "field that could drift 140.8 mm from it. Named per side rather than "
        "globbed because a mirrored pedal assignment is exactly the bug a glob "
        "would hide",
    ),
    Contact(
        a="driver_boot_r",
        b="pedal_throttle_pad",
        kind="presses",
        why="the throttle is the right foot at +`pedal_separation`/2, the other "
        "half of the same pair",
    ),
)

# **`shifter_lever` is deliberately absent.** A KZ is shifted with the right hand
# off the wheel, so that contact is *momentary*: declaring it permanently would
# assert one hand in two places at once, and gate 3 would then require it -- a
# driver whose right hand is on the rim would fail for not also being on the
# lever. The omission is the record of that decision rather than an oversight.
#
# Nothing about the driver is in `JOINTS`, and gates 1 and 2 skip every
# `driver_*` part: he is not mounted to anything, so gate 2's "every part touches
# a neighbor" is meaningless for him, and gate 1's overlap rule would fire on the
# six contacts above. Intra-driver pairs are skipped outright -- one articulated
# body authored as nineteen segments interpenetrates at every anatomical joint by
# construction, and declaring nineteen of those would be bookkeeping that asserts
# nothing.


# --- known-outstanding defects ---------------------------------------------
#
# Everything here fails a gate today, is a real fault in the mesh, and is
# downgraded to a warning line naming its issue so that a night's geometry work
# is not blocked by a fault it did not cause. Nothing else is downgraded.
#
# `measured` was taken at high detail. An entry that stops failing is fatal --
# "this is fixed, delete the waiver" -- so this list shrinks and cannot rot.

OPEN_DEFECTS: tuple[Defect, ...] = (
    # -- gate 1, #190: the rear protection's front edge has nowhere to be -----
    #
    # These two are one fact and it is arithmetic, not a modeling slip. Art. 9.5.5.1
    # caps the rear overhang at 400 and the KG C2 form's sourced depth is 187, so
    # the protection's front face lands at y -705 (`overhang_rear_protection` 367,
    # the middle of the 349.5..384.5 band the tire gap allows). Art. 4.10.2 then
    # requires a 5 mm minimum corner radius, which on a 3.8 mm wall can only be a
    # returned lip -- and a returned lip needs **10 mm of material behind the free
    # edge**, i.e. back to y -715.
    #
    # Both of §Chassis's rearmost tubes are inside that 10 mm:
    #
    #     chassis_cross_tail    y -713, Ø22 -> front surface -702, 3 mm forward
    #                           of the panel's own front face
    #     chassis_rear_bumper   legs at x +-310 from (y -715, z 50) to (-725, 140),
    #                           front surface -715, exactly on the lip's end
    #
    # There is no z band that escapes either: the cross tail sits at z 39..61 and
    # the panel's clearance windows put its bottom edge at 40, while the bumper's
    # legs sweep z 50..140 and the panel's bottom edge between windows is 95.
    #
    # Three ways out and none of them belongs to this wave. `cross_tail_y` could go
    # back 12 mm -- `params.py`'s own docstring records two readings for it "within a
    # tube diameter" and the other one is -724. The bumper's leg roots could start at
    # `rail_rear_y` rather than 20 mm forward of it. Or `overhang_rear_protection`
    # could come down to 352, which clears both and costs 15 mm of the tire gap's
    # 35 mm of margin. Recorded with the numbers so §Chassis can pick.
    Defect(
        a="bodywork_rear_panel",
        b="chassis_cross_tail",
        gate="overlap",
        measured=126,
        issue="#190",
        why="the frame's rear strut's front surface is at y -702 and the "
        "protection's front face at -705, so 3 mm of the tube is forward of the "
        "panel and the returned lip's 10 mm band is inside the rest of it. See the "
        "block comment above for the three fixes and who owns them",
    ),
    Defect(
        a="bodywork_rear_panel",
        b="chassis_rear_bumper",
        gate="overlap",
        measured=128,
        issue="#190",
        why="the bumper hoop's two legs stand at the same rail station the panel's "
        "front wall crosses, and their front surface at y -715 is exactly where the "
        "lip's fold ends. **The panel-to-bumper *joint* is deleted** either way -- "
        "Art. 4.11 puts the supports on *\"the two main tubes of the chassis\"* and "
        "`bodywork_rear_support_?` is what carries the panel now -- so this is a "
        "collision to clear rather than a contact to keep",
    ),
    # -- gate 1, #190 wave 3: the powertrain and cockpit moved and four other
    # assemblies did not -------------------------------------------------------
    #
    # Every entry below was measured on the built mesh after §30 and §40 landed, and
    # each names the pair that has to move rather than the one that did. They are
    # waivers and not declarations because none of them is a joint.
    # Two #190 waivers deleted here by #201, and by accident rather than by aim:
    # `bodywork_front_panel`/`pedal_*_pad` (was 27 pairs) and
    # `chassis_floor_tray`/`pedal_*` (was 72) both cleared when the pedals moved
    # from x +-85 to +-150 -- outboard of the panel's edge and of the tray's y
    # band at that x. The stale-waiver check is what noticed.
    Defect(
        a="bodywork_front_panel_bar",
        b="steering_bearing_upper",
        gate="overlap",
        measured=30,
        issue="#190",
        why="Art. 4.5.2's *\"two collars between the column brackets\"* is now built -- the "
        "upper nylon block at (0, +262, +393) -- and Art. 9.5.3 requires the panel's "
        "upper part attached to *that* support with independent bars. So the bar and "
        "the block belong together and the pair is a **missing joint on the bodywork "
        "side**, not a collision: the bar has to end on the block rather than pass "
        "through where the block used to not exist",
    ),
    Defect(
        a="bodywork_front_panel_stay_?",
        b="pedal_mount_*",
        gate="overlap",
        measured=24,
        issue="#190",
        why="the panel's stays run down to the frame through x +-125..+-259, which is the "
        "corridor §40.5 moved the pedal mounts into when it re-aimed them at the front "
        "loop's legs at (+-259, +560, +50). Both are right about where they belong and "
        "the two have not been reconciled",
    ),
    Defect(
        a="brake_*",
        b="radiator_*",
        gate="overlap",
        measured=92,
        issue="#190",
        why="**the left main rail is now carrying three assemblies at one station.** §20.6 "
        "puts `brake_balance_regulator` and `brake_line_rear` on the left rail's "
        "straight run inboard of the centreline, and §30.7 moves both radiator brackets "
        "onto the same rail with Ø30 clamps -- the lower one lands at y -169, which is "
        "inside the regulator. The radiator's anchor is a regulation (Art. 4.2.3 puts "
        "the welded radiator points on the frame) and the regulator's station is not, "
        "so the regulator is the part that should move. §Running gear owns it",
    ),
    Defect(
        a="chassis_bumper_socket_side_*_rear_l",
        b="radiator_*",
        gate="overlap",
        measured=96,
        issue="#190",
        why="the left side bumper's rear sockets against the re-placed core. **Not solvable "
        "by moving the bar**, and the arithmetic is why it is a waiver: Art. 9.4.2 fixes "
        "the attachment pitch at 500 +-5 and the front tyre blocks the forward station, "
        "so the rear socket has to land where the radiator is. §30.7 brought the core "
        "in from 265 to 250 wide and down from z 497 to 408, which is 15 mm and 89 mm "
        "in the right direction and not enough. §Chassis and §Bodywork own the bar",
    ),
    Defect(
        a="chassis_side_bar_l",
        b="radiator_tank_low",
        gate="overlap",
        measured=51,
        issue="#190",
        why="the same fact at the bar itself rather than at its socket",
    ),
    Defect(
        a="chassis_seat_strut_rear_r",
        b="engine_intake_boot",
        gate="overlap",
        measured=82,
        issue="#190",
        why="the right rear stay climbs from the bearing hanger to the seat's upper ear "
        "across the volume the intake boot now uses. The boot is where it is because "
        "the rearward pipe took its old lane: at x 252 +-34 it clears the pipe's "
        "inboard face at 294 by 9 mm, so it has no room to go further inboard, and the "
        "stay has no room to go further outboard without meeting the pipe itself",
    ),
    # **`chassis_steering_support_upper`/`fuel_tank*` is deleted, not waived.** It was 192
    # pairs against the tank and 212 against its rear strap, and both are zero. The V is
    # now four points a leg: up outboard of Art. 4.7's tank, level across above its rear
    # strap's crown, and only then in to the apex. `params.steering_support_shoulder_x`
    # carries the arithmetic that says no *straight* V can do it -- a straight leg needs
    # its foot at x 445 to cross the tank's flank above the tank's top, and the rails are
    # at 286. The final segment is collinear with the straight foot-to-apex line, which is
    # what keeps §Bodywork's `bodywork_front_panel_bar` landing on real tube; see
    # `frame.SUPPORT_COLLINEAR_FROM`.
    Defect(
        a="cooling_hose_pump_engine",
        b="engine_battery",
        gate="overlap",
        measured=18,
        issue="#190",
        why="the pump-to-crankcase hose passes under the battery, which moved inboard to "
        "x 143..228 to clear the re-routed intake boot. 18 pairs; the battery has one "
        "free axis left and it is forward, into the crankcase",
    ),
    # **`drive_*`/`seat_shell` is deleted, not waived**, and the resolution is the seat's.
    # Wave 3 found 90 pairs on `drive_output_shaft`, 60 on `drive_chain_guard`, 50 on
    # `drive_chain` and 48 on `drive_output_sprocket`, and called it a design decision
    # needing both sections. It is, and the arithmetic settles it one way: the shell's
    # right edge at the driveline's height is x 173..179 and `engine_clutch_cover`'s
    # inboard face is at 182, so the free lateral window is **6.8 mm** and a 9 mm chain
    # inside a compulsory 32 mm Art. 5.9 guard does not fit in it at any `chain_x`. The
    # chain cannot go outboard of the shell and it cannot go inboard of it -- that is the
    # driver -- so the shell is relieved on the right, which is why real KZ shells are
    # sold handed. `cockpit.SEAT_CHAIN_RELIEF` is the cutaway and it carries the depth.
    Defect(
        a="engine_cylinder",
        b="engine_plug_lead",
        gate="overlap",
        measured=38,
        issue="#190",
        why="the plug lead leaves a cap that the 25 degree lean rotated 46 mm forward and "
        "10 mm down, and its first waypoint still assumes an upright barrel. Cosmetic, "
        "internal to §30, and the only one in this list that is simply unfinished",
    ),
    Defect(
        a="engine_head_nut_5",
        b="engine_plug_lead",
        gate="overlap",
        measured=31,
        issue="#190",
        why="the same lead against one of the six head nuts",
    ),
    Defect(
        a="fuel_line_feed",
        b="shift_rod",
        gate="overlap",
        measured=20,
        issue="#190",
        why="both run rearward along the driver's right and the corridor is shared: the rod "
        "is pinned by a `sourced` 495 mm length between two joints and the line by the "
        "tank's Art. 4.7 position, so the two cross at about y +90. The line is the free "
        "one and it wants a lane below the rod rather than beside it",
    ),
    # **`radiator_hose_lower`/`seat_*` is deleted, not waived, and the waiver's diagnosis
    # was wrong even though its measurement was right.** It read the 101 pairs on
    # `seat_shell` and 48 on `seat_bracket_lower_l` as a 33 mm corridor between the
    # bracket at x -207 and the core's inboard face at -240, and proposed spending the
    # radiator's Art. 5.3.1 lateral margin on it. The leg in that corridor was never the
    # leg touching the seat: `_cooling` built the hose from
    # `[core fitting] + reversed(waypoints) + [engine fitting]`, so a waypoint list
    # authored radiator-first was swept engine-first and two of the four legs crossed the
    # driver. `HOSE_LOWER_ROUTE` has the four legs and every clearance on them. The core
    # did not move and did not need to.
    # **`radiator_core`/`radiator_hose_lower` is deleted and replaced by a declared
    # `routed` joint.** The waiver called it *"the outboard half of the same 33 mm
    # problem, seen from the core"*, and with the hose's waypoint order fixed the 20
    # remaining pairs are all at the low tank's own inlet -- the same fact as the three
    # joints already declared there against `radiator_tank_low`, `radiator_end_inboard`
    # and `radiator_fin_17`. A hose entering a tank in the corner of a core is inside the
    # core's footprint by construction, so it is a joint and not a collision.
    Defect(
        a="shifter_base",
        b="shifter_connector_arm",
        gate="overlap",
        measured=16,
        issue="#190",
        why="the collet is 75 mm up the rod, above both nylon bushes, and the arm still "
        "grazes the bracket plate's upper corner. 16 pairs, internal to §40, and the "
        "plate is 46 mm tall where a real OTK bracket is slotted rather than solid",
    ),
    # -- gate 1: parts built inside other parts ------------------------------
    Defect(
        a="drive_output_shaft",
        b="engine_clutch_bolt_4",
        gate="overlap",
        measured=28,
        issue="#192",
        why="a clutch cover bolt is buried in the output shaft -- the bolt "
        "circle is inside the shaft's radius at that angle",
    ),
    # -- gate 1, #190: the footprint moved and four other assemblies did not --
    #
    # Every entry below is the *same* fact seen from a different part: a chassis
    # built to `docs/KART_SPEC.md` §10 collides with bodywork, powertrain and
    # cockpit parts that are still built to the old footprint. They are waivers
    # rather than declarations because none of them is a joint -- a fairing is not
    # welded to a frame loop -- and they are recorded per pair, with the number,
    # so the wave that owns the other part can see exactly what it has to clear.
    Defect(
        a="chassis_bumper_socket_side_upper_rear_l",
        b="radiator_tank_low",
        gate="overlap",
        measured=107,
        issue="#190",
        why="the socket half of the same fact, recorded separately because it is "
        "the part that would have to move if the radiator did not",
    ),
    Defect(
        a="chassis_side_bar_upper_r",
        b="engine_starter",
        gate="overlap",
        measured=54,
        issue="#190",
        why="and the starter motor, 90 mm of the same intrusion at the rear "
        "socket. Same resolution as the exhaust: §30 re-places the cluster",
    ),
    Defect(
        a="chassis_bumper_socket_side_upper_rear_r",
        b="engine_*",
        gate="overlap",
        measured=90,
        issue="#190",
        why="the right rear socket post against the starter. The two clear "
        "windows on this rail are y > +240 and y < -345, and a 500 mm pitch does "
        "not fit between them",
    ),
    # -- gate 2: declared joints that do not touch ---------------------------
    Defect(
        a="exhaust_manifold_bolt_?",
        b="exhaust_spring_?",
        gate="gap",
        measured=10.19,
        issue="#190",
        why="each spring's forward hook is 10 mm from the bolt head it wraps. `_helix` "
        "starts its coil one `EXHAUST_SPRING_COIL_RADIUS` off the axis it is given, so "
        "the hook's own first turn is 6 mm out from the point it is authored at and the "
        "M6 head's across-corners radius is 5.8 -- the two miss by construction and the "
        "fix is a hook that is not a helix. Cosmetic, internal to §30, and the springs "
        "themselves are `sourced` at 70 mm free length and correctly placed on the pipe's "
        "own tabs at s = 70",
    ),
    Defect(
        a="brake_master_rear",
        b="brake_pushrod*",
        gate="gap",
        measured=8.93,
        issue="#190",
        why="Art. 4.12.2's doubled link is a **balance bar** and §20.6 built it as one "
        "rod plus one cable reaching both cylinders from the brake pedal's plate. §40.5 "
        "moved that plate: an organ arm on a bottom pivot at y +610 crosses a given "
        "height 38 mm further forward than the old hanging plate did, so the rod's "
        "direction changed and it no longer grazes the rear cylinder. The rod still "
        "reaches the front one. §Running gear owns the re-aim, and the number to aim at "
        "is `wheels._pedal_plate_y`, which this wave rewrote for the new arm",
    ),
    Defect(
        a="brake_pushrod",
        b="pedal_mount_l",
        gate="gap",
        measured=55.63,
        issue="#190",
        why="the same fact at the other end: the mount plates moved 105 mm to reach the "
        "front loop's legs at (+-259, +560, +50), and the pushrod was declared against "
        "the plate it used to pass through",
    ),
    Defect(
        a="chassis_bearing_hanger_r",
        b="cooling_pump_bracket",
        gate="gap",
        measured=90.50,
        issue="#190",
        why="§30.7 gives the pump bracket as *\"~145 mm, `chassis_bearing_hanger_r` to "
        "the pump\"* and `estimated`, and 145 mm is not enough: the belt places the pump "
        "spindle at (160, -386, 110) and the hanger plate spans y -562..-487 at z "
        "100..200, which is 100 mm behind it. The bracket also cannot run straight "
        "outboard, because that crosses the belt loop -- it passes 22 mm *under* the "
        "pump instead. A real bracket is an L and this one is a bar; internal to §30 and "
        "the only part of the cooling drive still floating",
    ),
    # **`chassis_rail_r`/`shifter_base` is deleted, not waived.** It was 106.85 mm and it
    # is now a closed `clamped` joint on `chassis_side_bar_r` -- see that entry for which
    # of the four candidate members the bracket actually reaches and by how much. The
    # rail was never under the lever; §40.4's cross-check was arithmetic on a frame that
    # §10 had already waisted.
    Defect(
        a="engine_battery",
        b="engine_crankcase_upper",
        gate="gap",
        measured=12.85,
        issue="#190",
        why="the battery went inboard to x 143..228 because the intake boot took its "
        "old lane, and the case's inboard face is at 240. The boot's own lane is fixed "
        "at both edges -- 9 mm to the pipe's inboard face at 294 and 32 mm to the seat "
        "shell -- so the battery has one free axis left and it is forward, into the "
        "crankcase's own y band. 12.85 mm, and it wants the strap bracket lengthened "
        "rather than the battery moved",
    ),
    # -- gate 3, #200: the first measured pass, seeded off the built mesh -----
    #
    # 48 findings at high detail, three causes, three tickets. Depths are
    # `genkart.driver_depth`'s figure -- torso rows measured against the
    # ADR-0057 rake-plane clip -- and the ADR-0055 seed table is NOT the source:
    # its leg and boot rows predate #201's 300 mm pedal separation and #202's
    # live foot chain, and 24 of its findings no longer exist.
    #
    # #204 -- the drive cluster at x 100-112 is inboard of the seat's own +-184
    # flank. A kart-geometry fault: the pelvis is where the sourced H-point
    # puts it, and a chain run belongs outboard of the seat.
    Defect(
        a="drive_chain*",
        b="driver_pelvis",
        gate="driver",
        measured=17.64,
        issue="#204",
        why="chain 5.77 mm and chain guard 17.64 mm inside the pelvis block; #200 "
        "flagged this cluster as suspicious before the gate could measure it",
    ),
    Defect(
        a="drive_chain*",
        b="driver_torso",
        gate="driver",
        measured=16.04,
        issue="#204",
        why="the same run higher up: guard 16.04 mm into the torso's clipped volume",
    ),
    Defect(
        a="drive_output_*",
        b="driver_pelvis",
        gate="driver",
        measured=5.58,
        issue="#204",
        why="output shaft 5.58 mm and sprocket 3.82 mm into the pelvis -- the "
        "sprocket at x 112 is what puts the whole chain line inboard",
    ),
    Defect(
        a="drive_output_*",
        b="driver_torso",
        gate="driver",
        measured=2.25,
        issue="#204",
        why="shaft and sprocket graze the torso's lower flank, 2.25 and 0.73 mm",
    ),
    # #205 -- Art. 9.5.3 says the front panel must not cover any part of the
    # feet, and as built it does. The boots are where #202's live pedal
    # derivation puts them, so the panel is the part that is wrong.
    Defect(
        a="bodywork_front_panel",
        b="driver_boot_?",
        gate="driver",
        measured=29.07,
        issue="#205",
        why="the panel's lower edge is 29.07/28.13 mm into the boots and occludes "
        "17/16 of their sample points from straight above -- the gate's ray-up "
        "form of Art. 9.5.3's 'cover any part of the feet'",
    ),
    Defect(
        a="bodywork_front_panel_stay_?",
        b="driver_boot_?",
        gate="driver",
        measured=16.42,
        issue="#205",
        why="both panel stays run 16.42 mm through the foot box on their way down "
        "to the pedal mounts",
    ),
    # #206 -- the limb paths. The leg hangs off an estimated +-180 knee splay
    # read from one photograph and the arm pose is the two-link solve that
    # closes the reach, so none of these can be adjudicated against a sourced
    # figure. Real shins do straddle the tank and pass by the side bars; which
    # of these is the mesh being honest about a tight cockpit and which is the
    # estimate being wrong is exactly what the ticket is for.
    Defect(
        a="chassis_side_bar_upper_?",
        b="driver_shank_?",
        gate="driver",
        measured=24.80,
        issue="#206",
        why="the shins cross the upper side bars symmetrically, 24.80 mm each side",
    ),
    Defect(
        a="chassis_side_bar_upper_?",
        b="driver_boot_?",
        gate="driver",
        measured=30.37,
        issue="#206",
        why="the boots sit across the same bars ahead of the shins, 29.72/30.37 mm",
    ),
    Defect(
        a="chassis_bumper_socket_side_upper_front_?",
        b="driver_shank_?",
        gate="driver",
        measured=30.01,
        issue="#206",
        why="the front upper bumper sockets are in the shin line, 30.00/30.01 mm",
    ),
    Defect(
        a="chassis_bumper_socket_side_upper_front_?",
        b="driver_boot_?",
        gate="driver",
        measured=33.67,
        issue="#206",
        why="and in the boot line just below, 33.67/33.53 mm -- the deepest "
        "single limb finding of the pass",
    ),
    Defect(
        a="chassis_tray_edge_?",
        b="driver_boot_?",
        gate="driver",
        measured=4.14,
        issue="#206",
        why="the boot soles clip the floor tray's turned edges, 0.00/4.14 mm",
    ),
    Defect(
        a="brake_*",
        b="driver_boot_l",
        gate="driver",
        measured=29.27,
        issue="#206",
        why="the rear master (29.27 mm), its bracket (8.82) and both brake lines "
        "(0.00/1.93) share the left foot's corridor -- #201's pedal package "
        "moved outboard into the same space the masters did",
    ),
    Defect(
        a="brake_master_rear",
        b="driver_shank_l",
        gate="driver",
        measured=3.25,
        issue="#206",
        why="the same master grazes the left shin above the boot, 3.25 mm",
    ),
    Defect(
        a="pedal_brake",
        b="driver_boot_l",
        gate="driver",
        measured=6.30,
        issue="#206",
        why="the sole wraps 6.30 mm over the pedal arm above the declared pad "
        "contact. Named per side like the contacts, because a mirrored pedal "
        "assignment is exactly the bug a glob would hide",
    ),
    Defect(
        a="pedal_throttle",
        b="driver_boot_r",
        gate="driver",
        measured=6.30,
        issue="#206",
        why="the right sole does the same over the throttle arm, 6.30 mm",
    ),
    Defect(
        a="tierod_?",
        b="driver_boot_?",
        gate="driver",
        measured=6.85,
        issue="#206",
        why="the tie rods pass 6.77/6.85 mm through the boot tops",
    ),
    Defect(
        a="tierod_?",
        b="driver_shank_?",
        gate="driver",
        measured=26.38,
        issue="#206",
        why="and 26.38/25.68 mm through the shins behind them",
    ),
    Defect(
        a="fuel_tank*",
        b="driver_shank_?",
        gate="driver",
        measured=22.70,
        issue="#206",
        why="the shins straddle the tank -- which real legs genuinely do -- at "
        "14.41 mm into the tank and 22.70 into its front strap",
    ),
    Defect(
        a="steering_rim",
        b="driver_shank_?",
        gate="driver",
        measured=14.14,
        issue="#206",
        why="the rim's lower arc crosses both shins 14.14 mm where they rise to "
        "the knees",
    ),
    Defect(
        a="steering_clutch_lever",
        b="driver_shank_l",
        gate="driver",
        measured=8.11,
        issue="#206",
        why="the clutch lever reaches 8.11 mm into the left shin's lane",
    ),
    Defect(
        a="steering_rim",
        b="driver_thigh_?",
        gate="driver",
        measured=11.74,
        issue="#206",
        why="the rim also sits 6.91/11.74 mm into the thighs -- the wheel-to-leg "
        "relationship #199 says cannot be judged against nobody",
    ),
    Defect(
        a="steering_rim",
        b="driver_forearm_?",
        gate="driver",
        measured=18.30,
        issue="#206",
        why="the fist center is *on* the rim by §60.2.1, so the wrist end of each "
        "forearm clips it by construction, 16.79/18.30 mm",
    ),
    Defect(
        a="steering_spokes",
        b="driver_glove_?",
        gate="driver",
        measured=7.96,
        issue="#206",
        why="the gloves close around the rim at 3 and 9 o'clock and their "
        "fingertips reach 7.96 mm into the spoke plate",
    ),
    Defect(
        a="engine_head",
        b="driver_upper_arm_r",
        gate="driver",
        measured=2.36,
        issue="#206",
        why="the right upper arm hangs at the 108.7 deg elbow the reach solve "
        "produced and grazes the head 2.36 mm",
    ),
    Defect(
        a="radiator_hose_upper",
        b="driver_upper_arm_r",
        gate="driver",
        measured=14.31,
        issue="#206",
        why="the rerouted upper hose cleared the chest (ADR-0055's worked "
        "example) and now passes 14.31 mm through the drooped right upper arm",
    ),
)


# --- reading the table -----------------------------------------------------


def _check_vocabulary() -> None:
    """Kinds, gates and duplicate entries — everything checkable without a mesh.

    Runs at import, so a typo in a `kind` fails before Blender has built the
    first tube rather than after the last one.
    """
    for joint in JOINTS:
        if joint.kind not in KINDS:
            raise SystemExit(
                "joints.py: %s/%s declares kind %r, which is not one of %s.\n"
                "           The vocabulary is closed on purpose. Adding a kind "
                "means finding a\n"
                "           tenth kind of joint on a real kart first."
                % (joint.a, joint.b, joint.kind, ", ".join(sorted(KINDS)))
            )
        if not joint.why.strip():
            raise SystemExit("joints.py: %s/%s has no why" % (joint.a, joint.b))
    for defect in OPEN_DEFECTS:
        if defect.gate not in ("overlap", "gap", "driver"):
            raise SystemExit(
                "joints.py: %s/%s waives gate %r; the gates are overlap, gap "
                "and driver" % (defect.a, defect.b, defect.gate)
            )
        if not defect.issue.startswith("#"):
            raise SystemExit(
                "joints.py: %s/%s must name an issue like '#192', not %r"
                % (defect.a, defect.b, defect.issue)
            )

    for contact in DRIVER_CONTACTS:
        if contact.kind not in CONTACT_KINDS:
            raise SystemExit(
                "joints.py: %s/%s declares contact kind %r, which is not one of "
                "%s.\n           That vocabulary is closed for the same reason "
                "KINDS is: a fourth\n           word means finding a fourth way a "
                "driver touches a kart."
                % (contact.a, contact.b, contact.kind, ", ".join(sorted(CONTACT_KINDS)))
            )
        if not contact.a.startswith("driver_"):
            raise SystemExit(
                "joints.py: contact %s/%s puts the non-driver part first; `a` is "
                "always\n           the driver side, because gate 3 reads it that "
                "way." % (contact.a, contact.b)
            )
        if contact.b.startswith("driver_"):
            raise SystemExit(
                "joints.py: contact %s/%s is driver-to-driver. Intra-driver pairs "
                "are\n           skipped outright -- spec §60.1.6 -- so declaring "
                "one asserts nothing." % (contact.a, contact.b)
            )
        if not contact.why.strip():
            raise SystemExit(
                "joints.py: contact %s/%s has no why" % (contact.a, contact.b)
            )

    seen: dict[tuple[str, str], Joint] = {}
    for joint in JOINTS:
        key = tuple(sorted((joint.a, joint.b)))
        if key in seen:
            raise SystemExit(
                "joints.py: %s/%s is declared twice. One joint, one entry -- two "
                "entries\n           for one pair means one of the two whys is "
                "wrong." % key
            )
        seen[key] = joint

    contacts_seen: dict[tuple[str, str], Contact] = {}
    for contact in DRIVER_CONTACTS:
        contact_key = (contact.a, contact.b)
        if contact_key in contacts_seen:
            raise SystemExit(
                "joints.py: contact %s/%s is declared twice. One contact, one "
                "entry." % contact_key
            )
        contacts_seen[contact_key] = contact


_check_vocabulary()


def _matches(pattern: str, names: list[str]) -> list[str]:
    return [name for name in names if fnmatch.fnmatchcase(name, pattern)]


def _expand(a: str, b: str, names: list[str]) -> list[tuple[str, str]]:
    """Every concrete unordered pair a glob pair covers, sorted.

    Sorted because both gates print from this and the message order has to be a
    function of the names alone -- `genkart.sh --check` compares two runs.
    """
    left = _matches(a, names)
    right = _matches(b, names)
    pairs = {
        (min(x, y), max(x, y)) for x in left for y in right if x != y
    }
    return sorted(pairs)


def _expand_or_die(what: str, a: str, b: str, names: list[str]) -> list[tuple[str, str]]:
    for pattern in (a, b):
        if not _matches(pattern, names):
            raise SystemExit(
                "joints.py: %s pattern %r matches no part of this kart.\n"
                "           A pattern that matches nothing is a declaration that "
                "silently stopped\n"
                "           applying -- which is how a renamed part loses its "
                "joint. Fix the\n"
                "           pattern or delete the entry." % (what, pattern)
            )
    pairs = _expand(a, b, names)
    if not pairs:
        raise SystemExit(
            "joints.py: %s %r/%r expands to no pair; both patterns match parts "
            "but never\n           two different ones." % (what, a, b)
        )
    return pairs


def declared(names: list[str]) -> dict[tuple[str, str], Joint]:
    """Every concrete pair the table declares, with the entry that declared it.

    One dictionary serves both gates: gate 1 asks whether a pair is in it, gate 2
    walks all of it. That is the point of the table.
    """
    out: dict[tuple[str, str], Joint] = {}
    for joint in JOINTS:
        for pair in _expand_or_die("joint", joint.a, joint.b, names):
            if pair in out:
                first = out[pair]
                raise SystemExit(
                    "joints.py: %s/%s is covered by two entries -- %r/%r and "
                    "%r/%r.\n"
                    "           Checked on the expanded pairs and not just on "
                    "the patterns,\n"
                    "           because two globs can quietly grow into each "
                    "other. One of the\n"
                    "           two whys is wrong; narrow a pattern or delete an "
                    "entry."
                    % (pair[0], pair[1], first.a, first.b, joint.a, joint.b)
                )
            out[pair] = joint
    return out


def contacts(names: list[str]) -> dict[tuple[str, str], Contact]:
    """Every concrete pair `DRIVER_CONTACTS` declares, with its entry.

    Gate 3's analogue of `declared()`: it asks whether a driver/kart overlap is
    permitted and walks every pair to require the contact. Call it only when the
    driver is built -- on a `--driver=false` kart every pattern here matches
    nothing, which `_expand_or_die` treats as the declaration rot it usually is.
    """
    out: dict[tuple[str, str], Contact] = {}
    for contact in DRIVER_CONTACTS:
        for pair in _expand_or_die("contact", contact.a, contact.b, names):
            if pair in out:
                first = out[pair]
                raise SystemExit(
                    "joints.py: contact %s/%s is covered by two entries -- "
                    "%r/%r and %r/%r. One of the two whys is wrong."
                    % (pair[0], pair[1], first.a, first.b, contact.a, contact.b)
                )
            out[pair] = contact
    return out


def waived(gate: str, names: list[str]) -> dict[tuple[str, str], Defect]:
    """Every concrete pair `OPEN_DEFECTS` downgrades for one gate."""
    out: dict[tuple[str, str], Defect] = {}
    for defect in OPEN_DEFECTS:
        if defect.gate != gate:
            continue
        for pair in _expand_or_die("waiver", defect.a, defect.b, names):
            out[pair] = defect
    return out


def waives_part(name: str) -> Defect | None:
    """The gap waiver naming `name`, if there is one.

    Gate 2's weak form reports a *part* rather than a pair -- "nothing on this
    kart is within 2 mm of this" -- so it cannot look a pair up. A part named on
    either side of any gap waiver is a part with a known attachment fault, and
    the weak finding is the same fault seen from the other end.
    """
    for defect in OPEN_DEFECTS:
        if defect.gate != "gap":
            continue
        if fnmatch.fnmatchcase(name, defect.a) or fnmatch.fnmatchcase(name, defect.b):
            return defect
    return None


def stale_waivers(
    gate: str, names: list[str], failing: set[tuple[str, str]]
) -> list[Defect]:
    """Waivers for `gate` that no longer cover a single failing pair.

    A glob waiver stays valid while *any* pair it covers still fails, because a
    detail level can move a marginal pair in and out of overlap on its own. The
    gate prints how many it is covering, so a waiver that is quietly shrinking is
    visible without being fatal.
    """
    stale: list[Defect] = []
    for defect in OPEN_DEFECTS:
        if defect.gate != gate:
            continue
        pairs = set(_expand_or_die("waiver", defect.a, defect.b, names))
        if not pairs & failing:
            stale.append(defect)
    return stale
