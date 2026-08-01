# 40 — Cockpit

Steering, seat, pedals, controls, gear lever, fuel tank. Section file for
`docs/kart_spec/40-cockpit.md`; conventions, provenance vocabulary and the
part-entry format are `00-front-matter.md`'s and are not restated. Units are
millimeters, origin on the ground at mid-wheelbase, +X kart right, +Y forward,
+Z up.

Every regulation quote below was located in
`refs/frontend/fia_karting_technical_regulations_2026.pdf` with
`pdftotext -layout` and carries its PDF page. **KZ/KZ2 is Group 2** (PDF p. 1,
*"Article 9 Group 2 Regulations — KZ … KZ2"*), which decides which of the
duplicated Group 1 / Group 2 articles applies: fuel tank capacity is **Art. 9.3**,
not Art. 8.3.

The measurement work behind this section is `refs/kart-visual/notes_column.md`
(steering), `notes_controls.md` (levers, pedals, tank) and `notes_radiator.md` §6
(the seat, off Tillett's published size chart). Where this section disagrees with
one of them the disagreement is stated with the arithmetic.

## 40.0 The four things this section settles

1. **The steering column no longer floats, because the welded end is now the
   authored end.** `params.steering_column_base()` derives the column's *fixed*
   lower end from its *free* upper end through a hardcoded 402 mm, so no
   expression in the build mentions the bracket that carries it — which is why
   #192's gate measures `chassis_steering_hoop`/`steering_bearing` at **23.36 mm**
   and `chassis_steering_hoop` itself as touching nothing at all. Inverted here:
   the bore is authored, the column length is a catalog part, the wheel center is
   derived, and the gap is arithmetically impossible.
2. **There are two column supports, not one.** A welded lower bracket at
   z 97 and a separate two-tube upper support carrying a nylon block at z 393.
   The single 22 mm hoop topping out at z 127 is dimensionally the lower bracket;
   the upper support — the most visible piece of hardware in the cockpit, and what
   Art. 9.5.3 requires the front panel's upper part to bolt to — is absent.
3. **The pedals were a rental kart's.** `pedal_z = 0.090` is a foot resting on
   the floor, and `pedal_width`/`pedal_length` describe a 70 x 120 flat plate
   where the part is a Ø18 x 80 transverse round bar on a forged arm.
4. **The kart has no fuel tank and Art. 9.3 requires one.** Its position is
   mandated by Art. 4.7 rather than chosen, and the mandate puts it through the
   steering column's path, which is why the real molding is notched.

## 40.1 Regulation quotes this section is built on

All from the pinned PDF. Quoted once here; part entries cite back.

**Art. 4.4 *Pedals/pedal kits*, PDF p. 9**
> Whatever their position, pedals must never protrude in front of the chassis,
> including the bumper.
> The brake pedal must be placed in front of the master cylinder.
> The accelerator pedal must be equipped with a return spring. A mechanical link
> between the accelerator pedal and the carburettor is mandatory.
> Pedal kits to relocate the driver's feet may only be used if supplied by the
> chassis manufacturer.

**Art. 4.5 *Steering system*, PDF p. 9**
> The steering system consists of a steering wheel, steering wheel hub, steering
> column, steering column bracket and two steering arms connected to the steering
> knuckles. **A spacer may be used between the steering wheel and the hub.**
> Although it is an articulated connection, the steering system must only move in
> one axis when the kart is in motion.

That one sentence about a spacer is what makes the 7 degrees of §40.2 a
regulation-legal part rather than a modeling liberty.

**Art. 4.5.1 *Steering wheel*, PDF p. 9**
> The steering wheel must be made of a continuous rim, not incorporating any
> obtuse angles (180-360 °) in its basic shape. The upper and lower thirds of the
> circumference may be straight or of a different radius to the rest of the wheel.
> Steering wheel rims are manufactured with a metallic structure made of steel or
> aluminium.
> The steering wheel hub must be securely attached to the column with at least one
> M6 screw (minimum grade 8.8) and a self-locking nut.

**Art. 4.5.2 *Steering column*, PDF pp. 9-10**
> The steering column must be mounted to the chassis with a bracket and an
> articulated joint. It must be fixed with a safety clip system for the lower
> bearing restraint nut and/or two collars between the column brackets. The
> steering column must have a minimum diameter of 18.0 mm, a minimum wall
> thickness of 1.8 mm and be made of magnetic steel.

Note the plural — *"between the column brackets"*. The regulation itself says
there are two.

**Art. 4.5.4 *Steering wheel devices*, PDF p. 10**
> No steering wheel device (such as a display or fuel cock) mounted on the
> steering wheel may protrude by more than 20 mm from the plane defined by the
> front of the steering wheel or have sharp edges.

**Art. 4.6 *Floor tray*, PDF p. 10**
> It is mandatory to have a floor tray made of rigid material stretching from the
> central strut to the front of the chassis frame. The floor tray must fit
> completely within the perimeter formed by the main tubes […] without protruding
> beyond the central axis of the tubes seen from the top.
> The floor tray may be perforated, but the holes must not have a diameter of more
> than 10 mm […] In addition, **two holes with a maximum diameter of 35 mm are
> allowed for steering column and/or gear shift lever access.**

**Art. 4.7 *Fuel tank*, PDF p. 10**
> The fuel tank must be securely fixed to the chassis and designed in such a way
> that neither the tank nor the pipes (that must be flexible) present any danger of
> leakage during the competition.
> A quick attachment to the chassis is strongly recommended.
> The fuel tank must in no way be shaped to act as an aerodynamic device.
> It must supply the engine only under normal atmospheric pressure. This means
> that, apart from the fuel pump located between the fuel tank and the carburettor,
> any system (mechanical or not) that may have an influence on the internal
> pressure of the fuel tank is not allowed.
> **It is mandatory to place the fuel tank between the main tubes of the chassis
> frame, ahead of the seat and behind the rotation axis of the front wheels.**

**Art. 4.8 *Seat* / 4.8.1 *Reinforcement plates* / 4.8.2 *Seat stays*, PDF pp. 10-11**
> The driver's seat must be designed to prevent him from moving towards the sides
> when cornering. It may be made of composite material.
> Reinforcement plates are required to support the upper part of the seat. They
> must have a minimum thickness of 1.5 mm, a minimum surface of 13 cm2 and a
> minimum diameter of 40 mm.
> All seat stays must be bolted at each end. If they are not used, these seat
> stays must be removed from the chassis frame and seat.

**Art. 4.2.3 *Chassis auxiliary parts*, PDF p. 8**
> These are the attachments, connections and attachment points welded to the frame
> for the steering, pedals, **seat with four seat supports**, bumpers, radiator(s),
> brakes, intake silencer, engine, exhaust and exhaust silencer.

**Art. 4.2.5 *Chassis components*, PDF p. 8**
> These are parts such as the acceleration and brake pedals, pedal kits, steering
> column holder, anti-roll bar, extra seat stays, radiator(s), holder […]

So the welded tabs are the frame's (4.2.3) and the bolt-on pedal kit, column
holder and seat stays are components (4.2.5). Four seat supports is a number, not
a style choice.

**Art. 4.12.2 *Brake control*, PDF p. 12**
> The brake control, i.e. the link between the pedal and the pump(s), must be
> doubled for safety and always be in conformity with the HF of the chassis it is
> homologated with. If a cable is homologated, it must have a minimum diameter of
> 1.8 mm.

**Art. 5.6.1 *Fuel lines*, PDF p. 16**
> Only one fuel line from the tank to the carburettor/fuel pump is allowed, as well
> as one fuel filter before the fuel pump.

**Art. 9.3 *Fuel tank capacity*, PDF p. 22** (Group 2 = KZ)
> 8 litres minimum.

**Art. 5.3.1 *Radiator*, PDF p. 15**, the one clause that binds the seat:
> They must not interfere with the seat.

That is a regulation **forbidding** a joint. No `Joint` may be declared between
any `radiator_*` part and `seat_shell`, and gate 1 then makes any overlap fatal.
`joints.py` currently declares `radiator_bracket_* <-> seat_shell` as `bolted`,
which this section says must be deleted — see §40.8.

## 40.2 Steering — the chain, authored from the welded end

**The authored numbers.** Three, and everything else follows.

| parameter | value | prov | basis |
| --- | --- | --- | --- |
| `lower_bore` | (0, +477, +97) | `derived` | 22 mm up the column axis from the threaded tip; the middle of the 10 mm journal. The bracket is welded and cannot move, so this is the datum. `notes_column` §3. |
| `column_length` | 490 | `sourced` | Real catalog lengths: OTK "38/50 Steering Column 470/490/510 mm", Birel ART "STEERING COLUMN RACING L490" / L520. 490 is the middle of the senior range. 470 and 510 are the adjustment range a tunable would sweep. |
| `column_rake` | 0.628 rad (36° from vertical) | `derived` | Measured on the column tube in `tonykart_racer401T_product.png`, corrected for that image's 11% anisotropy: 119.3 mm forward per 165.9 mm of rise, atan = 35.7°. ±3°. |

**The derivation, in arithmetic.** Axis unit vector, pointing up and rearward:

    axis = (0, -sin 36°, +cos 36°) = (0, -0.5878, +0.8090)

    journal_offset = 22                       # bore is 22 mm up-axis from the tip
    hub_stack      = 25                       # hub + inclined spacer, along the axis
    wheel_center   = lower_bore + axis * (column_length - journal_offset + hub_stack)
                   = (0, 477, 97) + axis * 493
                   = (0, 477 - 289.8, 97 + 398.8)
                   = (0, +187.2, +495.8)   ->  (0, +187, +496)

which is the wheel center **measured independently** off the side view at
(0, +187, +496). The chain is closed, not merely consistent. Two further points
fall on the same line and both were measured independently:

| point | derived | measured | agreement |
| --- | --- | --- | --- |
| threaded tip, `lower_bore - axis * 22` | (0, +490, +79) | tray-level, Art. 4.6 access hole | qualitative |
| upper support bore, `lower_bore + axis * 366` | (0, +262, +393) | (0, +263, +393) side view | 1 mm |
| hub clamp face, `lower_bore + axis * 468` | (0, +202, +476) | — | — |

**Why the gap cannot come back.** `steering_bearing`'s bore centre *is*
`lower_bore` by construction and the column's journal centre is the same point, so
`steering_bearing`/`steering_column` contact is identity. The only edit that can
open a gap is an edit to `lower_bore`, which is exactly the edit that should move
the column. The old failure was structural: with the base derived from the wheel,
no expression in the build mentioned the bracket.

**Specify against the *filleted* hoop, not the arithmetic one.**
`cockpit.COLUMN_LOWER_CLEAR`'s docstring reasons that `frame.py` puts the hoop's
apex control point at the column base and that the filleted centerline passes
within 5 mm of it, so the column is lifted 26 mm up-axis to stay out of the tube.
The module's arithmetic about an *unfilleted* tube is correct and the conclusion
is still wrong: `build.tube` cuts the apex corner and pulls the crown **below**
the control point that was supposed to meet the column, so the real clearance is
19.1 mm to the column and 23.1-23.36 mm to the bearing. **Consequence for this
spec: no part in §40 may be positioned by measuring from a `build.tube` control
point.** Every steering position here is an absolute coordinate, and the bracket
is specified as a plate with an authored bore — not as a bent tube whose crown is
assumed to pass through a point.

### `steering_column`
**Status:** built — re-derived, and four of its five inputs change
**Attaches to:** `steering_bearing` (pierced — the journal turns in the bush),
`steering_bearing_upper` (pierced), `steering_hub` (clamped)
**Envelope:** Art. 4.5.2 — minimum Ø18.0, minimum wall 1.8, magnetic steel
**Verification:** gate 1, gate 2 (three declared joints), `genkart.sh --check`

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| overall length, tip to top | `column_length` | 490 | `sourced` | catalog, above |
| outer diameter | `column_diameter` | 20.0 | `derived` | 50.0 px shaft against the 25.5 px annotated 10 mm feature in `birelart_kz_steering_column.jpg` = 19.6 mm. Corroborated: every European support block on the market is bored 20 mm. **Art. 4.5.2's 18.0 is a floor and `params.py` is built to the floor.** |
| wall thickness | — | 2.0 | `estimated` | Art. 4.5.2's minimum is 1.8; a 20 mm tube at exactly 1.8 has no margin for the two cross-drillings at the top, so one step up. Only visible at the open top end. |
| rake from vertical | `column_rake` | 0.628 rad | `derived` | above |
| journal diameter | — | 10 | `sourced` | Birel render annotation; the part is sold as "…d.10 L490 HI TECH". |
| journal length | — | 15 | `derived` | 193->225 rotated px at 0.485 mm/px = 15.5. |
| reduced stem, tip to shoulder | — | 30 | `derived` | 163->225 px = 30.1. |
| threaded tip | — | M8 | `derived` | second step profiles 21 px against the 25.5 px journal = 8.2 mm; the 8/10 catalog naming pair settles which is which. |
| journal offset from tip | — | 22 | `derived` | journal spans 14.5-30 mm from the tip; midpoint 22.3. |
| pitman hub, on the column | — | 64 long x Ø30, lower face 69 above the tip | `derived` | dark-mask profile of the render, 305->437 px. Belongs to §Running gear's steering-arm geometry; recorded here because it is a feature of this part. |
| hub fixing holes | — | two transverse, 23 and 41 below the top | `derived` | two dark spots at rotated x 1088/1126; 18 mm apart is the wheel-height adjustment. |

`params.py` changes: delete the local `length = 0.402` in
`steering_column_base()`; delete `wheel_center_y` and `wheel_center_z` as
authored fields and derive them; add `lower_bore`, `column_length`,
`column_rake`, `hub_stack`, `wheel_incline_delta`. `wheel_angle` 0.470 rad (27°)
becomes `column_rake` 0.628 (36°) — 9° of error, and `wheel_center_y` was
133 mm too far forward.

### `steering_bearing`
**Status:** built — repositioned and re-sized; it is a bush in a welded bracket,
not a collar on the tube
**Attaches to:** `steering_column` (pierced), `chassis_steering_bracket` (pressed
— §Chassis's part, see the demand below)
**Envelope:** Art. 4.5.2 — *"a bracket and an articulated joint […] a safety clip
system for the lower bearing restraint nut"*
**Verification:** gate 2; the 23.36 mm figure is the waiver this closes

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| bore centre | `lower_bore` | (0, +477, +97) | `derived` | §40.2 |
| bore diameter | — | 12 | `estimated` | a 10 mm journal in a molded bush; 1 mm of wall is the least that molds. Not sourced anywhere. |
| bush length along axis | — | 15 | `derived` | equal to the journal it captures. |
| outer diameter | — | 24 | `estimated` | bore plus a 6 mm collar; the part is never visible in any photograph in the repo. |
| axial retention | — | M8 nut + safety clip, under the bracket | `sourced` | Art. 4.5.2. Not a clamp and not a pillow block — the column must turn here. |

**Demand on §Chassis.** `chassis_steering_hoop` as built is dimensionally this
lower bracket and is welded to nothing (5.1 mm from `chassis_cross_front`). It must
be replaced by **`chassis_steering_bracket`**: a welded bracket on the front cross
member presenting a bore whose centre is within 2.0 mm of **(0, +477, +97)**, with
its own top face at z ≈ 85 (`estimated`, below the bore by a bush radius; this is
the single least-supported number in `notes_column`, because the bracket is behind
bodywork in every photograph found). Its feet must contact `chassis_cross_front`
within 2.0 mm — y +477 is 48 mm behind the front wheel axis, so it lands on the
frame's front cross tube rather than in the overhang.

**Demand on §Chassis, second.** `chassis_floor_tray` must carry a Ø35 access hole
centred on (0, +477), because the restraint nut and clip are reached from
underneath. Art. 4.6 permits exactly two such holes and this is one of them.

### `steering_bearing_upper`
**Status:** new
**Attaches to:** `steering_column` (pierced), `chassis_column_support_l` and
`chassis_column_support_r` (bolted — §Chassis's, see the demand)
**Envelope:** none. Art. 9.5.3 makes it structure: the front panel's upper part
must be attached to the steering column support with independent bars.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bore centre | (0, +262, +393) | `derived` + measured | 366 mm up-axis from `lower_bore`, i.e. 103 mm below the hub clamp face; measured independently at (0, +263, +393). |
| bore | Ø20.0 | `sourced` | "Tony Kart OTK Nylon Support for Steering Column 20mm"; "Birel 20mm Plastic Steering Shaft Block". |
| length | 31 | `sourced` | Birel ART "NYLON SUPPORT STEERING COLUMN L31". |
| outer size | 40 x 36 x 31 block | `estimated` | sized to a 20 mm bore with two bolt ears; the OTK part is the red anodized block visible in the side view. Two-hole bolt-through and centre-lock clamp styles both exist; take bolt-through. |
| material | nylon | `sourced` | part names, above. A bushing, not a rolling bearing. |

**Demand on §Chassis.** Two new tubes, **`chassis_column_support_l/r`**, Ø16
(`estimated`: the support tube reads 5.5 px perpendicular in the side view after
correcting for its lean, ≈15 mm at that scale, ±20%; 16 is the nearest standard
accessory size and is under the 21 mm threshold at which a homologation form counts
a tube as structural — do not quote it as measured). They converge upward into this
block and their upper ends must contact it within 2.0 mm at **(0, +262, +378)**,
the block's lower face. Lean **39° from vertical with the top forward of the
base** — the opposite way to the column, so the two cross in a narrow V; a support
built parallel to the column is wrong. Traced to (x ±0, y +200, z 243) before it
disappears behind bodywork; the weld point to the frame is not visible in any
image in the repo, so its lower end is §Chassis's `estimated`.

### `steering_hub`
**Status:** new
**Attaches to:** `steering_column` (clamped, one M6 grade 8.8 minimum),
`steering_hub_wedge` (bolted, 6x M6)
**Envelope:** Art. 4.5.1 — *"securely attached to the column with at least one M6
screw (minimum grade 8.8) and a self-locking nut"*
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bore | Ø20.0 | `derived` | the column it clamps. |
| bolt pattern | 6 hole | `sourced` | OTK catalog, "STEER.WHEEL HUB - 6 HOLE". |
| flange diameter | 60 | `estimated` | 6 holes on a Ø46 pitch circle with 7 mm of edge land; consistent with the 401T side view where the hub reads about a fifth of the rim's width. |
| length along the axis | 17.3 | `derived` | `hub_stack` 25 minus the wedge's 7.7 mm mean thickness. |
| material | aluminium | `sourced` | OTK "AL KZ STEERING WHEEL'S HUB". |

### `steering_hub_wedge`
**Status:** new — and it is the part that stops the wheel being built wrong
**Attaches to:** `steering_hub` (bolted), `steering_boss` (bolted)
**Envelope:** Art. 4.5 — *"A spacer may be used between the steering wheel and
the hub."*
**Verification:** gate 1, gate 2; the 7° is checkable as an angle between two
built face normals

**The wheel plane rakes 43° from vertical, seven degrees more than the column's
36°.** The edge-on rim trace in the side view measures 42.9° while the column tube
measures 35.7°, and the difference is hardware rather than measurement error: OTK
sells an **"INCLINED STEERING WHEEL HUB"** and an **"INCLINED SPACER FOR
STEERING"** whose only purpose is to lay the wheel back further than its column.
Build the wheel perpendicular to the column and it reads subtly wrong against
every photograph — and the error is invisible from any angle except a true side
elevation, which is why it survives a turntable.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| included angle | 7.0° = 0.122 rad | `derived` | 42.9° wheel plane minus 35.7° column, both off the same frame with the same scale correction. |
| outside diameter | 60 | `derived` | matches the hub flange it bolts to. |
| thickness, thin edge / thick edge | 4.0 / 11.4 | `derived` | 60 x tan 7° = 7.36 mm of taper across the face; 4.0 mm at the thin edge is the least that carries an M6 through-bolt. |
| mean thickness | 7.7 | `derived` | (4.0 + 11.4)/2. |
| orientation | thick edge **up** in the wheel plane | `derived` | the wheel must lay *back*, so the extra material is at the top. Getting this 180° wrong stands the wheel 7° more upright than the column and is the same magnitude of error with the opposite sign. |

`params.py` field: `wheel_incline_delta = 0.122`, added to `column_rake` to give
the wheel plane's rake. Authoring the wheel's absolute angle a second time is how
a wheel ends up skewed on its own column — `cockpit._column_frame`'s existing
instinct is right, it just needs the delta.

### `steering_rim`, `steering_spokes`, `steering_boss`
**Status:** built — part 7 sculpt (2026-07-31): windowed spoke plate, bolted
boss, grip re-measured thin and flattened; Anthony's sign-off on the board
**Attaches to:** `steering_spokes` <-> `steering_rim` (welded),
`steering_spokes` <-> `steering_boss` (welded), `steering_boss` <->
`steering_hub_wedge` (bolted, 6x M6)
**Envelope:** Art. 4.5.1 (continuous rim, no obtuse angles, metallic structure,
straight upper and lower thirds permitted); Art. 4.5.4 (nothing mounted on it may
protrude >20 mm ahead of its front plane); Art. 9.5.3 via §4 of the front matter
(the front panel sits below the top-of-wheel plane, 50 mm clear)
**Verification:** gate 1, gate 2, `genkart.sh --check`

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outside diameter | `wheel_diameter` | 320 | `sourced` size, `derived` choice | Kart wheels sell at 280/300/320/340. Seen edge-on in the side view the rim's trace is a straight segment of true length D: endpoints scale to 204.8 x 227.1 mm = **306 mm**, which picks 320 out of that list rather than 300 or 340. `wheel_diameter = 0.320` is the one steering number in `params.py` that is already right. |
| padded grip section | `wheel_rim_thickness` | 29 | `derived` | tube/OD ratio ≈ 0.10 on two references: `crg_roadrebel_steering.webp` radial scan through the wheel center, grip runs 47.7/56.5 px on a 510 px span (0.094–0.111, the fat run crosses the stitched seam); `vlr_emerald_2025_full.jpg` near edge-on, ~22 px tube on a ~232 px ring (0.096). 0.10 × 320 = 32 ±3; 29 is the thin edge of the band, picked by eye against the built render. Supersedes 38 (single 21 px mask, ±6) and 0.024 before that — both previous lives were single-source. |
| grip fore-aft squash | `cockpit.WHEEL_GRIP_AXIAL_SCALE` | 0.82 | `derived` | a kart grip is flattened along the wheel axis, not round; 0.82 is what the section photographs read. The 0.90 it shipped at existed only because the old 38 mm tube squashed deeper fouled the gate-3 glove rows; the 29 mm tube clears at 0.82. |
| bare rim tube | — | 20 | `estimated` | never visible under the foam; the usual round tube for a steel or aluminium rim, and consistent with 29 mm padded over ~4.5 mm of foam per side. |
| spokes | — | 3 flat plates, two upper diagonals and one lower, windowed | `derived` | the bare-chassis plan view and the CRG close-up both show a single flat drilled centre plate with three arms, not a cast hub. Matches `WHEEL_SPOKE_ANGLES`. |
| spoke windows | `cockpit.WHEEL_SPOKE_RAIL` 13, `WHEEL_SPOKE_WINDOW` (62, 118) | one cutout per arm over the middle ~55% of its radial run | `estimated` | off `crg_roadrebel_steering.webp` — the arm is wide with material removed as interior cutouts, not tapered to a stick. Built as four overlapping watertight prisms per arm (part 7, closes #199's stated omission) so the winding gate still covers the plate. |
| boss bolts | `cockpit.WHEEL_BOSS_BOLT_*` | 6 hex heads, Ø54 bolt circle, head 9.6 across flats × 4.5 proud | `estimated` | every reference boss carries six heads on a circle just inside the flange edge; the 6-hole count itself is `sourced` (OTK "STEER.WHEEL HUB - 6 HOLE", §40.2 hub row). Phased off the spoke angles so no head lands under an arm root. |
| rim shape | `WHEEL_OUTLINE` | round, height/width 0.975, slight bottom flat | `derived` | both primary references show a **round** rim: `crg_roadrebel_steering.webp` (continuous arc, no dip) and `tonykart_racer401T_p05.jpg` top-down (clean circle; the four red segments are grip pads, not lobes — the two-tone trap that produced the old butterfly outline). Art. 4.5.1's straight-thirds *permission* is not evidence of shape. |
| dish, rim plane ahead of the boss face | `WHEEL_DISH` | 15 | `estimated` | not separable from the hub stack in a side view at 2.9 mm/px. Kart wheels are close to flat; 15 mm clears the hub bolt heads. **`cockpit.WHEEL_DISH = 0.048` is 3.2x this**, and it is subtracted from the wheel centre to find the column's top, so it shortens the column by 33 mm as a side effect. |
| hub stack, along the axis | `hub_stack` | 25 | `derived` | side view: the rim centre sits 16.9 mm rearward and 17.5 mm above the column's top end = 24 mm back along the axis. OTK sells it as a stack — hub, spacer, inclined spacer. |
| top of the wheel, absolute | — | 613 | `derived` | 496 + (320/2) x cos 43° = 496 + 117. **Checks against Art. 9.1.1's 650 mm chassis height without the seat (PDF p. 22): 37 mm of margin.** Also the plane Art. 9.5.3 puts the front panel below, 50 mm clear. |

### `steering_clutch_lever`
**Status:** built — **re-attached**: it clamps the column, not the spoke plate
**Attaches to:** `steering_column` (clamped, two-bolt)
**Envelope:** none. Art. 4.12.2's 1.8 mm cable minimum is a *brake* rule and does
not bind a clutch cable.
**Verification:** gate 1, gate 2 — and the joint change is itself the fix:
`joints.py`'s `steering_clutch_lever <-> steering_spokes (bolted)` must be deleted

Two families are sold. This kart carries the first: **OTK 0113.A0KIT "Forged
clutch lever Kit, KZ"** — a two-bolt clamp around a tube and a large closed D-loop
grip, whose parts list is a support (0113.A1), an extension (0113.A2), a pin
(0113.A3), two bushes (0113.A4 3x4x3 mm, 0113.A5 5x6x4 mm) and a D5 Seeger
(`sourced`, kartshop OTK gear-lever-system category). The alternative is the plain
0113.00 lever mounted by the shifter. Type 1 on the **left**: the right hand is
busy with the gear lever, and OTK's forged kit exists because that is where KZ
drivers put it.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| clamp centre | (0, +234, +432) | `derived` | on the column axis, 55 mm below the hub clamp face: (0,202,476) - 55 x axis. Clears `steering_bearing_upper` (103 mm below the face) by 18.5 mm along the axis after both parts' half-lengths. |
| clamp bore | Ø20.0 | `derived` | the column. |
| D-loop centre | (-108, +228, +438) | `estimated` | the loop extends left into the left hand's fingers with that hand still on the rim. It sits ~70 mm behind the rim plane, which is not a choice: the clamp is 79 mm down the column from the hub face and the column is 7° off the wheel's normal, so any column-clamped lever is behind the rim by about that much. The loop's far end closes to ~55 mm. |
| D-loop size | 95 x 55 outside, 14 section | `estimated` | 0113.A0KIT photo proportions against a clamp sized for a 20 mm column. |
| stroke at the grip | 70 | `sourced(snippet)` | "The clutch lever stroke of the 2020 KZ World Champion is 7 cm, though the setup depends on the size of the driver's hand" — tkart.it via search summary; the page 403s from here and nobody in this project has read it. Treat as `estimated` on the recheck pass. |
| angular travel | 35° | `derived` | 70 mm at the 108 mm grip radius about the clamp = 0.622 rad. |
| cable | 1.5-2 mm inner in a 5 mm outer, down the column then rearward along the right rail with the shift rod | `estimated` | route and sizes both. |

**Handoff to §Powertrain:** the clutch actuating arm is on the engine's
**outboard** face at approximately **(+430, -200, +140)** (`estimated`: the TM
KZ-R1 homologation form's two engine photos show the sprocket on one face and the
clutch pack on the opposite, and the sprocket must face the kart's centerline).
§Powertrain owns that part; this section owns the cable only as far as the
engine's outboard face.

## 40.3 The seat — one parameter was holding two angles, and both were wrong

Sourced better than anything else in the cockpit, because kart seats are sold by
numbered size with a published chart. **Tillett T11 ML** taken as the
representative adult KZ size, from the Tillett/IKD dimension chart: A (internal
width at the hips) 32.5, B (internal width across the top of the back) 36.0,
C (external front-lip-to-back-top straight line) 46.0, D (front lip height above
the base plane) 10.0, E (back top height above the base plane) 33.5 cm.

### The derivation, and it closes on a number nobody fed it

Authored: base plane 32, D 100, E 335, shell 4, rake 22°, and the sourced
axle-to-back gap of 135.

    rear axle centre y -525, axle OD 50 -> front face y -500
    Tillett KZ "axle to driver's back" = 135 mm  ->  rearmost point y = -365
    the rearmost point of a reclined shell is the TOP of the back, not its base
    back rise above the base plane = E = 335, rake 22° from vertical
      horizontal run = 335 x tan 22° = 135.3
      hip (base of the back)  y = -365 + 135 = -230
    front lip: z = 32 + D 100 = 132
      pan length, hip to lip  = 260   (authored below as SEAT_PAN_LENGTH)
      lip y = -230 + 260 = +30
    check against Tillett C, which was NOT used above:
      lip (0, +30, 132) to back top (0, -365, 367)
      sqrt(395² + 235²) = 459.6  vs published C = 460      agreement 0.4 mm

A published dimension the derivation never touched coming back to 0.4 mm is the
reason to believe the rest of it.

### `seat_shell`
**Status:** built — re-dimensioned; five numbers change and one splits in two
**Attaches to:** `seat_bracket_upper_l`, `seat_bracket_upper_r`,
`seat_bracket_lower_l`, `seat_bracket_lower_r` (all bolted)
**Envelope:** Art. 4.8 (composite permitted; must prevent the driver moving
sideways), Art. 4.2.3 (four seat supports). Art. 9.1.1's 650 mm height limit
explicitly excludes the seat.
**Verification:** gate 1, gate 2 — the waiver this closes measures `seat_shell`
at **78.07 mm** from its rear stays and **7.31 mm** from the floor tray, the
latter being a part that should not be underneath it at all

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| base plane above ground | `seat_z` | 32 | `sourced` -> `derived` | Tillett seat positioning: *"5 mm is usually the maximum dimension that you can set the base of the seat below the tubes with a modern chassis"*, and the lowest tube is at `ground_clearance` 35. So 30 (5 mm proud below the tubes) to 35 (flush); take 32. **`seat_z = 0.075` is 43 mm high.** |
| back height above the base plane | `seat_height` | 335 | `sourced` | Tillett T11 ML, dimension E. Adult T11 range 280-335. **`seat_height = 0.290` is 45 mm short.** |
| back top, absolute | — | 367 | `derived` | 32 + 335. `notes_radiator` §6 states 365 for this and its own arithmetic gives 367; 365 is the base-plane-30 case. 2 mm, recorded rather than silently adopted. |
| rearmost point | — | y -365 | `derived from sourced` | 135 mm KZ axle-to-back gap ahead of the axle's front face at -500. |
| hip point | `seat_y` | -230 | `derived` | above. `cockpit._seat_spine` reads `seat_y` as the hip point, which is the right reading. **`seat_y = -0.060` is 170 mm too far forward**, and that single error is most of why the built cockpit does not fit a driver. |
| pan length, hip to front lip | `SEAT_PAN_LENGTH` | 260 | `derived` | 395 - 135.3. Was 300. |
| front lip rise above the base plane | `SEAT_PAN_FRONT_RISE` | 100 | `sourced` | Tillett dimension D. Was 55. |
| front lip, absolute | — | (0, +30, +132) | `derived` | 32 + 100. |
| shell chord rake from vertical | `seat_shell_rake` | 0.384 rad (22° ±5) | `derived` | from C 460 and E 335: total horizontal run sqrt(460² - 335²) = 315; subtract 150-200 mm of flat pan and the back rises 335 over 115-165 of run, atan = 19-26°. |
| width at the hips, internal / external | `seat_width` | 325 / **333** | `sourced` / `derived` | Tillett A; + 2 x 4 mm shell. `seat_width = 0.330` is right at the hips and should not move. |
| width at the shoulders, internal / external | `seat_width_shoulders` | 360 / **368** | `sourced` / `derived` | Tillett B; + 2 x 4 mm shell. |
| shell thickness | `seat_thickness` | 4 | `sourced` material, `estimated` thickness | Art. 4.8 permits composite; 4 mm is what a Tillett fiberglass shell measures. **`seat_thickness = 0.008` is twice it**, and the built shell's 8 mm rim is therefore twice as heavy an edge as the real one. |
| pan top | — | 36 | `derived` | 32 + 4. It is essentially on the floor, which is the point of a kart. |

**One `seat_width` cannot hold two widths.** A real shell is **35 mm wider at the
shoulders than at the hips**, and `cockpit.SEAT_HALF_WIDTH`'s table does the
opposite — it tapers to 0.812 of the hip width at the top, building a shell
268 mm across the shoulders where the part is 368. Fix in two places: add
`seat_width_shoulders`, and re-author the table's top entry from **0.812 to
1.105** (368/333) with the intermediate stations made monotonic. A straight box
reads wrong here and so does a tapered one.

**Rake: one parameter, two angles, and a third module reading it.**

| angle | value | belongs to | note |
| --- | --- | --- | --- |
| shell chord from vertical | 22° | this section, `seat_shell_rake` | the fiberglass's own line |
| driver's torso recline | 40-45° | §Driver | `estimated`. Not the same thing — a kart shell wraps and the spine lies back further than the shell's own chord. |
| radiator core rake | ~40° ±5 from vertical | §Powertrain | measured off photographs by `notes_radiator`, and **not** equal to the seat's chord |

`seat_back_angle = 0.610` rad (35°) currently sits between the first two and does
double duty for both. Worse, `radiator_rake_delta` **adds to it**, and its
docstring asserts that the radiator core sits in the plane a second seat's back
would occupy and that this *is* the number rather than an analogy. A measurement
agent found that false: the core rakes 40° ±5 while the shell's chord is 19-26°.

**So `seat_back_angle` -> `seat_shell_rake = 0.384` and
`radiator_rake_delta` must be replaced by an authored `radiator_rake` in the same
commit.** If the seat's number moves first and the coupling is left in place, the
radiator's rake silently changes from 35° to 22° and nothing in any gate objects —
a two-parameter drift of exactly the kind the coupling was introduced to prevent,
running in the other direction.

**No joint between the seat and any radiator part.** Art. 5.3.1: radiators
*"must not interfere with the seat"* (PDF p. 15). The radiator therefore hangs
off its own mount, not off the shell, and `joints.py`'s
`radiator_bracket_* <-> seat_shell (bolted)` is not a joint this spec permits.
Note that the front matter §5a says the radiator hangs off the seat **stays** while
`notes_radiator` §4 measured every reference as showing a dedicated frame bracket
and cites Art. 4.2.3 (welded radiator attachment points on the frame) and
Art. 4.8.2 (stays are bolted at each end and removed if unused, so a stay is not a
mounting rail). Whichever §Powertrain picks, it is not the shell. This section's
only claim is the prohibition.

### `seat_bracket_upper_l`, `seat_bracket_upper_r`
**Status:** new
**Attaches to:** `seat_shell` (bolted, M8 through a reinforcement plate),
`chassis_seat_strut_rear_l` / `_r` (bolted)
**Envelope:** Art. 4.8.1 — reinforcement plates minimum 1.5 mm thick, minimum
13 cm2, minimum Ø40; Art. 4.8.2 — bolted at each end
**Verification:** gate 2, both ends

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| pad centre, left / right | (±140, -338, +300) | `derived` | on the back's outer face: the back runs from (-230, 36) to (-365, 367), so z 300 is at y -337.7. x ±140 is inboard of the shoulder half-width 184 by 44 mm, which is where the visible discs sit in every photograph. |
| reinforcement plate | Ø45 x 1.6 | `sourced` (minima) + `derived` | Art. 4.8.1's Ø40 and 13 cm2 minima: Ø45 gives 15.9 cm2. 1.6 mm is one gauge over the 1.5 minimum. |
| bracket | 22 x 3 flat steel, ~60 long | `estimated` | a strap from the stay's end to the pad. Nothing publishes it. |

### `seat_bracket_lower_l`, `seat_bracket_lower_r`
**Status:** new
**Attaches to:** `seat_shell` (bolted), `chassis_seat_strut_front_l` / `_r`
(bolted)
**Envelope:** Art. 4.2.3 — four seat supports
**Verification:** gate 2, both ends

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| pad centre, left / right | (±150, -215, +70) | `estimated` | on the shell's outboard flank just above the pan at the hip station. Not stated in the regulations beyond "four seat supports"; universal in every photograph — the lower tabs come off the main tubes level with the seat's lower flank. |
| bracket | 22 x 3 flat steel, ~50 long | `estimated` | as the upper pair. |

**Demands on §Chassis, as numbers.** The seat publishes four pads. Each must
receive a stay whose end contacts it within 2.0 mm:

| pad | required point | `frame.py` today | move |
| --- | --- | --- | --- |
| upper l/r | (±140, -338, +300) | `chassis_seat_strut_rear_*` ends (±180, -250, +263) | 103.5 mm |
| lower l/r | (±150, -215, +70) | `chassis_seat_strut_front_*` ends (±178, -120, +280) | 231.6 mm |

Both of `frame.py`'s pairs currently terminate at upper-stay height (z 263-280),
so the kart has two upper stays and no lower brackets — which is why the shell's
nearest neighbor of any kind is the floor tray at 7.31 mm and its rear stays are
78.07 mm away. **And the floor tray should not be under the seat at all:**
Art. 4.6 stretches it *"from the central strut to the front of the chassis frame"*,
and `notes_column` §9 measured the real tray at y **+70 to +720** against
`params.py`'s `tray_front_y = 0.180` with `tray_length = 0.760`, which runs it from
-580 to +180 — under the driver's backside instead of under his feet. With the tray
corrected, the seat drops between the rails at z 32 with nothing beneath it, and
its only contacts are its four brackets. That is the correct answer and it is why
the 7.31 mm reading is a symptom of somebody else's bug.

## 40.4 The gear lever — issue #117, which had never had a reference pulled

### The three answers, one line each

| # | #117 asked | answer | prov |
| --- | --- | --- | --- |
| 1 | outboard offset | pivot **x +320**, knob **x +200** — the lever leans 120 mm *inboard* as it rises | `estimated` |
| 2 | knob height | **z +450** above ground, **414 mm above the seat pan** (pan top z 36) | `derived` |
| 3 | fore-aft | pivot **y +330**, knob **y +300** — the pivot is beside the driver's right **knee**, and the knob sits 20 mm behind the steering wheel's centre plane | `estimated` |

**The reasoning is the part worth keeping.** This is not a stubby thing beside the
driver's hip. Its knob lives ~40 mm off the steering wheel rim
(`sourced(snippet)`: *"Two fingers (approximately 4 cm) is the preferred distance
between the gear lever and the steering wheel"*, tkart.it via two independent
search summaries; the page 403s from here), so the lever is a ~450 mm bent shaft
standing beside the knee. And the pivot is forced forward by a part nobody sells:
**the only two shift rods on the market are 530 mm (OTK 0114.BA "Gear tie-rod,
530 mm") and 495 mm (Righetti Ridolfi / IKP hexagonal), both `sourced`.** A rod of
that length between two ball joints, reaching back to a selector at y ≈ -200, puts
the lever's own pivot at y ≈ +330. A hip-mounted pivot at y ≈ +100 needs a ~300 mm
rod and no catalog sells one. The rod length decides the fore-aft question against
the intuitive answer.

An independent check nobody arranged: the right main rail's centerline at y +330
interpolates to **x 323** in `frame._rail_path`, and the estimated pivot x is
**+320**. The bracket lands on the rail.

### `shifter_base`
**Status:** built — re-specified as a pivot bracket carrying two nylon bushes,
not a plate with a shift gate
**Attaches to:** `chassis_rail_r` (clamped), `shifter_lever` (pierced)
**Envelope:** none
**Verification:** gate 1, gate 2 — and the joint changes:
`shifter_base <-> chassis_floor_tray (bolted)` becomes
`shifter_base <-> chassis_rail_r (clamped)`, because the tray's edge is at the
rail's centerline (Art. 4.6) and the bracket is outboard of it

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bush axis | through (+330, +335, +75), direction (0.098, 0.026, 0.995) | `derived` | §40.4's closure, below. |
| bush pair | 2 x Ø13 bore x 20 long, 50 apart along the axis | `estimated` | OTK 0111.002 "nylon bush for gear system" is sold with no dimensions. Two bushes is `sourced` as a shape from the part set. |
| bracket | 40 x 6 steel plate, clamped to the Ø30 rail | `estimated` | the chassis bracket gives **three** discrete lever positions (`sourced(snippet)`), so it is slotted. |
| standoff from the rail | rod axis 7 mm outboard of the rail's outer surface | `derived` | rail centre x 324 at that station, tube radius 15 -> surface 339; rod at 330 with the bracket carrying it clear above the tube's crown at z 63.3, so the rod passes 11.7 mm over it. |

**The shift gate goes.** `cockpit._shifter` builds a slotted plate "which is what
says sequential rather than a stick". A KZ has no gate: the lever rotates about
its own rod's axis in two nylon bushes and the sequential detent is inside the
gearbox. The plate is a car part and it is the one piece of this assembly that was
invented rather than measured.

### `shifter_lever`
**Status:** built — re-specified as a Ø13 rod with a Ø20 hand tube kinked 55°
**Attaches to:** `shifter_base` (pierced), `shifter_knob` (pressed),
`shifter_connector_arm` (clamped, serrated collet)
**Envelope:** none
**Verification:** gate 1, gate 2, `genkart.sh --check`

**Mechanism, from the part photos** (`sourced` as shapes; `sources_controls.txt`
carries the images). OTK sells this as five pieces and their shapes say how it
works: **0111.B0** is one long thin rod, a shoulder, then a thicker tube kinked
away, with the rod's lower end **serrated**; **0111.002** is the nylon bush;
**0111.B0A** is a flat forged arm with a **serrated collet clamp** at one end and
a plain joint hole at the other; **0114.BA** is the 530 mm tie-rod. So the lever
rotates about its own rod's axis, the kink carries the knob off that axis, and the
connector arm below the bracket swings the rod fore-and-aft. The serrated collet
is what lets the arm be set to the sourced 90° against the rod.

**The closure, in arithmetic.** Authored: the rod's lower end R0 = (+330, +335,
+75), the knob N = (+200, +300, +450), the kink 55° ±3 (measured on
`ctl_otk_0111.B0.webp`, atan(150/104) over four samples along the upper axis), and
the photo's own length ratio rod:tube = 341:301 px = 53.12% : 46.88% of the bent
path.

    delta = N - R0 = (-130, -35, +375),  |delta| = 398.4
    |delta|² = P² (0.5312² + 0.4688² + 2 x 0.5312 x 0.4688 x cos 55°)
             = P² x 0.78758
    P = sqrt(158750 / 0.78758) = 448.9        total bent path
      rod  = 0.5312 P = 238.5
      tube = 0.4688 P = 210.4
    rod axis: cos(angle to delta) = (238.5 + 210.4 cos 55°)/398.4 = 0.9016 -> 25.6°
      taking the in-plane branch nearer vertical:
      a = (0.0984, 0.0265, 0.9947)            5.85° from vertical, leaning
                                              outboard and forward
    kink K = R0 + 238.5 a = (+354, +341, +312)
    tube  T = N - K = (-153.5, -41.3, +137.8),  |T| = 210.4  ✓
    angle(a, T) = 54.9°                                        ✓

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| rod length | 238 | `derived` | above. `notes_controls` gives 265 from the same photo ratio scaled by a 500 mm total that assumed a 395 mm exposed length measured from a bracket point 10 mm above the rail; re-anchoring on the rod's lower end gives 449 total and 238/210. Same photo, same ratio, different datum — and the 265/235 split does **not** close on the authored knob and pivot: it puts the knob 443.7 mm from the rod's end where the geometry needs 398.4. |
| hand tube length | 210 | `derived` | above. |
| kink angle | 55° ±3 | `derived` | photo, four samples. Out-of-plane rotation in the product shot biases it low; the ±3 covers it. |
| rod diameter | 13 | `derived` | 16.5 px at the re-derived scale 449/586 px = 0.766 mm/px = 12.6. |
| hand tube diameter | 20 | `derived` | 26.5 px x 0.766 = 20.3. |
| rod axis, from vertical | 5.85° | `derived` | above. Leans outboard and forward, and the kink brings the tube back inboard over the driver's knee — which is why the lever bows out to x +354 at the kink and returns to +200 at the knob. |
| knob radius about the rod axis | 172 | `derived` | 210.4 x sin 55°. |
| throw at the knob, per shift | 88 (80-100) | `derived` | 28 mm of rod travel / 55 mm arm = 0.510 rad = 29.2°, x 172 mm = 88 mm. `notes_controls` says ~100 mm at a 192 mm radius; the radius follows from the tube length, so the 88 is the same estimate carried through the corrected split. |
| rotation per shift | 29° | `derived` | above. A sequential box re-centres on its own detent, so the driver's motion is ~88 mm forward, release, ~88 mm back — total sweep ~176 mm and ~58°. |

**The diameters are the cross-check, not an input.** The 449 mm total came from
ergonomics (knob at the wheel, pivot where a real rod length puts it) with no
reference to the part photo. Feeding that length back through the photo's pixel
ratios yields a 13 mm rod, a 20 mm tube and a 28 x 47 knob — every one a sane real
number for a hand lever, and none of them chosen. A badly wrong anchor would have
produced 8 mm or 30 mm.

### `shifter_knob`
**Status:** built — dimensions confirmed, position moves
**Attaches to:** `shifter_lever` (pressed)
**Envelope:** none
**Verification:** gate 1, gate 2

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| centre | — | (+200, +300, +450) | `estimated` | one two-finger gap outboard of and below the rim's rightmost point (+160, +187, +496): \|Δ\| = 65 mm to that point, ~40 mm to the nearest grip surface. **`SHIFTER_KNOB = (0.262, 0.104, 0.392)` is 200 mm rearward of this** — the built knob is beside the seat's top edge, i.e. beside the hip, which is the placement §40.4 exists to correct. |
| diameter | `SHIFTER_KNOB_RADIUS` | 28 | `derived` | OTK 0112.B0 photo, knob Ø / tube Ø = 1.35. Built radius 0.026 = Ø52, which is nearly twice the part. |
| length | — | 47 | `derived` | same photo, 2.3 x tube Ø. |

### `shifter_connector_arm`
**Status:** new — **one part beyond the brief's authorized list**, recorded as
such. OTK 0111.B0A is a real catalogued piece and the linkage cannot exist
without it: without the arm there is nothing for the rod to push.
**Attaches to:** `shifter_lever` (clamped, serrated collet),
`shift_rod_end_front` (bolted, uniball)
**Envelope:** none
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length, collet centre to joint | 55 | `estimated` | it has to swing 25-30 mm of rod for one gear, and it is set equal to the selector arm so the linkage is 1:1 and the sourced 90° rule is symmetric at both ends. |
| joint centre | (+330, +276, +76) | `derived` | 55 mm rearward of the rod's lower end in the plane perpendicular to the rod axis, set to 90° against the rod by the collet. |
| section | 20 x 6 forged flat | `estimated` | 0111.B0A photo proportions. |
| angle to the rod | 90° | `sourced(snippet)` | tkart.it via two searches: 90° between the lever's connector arm and the return rod for a direct shift. The serrated collet is the hardware that makes an arbitrary angle settable, which is corroboration from the part rather than from the text. |

### `shift_rod`, `shift_rod_end_front`, `shift_rod_end_rear`
**Status:** new
**Attaches to:** `shift_rod` <-> both rod ends (threaded, M8 opposing pitches),
`shift_rod_end_front` <-> `shifter_connector_arm` (bolted),
`shift_rod_end_rear` <-> `engine_selector_arm` (bolted — §Powertrain's part)
**Envelope:** none
**Verification:** gate 1, gate 2. The rod itself touches only its two ends, which
is correct and is why both joints are declared.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length, eye to eye | **495** | `sourced` | Righetti Ridolfi / IKP hexagonal shift rod. The alternative is OTK 0114.BA at 530 mm, also `sourced`. |
| section | 13 across the flats, hexagonal | `sourced` | 13 mm wrench flats, pointkarting. Adjustable with a wrench on the rod itself. |
| ends | two uniball joints, M8, **opposing thread pitches** | `sourced` | the assembly is a turnbuckle and adjusts without disconnecting. The CRG lever is sold "complete with brackets and uniball joint". |
| front joint | (+330, +276, +76) | `derived` | the connector arm's joint. |
| rear joint | **(+215, -205, +95)** | `derived` | placed so the sourced 495 mm closes: sqrt(115² + 481² + 19²) = 494.9. |
| route | rearward along the driver's right, outboard of `seat_shell`, crossing over `chassis_rail_r` at y +88 with 18.4 mm of clearance | `derived` | interpolating the straight run: at y +88 the rod is at x 285, z 83.4 and the rail's crown at that station is z 65. Minimum clearance to `seat_shell` is **31 mm** — the shell's widest external half-width is 184 at the shoulders and the rod never comes inboard of x 215 while at z 76-95, where the shell is narrower still. |

**Handoff to §Powertrain, as a coordinate.** The engine end is not this section's.
§Powertrain must place the gearbox selector arm's rod-end joint within **3.0 mm of
(+215, -205, +95)**. With the selector shaft at (+215, -150, +95) — `estimated` by
`notes_controls` as low and forward on the crankcase's inboard face, where the
inboard face is at x 319 - 115 = 204 and a 55 mm arm clears by ~10 mm — that means
a 55 mm arm pointing rearward. **If §Powertrain moves the shaft, the rod choice
flips rather than the rod being stretched:** the 530 mm OTK part closes on a joint
at y ≈ -240, and any joint that needs a length between 495 and 530 is reachable on
the turnbuckle. A joint that needs less than 495 mm is not buildable from a part
anybody sells, which is the constraint that placed the lever in the first place.

## 40.5 Pedals — a Ø18 round bar on a forged arm, not a plate

**Organ type, bottom pivot, transverse axis, and this is proven from the parts
rather than judged.** OTK **0014.DC** (throttle) is a forged arm with a bushed
pivot eye at the **bottom** and the foot bar at the top; **0014.D3** is the
support plate; **0015.DC / 0015.DCA** ("Brake pedal, Adjustable, KZ, New type")
is the same family, its "adjustable" being a slotted plate part-way up the arm
carrying the pushrod clevis at one of three heights. All `sourced` as shapes.

### `pedal_throttle`, `pedal_brake`
**Status:** built — re-specified as forged arms; every dimension changes
**Attaches to:** `pedal_cross_tube` (pierced), `pedal_throttle_pad` /
`pedal_brake_pad` (welded)
**Envelope:** Art. 4.4 — pedals must never protrude in front of the chassis,
including the bumper. Art. 9.4.1's front overhang minimum of 350 puts the bumper
at y ≥ +875; the foot bar reaches y ≈ +635 at full travel, so **240 mm clear**.
**Verification:** gate 1, gate 2, and the `pedal_*_pivot` interface axes
`cockpit.py` already publishes

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| pivot, throttle / brake | `pedal_pivot` | (+150, +610, +50) / (-150, +610, +50), axis along X | `estimated` | throttle right, brake left (`sourced`, Art. 4.4 names only the two). y from the pedal-to-seat relationship below; z just under the frame's front tube, which is where 0014.D3's plate hangs its eye. |
| separation | `pedal_separation` | **300** | `estimated` | ±150, photogrammetric, issue #201: sole centers on S3's dead-front frame, 413 px, against two anchors — the sourced OTK M7 front panel (295 mm → 320) and the front tire centers (1105 mm → 291). 300 is the middle. Was 0.170 and before that 0.150, both of which put the feet against the steering column. |
| arm length, pivot to bar centre | `pedal_arm_length` | 180 | `estimated` | the part photo's proportions are self-consistent at this scale: the foot bar reads 0.44 of the arm's height, i.e. ~80 mm, which is one boot. At 145 mm the bar would be 64 mm — too narrow for a boot — and at 210 it would be 92. |
| arm rake, rearward from vertical | `pedal_arm_rake` | 0.140 rad (8°) | `estimated` | puts the bar 25 mm behind the pivot so the sole meets it square with the leg raised. |
| **foot bar centre** | derived | (±150, **+585**, **+228**) | `derived` | y = 610 - 180 sin 8° = 585.0; z = 50 + 180 cos 8° = **228.2**. `notes_controls` states 220 for this; 220 - 50 = 170 = 180 cos 19.2°, which contradicts its own 8° rake. 228 is what the note's own inputs give. |
| **`pedal_z`, corrected** | `pedal_z` | 0.228 | `derived` | **was 0.090** — 21 mm above the floor tray, which is a foot resting on the floor and not a pedal. 138 mm of error. |
| arm section | — | 22 x 8 at the pivot boss tapering to 16 x 6 | `estimated` | forged-arm proportions off 0014.DC. |
| brake pushrod clevis, height above the pivot | — | 56 | `derived` | the slotted plate's centre reads 160 px above the pivot bush on a 510 px pivot-to-bar span: 160/510 x 180. Absolute (-85, +602, +105). |
| **brake pedal ratio** | — | **3.2 : 1** | `derived` | 510/160 from the same photo, i.e. 180/56. Consistent with the ~3:1 that kart brake writing quotes. |
| throttle travel at the foot | — | 50 (45-55) | `estimated` | 16° of arm rotation, ~25 mm at a cable eye 90 mm up the arm; a slide carburettor wants ~25 mm. |
| brake travel at the foot | — | 32 | `estimated` | 10° of rotation. |

### `pedal_throttle_pad`, `pedal_brake_pad`
**Status:** built — re-specified. These are not rubber pads on plates; they are
the transverse **foot bars**, and the name is now misleading.
**Attaches to:** `pedal_throttle` / `pedal_brake` (welded)
**Envelope:** none
**Verification:** gate 1, gate 2. `joints.py` calls both pairs `bolted` ("the
rubber pad on the plate"); on the real part the bar is welded to the arm, so the
`kind` changes.

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| foot bar | `pedal_bar_diameter`, `pedal_bar_length` | Ø18 x 80, transverse | `estimated` | 0014.DC/0015.DCA proportions at the 180 mm arm scale; 80 mm is one boot wide. **Replaces `pedal_width = 0.070` and `pedal_length = 0.120`, a flat 70 x 120 plate. A plate is a rental-kart pedal.** |
| knurl / grip | — | cross-hatched over the middle 60 mm | `estimated` | visible as texture in the part photos; below the resolution that gives a dimension. |

### `pedal_cross_tube`, `pedal_mount_l`, `pedal_mount_r`
**Status:** built — re-positioned
**Attaches to:** `pedal_cross_tube` <-> `pedal_mount_?` (pierced),
`pedal_mount_?` <-> `chassis_cross_pedal` (clamped — a new §Chassis part, below)
**Envelope:** Art. 4.2.3 (welded attachment points for the pedals are the frame's)
and Art. 4.2.5 (the pedal kit itself is a component). Art. 4.4: a pedal kit that
relocates the driver's feet may only be used if supplied by the chassis
manufacturer — so the mounts are part of the chassis product, not a bolt-on.
**Verification:** gate 2 — this closes the **5.2 mm** waiver on
`pedal_mount_? <-> chassis_cross_front`

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| cross tube | `PEDAL_TUBE_DIAMETER` | Ø16, x -186…+186 at (y +610, z +50) | `estimated` | the pivot shaft both pedals swing on. Diameter unpublished. Span follows #201's pedals at ±150; 186 rather than more because the steering hoop's dive crosses this y-z plane at x 190. |
| mount plates | `PEDAL_MOUNT_X` | x ±180 | `derived` | outboard of both pedals (±150, #201) and inboard of the tube's ends (±186), so the arms swing free. |
| plate shape | — | bore for the frame tube at the top, pivot eye 25 mm below | `sourced` (shape) | 0014.D3. |

**Demand on §Chassis, and one unresolved conflict.** The pedal mounts currently
bolt to `chassis_cross_front` at y +525 and miss it by 5.2 mm, and they cannot
reach a pivot at y +610 from there. §Chassis must provide
**`chassis_cross_pedal`**: a transverse tube at **y +610, z +75**, Ø ≥ 22,
spanning at least x ±160, as part of the frame's forward structure — the CRG Road
Rebel homologation form publishes a 250 mm front overhang of the main tube ahead
of the front axis, so there is frame out to y +775 for it to belong to. The mounts
then clamp it with the eye 25 mm below at z +50.

**The conflict, stated rather than hidden:** a pivot at z +50 is **19 to 44 mm
below** the floor tray's top surface at that station (tray top z 69 where it bolts
to rails at z 50, higher at the front where the rails rise 25 mm). Art. 4.6 wants
the tray to be a single element stretching to the front of the frame and forbids
ribs, so it cannot simply be notched around two supports without an argument.
Either the tray's front edge stops at y ≤ +575 and the toe area is open frame, or
the pivot rises to z ≈ +100 and the foot bar with it to z ≈ +278. This section
authors z +50 because that is what the part photograph shows — the bore clamps the
frame tube and the eye hangs below it — and flags the tray question as §Chassis's
to settle. It is 25-50 mm and it moves the pedal face, so it is worth one
measurement rather than one opinion.

### Master cylinder — not this section's part, but this section's interface
**Bore: 22 mm, `sourced`, deferred to §Running gear.** `notes_controls` §5 gives
19 mm and marks it `estimated`, and says itself that it is the single number to
re-check before anything depends on it: the only OTK figure found is a "D13 x 8 mm"
BSM piston in a category that also contains a Mini Kid pump, so 13 is very likely
the cadet part; kart master cylinders are published in 19 and 22 mm (Franklin
Kart); OTK lists a pump named "22SRR". §Running gear is sourcing 22 mm from two
homologation forms, and **a homologation form beats a catalog inference**, so this
section adopts 22 and records 19 as superseded rather than carrying both.

What changes with the bore and what does not:

| quantity | 19 mm | 22 mm | note |
| --- | --- | --- | --- |
| pedal ratio | 3.2:1 | 3.2:1 | geometric, unaffected |
| piston travel | 10 mm | 10 mm | 32 mm foot travel / 3.2, unaffected |
| displacement per circuit | 2.84 cm3 | **3.80 cm3** | pi x 11² x 10 = 3801 mm3 |

**Interface demands on §Running gear.** Art. 4.4 (PDF p. 9): *"The brake pedal
must be placed in front of the master cylinder."* The brake pedal's pushrod clevis
is at **(-85, +602, +105)** and the pushrod is **68 mm** (`sourced`, OTK 0119.01
"Push rod, BSM, 68 mm"), so the cylinder's mouth is at **y ≈ +534** and its body
runs rearward from there — entirely behind the pedal, which is the whole content of
the article. The cylinder's axis must pass through the clevis. Art. 4.12.2 (PDF
p. 12) requires the pedal-to-pump link to be **doubled for safety**, with a
homologated cable at ≥1.8 mm; the second link is a pedal-side part that no section
currently owns and it is **required, not optional** — recorded here as an open item
rather than left in prose.

### The pedal-to-seat relationship, which is what makes the cockpit fit

| quantity | value | prov | basis |
| --- | --- | --- | --- |
| seat pan front lip | y +30 | `derived` | §40.3. `notes_controls` assumes y ≈ +90 for this from `seat_y = -0.060`; the corrected seat puts it 60 mm further back, which lengthens every reach below rather than shortening it. |
| lip to foot bar | 555 | `derived` | 585 - 30. |
| **hip point to foot bar** | **836** | `derived` | hip (0, -230, +36) to bar (±150, +585, +228): sqrt(815² + 192²), the y-z planar figure; the lateral is #201's and does not enter it. |
| adjustment available on a real kart | 180 mm in ten holes | `sourced` | IPK/Praga "Driver position set-up": adjustable pedalboard, "18 cm" of foot movement over ten positions. So 836 sits in a band from about 660 to 1010 and is not at either end. |

**This is the number `cockpit.py`'s own docstring says it cannot fix.** Criterion
1 of issue #13 measured hip point to pad face at **618.5 mm**, folding the knee to
89° where "nearly straight" is 850-870, and the module correctly refused to retune
around it because the fault was in `seat_y`, `pedal_y` and `wheel_center_y`.
Correcting the seat and the pedals independently, each to its own source, lands
**836 mm** — 14 to 34 mm short of the target and inside the pedalboard's own
adjustment range. Nobody fitted that. It is what two separately-sourced parts do
when both are put where their sources say.

The same arithmetic on the wheel: hip (0, -230, +36) to the rim's nearest grip
point. Rim centre (0, +187, +496), the rim's lower edge at (0, +115, +429): 348 mm
of reach in y and 393 in z, i.e. 525 mm from the hip, against
`driver_upper_arm` + `driver_forearm` = 550. It fits with 25 mm to spare where the
old geometry put the rim 4 mm past a fully extended arm. §Driver owns the check;
this section owns the two endpoints.

## 40.6 Fuel tank — mandated position, and it is why the molding is notched

The kart has no tank. Art. 9.3 (PDF p. 22) requires **8 litres minimum** for
Group 2, and Art. 4.7 (PDF p. 10) does not merely permit a position, it
**mandates** one: *"It is mandatory to place the fuel tank between the main tubes
of the chassis frame, ahead of the seat and behind the rotation axis of the front
wheels."*

### `fuel_tank`
**Status:** rebuilt at the sculpt wave — ADR-0063, a section loft, not three
boxes wearing a bevel; envelope shrunk twice at Anthony's sign-off
**Attaches to:** `chassis_floor_tray` (seated — it sits on the tray),
`fuel_tank_strap_front` and `_rear` (clamped), `fuel_tank_mount_*` (seated —
the four anchor tabs stand against the flanks and locate the tank),
`fuel_tank_filler` (threaded)
**Envelope:** Art. 4.7 in full — securely fixed, flexible pipes, no
pressurization other than the fuel pump, and **not shaped to act as an
aerodynamic device**. Art. 9.3 — capacity.
**Verification:** gate 1 (neighbors it must clear: `steering_column`,
`seat_shell`, both rails), gate 2 (declared joints), winding gate (one
watertight shell)

| dimension | field | value | prov | basis |
| --- | --- | --- | --- | --- |
| capacity | `tank_capacity` | 8.5 L | `sourced` | OTK **0073.EA** "Fuel tank, KZ, 8.5 Litre"; KG SER.003 and CKR also sell 8.5. Art. 9.3's minimum is 8, so 8.5 is the catalog size that clears it. |
| outer size | `tank_width/depth/height` | 228 W x 240 D x 201 H | `estimated` | 255 x 250 x 230 shrunk ~7% at the first sign-off, then width and height another 5% with the draft deepened to 0.88 at the second — the eye's call against the reference tanks both times. The lost volume is bought back **forward**: depth 235 -> 240 entirely on the front face, the least-visible dimension and the one with 185 mm of slack to its clause. Measured off the built mesh: **9.18 L shell, ~8.4 L inside a 3 mm wall** — ullage is gone, the molding holds the sourced 8.5 L brim-full, and this envelope is the floor: smaller argues with `tank_capacity`. |
| centre | `tank_center` | (0, +220, +169.5) | `derived` | Art. 4.7, three clauses at once: between the main tubes -> x 0; ahead of the seat (lip +30) -> rear face +100, **70 mm clear** (the tight side — depth moves only at the front); behind the front wheel axis (+525) -> front face +340, **185 mm clear**. z from the tray: bottom = tray top 69, so centre = 69 + 100.5 = 169.5 and top = 270. **Every one of the three coordinates is forced by the article, which is why this is `derived` and not `estimated`.** |
| lateral clearance to the rails | — | ≥70 per side | `derived` | the rail centerline interpolates `frame_half_strut` 286 at y +40 to `frame_half_waist` 139 at y +375 (the 297-at-+225 figure a previous revision carried was wrong — the frame waists harder than that). Tightest at the rear strap station y +282: rail at 180, built flank at 110. |
| **steering column relief** | — | notch 22 deep, plateau half-width 20 feathered to \|x\| 55, ramping in over y +300…+335 | `derived` | the column's Ø20 **lower** surface is z(y) = 87 + 1.376 (477 − y), which only reaches the shrunk top plane z 270 at y **344** — past the front face at 340 — so the shell now clears the column by 5+ mm with no notch at all. The notch is kept as the molded relief both references show (*"waisted at the bottom front to clear the steering column and the shins"*, `sourced` as a shape, 0073.EA photo), and it is **real geometry, not a declared joint**, because a joint would permit the interpenetration gate 1 exists to catch. |
| shape | — | superellipse section loft, draft 0.88 (top pulls in 12% against the base), molded sticker recess, waisted at the bottom front | `sourced` (shape) | 0073.EA photo and both reference tanks; construction per ADR-0063, values in `cockpit.TANK_*`. |
| fittings | — | **three** on the top rear: feed, return, vent, Ø8 nipples | `sourced` | *the KZ tank differs from the OK tank by an extra fitting for a return line* — that is the distinguishing feature of this part and it is the reason not to reuse an OK tank. The 0073.EA photo shows two red-collared fittings plus one bare nipple. Modeled as molded bosses on this mesh rather than as three separate parts, which is a mesh-count decision and not a claim about the real part. |

### `fuel_tank_strap_front`, `fuel_tank_strap_rear`
**Status:** rebuilt twice at the sculpt wave — Ø25 tube -> flat webbing ->
dead-vertical drop onto anchor tabs (the rail fan-out was rejected twice)
**Attaches to:** `fuel_tank` (clamped), `fuel_tank_mount_front_?` /
`_rear_?` (clamped — per-strap declarations, because a glob pair is a
cross-product and the gate demanded the front strap touch the rear tabs)
**Envelope:** Art. 4.7 — securely fixed; *"A quick attachment to the chassis is
strongly recommended."*
**Verification:** gate 2, three declared contacts each; drop verticality
measured off the built mesh (2 mm of x spread = the webbing's own thickness)

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| count | two, over the top | `estimated` | the 0073.EA photo shows two molded strap channels; the count follows the channels. |
| positions | y +282 and y +196 | `derived` | over the channels (`TANK_STRAP_Y`), inboard of the tank's faces; the +310/+140 figures a previous revision carried predate the depth shrink. |
| section | 25 x 2 nylon-reinforced strap, cam buckle | `estimated` | a quick attachment per Art. 4.7's recommendation; a bolted steel band would satisfy the article and lose the recommendation. |
| routing | flank-hugging over the top, then **dead vertical** down each flank onto its tab — no run out to the rails | `sourced` (shape) | neither reference shows webbing splayed off the tank's shoulders; the rails sit 60–100 mm outboard at these stations and the old diagonal feet were the rejected fan-out. |

### `fuel_tank_mount_front_l/r`, `fuel_tank_mount_rear_l/r`
**Status:** new at the sculpt wave sign-off (ADR-0063 amendment)
**Attaches to:** `chassis_floor_tray` (bolted — foot flange, an M5 pair
through the pan; Art. 4.6 permits holes to 10 mm, so the pan is drilled and
not slotted), `fuel_tank` (seated — the plates stand against the flank's
widest belt and locate the tank sideways), its strap (clamped)
**Envelope:** Art. 4.7 — the structure the *"quick attachment"* pulls against
**Verification:** gate 2, three declared contacts each

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| form | stamped steel L-tab: 3 plate, 30 wide, top at z 130, foot flange 22 outboard | `estimated` | proportions of a stamped strap bracket; 30 wide carries the 25 webbing with a rim. The plate top sits just above the flank's widest belt (z 90–130 off the built section) so the strap wraps the tab's outer face rather than threading behind it. |
| plate face | at the flank's widest x per station, embedded 0.5 | `derived` | read off each strap station's own section ring at build time, so the tabs follow the envelope wherever the eye moves it next. |

### `fuel_tank_filler`
**Status:** new
**Attaches to:** `fuel_tank` (threaded)
**Envelope:** none
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| centre | (0, +138, +270) | `estimated` | top face, rearward third, on the centerline — reached between the legs past the steering wheel. The 401T side view shows the red cap at the tank's top-rear corner. |
| size | Ø60 x 25 tall | `estimated` | 0073.EA photo proportions. |
| top of the cap | z 295 | `derived` | 270 + 25. Well under Art. 9.1.1's 650. |

### `fuel_line_feed`, `fuel_line_return`
**Status:** new
**Attaches to:** each `routed` into `fuel_tank`'s fittings at one end and handed
off to §Powertrain at the other
**Envelope:** Art. 5.6.1 (PDF p. 16) — *"Only one fuel line from the tank to the
carburettor/fuel pump is allowed, as well as one fuel filter before the fuel
pump."* Art. 4.7 — pipes must be flexible, and the fuel pump is the only
permitted influence on the tank's internal pressure.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| feed lines | **one**, and one filter before the pump | `sourced` | Art. 5.6.1. Two feed lines is a scrutineering failure, so this is a hard count and not a styling choice. The return line is not a feed line and does not count against it. |
| line size | 5 ID / 8 OD | `estimated` | standard translucent fuel hose; not published for this part. |
| route | up out of the top-rear face, rearward and to the right along the right rail under the seat's edge, to the pulse pump and then the carburettor | `estimated` | path only; that the pipes must be flexible is `sourced` (Art. 4.7). |
| handoff to §Powertrain | carburettor inlet ≈ **(+300, -160, +280)**, pulse pump between it and the tank | `estimated` | `notes_controls` §7.1. §Powertrain owns both fittings; these lines end at them. |

## 40.7 The throttle link and the return spring — required by Art. 4.4, deferred

Art. 4.4 is not permissive here: *"The accelerator pedal must be equipped with a
return spring. A mechanical link between the accelerator pedal and the carburettor
is mandatory."* Neither part exists and neither is in this section's authorized
new-part set, so both are recorded with their numbers and left as open items
rather than written into prose and forgotten:

| part | numbers | prov |
| --- | --- | --- |
| throttle return spring | Ø8 coil, free length 72 / 97 / 124 (three offered); anchor boss on the arm ~40 mm above the pivot, spring running rearward to a frame tab | `sourced` (Righetti Ridolfi "Tension Spring for Brake/Throttle Pedal"; OTK 0016.DA accelerator and 0016.DB brake springs are separate parts). The boss is `sourced` from the 0014.DC photo; the tab is `estimated`. |
| throttle cable | 1.5 mm inner in a 5 mm outer, rearward along the right rail, under the seat, up to the carburettor's throttle arm at ≈ (+300, -160, +280) | `sourced` that the link must be mechanical and must exist (Art. 4.4); route and sizes `estimated` |
| second brake link | ≥1.8 mm cable, pedal arm to the cylinder bracket | `sourced` (Art. 4.12.2) |

The mechanical-link clause also has a sim consequence worth writing down once:
there is no fly-by-wire on this kart, so a stuck throttle is a mechanical
possibility and the model may not assume the input can be cut electrically.

## 40.8 `joints.py` — what this section adds, changes and deletes

| pair | kind | action |
| --- | --- | --- |
| `steering_column` <-> `steering_bearing` | pierced | keep; the 23.36 mm waiver closes by construction |
| `steering_bearing` <-> `chassis_steering_bracket` | pressed | replaces `steering_bearing <-> chassis_steering_hoop` |
| `steering_column` <-> `steering_bearing_upper` | pierced | new |
| `steering_bearing_upper` <-> `chassis_column_support_?` | bolted | new |
| `steering_column` <-> `steering_hub` | clamped | replaces `steering_boss <-> steering_column` |
| `steering_hub` <-> `steering_hub_wedge` | bolted | new |
| `steering_hub_wedge` <-> `steering_boss` | bolted | new |
| `steering_clutch_lever` <-> `steering_column` | clamped | **replaces** `steering_clutch_lever <-> steering_spokes` |
| `seat_shell` <-> `seat_bracket_*` | bolted | new, four pairs |
| `seat_bracket_upper_?` <-> `chassis_seat_strut_rear_?` | bolted | replaces `seat_shell <-> chassis_seat_strut_*` |
| `seat_bracket_lower_?` <-> `chassis_seat_strut_front_?` | bolted | as above |
| `radiator_bracket_*` <-> `seat_shell` | bolted | **delete** — Art. 5.3.1 forbids it |
| `shifter_base` <-> `chassis_rail_r` | clamped | **replaces** `shifter_base <-> chassis_floor_tray` |
| `shifter_lever` <-> `shifter_connector_arm` | clamped | new |
| `shifter_connector_arm` <-> `shift_rod_end_front` | bolted | new |
| `shift_rod` <-> `shift_rod_end_?` | threaded | new, two pairs |
| `shift_rod_end_rear` <-> `engine_selector_arm` | bolted | new, and §Powertrain owns the far side |
| `pedal_?_pad` <-> `pedal_?` | welded | was `bolted`; the foot bar is welded to the arm |
| `pedal_mount_?` <-> `chassis_cross_pedal` | clamped | **replaces** `pedal_mount_? <-> chassis_cross_front`; closes the 5.2 mm waiver |
| `fuel_tank` <-> `chassis_floor_tray` | seated | new |
| `fuel_tank` <-> `fuel_tank_strap_?` | clamped | new |
| `fuel_tank_strap_(front\|rear)` <-> `fuel_tank_mount_(front\|rear)_?` | clamped | **replaces** `fuel_tank_strap_? <-> chassis_rail_?` at the sculpt wave — the straps anchor on their own tabs, not the rails (ADR-0063 amendment) |
| `fuel_tank_mount_*` <-> `chassis_floor_tray` | bolted | new at the sculpt wave |
| `fuel_tank` <-> `fuel_tank_mount_*` | seated | new at the sculpt wave — the tabs locate the tank |
| `fuel_tank` <-> `fuel_tank_filler` | threaded | new |
| `fuel_tank` <-> `fuel_line_?` | routed | new |

Waivers this section's numbers close: `steering_bearing`/`chassis_steering_hoop`
23.36 mm, `chassis_steering_hoop`/`chassis_cross_front` 5.1 mm,
`seat_shell`/`chassis_seat_strut_*` 78.07 mm, `pedal_mount_?`/`chassis_cross_front`
5.2 mm. Each of the four is closed by an authored coordinate plus a stated
tolerance, not by a nudge.

## 40.9 `params.py` — the change list

    # steering: author the welded end and the part, derive the free end
    lower_bore          = (0.0, 0.477, 0.097)   # was: not represented at all
    column_length       = 0.490                 # was: local 0.402 in steering_column_base
    column_rake         = 0.628                 # was: wheel_angle 0.470
    column_diameter     = 0.020                 # was: 0.018, the Art. 4.5.2 floor
    hub_stack           = 0.025                 # new
    wheel_incline_delta = 0.122                 # new: the inclined hub is a real part
    upper_bore          = (0.0, 0.262, 0.393)   # new, and derivable at 366 mm up-axis
    # wheel_center_y / wheel_center_z: DELETE as authored; derive to (0, 0.187, 0.496)
    wheel_rim_thickness = 0.029                 # was 0.024, then 0.038; part-7 two-ref re-measure

    # seat
    seat_z              = 0.032                 # was 0.075
    seat_y              = -0.230                # was -0.060
    seat_height         = 0.335                 # was 0.290
    seat_thickness      = 0.004                 # was 0.008
    seat_width          = 0.333                 # hips, external; was 0.330
    seat_width_shoulders= 0.368                 # new
    seat_shell_rake     = 0.384                 # replaces seat_back_angle 0.610
    # and radiator_rake_delta must become an authored radiator_rake IN THE SAME
    # COMMIT, or the seat's change silently moves the radiator from 35 to 22 deg

    # pedals
    pedal_pivot         = (0.085, 0.610, 0.050) # replaces pedal_y 0.560
    pedal_arm_length    = 0.180                 # new
    pedal_arm_rake      = 0.140                 # new
    pedal_z             = 0.228                 # was 0.090, derived not authored
    pedal_separation    = 0.300                 # was 0.170, then #201 measured it
    pedal_bar_diameter  = 0.018                 # replaces pedal_width 0.070
    pedal_bar_length    = 0.080                 # replaces pedal_length 0.120

    # gear lever
    shift_rod_end_lower = (0.330, 0.335, 0.075) # new
    shift_knob_center   = (0.200, 0.300, 0.450) # replaces SHIFTER_KNOB
    shift_kink          = 0.960                 # 55 deg
    shift_rod_length    = 0.495                 # new, sourced

    # fuel tank
    tank_capacity       = 0.0085                # m3, sourced OTK 0073.EA
    tank_size           = (0.255, 0.250, 0.230) # new
    tank_center         = (0.0, 0.225, 0.184)   # new, derived from Art. 4.7

## 40.10 Provenance and part tally

**Parts: 30.** 17 `built` (`steering_column`, `steering_bearing`,
`steering_boss`, `steering_rim`, `steering_spokes`, `steering_clutch_lever`,
`seat_shell`, `shifter_base`, `shifter_lever`, `shifter_knob`, `pedal_throttle`,
`pedal_throttle_pad`, `pedal_brake`, `pedal_brake_pad`, `pedal_cross_tube`,
`pedal_mount_l`, `pedal_mount_r`) and **13 `new`** (`steering_bearing_upper`,
`steering_hub`, `steering_hub_wedge`, `seat_bracket_upper_l/r`,
`seat_bracket_lower_l/r`, `shifter_connector_arm`, `shift_rod`,
`shift_rod_end_front/rear`, `fuel_tank`, `fuel_tank_strap_front/rear`,
`fuel_tank_filler`, `fuel_line_feed/return`). None marked `delete`; of the built
17, **every one** changes at least one dimension and four change their attachment.

**Numbers: 136 tagged rows carrying 138 tags** (a few rows carry two, where a
`sourced` size was picked out of a catalog list by a `derived` measurement):
`sourced` 32, `derived` 65, `estimated` 39, `sourced(snippet)` 2. Three further
`snippet` figures are cited in §40.4's prose rather than in a table — the 40 mm
knob-to-wheel gap, the reach rule and the bracket's three positions — so the
snippet count is **5**. The `snippet` five are all tkart.it via search summary —
the knob-to-wheel gap, the reach rule, the 90° lever-to-rod rule, the bracket's
three positions and the clutch stroke — and the recheck pass treats them as
`estimated`. `estimated` clusters in three places and each has a reason: the two
steering brackets (no manufacturer publishes a support drawing and every
photograph in the repo has the lower bracket behind bodywork), the gear lever's
absolute position on the chassis (published nowhere at all) and the fuel tank's
outer form (sold by capacity, never dimensioned).

Four `estimated` figures become measurements from a single photograph if one turns
up: a shot from the front or from underneath with the floor tray visible closes the
lower bracket's top face, the bracket's own size, the upper support's weld point
and the tray's front edge in one pass.

## 40.11 Things this section believes are wrong in files it does not own

Numeric, reported rather than acted on, and each one is verifiable before it is
believed.

1. **`radiator_rake_delta` is a live trap, not just a mislabel.** It is *added to*
   `seat_back_angle`. Correcting the seat's rake from 0.610 to 0.384 moves the
   radiator core 13° more upright with no gate objecting, because no gate measures
   a rake. The two changes have to land together. `params.py` §radiator.
2. **`cockpit.WHEEL_DISH = 0.048` shortens the steering column.**
   `_steering_column` computes its upper end as `center - axis * WHEEL_DISH`, so a
   dish that is 33 mm too large removes 33 mm of column. The dish estimate is
   15 mm and the hub stack is a separate 25 mm; conflating the two is what makes
   48 look reasonable.
3. **`params.py`'s floor tray is about 650 mm too far back.**
   `tray_front_y = 0.180` with `tray_length = 0.760` runs it from y -580 to +180 —
   under the seat. `notes_column` measured the real tray at **+70 to +720** at
   1.1236 mm/px, and Art. 4.6 agrees in words. `tray_width = 0.560` also
   over-reads; the tray measures ~382 mm across and Art. 4.6 caps it at the main
   tubes' centerline seen from above. This section depends on it twice: the seat's
   7.31 mm "attachment" is to a tray that should not be underneath it, and the
   column's lower nut passes through it.
4. **`cockpit.py`'s shift gate is invented.** `_shifter` builds a slotted plate
   "which is what says sequential rather than a stick". A KZ lever turns in two
   nylon bushes and the detent is inside the gearbox; there is no gate on any of
   the five OTK parts that make up the assembly.
5. **`axle_diameter`'s docstring says "Solid, 50 mm"** and Art. 9.2 (PDF p. 22)
   sets the maximum outside diameter with *"wall thickness according to Article
   4.3"* — a wall thickness clause means a tube. §Running gear's, and the front
   matter already flags it; repeated because `notes_column` used the 50 mm axle as
   a photogrammetric scale reference and a solid-vs-tube error there would move
   the seat's 135 mm axle-to-back gap.
6. **`tube_main = 0.030` has a sourced alternative it is not using.** The CRG Road
   Rebel homologation form publishes 32 mm ±0.5 in section B; `params.py`'s header
   calls 30 mm "KZ typical" with no citation. §Chassis's call, but it moves the
   rail crown that the shifter bracket clamps and the 18.4 mm the shift rod clears
   it by.
7. **`params.py`'s header still carries `Overall length 1830 mm max`** and the
   "where a regulation states a maximum, the maximum is used" framing, which the
   front matter §5 replaced with a derived 1920. It is the worked example of an
   estimate wearing the vocabulary of a limit and it is still in the file.
8. **`notes_controls` cites Art. 8.3 for the fuel tank minimum.** 8.3 is the
   **Group 1** article (PDF p. 20); KZ/KZ2 is Group 2 (PDF p. 1) and the article is
   **9.3** (PDF p. 22). Both say "8 litres minimum", so the number survives and
   only the citation was wrong — which is exactly the failure mode the §7.4/§7.2
   incident recorded, caught this time before it reached five files.
