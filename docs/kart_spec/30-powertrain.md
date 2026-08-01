# 30. Powertrain — engine, mount, driveline, exhaust, cooling

Issue #190. Everything that carries torque or coolant: the engine cluster, its
mount, the chain line, the exhaust from the port to the silencer outlet, and the
whole cooling circuit. `axle_sprocket` and `axle_rear` belong to §Running gear;
this section states its chain-line requirements as explicit numbers so the two
sections meet rather than each assuming the other moved.

Conventions, provenance vocabulary, the part-entry format and the cooling
envelope are §00. **Art. 5.3 is quoted once, in §5a, and every cooling row here
cites back to it rather than re-quoting it.** Millimeters and degrees
throughout; `params.py` is meters.

## 30.0 What the engine is, from the regulation

Art. **9.10.1** *Engine characteristics*, verbatim, PDF page **27**:

> Water-cooled 125 cm3 single-cylinder engine with a reed-valve intake and a
> gearbox, with one cooling circuit for the crankcase, cylinder and head.
> It must not be possible to separate the gearbox from the engine. The engine
> case must be made of two parts (vertical or horizontal).
> Exhaust port angle limited to maximum 199.0 °[…]
> Volume of the combustion chamber: 11.0 cm3 minimum […]
> Gearbox including the primary gear homologated with the engine.
> Hand-operated mechanical gearbox control.

Four things in that paragraph are already built and are now `sourced` rather than
assumed: the water jacket in **all three** castings (so the crankcase is a
cooling part, not just the cylinder and head — this is why the pump feeds the
case), the **two-part case** with its parting line, the reed valve, and the
hand shifter. Three more articles bound this section and are quoted where they
bite: **5.9** chain guard and **5.10** exhaust (PDF p. **17**), **9.12.1** KZ
carburettor and **9.13.1** KZ intake silencer (PDF pp. **28**, **30**),
**9.15.1** / **9.16.1** exhaust and silencer (PDF p. **30**), **9.17** / **9.18.1**
radiators and gearing (PDF p. **31**), **4.2.3** / **4.2.4** / **4.2.5** chassis
auxiliary parts and components (PDF p. **8**).

Two of those change what this repo believed:

- **Art. 9.18.1, PDF p. 31: *"The chain and sprockets are free."*** 219 pitch is
  **not** a KZ regulation. It is mandated in OK (9.18.2) and Group 3, and it is
  the universal trade standard, which is a different kind of fact and is tagged
  differently below. `CHAIN_PITCH`'s docstring reads as though the class required
  it.
- **Art. 9.17, PDF p. 31**, is where *"only one cooling circuit"* lives for all
  classes — Art. 5.3 says the same thing for the engine/radiator/pump group on
  p. 15. `notes_radiator.md` cites 5.3 alone; both are real and 9.17 is the
  general one. **Only OK is limited to one radiator**; KZ is not, so one radiator
  is practice, not a limit.

## 30.1 The seven things this section settles, in one table

| # | question | answer |
| --- | --- | --- |
| 1 | exhaust dimensions | 15-cone table from the engine HFs, §30.6. Developed length **674**, belly **136.5**, header **44.5**, stinger **26.3**. Every current `exhaust_*` parameter is replaced. |
| 2 | chain vs header | **No interference. Nothing moves.** The chain plane is x **+115**, the header stub x 294–344; 89.6 mm of clear air, measured. §30.5. |
| 3 | engine mount | clamps bored **Ø30.0 coaxial with the right rail**, bodies straddling the rail centerline at x 271.6 / 260.7. Gap 12.10 and 22.90 → **0.0**. §30.2. |
| 4 | crankcase outboard face | **x +398** unchanged; 107 mm to the pod's mouth at 505, 26 mm to the side bar's inboard surface at 424. §30.3. |
| 5 | radiator lateral dispute | **x −365, core width 250.** Lateral extent **−240 … −490**. §30.7. |
| 6 | radiator brackets and rake | rake **40° from vertical**, its own parameter; `radiator_z` **270**; both brackets clamp **`chassis_rail_l`**; **no joint to `seat_shell`, ever.** §30.7. |
| 7 | exhaust hanger | Ø30 mushroom clamp on **`chassis_cross_rear` at x +54**, 169 mm arm, spring cradle on the pipe at s = 513. §30.6. |

## 30.2 The mount — and the rail station this section requires

`_engine_mount`'s docstring contains the bug in words: the clamps sit *"about
11 mm outboard of the 30 mm tube's surface"*. #192 measures the front clamp
**12.10 mm** off `chassis_rail_r` and the rear **22.90 mm**, so the powertrain's
only load path to the chassis is air. A clamp that is beside a tube is not a
clamp; it has to be **bored on the tube's own centerline**.

The right rail's centerline through the engine bay. **This section originally read
it out of `frame.py:_rail_path` as built — a run pinching inboard from x 285 at
y −100 to x 245 at y −420, 7.13° in plan — and §Chassis has since respecified the
rail as straight.** The two disagreed by 36 to 47 mm on the powertrain's only load
path to the chassis, which the recheck pass flagged as the one cross-section
conflict that would have broken a build. §Chassis wins: its rail comes from the CRG
homologation form's published frame widths, and it establishes that the frame is
**widest at the rear**, where `frame.py` and the built mesh both have it backwards.

Restated against §Chassis's rail:

    z_rail   = ground_clearance + tube_main/2 = 35 + 15 = 50            derived
    x_rail(y) = 310                for -720 <= y <= -48                 §Chassis
    plan angle = 0 deg -- the run is straight, so the bore axis is
                 parallel to the kart's axis and the 7.13 deg tangent
                 this section carried is withdrawn                      derived

**Required rail station:** the right main rail's centerline must pass through
**(x 310.0, z 50.0)** at both **y = −191.5** and **y = −278.5**, OD **30.0**. Both
clamp bores are generated from the rail's own path and tangent, so this section
states the requirement and does not duplicate the geometry — if the rail moves
again the clamps follow and nothing here needs re-deriving by hand. That property
is what made this reconciliation two numbers rather than a rewrite.

### `engine_mount_clamp_front`, `engine_mount_clamp_rear`
**Status:** built, **geometry replaced**
**Attaches to:** `chassis_rail_r` (clamped, Ø30.0 bore, 2× M8), `engine_mount_plate` (bolted, 2× M8 vertical), `chassis_floor_tray` (pierced — the pan is cut around the clamp)
**Envelope:** none. Art. 4.2.5 lists *"engine bracket"* as a chassis component and 4.2.4 requires auxiliary parts not to fall off in motion; neither dimensions it.
**Verification:** gate 2 (the pair that is 12.10 / 22.90 mm today), gate 1 for the tray piercing, `genkart.sh --check`

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bore diameter | 30.0 | `derived` | equal to `tube_main`; a clamp bore is the tube it grips, so contact is 0.0 by construction and the pair is `clamped`, which permits the facet-level overlap |
| bore axis | the rail's local tangent, **0° in plan** | `derived` | above. §Chassis's rail is straight through the engine bay; the 7.13° this row carried came from the as-built pinching rail |
| front clamp, y span | −213 … −170 (43 long) | `estimated` | unchanged from the build; 43 mm is a clamp block's length |
| front clamp, x span | **308 … 333** | `derived` | rail_x(−191.5) = 310, so 310 − 2 to 310 + 23: 2 mm past the centerline guarantees engagement, 23 mm outboard carries the bolts |
| rear clamp, y span | −300 … −257 | `estimated` | unchanged |
| rear clamp, x span | **308 … 333** | `derived` | rail_x(−278.5) = 310, same construction. The two clamps now share one x span, because the rail is straight |
| both, z span | 36 … 72 | `derived` | 36 is 1 mm above the rail's underside at 35, because `ground_clearance` is measured to the rail and nothing may hang below it; 72 is `tray_top_z` + 3, which is the mount plate's underside, so clamp and plate touch at 0.0 mm |
| bolts | 2× M8 per clamp, vertical, at x = rail_x + 19, y = centre ± 15 | `estimated` | an M8 pair is what a kart engine clamp carries; nothing publishes the pattern |

**The clamps no longer pierce the floor tray, and the reason is worth keeping.**
This section originally declared four `pierced` joints: `chassis_floor_tray` as
built runs to x ±280 from y +180 back to −580, so with the rail at x 262.7–273.6
the rail passed *under* the pan and any mount reaching it had to go through the
tray — 8.4 mm of x at the front clamp, 19.3 at the rear.

Both halves of that have now moved. §Chassis puts the rail at x 310, outboard of
the pan's edge rather than inboard of it; and Art. 4.6 requires the tray to stretch
*"from the central strut to the front of the chassis frame"*, which puts it at
roughly y +40 to +760 — **565 mm forward of both clamps**. The clamps are at
y −170 to −300. There is no tray anywhere near them.

So the four `pierced` declarations are withdrawn, and `joints.py`'s existing
tray-to-bearing-hanger and tray-to-cross-rear entries must be **deleted rather than
waived** for the same reason: a declared joint whose parts are half a meter apart is
not an outstanding defect, it is a statement about a kart that no longer exists.
`frame.py`'s own report already said a real kart's floor pan stops at the back of
the footwell and this one does not; the article is what makes that a build failure
instead of an observation.

### `engine_mount_plate`
**Status:** built, unchanged
**Attaches to:** `engine_mount_clamp_*` (bolted), `engine_crankcase_lower` (bolted, 4× M8)
**Envelope:** none
**Verification:** gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| x span | 230 … 322 | `estimated` | unchanged; spans the rail and reaches inboard for the case's bolt pattern |
| y span | −305 … −165 | `estimated` | unchanged |
| z span | 72 … 100 | `derived` | 72 = `tray_top_z` 69 + 3; `powertrain.py:955`'s 3 mm is deliberate and correct — the table sits **on** the tray, and it is now also in contact with the clamps' top faces at 72, so the plate is no longer attached to nothing |

## 30.3 Crankcase, covers and the outboard face

### `engine_crankcase_upper`, `engine_crankcase_lower`
**Status:** built; **castings rebuilt as lofted sections**, #212 — they were two `build.box` calls
**Attaches to:** each other (bolted, on the parting line), `engine_mount_plate` (bolted), `engine_crankcase_deck` (welded — cast in), `engine_cylinder_base` (bolted, 4 studs), `engine_clutch_bell` (welded — one casting), `engine_ignition_cover` (bolted), `engine_reed_block` (bolted), `engine_starter` (bolted), `engine_battery` (clamped), `engine_water_inlet` (welded), `drive_sprocket_carrier` (pierced)
**Envelope:** Art. 9.10.1 — two-part case, PDF p. 27. No dimension.
**Verification:** gate 1 (`bodywork_sidepod_r` waiver, 44 pairs), gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| inboard face | x **+240** | `estimated` | clears the right seat stays, which pass x 206–224 at case height. Unchanged, and it is now the **widest** station rather than a flat wall — see draft below. |
| **outboard face** | x **+398** | `estimated` | unchanged at the parting line, and stated with both clearances: **107 mm** to the right pod's mouth at x 505 (§Bodywork's `sidepod_mouth_x`), and **26 mm** to the right side bar's inboard surface, which is at x **424** at y −245 — *not* the 432 `IGNITION_COVER_OUTBOARD_X`'s comment claims |
| front face | y −145 | `estimated` | the front seat stay ends at y −129 |
| rear face | y −345 | `estimated` | leaves room for the reed block and carburettor |
| parting line | z 150 | `derived` | = `engine_z`, the crank axis, by definition of a split case |
| deck (upper casting top) | inclined, §30.4 | `derived` | was a flat z 240 — **built now**, as `engine_crankcase_deck` |
| lower half inset | 3 in x, 5 in y | `estimated` | the bolting flange's overhang. 3 rather than 5 in x because `drive_sprocket_carrier`'s inboard face is at x 393 and both halves have to be inside it |
| **section** | superellipse, n = **4** | `estimated` | every vertical arris is a radius. n = 4 pulls the corner to 0.841 of the box's, about 20 mm in |
| **draft, upper half** | 6 mm a side over 90 mm, **3.8°** | `estimated` | walls lean in going down. A real draft angle for sand casting, and enough that the highlight down the flank moves |
| **sump foot** | 116 × 166 at z 100 | `derived` | the lower half swells from a flat foot to nearly full width at the parting line. The foot is sized by the contact, not the look: it puts **61 × 140 mm** on `engine_mount_plate`'s table (x 230–322, top z 100) |

