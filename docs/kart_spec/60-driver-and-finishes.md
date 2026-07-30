# 60 — Driver package, finishes and livery

Issue #190, §60 of `KART_SPEC`. Conventions, provenance tags and the part-entry
format are `00-front-matter.md`'s and are not restated. Millimeters throughout;
`params.py` is in meters. Blender axes, origin on the ground at mid-wheelbase.

Three sources are load-bearing here and each is named where it is used:

    refs/frontend/fia_karting_technical_regulations_2026.pdf   the only regulation text
    refs/kart-visual/notes_radiator.md  §6                     the seat, already sourced
    the photographs in refs/kart-visual/                       what settles a finish

**The seat is not re-derived.** §6 of `notes_radiator.md` sources it from Tillett's
published size chart (T11 ML: A 32.5, B 36.0, C 46.0, D 10.0, E 33.5 cm) and every
driver datum below hangs off that block. Where this section disagrees with a
`params.py` field it says so in §60.6 and changes nothing.

---

## 60.1 The seated driver, as hard points

There is **no driver mesh**: `assets/generated/kart.json` holds 146 parts and not
one of them is a person. `build.py` already carries `suit_fabric`, `helmet_shell`
and `rubber_grip` for a driver that was never built, so the materials predate the
geometry by two milestones.

The whole driver is built off two things and nothing else: the seat, which is
sourced, and one anthropometric table, which is sourced.

### 60.1.1 The seat datums this rests on

All `derived from sourced`, all from `notes_radiator.md` §6, restated because every
row below cites one of them:

| datum | value | basis |
| --- | --- | --- |
| seat base plane above ground | 32 | Tillett positioning note + `ground_clearance` 35 |
| seat pan (the surface the weight sits on) | **z = 36** | base 32 + 4 mm shell |
| seat front lip | z = 132 | base 32 + D 100 |
| seat back top | **z = 365, y = −365** | base 32 + E 335; Tillett KZ axle-to-back gap 135 with the axle face at −500 |
| back shell rake from vertical | **22°** (19–26) | from C 460 and E 335, pan flat 150–200 subtracted |
| seat front lip, fore-aft | **y = −50** | `derived`: −365 + √(460² − 335²) = −365 + 315 |
| back surface at height z | `y = −365 + 0.404·(365 − z)` | `derived` from the 22° rake, tan 22° = 0.404 |
| shell external width, hips / shoulders | 333 / 368 | A and B + 2 × 4 mm |

### 60.1.2 The anthropometry, and its citation

`sourced`. **NASA, *Anthropometric Source Book*, Volumes I–III, 1978** (Webb
Associates, Anthropometry Research Project), read via the US DoD Ergonomics
Working Group's *Anthropometric Guidelines* tabulation at
`denix.osd.mil/ergo-wg/…/AnthropometricGuidelines.doc`, fetched and read
2026-07-28. Published in inches; converted here, arithmetic shown.

Anthropometry is publishable reference data — this is a `sourced` citation in the
front matter's sense, not a `snippet`. Two ANSUR routes were tried first and
neither served a percentile table that could actually be read (the NC State
summary PDF 404s; the UMTRI ANSUR files are code books, not statistics), so this
is the table that was read rather than the table that was wanted. It is a 1978
US-military-male sample and the driver is therefore a **50th-percentile adult
male of that population**, which is a modeling choice and is stated as one.

| dimension (50th pct male) | inches | **mm** |
| --- | --- | --- |
| stature | 68.7 | 1745 |
| sitting height, pan to vertex | 34.1 | 866 |
| sitting eye height | 31.0 | 787 |
| midshoulder height, sitting | 24.5 | 622 |
| shoulder breadth (bideltoid) | 17.9 | 455 |
| upper-arm length, shoulder to elbow | 14.5 | **368** |
| **elbow-to-fist length** | 14.2 | **361** |
| hand length | 7.5 | 191 |
| buttock-popliteal length | 19.2 | 488 |
| knee height, sitting | 21.3 | 541 |

`elbow-to-fist length` is the dimension the reach check needs and the reason this
table was worth chasing: it runs from the elbow to the **center of the closed
fist**, which is exactly where a steering-wheel rim sits. `driver_forearm` is an
elbow-to-wrist bone and stops 90-odd millimeters short of any grip.

Segment lengths not in that table, `derived` from Drillis & Contini's
stature fractions, which is standard biomechanics reference data:

    thigh, hip joint to knee joint    0.245 x 1745 = 428
    shank, knee joint to ankle joint  0.246 x 1745 = 429

### 60.1.3 The two placement estimates, stated as estimates

| quantity | value | prov | reasoning |
| --- | --- | --- | --- |
| hip joint above the pan | **95** | `estimated` | the H-point sits 90–100 mm above a firm seat surface in every seating-package convention. Nothing in this repo sources it. |
| hip joint forward of the back contact | **100** | `estimated` | roughly half the pelvis depth. Checked below against a figure measured by somebody else, and it lands within 4 mm. |
| torso recline from vertical | **25°** | `estimated` | bracketed by the shell chord's sourced 19–26°, taken 3° back of the middle because the spine continues above where the shell ends. `notes_radiator.md` §6 offers 40–45° for the torso; §60.6 shows that angle cannot be built. |

So: **hip joint at (±85, −170, 130).**

    z:  pan 36 + 95                                        = 131 -> 130
    y:  back surface at z 130 is -365 + 0.404 x 235 = -270
        hip 100 forward of it                              = -170

**And it is corroborated.** The §40 Cockpit agent measured hip-to-pedal-face at
**735 mm** from a completely different direction. This spec's hip and
`params.py`'s pedal — *as both stood then* — gave

    sqrt((560 + 170)^2 + (90 - 130)^2) = sqrt(730^2 + 40^2) = 731.1 mm

which agrees to **4 mm, 0.5%**. Two chains, one answer; the hip is solid. (The
pedal has since been re-authored and #202 moved the ball of the foot with it —
hip to the live ball is now 767.7 mm. Both 735 and 731.1 were reads of the *old*
pedal face, so the corroboration stands as evidence about the hip, which did not
move; neither figure is a current pedal measurement.)

### 60.1.4 The hard points

Torso axis unit vector at 25° from vertical, leaning back:
`(0, −sin 25°, cos 25°) = (0, −0.4226, 0.9063)`. Along-torso distances are the
table's sitting heights minus the 95 mm hip rise.

| point | x | y | z | prov | arithmetic |
| --- | --- | --- | --- | --- | --- |
| hip joint (H-point) | ±85 | **−170** | **130** | `derived` | §60.1.3 |
| shoulder joint (acromion) | ±200 | **−393** | **608** | `derived` | 622−95 = 527 along torso: −170 − 527·0.4226; 130 + 527·0.9063 |
| shoulder outer surface | ±227 | −393 | 608 | `derived` | bideltoid 455 / 2 |
| eye | ±32 | **−462** | **757** | `derived` | 787−95 = 692 along torso |
| vertex, bare head | 0 | −496 | 829 | `derived` | 866−95 = 771 along torso |
| helmet center | 0 | **−454** | **738** | `derived` | 100 mm below the vertex along the head axis |
| helmet crown, outer | 0 | −511 | **860** | `derived` | vertex + 35 mm of liner and shell |
| knee joint | ±180 | **+131.7** | **433.5** | `derived` | two-link solve, below |
| ankle joint | ±150 | +456.8 | 153.6 | `derived` | heel + (55, 68) rotated with the sole; lateral moved with #201, the foot runs straight in plan |
| heel contact | ±150 | +479.0 | **69** | `derived` | walked back from the bar so the sole's tangency closes; tray top is z 69 — **see §60.6, there is no tray there** |
| ball of foot / pedal contact | ±150 | +592.1 | +222.9 | `derived` (`P.driver_ball`) | on the bar's Ø18 **surface**; lateral is `pedal_separation`/2 (#201's measured 300) — throttle x +150 right foot, brake x −150 left |

**The whole foot chain is a live derivation since #202, not a set of fields.**
The old table had the ball at (±75, 560, 90), tagged `sourced` from `pedal_y` /
`pedal_z` — and those fields had since been re-authored 138 mm higher, so the
citation stayed true as a sentence while false as a number, both `presses`
contacts failed by 54.8 mm, and 24 of gate 3's first 64 findings were boots in
the pedal linkage. A provenance tag records where a figure came from, not
whether it still agrees with it. `P.driver_heel/ball/ankle/knee` now solve off
`pedal_bar_y` / `pedal_bar_z` / `pedal_separation` on every read, so the numbers
in this table are the solve's output, printed for reading rather than authored.

