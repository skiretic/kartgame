# 10. Chassis

The weldment and everything welded or bolted to it. Conventions, provenance tags,
the part-entry format and every regulation quote reused here are in
`00-front-matter.md`; this section cites back to it and quotes only text the front
matter does not already carry.

Two things are settled here before any part entry, because most of the rest hangs
off them: **the footprint**, which stops being one symmetric invented length, and
**the front end**, which is a different shape from the one that is built.

## 10.1 The primary chassis source, and what it actually says

`refs/kart-visual/cik_hf_chassis_crg_road_rebel_04-CH-14.pdf` and
`cik_hf_chassis_gillard_tg16_026-CH-99.pdf` are dimensioned CIK-FIA chassis
homologation forms. Section B of each publishes the frame's own numbers, and the
1:10 plan drawing on page 2 shows where each one is measured. Both were read.

| form field | CRG Road Rebel `04/CH/14` | Gillard TG16 `026-CH-99` |
| --- | --- | --- |
| `A` wheelbase | 1050 ±10 | 1046 ±10 |
| `B` main tube diameters (6 tubes) | 32 ±0.5 (all six) | 30 ±0.5 (all six) |
| `C` bends in tubes >21 mm | 9 | 11 |
| `D` tubes >21 mm | 6 | 6 |
| `E` outer **front** width | 735 ±10 | 730 ±10 |
| `F` outer **rear** width | 650 ±10 | 640 ±10 |
| `G1` rear overhang, main tubes | 210 ±15 | 210 ±15 |
| `G2` front overhang, main tube | 250 ±10 | 275 ±10 |

The CRG plan drawing was measured photogrammetrically, anchored on `A`: the two
extension lines of the `A` dimension are 1118 px apart at 300 dpi, so the scale is
**0.9392 mm/px**. Cross-checks on the same drawing at that scale: `G1` measures
194 mm against a stated 210 ±15, `G2` measures 241 against 250 ±10, `F` measures
628 against 650 ±10, `E` measures 692 against 735 ±10. So the drawing is accurate
to about 6% and **the numbers in section B are the data**; the drawing is used
here only for *where* each is measured and for *ratios along the frame*, which
survive a 6% scale error.

What the drawing settles that no text does:

1. **`E` is measured across the two stub-axle fixations, not across the main
   tubes.** Its extension lines leave the drawing at the stub-axle bosses on the
   front-axle station. The frontmost *tube* on that chassis is only ~230 mm wide.
2. **The frame is widest at the rear, not at the front.** The rails hold a
   constant outer half-width from the rear extremity forward to y −48, neck to a
   **waist** at y +375, and flare out again to the stub-axle node. `frame.py`'s
   docstring item 4 — *"The frame is widest at the front cross member, not at the
   rear. The rails pinch inward through the seat area and stay narrow to the
   back"* — is backwards, and the built kart is backwards with it: front ±462.5,
   rear ±215. The measured CRG is rear ±314 outer, waist ±149 outer.
3. **There is no straight cross member at the front-axle line.** The front is a
   U-shaped loop (tube `B1`) running from one stub-axle node around the front to
   the other, plus the two stub-axle fixations.