**The castings are a superellipse and not a rounded rectangle, and the reason is a
number.** `|x/a|^n + |y/b|^n = 1` has its furthest point on the **diagonal**, at
`half × 2^(0.5 − 1/n)` — 1.149 at n = 4, 1.231 at n = 5, 1.260 at n = 6. Squaring
a section therefore *grows* it, all of the growth at the corners, and the first
build of #212 shipped a docstring claiming the exact opposite. Gate 1 answered with
twelve intersecting pairs. A rounded rectangle reading the same off the photograph
— 128 across flats on a 22 mm radius — would have pushed the barrel's corners 17 mm
outside the Ø128 jacket every clearance figure in this section was measured against.
The invariant that does hold, and the one the tables lean on, is that **a
superellipse is exactly `half` on its axes**.

### `engine_crankcase_deck`
**Status:** **new**, #212 — the deck plane §30.4 has specified since M5 and nothing built
**Attaches to:** `engine_crankcase_upper` (welded — cast in, then machined), `engine_cylinder_base` (bolted), `exhaust_manifold`/`_spigot` (pierced) and `exhaust_manifold_bolt_[01]` (threaded), all three of those per #213
**Envelope:** none
**Verification:** gate 1, gate 2, and the arithmetic below

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| top face | the deck plane of §30.4, less 1.5 | `derived` | built at z 238.5 and put through the same `_lean` matrix the flange gets, so the two can only ever be parallel |
| top half-extents | 74 × 80, n = 4 | `derived` | the flange is 70 × 76 at the same exponent, so the boss's outline is outside the flange's at **every** azimuth — minimum clearance **4.00 mm** over 720 samples |
| bottom face | flat, z 196 | `estimated` | 44 mm down, horizontal — a machined boss on a cast body is two frames at once. The top plane's lowest point is 203.4, so the solid never turns itself inside out |
| bottom half-extents | 74.5 × 83 | `estimated` | inside the case wall at that height (76.6 × 97.6). A boss that pokes out through the flank is not a boss |
| proud at the rear | 33.6 | `derived` | wholly buried at the front. That is the same 33 mm the wedge was |

**What this part is for.** The barrel leans 25° and the case's top was flat at
z 240, so the flange's bottom face and the deck it landed on were two planes 25°
apart. Two such planes cross once and diverge either side of the crossing: 18
triangle pairs overlapped **at** the crossing, so gate 1 saw an overlap, gate 2 saw
a contact, and neither could see the wedge. Measured along the flange's own bottom
face, rear edge to front:

    station        was            now
    −76 mm         +32.1 air      1.5 seated
    −38 mm         +16.1 air      1.5 seated
    bore centre      0.0          1.5 seated       <- the crossing
    +38 mm         −16.1 buried   1.5 seated
    +76 mm         −32.1 buried   1.5 seated

The 1.5 mm is deliberate and is not slack: coplanar faces z-fight, so the flange
buries its bottom face in the boss. **This is the class of defect neither gate can
find** — the wedge is outside both meshes, so it is neither an undeclared overlap
nor a gap between declared parts. It took somebody looking at the engine.

**The 26.7 mm sidepod intrusion is fixed by construction, from the other side.**
`CRANKCASE_OUTBOARD_X = 0.398` was justified against the side bar at 0.420 and
nobody checked the pod wall at 0.372 — 26 mm inboard of the crankcase. With the
pods moved to the tapering Art. 9.5.4 datum, the pod's mouth is at 505 and the
crankcase has 107 mm. **This section changes nothing to achieve that**; it states
398 and 505 so that neither section has to assume the other moved.

### `engine_clutch_bell`, `engine_clutch_cover`, `engine_clutch_bolt_0..5`
**Status:** built, unchanged
**Attaches to:** bell/cover (bolted), bell/crankcase_upper (welded), cover/bolts (threaded), cover and bell/`drive_sprocket_carrier` (pierced)
**Envelope:** none
**Verification:** gate 1, gate 2

Bell r 60 at (y −228, z 172), 14 proud; cover r 56, inboard face x 196, 6 bolts
on a Ø46 circle. All `estimated` from the R2 reference photographs. The clutch is
a hand-operated multi-plate **inside** the cases — Art. 9.10.1's *"hand-operated
mechanical gearbox control"* is the shifter; the clutch lever on the wheel is
§Cockpit's.

**Delete the three `engine_water_pump` joints on this casting** — see §30.7: Art.
5.3.2 puts the KZ pump on the rear axle, not on the clutch cover.

### `engine_ignition_cover`, `engine_ignition_bolt_0..4`
**Status:** built, unchanged
**Attaches to:** `engine_crankcase_upper` (bolted), bolts (threaded)
**Envelope:** none
**Verification:** gate 1 (`bodywork_sidepod_r` waivers, 76 and 8 pairs — fixed by the pod move)

r 52 at z 185, outboard face x 430, 5 bolts on Ø42, all `estimated`. It clears
`chassis_side_bar_r` **vertically** and not laterally: the cover's outboard face
at 430 is **6.1 mm past** the bar's inboard surface at 424, and the pair does not
overlap only because the cover spans z 133–237 and the bar z 96–116 — **16.7 mm**
of vertical clearance. The docstring's "2 mm short in x" is out of date and is a
clearance that does not exist; the 16.7 is the real one and is the one to hold.

## 30.4 Cylinder, head and the forward lean

**The cylinder leans 25° forward, and this is forced, not styled.** The exhaust
port axis is 25° out of the plane perpendicular to the bore, tilted toward the
crankcase (`notes_exhaust.md` §1, two independent measurements off the KZ-R1 HF
p. 3, 1.7° apart). With a **vertical** cylinder that makes the pipe's inlet axis
point 25° **downward**, and a 674 mm chamber cannot be packaged from there:

    vertical cylinder, port centre 49.9 above the base face at z 240 -> z 289.9
    port axis (0, -0.9063, -0.4226) exits the Ø128 jacket at (319, -314, 260.1)
    pipe inlet face 28 mm along it                  -> (319, -339.4, 248.3)
    the bend does not start until s = 134.7, so at the rear axle line (y -525)
    the pipe has fallen to z 161.8 with a Ø70 section: bottom 127, and the
    axle's top is 172.5.  Interference, and no roll of the bend plane fixes it
    because the drop happens before the bend.  Measured over phi = 0..20 deg:
    the axle gap runs -62.7 .. -51.1 mm, i.e. always inside the axle.

    clearing the axle needs the drop from the inlet to y -525 under 38 mm over
    186 mm of run, i.e. an inlet-axis elevation within ~12 deg of horizontal.
    A 25 deg forward lean puts it at exactly 0 deg.

So: **lean 25° forward, `derived`** — from the sourced port angle plus the
packaging arithmetic above. The confirming photograph does not exist here;
`notes_exhaust.md` §9 item 2 lists the lean direction as an open question and one
side-on photograph of a KZ engine in a chassis would turn this from `derived`
into `sourced`.

    deck plane: through (319, -250, 240), normal (0, sin25, cos25)
    deck z(y) = 240 - 0.4663 * (y + 250)          derived
      at y -326 (flange rear edge):  275.4
      at y -174 (flange front edge): 204.5
    behind y -326 the upper casting's top is flat at z <= 255, which is the
    gearbox and reed-block housing, not the deck.  This is what leaves the
    pipe's underside 7.8 mm of clearance over the case at y -345.

### `engine_cylinder`, `engine_cylinder_base`, `engine_cylinder_base_nut_0..3`
**Status:** built, **re-oriented** (25° forward about the lateral axis, through the base-face centre)
**Attaches to:** `engine_crankcase_upper` (bolted, base flange on the deck), each other (welded — one casting), `engine_head` (bolted), nuts (threaded), `exhaust_manifold` (bolted), `exhaust_chamber` (seated, spigot in the port)
**Envelope:** Art. 9.10.1 exhaust port angle ≤199.0° measured per Appendix 3, PDF p. 27 — a port *width* angle, not a geometry this mesh expresses. Otherwise none.
**Verification:** gate 1 (the `exhaust_chamber`/`engine_crankcase_upper` waiver, 50 pairs, is re-measured against the new routing), gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bore | 54.0 | `sourced` | KZ-R1 HF section A, *"Original bore 54 mm"* |
| axis (x, y) at the base face | 319, −250 | `estimated` | unchanged; x is the case's own centre |
| base face z | 240 | `derived` | `MOUNT_PLATE_TOP` 100 + `CRANKCASE_HEIGHT` 140 |
| lean | 25° forward from vertical | `derived` | above |
| jacket **half-width across flats** | 64 | `estimated` | unchanged as a number and changed as a shape — it was a radius. No fins: Art. 9.10.1 water-cools the cylinder, ADR-0028 |
| **jacket section** | superellipse n = **5** | `estimated` | `eng_tm_kz10_dress.jpg` at 0.346 mm/px: the barrel's front face reads flat across **275 of its 370 px**, so 74% of the width is a flat face and only the outer quarter turns. `det_tonykart_401t_museum.jpg` shows the same casting from the rear — flat panel, straight flanks. A jacket is a squared casting with a bore in it. n = 5 and not 6 because the corner stands at 78.8 against 80.6, and those 1.8 mm are the whole clearance against the base nuts |
| **cast corner webs** | 4 × **5 mm** proud, 40° of arc each | `estimated` | R1 shows them running the barrel's full height and dying into the fillet over the base flange. Diagonal 78.8 + 5 = 83.8 |
| **skirt tuck** | 104 across flats at z 254, flaring to 128 at 274 | `derived` | **a clearance, not a style line.** The four base nuts sit at the flange's corners, 83.6 mm out on the diagonal, inner faces at 76.1, standing z 258–266. A barrel at full width puts its webbed corner at 83.8 and eats all four — which is what the first build did. Both photographs show exactly this hollow with the nuts sitting in it |
| **top deck section** | superellipse n = **2.4** | `derived` | the head is round and lands on it. The casting squares up as it comes down, which is the transition both photographs show |
| cylinder height, base face to deck | 95.0 | `sourced` | KZ-R1 HF p. 3 development, 1581 px at 0.0601 mm/px. **The build's 108 mm of jacket is 13 mm tall**; flagged, not changed, because the head's proportion was tuned against a render |
| **port centre, above the base face** | **49.9** | `derived` | HF p. 3 development, 830 px. Lower edge 35.8, upper 64.0 |
| port window | **44.1 × 28.2** | `derived` | same, 733 × 470 px. 81.6% of the bore because the HF lists **three** exhaust ports, one main plus two auxiliaries |
| port axis | 25° off perpendicular-to-bore toward the crankcase, **0° in plan** | `derived` | HF p. 3 section (24.8°) and base view (26.5°); the plan symmetry of the flange, its four bolts and the port oval about the cylinder centreline |
| port face centre, in the kart | **(319, −300, 285.2)** | `derived` | 49.9 up the leaned axis is (319, −228.9, 285.2); the horizontal port axis leaves the Ø128 jacket 70.6 mm rearward of it at 64/sin(115°); the machined face stands a little proud, so −300 rather than −299.5 |
| flange bolts | **4× M6**, on ~**62 × 44** | `sourced` (count/size) / measured ±10% (pattern) | kartshop, *"TM KZ manifold D2 28: 4x allen bolt M6 x 20 mm"*; pattern off HF p. 3 base view, quoted as the mean of a trapezoid that reads 66 upper / 57 lower |
| temperature-sensor boss | 25 from the pipe's inlet face | `sourced` | dimensioned "25" on both HF drawings, *"hole for temperature sensor"*. Art. 9.10.2 permits a sensor in the KZ2 manifold |

### `engine_head`, `engine_head_nut_0..5`, `engine_plug_boss`, `engine_plug_hex`, `engine_plug_insulator`, `engine_plug_cap`, `engine_plug_lead`
**Status:** built, **carried by the cylinder's 25° lean** — the head's centre moves from (319, −250, 348) to **(319, −204.4, 337.9)**, and everything above it with it
**Attaches to:** `engine_cylinder` (bolted), nuts (threaded), boss (welded — cast in), plug into boss (threaded), insulator (pressed), cap (seated), lead (routed), `engine_water_outlet` (bolted)
**Envelope:** Art. 5.2.1, PDF p. 15 — *"Dimensions of the threaded spark-plug housing: length 18.5 mm, pitch M14 x 1.25 mm"*
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| head radius | 53 | `estimated` | leaves 11 mm of barrel crown showing all round; a 60 mm head reads as a lid. Still the outline's **maximum** — the lobing below only ever comes inside it |
| **outline** | **6 lobes**, base 45 + 8 amplitude, n = 2.6 | `estimated` | the outline bulges at every stud. `eng_tm_kz10_dress.jpg` rows b–d have six raised pads round the perimeter, each with its fastener in a counterbore; `det_tonykart_401t_museum.jpg` looks straight down on the same six. Phased at 0, which is `engine_head_nut_0`, so each nut lands on its own pad |
| **nut deck** | flat annulus at z **393**, radius 30 to 40.5 | `derived` | a nut on the Ø38 circle spans radius 30.5 to 45.5, so it sits fully on the casting where the lobes carry it to 46 and overhangs between them. That is what both photographs show and it is why the lobes are worth having |
| **crown** | domes **7 mm**, radius 30 in to the apex at 17.5 | `estimated` | `engine_height` still fixes the head's top at 400 and the apex **is** that top — the deck came down 7 mm to buy the crown rather than the head growing. The six nuts top out at 400 exactly, level with it |
| **plug bore** | **13.5 deep**, Ø35 mouth to Ø24 floor | `estimated` | a two-stroke plug goes down a bore; it was standing on a boss above a flat plate. Measured **along the bore axis** off the built mesh — a world-z reading compares the rim's highest corner to a point on the axis and says the plug is 4.5 mm *below* the crown |
| plug protrusion | **3.0 mm of hex** above the crown | `derived` | the rest of it is down the hole. Hex circumscribed radius 12.1 against a bore wall at 15.2 at that height |
| combustion chamber volume | ≥11.0 cm³ | `sourced` | Art. 9.10.1, p. 27. Not a mesh dimension; recorded because it is the only *internal* number the regulation fixes |
| plug thread | M14 × 1.25, housing 18.5 long | `sourced` | Art. 5.2.1, p. 15. `PLUG_HEX_FLATS` 21 is the B-series spanner size, `estimated` |
| bolt circle | Ø38, 6 nuts | `estimated` | R2 shows six in spotfaced counterbores |