The foot's pose, `derived`, `P.driver_foot_pitch`: heel on the tray-top plane
z 69, sole tangent to the foot bar's Ø18 cylinder, heel-to-ball 191 along the
sole (`driver_foot_link`, `estimated` — consistent with Drillis & Contini's
foot 0.152 × 1745 = 265 with the ball at ~0.72 of it, and it is the old flat
offset's length, √(190² + 21²) = 191.2).

    bar center (584.9, 228.2);  r = 9;  heel plane z 69
    heel-to-bar-center = √(191² + 9²) = 191.2
    heel_y  = 584.9 − √(191.2² − 159.2²) = 584.9 − 105.9 = 479.0
    pitch   = atan2(159.2, 105.9) − asin(9/191.2) = 56.4° − 2.7° = **53.7°**
    ball    = heel + 191·(cos, sin) = (592.1, 222.9)   — on the bar surface
    ankle   = heel + 55·(cos, sin) + 68·(−sin, cos) = (456.8, 153.6)

A 53.7° pitch is a driver with the heel down and the toes up on an organ pedal.
The ankle offset (55 along the sole, 68 perpendicular — the 68 is Drillis &
Contini ankle height, 0.039 × 1745) rotates with the foot; the old table applied
it unrotated, which was tolerable at the flat foot's 6.3° and would be wrong by
half the offset's own size at 53.7°.

Overall seated height, helmeted: **860 mm** above the asphalt. `derived`. That is
210 mm above Art. 9.1.1's 650 mm chassis ceiling, which is what every photograph
of a kart shows and is the sanity check on the whole chain.

Shoulder span: `driver_shoulder_span = 400` is **biacromial breadth and is
correct**; the 455 bideltoid is the flesh, 27 mm wider per side. Both are wanted —
400 places the joints for the reach solve, 455 sizes the mesh.

Knee, two-link solve (`P.driver_knee`, live). Hip (−170, 130) to ankle
(456.8, 153.6), d = 627.3.

    a = (428^2 + 627.3^2 - 429^2) / (2 x 627.3) = 392591 / 1254.5 = 312.9
    h = sqrt(428^2 - 312.9^2) = sqrt(85278)                       = 292.0
    knee = hip + 312.9 * along + 292.0 * perpendicular = (131.7, 433.5)

Knee lateral **±180**, `estimated` from `exh_commons_buntschu_kz2.jpg`: the driver's
knees are splayed clearly *outboard* of the steering wheel rim with the front panel
between them. This is the one lateral figure here read off a photograph, and the
photograph is a three-quarter front-left action shot, so it is a proportion read
against the front track and not a measurement — hence `estimated` and hence ±180
rather than a decimal.

### 60.1.5 Helmet and equipment as geometry

**Art. 7 is the article that makes equipment compulsory, not Art. 3.** The brief
this section was written to said Art. 3; the text puts driver safety equipment in
**ARTICLE 7: DRIVER SAFETY EQUIPMENT**, PDF pages 19–20. Art. 3.6 *Mass* mentions
equipment only in passing — *"The driver must be fully equipped for the driving
conditions (with helmet, gloves and boots)"*, PDF p. 5 — as a weighing condition.
Same class of error as the §7.4-for-§7.2 case the front matter records: an article
number recalled rather than located.

Art. 7 preamble, verbatim, PDF p. 19:

> The driver must at all times wear a homologated full-face helmet, overalls and a
> karting body protection, as well as gloves, boots.
> Wearing a scarf, muff, or any loose clothes around the neck, even inside the
> overalls, is not allowed.
> Long hair must be completely contained in the helmet, the balaclava or the
> overalls.

| item | article | page | what the text requires | geometry |
| --- | --- | --- | --- | --- |
| helmet | 7 preamble + **7.1** | 19 | *"homologated full-face helmet"*; Snell-FIA / FIA 8859-2024 / 8860-2018 families; for under-15s *"the weight of the helmet must not exceed 1,500g including paint, visor and all accessories"*. *"Helmets must have an efficient and unbreakable visor for the eye opening."* | full-face shell + visor aperture. Outer 250 wide × **340 long** × 300 tall; center (0, −454, 738). `driver_helmet_radius = 125` is right laterally and 90 mm short fore-aft — a helmet is an ellipsoid, not a sphere. Visor aperture centered on the eye point (±32, −462, 757), ~95 mm tall × 200 wide, `estimated`. |
| overalls | **7.2** | 19–20 | *"Fabric overalls must have either: i) a «Level 2» CIK-FIA homologation … or ii) be Grade 1 or Grade 2 Karting Overalls complying with FIA Standard 8877-2022."* Karting overalls to 8877-2022 *"are mandatory from 01.01.2030"* | a single close-fitting layer over the whole body, collar to wrist to ankle. 6–8 mm of thickness over the torso, `estimated`. |
| gloves | **7.3** | 20 | *"Gloves must completely cover the hands and wrists or must comply with FIA Standard 8877-2022."* 8877-2022 mandatory for FIA Championships and anything on the International Sporting Calendar. | hands + wrists covered; the cuff overlaps the sleeve. Grip axis at the wheel rim. |
| boots | **7.4 Shoes** | 20 | *"Shoes must cover the feet and protect the ankles or must comply with FIA Standard 8877-2022."* | above-ankle boot; sole contacts the pedal at the ball of the foot, (±150, 592.1, 222.9) — §60.1.4's live solve. |
| rib protector | **7.5 Karting body protection** | 20 | *"The use of karting body protection complying with FIA Standard 8870-2018, and of the correct size in relation to the driver's height - or up to one size lower - will be mandatory …"* | a rigid shell over the ribs, front and back, roughly z 250–450 in the torso frame. Adds 12–18 mm per side over the overalls and is the reason a driver fills a 325 mm hip / 360 mm shoulder seat. `estimated`. |
| neck brace | **none** | — | **The 2026 TR does not require one.** No neck collar, brace or support appears anywhere in Art. 7, and the preamble's ban on *"a scarf, muff, or any loose clothes around the neck"* points the other way. Art. 7.1's note that *"the M6 anchorages cannot be used in karting for safety reasons"* explicitly rules out FHR/HANS. `exh_commons_buntschu_kz2.jpg` shows a KZ2 driver mid-corner with no collar. | not modeled. If one is wanted it is a styling choice and must not be labeled as a regulation. |

That last row is the point of the table. The brief asked for the article that makes
a neck brace compulsory; there isn't one, and inventing a citation for it would be
exactly the failure the front matter's §1 exists to prevent.

### 60.1.6 The driver as built parts — the part set, its contacts, and gate 3

**This subsection is a contract.** Issue #17's driver, issue #200's gate 3 and
issue #194's mass lumps are three separate pieces of work that all read the part
names and hard points below, so the names are fixed here before any of them starts
and none of them may rename one. ADR-0055.

**Why the driver is built geometry and not a hidden collision proxy.** Gate 1
asserts no part is inside another and gate 2 asserts every part touches something.
Neither has any opinion about the volume a seated human occupies, because that
volume does not exist in the build — so a radiator hose crossed the driver's lumbar
spine 79.4 mm deep while both gates stayed green, and the only thing that caught it
was Anthony looking at a viewport. Two clauses of the regulation this document is
written against are *about the driver* and are unverifiable without him:

> **Art. 9.5.3** It must not impede the normal functioning of the pedals or cover
> any part of the feet in the normal driving position.
>
> **Art. 9.5.4** No part of the side bodywork may cover any part of the driver
> seated in the normal driving position.

A proxy would satisfy the gate and leave the cockpit rendering empty, which is
half of what #199 says is wrong with it — the seat, pedal and wheel relationship
cannot be judged against nobody. So the driver is a real, exported, materialed
part set.

#### The parts

Nineteen parts, all prefixed `driver_`, built by `tools/blender/kartlib/driver.py`
into the `driver` collection group — which `build.GROUPS` already carries, as
`genkart.MODULES` already carries `("driver", "#17 driver with IK-ready arms")`.

Endpoints are §60.1.4's hard points and are **not** to be re-derived. Where a row
says *segment*, the part spans those two points. Cross-sections are **not
specified here on purpose**: they are flesh, §60.1.4 has no flesh dimension in it
beyond the three breadths below, and a number invented in this table would be an
estimate wearing the authority of a contract — front matter §1's second defect
exactly. The building module owns them, derives what it can from the breadths, and
carries reasoning on every one it has to estimate.

| part | segment or center | from §60.1.4 | material |
| --- | --- | --- | --- |
| `driver_pelvis` | hip block about the H-point | (±85, −170, 130) | `overalls_fabric` |
| `driver_torso` | hip → shoulder, along the 25° torso axis | (0, −170, 130) → (0, −393, 608) | `overalls_fabric` |
| `driver_rib_protector` | over the torso, z 250–450 in the torso frame | Art. 7.5, §60.1.5 | `protector_shell` |
| `driver_neck` | shoulder line → helmet underside | (0, −400, 615) → (0, −437, 694) | `overalls_fabric` |
| `driver_helmet` | ellipsoid, 250 wide × 340 long × 300 tall | center (0, −454, 738) | `helmet_shell` |
| `driver_helmet_visor` | aperture on the eye point, ~200 × 95 | (±32, −462, 757) | `visor_tint` |
| `driver_upper_arm_l` / `_r` | shoulder → elbow | (±200, −393, 608) → elbow, arm 368 | `overalls_fabric` |
| `driver_forearm_l` / `_r` | elbow → fist center | elbow → grip, elbow-to-fist 361 | `overalls_fabric` |
| `driver_glove_l` / `_r` | fist at the rim, cuff overlapping the sleeve | on the rim, §60.2.1 | `glove_leather` |
| `driver_thigh_l` / `_r` | hip → knee | (±85, −170, 130) → (±180, +131.7, 433.5) | `overalls_fabric` |
| `driver_shank_l` / `_r` | knee → ankle | (±180, +131.7, 433.5) → (±150, +456.8, 153.6) | `overalls_fabric` |
| `driver_boot_l` / `_r` | ankle → ball of foot, above-ankle | (±150, +456.8, 153.6) → (±150, +592.1, 222.9) | `boot_leather` |

The elbow is **not** a hard point in §60.1.4 and must not be invented as one. It
is the solution of the two-link reach with the upper arm 368 and elbow-to-fist
361 against the shoulder-to-grip distance §60.2 measures, and §60.2.2 shows that
distance does not close at a comfortable elbow angle. **So the elbow is where the
reach arithmetic puts it, the residual is reported as a number, and the arms are
built at whatever angle actually closes** — a driver built with locked straight
arms is a legitimate output of a cockpit that does not fit, and it is a far more
useful render than one with the arms quietly shortened to suit.

Five new materials, following §60.3's four-tuple form
`(hex, luminance, roughness, metallic)`. Values are `estimated` and owned by the
building module; the *names* are fixed here so nothing downstream drifts:
`overalls_fabric`, `protector_shell`, `helmet_shell`, `visor_tint`,
`glove_leather`, `boot_leather`. **Nothing about the driver takes a livery role** —
a suit is not bodywork and must not pick up `bodywork_wrap`.

#### The contacts, and why they are not `Joint`s

`joints.KINDS` is closed on purpose and every one of its nine kinds was drawn from
a real joint on a kart: welded, bolted, threaded, pressed, seated, clamped,
meshed, routed, pierced. **A driver is none of those.** He is not fastened to the
kart, he carries no load into it through a fastener, and a hand on a rim is not a
clamp. Widening `KINDS` to fit him would break the rule that makes that vocabulary
worth having, so the driver gets its own table, `joints.DRIVER_CONTACTS`, with its
own three-word vocabulary:

    sits_on   weight passes through this surface into the kart
    grips     a hand closes around it
    presses   a foot bears on it

| a | b | kind | why |
| --- | --- | --- | --- |
| `driver_pelvis` | `seat_shell` | `sits_on` | the H-point is 95 mm above the pan; this pair is where 78 kg enters the chassis |
| `driver_torso` | `seat_shell` | `sits_on` | the back bears on the shell at the 22° rake §60.1.1 sources |
| `driver_thigh_l` / `_r` | `seat_shell` | `sits_on` | the pan's front lip at y −50 carries the thighs |
| `driver_rib_protector` | `seat_shell` | `sits_on` | ADR-0057: the protector is worn *between* torso and shell over z 250–450, so it — not the torso — is what bears on the shell there. The torso's own row is the contact at the shell's **top edge**, above the protector's band, where the 25° torso leaves the 22° shell. Until #17's surface pass re-insets the torso by the protector's thickness, the protector overlaps the shell by roughly its own 12–18 mm; this row is what makes that overlap declared rather than waived. |
| `driver_glove_l` / `_r` | `steering_rim` | `grips` | §60.2.1's rim, straight-ahead |
| `driver_boot_l` | `pedal_brake_pad` | `presses` | brake is the left foot, x −150 per `pedal_separation` (#201) |
| `driver_boot_r` | `pedal_throttle_pad` | `presses` | throttle is the right foot, x +150 |

**The gear lever is deliberately absent from that table.** A KZ is shifted with the
right hand off the wheel, so `shifter_lever` is a *momentary* contact and a
permanent declared one would assert a hand in two places. It is not modeled and
the omission is the record of that decision, not an oversight.

#### Gate 3 — driver clearance

Issue #200, and it runs in the geometry stage beside gates 1 and 2, so `--watch`
reports it on every save.

- **Gates 1 and 2 skip every `driver_*` part.** The driver is not a kart part: he
  is not mounted to anything, so gate 2's "every part touches a neighbor" is
  meaningless for him, and gate 1's overlap rule would fire on the seat contacts
  above. Every pair involving a `driver_*` part belongs to gate 3 alone.
- **Intra-driver pairs are skipped entirely.** One articulated body authored as
  nineteen segments interpenetrates at every anatomical joint by construction, and
  declaring nineteen of those would be bookkeeping that asserts nothing.
- **Every other part must not intersect any `driver_*` part.** A `DRIVER_CONTACTS`
  row does what a `Joint` does — permits the overlap *and* requires contact within
  `CONTACT_TOLERANCE`, so a glove 40 mm off the rim fails just as loudly as a hose
  through the spine.
- The two regulation clauses become assertions rather than prose: no `bodywork_*`
  part may occlude a `driver_*` part (9.5.4), and no `bodywork_front_panel`
  triangle may sit above a boot or inside the pedal sweep (9.5.3).
- Waivers are itemized exactly like `OPEN_DEFECTS`, each carrying its **measured
  depth in millimeters** and an issue number, and an entry that no longer fails is
  fatal. The waiver list is seeded so the gate is green on its first run and every
  later fix is a waiver deleted.
- **The gate tests mesh against mesh, and the torso's rear boundary is the
  §60.1.1 rake plane, not the shell's triangles.** ADR-0057, settling ADR-0055's
  amendment: §60.1.4 publishes no torso fore-aft half-depth, an unclipped capsule
  swallows the seat under any radius yet invented (84.4–99.32 mm), and the one
  number that changed sign across the upper-hose fix was the **clipped** one. So
  any torso-region depth gate 3 reports is measured on the driver volume
  intersected with the half-space forward of `y(z) = −365 + 0.404·(365 − z)` —
  the plane and the built shell disagree by up to 11.1 mm at the pan transition,
  the plane is the conservative side, and it cannot move when a bevel does.

**A finding that cannot be adjudicated against a sourced figure gets a waiver and
a ticket, not a geometry change.** The knee splay of ±180 is `estimated` off one
three-quarter action photograph and the whole leg path hangs off it; spending a
regulated clearance to fix a leg whose position is a guess is how a real
constraint gets consumed by a modeling error. Six of the first measured findings
are in exactly that category.

#### The switch, and why it is not optional

`driver.py` is wired into `MODULES` behind a flag that builds the bare kart. Every
§6.4 driving figure, every `drive.sh` scenario and every published still predates
the driver, and each one has to stay reproducible from the command that made it —
the rule `shots/` is held to. A turntable of the kart *with* a driver is a new
still, not a redefinition of an old one.

**The spelling is `--driver=false`, not `--set=driver=false`, and that is a
correction.** `--set` is typed against `KartParams`' fields by
`genkart.parameters_from`, which raises `SystemExit("no such parameter 'driver'")`
before any module runs — so the spelling this subsection originally carried cannot
work without a `params.py` field, and `params.py` is single-owner. `driver.py`
reads `--driver=` off `sys.argv` instead, exactly as `build.selected_livery` reads
`--livery=` and for the same reason: `genkart.sh` forwards every unrecognized
argument verbatim and `genkart.py`'s parser collects unknown keys without
complaining.

### 60.1.7 The cross-sections, as built — issue #17

`tools/blender/kartlib/driver.py`. Every number here is the building module's, per
§60.1.6's rule that the cross-sections are not specified in the contract; every one
carries a tag and the estimates carry their reasoning. Millimeters.

**Eighteen parts, not nineteen.** §60.1.6 says nineteen and its own table
enumerates twelve rows — six singles and six left/right pairs — which is 18. The
built set is exactly those eighteen names and none was renamed. The count in the
prose is the error, not the table.

| quantity | value | prov | basis |
| --- | --- | --- | --- |
| pelvis width | **325** | `derived` | `driver_hip_breadth`, unchanged. |
| pelvis depth | **200** | `derived` | 2 × §60.1.3's 100 mm hip-forward-of-back-contact, which that subsection calls "roughly half the pelvis depth". |
| pelvis height | **188** | `derived` | `2 × (driver_hip_z − (seat_z + seat_thickness))` = 2 × (130 − 36). Expressed that way rather than as a constant so the block's underside *is* the pan whatever the seat does — measured at 36.00 against a pan of 36.00, 0.00 mm. |
| pelvis / boot superellipse exponent | 3.0 | `estimated` | squarer than an ellipse, rounder than a box. A pelvis in a shell and a boot sole both read that way. |
| torso rear face | the seat's own 22° chord | `derived` | the plane through 100 mm behind the H-point at `p.seat_shell_rake`. Reproduces §60.1.1's published back surface to 1 mm, §60.1.3's hip derivation exactly, and gives the acromion 70.2 mm of rearward half-depth. **A rigid 25° torso box instead stands 33 mm proud of the shell's top edge** — measured — because 3° over 280 mm of shell is 15 mm on top of the 16 mm it starts at. |
| torso width, three stations | **325 / 360 / 455** | `derived` / `derived` / `sourced` | §60.1.1's own taper: `driver_hip_breadth` at the H-point, `driver_seated_shoulder_breadth` where the shell ends, `driver_bideltoid` at the acromion. Nothing styled. **These are lateral half-widths only** — see the section table below. |
| torso depth at the hip | 200 | `derived` | = the pelvis depth. |
| torso depth at the chest | **215** | `estimated` | the derived 200 plus 7.5%. The chest is the deepest station; the rib protector is a separate part, so this is flesh and overalls only and is not double-counting the shell. |
| torso depth at the acromion | **210** | `estimated`, arithmetic shown | the back plane at `driver_shoulder_z` sits 70.2 mm behind `driver_shoulder_y`; a shoulder joint is about one third of the chest depth forward of the back, so 3 × 70.2 = 211 → 210. The one-third is the estimate; the 70.2 is derived from two sourced positions. |

**The torso section is an off-center ellipse about the spine, not a circle, and the
breadths are never spent fore-aft.** A circular section spends the 455 bideltoid as
a 227 mm radius, i.e. a 455 mm chest depth, and the resulting volume reaches back
*through* the seat — and gate 3 would not catch it, because
`joints.DRIVER_CONTACTS` declares `driver_torso`/`seat_shell` as `sits_on` and a
declared contact **permits** interpenetration. Same shape of failure as the inverted
winding: invisible because the thing that would show it is switched off by design.
So the rear half-depth is not an estimate at all — it is bounded by the sourced 22°
back plane, which is the surface he leans on.

| station | z | spine y | rear face y | **rear half-depth** | **forward half-depth** | half-width |
| --- | --- | --- | --- | --- | --- | --- |
| hip / H-point | 130.0 | −170.0 | −270.0 | **100.0** | **100.0** | 162.5 |
| shell top | 367.0 | −280.5 | −365.8 | **85.2** | **129.8** | 180.0 |
| acromion | 608.0 | −392.9 | −463.1 | **70.2** | **139.8** | 227.5 |
| shoulder cap | 632.0 | −404.1 | −472.8 | **68.8** | **121.2** | 215.0 |

Rear half-depths are `derived` (the sourced back plane against the spine axis);
forward half-depths are the `estimated` total depth less the rear; half-widths are
§60.1.1's breadths halved. Symmetric only at the hip, which is right — §60.1.3's
100 mm *is* half the pelvis depth.

**Measured on the built mesh**, rear surface to `seat_shell`:

| part | nearest rear-face vertex to `seat_shell` | at |
| --- | --- | --- |
| `driver_torso` | **0.04 mm** | (45.3, −309.4, 231.6) |
| `driver_pelvis` | **0.02 mm** | (0.0, −275.0, 143.4) |
| `driver_rib_protector` | **0.37 mm** | (0.0, −361.6, 356.6) |

which is what `sits_on` is supposed to mean. **The deep overlaps with `seat_shell`
are lateral and low, not fore-aft**: the worst `seat_shell` vertex inside
`driver_torso` is 61.92 mm in at **(86.8, −213.2, 191.9)** and inside
`driver_pelvis` 74.59 mm at **(86.0, −185.7, 136.6)** — both well inboard of the
shell's 162.5 mm inner half-width and above the pan, so they are the built shell's
own pan-to-back transition wrapping the hips rather than a driver sunk into his
seat back. That distinction is the whole point of measuring the rear face
separately.

**ADR-0055's 79.4 mm is model-dependent and the built mesh supersedes it.** That
figure came from a constant-radius capsule union at chest r 162; a tapered capsule
reads 94.8 at the same point and a circular-section solid reads 99.32. Against the
**built** `driver_torso`, ADR-0055's probe point (0, −362, 368) is **3.86 mm** from
the surface — the rear face at that height is y −366.2, exactly on the sourced back
plane. So none of the three capsule numbers describes this part, and the one to
quote from here on is the mesh's.
| shoulder cap, above the acromion | 430 wide × 190 deep over 24 of rise | `estimated` | the deltoid is widest *at* the acromion and rolls over above it. |
| torso exponents | 2.6 hip, 2.3 chest and up | `estimated` | close to elliptical, rounder as it rises out of the shell. |
| rib protector, along-torso stations | **250 → 450** | reading of §60.1.5 | §60.1.5's "z 250–450 in the torso frame" read as distance along the 25° axis, which is world z **357–538** — the ribs, with the acromion at 527 along. The alternative reading, world z, would put a rib protector across the lumbar spine. |
| rib protector build | a closed band, outer face 1 mm proud of the torso, 15 mm inward | `estimated` placement | the real part is a front shell and a back shell strapped at the sides; a closed band is the simplification, and it is chosen because it is **watertight** and therefore checkable by the winding gate. The 15 mm goes *inward* because 325 and 360 are already the breadths of a driver wearing one — that is what `driver_protector_thickness`'s docstring means by "they are what makes the seat fit" — so putting it outside counts it twice. |
| neck | **120** diameter, flared 1.15× at the base | `derived` from an `estimated` | bare 106 (`estimated`, a little under half `driver_helmet_width`) plus 2 × `driver_overalls_thickness`. Art. 7's preamble bans "a scarf, muff, or any loose clothes around the neck", so the collar is the overalls and nothing else. |
| upper arm | **109** at the root → 86 at the elbow | `estimated`, arithmetic shown | `(driver_bideltoid − driver_seated_shoulder_breadth) / 2` = 47.5 is the deltoid's outboard stand-off; doubled as a diameter is 95, plus 2 × 7 of overalls is 109. The two breadths are measured at different heights, so this is a proportion and not a derivation, and it is tagged accordingly. |
| forearm | 96 at the elbow → 70 at the wrist | `estimated` | slimmer than the upper arm at the elbow, tapering hard. |
| glove, along the rim | **105** | `derived` | the NASA table's `hand length` 191 × 0.55 for a closed fist. |
| glove, across and through | 96 × 86 | `estimated` | hand breadth is not in §60.1.2's table. |
| thigh at the hip | **162.5** | `derived` | two thighs fill `driver_hip_breadth`, so each is half of it. Computed from the field. |
| thigh at the knee | 128 | `estimated` | a clothed knee measures about that across. |
| shank | 124 at the knee → 94 at the ankle | `estimated` | |
| boot shaft | **108** diameter, 90 of rise above the ankle | `derived` / `estimated` | the cuff goes over the overalls, so 94 + 2 × 7. The 90 mm rise is `estimated` against Art. 7.4's *"protect the ankles"*. |
| boot foot | half-widths 42 / 48 / 38, half-heights 48 / 40 / 30, toe 45 past the ball | `estimated` | 96 across the ball sits between two figures that *are* written down: `pedal_bar_length` 0.080 is documented in `params.py` as "one boot", and a 50th-percentile male foot breadth is about 100. |
| visor aperture | 200 × 95 | `estimated`, §60.1.5 | checked against `driver_eye_x` rather than trusted: an aperture narrower than 2 × 32 plus a margin is a driver who cannot see out, and the module widens it and says so if that ever happens. |
| visor standoff / thickness | 1 / 4 | `estimated` | a polycarbonate visor is 2–3 mm in a rebate; 4 at a 1 mm standoff is the same read with no rebate. **The helmet's eye port is not cut open** — §60.1.6 specifies an ellipsoid, a closed ellipsoid is watertight and therefore checkable, and an aperture cut at two densities would not be the same shape at both. |
| elbow swivel | **down, with a quarter of it outboard** | `estimated`, read off two photographs | `exh_commons_buntschu_kz2.jpg` and `exh_commons_panfilov_kz2.jpg` both show the elbows *tucked* — below the shoulder and barely outside the torso line, forearms angling up and inboard to the rim. It matters here: at the reach this cockpit actually has the elbow is **212.4 mm** off the shoulder-to-grip line, so unlike a locked arm the swivel is a real choice. Built, the right elbow lands at **(234.4, −133.9, 349.0)** — 34 mm outboard of the shoulder and 259 mm below it. An earlier armchair 45° guess put it at (331.7, −116.6, 403.9), 132 mm outboard, out over the sidepod, and 30 mm into `engine_plug_lead`; neither photograph supports it and the interference went away when the photograph did the choosing. |

**Three inconsistencies in §60.1's own numbers, found by building them.** None is
patched in geometry; all three are decisions above this module's pay grade.

1. **The neck's two endpoints are the only §60.1.6 coordinates with no `params.py`
   field**, so they are the one pair of literals in `driver.py`. They are **3.9 mm
   and 4.1 mm rearward** of the 25° torso axis at their own heights — measured. Small
   and consistent, so probably rounding in §60.1.6's own derivation, but it means the
   neck is the one part not on a published datum.
2. **The helmet cannot be both "300 tall × 340 long" and crowned at
   `(0, −511, 860)`.** §60.1.4 derives that crown 135 mm from the centre *along the
   raked head axis*; an **axis-aligned** 300 mm ellipsoid centred at z 738 crowns at
   **888**, 28 mm higher, at y −454 rather than −511. `driver.py` builds it
   axis-aligned, because `_width` / `_length` / `_height` are world words and a shell
   raked 25° is neither 340 long nor 300 tall — and because a raked shell puts the
   visor 27–107 mm *above* the eye and the face 25° skyward, measured. So the
   contract's three dimensions win and §60.1.4's crown row is the casualty.
3. **§60.1.4 puts the eye point on the helmet's own fore-aft mid-plane**, because it
   walks sitting eye height along the torso axis and the helmet centre is derived
   along the same axis. Built, the eye is **178 mm behind the shell's front face**;
   real eyes sit near the front of the skull, roughly 100 mm behind a helmet's front.
   The driver looks out of the middle of his own head. This is the same field
   `kartview.gd`'s cockpit camera and `engine_voice_rig.gd`'s listener read, so it is
   a §60.1.4 decision with runtime consequences and not a mesh tweak.

**Density and cost.** Eighteen parts: **1,352 verts / 2,628 tris at low** and
**7,944 / 15,812 at high**. Ring counts come from `detail.tube_segments` (12 / 32)
and station counts from `detail.bend_segments // 2` (3 / 7); interpolation between
authored stations is **linear**, so both densities lie on the same surface rather
than on two surfaces, which is what `Detail`'s contract requires for #19's bake. No
part is beveled: `build.bevel_object`'s 4 mm high-detail offset would chamfer most
of the protector's 15 mm wall away, and a smooth-shaded body has no hard edges for a
bevel to earn its vertices on.

**"Same shape at two densities" is measured rather than claimed.** Every low-detail
vertex against the high-detail surface of its own twin: **worst 1.165 mm**
(`driver_helmet`), and eleven of the eighteen parts are under 0.4 mm — `driver_torso`
0.815, `driver_rib_protector` 0.756, `driver_pelvis` 0.597, `driver_glove_?` 0.361,
`driver_thigh_?` 0.353, `driver_helmet_visor` 0.297, `driver_neck` 0.295,
`driver_shank_?` 0.265, `driver_boot_?` 0.245, `driver_upper_arm_?` 0.233,
`driver_forearm_?` 0.205. The whole spread is chord error of an inscribed polygon: a
12-gon on the helmet's 125 mm radius sits `r(1 − cos 15°)` = 4.3 mm inside the true
ellipse and a 32-gon `r(1 − cos 5.6°)` = 0.6 mm, so about a millimeter between them
is exactly what two densities of the *same* surface look like. Nothing here is a
second shape, which is the smeared bake #19's acceptance criteria calls out.

**All eighteen are watertight and enclose positive volume**, so every one is checked
by the winding gate rather than skipped by it. That drove three construction
choices: the rib protector is a closed band, the boot is two closed lofts in one
mesh rather than one loft round a 90° corner, and the visor is a closed lens with
four rim strips whose windings are derived in comments rather than tried.

### 60.1.8 The six driver finishes, measured — and what the photographs said about the pose

§60.1.6 fixes six material names and leaves their values to the building module.
Sampled off **`exh_commons_buntschu_kz2.jpg`** — a KZ2 driver mid-corner, and the
only photograph in this repo of a driver *in* a kart — with a 16-level modal sample
plus a mean per region, and cross-read against
**`exh_commons_panfilov_kz2.jpg`**. It is an overcast outdoor frame, so whites carry
a blue sky cast and dark surfaces are the trustworthy hues.

| region | modal sample | mean | measured linear luminance |
| --- | --- | --- | --- |
| overalls, navy sleeve | **#182848** | #2a364d | 0.037 |
| overalls, white chest panel | #d8e8f8 | #c1d3ea | 0.639 |
| glove | #181818 | #3b3d41 | 0.046 |
| boot | #282838 | #2f383c | 0.038 |
| helmet shell, white | #d8e8f8 | #b1bed3 | 0.509 |
| visor | **#083858** | #265173 | 0.075 |
| radiator core, for scale | #a8b8c8 | #849ba6 | 0.311 |
| asphalt, for scale | #989898 | #909091 | 0.279 |

So, in §60.3's four-tuple form `(hex, luminance, roughness, metallic)`:

| material | four-tuple | notes |
| --- | --- | --- |
| `overalls_fabric` | **`("#1c2c4c", 0.060, 0.65, 0.0)`** | hex `derived` from the #182848 mode warmed toward the mean. **`build.py`'s existing `suit_fabric` is `#16305c` at 0.055 — within a hue step and 0.005 of luminance, which is corroboration and not coincidence.** The luminance is deliberately above the measured ratio: 0.037 against a 0.311 radiator scales to 0.029 on this project's `radiator_alu` 0.240, and a cockpit-camera-distance surface at 0.029 with no albedo texture is a black hole. Same reasoning `engine_cast` is held at 0.175. |
| `protector_shell` | **`("#232326", 0.032, 0.50, 0.0)`** | all `estimated` — **no photograph in this repo shows a rib protector at all**, see below. Placed darker than the overalls and lighter than the tires so it separates from both. |
| `helmet_shell` | **`("#f0ece6", 0.640, 0.14, 0.0)`** — **unchanged** | already in `FIXED_FINISHES` and the measurement supports it: the shell is by far the brightest thing on the driver in both frames, and roughness 0.14 is right for an Arai's clear coat. Reported as confirmed rather than re-tuned. |
| `visor_tint` | **`("#123a5c", 0.055, 0.08, 0.0)`** | hex `derived` from #083858. Roughness **0.08** makes it the smoothest surface on the kart, level with `tube_chrome` and glossier than the wrap's 0.16 — which is true of a real visor. Metallic 0.0: it is tinted polycarbonate, not a mirror. Luminance below the measured 0.075 on purpose, because most of that read *is* the sky reflection a roughness-0.08 material will generate for itself. |
| `glove_leather` | **`("#242428", 0.030, 0.55, 0.0)`** | hex `derived` from #181818/#3b3d41. Roughness 0.55: perforated suede, matte, and definitely not `rubber_gloss`'s 0.40, which is what it is standing in as. |
| `boot_leather` | **`("#26262c", 0.034, 0.38, 0.0)`** | hex `derived` from #282838. Glossier than a glove — a karting boot is smooth coated leather. |

**Three findings from those photographs, all outside this module's geometry.**

1. **§60.3.8's claim that the suit is the brightest thing on the kart is half right
   and the wrong half is load-bearing.** It says the radiator is *"the brightest
   object on the kart besides the driver's suit"*. Measured, the suit's **navy body
   is 0.037** and its **white chest panel is 0.639**, against a radiator at 0.311.
   The bright thing is a panel, not the garment. A race suit is a multi-panel livery
   object — navy body, white chest, fluorescent yellow-green shoulder and sleeve
   stripes, sponsor blocks — and `overalls_fabric` is one material, so the panels are
   a texture this pipeline cannot write. Same standing as `ZONES`' owed glyph: the
   material is the navy body and the rest is owed, said plainly rather than left to
   be discovered in a render.
2. **Neither driver wears a visible rib protector.** In both frames the torso is
   plain overalls with no external shell, which means the protection is worn
   *under* the suit. §60.1.5 says *"Adds 12–18 mm per side over the overalls"* and
   `driver.py` builds it 1 mm proud accordingly, so the built part is a vest *over*
   the suit. Both products exist and 8870-2018 covers both, but the photographic
   evidence in this repo is for under, and under makes `driver_rib_protector` an
   invisible part — which §60.1.6 argues against on its own terms. Flagged as a
   decision, not changed.
3. **The head is upright and the elbows are tucked.** Both drivers hold the helmet
   vertical with the face pointing where the kart is going, which is the independent
   support for building the shell axis-aligned rather than raked with the torso
   (§60.1.7 item 2). Both hold the elbows *below the shoulder and barely outside the
   torso line*, forearms angling up and inboard to the rim — which is why
   `ELBOW_SWIVEL` is down with a quarter of that outboard rather than the 45° wing an
   armchair guess produced. And both grip at roughly **10 and 2 o'clock**, not
   §60.2.1's 3 and 9. Three-and-nine is kept because it is the straight-ahead datum
   the whole reach table is built on, but it is not where a driver's hands are, and
   10-and-2 is the *harder* reach — it is 30° of the lock sweep §60.2.5 tabulates,
   at 644 mm instead of 592.

### 60.1.9 What the driver is built inside, measured — gate 3's seed list

Issue #200's waiver list, measured off the built mesh at high detail with a
triangle-overlap test and a ray-crossing depth probe in both directions. **64
intersecting pairs across 34 distinct kart parts**, none of them fixed here:
§60.1.6's rule is that a finding which cannot be adjudicated against a sourced
figure gets a waiver and a ticket, and the leg path hangs off `driver_knee_x` = ±180
and `driver_ball_z` = 0.090, one `estimated` off a photograph and one stale.
(#202 has since deleted the stale half: the ball is `P.driver_ball`, solved off
the live pedal, and the leg rows below are due their re-measure.)

Grouped by cause, because thirty-four parts is not thirty-four problems:

| cause | pairs | depths | disposition |
| --- | --- | --- | --- |
| **the six declared contacts** — `seat_shell` wrapping the hips and torso, `steering_rim` inside both gloves | 7 | up to 74.6 (`seat_shell` in `driver_pelvis`), 42.5 (`steering_rim` in a glove) | **correct, not defects.** A `sits_on` and a `grips` row permit exactly this. |
| **the boots 138 mm below the pedals** — `pedal_throttle` 61.5, `pedal_brake` 34.3, `pedal_brake_clevis` 11.5, `brake_pushrod` 9.0, `brake_line_rear` 7.2, `brake_pushrod_link` 6.5, `brake_master_rear` 3.4, `brake_master_bracket` 2.2, `steering_pitman` 1.4, `chassis_steering_hoop` 1.1, `tierod_?` 2.2, `tierod_end_?_inner` 0.8, `chassis_floor_tray` 0.0 | 24 | 0.0 – 61.5 | **one parameter fix, §60.2.5.** Most of this list disappears when `driver_ball_*` tracks the live foot bar. |
| **the wheel in the knees** — `steering_rim` in `driver_shank_?` 17.5 and `driver_thigh_?` 10.7 | 4 | 10.7 – 17.5 | **§60.2.3 predicted it to the millimeter** at a wheel centre near 190; the live centre is 187.3. Rests on `driver_knee_x` ±180, `estimated`. Waiver. |
| **the fuel tank between the shins** — `fuel_tank_strap_front` 52.3, `fuel_tank` 36.9 in the shanks and 23.4 in the boots, `fuel_tank_strap_rear` 0.8 | 8 | 0.8 – 52.3 | a KZ tank does live between the legs, but not 52 mm into them. Real, and it is a tank-placement question. |
| **the chain run through the right hip** — `drive_chain_guard` 20.4 / 18.8, `drive_chain` 9.9 / 7.0, `drive_output_sprocket` 6.3 / 3.4, `drive_output_shaft` 5.5 / 3.3 | 8 | 3.3 – 20.4 | real. The pelvis is 325 wide and the output end is inside that. |
| **the upper radiator hose through the driver again** — `radiator_hose_upper` in `driver_rib_protector` 13.7, `driver_torso` 33.6, `driver_upper_arm_r` 9.0 | 3 | 9.0 – 33.6 | **measured against the two-point `HOSE_UPPER_ROUTE` that was in this worktree**, `((0, −0.4, 0.39), (0.15, −0.3, 0.375))`. A three-point reroute behind the seat back has since landed elsewhere and is *not* in these numbers — re-measure after the merge. This is the same class of defect ADR-0055 cites as the reason the driver exists at all. |
| **the front bumper sockets and side bars at the ankles** — `chassis_bumper_socket_side_upper_front_?` 14.4 / 7.3, `chassis_side_bar_upper_?` 3.4, `chassis_tray_edge_?` 5.6, `chassis_steering_support_upper` 9.3, `steering_clutch_lever` 7.0 | 9 | 3.4 – 14.4 | mostly a consequence of the boots being low; re-measure after the `driver_ball_*` fix. |
| **the engine on the driver's right** — `engine_head` 4.5 in `driver_upper_arm_r` | 1 | 4.5 | real but marginal. An earlier 45° elbow swivel put the arm 30 mm into `engine_plug_lead`; the photograph-corrected swivel (§60.1.8) removed that pair entirely. |

**Two of the eight groups are single parameter edits, not geometry work**, and both
are named in §60.2.5. That is the useful shape of this list: it is not sixty-four
independent faults.

---

## 60.2 The reach arithmetic — does the cockpit fit the driver?

Issue #17 wants the hands to reach the wheel at full lock. It is arithmetic, and
the arithmetic does not close.

### 60.2.1 The wheel's rim, in world coordinates

`sourced` from `params.py`: `wheel_diameter` 320 (radius 160), `wheel_angle` 0.470
rad = 26.93° from vertical, center at `(0, 320, 480)`.

The column axis runs forward and down from the center, `(0, sin θ, −cos θ) =
(0, 0.4527, −0.8917)`. The disc lies in the plane normal to it, spanned by
`e1 = (1, 0, 0)` and `u = (0, 0.8917, 0.4527)`. So `u` is the in-plane "up", and
the top of the rim is **forward** of the center — the driver looks down at a wheel
whose far edge is its top, which is what a laid-back kart column does.

    rim(phi) = C + 160 * (sin phi * e1 + cos phi * u)

| rim point | position | `derived` |
| --- | --- | --- |
| top / far edge | (0, 462.7, 552.4) | 320 + 160·0.8917 ; 480 + 160·0.4527 |
| bottom / near edge | (0, 177.3, 407.6) | |
| 3 and 9 o'clock — the hands | **(±160, 320, 480)** | |

### 60.2.2 Straight-line reach required, and available

Shoulder joint (200, −393, 608) to the right hand's grip at 3 o'clock
(160, 320, 480):

    dx = 40      dy = 713      dz = -128
    d  = sqrt(1600 + 508369 + 16384) = sqrt(526353) = 725.5 mm     REQUIRED

| available | value | verdict |
| --- | --- | --- |
| `params.py` as it stands: `driver_upper_arm` 290 + `driver_forearm` 260 | **550** | **short by 175.5 mm.** And these are bones — the forearm ends at the wrist, so there is no hand in the number at all. |
| sourced 50th-pct male: upper arm 368 + elbow-to-fist 361 | **729** | closes by **3.5 mm**, with the elbow at 180°. A driver cannot steer with locked arms. |
| comfortable, elbow at 110°: `sqrt(368² + 361² − 2·368·361·cos 110°)` | **597.2** | the wheel is **128.3 mm too far from the shoulder.** |

**At full lock it is worse.** Kart steering is direct; a hand starting at 3 o'clock
travels up the rim as the wheel turns.

| wheel rotation | outside hand's rim point | shoulder distance | vs 729 available |
| --- | --- | --- | --- |
| 0° (straight) | (160, 320, 480) | 725.5 | 3.5 mm spare |
| 60° | (80, 443.6, 542.7) | 847.7 | **short 118.7** |
| 90° (top of rim) | (0, 462.7, 552.4) | 880.5 | **short 151.5** |

So issue #17's requirement fails by 119–152 mm depending on how much lock is
called full, using the *generous* sourced segment lengths. With the segment
lengths actually in `params.py` it fails by 300 mm.

### 60.2.3 What should move, and why it is not one number

Solving for the `wheel_center_y` that puts the straight-ahead grip at a 110° elbow,
with `wheel_center_z` unchanged at 480:

    dy = sqrt(597.2^2 - 40^2 - 128^2) = sqrt(338664) = 581.9
    wheel_center_y = -393 + 581.9 = 188.9   ->  190 mm      (from 320)

That is a **130 mm move rearward** and it is the dominant error. But it cannot be
made alone, and this is the interesting part:

- **At the current 320 the knees are clear.** The rim's rearmost point is
  y = 177.3 and the knee joint is at y = +131.7 (#202 moved it 8.7 forward), so
  the wheel is entirely ahead of the knees with 45.6 mm to spare. No conflict,
  and no reach either.
- **At 190 the rim's lower arc runs through the knee sweep.** At the knee's
  y = 131.7 the rim passes `x = ±146, z = 450`; the knee joint is at
  `(±180, 131.7, 433.5)`, so the rim's lower arc lies **34 mm inboard of the knee
  joint and 17 mm above it**.
  It works only because the knees straddle the column — which is precisely what
  `exh_commons_buntschu_kz2.jpg` shows, and precisely why that ±180 is the number
  the geometry is most sensitive to and the least well sourced.
- **Raising the wheel is nearly out of headroom.** Art. 9.1.1 caps chassis height
  at 650 mm from the ground, seat excluded, and the front matter reads the steering
  wheel as living under it. The rim's top is at `wheel_center_z + 160·cos 26.93° =
  wheel_center_z + 142.7`, so `wheel_center_z ≤ 507.3`. There is **27 mm** of
  vertical adjustment available, not 100.

**Verdict: the cockpit does not fit the driver.** `wheel_center_y` should come back
from 320 to about **195**, `wheel_center_z` may rise as far as **505** and no
further, and the fit then depends on a knee splay of ±180 that is read off one
action photograph. That is a two-parameter fit against a soft constraint and it
belongs in a ticket with the knee splay measured properly, not in a one-line
parameter edit. It is also the wrong reach *before* the arm segments are corrected:
290 + 260 is a small adult's arm with no hand on the end of it.

### 60.2.4 The pedals — these close, comfortably

Hip (−170, 130) to the ankle at (456.8, 153.6): **627.3 mm** required.
Available thigh + shank: 428 + 429 = **857 mm**.

    slack             = 857 - 627 = 230 mm
    leg extension     = 627 / 857 = 73%

**The pedals reach with 262 mm to spare.** If anything they are *close*: 69% is a
deeply folded leg, and a driving-position convention of 80–85% extension would put
the pedal face nearer y = 660 than y = 560. That is a comfort observation, not a
defect — a KZ driver's knees genuinely are up around the wheel.

Cross-check against the two figures the §40 Cockpit agent measured:

| their figure | this spec | agree? |
| --- | --- | --- |
| hip to pedal face **735** | **731.1** | yes, 4 mm / 0.5% |
| seat pan front edge to pedal face **495** | **611** from the Tillett front lip at y = −50, `sqrt(610² + 42²)` | **no, 116 mm apart** |

Both are right about different seats. 495 implies a front lip at y ≈ +65; the
sourced T11 ML puts it at **y = −50**. So the *built* seat mesh's front lip stands
about 115 mm forward of where a real T11 ML's does — consistent with
`notes_radiator.md` §6's finding that `seat_height = 0.290` is 45 mm short of the
sourced 335 and that the shell is built as a single-width box. The hip agreeing to
4 mm while the lip disagrees by 116 says the *seat* is the part that is wrong, not
the driver and not the pedals.

### 60.2.5 The cockpit moved. Everything above §60.2.4 is measured against a wheel and a pedal box that no longer exist

Issue #17, measured off the built mesh. **§60.2.1 through §60.2.4 are stale at the
premise, and the answer changes sign.**

Every number in §60.2.1 comes from `wheel_angle` 0.470 and an authored
`wheel_center_y` 0.320 / `wheel_center_z` 0.480. **All three fields are deleted from
`params.py`.** The steering is now authored from the welded end: `column_rake` is
0.628 (36.0°, measured on the column tube in `tonykart_racer401T_product.png` and
corrected for that image's 11% anisotropy), `wheel_incline_delta` adds 7.0° of
inclined-hub wedge, and `P.wheel_center` *derives* the centre from the lower bore.
That field's own docstring says it: *"the old pair was 133 mm too far forward."*

| | §60.2 as written | live, `P.wheel_center` / `P.wheel_rake` |
| --- | --- | --- |
| wheel centre | (0, **320**, 480) | **(0, 187.3, 495.9)** |
| wheel plane rake from vertical | 26.93° | **42.97°** |
| right-hand grip, 3 o'clock | (160, 320, 480) | **(160, 187.3, 495.9)** |

So the reach, re-measured with the *same* sourced segment lengths:

| | required | available 368 + 361 | verdict |
| --- | --- | --- | --- |
| straight ahead | **592.4** | 729.0 | closes, **136.6 mm of fold**, elbow at **108.71°** |
| 30° of lock | 644.4 | 729.0 | 84.6 mm spare |
| 60° of lock | 692.4 | 729.0 | 36.6 mm spare |
| 90° of lock, hand at the top of the rim | 725.5 | 729.0 | 3.5 mm spare, elbow locked |

**§60.2.2's verdict is reversed and §60.2.3's fix has already been applied.**
§60.2.3 solved for the `wheel_center_y` that puts the straight-ahead grip at a
110° elbow and got **190**; the live derived centre is **187.3**, and the built elbow
comes out at **108.71°** — within 1.3° of the comfort target that section named,
from a chain that never used it. Two independent derivations landing 2.7 mm apart is
the strongest corroboration in this section.

**And §60.2.3's warning about the knees was right to the millimeter.** It predicted
that at a wheel centre near 190 the rim's lower arc would pass "39 mm inboard of the
knee joint and 4 mm above it", and that it works only because the knees straddle the
column. Built and measured: `driver_thigh_l/r` and `driver_shank_l/r` each intersect
`steering_rim`, by **8.5 mm** and **17.5 mm** respectively. The knees do straddle the
column and they graze the rim doing it. That interference rests entirely on
`driver_knee_x` = ±180, which is `estimated` off one three-quarter action
photograph, so per §60.1.6 it is a waiver and a ticket rather than a geometry change.

**The pedals, by contrast, have gone the other way and this one is a defect.**
`driver_ball_*`'s docstring calls itself `sourced` from `pedal_y` / `pedal_z`. Those
fields no longer hold those values: the pedal box was re-authored as an organ pedal
on a bottom pivot, so `pedal_z` is the **derived** foot-bar height 0.228 (its own
docstring: *"Was 0.090 — 21 mm above the floor tray, which is a foot resting on the
floor and not a pedal. 138 mm of error"*), `P.pedal_bar_y` is 0.585, and
`pedal_separation` moved 150 → **170**.

| | driver block | live cockpit | Δ |
| --- | --- | --- | --- |
| ball of foot, x | ±75 | ±85 | 10 |
| ball of foot, y | 560 | 585.0 | 24.9 |
| ball of foot, z | **90** | **228.2** | **138.2** |

Total 140.8 mm. So `driver.py` builds the boots where the driver block says, and the
result is measured at **56.84 mm** of gap from each `pedal_*_pad` while the boot
passes *through* `pedal_brake` / `pedal_throttle` — the arm — by 9.1 mm and through
`pedal_brake_clevis` by 11.2 mm. Both `presses` contacts in
`joints.DRIVER_CONTACTS` therefore fail by 54.8 mm against `CONTACT_TOLERANCE`, and
about a third of the driver's interference list is boots down in the pedal linkage
where a foot 138 mm lower than the pedal has to be. The whole leg chain hangs off it:
`driver_ankle_*` and `driver_heel_*` are derived from that ball, and
`driver_knee_*`'s two-link solve closes on that ankle.

**This is the one finding here that is a straight parameter fix rather than a
decision.** `driver_ball_x` → `pedal_separation / 2`, `driver_ball_y` →
`P.pedal_bar_y`, `driver_ball_z` → `P.pedal_bar_z`, then re-run §60.1.4's ankle,
heel and knee arithmetic. It is not made here because `params.py` is single-owner and
the driver block is §60.1.4's, not #17's. `driver.py` prints the divergence in
millimeters on **every build** so the waiver cannot rot.

**Resolved, #202 — and by deletion rather than by closing the number.** The
`driver_ball_*` / `driver_ankle_*` / `driver_heel_*` / `driver_knee_y/z` fields
are gone from `params.py`; `P.driver_heel/ball/ankle/knee` solve the whole chain
off `pedal_bar_y` / `pedal_bar_z` / `pedal_separation` on every read (§60.1.4 has
the arithmetic), so the citation-true-but-number-false failure has no field left
to live in. Re-measured off the built mesh: `driver_boot_l`/`pedal_brake_pad`
and `driver_boot_r`/`pedal_throttle_pad` each at **0.20 mm** (was 56.84), the
residual being mesh discretization of an exact sole-to-bar tangency — both
`presses` contacts in contact within `CONTACT_TOLERANCE` 2.0. The divergence
print is replaced by a pose print: foot pitch 53.7°, ball 0.00 mm off the bar
surface.

---

## 60.3 Finishes, per part group

Twelve materials cover 146 parts. The grouping below is by *finish*, not by
material name, because three of the twelve names are each doing two or three jobs.

Every claim names the image it came from. **Read `refs/kart-visual/` with the
seat trap in mind:** `notes_radiator.md` §7 records that the three Tony Kart
frames and `crg_roadrebel_kz_side.webp` are all **seatless studio chassis shots**,
and that the graphics-wrapped object in the middle of `tonykart_rear_header.jpg`
is the **front panel 1.2 m away**, not a seat. One further frame is discussed in
§60.3.7 and its identification is flagged rather than asserted.

### 60.3.1 Powder-coated steel tube — 33 parts, `frame_powdercoat`

`chassis_rail_*`, `chassis_cross_*`, `chassis_*_hoop_*`, `chassis_side_bar_*`,
`chassis_seat_strut_*`, `chassis_bearing_hanger_*`, `chassis_rear_bumper`.

Art. 9.4 requires the protections be *"made of magnetic steel round tubing"*, so
the tube material is regulated; the coating is not.

| property | spec |
| --- | --- |
| base color | **a livery variable, not a fixed value.** See §60.5. |
| finish | **gloss** powder coat, not satin |
| roughness | **0.30** — `frame_powdercoat`'s 0.42 is too rough. `crg_roadrebel_kz_detail7.webp` shows a hard specular streak running the length of every tube, and a 30 mm tube at 0.42 will not produce it. |
| metalness | 0.0 |
| wear | chips to bright steel on the rails' underside where the kart is dropped on a stand; a polished ring at every bolted clamp. `estimated` — **no photograph in this repo shows a used frame.** |

Images: `crg_roadrebel_kz_detail7.webp` (gloss black rails),
`tonykart_racer401T_p01.jpg` (gloss green tube, and the reason §60.5's Heritage
palette is not invented).

**Four sets of parts are in this group and should not be:**

| part | current | should be | image |
| --- | --- | --- | --- |
| `chassis_rear_bumper`, `chassis_nose_hoop_*` | `frame_powdercoat` | `tube_chrome` — **new.** Bright chrome or polished stainless. In `crg_roadrebel_kz_detail7.webp` the front bumper tube crossing the top of the frame is unmistakably chrome against gloss-black rails. Roughness 0.08, metalness 1.0. | detail7 |
| `pedal_brake`, `pedal_throttle`, `pedal_mount_*`, `pedal_cross_tube` | `frame_powdercoat` | `stainless_polished` — **new.** The pedal loop at the lower left of `crg_roadrebel_kz_detail7.webp` is raw polished stainless, brighter and cooler than any coating, with visible bend-forming marks. Roughness 0.18, metalness 1.0. | detail7 |
| `engine_airbox`, `engine_airbox_lid`, `engine_battery` | `frame_powdercoat` | `plastic_matte_black` — **new.** Moulded, not painted. Reads flatter and browner than powder coat. Roughness 0.55, metalness 0.0. | `exh_commons_shifter_engine.jpg` (filter and airbox) |
| `steering_column`, `steering_clutch_lever`, `shifter_lever`, `shifter_base` | `frame_powdercoat` | column and lever are plated steel (`tube_chrome`); the clutch lever and shifter base are anodized aluminum (`anodized_clear`). | `crg_roadrebel_steering.webp`, `birelart_kz_steering_column.jpg` |

### 60.3.2 The floor tray — 1 part, `tray_aluminium`, and the committed finish is wrong

`chassis_floor_tray`. Committed as "anodized aluminum". The photograph disagrees
specifically and it is the largest single surface on the kart.

`tonykart_racer401T_p01.jpg` is a close-up of the tray and shows a **granular
anti-slip surface**, white-silver, coarse enough to resolve individual grains at
that magnification, printed over with green and red pinstripes and a `RACER 401T`
decal. Sampled across the frame the surface's dominant mode is **#c8c8d2** —
brighter than any metal on the kart and reading as a *coating*, not as aluminum.

| property | spec | prov |
| --- | --- | --- |
| base color | **brighter than everything else on the chassis**, roughly 4× the powder coat's value; a neutral white with a faint cool cast | `derived` from p01, mode #c8c8d2, studio white-sweep lighting so the cool cast is partly the photograph |
| roughness | **0.85** | `estimated` from the granular texture; `tray_aluminium`'s 0.58 is a metal's number |
| metalness | **0.0** | it is a coating over aluminum, and it does not read as metal |
| decoration | a printed decal zone, chassis-brand and pinstripes; see §60.5 | `sourced`, p01 |
| wear | heel scuffs wearing the grain smooth in two ovals, plus chain-oil fling along the right-hand edge | `estimated`, **and see §60.6 — on this kart the heels land 299 mm ahead of the tray's front edge (#202), so this wear cannot happen where it should** |

`tray_aluminium`'s comment argues the value down because a bright metal slab
"swamps the frame it is bolted to". That instinct was right and the diagnosis was
wrong: it is not a dark metal, it is a bright non-metal.

### 60.3.3 Tires — 4 parts, `tire_rubber`

| property | spec |
| --- | --- |
| base color | the darkest thing on the kart; a brown-black, warmer than the frame's blue-black gloss |
| roughness | 0.72 sidewall (keep), **0.62 on the tread face** — a scrubbed slick is glossier than its sidewall |
| metalness | 0.0 |
| detail | fine circumferential mould lines across the tread, clearly visible in `crg_roadrebel_kz_detail7.webp`; moulded sidewall lettering |
| wear | **marbles** — discrete rubber pellets picked up on the shoulders; a bluish sheen on the worked band; a dust film over everything after a session | `exh_commons_buntschu_kz2.jpg`, on track and in use, is the only in-repo image of a tire that has done any work |

### 60.3.4 Rims — 4 parts, `rim_magnesium`

Cast magnesium. Two finishes, both committed and both confirmed:

| variant | relationship | roughness | metalness |
| --- | --- | --- | --- |
| **gold anodized** | warmer and considerably darker than the regulation number yellow; duller and greyer than brass; warmer than the aluminum radiator | 0.35 | 1.0 |
| **black anodized** | flatter and slightly lighter than the frame's gloss powder coat — an anodize is never as glossy as a coating | 0.45 | 1.0 |

Images: `exh_commons_buntschu_kz2.jpg` (gold, all four wheels, in use);
`crg_roadrebel_kz_detail7.webp` (a gold-anodized flange collar on a **black**
anodized hub — the two finishes appear on one assembly, which is the detail worth
having).

`rim_magnesium` at (0.380, 0.375, 0.360) roughness 0.30 is a plausible *bare*
magnesium and is neither of the two finishes that are actually sold. Wear: brake
dust darkening the rear rims' inner faces, and bead-flange scuffing from tire
fitting. `estimated` — buntschu is too small to resolve either.

### 60.3.5 Steel running gear — 10 parts, `axle_steel`, and it is four finishes

| part | finish | image |
| --- | --- | --- |
| `axle_rear` | heat-treated steel, dark satin. Keep 0.42 / 1.0. Wear: **bright polished bands** where the bearings, hubs and sprocket carrier clamp — the only part of a 1,080 mm axle that ever gets touched. | `estimated` |
| `drive_chain` | **`chain_oiled` — new.** Near-black with oil, and *not* the axle's value: the roller ends and the tooth-contact flanks polish to bright bare steel while everything between them holds black oil and dust. Base color darker than `axle_steel`, roughness 0.30 on the worked facets, 0.55 elsewhere. | `estimated` — **no in-repo photograph shows a used chain.** Both CRG frames are new karts with the chain guard fitted. |
| `axle_sprocket`, `drive_output_sprocket` | bright machined steel or 7075; a **swept, polished flank** on the drive side of every tooth and a duller cast/blank face between. | `estimated` |
| `axle_stub_fl/fr` | bright zinc-plated steel, cooler and brighter than the axle, with a faint yellow-passivate cast on the nuts. Clearly resolved. | `crg_roadrebel_kz_detail7.webp` |
| `exhaust_spring_0/1` | bright spring steel, **heat-tinted straw at the header end and clean at the silencer end** | `derived` from the exhaust gradient, §60.3.9 |
| `engine_plug_hex` | black oxide hex | `estimated` |

### 60.3.6 Bodywork — 4 parts, `bodywork_plastic`, and it is two finishes on one part

`bodywork_nose_fairing`, `bodywork_rear_panel`, `bodywork_sidepod_l/r`.

The substrate is moulded polyethylene/polypropylene; the *read* is dominated by
the **printed vinyl wrap over it**, which is much glossier than the raw plastic and
carries a clear laminate.

| surface | relationship | roughness | metalness |
| --- | --- | --- | --- |
| wrapped outer faces | color is a livery variable; a hard clear-coat highlight, glossier than the powder-coated frame | **0.16** | 0.0 |
| unwrapped inner faces and mounting flanges | the substrate: satin, flat, no highlight, and normally the panel's own moulded color rather than the livery's | 0.55 | 0.0 |

Images: `crg_roadrebel_kz_detail11.webp` (the pod's wrapped outer face carries a
long unbroken specular streak); `tonykart_racer401T_p06.jpg` (wrap texture);
`birelart_kz_graphics.jpg` (the wrap **artwork itself**, die-cut per panel — the
single most useful livery reference in this repo).

`bodywork_plastic`'s 0.28 splits the difference between the two and gets neither.
Wear: stone chipping along the fairing's leading edge and scuffing on the pods'
lower outboard corners where kart-to-kart contact lands. `estimated` — every
bodywork image in this repo is a studio shot of a new panel.

### 60.3.7 The seat — 1 part, `seat_fiberglass`, and this one is badly wrong

`seat_shell`. Committed finish: "translucent fiberglass seat". Correct — and
`seat_fiberglass` is set to `(0.055, 0.055, 0.058)`, i.e. **near-black**, which is
the opposite of what a bare fiberglass kart seat looks like.

| property | spec | prov |
| --- | --- | --- |
| base color | **one of the brightest objects on the kart** — a pale grey with a distinct green cast, roughly 4–5× `frame_powdercoat`'s value; greyer and greener than the tray, much duller than the spark-plug porcelain | `derived` from an image, with the caveat below |
| finish | gel-coated fiberglass: glossy, with a slight orange-peel | `sourced` (Art. 4.8 *"It may be made of composite material"*; Tillett shells are gel-coated) |
| roughness | **0.22** | `estimated` |
| metalness | 0.0 | |
| translucency | real, and visible at the thin flared lip and the flank where light passes through — 4 mm of glass laminate is not opaque | `estimated` |
| wear | glass fiber print-through where the driver's shoulder blades and hips rub the gel coat away; four reinforcement discs (Art. 4.8.1, ≥1.5 mm, ≥13 cm², ≥40 mm dia.) at the upper stay bolts; a transponder on the back (Art. 3.11) | `sourced` for the discs and the transponder via `notes_radiator.md` §6 |

**The image, flagged rather than asserted.** The large pale grey-green shell filling
the upper right of `crg_roadrebel_kz_detail11.webp` is, on my reading, the **seat's
outboard flank and rolled lower lip** — the rolled edge is characteristic and
nothing else on a kart has that flare. But `notes_radiator.md` §7 states flatly that
there is no photograph in this repo showing a KZ seat, and the alternative reading
is the far sidepod's inner face seen across the kart. I could not settle it. So:
the *relationship* (pale, glossy, green-grey, far brighter than the frame) is what
this row asserts, and it is a general fact about bare fiberglass kart seats that any
Tillett product page corroborates; the specific frame is offered as evidence with
that doubt attached. **This does not license leaving the material at near-black** —
whichever object it is, nothing in that frame supports 0.055.

### 60.3.8 Engine and machined alloy — 75 parts under `engine_alloy`, which is three finishes

75 of 146 parts share one material. It covers three surfaces that photograph
nothing like each other, and `exh_commons_shifter_engine.jpg` has two of them in
one frame, lit identically, which is what makes the split measurable rather than
asserted:

| finish | parts | relationship | rough | metal |
| --- | --- | --- | --- | --- |
| **raw sand casting** | `engine_crankcase_*`, `engine_cylinder*`, `engine_head`, `engine_clutch_cover`, `engine_ignition_cover`, `engine_reed_block`, `engine_starter`, `engine_water_*`, all nuts and bolts on them | warm grey with a tan-green cast, matte, no directional highlight. Measured in that frame at #605944 against the machined billet's #f6f4ca in the same light — the casting is **less than half the billet's value and warmer.** A magnesium casting is warmer and greyer still than an aluminum one. | 0.66 (keep) | 1.0 |
| **machined billet / clear anodize** — new `anodized_clear` | `engine_mount_plate`, `engine_mount_clamp_*`, `radiator_bracket_*`, `steering_boss`, `steering_spokes`, `steering_bearing`, `drive_sprocket_carrier` | brighter, whiter and much smoother than any casting, with **directional machining marks**. Also the caliper bodies and master cylinders in `crg_roadrebel_kz_detail7.webp` and `detail11.webp`. | **0.35** | 1.0 |
| **gold anodize** — new `anodized_gold` | wheel hub collars, disc carriers, and whichever fasteners the livery wants | warm and saturated, darker than the number yellow, lower roughness than a casting | 0.30 | 1.0 |
| **brazed radiator aluminum** — new `radiator_alu` | `radiator_fin_*` (19), `radiator_tank_high/low`, `radiator_end_*`, `radiator_divider`, `radiator_cap` | brighter and **cooler** than any casting — an unpainted brazed core is the only cool-white metal on a KZ. Confirmed at distance in `exh_commons_buntschu_kz2.jpg`, where the radiator is the brightest object on the kart besides the driver's suit. | 0.45 | 1.0 |

`radiator_core` already has its own material at 0.120. The 24 parts that *make up*
the radiator are on `engine_alloy`, i.e. the core is one value and its own tanks and
fins are a sand-casting's value. `anodized_gold` is confirmed twice, on the CRG
front hub collar (`detail7`) and on the rear disc carrier (`detail11`, cropped).

Wear: the exhaust-side of the cylinder and head discolor; oil film and track grime
collect in every draft angle and casting radius on the crankcase; the clutch cover
polishes bright along the edge the driver's right heel strikes. All `estimated` —
`exh_commons_shifter_engine.jpg` is the only used engine in the repo and its header
is heat-wrapped, which hides the gradient rather than showing it.

### 60.3.9 Exhaust — 2 parts, `exhaust_steel`, and the gradient is the whole finish

`exhaust_chamber`, `exhaust_silencer`, plus `exhaust_flange` and
`exhaust_flange_nut_*` which are wrongly on `engine_alloy`.

Committed finish: nickel-plated. Correct. But **a single base color cannot express
this part**, because the interesting thing about a two-stroke pipe is a heat
gradient along its own axis, from the flange outward:

| position along the pipe | appearance |
| --- | --- |
| flange and header, hottest | matte grey-brown, plating burnt off, the darkest metal on the kart |
| header into the diverging cone | blue-purple oxide band |
| convergent cone | straw and gold |
| stinger and silencer, coolest | bright nickel, near-chrome |

`exhaust_steel` at 0.36 / (0.088, 0.085, 0.084) is roughly the *flange* end and is
wrong for the silencer by an order of magnitude in value. This is a per-part
gradient texture or a vertex-color ramp, not a material value, and it should be
specified that way.

Images: `exh_commons_shifter_engine.jpg` (a used, discolored, heat-wrapped header
— the gradient's existence, not its detail); `exh_eurokart_*` and
`exh_fastech_tmkz_pipe.png` (new plated pipes, the bright end of the ramp).
The banding *sequence* is `estimated` from general steel-oxide temper colors; no
photograph in this repo resolves it on a kart pipe.

### 60.3.10 Brakes — `new` parts, and the committed finish is half wrong

The committed finish is "drilled steel brake discs". Drilled: yes. Steel-bright:
**no**, and this is the clearest disagreement between the committed list and the
photographs.

`crg_roadrebel_kz_detail7.webp` (front) and `crg_roadrebel_kz_detail11.webp` (rear)
both show a disc that is **dark grey to near-black**, with a ring of small drilled
holes and a scalloped or waved periphery — not a bright turned steel face. Sampled
in the crop at #303330. Ventilated iron or a dark-coated steel.

| property | spec | prov |
| --- | --- | --- |
| base color | dark, close to the frame's black but greyer and flatter; distinctly *not* a bright metal | `derived`, detail7 + detail11 |
| roughness | 0.55 | `estimated` |
| metalness | 1.0 | |
| wear | a **bright swept annulus** inside the pad track, polished to near-mirror; dark and lightly rust-bloomed outside it; polished bright rings around each drilled hole's edge | `estimated`, **and this is the wear claim with the weakest evidence in this section — both reference discs are brand new and show no swept band at all.** |
| caliper | clear-anodized machined aluminum, bright, with black-anodized detail parts | `sourced`, detail7 and detail11 both |
| master cylinder | **anodized, and the color is a livery variable** — detail7 shows two *red* anodized cylinders, detail11 shows a black one | `sourced`, both |
| brake line | **red** PVC or braided sleeve, saturated, the most chromatic non-livery item on the kart | `sourced`, red in both frames |

### 60.3.11 Rubber, hose and small parts — 10 parts under `rubber_grip`

| part | finish | image |
| --- | --- | --- |
| `radiator_hose_upper/lower` | **`hose_silicone` — new.** A saturated color, not black: `notes_radiator.md` §5 records the CRG runs as **orange** over a protective sleeve, at ~33 mm OD against 28 mm of bare hose. Roughness 0.42. Sleeved sections read as a woven braid, not as smooth rubber. | `crg_roadrebel_kz_side.webp` via notes_radiator §5 |
| `steering_rim` | moulded rubber, suede or leather; **a livery variable** — red-and-black in `exh_commons_buntschu_kz2.jpg`. Wear: darkens and polishes where the hands sit, at 9–10 and 2–3 o'clock. | buntschu |
| `engine_plug_lead`, `engine_plug_cap` | black rubber, glossier than the hose, roughness 0.40 | `exh_commons_shifter_engine.jpg` |
| `engine_intake_boot` | black rubber, matte, roughness 0.70 | same |
| `engine_throttle_cable` | black outer sheath with a bright steel nipple and adjuster | `tonykart_racer401T_p04.jpg` |
| `pedal_brake_pad`, `pedal_throttle_pad` | black rubber with a moulded tread. Wear: a smooth flat worn where the ball of the foot lands. | `estimated` |
| `shifter_knob` | black, or an anodized ball — a livery variable | `estimated` |

Also `plug_ceramic`: glazed white porcelain, keep as the brightest object on the
engine; wear is a light brown ring at the insulator's base from blow-by,
`estimated`.

### 60.3.12 New parts the other sections add

| part | finish |
| --- | --- |
| fuel tank | translucent polyethylene, natural amber-white, under a **two-piece printed wrap**. `birelart_kz_graphics.jpg` carries a dedicated `SERB 9Lt` left/right decal pair, so the tank is a livery surface in its own right. 9 litres clears Art. 9.3's *"8 litres minimum"* (PDF p. 22). The wrapped tank in `crg_roadrebel_kz_detail11.webp` is glossy; the unwrapped upper surface is satin and translucent. |
| front panel | wrapped plastic per §60.3.6, plus the number zone of §60.4 |
| number panels | flexible opaque plastic. Art. 3.7: *"The number plates must be made of flexible opaque plastic and be visible at all times."* Matte, roughness 0.45 — a number plate does not carry the bodywork's clear laminate, and it must stay legible off-axis. |
| tie rods | bright chrome or zinc-plated rod with **black-anodized** rod ends. Clearly resolved in `crg_roadrebel_kz_detail7.webp`. |
| exhaust support | carbon fiber twill with a resin gloss — `exh_eurokart_3.jpg` is a New-Line carbon exhaust support, and the weave scale is readable. Stainless band clamp with a zinc worm drive. |
| curtain / radiator blind | matte black composite, one piece, screwed to the core's side rails (Art. 5.3.1) |

### 60.3.13 The honest state of the wear evidence

| wear claim | photograph behind it |
| --- | --- |
| tires — marbles, blued band, dust | **yes**, `exh_commons_buntschu_kz2.jpg` |
| engine castings — grime in the draft angles | **partly**, `exh_commons_shifter_engine.jpg` |
| exhaust — a heat gradient exists | **partly**, same frame; the banding sequence is not resolved |
| chain — oiled dark with bright facets | **no** |
| disc — swept band and rust | **no**, both refs are new discs |
| tray — heel scuffs | **no**, and §60.6 says the heels do not even land on it |
| rims — brake dust, bead scuffs | **no** |
| bodywork — chips and scuffs | **no** |
| frame — chips at the rails | **no** |

Six of nine have nothing behind them, for one reason: **every chassis and bodywork
photograph in this repo is a studio shot of a brand-new kart.** Wear is what
separates this from a CAD render, and this repo currently cannot source it. That is
a fetch job — a used-kart set, ideally a post-session paddock shot — and it should
be a ticket, not six `estimated` rows quietly carrying the whole claim.

---

## 60.4 Racing numbers and number panels — the regulation, verified

### 60.4.1 The text, verbatim

**Art. 3.7 *Racing numbers and number plates*, PDF pages 5–6** of
`refs/frontend/fia_karting_technical_regulations_2026.pdf`. Located by reading the
section boundaries — Art. 3.6 *Mass* precedes it and Art. 3.8 follows on p. 6 — not
recalled.

> Racing numbers must be black, in an Arial font on a yellow background.
> For short circuits, they must be at least 15 cm high and have a 2 cm thick stroke.
> For long circuits, they must be at least 20 cm high and have a 3 cm thick stroke.
> Racing numbers must be bordered by a yellow background of at least 1 cm.
> They must be fitted before scrutineering, on the front panel, rear wheel
> protection or rear number plate, and on both sides towards the rear of the
> bodywork.
> The driver is responsible for ensuring that the required numbers are clearly
> visible to Timekeepers and Officials.
> The number plates must be made of flexible opaque plastic and be visible at all
> times. They must be fixed without possibility of removal.
> In Group 4, the number plate fitted at the back of the kart must be flat and have
> rounded corners (diameter of rounded corners 15 to 25 mm) with 220 mm sides.
> It may be made of polyester. The racing number may be printed on the rear
> radiator.
> For FIA Karting Championships, Cups and Trophies, the driver's name as well as
> the flag of his nationality must be displayed at the front of the lateral
> bodywork.
> […] The flag and name letters must be at least 3 cm high.
> For FIA Karting Championships, Trophies and Cups, the CIK-FIA may require
> advertising on the front panel and front fairing. For all other competitions,
> only the organiser's advertising is permitted; in that case, the organiser must
> supply the stickers. This advertising must not be more than 5 cm high and may
> only be affixed to the upper or lower part of the number plate.

Two further sentences put the panel *locations* in the bodywork articles rather
than in Art. 3.7:

> A space for racing numbers must be provided on the front panel.
> — Art. **9.5.3** *Front panel*, PDF p. 24

> A space for racing numbers must be provided on the vertical surface close to the
> rear wheels.
> — Art. **9.5.4** *Side bodywork*, PDF p. 25

### 60.4.2 Did the "≥150 mm tall, 20 mm stroke, ≥10 mm yellow border, Art. 3.7" note survive?

**Mostly, and for the first time in this repo the recalled article number was
right.** Art. 3.7 is correct. But three things in that note need correcting and one
of them is a real trap:

1. **The article number holds.** 3.7 is where the rule is. Worth recording,
   because the front matter's §7.4/§7.2 case has made every article number in this
   project suspect on sight.
2. **150 / 20 / 10 are the *short-circuit* figures only.** The text carries a
   second set the note dropped entirely: **200 mm high and a 30 mm stroke for long
   circuits**. A spec that says "≥150 mm" without saying "short circuits" is an
   estimate wearing the vocabulary of a limit in the other direction — it reads as
   *the* rule and is half of it. Valdirone is a short circuit, so 150/20/10 is what
   this project builds to; the number panel geometry must still be able to carry
   200/30 or a long-circuit layout is a re-author rather than a re-skin.
3. **The border is not a separate color.** The note said "≥10 mm yellow border",
   which reads as a border *around* a differently-colored field. The text says the
   background *is* yellow and that the numbers *"must be bordered by a yellow
   background of at least 1 cm"* — one yellow field, with at least 10 mm of it
   clear around the glyph on every side. There is no second color.
4. **The font is specified and the note omitted it.** *"black, in an Arial
   font"*. That is a hard constraint on the texture, not a suggestion.
5. **The 220 mm rear plate is Group 4, not KZ.** KZ is **Group 1** (§00 §2b,
   ADR-0054); this line said Group 2, which is KZ2. The point is unaffected —
   neither group is Group 4 — but anyone lifting "220 mm sides" into this kart
   would be building a superkart's plate.

### 60.4.3 What those figures mean geometrically

`derived`, and the derivation matters because it constrains panels this section
does not own.

**150 mm cap height with a 20 mm stroke effectively mandates Arial Bold.** Arial
Regular's digit stem is about 0.093 em; at a 150 mm cap height (0.716 em) the em is
209.5 mm and the stem is ~19.5 mm — just barely 20. Arial Bold's stem is ~0.13 em
= 27 mm, comfortably over. Arial Regular is the marginal case, so **Bold is the
safe spec** and Regular must be checked rather than assumed.

    em         = 150 / 0.716              = 209.5 mm
    digit advance, Arial Bold, 0.556 em   = 116.5 mm

| digits | glyph block | **+10 mm border all round** |
| --- | --- | --- |
| 1 | 116.5 × 150 | **137 × 170** |
| 2 | 233 × 150 | **253 × 170** |
| 3 | 350 × 150 | **370 × 170** |

Three-digit numbers are normal in KZ2 — `exh_commons_buntschu_kz2.jpg` carries
**110**, and `refs/frontend/genk2026_kz2_entry_list.pdf` is full of them. So the
number zone the bodywork has to provide is **370 × 170 mm** for the general case.

**Three consequences, all of which are findings rather than restatements:**

- **A three-digit number does not fit on a front panel.** Art. 9.5.3 caps the panel
  at *"250.0 mm minimum and 300.0 mm maximum"* wide. 370 > 300. Two digits at
  253 mm fit a 300 mm panel with 47 mm to spare and **do not fit a 250 mm panel at
  all**. This is not a defect in the regulations; it is why a three-digit kart
  carries the full number on the pods and the rear and an abbreviated one on the
  front, and it is a constraint the §Bodywork agent needs in writing.
- **The 170 mm zone height is within 10 mm of every panel's full height.**
  `sidepod_height` is 180. The KG C2 rear protection is 177 tall (`sourced`, front
  matter §5b). Both leave 7–10 mm of margin. **The 150 mm regulation number height
  is what sets the height of kart bodywork, not styling** — that falls straight out
  of three independent numbers and is the most useful thing in this section for
  whoever sizes a panel.
- The zone must be **flat and vertical**. Art. 9.5.4 says *"on the vertical surface
  close to the rear wheels"*, and a 370 mm zone wrapped around a pod's rear
  curvature is not legible off-axis, which is what Art. 3.7's *"clearly visible to
  Timekeepers and Officials"* is for.

### 60.4.4 Number zones, per panel

Read off `birelart_kz_graphics.jpg` (1668 × 1608), the Birel ART Freeline decal-kit
**flat layout** — die-cut sticker shapes per panel with the yellow number fields
drawn in. This is vector artwork exported to JPEG, not a photograph, which is why
§60.5 can source a color off it.

| panel | zones | evidence |
| --- | --- | --- |
| **front panel** (`new`) | **three** yellow fields in the Birel ART kit: one central rectangle on the vertical face carrying the number, plus two angled fields on the upper wings. Central field ≥253 × 170 (two digits), centered laterally, top edge under Art. 9.5.3's steering-wheel plane. | `birelart_kz_graphics.jpg`, the `PN509` element |
| **side pods** ×2 (`built`) | one large field at the **rear-outboard end** of each pod, wrapping onto the vertical face — exactly where Art. 9.5.4 puts it. In the Birel ART `CL FL AERO` decals it is a trapezoid occupying the pod's full height for the rearmost ~35% of its length. ≥370 × 170. | `birelart_kz_graphics.jpg`; confirmed in use on the right pod of `exh_commons_buntschu_kz2.jpg`, which reads **110** |
| **rear wheel protection** (`new`) | one field on the **main** (central) part, ≥370 × 170. Art. 3.7 offers *"rear wheel protection or rear number plate"*; KZ has no mandated plate, so the number goes on the panel. It must sit on the main part, because the two outer parts are committed to the contrast color of §60.4.5. | Art. 3.7 + Art. 9.5.5.1 |
| **front fairing** (`built`) | **no number zone.** Art. 3.7 does not list it and the Birel ART fairing decal has no yellow anywhere. It is a sponsor and CIK-advertising surface only. | `birelart_kz_graphics.jpg`, the wide `PN509` element |
| **front of the lateral bodywork** | driver **name + national flag**, letters ≥30 mm, mandatory for FIA Championships/Cups/Trophies. So the pods carry a number zone aft and a name zone forward, and the sponsor block lives between them. | Art. 3.7, PDF p. 6 |
| **number plate, upper or lower strip** | organiser advertising, ≤50 mm high, and only there | Art. 3.7, PDF p. 6 |
| **fuel tank** (`new`) | a two-piece sponsor wrap, left and right. No number. | `birelart_kz_graphics.jpg`, the `SERB 9Lt` pair |
| **floor tray** | chassis-brand decal and pinstriping. No number. | `tonykart_racer401T_p01.jpg` |

### 60.4.5 The rear protection's two outer parts

Art. **9.5.5.1**, PDF p. 25, verbatim and complete — the front matter's quote of
this stops one sentence early and the missing sentence is the one that says how:

> The two adjustable outer parts of the homologated rear wheel protection must have
> a color that is clearly different from the main part of the rear wheel protection.
> This can be done by a dedicated sticker kit or by adding color to the parts during
> production.

So the contrast may be a **decal**, which makes it a UV zone rather than a
geometry-and-material problem.

| property | value | prov |
| --- | --- | --- |
| which parts | the two outboard adjustable sections, in the extension of the rear wheels | `sourced`, 9.5.5.1 |
| zone width per side | **300 mm**, full panel height | `estimated`. 9.5.5.1's ground-clearance rule requires *"at least three spaces of a 200.0 mm minimum width, located in the extension of the rear wheels and the centreline"*, so an outer part is at least 200 mm wide; 300 of a 1,360 mm panel leaves a 760 mm main section, which comfortably carries the 370 mm number zone. |
| contrast test | *"clearly different"* — no numeric threshold in the text. Spec it as **≥0.25 difference in linear luminance** from the main part, so it is checkable rather than judged. | `estimated`, and flagged as an invented threshold |
| what it must not be | the number-field yellow, in any palette. A third color adjacent to a regulation yellow field reads as a printing error. | `derived` |

---

## 60.5 Livery palettes

Geometry is not this section's. These are palettes: named relationships, with hex,
and every hex tagged.

### 60.5.1 The one color here that is genuinely sourced

**Number-field yellow: `#ecd44c`.** `sourced`.

Sampled at four independent number zones in `birelart_kz_graphics.jpg` — the front
panel's central field, its left wing field, and both `CL FL AERO` pod fields — and
all four returned **(236, 212, 76)** to the byte. It is a flat vector fill in a
manufacturer's own decal-kit artwork, so there is **no photographic white balance
to correct**, which is what separates this from every other color below.

Number glyph: **`#000000`**, `sourced` — Art. 3.7, *"must be black"*.

**This supersedes the provisional `#d7c354` for number fields.** `#d7c354` is 8%
darker and less saturated; the two adjacent will read as a faded reprint of one
another. Where palette C wants `#d7c354` as a circuit accent, keep it away from a
number field, or separate them with a white keyline **outside** Art. 3.7's 10 mm of
clear yellow — the article requires 10 mm of yellow around the glyph and says
nothing about what sits beyond it, so a keyline is legal.

Birel ART's own livery red measured the same way: **`#9e3b36`**, `sourced` as a
vector fill. Recorded because it is the only other exactly-known color in this
repo, not because any palette below uses it.

### 60.5.2 A — Heritage

Green tubes, gold magnesium rims, white wrap with green and red pinstripes.

| element | relationship | hex | prov |
| --- | --- | --- | --- |
| frame tube | deep racing green, gloss powder coat; **one stop darker and bluer than the pinstripe green**, so the tube reads as structure and the stripe as decoration | `#1d5c33` | `estimated`. `tonykart_racer401T_p01.jpg`'s tube samples at `#1d3e2b` but that pixel is in shadow on a 30 mm cylinder; the frame's dominant green mode is `#285032`. Studio white-sweep lighting, cool, so the true hue is warmer than either. |
| pinstripe green | printed vinyl, brighter and yellower than the tube | `#2d5839` | `estimated`, measured off p01's tray; printed vinyl on a flat surface, so closer to true than the tube |
| pinstripe red | a warm signal red, not a crimson | `#c33533` | `estimated`, measured off p01's tray |
| wrap white | a warm white, clearly warmer than the number field's neighbor | `#f2f0ec` | `estimated` |
| rims | gold-anodized magnesium: warmer and much darker than the number yellow, duller than brass | `#9a7b34` | `estimated`; `exh_commons_buntschu_kz2.jpg` confirms the finish exists on a KZ2 and is too small and too shadowed to sample |
| tray | §60.3.2's anti-slip white, with the green/red pinstripes | `#c8c8d2` | `estimated`, measured mode from p01 |
| accents | chrome bumper tubes, clear-anodized machined parts, red brake lines | — | — |

### 60.5.3 B — Factory

Black tubes, black rims, orange and black wrap.

| element | relationship | hex | prov |
| --- | --- | --- | --- |
| frame tube | gloss black powder coat, reading **blue-black against the tires' brown-black** — the two must not collapse into one value | `#14161a` | `estimated`; `crg_roadrebel_kz_detail7.webp` shows exactly this separation |
| rims | satin black anodized magnesium, **flatter and slightly lighter than the tube's gloss** | `#1c1c1e` at roughness 0.45 | `estimated` |
| wrap orange | a fluorescent orange, hotter than any process orange | `#f4491f` | `estimated`, **and the measurement failed, which is the honest part**: across `crg_roadrebel_kz_detail7.webp` **36.6%** of the orange pixels have the red channel clipped at ≥253, and in `detail11.webp` **54.1%**. A fluorescent ink under studio light saturates the sensor, so the hue is unrecoverable from these frames and the value is certainly wrong. Any orange sampled off a CRG photograph in this repo is a guess wearing a measurement's clothes. |
| wrap black | the wrap's black, glossier than the tube's and slightly warmer | `#17171a` at roughness 0.16 | `estimated` |
| accents | grey-silver graphic flashes (`crg_roadrebel_kz_detail11.webp` uses a mid grey as the third color, not white), gold-anodized hubs, red brake lines | `#9a9ea0` | `estimated` |

### 60.5.4 C — Valdirone

Deep blue tubes, silver rims, white wrap, accented on the circuit's provisional
panel yellow.

| element | relationship | hex | prov |
| --- | --- | --- | --- |
| frame tube | deep navy, gloss; dark enough to read as near-black at distance and clearly blue up close | `#14284b` | `estimated` |
| rims | bright silver — clear-anodized or polished magnesium, **cooler and brighter than the radiator core** and the only near-white metal on the kart besides it | `#b9bcc0` | `estimated` |
| wrap white | a cool white, a half-stop bluer than Heritage's | `#f4f5f7` | `estimated` |
| circuit accent | the documented provisional Valdirone panel yellow | `#d7c354` | `estimated` — carried forward from the front-end work, not measured here |
| accent, adjacency rule | **never adjacent to a number field.** `#d7c354` against `#ecd44c` is an 8% value step in the same hue and reads as a print defect. Separate with the wrap white or the navy. | — | `derived` from the two hexes |
| accents | navy-anodized fasteners, silver graphic flashes, red brake lines | — | — |

**Number zones stay `#ecd44c` with `#000000` Arial Bold glyphs in all three
palettes.** They are regulated, not styled, and they are the one part of the
livery that must be identical across every variant.

---

## 60.6 Things I believe are wrong in files I do not own

Numbers, not opinions. Nothing here was edited.

**1. `params.py`: the driver is 130 mm underground, and it is one bug in two fields.**

    driver_shoulder_z = 0.470     spec 608     138 mm low
    driver_eye_z      = 0.620     spec 757     137 mm low

The identical error in both is the tell. The *relative* geometry is exactly right:
params' eye−shoulder gap is 150 mm and the sourced gap is
`(787 − 622) × cos 25° = 149.5 mm`. And placing the hip joint at **z = 0** instead
of on the seat pan reproduces both fields to within 8 mm:

    527 x cos 25 = 477.7    vs driver_shoulder_z 470
    692 x cos 25 = 627.2    vs driver_eye_z      620

So both were measured **from the asphalt with the driver's hip on it**, rather than
from the seat pan 130 mm up. The correct values are 608 and 757. This is not
cosmetic — see item 3.

**2. `params.py`: `driver_upper_arm` and `driver_forearm` are a small adult's arm
with no hand on it.**

    driver_upper_arm  0.290    sourced 50th pct male 0.368    78 mm short
    driver_forearm    0.260    elbow-to-fist 0.361           101 mm short

`driver_forearm`'s docstring says the two are what issue #17's reach arithmetic
runs on. They are elbow-to-**wrist**, so the arithmetic they support stops 90 mm
before any grip. The reach required is 725.5 mm; 290 + 260 = 550 misses by 176 mm
and 368 + 361 = 729 closes it with the elbow locked at 180°. Both fields should
change, and even then §60.2.3 says the wheel has to move.

**3. `src/core/chassis.h`: three of the four driver mass lumps are ~125 mm too low,
which is a load-transfer term, not a cosmetic one.**

Converting each lump's Godot position to spec coordinates (`(x, Y, Z)_godot`
→ `(x, −Z, Y)_blender`) and comparing to §60.1.4's segment centroids:

| lump | chassis.h (y, z) | this spec (y, z) | Δy | Δz |
| --- | --- | --- | --- | --- |
| `driver trunk` 39.0 kg | (−248, 330) | (−281, 369) | 33 | **−39** |
| `driver arms` 7.8 kg | (+32, 420) | (−37, 544) | 69 | **−124** |
| `driver legs` 25.0 kg | (+122, 170) | (+125, 288) | 3 | **−118** |
| `driver head and helmet` 6.2 kg | (−128, 560) | (−454, 738) | **326** | **−178** |

The fore-aft agreement on legs (3 mm) and trunk (33 mm) says the longitudinal model
is sound; every z is low by the same 120–180 mm as item 1's driver, from the same
cause. 78 kg of a ~170 kg kart sitting 125 mm lower than it should understates the
CoM height, and CoM height is the numerator of the load-transfer term that every
§6.4 figure and the 2.43 g rollover threshold depend on.

`chassis.h` carries an explicit instruction not to correct these against
`params.py`'s seat until **issue #107** closes, and I have not. But #107 is about
the longitudinal calibration, and this is a vertical error of a different sign and a
different cause. It should be its own ticket rather than sheltering under #107's.

**4. `src/core/chassis.h`: the head lump is 326 mm too far forward, and it is where
the cockpit camera and the audio listener both sit.**

`KartBody::driver_head_position()` returns the *"driver head and helmet"* lump
center, and `cockpit_camera.gd` and `engine_voice_rig.gd` both use it. That lump is
at spec `(0, −128, 560)`; the helmet center derived from the sourced seat and the
sourced anthropometry is `(0, −454, 738)`. So the cockpit camera sits **326 mm
forward and 178 mm below** the driver's head — roughly over his sternum, at
chest-top height, looking out from inside the driver.

`cockpit_camera.gd`'s own header argues that an eye offset would be *"a number with
no source"* and that `chassis.h` has *"a 260 mm helmet box and nothing inside it"*.
That was the right instinct and this section removes its premise: the eye point is
now `(±32, −462, 757)`, sourced, with its arithmetic shown. The camera has an eye
point to use.

**5. `params.py`: the floor tray stops 190 mm behind the driver's heels.**

    tray_front_y = 0.180   tray_length = 0.760   ->  tray spans y -580 .. +180
    heel contact        y = +479         299 mm ahead of the tray's front edge
    foot bar            y = +585         405 mm ahead of the tray's front edge

(Written at heel y +370 against the old pedal face at +560; #202's live solve
moved both and the gap *grew*. The finding is the same, only worse.)

There is no floor under the driver's feet, which is why §60.3.2's heel-scuff wear
cannot be placed on this kart and why the heel's `z = 69` is a hypothetical. A real
kart's tray runs forward to the front cross member; `tray_front_y` wants to be
around 0.500 with `tray_length` around 1.08. The tray's z is fine —
`ground_clearance 0.035 + tube_main 0.030 + tray_thickness 0.004` puts the top at
**0.069**, which is what `radiator_z`'s docstring asserts.

**6. `params.py`: `driver_helmet_radius = 0.125` is a sphere where a helmet is an
ellipsoid.** 250 mm is right for a helmet's *width*; a full-face karting helmet is
about **340 mm** front-to-back. A spherical helmet is 90 mm short in length, and it
is short in the direction the cockpit camera looks. `chassis.h`'s 0.26 m cube has
the same problem for the same reason.

**7. `notes_radiator.md` §6: the estimated 40–45° torso recline cannot be built.**
That row is `estimated` and marked as such, so this is a correction and not a
defect. At 43° from vertical, a hip at (−170, 130) puts the shoulder joint at
`y = −170 − 527 × sin 43° = −529` — **4 mm behind the rear axle centerline**, with
the acromion 164 mm behind the seat back's own rearmost point. The seat's total
horizontal run, front lip to back top, is 315 mm; a 43° torso needs 359 mm of run
for the torso alone. It does not fit. The shell chord's sourced 19–26° is the
number that closes, which is what §60.1.3 uses. It is worth noting that the 43°
figure and `driver_shoulder_z = 470` are *nearly* consistent with each other, which
is probably how both survived: 43° recline off a ground-level hip gives 473.

**8. `build.py`: `MATERIALS` has 15 entries for at least 22 distinct finishes, and
one entry covers 75 of 146 parts.** The splits are §60.3's; the headline is that
`engine_alloy` holds a raw sand casting, machined billet, gold anodize and a brazed
radiator core simultaneously, and `seat_fiberglass` at `(0.055, 0.055, 0.058)` is
near-black where a bare fiberglass kart seat is one of the brightest objects on the
kart. The comments in that block are careful and well reasoned about *value
separation between neighbors*, which is why several of them are wrong: they solved a
composition problem by darkening a material, when the fix was a second material.

**9. `refs/kart-visual/sources.txt`:** `notes_radiator.md` §8 already flags the
`tonykart_racer401T_p05.jpg` caption for claiming a seat and a top-down view where
there is neither. Still unfixed at this commit. Not my file and not that agent's
either, so recording it a second time rather than a third.

---

## 60.7 Provenance count for this section

| tag | numbers | notes |
| --- | --- | --- |
| `sourced` | **31** | 12 regulation quotes with verified article numbers and pages (Art. 3.6, 3.7, 3.11, 4.8, 4.8.1, 7 preamble, 7.1–7.5, 9.3, 9.5.3, 9.5.4, 9.5.5.1); the NASA 1978 anthropometric table's 10 dimensions; `#ecd44c` and `#9e3b36` from vector artwork; the Tillett chart via `notes_radiator.md`. |
| `derived` | **34** | every hard point in §60.1.4, the whole reach and pedal arithmetic, the number-zone block sizes, the Arial Bold stroke check, the seat front lip at y −50, and the six defect figures in §60.6. |
| `estimated` | **29** | the three placement estimates in §60.1.3, the knee splay, the equipment thicknesses, most roughness values, all wear placement, and every palette hex except `#ecd44c`. |
| `snippet` | **0** | nothing here rests on a page that could not be opened. |

`estimated` is a third of this section, which is what the front matter says a
normal outcome looks like. The two that should worry a reader are named where they
sit: the **knee splay of ±180**, because the whole steering-wheel fix depends on it
and it is read off one action photograph; and the **six wear claims with no
photograph at all** in §60.3.13, because wear is the thing this section exists to
specify.