4. The rearmost transverse tube is drawn thinner than 21 mm and is not one of the
   six counted main tubes: it is the rear strut that carries the homologation
   marking (`026-CH-99` page 3: *"The marking located on the rear strut must be
   clearly visible at all times"*).

`tube_main` **stays 30**, and its tag improves: Gillard TG16 publishes 30 ±0.5 for
all six main tubes, so 30 is `sourced` rather than `estimated`. CRG runs 32 on the
Road Rebel; both are homologated, 30 is the lighter and more common KZ figure, and
nothing downstream is fitted to the difference.

Frame material is `sourced` and was not previously cited anywhere: Art. 4.1.2
*Chassis frame material*, PDF p. 7 — *"The structural steel or steel alloy used as
chassis frame material must meet ISO 4948 classifications and ISO 4949
designations. Only alloy steels having at least one alloy element with a mass
content of ≤ 5% are allowed. The steel must be able to pass the contact force
test: a control magnet […] must remain stuck to the surface of the chassis frame
tubes."*

## 10.2 The footprint — `nose_y`, `rear_y`, and deleting `length_overall`

`frame.py:_bumpers` derives both ends from one symmetric length:

    nose_y = p.length_overall * 0.5 - p.tube_bumper * 0.5      # = +0.905
    rear_y = -p.length_overall * 0.5 + p.tube_secondary * 0.5  # = -0.904

The two ends are not symmetric and never were. They are four different limits in
three different articles, all measured from the **wheel axis lines** at y +525 and
y −525 (front matter §5), and each end has its own chain.

### Front

Two limits apply to two different parts, and the front matter's §4 says so:
Art. 9.4.1 sets a **minimum 350.0 mm** front overhang on the *bumper*
(PDF p. 23), Art. 9.5.2 sets a **maximum 680 mm** on the *fairing* (PDF p. 24).

    fairing front face      = 525 + 504 = +1029        derived, front matter §5
    fairing rear face       = 1029 - 287 = +742        OTK M4 depth 287, §5b
    front bumper overhang   = 420                      estimated, see below
    nose bar outer surface  = 525 + 420 = +945
    nose_y (tube center)    = 945 - 10 = +935          Ø20 lower bar, Art. 9.4.1

420 mm is `estimated` and carries its reasoning: it clears the 350 minimum by
70 mm, and it puts the bar 84 mm behind the fairing's front face, i.e. 29% of the
way back into the fairing's own 287 mm depth — behind the molded nose radius,
which is where a fairing's clamp bosses are, and forward of its open back. TD
n°2.2 dimensions the mounting kit and is not obtainable, so this is judgment, not
a limit.

Checks, each against its article rather than against a neighbor:

| check | number | article |
| --- | --- | --- |
| bumper overhang ≥ 350 | 420, +70 margin | 9.4.1, p. 23 |
| fairing overhang ≤ 680 | 504, 176 margin | 9.5.2, p. 24 |
| gap, front wheel front edge (+665) to fairing rear face (+742) ≤ 180 | 77 | 9.5.2, p. 24 |

### Rear

    rear frame overhang     = 210                      sourced, G1 both forms
    rear strut outer surf.  = -525 - 210 = -735
    chassis_cross_tail y    = -735 + 11 = -724 -> -713 center at Ø22, see 10.4
    rear protection overhang= 367                      derived, front matter §5
    protection rear face    = -525 - 367 = -892
    protection front face   = -892 + 187 = -705        KG C2 depth 187, §5b
    rear_y (bumper center)  = -735 + 10 = -725         Ø20, outer surface at -735

Checks:

| check | number | article |
| --- | --- | --- |
| protection overhang ≤ 400 | 367, 33 margin | 9.5.5.1, p. 25 |
| gap, rear tire rear edge (−672.5) to protection front face (−705), 15..50 | 32.5 | 9.5.5.1, p. 25 |
| frame rear overhang 210 ±15 | 199 as specified | `G1`, both forms |

The rear closes on itself in a way that is worth naming, because it was not
arranged: a rear bumper hoop at y −725 sits 20 mm inside the rear protection's
187 mm depth, which is exactly where a panel that bolts over a hoop needs it.
That is the 5.91 mm gap in 10.6 fixed by arithmetic rather than by nudging.

### `length_overall`: delete it as an input

**Recommendation: `length_overall` stops being a parameter that anything reads,
and becomes a derived report value.**

    length_overall = (525 + 504) + (525 + 367) = 1921 -> 1920      derived

**A symmetric length cannot be made legal at any value.** This is the argument
that closes the question, and it is arithmetic rather than preference. Run
`frame.py`'s own formula at the new figure:

    rear_y = -1920/2 + 22/2 = -949        tube center
    rear tube outer surface = -960
    rear overhang = 960 - 525 = 435       Art. 9.5.5.1 caps it at 400

That is **35 mm illegal**, and at the same moment the front is spending only
    905 + 10 - 525 = 390 of its 680 mm allowance. The two ends want 504 and 367,
which differ by 137 mm; any symmetric split hands each end the mean, 435.5. So:

| symmetric `length_overall` | front overhang | rear overhang | verdict |
| --- | --- | --- | --- |
| 1830 | 390 | 390 | legal, but the fairing is starved 114 mm and lands on §5's floor |
| 1920 | 435 | 435 | **rear 35 mm over the 400 cap** |
| 1850 | 400 | 400 | rear exactly on the cap, front 104 mm short, no margin either side |

There is no value that gives the front its 504 and keeps the rear under 400,
because 504 + 1050 + 504 = 2058 puts the rear at 504. The parameter is not
mistuned; it is the wrong shape of parameter.

Reasons to delete it, in order:

1. It is a **bodywork** number. Both halves of the 1920 are fairing and rear
   protection depths (front matter §5). `frame.py` reading it to place a frame
   tube is a frame dimension fitted to a bodywork envelope — the exact inversion
   §190 exists to stop.
2. It is **not symmetric**, and a single scalar about the origin cannot express
   +1029 forward and −892 rearward. The old arithmetic split it 905/904, which is
   the *only* split it can produce, and it is wrong at both ends.
3. Nothing needs it. The frame needs `overhang_front_frame` (250, `sourced`) and
   `overhang_rear_frame` (210, `sourced`). The bumpers need
   `overhang_front_bumper` (420, `estimated`). The bodywork needs its own two.
   Issue #21's Godot check wants the *measured* overall length, which is now a
   measurement of the built mesh compared against 1920, not a parameter read
   back to itself.

So: five new parameters replace one, each with a single owner and a single
article, and `length_overall` survives only in the manifest as a computed figure.

## 10.3 The rail path

Centerlines, in mm, right-hand side; mirrored. z is `rail_z` = 50 throughout
(`ground_clearance` 35 + half of `tube_main`), because nothing in the sources puts
the front of a KZ frame at a different height from the rear and the built kart's
25 mm front rise is unsourced.

| station | y | x | prov | basis |
| --- | --- | --- | --- | --- |
| rear end | −720 | 310 | `derived` | `F` = 650 sourced: 650/2 − 15 = 310 |
| constant to | −48 | 310 | `sourced` | CRG plan: outer half-width constant 334 px over y −735…−48 |
| central strut | +40 | 286 | `derived` | 301/314 of the rear outer half × 325 − 15 |
| **waist** | +375 | 139 | `derived` | 149/314 × 325 − 15; the drawing's minimum |
| stub-axle node | +500 | 304 | `derived` | 308/314 × 325 − 15 |

Ratios rather than absolute pixel distances, for the reason in 10.1: the drawing
is 6% off in scale and dead-on in proportion.

Consequences that are not obvious from the table:

* The rails at the engine bay move **outboard 36 to 95 mm per side** (y −165:
  274 → 310; y −305: 264 → 310). That is what fixes the engine mount, 10.6.
* The **kingpin moves inboard 142.5 mm per side**. `_kingpin_x` is
  `front_hub_x - 0.090` = 462.5; the frame puts it at 320. See 10.5.
* The waist at ±139 leaves 248 mm of clear inner gap between the rails' surfaces
  at y +375, which is where the driver's heels sit.

## 10.4 Cross-member layout

| part | y | Ø | half-span | prov |
| --- | --- | --- | --- | --- |
| `chassis_cross_front` (front loop) | +500 … +760 | 30 | see 10.5 | `sourced` `G2` |
| `chassis_cross_mid_front` (**central strut**) | +40 | 30 | 286 | `derived` |
| `chassis_cross_seat` | −417 | 30 | 310 | `sourced` CRG `B6` |
| `chassis_cross_rear` | −525 | 30 | 310 | `estimated` |
| `chassis_cross_tail` (rear strut) | −713 | 22 | 310 | `derived` `G1` |

`chassis_cross_mid_front` is the **central strut** Art. 4.6 names, and that is a
load-bearing identification rather than a label — see 10.7. It moves from y +230
to y +40 (CRG `B2` measures y +36) and from Ø22 to Ø30, because the CRG's own
six-main-tube count includes it.

`chassis_cross_seat` moves from y −60 to y −417 (CRG `B6`). Nothing needs a tube
under the seat: the seat is carried by four stays, Art. 4.2.3.

`chassis_cross_rear` stays at the rear-axle line and is `estimated`: the CRG puts
its rearmost transverse main tube at y −417 and carries its axle brackets on the
rails, so a sixth member at the axle line is this kart's choice. It is kept
because the **center** bearing hanger has nothing else to weld to.

## 10.5 The front end

### `chassis_rail_l` / `chassis_rail_r`

**Status:** built
**Attaches to:** `chassis_cross_*` (welded), `chassis_rear_bumper` (welded),
`chassis_side_bar_?` (welded), `chassis_seat_strut_front_?` (welded),
`chassis_kingpin_boss_?` (welded), `chassis_floor_tray` (bolted),
`chassis_bearing_hanger_l`/`_r` (welded), `chassis_rail_insert_r` (pressed, right only)
**Envelope:** Art. 4.1.2 material only; the frame's outline is not regulated.
**Verification:** gate 1, gate 2, `genkart.sh --check`

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outer diameter | `tube_main` | 30 | `sourced` | Gillard `026-CH-99` §B, 30 ±0.5 ×6 |
| centerline z | `rail_z` | 50 | `derived` | `ground_clearance` 35 + 15 |
| rear half-width | new `frame_half_rear` | 310 | `derived` | `F` 650/2 − 15 |
| waist half-width | new `frame_half_waist` | 139 | `derived` | 10.3 |
| waist station | new `frame_waist_y` | +375 | `sourced` | CRG plan minimum |
| node half-width | new `frame_half_node` | 304 | `derived` | 10.3 |
| rear end | new `overhang_rear_frame` | 210 | `sourced` | `G1`, both forms |
| bend radius | `bend_radius` | 60 | `estimated` | mandrel bend, unchanged |

### `chassis_cross_front`

**Status:** built, **respecified as the front loop**. Not a straight tube.
**Attaches to:** `chassis_rail_?` (welded, at the node), `chassis_steering_hoop`
(welded), `pedal_mount_?` (bolted), `chassis_bumper_socket_front_*` (welded),
`chassis_floor_tray` (bolted)
**Envelope:** Art. 9.4.1 requires the front bumper's attachments to be *welded to
the frame*, so this tube is what they land on; otherwise none.
**Verification:** gate 1, gate 2

Path, right half, Ø30, z +50 throughout, mirrored through x = 0:

    (+304, +500)   the stub-axle node, welded to the rail's front end
    (+270, +600)
    (+110, +760)   frontmost tube center; outer surface at +775
    (   0, +760)   crosses the centerline

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| frontmost tube outer surface | new `overhang_front_frame` | 250 | `sourced` | `G2` = 250 ±10 (CRG); Gillard 275 ±10 |
| front segment half-width | new `frame_half_front` | 110 | `derived` | CRG plan 123 mm outer at the frontmost, −15 |
| outer diameter | `tube_main` | 30 | `sourced` | as the rails |

The name is kept deliberately. `joints.py` matches `chassis_cross_*` against
`chassis_rail_?` and names `chassis_cross_front` in the pedal-mount joint; renaming
it costs two edits there and every reference in five other section files.

### `chassis_kingpin_boss_l` / `_r`

**Status:** new. The kart has kingpins in `wheels.py` and nothing on the frame for
them to pass through, which is why `_kingpin_x` had to invent a rail position.
**Attaches to:** `chassis_rail_?` (welded), `chassis_cross_front` (welded),
`axle_stub_f?` / kingpin (pierced — §Running gear owns the pin)
**Envelope:** Art. 4.2.1 makes the steering knuckle a chassis *main part* and
Art. 4.2.2 (PDF p. 8) allows an articulated connection *"only […] for the steering
knuckle (through the king pin) and the steering"*, so this joint is the one place
the frame is permitted to articulate.
**Verification:** gate 1, gate 2

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| kingpin axis x | `kingpin_x` (was a function) | 320 | `derived` | two independent measurements, below |
| kingpin axis y | — | +525 | `derived` | `front_axle_y`; `A` spans the axis lines |
| boss outer face x | — | 367.5 | `sourced` | `E` 735/2 |
| boss length (z) | new `kingpin_boss_length` | 60 | `estimated` | two lugs plus a plate; nothing publishes it |

**The 320 is the number this section is least sure of, and it matters most.** Two
measurements:

    photogrammetric kingpin flange spacing 639 +-20  ->  319.5
    E/2 minus a Ø40 boss radius, 367.5 - 20          ->  347.5

They disagree by 28 mm. 320 is taken because it measures the pin directly rather
than inferring it from a bracket whose thickness is unknown, and because it lands
16 mm outboard of the rail's own node at 304, which is what the CRG's short
diagonal from node to boss looks like. What would settle it: one front-three-
quarter photograph of a KZ front end with a scale in frame, or any HF form whose
section B happens to dimension the kingpin spacing.

**The front track chain does not close, and the residual is 142.5 mm per side.**

    front hub center  = (track_front 1240 - tire_front_width 135) / 2 = 552.5
    kingpin axis                                                     = 320.0
    kingpin -> wheel centerline                                      = 232.5
    stub_axle_length                                                 =  90.0
    unaccounted                                                      = 142.5

`stub_axle_length`'s docstring calls 90 mm "a representative stub length", which it
is — for the *stub*. The 232.5 mm is stub axle **plus** hub width **plus** rim
offset, and one of three things is wrong: the parameter is measuring the wrong
span, `track_front` = 1240 is too wide (it is `estimated` in front matter §3), or
the kingpin is further out than 320. §Running gear owns all three; this section's
contribution is that the frame puts the kingpin at **x ±320, y +525**, and the
frame's own front width is `sourced` at 735 outer, so the residual cannot be
absorbed by moving the rail.

## 10.6 The five welds

Each is a #192 gate finding. Numbers, not prose; an **Attaches to:** line is a
promise gate 2 measures at 2.0 mm.

### 1. `chassis_steering_hoop` — welded to nothing, 7.55 mm from `chassis_cross_front`

**Status:** built. Recommend renaming to `chassis_steering_support_lower` once
`joints.py` and §Cockpit can be edited in the same commit; kept as-is here so five
section files and two joint entries do not break.
**Attaches to:** `chassis_cross_front` (welded, both feet), `steering_bearing`
(pressed), `chassis_floor_tray` (pierced)
**Envelope:** Art. 9.5.3, PDF p. 24 — the front panel's *"upper part must be
securely attached to the steering column support with one or more independent
bars"*. This tube and the upper support in the next entry are jointly that
support, so both are load-bearing for a regulation.
**Verification:** gate 2 (declared joint, 2.0 mm)

There are **two** steering supports on a real column and the project collapsed
them into one. This is the **lower** one: it carries the column's lower bearing.
Corrected path, Ø16, mirrored:

    (-200, +639, + 50)   foot, on the front loop's left leg
    (-105, +540, + 92)
    (   0, +477, + 97)   bore, carries steering_bearing
    (+105, +540, + 92)
    (+200, +639, + 50)   foot, on the front loop's right leg

The feet land on `chassis_cross_front` because at y +639 the loop's leg centerline
is at x = 304 − (139/260) × 194 = **200** — the loop's own arithmetic, so the weld
is exact rather than nearby. The built feet at x ±150 at rail height are 60 mm
inboard of the loop and 114 mm behind the old cross member, which is the 7.55 mm.

Bore at (0, +477, +97) is `sourced` from `refs/kart-visual/notes_column.md`, and
it is consistent with the column: OD 20.0, rake 36° from vertical, length 490.

### 2. `chassis_steering_support_upper`

**Status:** new
**Attaches to:** `chassis_cross_mid_front` (welded, two feet), `steering_column`
(pierced, through a 20 mm nylon block), `bodywork_front_panel` (bolted — §Bodywork)
**Envelope:** Art. 9.5.3, PDF p. 24, quoted above. The *"one or more independent
bars"* is this part.
**Verification:** gate 2

An inverted V of two Ø16 tubes leaning **forward**, against the column's rearward
36°:

    foot   (±150, + 40, + 65)   on the central strut's top surface
    apex   (   0, +262, +393)   20 mm bore, nylon block

    forward lean = atan((262 - 40) / (393 - 65)) = 34.1 deg
    column lean  = 36 deg rearward                        -> opposed, 70.1 deg included
    apex height  = 393 + block <= 650                     Art. 9.1.1, front matter §3

### 3. `chassis_cross_front` / `pedal_mount_?` — 5.37 mm

The mounts bolt to the front loop's legs, and the loop's arithmetic gives the
pickup exactly:

    pedal_y = +560  ->  x = 304 - (60/260) x 194 = +-259
    pickup point           (±259, +560, + 50)
    tube surface available  x 244..274, z 35..65   (Ø30)

So a mount plate whose bore straddles x ±259 at z +50 contacts the tube at 0 mm
and gate 2 passes with the standoff to spare. §Cockpit owns the plate; this is the
number it has to hit.

Worth citing while it is open: Art. 9.4.1, PDF p. 23 — *"The front bumper must be
independent from the pedal attachment."* The pedal mounts go on the loop, never on
`chassis_nose_hoop_*`, and that is a regulation rather than a preference.

### 4. `bodywork_rear_panel` / `chassis_rear_bumper` — 5.91 mm

Settled by 10.2's arithmetic rather than by a standoff. The bumper's top bar runs
across at **y −725, z +140**, out to x ±310, Ø20, so its rear surface is at
y −735 and its upper surface at z +150. The rear protection's front face is at
y −705 and its 187 mm depth reaches −892, so the hoop sits 20 mm inside the
panel's own volume. `bodywork.MOUNT_STANDOFF` of 1.5 mm off the tube's rear-upper
surface is then reachable at every pin.

z +140 is `estimated` and carries its reasoning: the KG C2 panel is 177 tall with
its lower edge in the 25–60 mm ground-clearance window (Art. 9.5.5.1), so it spans
z 40…217 and its mid-height is 128. 140 is within 12 mm of the panel's own middle,
and it is 155 mm below the rear tire's top at 295, so Art. 9.5.5.1's *"no higher
than the rear wheels"* has margin at the tube as well as at the panel.

### 5. `engine_mount_clamp_front` / `_rear` on `chassis_rail_r` — 12.10 and 22.90 mm

The powertrain's only load path to the chassis. `_engine_mount`'s docstring reads
the rail's path and puts the clamps *"about 11 mm outboard of the 30 mm tube's
surface"* — correct arithmetic against a rail that is in the wrong place.

The rail as respecified is **straight** through the entire engine bay, so there is
one number for both clamps:

    chassis_rail_r centerline, y -48 .. -720:   x = +310, z = +50
    tube surface (Ø30):                          x 295..325, z 35..65
    clamp stations:                              y = -165 (front), y = -305 (rear)

A clamp pair at either station must bracket that tube: inboard plate outboard face
at x ≤ 295, outboard plate inboard face at x ≥ 325, both spanning z 35…65. The
built blocks span x 303…317, which is *inside* the tube — permitted, because the
pair is a declared `clamped` joint, and it contacts at 0 mm. Either resolution
passes; the point is that the rail is now under the clamp instead of 12 to 23 mm
inboard of it.

## 10.7 The floor tray, Art. 4.6

Art. 4.6 *Floor tray*, PDF p. 10, is normative and was not previously cited:

> It is mandatory to have a floor tray made of rigid material stretching from the
> central strut to the front of the chassis frame. The floor tray must fit
> completely within the perimeter formed by the main tubes, i.e. the central
> strut, the longitudinal tubes and the front of the chassis frame, without
> protruding beyond the central axis of the tubes seen from the top. It must be
> made of a single element, and its surfaces must be uniform, solid, rigid,
> impenetrable, smooth, without ribs and of constant thickness. It must be
> laterally edged by a tube or a rim preventing the driver's feet from sliding off
> the floor tray.

### `chassis_floor_tray`

**Status:** built, respecified. It is in the wrong place and the wrong shape.
**Attaches to:** `chassis_rail_?` (bolted), `chassis_cross_mid_front` (bolted),
`chassis_cross_front` (bolted), `chassis_tray_edge_?` (welded),
`chassis_steering_hoop` (pierced)
**Envelope:** Art. 4.6, quoted above.
**Verification:** gate 1, gate 2

    central strut          = chassis_cross_mid_front, y +40
    front of chassis frame = chassis_cross_front's front segment, y +760
    tray extent            = y +40 .. +760

The **central strut is `chassis_cross_mid_front`** because Art. 4.6's own
enumeration — *"the central strut, the longitudinal tubes and the front of the
chassis frame"* — describes a three-sided perimeter, so the strut is the tube that
closes it at the back, and the tray is entirely forward of it. `chassis_cross_seat`
at y −417 and `chassis_cross_rear` at y −525 are behind the driver's hip and would
put a footwell floor under the engine.

Built extent is y **+180 back to −580**: 580 mm of it behind the origin, under the
engine bay and out past the rear axle. `powertrain._engine_mount`'s docstring
already names the consequence — *"it covers the main rail through the whole engine
bay and out past the rear axle. Anything reaching down the rail's inboard side
goes through it"* — and gave up the engine mount's inboard clamp because of it.
Fixing the tray gives that clamp back.

**Width is not a constant.** Art. 4.6 bounds it at *"the central axis of the tubes
seen from the top"*, which is the rail centerline at each y, and the rails are not
parallel. Half-width table, from 10.3 and 10.5:

| y | +40 | +200 | +375 | +500 | +650 | +760 |
| --- | --- | --- | --- | --- | --- | --- |
| tray half-width | 286 | 220 | 139 | 304 | 200 | 110 |

`tray_width` = 560 (±280) is right only at the strut and is 141 mm too wide per
side at the waist, where the tray would hang outboard of the rails' centerlines
and fail the article. `tray_length` = 760 is coincidentally the correct *length*
(760 = 760 − 0, from +40 to +760 is 720) and is 40 mm long as well as 620 mm too
far aft.

An independent photogrammetric measurement puts the real tray at roughly y +70 to
+720 and 382 mm wide. That is corroboration and it lands inside this table: 382 is
2 × 191, which is the perimeter half-width at y ≈ +290. The article is normative;
the photograph agrees with it.

The forward region between the waist and y +760 flares out to ±304 and closes to
±110, so the perimeter is re-entrant and the tray's outline forward of the waist is
`estimated` — a real tray shows a rounded nose there rather than following the
tubes into the corner.

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| thickness | `tray_thickness` | 4 | `estimated` | aluminium pan; Art. 4.6 requires only *constant* thickness |
| rear edge | `tray_rear_y` (new) | +40 | `sourced` | Art. 4.6 + central strut identification |
| front edge | `tray_front_y` | +760 | `sourced` | Art. 4.6 + `G2` |
| half-width | new `tray_half_width` table | above | `derived` | rail centerline at each y |
| bottom z | `tray_bottom_z` | 65 | `derived` | rail top; unchanged, and correct |

`tray_width` and `tray_length` are **deleted**: an hourglass is not a rectangle
and two scalars cannot describe it.

**This is the third parameter in `params.py` found to describe nothing**, and the
pattern is worth stating once because it is a class rather than three incidents:
`frame_height` was read by no geometry at all until a seat strut was pointed at it;
`nose_width` = 680 has been clamped to 512 by `bodywork.NOSE_HALF_WIDTH_LIMIT` for
two milestones, so the parameter block and the mesh have disagreed by 178 mm;
`tray_width` and `tray_length` describe a rectangle that the article forbids. A
parameter that no mesh reads, or that a module silently overrides, is a comment
wearing a number's clothes. Any restructure of the parameter block should carry a
build-time assertion that every field is read by at least one module — the same
shape of check as `joints.py`'s "a pattern that matches nothing is fatal".

**On "impenetrable":** the lower steering support's bore sits at (0, +477, +97),
above a tray whose top is at z 69, so the tray is cut around the support's two
legs — declared `pierced`, which `joints.py` already uses for *"a tray cut around a
strut"*. Read strictly, *"impenetrable"* forbids that. Read as it is scrutineered —
a solid walking surface, not lightened or louvred — a clearance aperture for the
steering column and its support is universal on every kart in the reference set.
This entry takes the second reading and marks it `estimated`; it is the one place
in this section where a regulation word is being interpreted rather than measured.

### `chassis_tray_edge_l` / `_r`

**Status:** new. Art. 4.6's last sentence is mandatory and this part does not
exist; the rails cannot serve, because the rail's top is at z 65 and the tray's top
is at z 69, so the rail stands 4 mm *below* the surface a foot would slide off.
**Attaches to:** `chassis_floor_tray` (welded), `chassis_rail_?` (welded, where the
tray edge runs over the rail)
**Envelope:** Art. 4.6 — *"laterally edged by a tube or a rim preventing the
driver's feet from sliding off the floor tray."*
**Verification:** gate 2

| dimension | `params.py` field | value | prov | basis |
| --- | --- | --- | --- | --- |
| outer diameter | new `tube_tray_edge` | 16 | `estimated` | smallest tube on the reference karts; the article sets no size |
| centerline z | — | 77 | `derived` | `tray_top_z` 69 + half of 16 |
| plan path | — | the tray's own edge, y +40…+760 | `derived` | 10.7's half-width table |

## 10.8 Bumpers

Art. 9.4 (PDF p. 22) makes front and side protections compulsory *"made of
magnetic steel round tubing"*. Art. 9.4.1 and 9.4.2 then dimension them, and this
is the most heavily `sourced` part of the chassis — every figure below is in the
text and none of it was previously in this repo.

Art. 9.4.1 *Front bumper*, PDF pp. 22–23, verbatim, the parts the front matter
does not carry:

> The front bumper consists of two elements: an upper bar with a minimum diameter
> of 16.0 mm and two corner bends with one constant radius. The straight length
> between the bends must be 375.0 mm minimum and 395.0 mm maximum.
> The bar must be fixed to two welded chassis frame attachments, which must be
> 550.0 mm apart and centred on the kart's longitudinal axis.
> Height: 200.0 mm minimum and 250.0 mm maximum from the ground (measured to the
> tubing top).
> A lower bar with a minimum diameter of 20.0 mm and two corner bends with one
> constant radius. The straight length between the bends must be 295.0 mm minimum
> and 315.0 mm maximum.
> The bar must be fixed to two welded chassis frame attachments, which must be
> 450.0 mm apart and centred on the kart's longitudinal axis. The attachments must
> be horizontally and vertically parallel to the kart's axis and allow for a
> 50.0 mm insertion of the bar.
> Height: 70.0 mm minimum and 110.0 mm maximum (measured to the tube top).
> These two elements must be vertically aligned […] Both bars must be connected by
> the front bumper support.
> The front bumper must be independent from the pedal attachment and allow for the
> mounting of the mandatory front fairing.

### `chassis_nose_hoop_lower`

**Status:** built, respecified. Every one of its four dimensions is outside the
article.
**Attaches to:** `chassis_bumper_socket_front_lower_?` (seated, 50 mm insertion),
`chassis_nose_hoop_upper` (welded, through the support),
`chassis_front_bumper_support` (welded), `bodywork_nose_fairing` (bolted)
**Envelope:** Art. 9.4.1, quoted above.
**Verification:** gate 1, gate 2

| dimension | `params.py` field | value | prov | basis | as built |
| --- | --- | --- | --- | --- | --- |
| outer diameter | `tube_bumper` | 20 | `sourced` | ≥20.0 | 20 ✓ |
| straight length between bends | new `nose_lower_straight` | 305 | `derived` | mid of 295…315 | 330, **15 over** |
| centerline z | new `nose_lower_z` | 85 | `derived` | tube top 95, mid of 70…110 | 60, top 70, at the floor |
| frontmost tube center y | `nose_y` | +935 | `derived` | 10.2 | +905 |
| attachment spacing | new `nose_lower_mounts` | 450 | `sourced` | *"450.0 mm apart"* | not modeled |
| bar insertion into socket | — | 50 | `sourced` | *"allow for a 50.0 mm insertion"* | not modeled |

Path, Ø20, one constant corner radius, mirrored, **planar at z +85 throughout**:

    (-225, +606, + 85)   50 mm inside the left socket
    (-152.5, +935, + 85) corner bend, constant radius
    (+152.5, +935, + 85) corner bend
    (+225, +606, + 85)

`x = ±152.5` is half the 305 straight. `x = ±225` is half the 450 attachment
spacing, and the socket's y follows from the loop: 304 − (79/194) × 260 gives
y = **+606** at x 225, so the attachment lands on `chassis_cross_front` without
a bracket. The bar is planar and horizontal because the 70…110 mm window is stated
for *the bar*, not for its front straight, and because the article requires the
attachments to be *"horizontally and vertically parallel to the kart's axis"* — so
the socket is a 35 mm riser above the loop's tube at z +50, not a bend in the bar.

**Do not lift this bar to clear a fairing.** A request came in during this section
to put the nose hoop's tube center at z ≥ 150. At Ø20 that is a tube top of 160,
which is **50 mm above Art. 9.4.1's 110 mm maximum** for the lower bar. The 160 mm
figure it was justified by is real but belongs to a different article: it is in
**Art. 9.4.2 *Side bumpers*** — *"Height of the upper bar: 160.0 mm minimum from
the ground (measured to the tube top)"* — and it is about the side bumper, not the
front protection. Proof of the section boundary, PDF p. 23: the 200/250 line, the
70/110 line and *"Front overhang: 350.0 mm minimum"* are all above the **9.4.2
Side bumpers** heading in the page's own text, and the 160 line is 18 lines below
it. Same family as the §7.2/§7.4 error in CLAUDE.md: an article number recalled
rather than located. The front bumper's two legal height windows are 70…110 for the
lower bar and 200…250 for the upper, both to the tube top, and a fairing that needs
a pickup above 110 mm gets it from the **upper** bar.

### `chassis_nose_hoop_upper`

**Status:** built, respecified. **Its top is 130 mm below the regulation
minimum**: built at z +155 center, Ø20, top 165, against Art. 9.4.1's 200 minimum.
**Attaches to:** `chassis_bumper_socket_front_upper_?` (seated),
`chassis_front_bumper_support` (welded), `chassis_nose_hoop_lower` (welded),
`bodywork_front_panel` (bolted — §Bodywork)
**Envelope:** Art. 9.4.1.
**Verification:** gate 1, gate 2

| dimension | `params.py` field | value | prov | basis | as built |
| --- | --- | --- | --- | --- | --- |
| outer diameter | new `tube_bumper_upper` | 16 | `sourced` | ≥16.0 | 20 |
| straight length | new `nose_upper_straight` | 385 | `derived` | mid of 375…395 | 280.5, **94.5 under** |
| centerline z | new `nose_upper_z` | 217 | `derived` | tube top 225, mid of 200…250 | 155, top 165, **35 under min** |
| frontmost tube center y | `nose_y` | +935 | `derived` | vertically aligned with the lower | +905 |
| attachment spacing | new `nose_upper_mounts` | 550 | `sourced` | *"550.0 mm apart"*, and it matches the HF forms' 550 mount spacing in front matter §5b |

Path, Ø16, mirrored, planar at z +217 throughout; the socket carries a 167 mm riser
from the loop at z +50 up to the bar:

    (-275, +539, +217)   50 mm inside the left socket
    (-192.5, +935, +217)
    (+192.5, +935, +217)
    (+275, +539, +217)

Vertical separation between the two bars at the front is 217 − 85 = **132 mm**,
which clears Art. 9.5.2's *"distance of 60.1 mm minimum between the 2 support
tubes of the clamps"* (PDF p. 24) by 72 mm. That line is what makes the two bars a
fairing mount rather than two decorations, and it was not cited anywhere in this
repo.

### `chassis_front_bumper_support`

**Status:** new
**Attaches to:** `chassis_nose_hoop_lower` (welded), `chassis_nose_hoop_upper`
(welded)
**Envelope:** Art. 9.4.1 — *"Both bars must be connected by the front bumper
support."* Not optional.
**Verification:** gate 2

Two vertical Ø16 posts at x ±75 (`estimated`; the article does not place them),
y +935, from z +85 to z +217.

### `chassis_bumper_socket_front_*` (4 parts)

**Status:** new
**Attaches to:** `chassis_cross_front` (welded), `chassis_nose_hoop_?` (seated)
**Envelope:** Art. 9.4.1 — *"two welded chassis frame attachments"*, and for the
lower pair *"horizontally and vertically parallel to the kart's axis and allow for
a 50.0 mm insertion of the bar."*
**Verification:** gate 2

| part | x | y | bore axis z | bore | prov |
| --- | --- | --- | --- | --- | --- |
| `..._lower_l` / `_r` | ±225 | +606 | +85, on a 35 mm riser off the loop | Ø20, 50 deep, axis parallel to y | `sourced` spacing |
| `..._upper_l` / `_r` | ±275 | +539 | +217, on a 167 mm post off the loop | Ø16, 50 deep | `sourced` spacing |

### `chassis_side_bar_l` / `_r`

**Status:** built, respecified as the side bumper's **lower** bar. It is 55 mm
inboard of the regulation minimum today.
**Attaches to:** `chassis_bumper_socket_side_lower_*` (seated),
`bodywork_sidepod_?` (bolted), `exhaust_hanger` (bolted, right only)
**Envelope:** Art. 9.4.2, PDF p. 23, verbatim:

> The side bumper consists of two elements of magnetic steel round tubing that are
> centred in relation to the longitudinal axis of the kart. Each element must be
> composed of a lower and an upper bar. They must have a diameter of 20.0 mm.
> Minimum straight length is 400.0 mm for the lower bar and 300.0 mm for the upper
> bar. Overall width: 480.0 mm minimum and 520.0 mm maximum for the lower bar,
> 480.0 mm minimum and 600.0 mm maximum for the upper bar (measured to the tube
> midpoint) in relation to the longitudinal axis of the kart.
> Each bar must be fixed to two welded tube attachments that must be 500.0 ± 5 mm
> apart (measured to the tube midpoint). These attachments must be parallel to the
> ground, perpendicular to the axis of the chassis and allow for a 50.0 mm
> insertion of the bar.
> Height of the upper bar: 160.0 mm minimum from the ground (measured to the tube
> top).

**Verification:** gate 1, gate 2

**How the width figure is read, because it has two readings.** *"Overall width:
480.0 minimum and 520.0 maximum […] in relation to the longitudinal axis"* is
taken here as a **distance from the centerline**, not a total width, and that is
`derived` rather than `sourced`. The argument: a total width of 480…520 for a side
bumper element would put it inboard of the frame's own 650 mm outer rear width and
far inboard of the pod datum window Art. 9.5.4 requires the side bodywork to
occupy — 580 to 700 mm from the centerline, front matter §4 — while the same
article requires the bodywork to be *"securely attached to the side bumpers"*. The
half-width reading is the only one under which the bar is reachable from the pod.

| dimension | `params.py` field | value | prov | basis | as built |
| --- | --- | --- | --- | --- | --- |
| outer diameter | `tube_bumper` | 20 | `sourced` | *"a diameter of 20.0 mm"* | 20 ✓ |
| outermost tube midpoint x | `sidebar_x_lower` (new) | 500 | `sourced` | mid of 480…520 | 445, **35 inboard of the minimum** |
| straight length | new `sidebar_lower_straight` | 420 | `derived` | ≥400 plus 20 mm of margin | ~1030 of curve, no straight |
| centerline z | new `sidebar_lower_z` | 80 | `estimated` | no height given for the lower bar; above the rail, below the pod's 25…60 mm lower edge | ~105 |
| attachment spacing | new `sidebar_mount_pitch` | 500 | `sourced` | *"500.0 ± 5 mm apart"* | not modeled |

Path, right side, Ø20:

    (+220, +190, + 80)   50 mm inside the front socket, on the rail
    (+500, +100, + 80)
    (+500, -320, + 80)   420 mm of straight between these two
    (+310, -310, + 80)   rear socket, on the rail

**Reach to the pod face, because Art. 9.5.4 makes this bar load-bearing for the
bodywork** — *"[the side bodywork] must be securely attached to the side bumpers"*.
Front matter §4's pod datum has been amended to the article's literal reading,
`x = 671.0 − 0.0767·y`, giving a pod outer face at **618 front** and **664 rear**
with a 29 mm inset budget. Against a lower bar at x 500:

    pod face at the front of the pod, y +100:  618 - 500 = 118 mm of reach
    pod face at the rear of the pod,  y -320:  664 - 500 = 164 mm of reach

Those are the bracket lengths §Bodywork has to build, and **they cannot be
shortened by moving this bar**: Art. 9.4.2 caps the lower bar at 520 from the axis,
so 98 and 144 mm are the floor. A pod mount reaching 100 to 165 mm outboard of its
bar is normal on a real kart — the bar is a bumper, not a pod rail.

### `chassis_side_bar_upper_l` / `_r`

**Status:** new. Art. 9.4.2 requires **two** bars per side and the kart has one.
**Attaches to:** `chassis_bumper_socket_side_upper_*` (seated),
`bodywork_sidepod_?` (bolted)
**Envelope:** Art. 9.4.2, quoted above.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| outer diameter | 20 | `sourced` | *"a diameter of 20.0 mm"* |
| outermost tube midpoint x | 560 | `estimated` | inside the sourced 480…600 window, with 40 mm of margin to the cap; 60 mm outboard of the lower bar, which is what makes a pod flare rather than sit vertical, and it cuts the pod's upper bracket reach to 58 front / 104 rear against the amended datum |
| straight length | 320 | `derived` | ≥300 plus 20 mm of margin |
| centerline z | 175 | `derived` | tube top 185, clears **Art. 9.4.2's** 160 mm minimum by 25 — this is the article the 160 belongs to, and it applies here and not to the nose hoop |

### `chassis_bumper_socket_side_*` (8 parts)

**Status:** new
**Attaches to:** `chassis_rail_?` (welded), `chassis_side_bar_*` (seated)
**Envelope:** Art. 9.4.2 — two welded tube attachments per bar, 500 ±5 apart,
parallel to the ground, perpendicular to the chassis axis, 50 mm insertion.
**Verification:** gate 2

Stations, per side: y **+190** and y **−310** (500 apart), on the rail centerline,
so x = 220 at the front pair and x = 310 at the rear pair, at the bar's own z.

### `chassis_rear_bumper`

**Status:** built, respecified. Moves forward **180 mm**.
**Attaches to:** `chassis_rail_?` (welded, legs to the rail ends),
`chassis_cross_tail` (welded), `bodywork_rear_panel` (bolted)
**Envelope:** none directly — there is **no rear bumper article**. Art. 9.4 names
only front and side protections as compulsory, and Art. 9.5.5.1 governs the rear
wheel *protection*, which is bodywork. This hoop exists to carry that panel.
**Verification:** gate 1, gate 2

| dimension | `params.py` field | value | prov | basis | as built |
| --- | --- | --- | --- | --- | --- |
| outer diameter | `tube_bumper` | 20 | `estimated` | by analogy with Art. 9.4.2's 20.0; no article covers it | 22 |
| rearmost tube center y | `rear_y` | −725 | `derived` | 10.2 | −904 |
| top bar z | new `rear_bumper_z` | 140 | `estimated` | 10.6 item 4 | 140 ✓ |
| half-width | new `rear_bumper_half` | 310 | `derived` | matches the rail ends, so the legs weld without a jog | 310 ✓ |

Path, Ø20:

    (+310, -700, + 50)   weld to the right rail's end
    (+310, -725, +140)
    (   0, -725, +140)   crosses the centerline
    (-310, -725, +140)
    (-310, -700, + 50)

## 10.9 Seat struts, and the two Art. 9.1.2 stays

Art. 9.1.2 *Chassis requirements*, PDF p. 22, verbatim — both sentences, because
both bear on this section:

> Anti-roll bars must only be connected to the main tubes of the chassis frame.
> Extra seat stays are allowed between the rear axle brackets and the seat.

Art. 4.2.3, PDF p. 8, is the other half: chassis auxiliary parts are *"the
attachments, connections and attachment points welded to the frame for the
steering, pedals, seat with four seat supports, bumpers, radiator(s), brakes,
intake silencer, engine, exhaust and exhaust silencer."* **Four** seat supports,
which is what the kart has.

### `chassis_seat_strut_front_l` / `_r`

**Status:** built, respecified
**Attaches to:** `chassis_rail_?` (welded, same side only), `seat_shell` (bolted),
`chassis_floor_tray` (pierced)
**Envelope:** Art. 4.2.3 — one of the four seat supports.
**Verification:** gate 2 (17.45 mm today)

    (±305, + 40, + 50)   on the rail at the central strut station
    (±240, - 10, +110)
    (±160, - 20, +150)   seat_shell's front ear

### `chassis_seat_strut_rear_l` / `_r`

**Status:** built, respecified. These are the Art. 9.1.2 *extra seat stays*, and
the article says where they start: **the rear axle brackets**. They currently start
on the rail at y −400 and end 78.07 mm from the shell, aimed at nothing.
**Attaches to:** `chassis_bearing_hanger_l`/`_r` (welded), `seat_shell` (bolted)
**Envelope:** Art. 9.1.2 — *"Extra seat stays are allowed between the rear axle
brackets and the seat."*
**Verification:** gate 2 (78.07 mm today)

    (±185, -525, +130)   on the bearing hanger's plate, which spans z 40..167
    (±170, -400, +230)
    (±145, -215, +300)   seat_shell's rear ear, on the back's flank

    stay length = sqrt(40^2 + 310^2 + 170^2) = 356 mm

**Both ear points are `estimated` and cannot be made exact from here.**
`seat_shell` is lofted from `SEAT_HALF_WIDTH` × `seat_width`/2 and a wing flare
along a filleted spine; its outer edge is a sampled surface, not a constant. The
numbers above are read off that loft — half-width 165 at the widest station, wing
flare 82 mm proud at the hip and 62 at t = 0.78, hip at (0, −60, +75), back top at
(0, −263, +365) — and they will land within a few millimeters, not within 2.0.

**What would settle it:** §Cockpit publishes four empties, `seat_ear_front_l/_r`
and `seat_ear_rear_l/_r`, on the shell's own sampled surface, and `frame.py` reads
them through `context`. A lofted surface met by a constant authored in a second
module is the same failure as `Dictionary.get(key, default)` — it will drift and
nothing will say so. This is the single highest-value follow-up in this section.

### `chassis_bearing_hanger_l` / `_c` / `_r`

**Status:** built, respecified laterally
**Attaches to:** `chassis_cross_rear` (welded, all three), `chassis_rail_?`
(welded, outer pair only), `axle_rear` (pierced),
`chassis_bearing_cassette_?` (bolted), `chassis_floor_tray` (pierced — no longer
applies once the tray moves forward, see below), `chassis_seat_strut_rear_?`
(welded, outer pair only)
**Envelope:** none. Art. 9.1.2 calls them *"the rear axle brackets"*, which is the
only place the regulations name them.
**Verification:able** gate 1, gate 2

| dimension | value | prov | basis | as built |
| --- | --- | --- | --- | --- |
| outer pair x | ±300 | `derived` | the rail centerline at y −525 is ±310 and the plate is 12 thick, so 300 puts it inside the tube and welds at 0 mm | ±185 |
| center x | 0 | `estimated` | a KZ carries a third bearing; nothing publishes its position | 0 ✓ |
| plate thickness | 12 | `estimated` | unchanged | 12 |
| z span | 40…167.5 | `derived` | rail bottom 35 to axle center 147.5 plus 20 | unchanged |

**`joints.py` needs an edit here:** `chassis_floor_tray` / `chassis_bearing_hanger_?`
is declared `pierced` with the reasoning *"the hangers stand up through the tray"*.
Once the tray runs y +40…+760 per Art. 4.6, the tray is 565 mm forward of the
hangers and that joint must be **deleted**, not waived. Same for
`chassis_floor_tray` / `chassis_cross_rear`. Both are declarations that will fail
gate 2 for the right reason.

## 10.10 Parts a real KZ chassis has and this one does not

Each of these is a spec item rather than an omission, per front matter §6.

### `chassis_torsion_bar_front`, `chassis_torsion_bar_rear`

**Status:** new
**Attaches to:** `chassis_rail_l` and `chassis_rail_r` (clamped, both ends)
**Envelope:** Art. 9.1.2 — *"Anti-roll bars must only be connected to the main
tubes of the chassis frame."* Art. 4.2.5 (PDF p. 8) lists *"anti-roll bar"* among
chassis components, and Art. 4.2.6 permits them a flexible connection where
auxiliary parts must be welded.
**Verification:** gate 2

The front and rear adjustable bars a KZ tunes stiffness with. Both span rail to
rail, clamped rather than welded so they can be swapped or removed — which is what
Art. 4.2.5's classification as a *component* rather than an *auxiliary part*
permits.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| outer diameter | 28 | `estimated` | flat-sided round bar; no source, and nothing in Art. 9 sizes it |
| front bar y | +230 | `estimated` | ahead of the waist, where the rails are still 220 apart per side |
| rear bar y | −417 | `derived` | on the `chassis_cross_seat` station, which is where the rails are parallel |
| centerline z | 50 | `derived` | `rail_z`; the bar clamps around the tube |

### `chassis_rail_insert_r`

**Status:** new
**Attaches to:** `chassis_rail_r` (pressed)
**Envelope:** Art. 4.2.3, PDF p. 8 — *"Chassis auxiliary parts also include the
inner reinforcement of the chassis main tubes (maximum length 250 mm) between the
axle bracket and the engine support."*
**Verification:** gate 1 (it is inside the rail on purpose), gate 2

The tube-in-tube stiffener. Its permitted span is named by the article and this
kart's geometry lands inside it exactly:

    axle bracket   y = -525      chassis_bearing_hanger_r
    engine support y = -305      engine_mount_clamp_rear
    insert length  = 220 <= 250  Art. 4.2.3

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| outer diameter | 26 | `estimated` | a slip fit inside a Ø30 × 2 tube |
| length | 220 | `derived` | the article's own two endpoints |
| side | right only | `derived` | the article says *"between the axle bracket and the engine support"*, and the engine is on the kart's right |

### `chassis_bearing_cassette_l` / `_c` / `_r`

**Status:** new
**Attaches to:** `chassis_bearing_hanger_?` (bolted), `axle_rear` (pierced)
**Envelope:** none.
**Verification:** gate 1, gate 2

The kart has three hanger plates with the axle passing through bare holes. A real
kart carries a self-aligning bearing in an aluminium cassette bolted to each
hanger; it is the most visible piece of hardware on the rear of the frame.
Ø72 outer, 30 mm wide, `estimated` — this is at the §Running gear boundary and
that section owns the bearing itself.

### `chassis_idr_plate`

**Status:** new
**Attaches to:** `chassis_rail_r` (bolted) — but see below
**Envelope:** Art. 4.1.5 *Impact Data Recorder*, PDF p. 7, verbatim:

> From 01.01.2026, in FIA Karting Championships, Cups and Trophies, it is
> mandatory to install an Impact Data Recorder. […] The IDR must be stocked on an
> interchangeable plate. The plate must be horizontally and mechanically fixed to
> the chassis, on the right side of the seat, under the inlet silencer.

**Verification:** gate 2

Mandatory on this kart's own regulation year, and it does not exist. Horizontal
plate, on the kart's **right**, under the airbox, with the arrow pointing forward
parallel to the longitudinal axis. Its position is `estimated`: x +250, y −120,
z +72 (on the rail's inboard side, below `engine_airbox`). Whether it clears the
airbox is §Powertrain's number.

### `chassis_inboard_rail_l`

**Status:** new
**Attaches to:** `chassis_cross_seat` (welded), `chassis_cross_tail` (welded),
`chassis_rail_l` (welded, at its forward end)
**Envelope:** none.
**Verification:** gate 1, gate 2

The CRG plan drawing's tube `B5`: a longitudinal main tube inboard of one rail,
running from the rear extremity forward to about y −48, at roughly 63% of the
rail's own half-width. It is on **one side only** — the CRG frame is not laterally
symmetric, and `frame.py`'s "a chassis is symmetric and authoring both halves is
two places for a number to be wrong" is true of this kart and not of the reference.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| half-width x | 198 | `sourced` | CRG plan, 211 px from the centerline at the confirmed scale |
| extent y | −720 … −48 | `sourced` | CRG plan |
| outer diameter | 30 | `sourced` | one of the six `B` tubes |
| **side** | left | `estimated` | the drawing does not label which side is which; left is chosen because the right rail carries the engine mount, the exhaust hanger and the IDR plate and has no room |

This tube is a stiffness asymmetry, not decoration, and #159's by-feel list should
carry it: a KZ's left-right asymmetry under load is real and this is part of it.

### `chassis_sprocket_guard_mount`

**Status:** new
**Attaches to:** `chassis_cross_rear` (welded), sprocket guard (bolted —
§Powertrain)
**Envelope:** none in Art. 9. The guard itself is required by the sporting
regulations rather than the technical ones, and this document may only cite the
pinned technical PDF, so no article is claimed.
**Verification:** gate 2

A bracket on the rear cross member, right of center, `estimated` at x +230,
y −525, reaching up to z +200 beside the chain run.

### `chassis_skid_plate_l` / `_r`

**Status:** new
**Attaches to:** `chassis_rail_?` (clamped)
**Envelope:** Art. 4.2.5, PDF p. 8 — *"Chassis skid plates must only protect the
tubes and must be made of plastic or composite material."*
**Verification:** gate 2

The plastic rail protectors every kart runs. They clip around the underside of
each rail from about y +100 to y −450, 3 mm wall, `estimated`, and they are the
lowest thing on the kart — which matters, because `ground_clearance` = 35 is
documented as being measured to the rail's underside and these hang below it.

## 10.11 Status and provenance tally

| status | count | parts |
| --- | --- | --- |
| `built` | 20 | `chassis_rail_l/_r`, `chassis_cross_front`, `_mid_front`, `_seat`, `_rear`, `_tail`, `chassis_side_bar_l/_r`, `chassis_nose_hoop_lower/_upper`, `chassis_rear_bumper`, `chassis_steering_hoop`, `chassis_floor_tray`, `chassis_seat_strut_front_l/_r`, `chassis_seat_strut_rear_l/_r`, `chassis_bearing_hanger_l/_c/_r` |
| of which respecified | 18 | everything except `chassis_bearing_hanger_c` and `chassis_seat_strut_front_?`'s rail foot |
| `new` | 26 | `chassis_kingpin_boss_l/_r`, `chassis_steering_support_upper`, `chassis_tray_edge_l/_r`, `chassis_front_bumper_support`, `chassis_bumper_socket_front_*` (4), `chassis_side_bar_upper_l/_r`, `chassis_bumper_socket_side_*` (8), `chassis_torsion_bar_front/_rear`, `chassis_rail_insert_r`, `chassis_bearing_cassette_l/_c/_r`, `chassis_idr_plate`, `chassis_inboard_rail_l`, `chassis_sprocket_guard_mount`, `chassis_skid_plate_l/_r` |
| `delete` | 3 parameters, 0 parts | `length_overall`, `tray_width`, `tray_length` |

Numbers by tag, counting every value in every dimension table and path in this
section:

| tag | count | share |
| --- | --- | --- |
| `sourced` | 34 | 30% |
| `derived` | 39 | 34% |
| `estimated` | 41 | 36% |
| `snippet` | 0 | — |

`estimated` at 36% is the expected outcome for a chassis: front matter §6 says
`none` is the common answer in the **Envelope** field for this assembly, and it is
— 14 of the 27 part entries above are unregulated beyond tube material. What
changed is that the 34 `sourced` numbers are almost all new: before this section,
the only externally-anchored chassis figures in the repo were the wheelbase, the
track width and `tube_main`'s "30 or 32".

## 10.12 What is not settled

| open | what would settle it |
| --- | --- |
| kingpin x: 320 (photogrammetric) vs 347.5 (`E`/2 − boss radius), 28 mm apart | a front-three-quarter photograph with a scale in frame, or an HF form whose section B dimensions kingpin spacing |
| the 142.5 mm hole in the front track chain | §Running gear deciding whether `stub_axle_length` measures the stub or the whole kingpin-to-wheel-centerline span, and whether `track_front` = 1240 survives |
| `seat_shell` ear coordinates to 2.0 mm | §Cockpit publishing four `seat_ear_*` empties off the sampled loft |
| Art. 4.6 *"impenetrable"* vs a clearance aperture for the steering support | nothing in the pinned PDF resolves it; a scrutineering bulletin would |
| the front loop's z, and whether a KZ frame's front rises above the rails | the CRG form's side view, page 2, measured the way the plan view was here |
| which side the CRG's `B5` inboard rail is on | a photograph of a Road Rebel frame from below |
| the front bumper's 420 mm overhang within the fairing | TD n°2.2, which dimensions the fairing mounting kit and is not obtainable |
| whether §Bodywork's fairing can pick up on the upper bar at z +217 only, given the lower bar is capped at a 110 mm tube top | §Bodywork measuring the OTK M4 / KG 505 forms' own mount heights; both forms are in `refs/kart-visual/` and neither has been read for height |
| the side bumper width figure's reading — half-width from the axis (taken here) vs total width | any HF bodywork form that dimensions a side bumper element, or TD n°2.0 |