### `engine_water_outlet`
**Status:** built, **relocated** with the head
**Attaches to:** `engine_head` (bolted), `radiator_hose_upper` (routed)
**Envelope:** Art. 5.3 — one circuit for the case, cylinder and head (9.10.1). §5a for the tubing spec.
**Verification:** gate 2

Elbow mouth at **(295, −190, 360)**, `estimated`: on the head's inboard-rear
quadrant, pointing inboard and rearward, which is the shortest run to a
left-side radiator that does not cross the cylinder. Was (299, −182, 376) against
an unleaned head.

### `engine_water_inlet`
**Status:** **new**
**Attaches to:** `engine_crankcase_upper` (welded — cast boss), `cooling_hose_pump_engine` (routed)
**Envelope:** Art. 5.3, §5a
**Verification:** gate 2

Boss Ø24 × 11 proud, centre **(258, −330, 165)**, mouth **(244, −330, 165)**,
`estimated`: on the crankcase's inboard face, low, because Art. 9.10.1 water-cools
the **crankcase** as well and the coolant has to get in somewhere. Before it
existed the lower hose ended on the clutch cover.

Two corrections in one row. This section said **(240, −300, 175)** and the build
has been at y −330, z 165 since the part landed — a drift nobody caught, because
no gate reads this file; the built figures are the ones above. And x moved 240 →
**258** with #212: the boss sits 85 mm back from the case's centre, which is deep
in the rear corner, and once the casting had corner radii the wall there was at
255 while the boss still ended at 240. It was standing 15 mm off the casting it is
cast into. `PORT_MOUTHS["engine_inlet"]` is the same point less the boss's own 14
and moves with it.

That move **fixed a #190 waiver nobody was aiming at**: the neck bolted to this
boss went outboard with it and left `engine_battery`, so
`engine_battery`/`engine_inlet_neck` is deleted and known-open drops 15 → 14. The
two that remain on the battery still want the same cure this section already
names, which is moving the battery forward.

## 30.5 Driveline, the chain line, and the guard

### The interference the exhaust notes could not resolve does not exist

`notes_exhaust.md` §4.3's last row is *"chain and axle sprocket: NOT CHECKED […]
the header stub sits at x 300-330 […] which is exactly where a right-side chain
run lives. This is the one interference a spec-writer must resolve before
building."* It is resolved by measurement, and the answer is that **nothing
moves**:

    chain plane          x  +115         (band x 110.5 .. 119.5)
    header stub          x  294 .. 344   (y -328 .. -463, dia 44.5 -> 50.8)
    minimum gap          89.6 mm         measured over the whole pipe, 5 mm
                                         sampling, against the band and both
                                         sprocket wraps
    to the axle sprocket 52 mm           nearest-surface, Ø145 disc at x 111..119

A chain plane at 300–330 is not possible on this kart or on a real one: the
plane must contain the axle sprocket, and an axle sprocket 300 mm from the
centreline sits outboard of the outer bearing hanger at x 185. The notes' worry
came from reading a plan-view *x* for the pipe and assuming the chain shared it.
**The 12° of mount yaw §4.1 introduced "to buy sprocket room" is therefore not
needed, and it is deleted** — see §30.6, where a square-mounted engine is also
the only thing that makes the crank parallel to the axle.

### `drive_output_sprocket`, `engine_sprocket` (pivot)
**Status:** built; **teeth, pitch diameter and y all corrected**
**Attaches to:** `drive_output_shaft` (pressed), `drive_chain` (meshed)
**Envelope:** Art. 9.18.1, PDF p. 31 — *"The chain and sprockets are free."* So there is no envelope, and 219 is practice.
**Verification:** gate 1 (`drive_chain`/`drive_output_shaft` waiver, 112 pairs), gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| pitch | **5.5626** | `derived` | 219 = 0.219 in × 25.4. The *choice* of 219 for KZ is `estimated` practice — Art. 9.18.1 leaves it free — corroborated by Art. 9.18.2 making 219 compulsory in every OK class, i.e. it is the karting standard |
| teeth | **12** | `estimated` | KZ front sprockets run 10–14; 12 with 82 gives 6.83:1, which is the middle of KZ final drive |
| **pitch diameter** | **21.49** | `derived` | `p / sin(pi/N)` = 5.5626 / 0.258819. **The module computes `p*N/pi` = 21.25, which is 1.1% small.** The approximation is harmless at 82 teeth (0.03%) and is not at 12 |
| centre (x, y, z) | **115, −268.5, 150** | `derived` | x is the chain plane; z is `engine_z`, the crank axis, because the gearbox output and the crank are at the same height on a kart engine; y is set by the chain's whole-pitch count below (was −268) |

### `drive_chain`
**Status:** built, **length corrected**
**Attaches to:** `drive_output_sprocket` (meshed), `axle_sprocket` (meshed)
**Envelope:** Art. 5.9 requires the guard, not the chain.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| axle sprocket teeth | **82** | `derived` | §Running gear builds `axle_sprocket` at Ø145; `p/sin(pi/82)` = **145.23**, so 145 *is* an 82-tooth 219 sprocket to 0.2 mm. The existing number was right and now has a tooth count behind it |
| centre distance | **256.5** | `derived` | solved for a whole even pitch count, below |
| chain length | **142 pitches = 789.9** | `derived` | `2*sqrt(C² - ΔR²) + R_big*(pi + 2*asin(ΔR/C)) + R_small*(pi - 2*asin(ΔR/C))` with R 10.745 / 72.615: 790.87 at C = 257.01, which is 142.18 pitches. A chain is an even whole number of pitches, so C comes back 0.49 mm to 256.5 and `SPROCKET_Y` to −268.5 |
| wrap angles | 152.1° small, 207.9° large | `derived` | `pi ∓ 2*asin(61.87/256.5)` |
| band section | 9 × 8 | `estimated` | 219 roller chain, modelled as a flat band — a swept circle reads as a bungee |

### `drive_output_shaft`, `drive_sprocket_carrier`
**Status:** built, unchanged in size; the shaft's overlap with the chain is a real fault
**Attaches to:** shaft/sprocket (pressed), shaft/carrier (pressed), carrier/`engine_clutch_cover` (pierced), carrier/`engine_clutch_bell` (pierced), carrier/`engine_crankcase_*` (pierced)
**Envelope:** none
**Verification:** gate 1 — two waivers, 112 and 28 pairs

Shaft Ø18 spanning x 100–185; carrier Ø64 spanning x 180–245. The shaft's Ø18 is
1.9 mm inside the chain's band because the pitch radius is 10.745 and the
approximate formula gave 10.62: with the correct pitch diameter the chain's
inner strand sits **1.0 mm** clear of a Ø18 shaft, which is still not enough.
**The shaft must reduce to Ø16 outboard of x 130** (`estimated`), giving 2.7 mm,
or the sprocket must go to 13 teeth (PD 23.3). Ø16 is the smaller change and is
what is specified. The `engine_clutch_bolt_4` intrusion is the same fault seen
from the cover side and clears with it.

### `drive_chain_guard`
**Status:** **new** — and it is compulsory
**Attaches to:** `chassis_cross_rear` (bolted, inboard flange at x 103 down to z 65), `axle_rear` (pierced), `drive_output_shaft` (pierced)
**Envelope:** Art. **5.9**, PDF p. **17**, verbatim: *"A chain guard is mandatory in all classes. Chain guards may be made of composite material. […] In gearbox classes, the chain guard must cover the sprocket and the crown wheel down to the centre of the crown wheel axis."*
**Verification:** gate 1 (must **not** touch `drive_chain`, `axle_sprocket` or `drive_output_sprocket`), gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| lateral span | x 101 … 133, 2 mm wall | `derived` | the chain band is 110.5–119.5 and both sprockets sit inside it; 7.5 mm of clearance a side, which is the smallest gap a composite guard can hold without rubbing |
| fore-aft span | y −258 … −610 | `derived` | from the small sprocket's front (−268.5 − 10.7) to the crown wheel's rear (−525 − 72.6), plus 10 mm each end |
| lower edge | z **147.5** | `sourced` | Art. 5.9 — *"down to the centre of the crown wheel axis"*, and the axle centre is `rear_axle_z` = 147.5 |
| crown | 82 mm from the axle centre over the crown wheel (z 230), 22 mm above the chain elsewhere | `derived` | 9.4 mm over a Ø145 sprocket |
| material | composite | `sourced` | Art. 5.9, p. 17 |
| mounting flange | x 103, y −525, z 147.5 → 65 | `estimated` | inboard of the crown wheel's 111 by 8 mm; lands on `chassis_cross_rear`, which Art. 4.2.3 already contemplates as a welded attachment point |

**It is pierced by the axle and by the output shaft, and that is correct** — the
guard is cut around both. Declaring them is what stops gate 1 reading a
compulsory part as a collision.

## 30.6 Exhaust

### 30.6.1 The port faces rearward. Settled, three ways.

1. **Art. 5.10, PDF p. 17, verbatim:** *"It is mandatory for the exhaust to pass
   rearward and not cross the plane defined by the driver seated in the normal
   driving position."* A regulation, read in the pinned text. Combined with a
   pipe that bends through only 95°, a forward-facing port cannot comply.
2. **The photograph.** `tonykart_racer401T_p05.jpg`: the intake silencer is on
   the engine's **forward** face, and the exhaust joint, its two retaining
   springs and the pipe are all on the **rear** face. This is a topological
   reading — which face a part is on — and it survives the fact that p05 is a
   high front three-quarter and not a plan view. `exh_koene_tk401rr_kz.jpg`, an
   actual KZ, agrees.
3. **Arithmetic.** A single-plane 95° bend with a 552 mm chord cannot take a
   forward-pointing inlet to a rearward-pointing outlet; that needs ~180°. Both
   degenerate cases were computed: the stinger lands 550 mm outboard of the kart
   or 430 mm below the ground.

`powertrain.py`'s module docstring says the opposite in two paragraphs and
reasons from it — *"there is nowhere for it to go […] getting this backwards puts
the pipe in the tire and the airbox in the radiator."* The premise was wrong and
the conclusion was too: rearward is the only family that closes, and the airbox
is what has to move.

### 30.6.2 The pipe — 15 cones, and every one of them sourced

Art. **9.15.1**, PDF p. **30**: *"All KZ engines must be fitted with the exhaust
homologated with the engine and described in the engine´s HF."* That makes the HF
table normative, and two of them were retrieved: TM KZ-R1 `041-EZ-75` and
KZ-R2 `041-EZ-02`. Each carries both diameters and both slant lengths of all
15 cones. R1 figures; the R2's stated internal volume and mass are within 2%.

Two checks that the transcription is right rather than plausible: summing the 15
frusta and insetting the wall reproduces the HF's own stated internal volume at
1.07 mm (R1) and 1.29 mm (R2); and `outer − inner slant = theta * D_mean` summed
per cone gives 95.3° of total bend, against a photograph on the facing page
showing about a right angle.

Sampling (part 4 of the sculpt wave, no dimension moved): 24/48 radial segments
and 8/24 path steps per cone at low/high detail, up from 16/32 and 4/8 — the
shipped glb is the low mesh and baked normals cannot fix silhouettes, so the
~Ø112 belly's 22 mm silhouette flats were geometry, not shading. The centerline
walk is arc-length exact at any density, so the developed 674.6 mm is invariant
under the change.

