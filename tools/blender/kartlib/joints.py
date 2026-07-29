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

    `gate` is `"overlap"` or `"gap"`; `measured` is intersecting triangle pairs
    for the first and millimeters for the second -- the **worst** figure the entry
    covers, measured at high detail when the waiver was written. The number is
    here so that a waiver whose fault got *worse* is visible in a diff rather
    than being covered by the same one line.
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
    Joint(
        a="steering_column",
        b="tierod_end_?_inner",
        kind="bolted",
        why="**this joint is declared against the wrong part on purpose, and it is "
        "waived.** The inner rod end belongs on `steering_pitman`, which §Cockpit "
        "has not built; the pitman is clamped to the column, so the column is the "
        "part it is attached *through*. The station is not a guess -- OTK's "
        "\"38/50\" designation puts the outer hole 50 mm off the column axis and a "
        "KZ runs the outer hole -- and it is 51.7 mm from the column's own surface, "
        "which is exactly the pitman's reach. Spec §40 closes this",
    ),
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
    Joint(
        a="engine_clutch_bell",
        b="engine_water_pump",
        kind="bolted",
        why="the pump body bolts to the case and its drive spigot reaches into "
        "the bell, which is where a KZ takes the pump drive from",
    ),
    Joint(
        a="engine_clutch_cover",
        b="engine_water_pump",
        kind="bolted",
        why="the pump sits in a recess in the same cover",
    ),
    Joint(
        a="engine_crankcase_upper",
        b="engine_water_pump",
        kind="bolted",
        why="the third face of the same three-way joint",
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
        b="engine_airbox_lid",
        kind="bolted",
        why="the lid closes the box",
    ),
    # --- powertrain.py: exhaust ---------------------------------------------
    Joint(
        a="engine_cylinder",
        b="exhaust_flange",
        kind="bolted",
        why="the flange bolts to the barrel's exhaust port face",
    ),
    Joint(
        a="engine_cylinder_base",
        b="exhaust_flange",
        kind="bolted",
        why="the flange is tall enough to reach the base casting below the port",
    ),
    Joint(
        a="engine_cylinder",
        b="exhaust_chamber",
        kind="seated",
        why="the header's spigot goes **into** the exhaust port, 16 mm deep as "
        "built; a header that only touched the port face would blow gas",
    ),
    Joint(
        a="engine_cylinder_base",
        b="exhaust_chamber",
        kind="seated",
        why="the port is bored through the base casting as well as the barrel, "
        "so the same spigot is inside both",
    ),
    Joint(
        a="exhaust_chamber",
        b="exhaust_flange",
        kind="seated",
        why="the flange is a loose ring around the header pipe -- that is why "
        "it needs springs rather than being welded on",
    ),
    Joint(
        a="exhaust_chamber",
        b="exhaust_spring_?",
        kind="clamped",
        why="two springs pull the chamber onto the flange. The hooks wrap the "
        "pipe's lugs, so hook and lug share volume",
    ),
    Joint(
        a="exhaust_flange",
        b="exhaust_spring_?",
        kind="clamped",
        why="the other hook of each spring, over the flange's studs",
    ),
    Joint(
        a="exhaust_flange",
        b="exhaust_flange_nut_?",
        kind="threaded",
        why="two nuts on those studs",
    ),
    Joint(
        a="exhaust_flange_nut_0",
        b="exhaust_spring_0",
        kind="clamped",
        why="the spring hooks over the stud under the nut, so it wraps the nut's "
        "hex. Written per side because spring 0 does not reach nut 1",
    ),
    Joint(
        a="exhaust_flange_nut_1",
        b="exhaust_spring_1",
        kind="clamped",
        why="as spring 0",
    ),
    Joint(
        a="exhaust_chamber",
        b="exhaust_silencer",
        kind="seated",
        why="the silencer slips over the chamber's stinger",
    ),
    Joint(
        a="exhaust_hanger",
        b="exhaust_silencer",
        kind="clamped",
        why="the hanger's strap wraps the silencer body",
    ),
    Joint(
        a="exhaust_hanger",
        b="chassis_side_bar_r",
        kind="bolted",
        why="the hanger's other end bolts to the right side bar. It is the only "
        "thing holding the back of the exhaust up, so a gap here is a "
        "silencer suspended in air",
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
        a="engine_water_outlet",
        b="radiator_hose_upper",
        kind="routed",
        why="the other end of the top hose, on the head's outlet elbow",
    ),
    Joint(
        a="engine_water_pump",
        b="radiator_hose_lower",
        kind="routed",
        why="the other end of the bottom hose, on the pump's inlet",
    ),
    Joint(
        a="radiator_bracket_lower",
        b="radiator_end_inboard",
        kind="bolted",
        why="both brackets pick up on the core's inboard end channel. "
        "BRACKET_*_LOCAL anchors them at 1.15 of the core's own half-width, "
        "i.e. deliberately *past* its edge -- which was the fix for a bracket "
        "that started inside the fin pack, and left the bracket touching "
        "nothing at all",
    ),
    Joint(
        a="radiator_bracket_upper",
        b="radiator_end_inboard",
        kind="bolted",
        why="as the lower bracket",
    ),
    Joint(
        a="radiator_bracket_*",
        b="seat_shell",
        kind="bolted",
        why="on a KZ the radiator hangs off the seat's wing, not off the frame, "
        "and here it has to: BRACKET_DIAMETER's docstring records that the "
        "exhaust belly fills the whole volume between the radiator's "
        "underside and the main rail. So this pair is the radiator's entire "
        "attachment to the kart",
    ),
    # --- cockpit.py ---------------------------------------------------------
    Joint(
        a="seat_shell",
        b="chassis_seat_strut_*",
        kind="bolted",
        why="the four stays land on the seat's mounting ears -- frame.py calls "
        "them 'diagonal stays from the rails up to the seat's mounting ears'. "
        "The seat is not carried by the floor tray, which is why the weak "
        "test's nearest-neighbor answer for the seat is the wrong part",
    ),
    Joint(
        a="steering_bearing",
        b="steering_column",
        kind="pierced",
        why="the column turns inside the bearing collar; the collar is the part "
        "that does not turn",
    ),
    Joint(
        a="steering_bearing",
        b="chassis_steering_hoop",
        kind="pressed",
        why="the collar is carried by the frame's steering hoop, which exists "
        "for nothing else. This is issue #192's worked example",
    ),
    Joint(
        a="steering_boss",
        b="steering_column",
        kind="clamped",
        why="the wheel's boss clamps the column's top",
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
        b="steering_spokes",
        kind="bolted",
        why="the clutch lever's pivot bolts to the spoke plate",
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
        kind="bolted",
        why="the pedal mounts bolt to the front cross member, which is the tube "
        "at the front axle line carrying the kingpins",
    ),
    Joint(
        a="pedal_brake",
        b="pedal_cross_tube",
        kind="pierced",
        why="the pedal's boss swings on the cross tube",
    ),
    Joint(a="pedal_throttle", b="pedal_cross_tube", kind="pierced", why="as the brake pedal"),
    Joint(a="pedal_brake", b="pedal_brake_pad", kind="bolted", why="the rubber pad on the plate"),
    Joint(a="pedal_throttle", b="pedal_throttle_pad", kind="bolted", why="as the brake pedal"),
    Joint(
        a="shifter_base",
        b="chassis_floor_tray",
        kind="bolted",
        why="the hand shifter's base bolts down to the tray beside the seat. It "
        "survived the tray moving to Art. 4.6's extent by 1 mm: the base's "
        "underside is at z 70 and the pan's top at 69",
    ),
    Joint(
        a="shifter_base",
        b="chassis_tray_edge_r",
        kind="bolted",
        why="and it butts against the pan's edging tube, which runs along the "
        "pan's edge at x 273..286 through the base's own y band. The base is "
        "inboard of the tube and shares its bolts",
    ),
    Joint(
        a="shifter_base",
        b="shifter_lever",
        kind="pierced",
        why="the lever pivots inside the base's fork",
    ),
    Joint(a="shifter_knob", b="shifter_lever", kind="pressed", why="the knob is on the lever"),
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
        a="bodywork_front_panel_stay_?",
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
    # -- gate 1: parts built inside other parts ------------------------------
    Defect(
        a="exhaust_chamber",
        b="engine_crankcase_upper",
        gate="overlap",
        measured=50,
        issue="#192",
        why="the header is 12.9 mm inside the crankcase. The spigot in the "
        "exhaust port is a declared joint; this is 60 mm further down the "
        "pipe and is not",
    ),
    Defect(
        a="drive_chain",
        b="drive_output_shaft",
        gate="overlap",
        measured=112,
        issue="#192",
        why="the chain grazes the output shaft by 1.9 mm. The chain has to "
        "clear the shaft it is driven by; the sprocket's pitch radius is "
        "what should separate them",
    ),
    Defect(
        a="drive_output_shaft",
        b="engine_clutch_bolt_4",
        gate="overlap",
        measured=28,
        issue="#192",
        why="a clutch cover bolt is buried in the output shaft -- the bolt "
        "circle is inside the shaft's radius at that angle",
    ),
    Defect(
        a="radiator_hose_lower",
        b="engine_clutch_cover",
        gate="overlap",
        measured=33,
        issue="#192",
        why="the bottom hose is routed 12.1 mm through the clutch cover on its "
        "way from the pump to a radiator that is now on the other side of "
        "the kart. The hose's engine end was not re-routed when the radiator "
        "moved",
    ),
    Defect(
        a="radiator_hose_lower",
        b="engine_clutch_bolt_1",
        gate="overlap",
        measured=15,
        issue="#192",
        why="the same hose through one of that cover's bolts",
    ),
    Defect(
        a="radiator_hose_lower",
        b="radiator_bracket_lower",
        gate="overlap",
        measured=58,
        issue="#192",
        why="the lower mounting bracket passes through the lower hose. Both are "
        "anchored off the core's inboard end and neither was placed against "
        "the other",
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
        a="chassis_side_bar_upper_l",
        b="radiator_*",
        gate="overlap",
        measured=42,
        issue="#190",
        why="the left upper bar and its rear socket pass through the radiator's "
        "low tank. **This is not solvable by moving the bar**, and the arithmetic "
        "is why it is a waiver: the tank reaches x -497.5 at y -96..-144, and the "
        "front tire's disc blocks x 500 forward of y +385, which leaves 481 mm of "
        "clear rail for an attachment pitch Art. 9.4.2 fixes at 500 +-5. Spec "
        "§30.7 re-places the core (x -240..-490, z 408 top against 480 today) and "
        "moves its brackets onto `chassis_rail_l`; §Powertrain owns it",
    ),
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
        b="exhaust_chamber",
        gate="overlap",
        measured=114,
        issue="#190",
        why="the right upper bar against the built exhaust, which runs *forward* "
        "along the pod at x 290..424, z 47..299 from y -202 all the way to +240. "
        "That is the volume Art. 9.4.2's upper bar has to cross to reach the rail. "
        "Spec §30.6 replaces the whole pipe with the 15-cone form geometry and "
        "§30.4's 25 degree cylinder lean, which re-routes it entirely",
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
    Defect(
        a="chassis_bumper_socket_side_upper_rear_r",
        b="exhaust_chamber",
        gate="overlap",
        measured=64,
        issue="#190",
        why="the same post against the pipe",
    ),
    Defect(
        a="chassis_floor_tray",
        b="pedal_*",
        gate="overlap",
        measured=72,
        issue="#190",
        why="Art. 4.6 puts the floor tray *"
        "from the central strut to the front of "
        "the chassis frame"
        "*, i.e. y +40..+760 -- so it is now under the pedals "
        "instead of under the engine, and both pedal plates and both pads pass "
        "through it. §40.5 saw this coming and says so: a pivot at z +50 is "
        "19..44 mm **below** the pan's top surface, and Art. 4.6 forbids ribs and "
        "wants a single element, so the pan cannot simply be notched. It "
        "respecifies the pedal box at `pedal_z` 228 on a new `chassis_cross_pedal` "
        "at y +610, which puts the pads above the pan. §Cockpit owns it",
    ),
    Defect(
        a="chassis_steering_hoop",
        b="pedal_mount_?",
        gate="overlap",
        measured=64,
        issue="#190",
        why="the lower steering support's arms run level at bore height (z 97) "
        "from x +-170 inboard, because at rail height they would cross Art. 4.6's "
        "edging tube. The pedal mounts are plates on edge at x +-120..+-130 "
        "spanning z 87..175, aimed at a cross member that no longer exists, so the "
        "arms pass through them. Same resolution as the tray/pedal pair: §40.5's "
        "pedal box is 138 mm higher and 50 mm further forward",
    ),
    Defect(
        a="chassis_seat_strut_front_r",
        b="exhaust_chamber",
        gate="overlap",
        measured=60,
        issue="#190",
        why="the right front seat stay roots on the rail at the central strut's "
        "station, per Art. 4.2.3, and the built exhaust belly is 296 mm from the "
        "centerline at that station -- 10 mm outboard of the rail's own "
        "centerline. The channel between the belly and `shifter_base` (x 235..276) "
        "is 20 mm wide and the stay is a Ø20 tube, so there is no path: the stay "
        "either clears the pipe or clears the shifter. It clears the shifter, "
        "because §30.6 moves the pipe and §40 keeps the lever where it is",
    ),
    # -- gate 2: declared joints that do not touch ---------------------------
    Defect(
        a="steering_column",
        b="tierod_end_?_inner",
        gate="gap",
        measured=46.12,
        issue="#190",
        why="**the part these belong on does not exist.** Art. 4.5.3 permits the "
        "rose joints and §Cockpit owns the pitman plate they bolt to; until it is "
        "built the inner rod ends are declared against `steering_column`, which is "
        "what a pitman is clamped to. The 46.12 mm is not slack -- it is the pitman's "
        "reach: OTK's \"38/50\" designation puts the outer tie-rod hole 50 mm off the "
        "column axis and a KZ runs the outer hole, which leaves 50 less the column's "
        "9 mm radius less the rod end's 11 mm. The station is corroborated the other "
        "way round: rod end here to the outer end at (320, 417, 140) measures "
        "**271 mm** against a sourced OTK *\"STEERING TIE-ROD 270 mm\"*, and that "
        "0.3% agreement is the third leg of spec §20.3.1's kingpin derivation. Closes "
        "when §40 builds `steering_pitman`",
    ),
    Defect(
        a="steering_bearing",
        b="chassis_steering_hoop",
        gate="gap",
        measured=37.46,
        issue="#192",
        why="issue #192's headline, and #190 moved the hoop rather than the "
        "column: its bore is now at (0, +477, +97), sourced off "
        "`refs/kart-visual/notes_column.md` and consistent with a Ø20 column at "
        "36 degrees from vertical, while `cockpit.py` still derives the column "
        "from `wheel_angle` = 0.470 rad and puts its lower end at (0, +502, +121). "
        "So the two are 37.5 mm apart and the frame is the one that is right. "
        "§Cockpit owns the column and spec §40.2 moves it onto this bore, which "
        "is also what makes the upper support's apex land on it. Original "
        "finding, for the record: the hoop tops out at z 127.2 mm and the "
        "column's lower bearing bottoms at 140.4 mm -- 13.2 mm of air in the "
        "vertical, 23.1 mm as a true surface gap because the two are not "
        "coaxial. cockpit.COLUMN_LOWER_CLEAR lifts the column off the hoop "
        "on purpose to stop it running through the tube, but build.tube "
        "fillets the hoop's apex and pulls the tube's crown *down* below the "
        "control point that was supposed to meet it, so the clearance is "
        "twice what the module thinks. The column itself is 19.1 mm clear",
    ),
    Defect(
        a="seat_shell",
        b="chassis_seat_strut_*",
        gate="gap",
        measured=17.67,
        issue="#192",
        why="the seat still floats above its own stays, and #190 closed most of "
        "the gap rather than all of it: from 16.6 mm (front) and 78.1 mm (rear) "
        "to contact at the front pair and 17.67 mm at the rear. The rear pair now starts where Art. 9.1.2 says it does "
        "-- on the bearing hanger -- and both pairs end on `frame.SEAT_EAR_*`, "
        "which are constants read off `cockpit.py`'s loft. **That is why the last "
        "10 mm cannot be closed from here:** the shell's outer edge is a sampled "
        "surface and a constant authored in a second module will always miss it "
        "by a few millimeters and never say so. The fix is §Cockpit publishing "
        "four `seat_ear_*` empties off the loft and this module reading them "
        "through `context` -- spec §10.9 calls it the highest-value follow-up in "
        "the section and it is",
    ),
    Defect(
        a="radiator_bracket_*",
        b="radiator_end_inboard",
        gate="gap",
        measured=12.7,
        issue="#192",
        why="both brackets stop 12.7 mm short of the core they carry. "
        "BRACKET_*_LOCAL's 1.15 of the core half-width is 19.9 mm past the "
        "edge and the bracket rod's own radius is 8 mm, which is exactly the "
        "gap. Anchoring outboard of the core was the right fix for the "
        "bracket crossing the fin pack; 1.0 rather than 1.15 is the rest of "
        "it",
    ),
    Defect(
        a="radiator_bracket_*",
        b="seat_shell",
        gate="gap",
        measured=68.2,
        issue="#192",
        why="and the other end of both brackets reaches for a seat wing that "
        "is not there. BRACKET_*_SEAT is a world point authored at x 0.180 "
        "and mirrored by RADIATOR_SIDE, so it tracks the correct side of the "
        "kart but not the seat's actual surface. The radiator is attached to "
        "the kart at neither end",
    ),
    Defect(
        a="exhaust_hanger",
        b="chassis_side_bar_r",
        gate="gap",
        measured=60.47,
        issue="#190",
        why="the exhaust hanger holds the silencer and is bolted to nothing, and "
        "#190 moved the bar it reaches for: Art. 9.4.2 sets the lower bar at "
        "480..520 mm from the axis and it was built at 445, so it went outboard "
        "55 mm and its front bend came back to y +382 to clear the front tire. "
        "The hanger sits at x 363..405, y +289..+303. Spec §30.6 re-routes the "
        "whole exhaust and puts the hanger on `chassis_cross_rear`, which is a "
        "different part on a different tube -- so this closes by being replaced",
    ),
    Defect(
        a="pedal_mount_?",
        b="chassis_cross_front",
        gate="gap",
        measured=104.97,
        issue="#190",
        why="the mounts reach for a straight cross member at the front axle line "
        "and there is not one: spec §10.1 item 3 measures the front of a CRG "
        "chassis as a U-loop plus two stub-axle fixations, so `chassis_cross_front` "
        "now runs y +500..+760 out at x +-110..+-304. `_pedal_mounts` aims its "
        "brackets at `(0, front_axle_y, rail_z + 0.025)`, which is empty air 105 mm "
        "away. The number this bracket has to hit is in spec §10.6 item 3 and it "
        "is exact: the loop's leg centerline passes (+-259, +560, +50), so a mount "
        "plate whose bore straddles x +-259 at z +50 contacts the tube at 0 mm. "
        "§40.5 owns the plate and moves the whole pedal box to a pivot at z +228 "
        "on a new `chassis_cross_pedal`; this waiver closes with it",
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
        if defect.gate not in ("overlap", "gap"):
            raise SystemExit(
                "joints.py: %s/%s waives gate %r; the gates are overlap and gap"
                % (defect.a, defect.b, defect.gate)
            )
        if not defect.issue.startswith("#"):
            raise SystemExit(
                "joints.py: %s/%s must name an issue like '#192', not %r"
                % (defect.a, defect.b, defect.issue)
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
