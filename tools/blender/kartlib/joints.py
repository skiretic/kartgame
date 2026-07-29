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
        b="chassis_seat_strut_*_l",
        kind="welded",
        why="both seat stays start on the rail. Same side only: a strut welded "
        "to the opposite rail would be a mirroring bug and this is where it "
        "would surface",
    ),
    Joint(
        a="chassis_rail_r",
        b="chassis_seat_strut_*_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_rail_l",
        b="chassis_side_bar_l",
        kind="welded",
        why="the side bar leaves the rail just behind the front axle line and "
        "returns to it at the rear; those two tangent points are the weld",
    ),
    Joint(
        a="chassis_rail_r",
        b="chassis_side_bar_r",
        kind="welded",
        why="the right-hand pair of the same joint",
    ),
    Joint(
        a="chassis_nose_hoop_lower",
        b="chassis_nose_hoop_upper",
        kind="welded",
        why="the two nose tiers share their rearmost control point at the front "
        "cross member, so the tubes converge and touch there",
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
    Joint(
        a="chassis_floor_tray",
        b="chassis_cross_rear",
        kind="seated",
        why="the tray runs back past the rear axle and lands on this member on "
        "its way; it is not fastened here but it does bear on it",
    ),
    Joint(
        a="chassis_floor_tray",
        b="chassis_bearing_hanger_?",
        kind="pierced",
        why="the hangers stand up through the tray, which is cut around them. "
        "The tray covers the main rail through the whole engine bay -- see "
        "powertrain._engine_mount, which had to give up its inboard clamp "
        "because of it",
    ),
    Joint(
        a="chassis_floor_tray",
        b="chassis_seat_strut_*",
        kind="pierced",
        why="the four seat stays leave the rails inboard of the tray's edge and "
        "pass up through it",
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
        why="the hand shifter's base bolts down to the tray beside the seat",
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
        why="the rear protector mounts over the bumper hoop the same way",
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
    # -- gate 2: declared joints that do not touch ---------------------------
    Defect(
        a="steering_bearing",
        b="chassis_steering_hoop",
        gate="gap",
        measured=23.1,
        issue="#192",
        why="issue #192's headline. The hoop tops out at z 127.2 mm and the "
        "column's lower bearing bottoms at 140.4 mm -- 13.2 mm of air in the "
        "vertical, 23.1 mm as a true surface gap because the two are not "
        "coaxial. cockpit.COLUMN_LOWER_CLEAR lifts the column off the hoop "
        "on purpose to stop it running through the tube, but build.tube "
        "fillets the hoop's apex and pulls the tube's crown *down* below the "
        "control point that was supposed to meet it, so the clearance is "
        "twice what the module thinks. The column itself is 19.1 mm clear",
    ),
    Defect(
        a="chassis_steering_hoop",
        b="chassis_cross_front",
        gate="gap",
        measured=5.1,
        issue="#192",
        why="and the hoop is not welded to the frame either. Its feet are "
        "authored at x +-0.150 at rail height, where the rails are out at "
        "x 0.30 and the front cross member is 60 mm further forward",
    ),
    Defect(
        a="seat_shell",
        b="chassis_seat_strut_*",
        gate="gap",
        measured=78.1,
        issue="#192",
        why="the seat floats above its own stays: 16.6 mm off the front pair and "
        "78.1 mm off the rear pair, which do not reach it at all. Its nearest "
        "part of any kind is the floor tray at 6.9 mm, which is not what "
        "carries a seat",
    ),
    Defect(
        a="engine_mount_clamp_front",
        b="chassis_rail_r",
        gate="gap",
        measured=12.1,
        issue="#192",
        why="the engine mount clamps nothing: _engine_mount's own docstring "
        "puts the clamps 'about 11 mm outboard of the 30 mm tube's surface'. "
        "So the whole powertrain's only attachment to the chassis is 12 mm "
        "of air, and the mount plate is a further 3 mm clear of the tray on "
        "purpose",
    ),
    Defect(
        a="engine_mount_clamp_rear",
        b="chassis_rail_r",
        gate="gap",
        measured=22.8,
        issue="#192",
        why="the rear clamp of the same mount, further out again because the "
        "rail is still pinching inward at y -0.305",
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
        measured=15.7,
        issue="#192",
        why="the exhaust hanger holds the silencer and is bolted to nothing",
    ),
    Defect(
        a="pedal_mount_?",
        b="chassis_cross_front",
        gate="gap",
        measured=5.2,
        issue="#192",
        why="both pedal mounts hang 5.2 mm off the cross member they bolt to. "
        "The pedal box is therefore held on by the cross tube alone, and the "
        "cross tube is held by the mounts",
    ),
    Defect(
        a="bodywork_rear_panel",
        b="chassis_rear_bumper",
        gate="gap",
        measured=5.5,
        issue="#192",
        why="the rear protector is 5.5 mm off the bumper hoop it mounts to, "
        "where MOUNT_STANDOFF asks for 1.5 mm. The nose fairing gets its "
        "standoff right at 1.5 mm, so this is the rear panel's own pickup "
        "and not the standoff constant",
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