| # | s start | s end | dia start | dia end | what |
| --- | --- | --- | --- | --- | --- |
| 1 | 0.0 | 67.7 | **44.5** | 47.0 | header stub; sensor boss at 25, spring tabs at ~70 |
| 2 | 67.7 | 101.2 | 47.0 | 49.0 | stub |
| 3 | 101.2 | 134.7 | 49.0 | **50.8** | end of the straight — **the bend starts here** |
| 4 | 134.7 | 157.8 | 50.8 | 55.7 | diffuser 1 |
| 5 | 157.8 | 180.9 | 55.7 | 61.0 | diffuser 2 |
| 6 | 180.9 | 212.8 | 61.0 | 70.3 | diffuser 3 |
| 7 | 212.8 | 244.6 | 70.3 | 79.8 | diffuser 4 |
| 8 | 244.6 | 276.4 | 79.8 | 89.0 | diffuser 5 |
| 9 | 276.4 | 308.3 | 89.0 | 98.3 | diffuser 6 |
| 10 | 308.3 | 340.1 | 98.3 | 107.4 | diffuser 7 |
| 11 | 340.1 | 409.0 | 107.4 | **136.5** | opens hard into the belly |
| 12 | 409.0 | 472.5 | 136.5 | 135.0 | **the belly**, effectively cylindrical |
| 13 | 472.5 | 513.1 | 135.0 | 114.5 | first baffle cone, shallow |
| 14 | 513.1 | 622.6 | 114.5 | 55.8 | main baffle cone, the long one |
| 15 | 622.6 | 674.6 | 55.8 | **26.3** | final cone to the stinger |

All 15 rows `sourced`. Per-cone turn in degrees, `derived`: 0, 0, 0, 8.93, 7.95,
8.99, 10.08, 10.05, 9.97, 10.03, 8.13, 6.08, 6.29, 6.06, 2.79.

Headline scalars, replacing three parameters that match nothing:

| quantity | value | prov | against the build |
| --- | --- | --- | --- |
| developed centreline length | **674** | `derived` | `exhaust_length` 620 — **54 short** |
| max belly diameter | **136.5** | `sourced` (`phi N`) | `exhaust_max_diameter` 130 — **6.5 small** |
| header diameter at the inlet face | **44.5** | `sourced` (`phi A`) | `exhaust_pipe_diameter` 34 — matches nothing in the real pipe |
| stinger exit | **26.3** | `sourced` (`phi R`) | absent |
| baffle start | **114.5** | `sourced` | absent |
| wall thickness | **1.0** (solve 1.07–1.29) | `derived` | absent. Art. **5.10**, p. 17: *"The exhaust must be made of magnetic steel in all categories. Minimum sheet metal thickness is 0.75 mm if not otherwise specified in the HF."* |
| mass | 1130 g minimum | `sourced` | both HFs |
| total bend | 95° | `derived` | — |
| chord, inlet to exit | 552 | `derived` | — |

**Build it as a sequence of cones, not as a profile curve.** `EXHAUST_PROFILE`'s
nine ratio points are a smooth silhouette; a KZ chamber is 15 straight-sided
frusta with visible weld beads at the joins, and that faceting *is* the shape.
The cone table belongs in `powertrain.py` as `EXHAUST_CONES` — it is a shape,
not a scalar, and §00's single-owner rule puts scalars in `params.py` and shapes
in the module that builds them.

### 30.6.3 Routing — a construction, and it says so

    inlet face          (319, -328, 285)   derived, §30.4
    inlet axis          (0, -1, 0)         derived: 0 deg in plan and 0 deg in
                                           elevation.  Plan 0 because the port
                                           is square in plan and the crank must
                                           be parallel to the rear axle for the
                                           chain to run at all, so the engine is
                                           square on its mount and there is no
                                           yaw to inherit.  Elevation 0 by the
                                           25 deg lean, §30.4.
    bend-plane tilt     12 deg nose-down   estimated
    bend turns          inboard (-x)       derived: outboard puts the belly in
                                           the right rear tyre

**No accessible photograph anywhere shows a KZ expansion chamber fitted to a
kart** — every manufacturer display kart in `refs/` is shot without one. So §30.6.3
is a **construction from sourced part geometry plus regulation constraints**, not
a measurement, and none of it should be read as though a photograph stood behind
it. The one document that would settle it is named in §30.10.

