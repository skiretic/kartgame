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
#: stand their parts off. `bodywork.MOUNT_STANDOFF` is 1.5 mm and says so; the
#: engine mount table is 3 mm clear of the floor tray. 2 mm passes the first and
#: fails the second, which is the correct outcome both times — the standoff is a
#: faceting allowance, the 3 mm is a part attached to nothing.
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
        why="the live axle runs right through both rear hubs and out to the "
        "wheel nuts; on a kart the rear wheels are keyed to it, not to hubs",
    ),
    Joint(
        a="axle_stub_fl",
        b="wheel_fl_rim",
        kind="pierced",
        why="the stub axle passes through the front hub's bearings",
    ),
    Joint(a="axle_stub_fr", b="wheel_fr_rim", kind="pierced", why="as front left"),
    Joint(
        a="axle_rear",
        b="chassis_bearing_hanger_?",
        kind="pierced",
        why="the axle runs through a bearing in each of the three hangers. The "
        "hanger is the frame's part and the axle is the driveline's, which is "
        "the one place those two modules meet",
    ),
    Joint(
        a="axle_rear",
        b="axle_sprocket",
        kind="clamped",
        why="the rear sprocket's carrier clamps around the axle tube, so its "
        "bore is inside the axle's surface",
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
    # --- bodywork.py --------------------------------------------------------
    Joint(
        a="bodywork_nose_fairing",
        b="chassis_nose_hoop_lower",
        kind="bolted",
        why="the fairing's molded pins pick up on the lower nose tier. "
        "bodywork.MOUNT_STANDOFF holds them 1.5 mm off the tube on purpose, "
        "which is inside CONTACT_TOLERANCE and is the reason the tolerance is "
        "not zero",
    ),
    Joint(
        a="bodywork_rear_panel",
        b="chassis_rear_bumper",
        kind="bolted",
        why="the rear protector mounts on the bumper hoop -- and after #190 moved "
        "the hoop 179 mm forward, to y -725 where Art. 9.5.5.1's 400 mm overhang "
        "cap and the panel's own 187 mm depth put it, the two are **in contact for "
        "the first time**: this pair was a waived 5.5 mm gap and the waiver is "
        "deleted. The panel now meets the hoop's front face rather than wrapping "
        "over it, which is a fact about the panel -- spec §50.11 respecifies it at "
        "1390 x 187 x 177 and §Bodywork owns the change",
    ),
    Joint(
        a="bodywork_sidepod_l",
        b="chassis_side_bar_l",
        kind="bolted",
        why="frame.py says it plainly: the side bars are 'what a sidepod bolts "
        "to'. The pod wraps outboard of the bar and picks up on it",
    ),
    Joint(a="bodywork_sidepod_r", b="chassis_side_bar_r", kind="bolted", why="the right-hand pod"),
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
    # -- gate 1: parts built inside other parts ------------------------------
    Defect(
        a="bodywork_sidepod_l",
        b="radiator_*",
        gate="overlap",
        measured=92,
        issue="#192",
        why="the radiator passes through the left sidepod, 13.7 mm deep at the "
        "low tank. bodywork.SIDEPOD_TOP_X's comment reasons about a radiator "
        "'at x = 0.330 with a 45 mm core, so its outer face is at 0.353' and "
        "sets the pod's mouth to 0.360..0.372 to clear it. The comment was not "
        "updated when the radiator moved sides. "
        "NOTE, and this correction is the point: an earlier version of this "
        "entry said the core's outboard face is at 0.385, being radiator_x "
        "0.365 plus a 0.020 half thickness. That is the WRONG AXIS. Thickness "
        "runs along the core's own face normal, which radiator_rake tips "
        "forward, not outboard. The lateral half extent is half of "
        "radiator_width -- 0.125 -- so the outboard face is at 0.490, and a "
        "pod built to clear 0.385 is still 105 mm inside the radiator. "
        "params.radiator_width's own docstring warns about exactly this "
        "confusion, in these words: width is across the kart, height is up the "
        "slant, thickness is through the core along the face's own normal. "
        "docs/KART_SPEC.md section 30 settles the lateral extent as "
        "-0.240..-0.490 so that neither the pod nor this waiver has to "
        "re-derive it and pick the wrong axis again",
    ),
    Defect(
        a="bodywork_sidepod_r",
        b="engine_crankcase_upper",
        gate="overlap",
        measured=44,
        issue="#192",
        why="the crankcase is 26.7 mm inside the right pod, 274 of its "
        "vertices enclosed. CRANKCASE_OUTBOARD_X is 0.398, chosen against "
        "the side bar's inboard surface at 0.420 -- but the pod's top edge "
        "is at 0.372 and its wall is inboard of the bar, and nothing checked "
        "that",
    ),
    Defect(
        a="bodywork_sidepod_r",
        b="engine_ignition_cover",
        gate="overlap",
        measured=76,
        issue="#192",
        why="12.9 mm of the same intrusion, one casting further out",
    ),
    Defect(
        a="bodywork_sidepod_r",
        b="engine_ignition_bolt_4",
        gate="overlap",
        measured=8,
        issue="#192",
        why="1.4 mm of the same intrusion; the outermost cover bolt clips the "
        "pod",
    ),
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
        a="bodywork_nose_fairing",
        b="chassis_cross_front",
        gate="overlap",
        measured=236,
        issue="#190",
        why="the front of the frame is now a loop reaching y +760 (`G2` = 250 +-10 "
        "on the CRG form) and the built fairing spans y +618..+902 with its lower "
        "skin at z 46..51, so the loop passes through that skin. The fairing is "
        "the part that is wrong: spec §50 puts its front face at y +1029 and its "
        "rear at +742, i.e. entirely forward of the loop, and sizes it 1090 mm "
        "wide against Art. 9.5.2's 1000 mm **minimum** -- the built panel is "
        "512 mm wide. §Bodywork owns it",
    ),
    Defect(
        a="bodywork_sidepod_?",
        b="chassis_side_bar_upper_?",
        gate="overlap",
        measured=108,
        issue="#190",
        why="Art. 9.4.2 requires *two* bars per side and the kart had one, so the "
        "upper bar is new -- and its legs have to come inboard to the frame "
        "through the volume the built pod occupies (pod top edge z 232, outer face "
        "480; the bar is at z 175 and x 560). There is no legal height for it that "
        "clears: the article's floor is a 160 mm tube top and the pod spans "
        "z 48..232. Spec §50.4 moves the pod's outer face to Art. 9.5.4's tapered "
        "datum at x 618..664, outboard of both bars, which is where a pod that is "
        "*"
        "securely attached to the side bumpers"
        "* has to be",
    ),
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