The tilt is `estimated` and its family is wide. Measured across the family
(chamber floor / crown / stinger exit z): 0° → 217 / 353 / 285; 8° → 185 / 330 /
225; **12° → 169.5 / 318.4 / 195**; 16° → 152 / 316 / 166; 20° → 124 / 314 / 137;
26° → 82 / 313 / 95. 12° is chosen because it is the shallowest tilt that leaves
the silencer body (Ø120 on the stinger's axis) 130 mm of ground clearance —
comfortably above the 25–60 mm the rear bodywork itself runs at — while keeping
the chamber's crown 131.6 mm under Art. 5.10's ceiling. The crown's maximum is
**not** a function of the tilt: it peaks at 318 in the diffuser at s ≈ 213 where
the bend has barely started, so it is set by the sourced port height and cannot
be tuned away.

Centreline in the kart's frame, `derived` from the cone table and the three
placement rules above. Diameter is the outside of the sheet.

| s | x | y | z | dia | what |
| --- | --- | --- | --- | --- | --- |
| 0 | **319.0** | **−328.0** | **285.0** | 44.5 | inlet face, on the manifold spigot |
| 68 | 319.0 | −395.7 | 285.0 | 47.0 | spring tabs |
| 101 | 319.0 | −429.2 | 285.0 | 49.0 | stub |
| 135 | 319.0 | −462.7 | 285.0 | 50.8 | **bend starts** |
| 158 | 317.6 | −485.8 | 284.7 | 55.7 | |
| 181 | 313.0 | −508.3 | 283.7 | 61.0 | |
| 213 | 302.3 | −538.2 | 281.4 | 70.3 | crossing the rear axle line, 69.8 above it |
| 245 | 286.9 | −565.8 | 278.2 | 79.8 | |
| 276 | 267.0 | −590.3 | 273.9 | 89.0 | inboard of the right rear tyre |
| 308 | 243.2 | −610.8 | 268.9 | 98.3 | |
| 340 | 216.4 | −626.9 | 263.2 | 107.4 | |
| 409 | **153.6** | **−651.5** | **249.8** | 136.5 | **belly front** |
| 472 | **93.2** | **−666.4** | **237.0** | 135.0 | **belly rear** |
| 513 | 53.8 | −671.6 | 228.6 | 114.5 | baffle cone starts; the support grips here |
| 623 | −38.6 | −674.0 | 205.9 | 55.8 | crossing the kart centreline |
| 675 | **−104.0** | **−670.7** | **195.1** | 26.3 | **stinger exit** |

Exit direction `(−0.976, +0.063, −0.208)` — almost straight to the kart's left,
very slightly forward, 12° down.

Clearances, `derived`, computed as a swept circle sampled every 0.34 mm of arc
against every hard point in this build:

| to | figure |
| --- | --- |
| right rear tyre, inner face x 485 | **139.5** |
| rear axle, top z 172.5 | **69.8** |
| axle sprocket, Ø145 disc at x 111–119 | ~52 |
| chain band, x 110.5–119.5 | **89.6** |
| ground | **169.5** |
| Art. 5.10 ceiling, 450 | **131.6** of margin, crown at 318.4 |
| `seat_shell`, x ±184 | 108 |
| `chassis_floor_tray`, top z 69 | 164 |
| `chassis_cross_tail` / `chassis_cross_rear` | 114 / 152 |
| `engine_crankcase_upper` rear housing, top z 255 | **7.8** — the tightest pair on the pipe |
| `engine_reed_block` / `engine_battery` | 39 / 30 |
| `engine_carb_cap` | 10, **after lowering the carburettor 12 mm** — see §30.6.6 |
| rearmost point | y **−733.9**; §Bodywork's rear protection band starts at −687 |

### `exhaust_chamber`
**Status:** built, **rebuilt from the cone table and re-routed**
**Attaches to:** `engine_cylinder` and `engine_cylinder_base` (seated, spigot in the port), `exhaust_manifold` (seated, slip joint), `exhaust_spring_?` (clamped), `exhaust_hanger_cradle` (clamped), `exhaust_connector` (seated)
**Envelope:** Art. 5.10 (magnetic steel, ≥0.75 mm, rearward, ≤450 above the ground, must not cross the driver's plane), Art. 9.15.1 (the HF's pipe). PDF pp. 17, 30.
**Verification:** gate 1, gate 2, `genkart.sh --check`

### `exhaust_manifold`
**Status:** **renamed from `exhaust_flange`**, and its shape changes
**Attaches to:** `engine_cylinder` (bolted, 4× M6 on 62 × 44), `engine_cylinder_base` (bolted), `exhaust_chamber` (seated), `exhaust_manifold_bolt_0..3` (threaded), `exhaust_spring_?` (clamped)
**Envelope:** Art. 5.10
**Verification:** gate 1, gate 2

`notes_exhaust.md` §1 is unambiguous: **there is no flange on the pipe.** A short
manifold bolts to the cylinder and the pipe slips over its spigot on springs. So
the part is a manifold, not a loose ring:

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| length, cylinder face to pipe inlet face | **28** | `sourced`, flagged | kartshop sells the TM KZ manifold as "D2 28 / 29 / 30.5" and calls the number the length; three options 2.5 mm apart is a length shim, not a restrictor family. Flagged because on Vortex ROK the same designation is a bore |
| spigot OD | ~43 | `estimated` | the pipe's inlet bore is 44.5 `sourced`; a slip joint needs a few tenths plus room for carbon |
| flange plate | ~78 × 60 × 8 | `estimated` | a 62 × 44 bolt rectangle plus M6 heads and edge. The real part exists (kartshop *"Exhaust Gasket, R3 / R2 / R1 / KZ10"*) and nobody publishes its dimensions |
| bolts | **4× M6 × 20** | `sourced` | as above. **The build has two nuts; it needs four**, and `exhaust_flange_nut_?` becomes `exhaust_manifold_bolt_0..3` |

### `exhaust_spring_0`, `exhaust_spring_1`
**Status:** built, unchanged in count, **repositioned** to the tabs at s ≈ 70
**Attaches to:** `exhaust_chamber` (clamped), `exhaust_manifold` (clamped), `exhaust_manifold_bolt_0`/`_1` (clamped)
**Envelope:** none
**Verification:** gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| count | **2** | measured | KZ-R2 HF p. 13 at 500 dpi: two bent-tab hooks side by side; p05 shows two fitted |
| free length | **70** | `sourced` | eurokart, *"TM K9/K9B/K9C KZ10/10B/KZ10C/KZ-R1 exhaust spring 70mm KZ"* |
| wire / coil | 2.5 / 12 | `estimated` | proportion off HF p. 13 against the pipe's own 47 mm OD at that station |
| tab station | s ≈ 70, ±15% | measured | HF p. 13, scaled on the pipe's sourced 46.5 mm OD, 0.146 mm/px, perspective view |
| articulation | ~3° cone, ~5 mm axial, pivoting about (319, −316, 285) | `derived` | what a 25 mm slip joint with 70 mm springs allows. **This is why the chamber cannot be rigid to the engine** and why the support downstream is a spring cradle |

### `exhaust_silencer`
**Status:** built, **relocated and resized**
**Attaches to:** `exhaust_connector` (seated), `exhaust_silencer_band_0`/`_1` (clamped), `exhaust_silencer_saddle` (seated)
**Envelope:** Art. **9.16.1**, PDF p. **30**: *"Use of a CIK-FIA homologated exhaust silencer is mandatory. Fitting of the exhaust and silencer must be done according to TD n°2.7."* Art. **5.10**, p. 17: the outlet's external diameter *"must be more than 3 cm"* and *"must not exceed the outer limits of the kart"*, and the system *"must discharge behind the driver"*.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| overall length | **450** | `sourced` | Elto ICC/KZ silencer, listed 450 × 120 × 29 |
| body diameter | **120** | `sourced` | same listing, corroborated by the retaining hardware being *"large jubilee clip for exhaust silencer x2, 120-140mm"* |
| body length | ~390 | `estimated` | 450 less a ~30 mm spigot each end |
| inlet spigot | **29** | `sourced` | same listing; takes the 26.3 stinger through the connector |
| outlet | **32** | `sourced` (the floor) / `estimated` (the value) | Art. 5.10's >30; 32 is the smallest round tube above it |
| body axis | along **x** at y **−760**, z **190** | `estimated` | transverse because the clear box behind the axle is ~365 mm deep and a 450 mm can cannot lie fore-and-aft in it; the stinger already points along −x, i.e. along the body axis |
| body span | x −140 … +250; spigots to −170 and +280 | `estimated` | offset right so the U-bend from the stinger stays short. Outlet at x 280 is 420 mm inside the kart's 700 limit — Art. 5.10 satisfied |
| ground clearance | 130 | `derived` | 190 − 60 |
| material | aluminium or steel body, glass-fibre packing | `sourced` | dealer listings offer both, plus *"glassfibre for Elto silencer TD-3"* as a service item |
| an alternative family | 89 × 349 | `sourced` | MC Racing KZ/ICC. Recorded so nobody reads 120 × 450 as the only shape; the sourced clamps fit the 120 |

**Interface for §Bodywork, as a number:** the rear protection's inner clear
volume must contain the chamber at **x −117 … 346, y −652 … −734, z 170 … 318**
and the silencer at **x −170 … 280, y −700 … −820, z 130 … 250**. Its own front
face lands at y −687 … −722 (Art. 9.5.5.1's 15–50 mm behind the rear tyres), so
both parts are inside its fore-aft band by construction and the shell has to be
cut for them rather than in front of them.

### `exhaust_connector`
**Status:** **new** — the U-bend
**Attaches to:** `exhaust_chamber` (seated), `exhaust_silencer` (seated)
**Envelope:** Art. 5.10
**Verification:** gate 1, gate 2

Ø30 × 1.0 magnetic steel, from the stinger exit (−104, −671, 195) out to an apex
near (−205, −715, 192) and back into the silencer's inlet spigot at (−170, −760,
190). ~200 mm developed. `estimated`, but the *part* is `sourced`: the catalogues
sell a **"muffler bent pipe" / "exhaust with U-bend"** as a separate item, which
is exactly what turns a leftward stinger back into a rightward can.

### `exhaust_hanger_clamp`, `exhaust_hanger_arm`, `exhaust_hanger_cradle`
**Status:** **replaces `exhaust_hanger`**, which is bolted to nothing — #192 measures it **14.39 mm** off `chassis_side_bar_r`
**Attaches to:** clamp/`chassis_cross_rear` (clamped, Ø30.0 bore), clamp/arm (bolted through a slot), arm/cradle (bolted), cradle/`exhaust_chamber` (clamped)
**Envelope:** Art. 4.2.5 lists *"exhaust and exhaust silencer holder"* as a chassis component; Art. 4.2.3 puts its welded attachment point on the frame. PDF p. 8. No dimension.
**Verification:** gate 2 — the pair that is 14.39 mm today

**Why the side bar cannot be the anchor.** `chassis_side_bar_r` is swept at
`tube_bumper` = **20 mm**. The sourced mushroom clamp is offered in 28 / 30 / 32
and `notes_exhaust.md` §6.3 matched it to `tube_main` = 30. The 30 mm tubes on
this kart are `chassis_rail_*`, `chassis_cross_front` and `chassis_cross_rear`,
and of those only the rear cross member is within reach of a rearward pipe.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| clamp tube | **`chassis_cross_rear`**, Ø30, at **x +54, y −525, z 50** | `derived` | the only 30 mm tube within 200 mm of the pipe. x 54 is directly under the grip point |
| clamp bore | **30.0** | `sourced` | the mushroom clamp family is 28/30/32 and `tube_main` is 30 |
| clamp body | ~46 OD × 30 long, split, two screws, Ø20 mushroom boss standing 15 proud (top at z 88) | `estimated` | proportions off `exh_eurokart_6.jpg` against the 30 mm bore |
| arm | 25 × 4 carbon plate, two 30 mm slots, **169 mm** long | `estimated` (section) / `derived` (length) | from the boss top (54, −525, 88) to the pipe's underside (54, −671.6, 171.4). `notes_exhaust.md` estimated ~150 from a photograph with no dimensioned feature; the geometry says 169 |
| cradle | spring, **Ø12 × 130 free**, round the pipe at **s = 513, (53.8, −671.6, 228.6)**, dia 114.5 | `sourced` (the spring) / `estimated` (the station) | eurokart *"exhaust cradle spring D.12mm L.130mm"*. The baffle cone is the only part of the pipe both stiff enough to clamp and reachable; real installations clip the cone, not the belly |
| count | **1** support | `estimated` | one arm, one clip, one clamp is what the catalogue sells as a set, and a second would over-constrain a pipe that must articulate at the slip joint |

Arm clearances, `derived`: 81 mm over `chassis_cross_tail`, 20 mm under the rear
axle, 34.5 mm from the clamp body to the axle's underside, clear of the crown
wheel at x 111–119 by 34 mm.

### `exhaust_silencer_saddle`, `exhaust_silencer_band_0`/`_1`, `exhaust_silencer_isolator`, `exhaust_silencer_bracket`
**Status:** **new**
**Attaches to:** bracket/`chassis_cross_rear` (clamped, Ø30 bore at x +150), bracket/isolator (bolted, M8), isolator/saddle (bolted), saddle/`exhaust_silencer` (seated), bands/silencer (clamped), bands/saddle (pierced — the band threads through the saddle's slot)
**Envelope:** Art. 4.2.5, as the hanger
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bands | **2**, 120–140 range | `sourced` | eurokart *"large jubilee clip for exhaust silencer x2"*; `exh_eurokart_5.jpg` shows the pair |
| band stations | x −50 and +160 | `estimated` | ~90 mm in from each end of the 390 body, where the saddle upstands land in `exh_eurokart_3.jpg` |
| saddle | ~110 wide, 70 upstands, cradle r 60 | `estimated` | proportions off `exh_eurokart_3.jpg` against the 120 body |
| isolator | rubber bush Ø28 × 12 on an M8 stud | `estimated` | same photograph; the bolt is clearly an M8 hex |
| bracket | x +150 on `chassis_cross_rear`, 242 mm to the saddle base at (150, −760, 130) | `estimated` | 96 mm outboard of the pipe support's clamp so the two do not collide on the same tube; passes 62 mm under the chamber and 19 mm outboard of the crown wheel |

### 30.6.6 What the exhaust displaces

The pipe now occupies the volume the intake was built in. Three parts move, and
their numbers are `estimated` packaging choices with the clearances stated.

| part | was | becomes | why, and the resulting clearance |
| --- | --- | --- | --- |
| `engine_airbox` / `_lid` | z 280 … 400 | **z 340 … 460**, x and y unchanged (250…420, −567…−452) | the pipe passes through x 245–344 at z 238–325 there. Raised 60 mm: **15 mm** over the pipe's crown. Clear of the driver's shoulder (x ±200) by 50 mm and of the right rear tyre's inner face by 65 |
| `engine_carb` and its cap | axis z 205, cap top 262 | axis z **193**, cap top **250** | the cap at 262 fouls the pipe's underside at 260 by 2 mm. The port height is `derived` from the HF and does not move, so the carburettor does. Result: **10 mm**. The bowl drops to z 140–172, still 71 mm over the tray |
| `engine_intake_boot` | straight carb-to-airbox | **(312, −440, 193) → (255, −443, 193) → (250, −460, 330) → (290, −455, 385)** | the boot has to cross the pipe's z band and can only do it inboard of the pipe: at x 250 ± 34 it clears the pipe's inboard face (294) by **9 mm** and the seat shell (184) by 32 |

### `engine_airbox`, `engine_airbox_lid`, `engine_airbox_duct_0`/`_1`
**Status:** airbox and lid built and **relocated**; the two ducts are **new**
**Attaches to:** lid (bolted), `engine_intake_boot` (routed), ducts (welded)
**Envelope:** Art. **9.13.1**, PDF p. **30**, verbatim: *"They must have two ducts with a 30.0 mm maximum diameter."* Art. **5.5**, p. 16, and Art. 9.13 require the intake silencer to be CIK-homologated in Group 2 at all.
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| box | 170 × 115 × 120 at x 250…420, y −567…−452, z 340…460 | `estimated` | as above |
| **ducts** | **2**, Ø **30.0**, at x 290 and 350, z 350, projecting forward from y −310 to −265 | `sourced` (count and diameter) / `estimated` (position) | Art. 9.13.1 is a maximum and every KZ silencer runs it. 45 mm long leaves 8 mm to the leaned head's rearmost point at y −257 |
| boot bore | 68 over the carburettor's 64 mm spigot | `estimated` | unchanged |

## 30.7 Cooling

Art. 5.3, 5.3.1 and 5.3.2 are quoted in full in **§00 §5a** and are not repeated.
Art. **9.17**, PDF p. **31**, adds *"In all classes, only one cooling circuit for
the engine and radiators is allowed"* and limits **OK** — not KZ — to one
radiator.

### 30.7.1 The lateral dispute, settled: x −365, width 250

Four independent readings, and they do not all deserve equal weight.

| reading | core centre x | core width | outer edge | viewpoint |
| --- | --- | --- | --- | --- |
| EM-Technology EM-01 catalogue part, published 250 × 435 × 40 | — | **250** | — | a dimensioned part, no viewpoint at all |
| `tonykart_rear_header.jpg`, dead rear, 1.0409 mm/px | **−364** | 234 ±12 | 481 | **dead rear: lateral is perpendicular to the view axis and is not foreshortened. ±3%** |
| `crg_roadrebel_kz_front.webp`, dead front, 1.807 mm/px (§Bodywork) | — | — | **479** | dead front: same good case |
| `tonykart_racer401T_p05.jpg` (exhaust agent) | −407 | 267 | 540 | high front three-quarter, proved not a plan view — 34° off vertical, and lateral readings need a radial displacement correction this one did not get |

**Verdict: centre x = −365, core width = 250, lateral extent x −240 … −490.**

The reasoning, in order of weight:

1. **A catalogue part with a published dimension outranks any photograph.** 250 ×
   435 × 40 is the EM-01; the KZ family runs 240 (New-Line RS MAX), 245, 250,
   265 (EM-09 PRO), 290 (EM-02). `radiator_width = 0.265` is the largest core in
   the catalogue, chosen because it filled the gap between the seat edge and the
   side bar — a fit argument wearing a part dimension, which is the exact failure
   `length_overall = 1830 mm max` was written up for.
2. **Both dead-on views agree and the three-quarter does not.** Predicted outer
   edge at 365 + 125 = **490**; measured 479 (dead front) and 481 (dead rear),
   i.e. −11 and −9. Predicted at 407 + 133.5 = **540.5**; the same two
   measurements are 61 and 59 mm away. The 265-width variant predicts 497.5,
   which is 18.5 from the front-view figure against 11 for the 250.
3. **The 407 reading is p05's uncorrected class.** `notes_radiator.md` §7 reads
   the same core in p05 at −399 raw and corrects it to **−371** for a 200 mm
   mid-height, 6 mm from the dead-rear answer. The correction is the whole
   difference; −407 is what the frame gives before it.
4. **Art. 5.3.1's own margin agrees.** At 365/250 the outboard edge is 490
   against the 550 limit — 60 mm. At 407/267 it is 540.5, **9.5 mm** of margin on
   a figure measured off a foreshortened photograph. A real kart is not built
   there.

**State the lateral extent as its own number, once.** `radiator_x ± radiator_width/2`
= **x −240 … −490**. It is not `radiator_x ± radiator_thickness/2`: the core's
own frame puts `+x` along the face normal, which points **forward**, so 365 + 20
= 385 is a plane 20 mm ahead of the core face and has nothing to do with how far
outboard the core reaches. Anything that needs the outboard face — a pod, a
waiver, a bracket — reads −490 from this line and does not re-derive it.

### 30.7.2 Rake and height, and the coupling that has to break

`radiator_rake_delta`'s docstring asserts that the core sits in the plane a
second seat's back would occupy and that *"that is not an analogy used to explain
the number — it is the number"*. It is an analogy, and it is holding two
different angles in one parameter: the core rakes **40–47° from vertical** and
the seat shell's own chord is **19–26°** (`notes_radiator.md` §6, from Tillett
C = 460 and E = 335). **Delete `radiator_rake_delta`. The radiator gets
`radiator_rake`, its own number, and stops reading `seat_back_angle`.** The
seat's rake belongs to §Cockpit and moving one must not move the other.

    radiator_rake = 45 deg from vertical (= 45 deg from horizontal)   derived

    the one value two ranges share:
      notes_radiator.md's height-budget solve      40 +/- 5 from vertical
      sourced kart practice, tuned in 5 deg steps  45-60 from horizontal
                                                   = 30-45 from vertical
    45 from vertical is the top of the first and the bottom of the second.

Height. `radiator_z = 0.320` puts the core's top at 497 — **3 mm** under the FIA
ceiling, and about 110 mm above where the photographs put it.
`notes_radiator.md` recommends 220. **240 is specified**, and the 20 mm is not a
disagreement but two surfaces the notes' solve did not include:

    vertical half-extent = (radiator_height/2)*cos(rake) + (thickness/2)*sin(rake)
                         = 217.5*0.7071 + 20*0.7071 = 167.9        derived
      the second term is the one the notes' "220 - 435*cos40/2 = 53" omits: the
      core is 40 mm thick and raked, so its lowest point is a tank *corner*,
      12 mm below its centreline edge.

    floors, both exact:
      Art. 5.3.1 "above the chassis frame" -> rail top = rail_z + 15 = 65
      chassis_floor_tray top                                        = 69
      the core spans x -240..-490 and the tray reaches x -280, so the tray is
      genuinely under the core's inboard 40 mm.  It is the binding floor.

    radiator_z = 69 + 3 + 167.9 = 240      derived

| resulting figure | value | check |
| --- | --- | --- |
| lowest point | z **72.1** | 3.1 above the tray, 7.1 above the rail top. Art. 5.3.1 satisfied |
| highest point | z **407.9** | **92 mm** under the 500 ceiling, against 3 mm today |
| fore-aft span | y **−402.9 … −67.1** | Art. 5.3.1's window is −515 … −15 (§5a). Inside, both ends |
| outboard edge | x **−490** | ≥150 from the 700 extremity: **60 mm** of margin |
| inboard edge | x **−240** | the seat shell's outboard face is ±184 (Tillett B 360 + 2×4). **56 mm**, and no joint |

**Residual disagreement, recorded rather than hidden:** the photogrammetric top
edge is 375 ±20, and 408 is 13 mm above that bar. The two exact floors and the
regulation win, because `notes_radiator.md` §7 gives the vertical figure a ±20
dominated by camera elevation and monotone in it — 371 at 24°, 387 at 8° — while
the rail and tray tops are arithmetic. An elevation shallower than 8° closes the
gap on its own.

### `radiator_core`, `radiator_tank_low`, `radiator_tank_high`, `radiator_end_inboard`, `radiator_end_outboard`, `radiator_fin_0..17`, `radiator_divider`
**Status:** built; **`radiator_width`, `radiator_z`, the rake and the tank height all change**
**Attaches to:** core/fins (welded), core/tanks (welded), core/ends (welded), core/divider (welded), divider/tanks (welded), divider/`radiator_fin_4` and `_5` (welded), fins/tanks (welded), ends/tanks (welded)
**Envelope:** Art. 5.3.1 and 9.17, §5a and above
**Verification:** gate 1 — including the **inverted** assertion that no `radiator_*`/`seat_shell` pair may exist at all — gate 2, `genkart.sh --check`

| dimension | `params.py` | value | prov | basis |
| --- | --- | --- | --- | --- |
| core height, up the slant, tanks included | `radiator_height` | **420** | `sourced` | New-Line RS size M is 245 × **420**; the KZ family runs 420–470 (EM-01/EM-02 435, RS MAX 430), so this is its short end. Was 435, and came down with ADR-0065 — standing the core up raises its top and 15 mm of length is what holds it. The older 0.432 was right for the wrong reason: its docstring traced it to "17 in", which nobody sourced |
| core width, across the kart | `radiator_width` | **250** | `sourced` | EM-01. Was 265 |
| core thickness, through the face | `radiator_thickness` | **40** | `sourced` | EM-01 and EM-02 |
| centre x | `radiator_x` | **365**, sign from `RADIATOR_SIDE` = −1 | `derived` | §30.7.1 |
| centre y | `radiator_y` | **−282** | `estimated` | not re-measurable: the only frame that shows it in plan is p05 and its fore-aft is unusable. Retained because it sits comfortably inside Art. 5.3.1's window, and **relabelled** — its docstring's "measured off V4, the plan-view reference" is a citation nothing in this repo can support. Moved 47 mm rearward from −235 by ADR-0065; 98 mm of margin to the axle |
| centre z | `radiator_z` | **270** | `derived` | above, then +30 by ADR-0065 to lift the low tank off `chassis_side_bar_l`. 262 is the bare floor and leaves the bar **1.7 mm**, which fires no gate and is knife-edge; 270 is that floor plus margin. Core top **422**, bottom 118 |
| rake | `radiator_rake` | **40° from vertical** | `derived` | replaces `radiator_rake_delta`. Was 45, on the edge of both bracketing ranges; 40 sits inside both (height-budget 40 ±5, kart practice 30–45 from vertical). **The change from 45 is what closes #190's radiator cluster** — the low tank's fore-aft half-extent is `(height/2) sin(rake)`, 154 mm at 45 and 140 at 40, and 14 mm walks it clear of the side-bumper socket. ADR-0065 |
| yaw in plan | — | **0° ±3** | `derived` | p05: the core's inboard and outboard edges are parallel to within 7 px over 178. The fin face points **forward**, the way the driver does |
| tank height | `RADIATOR_TANK_HEIGHT` | **22** ±6 | `estimated` | the polished band above the fin block reads ~16 px in the dead-rear shot, foreshortened by rake and elevation, so the true height is larger; cross-checked against the CRG close-up. Was 30. Fin block becomes **391** |
| tank proud of the core, each face | `RADIATOR_TANK_PROUD` | 7 | `estimated` | a folded box welded across the tube ends is necessarily thicker than the fin pack |
| end channels | `RADIATOR_END_PLATE` | 12 | `estimated` | unchanged |
| flat-tube pitch | — | 10.4 | `derived` | 38 bright tube lines at a median 10.0 px, 1.0409 mm/px → ~24 tubes across 250 |
| fin pitch, as modelled | `RADIATOR_FIN_PITCH` | 12 | `estimated` | the real pitch is **1.8 ±0.5** (`estimated`, unresolvable below ~2 mm at 1.04 mm/px, checked against 12–16 fins/inch practice) and 288 fins cost more than the rest of the kart. 12 mm is where the pattern still reads at cockpit range without shimmering at chase range; issue #19's bake is the designed answer above it. With width 250 the count is **18**, and the divider at −0.44 of the half-width still lands between fins 4 and 5, so joints.py's two explicit rows stay correct |
| divider | `RADIATOR_DIVIDER_*` | at −0.44 of the half-width, 11 thick, 5 proud | `sourced` (that it exists) / `estimated` (dimensions) | a New-Line KZ core is double-pass; the welded rib splitting the face is the single most recognizable thing about one |

### 30.7.3 Hose ports — six of them, and they are parts

Every hose termination on the kart used to be a swept tube arriving at a casting
and stopping, with its last centimetre inside the part it fed. ADR-0064 has the
full argument; the short version is that all six were declared `kind="routed"`,
which is the kind for a hose that *lies against* something, so the contact gate
was satisfied by an 18 mm overshoot.

Each port is now two parts, `<port>_neck` and `<port>_hose_clamp`:

| dimension | constant | value | prov | basis |
| --- | --- | --- | --- | --- |
| neck collar Ø | `NECK_ROOT_DIAMETER` | 30 | `estimated` | the brass barbs in `eng_tm_kz10_dress.jpg` all show a root collar wider than the shank the hose grips |
| neck shank Ø | `NECK_SHANK_DIAMETER` | 21 | `derived` | `HOSE_DIAMETER`'s *sourced* 20 mm ID plus a millimetre so the rubber grips |
| neck length | `NECK_LENGTH` | 20 | `estimated` | must carry the clamp on hose that is over shank |
| neck exposed | `NECK_EXPOSED` | 8 | `estimated` | what the hose does **not** cover. Without it the neck is 100% inside the hose and the fitting still does not read — the step is the whole point |
| clamp station | `CLAMP_ALONG` | 16 | `estimated` | behind the barb ridge, clear of the collar |
| clamp band | `CLAMP_WIDTH` / `CLAMP_PROUD` | 9 / 1.5 | `estimated` | a 9 mm worm band is the trade size for a 20–32 range; read off the same photograph |
| screw housing | `CLAMP_HOUSING` | 15 × 11 × 9 | `estimated` | band and housing are **one mesh**, because a worm clamp is one item and without the housing it reads as a ferrule |
| lead-out | `HOSE_PORT_LEAD` | 34 | `derived` | `NECK_LENGTH` + `CLAMP_ALONG` + `CLAMP_WIDTH`/2, so the band lands on straight hose |

`PORT_MOUTHS` carries each port's mouth and outward axis — one line per port,
because every one of them was previously a hose control point sitting at a boss's
**centre**. The pump's two are now correct for a centrifugal pump: **axial
suction on the end face, radial discharge on the flank**; the inlet was on the
crown. The head's outlet moved to the casting's **rear** face, 43 mm the other
side of `CYLINDER_AXIS_Y`, because the radiator is rearward-left and the hose was
hairpinning forward and back over the head.

A port's collar overhangs the 40 × 22 tank end into the fin block and the end
channel. Declared `welded`: a kart radiator is one furnace-brazed assembly. The
**steel clamp gets no such row** — it is a bolt-on item and keeps its distance.

### `radiator_cap`
**Status:** built, unchanged
**Attaches to:** `radiator_tank_high` (threaded)
**Envelope:** none
**Verification:** gate 2

Neck plus cap, Ø38 × 41 tall, at −0.62 of the half-width, i.e. the **outboard**
third of the high tank. `derived` from the dead-rear shot (cap silhouette peaks
19 px above the tank at x 1090–1160 px) and confirmed by the CRG close-up, which
shows the same cap outboard with an overflow hose looping off it.

### `radiator_curtain`
**Status:** **new**
**Attaches to:** `radiator_end_inboard` and `radiator_end_outboard` (threaded — *"specific double thread screws"* into the side rails)
**Envelope:** Art. 5.3.1, §5a: the fairing/cover system *"may be adjustable, but it must not be detachable when the kart is in motion"*, and the baffles *"must be securely fixed to the radiator(s) with screws. They must be one-piece and may be made of composite material."*
**Verification:** gate 1, gate 2

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| width | **250**, matching the core | `sourced` | Direct-Karting sells a 250 mm curtain for the 250 mm radiator, 290 for the 125 RS, 230 for the X30 big; New-Line's air shield is 25 cm. The curtain **is** the core's width |
| travel | **391**, the full fin block | `derived` | *"height-adjustable system […] limiting the flow of air"*: to blank the core it must span the fin block, 435 − 2×22 |
| slots | two, **55 mm** | `sourced` | New-Line curtain description, for intermediate settings |
| drive | pulley and O-ring the driver pulls | `sourced` | catalogue copy, and it is what makes it adjustable without being detachable |
| thickness / standoff | 2 mm composite, 3 mm proud of the core's forward face | `estimated` | Art. 5.3.1 permits composite; the standoff is a faceting allowance |
| shipped with the radiator | **no** | `sourced` | EM-01 ships *"without shutter blind"*; the blind is a separate €102 line |

### `cooling_pump_body`, `cooling_pump_pulley`, `cooling_axle_pulley`, `cooling_belt`, `cooling_pump_bracket`
**Status:** **`cooling_pump_body` renames and relocates `engine_water_pump`**; the other four are **new**
**Attaches to:** body/bracket (bolted), bracket/`chassis_bearing_hanger_r` (bolted), pump pulley/body (pressed), axle pulley/`axle_rear` (clamped), belt/both pulleys (meshed), body/`cooling_hose_pump_engine` (routed), body/`radiator_hose_lower` (routed)
**Envelope:** Art. **5.3.2**, §5a: *"In Groups 1 & 2, the water pump must be mechanically controlled either by the engine or by the rear wheel axle."* Electric pumps are prohibited in KZ.
**Verification:** gate 1, gate 2

**Delete the three joints binding `engine_water_pump` to `engine_clutch_bell`,
`engine_clutch_cover` and `engine_crankcase_upper`.** The pump is not on the
engine: the KZ trade sells it as *"KZ water pump with HTD axle pulley and tooth
belt"*, Art. 5.3.2 permits either drive, and the axle drive is what the parts
that exist are built for.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| **belt** | **170XL031** — XL profile, 5.08 mm pitch, **431.8 mm** pitch length, **85 teeth**, **7.9 mm** wide | `sourced` (part number) / `derived` (decode) | one trade listing names the belt as CD 170XL 031. 170 = 17.0 in = 431.8 mm; 431.8/5.08 = 85; 031 = 5/16 in = 7.9 |
| axle pulley | PD **65**, clamping the Ø50 axle | `derived` | axle 50 (Art. 9.2, ≤50.0 OD) plus 6–9 mm of clamp wall and flange a side |
| pump pulley | PD **25** (30 as the "slow it down" option) | `estimated` (25) / `sourced` (30) | New Line sells a 30 mm pump pulley *to slow the pump down for larger tracks*, which only reads that way if the stock one is smaller. Ratio ~2.2:1 — the pump spins about twice axle speed |
| **centre distance** | **143.8** | `derived` | `L = 2C + (pi/2)(D1+D2) + (D1-D2)²/(4C)` with 431.8, 65, 25 → `2C + 400/C = 290.43` → C = 143.83. This is the belt telling us where the pump sits |
| belt plane | x **+160** | `estimated` | 40 mm outboard of the crown wheel's 119, 25 mm inboard of the right bearing hanger at 185 |
| pump spindle | **(160, −386, 110)** | `derived` (radius) / `estimated` (bearing) | 143.8 mm from the axle centre (Δy 139, Δz 37.5), forward and below, which is the only quadrant that is not already occupied: rearward is the exhaust chamber, above is the airbox and the battery, and straight down is the floor tray |
| pump body | Ø **60 ±10**, axis along x | `estimated` | **not published on any page reachable from here, and no reference photograph in this repo shows the pump at all** — every Tony Kart and CRG frame here is a studio chassis shot with the pump absent or behind the rear panel. Read off trade photographs as roughly a quarter of a 250 mm core, and it has to house an impeller fed by 20 mm spigots. **This is the softest number in the section and it stays `estimated`** |
| bracket | ~145 mm, `chassis_bearing_hanger_r` to the pump | `estimated` | the nearest axle-adjacent structure. Art. 4.2.3 already contemplates welded attachment points here |

Pump clearances, `derived`: floor tray top 11 mm below, `engine_battery` 20 mm
above, right rear seat stay 35 mm outboard, chain band 10.5 mm inboard, chain
guard 7.5 mm. The axle pulley clears the crown wheel by 35 mm and the right
bearing hanger by 19.

### `radiator_bracket_lower`, `radiator_bracket_upper`
**Status:** built; **both ends re-anchored**. #192 measures them **12.25 / 12.32 mm** off `radiator_end_inboard` and **44.09 / 68.62 mm** off `seat_shell`
**Attaches to:** `radiator_end_inboard` (bolted), `chassis_rail_l` (clamped, Ø30.0 bore). **Not** `chassis_floor_tray` — Art. 4.6 puts the tray forward of the central strut, 500+ mm from here
**Envelope:** Art. 4.2.3 puts the welded attachment points for *"the radiator(s)"* on the frame and Art. 4.2.5 lists *"radiator(s), holder"* as a chassis component (PDF p. 8). **Art. 5.3.1, §5a: the radiator *"must not interfere with the seat."***
**Verification:** gate 2 at both ends; gate 1's **inverted** assertion at `seat_shell`

**Declare no joint between any `radiator_*` part and `seat_shell`, now or ever.**
§00 §5a states it as the first case in this document of a regulation *forbidding*
a joint: Art. 5.3.1's *"must not interfere with the seat"* makes any overlap
fatal, and the current `radiator_bracket_*`/`seat_shell` declaration is a
regulation violation written into the build table. The core's inboard edge at
−240 clears the shell's outboard face at −184 by 56 mm and must keep doing so.

**The anchor is `chassis_rail_l`, not the seat and not the stays.** The brief for
this section proposed the seat stays; the measurement report argues the frame,
and the frame wins on three counts: Art. 4.2.3 puts the *welded* radiator
attachment points on the frame; Art. **4.8.2** requires seat stays to be bolted
at each end and **removed if unused**, so a stay is a removable member and not a
mounting rail; and the CRG close-up — the only image in the repo that shows the
bracket — shows *"a pair of thin vertical rods that drop to a chassis clamp"*,
which is a rail clamp, not a stay clamp. Art. 5.3.1's *"radiators must be placed
above the chassis frame"* points the same way: the frame is what it stands on.

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| core-end anchor fraction | (−1.0, **1.0**, −0.52) and (−1.0, **1.0**, +0.44) of the core's own half-extents | `derived` | **1.15 is the bug**: 1.15 × 125 = 143.75 is 18.75 mm past the core's edge and the rod's radius is 8, which is the 12.3 mm gap almost exactly. 1.0 puts the rod's axis in the plane of the inboard end channel's outer face, so the rod is 8 mm engaged in a 12 mm channel — contact 0.0, and `bolted` permits the overlap. Anchoring in *fractions* was the right fix for a bracket that started inside the fin pack; 1.15 was the wrong fraction |
| lower bracket | **(−240, −169.2, 145.9) → (−310, −169.2, 50)**, 118.7 mm | `derived` | core end from the fractions above at rake 45° and z 240; rail end is §Chassis's straight rail mirrored, x −310. Was 102.6 mm to a rail at −276.4, which was the as-built pinching rail |
| upper bracket | **(−240, −316.8, 293.5) → (−310, −316.8, 50)**, 253.4 mm | `derived` | same, against the straight rail at x −310. Was 244.1 mm to −257.9. Still near-vertical, which is what the photograph shows — it leans 16° outboard rather than 4° |
| rod | Ø16, bent, mid-point 18 below the chord | `estimated` | unchanged; a bracket bowing upward looks sprung |
| rail clamp bore | **30.0** | `sourced` | the mushroom clamp family is 28/30/32 and `tube_main` is 30 |
| fasteners | 4 into the core (2 per side rail) + 2 clamping the rail | `estimated` | counted off the CRG close-up. A read of one photograph, not a spec |
| adjustable | rake **yes**, height yes | `sourced` (rake) / `estimated` (height) | *"universal adjustable radiator supports"* are a catalogue item and EM-01 ships with a support; kart practice retunes the rake with ambient temperature in 5° steps |

Both rods pass through `chassis_floor_tray` (x ±280, z 65–69) at x −240 … −258,
because the left rail at these stations is at x −257.9 … −276.4, i.e. **inboard
of the tray's own edge**. Declared `pierced`, same as the engine mount clamps and
the seat stays, and the same underlying fault: the pan is too big.

The upper rod clears the left rear seat stay by 39 mm, `derived`.

### `radiator_hose_upper`, `radiator_hose_lower`, `cooling_hose_pump_engine`
**Status:** upper and lower built and **re-routed**; the pump-to-engine hose is **new**
**Attaches to:** upper: `radiator_tank_high`, `radiator_core`, `radiator_end_inboard`, `engine_head` and `engine_water_outlet` (all routed) -- **five, not two.** Front matter §6 says this column *becomes* the `Joint` rows, so a row short here is a joint missing from `joints.py`; these three were declared when the hose's waivers were retired and the spec was not updated with them. Lower: `radiator_tank_low` and `radiator_end_inboard` (routed) and `cooling_pump_body` (routed). Pump-to-engine: `cooling_pump_body` and `engine_water_inlet` (routed).
**Envelope:** Art. 5.3.1, §5a — *"All tubing must be made of a material designed to withstand heat (150 °C) and pressure (10 bar)."*
**Verification:** gate 1 and gate 2. **There are no `radiator_hose` waivers.** This line said *"three waivers today, 33 + 15 + 58 pairs"*; all three became declared `routed` joints, and `OPEN_DEFECTS` has carried no `radiator_hose` entry since. A stale waiver count in a spec reads as an open defect that nobody needs to fix

| dimension | value | prov | basis |
| --- | --- | --- | --- |
| bore | **20 ID** (3/4 in) | `sourced` | FTP silicone kart radiator hose; 3/4 in is the trade standard |
| outer diameter | **28** | `derived` | 20 + 2 × 4 mm three-ply silicone wall. A photo reading gives 33, which is protective sleeving and must not be modelled as bare hose |
| rating | 150 °C, 10 bar | `sourced` | Art. 5.3.1, §5a |
| upper route, **radiator-first** | (−170, −410, 388) → (220, −404, 376) → (232, −228, 382) | `estimated` | hot water enters the **high** tank so the core drains downward, which New-Line's *"curved top tank inlet designed to evenly distribute water"* confirms is the inlet end. **Three corrections, all of them the kind that get built.** (1) The old row claimed the run *"crosses behind the seat back"* and it did not: its second waypoint pulled the crossing 65 mm forward of the shell's top edge and the hose passed 79 mm through the driver's chest, which no gate could see because the driver was not a part. Issue #200, ADR-0055. The route above stays at y ≤ −404 until it is 220 mm outboard, then climbs the head's *inboard* flank; measured, 26.36 mm to `seat_shell` against 1.67 before. (2) The old row was written **engine-first** while `_cooling` has consumed both routes **radiator-first** since #190 wave 3b — so anyone implementing from this spec would reproduce the exact `reversed()` bug 6938909 fixed. Direction is now in the row's own label. (3) Its last point (−330, −378, 383) was never a waypoint: that end is the tank fitting the code derives from `HOSE_UPPER_LOCAL`, at (−258.8, −381.1, 386.1) |
| lower route | (−250, −92, 100) → (−215, −200, 102) → (−215, −350, 105) → (0, −440, 95) → (140, −400, 110) | `estimated` | cold return is the low run by construction: bottom tank out, pump in, and the pump is at axle height. The x −215 leg is **inboard of both radiator brackets** — 19 mm from the lower rod's surface, 17 mm from the seat shell — which is what fixes the 58-pair overlap; the crossing at z 95 passes 28 mm under the chain's lower strand and 26 above the tray |
| pump-to-engine | (160, −386, 140) → (240, −300, 175), ~180 mm | `estimated` | the pump's outlet to the crankcase inlet boss. Short by design |
| what the routing must clear | the seat shell, the driver's hips and elbows, and the **spinning rear axle** | `derived` | a hose cannot cross the axle plane, so the upper run goes above and behind it and the lower run stays forward of it |

## 30.8 Joints to add, change and delete

`joints.py` is not this section's file. This is the delta, with the reason.

**Add:**

| a | b | kind | why |
| --- | --- | --- | --- |
| `engine_mount_clamp_*` | `chassis_floor_tray` | pierced | the pan is cut around the clamps; the rail is under the tray so no mount can avoid it |
| `radiator_bracket_*` | `chassis_rail_l` | clamped | Ø30 bore on the left main rail. The radiator's entire attachment to the kart |
| `radiator_bracket_*` | `chassis_floor_tray` | pierced | as the clamps |
| `radiator_curtain` | `radiator_end_*` | threaded | Art. 5.3.1's *"securely fixed to the radiator(s) with screws"* |
| `drive_chain_guard` | `chassis_cross_rear` | bolted | Art. 5.9's compulsory guard needs an anchor |
| `drive_chain_guard` | `axle_rear` | pierced | the guard comes down to the axle centre and is cut for it |
| `drive_chain_guard` | `drive_output_shaft` | pierced | same, at the other end |
| `exhaust_hanger_clamp` | `chassis_cross_rear` | clamped | Ø30 bore, x +54 |
| `exhaust_hanger_clamp` | `exhaust_hanger_arm` | bolted | through the arm's slot |
| `exhaust_hanger_arm` | `exhaust_hanger_cradle` | bolted | |
| `exhaust_hanger_cradle` | `exhaust_chamber` | clamped | spring cradle on the baffle cone |
| `exhaust_chamber` | `exhaust_connector` | seated | |
| `exhaust_connector` | `exhaust_silencer` | seated | |
| `exhaust_silencer_bracket` | `chassis_cross_rear` | clamped | x +150 |
| `exhaust_silencer_bracket` | `exhaust_silencer_isolator` | bolted | M8 through the rubber bush |
| `exhaust_silencer_isolator` | `exhaust_silencer_saddle` | bolted | |
| `exhaust_silencer_saddle` | `exhaust_silencer` | seated | |
| `exhaust_silencer_band_?` | `exhaust_silencer` | clamped | two jubilee clips |
| `exhaust_silencer_band_?` | `exhaust_silencer_saddle` | pierced | the band threads through the saddle's slot |
| `engine_airbox` | `engine_airbox_duct_?` | welded | the two Ø30 ducts Art. 9.13.1 requires |
| `engine_crankcase_upper` | `engine_water_inlet` | welded | cast boss |
| `engine_water_inlet` | `cooling_hose_pump_engine` | routed | |
| `cooling_pump_body` | `cooling_hose_pump_engine` | routed | |
| `cooling_pump_body` | `cooling_pump_bracket` | bolted | |
| `cooling_pump_bracket` | `chassis_bearing_hanger_r` | bolted | |
| `cooling_pump_body` | `cooling_pump_pulley` | pressed | |
| `cooling_axle_pulley` | `axle_rear` | clamped | Art. 4.3's fourth keyway is the sprocket's, not the pulley's; the pulley clamps |
| `cooling_belt` | `cooling_pump_pulley` | meshed | |
| `cooling_belt` | `cooling_axle_pulley` | meshed | |

**Add, #212 — the deck and what the squared jacket brought with it:**

| a | b | kind | why |
| --- | --- | --- | --- |
| `engine_crankcase_upper` | `engine_crankcase_deck` | welded | cast into the upper half and then machined; its lower two thirds are inside the casting |
| `engine_crankcase_deck` | `engine_cylinder_base` | bolted | **the gap.** Both faces go through `powertrain._lean`, so they can only ever be parallel |
| `engine_cylinder` | `exhaust_manifold_bolt_[23]` | threaded | the upper pair threads into the barrel — the other half of what `engine_cylinder_base`/`exhaust_manifold_bolt_[01]` already says about the 62 × 44 pattern straddling the parting. Undeclared while the jacket was a revolution because the bolts cleared it: they sit 31 mm either side of the port axis, where a squared jacket stands 7 mm further out |
| `engine_crankcase_deck` | `exhaust_manifold` | pierced | #213 |
| `engine_crankcase_deck` | `exhaust_manifold_spigot` | pierced | #213 |
| `engine_crankcase_deck` | `exhaust_manifold_bolt_[01]` | threaded | #213 |

**Change, #212:**

| pair | what changes |
| --- | --- |
| `drive_sprocket_carrier`/`engine_crankcase_*` | glob narrowed to `engine_crankcase_[lu]*`. **A glob pair is a cross product**: `engine_crankcase_deck` landed on top of the case, 14 mm from this shaft and 90 mm above its axis, and the wider pattern demanded the output shaft pierce the cylinder's deck |
| `engine_crankcase_upper`/`engine_cylinder_base` | kept, `why` rewritten — the flange's forward half still reaches past the boss into the case's own top, which is a barrel's skirt entering its spigot bore |
| `engine_crankcase_upper`/`engine_cylinder` | kept, `why` rewritten. It used to argue that a prismatic deck was a crankcase change nobody had to make. The forward dip was always right; the sentence justifying the rear wedge was not |

**Rename:** `exhaust_flange` → `exhaust_manifold`; `exhaust_flange_nut_0..1` →
`exhaust_manifold_bolt_0..3` (two becomes four); `exhaust_hanger` → the three
`exhaust_hanger_*` parts; `engine_water_pump` → `cooling_pump_body`.

**Delete, with the reason:**

| a | b | why it goes |
| --- | --- | --- |
| `radiator_bracket_*` | `seat_shell` | **Art. 5.3.1 forbids it.** §00 §5a: the absence of this pair is an assertion, not an omission |
| `engine_water_pump` | `engine_clutch_bell` | Art. 5.3.2 — the KZ pump is on the axle |
| `engine_water_pump` | `engine_clutch_cover` | same |
| `engine_water_pump` | `engine_crankcase_upper` | same |
| `engine_water_pump` | `radiator_hose_lower` | becomes `cooling_pump_body`/`radiator_hose_lower` |

**Waivers that should stop failing** once this section is built, and are therefore
fatal if they remain: `engine_mount_clamp_front`/`chassis_rail_r` (12.1),
`engine_mount_clamp_rear`/`chassis_rail_r` (22.8),
`radiator_bracket_*`/`radiator_end_inboard` (12.7),
`radiator_bracket_*`/`seat_shell` (68.2), `exhaust_hanger`/`chassis_side_bar_r`
(15.7), `radiator_hose_lower`/`radiator_bracket_lower` (58 pairs),
`radiator_hose_lower`/`engine_clutch_cover` (33), `radiator_hose_lower`/
`engine_clutch_bolt_1` (15), `drive_chain`/`drive_output_shaft` (112),
`drive_output_shaft`/`engine_clutch_bolt_4` (28).
`exhaust_chamber`/`engine_crankcase_upper` (50) must be **re-measured** against
the new routing rather than assumed fixed — the pipe passes 7.8 mm over the
crankcase's rear housing and that is this section's tightest pair.

## 30.9 `params.py` — the delta

| field | from | to | note |
| --- | --- | --- | --- |
| `exhaust_length` | 0.620 | **delete** | replaced by `exhaust_developed_length` 0.674 and the cone table |
| `exhaust_max_diameter` | 0.130 | **0.1365** | `phi N`, sourced |
| `exhaust_pipe_diameter` | 0.034 | **delete** | replaced by `exhaust_header_diameter` 0.0445 |
| — | — | **`exhaust_stinger_diameter` 0.0263** | new, sourced |
| — | — | **`exhaust_baffle_diameter` 0.1145** | new, sourced |
| — | — | **`exhaust_wall` 0.0010** | new, derived; Art. 5.10 floor 0.00075 |
| — | — | **`chain_x` 0.445** | new; was authored 0.115 and the 2026-07-31 corridor audit moved it. Issue #112: `powertrain.CHAIN_X` and `wheels.SPROCKET_X` were the same number with opposite signs and neither owned it; both now read +0.445 — chain and crown wheel on the driver's right, **outboard of the engine** (KZ-R1 HF p. 1: drive side opposite the clutch; p05 top-down: guard plate +378..+462, bare axle inboard), brake disc on the left. The 0.115 value ran the chain through the seat's right flank and forced the 74 mm `SEAT_CHAIN_RELIEF` tunnel |
| — | — | **`chain_pitch` 0.0055626** | new |
| — | — | **`sprocket_teeth_engine` 12**, **`sprocket_teeth_axle` 82** | new. `wheels.SPROCKET_DIAMETER` 0.145 becomes derived: `p/sin(pi/82)` = 145.23 |
| `radiator_width` | 0.265 | **0.250** | EM-01, sourced |
| `radiator_height` | 0.432 | **0.435** | EM-01, sourced — same number, real citation |
| `radiator_z` | 0.320 | **0.240** | derived, §30.7.2 |
| `radiator_rake_delta` | 0.0 | **delete** | its premise is false |
| — | — | **`radiator_rake` 0.7854** (45°) | new, its own number, no longer reading `seat_back_angle` |
| `radiator_y` | −0.235 | −0.235, **docstring corrected** | the value stands; "measured off V4, the plan-view reference" does not |

Module constants that move or change: `CHAIN_X` and `SPROCKET_Y` (−0.268 →
−0.2685), `EXHAUST_PATH` and `EXHAUST_PROFILE` (replaced by `EXHAUST_CONES` plus
the three placement rules), `SILENCER_*`, `EXHAUST_HANGER_Y` (deleted — the
hanger is now a station on `chassis_cross_rear`), `BRACKET_*_LOCAL` (1.15 → 1.0),
`BRACKET_*_SEAT` (deleted, replaced by rail clamps), `HOSE_*_ENGINE`,
`RADIATOR_TANK_HEIGHT` (0.030 → 0.022), `CARB_AXIS_Z` (0.205 → 0.193),
`CARB_TOP_CAP_TOP_Z` (0.262 → 0.250), `AIRBOX_LO`/`_HI` (+60 in z),
`CYLINDER_*` (the 25° lean), and `EXHAUST_FLANGE_*` → the manifold.

## 30.10 Provenance

Parts by status: **41 built** (34 engine and mount names, 4 driveline, 6 exhaust
before the split, 12 radiator — counting numbered families as one), of which
**19 change geometry** and **4 are renamed**; **17 new**
(`drive_chain_guard`, `engine_water_inlet`, `engine_airbox_duct_0`/`_1`,
`exhaust_connector`, `exhaust_hanger_clamp`/`_arm`/`_cradle`,
`exhaust_silencer_saddle`/`_band_0`/`_band_1`/`_isolator`/`_bracket`,
`radiator_curtain`, `cooling_pump_pulley`, `cooling_axle_pulley`, `cooling_belt`,
`cooling_pump_bracket`); **0 deleted**, though `engine_water_pump` survives only
as a rename to a different place on the kart.

Numbers by tag: **34 `sourced`**, **41 `derived`**, **48 `estimated`**, **6
measured-from-a-drawing-with-its-scale-stated**. No `snippet`.

The sourced core is the exhaust: 15 cones with both diameters and both slant
lengths, twice over from two independent homologation forms, plus five regulation
articles read out of the pinned PDF's own text. The `estimated` bulk is mounting
hardware and the three exhaust placement rules, and that ratio is the honest one
for this assembly.

**Two things would move most of the `estimated` to `sourced`, and naming them is
more useful than guessing harder:**

1. **CIK-FIA Technical Drawing n. 2.7, *"fitting of the exhaust and silencer"*,**
   cited normatively by Art. 9.16.1 (PDF p. 30). It is the official answer to the
   routing, the silencer's placement and every mount in §30.6.
   `fiakarting.com/page/technical-drawings` **serves an empty body to a
   non-browser fetch**, and there is no browser here. Anyone with one can get it
   in a minute; it is a request the owner can satisfy and nothing else in this
   section is a better use of effort.
2. **One side-on photograph of a KZ engine installed in a chassis**, to confirm
   the cylinder's 25° forward lean. §30.4 derives it from the sourced port angle
   plus packaging arithmetic and the whole exhaust routing rests on it.

And one thing that cannot be fixed by asking: **no accessible photograph anywhere
shows a KZ expansion chamber actually fitted to a kart.** Every manufacturer
display kart in `refs/` — three Tony Kart frames and the CRG Road Rebel — is shot
without one. §30.6.3 is therefore a construction from sourced part geometry and
regulation constraints, and it says so in its own heading rather than implying a
photograph behind it.

## 30.11 Things wrong in files this section does not own

Numerically, and each was measured rather than suspected.

1. **`joints.py:868`'s waiver threshold is on the wrong axis.** It reasons about
   a radiator *"at x = 0.330 with a 45 mm core, so its outer face is at 0.353"*
   and about `radiator_x` 0.365 *"with a half thickness of 0.020, so the core's
   outboard face is at 0.385."* 385 is a plane 20 mm along the core's own **face
   normal**, which points forward. The core's lateral half-extent is
   `radiator_width/2` = 125, so its outboard face is at **−490** — the waiver is
   **105 mm** short, and a pod built to clear 385 is still deep inside the
   radiator. `params.py`'s `radiator_width` docstring warns about exactly this
   confusion at length. Same family as the mirrored caption that put the radiator
   on the engine's side.
2. **`chassis_side_bar_r`'s inboard surface is at x 424 at y −245, not 432.**
   `IGNITION_COVER_OUTBOARD_X`'s comment cites *"x 0.432..0.452"* and claims the
   ignition cover *"clears only because it stops 2 mm short of the bar in x"*.
   Interpolating `frame.py`'s `side_path` gives a centreline of 433.9 at y −245,
   so the inboard surface is 423.9 and the cover at 430 is **6.1 mm past it**.
   The pair does not overlap, but for a different reason — 16.7 mm of vertical
   separation — and the stated 2 mm margin does not exist. §Chassis and whoever
   next moves the side bar should know that the cover's clearance is vertical.
3. **`params.py`'s `axle_diameter` docstring says "Solid, 50 mm".** Art. **9.2**,
   PDF p. **22**: *"Maximum 50.0 mm outside diameter (wall thickness according to
   Article 4.3)."* A wall-thickness clause means a **tube**, and Art. 4.3 (p. 8)
   tabulates the minimum wall against the OD. §Running gear's. Flagged here
   because `cooling_axle_pulley`'s Ø65 is derived from the axle's 50 and the
   `axle_sprocket`/`cooling_axle_pulley` clamp fits are too.
4. **`frame.py`'s floor tray reaches x ±280 from y +180 to −580, and the main
   rails run *inboard* of its edge** — x 262.7 to 276.4 through the engine bay.
   Every part that has to reach a rail therefore pierces the pan: two engine
   mount clamps and two radiator brackets in this section alone, and the seat
   stays and bearing hangers already. `frame.py`'s own report says a real kart's
   floor pan stops at the back of the footwell. Four `pierced` declarations are
   the cost of not fixing it; that is a legitimate choice, but it should be a
   choice.
5. **`wheels.SPROCKET_X` agreed on the sign and both modules were wrong on the
   magnitude.** The −0.115/+0.115 sign fight was #112 and was settled by hoisting
   the number to `params.chain_x`; the 2026-07-31 corridor audit then moved the
   magnitude to **+0.445** — the sprocket sits between the right bearing hanger
   (+300) and the right hub (~+548), outboard of the engine, where p05's chain
   guard plate (+378..+462) puts it. `wheels.SPROCKET_X` remains a module literal
   that must equal `params.chain_x`; completing the hoist there is still open
   under #112.
6. **`refs/kart-visual/sources.txt`'s caption for `tonykart_racer401T_p05.jpg`
   calls it a top-down and lists a seat in it — and the caption is right.** The
   claim that stood here, "three agents have now measured it as a high front
   three-quarter, and there is no seat in the frame", is false of the file on
   disk (unchanged since its 2026-07-28 fetch): the 2026-07-31 corridor audit
   re-opened it and it is a near-orthographic top-down — symmetric kingpins,
   circular wheel outlines, full plan silhouette — with the translucent seat
   filling the center of frame. Whatever those agents opened, it was not this
   file. The audit scaled it at 2.10 mm/px on the sourced 1050 wheelbase
   (front tire diameter cross-checks to 2%) and its lateral measurements are
   what moved `chain_x`. Distances `notes_exhaust.md` took from it should be
   re-derived against that scale rather than dismissed.
