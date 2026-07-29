# 99. Recheck — every citation re-read against the primary source

This pass did not write the document it is checking. Nothing here was taken from
`docs/REFERENCES.md`, `ARCHITECTURE.md`, `DECISIONS.md`, `ROADMAP.md`,
`CLAUDE.md`, `refs/kart-visual/notes_*.md` or any `sources*.txt` caption, and no
source comment in `tools/` or `src/` was treated as establishing anything. Those
files were read only to understand what a row was claiming.

Primary sources actually opened, in full or at the cited page:

    refs/frontend/fia_karting_technical_regulations_2026.pdf   all 60 pages,
        re-extracted per-page with pdftotext -layout so every page attribution
        could be checked as well as every quote
    refs/kart-visual/cik_hf_chassis_crg_road_rebel_04-CH-14.pdf      section B
    refs/kart-visual/cik_hf_chassis_gillard_tg16_026-CH-99.pdf       section B
    refs/kart-visual/cik_hf_frontfairing_otk_m4_100-CA-20.pdf        p2 rendered
        at 200 dpi -- the dimensions are a raster and extract as nothing
    refs/kart-visual/cik_hf_frontfairing_kg505_2-CA-20.pdf           p2
    refs/kart-visual/cik_hf_rearprotection_kg_c2_003-BR-48.pdf       p2, p3
    refs/kart-visual/run_vega_xh3-ant.pdf / -post.pdf                p2, p3
    refs/kart-visual/run_cik_homologation_tyres_2024-2026.pdf        Vega block
    refs/kart-visual/run_crg_ven05_brake.pdf                         p3
    refs/kart-visual/run_birelart_freeline_007-B4-69.pdf             p3
    refs/kart-visual/exh_tm_kzr1_homologation.pdf                    p9 rendered
        at 220 dpi -- all 15 cones, both diameters, both slant lengths
    refs/kart-visual/exh_tm_kzr2_homologation.pdf                    mass, volume
    refs/kart-visual/tonykart_racer401T_product.png                  re-measured

## 99.1 Verdict

"Rows checked" means a tagged figure or an article number that was put against a
primary source. It is not the whole document: the pass prioritized citations,
group applicability, form-backed rows and cross-section pairs, per the brief.

| section | rows checked | confirmed | corrected | downgraded | unverifiable |
| --- | --- | --- | --- | --- | --- |
| 00 front matter | 51 | 41 | 6 | 2 | 2 |
| 10 chassis | 44 | 40 | 2 | 0 | 2 |
| 20 running gear | 62 | 58 | 0 | 4 | 0 |
| 30 powertrain | 47 | 45 | 1 | 0 | 1 |
| 40 cockpit | 26 | 23 | 1 | 1 | 1 |
| 50 bodywork | 38 | 35 | 2 | 0 | 1 |
| 60 driver and finishes | 14 | 14 | 0 | 0 | 0 |
| **total** | **282** | **256** | **12** | **7** | **7** |

What that hides, and it matters more than the ratio: **the four heaviest
form-backed blocks in the document are exact.** Every one of these was read off
the primary and matched digit for digit.

- **The 15-cone exhaust table, §30.6.2.** All 30 diameters and all 30 slant
  lengths on `041-EZ-75` p. 9. Every centreline station in the spec is the mean
  of the form's own `L min` / `L max` pair for that cone, to 0.1 mm, all fifteen
  times. Developed 674.6, header ØA 44.5, belly ØN 136.5, baffle ØP 114.5,
  stinger ØR 26.3, sensor boss at the dimensioned "25". Nothing to correct.
- **Both chassis homologation forms, §10.1.** CRG `04/CH/14`: A 1050 ±10,
  B 32 ±0.5 ×6, C 9, D 6, E 735 ±10, F 650 ±10, G1 210 ±15, G2 250 ±10.
  Gillard `026-CH-99`: 1046, 30 ×6, 11, 6, 730, 640, 210, 275. The spec's table
  reproduces both exactly, including which figure carries which tolerance.
- **Both Vega tire forms, §00 §3 and §20.2.** `047-TO-12` p. 3: Ø260, tread 110,
  overall 130, rim 120. `047-TO-14` p. 3: Ø274, 179, 207, 198, bead Ø126.2
  +0/−1. Plus tread depth 3.3/3.5, thickness 3.5/3.8, mass 1200/1600 g ±10%,
  service pressure 0.85 ±0.3 bar. Every one confirmed, and the forms are indeed
  "Groups 1 & 2" as claimed. Corroborated against the 2024–2026 technical list,
  which also confirms there is no Bridgestone entry of any kind.
- **The CRG VEN brake form, §20.6.** `82/FR/11`, "All category": 1+1 master
  cylinders at 22 mm, front 4 pistons at 26 mm per wheel, rear 2 at 32 mm, 2 pads
  each, aluminium, ventilated both ends, front 12 ±1 / Ø150 ±1.5 / 149 / 92 /
  pads 38 ±1.5, rear 18.5 ±1 / Ø195 ±1.5 / 194 / 136 / pads 58 ±1.5. Exact.
  Birel `007-B4-69` cross-check: 22 mm bores, 2 front / 4 rear pistons at 25 mm,
  Ø150 / Ø180. Exact. **22 mm is confirmed identical across a 2005 form and a
  2018 one**, which is what §20.6.1 claims.
- **The OTK M4 fairing, §00 §5b and §50.8.** 1090 × 287 × 227, `acciaio Ø20x1.5`
  and `acciaio Ø16x1.5`, tube pairs dimensioned **450** and **550**. All six
  figures confirmed — and see correction C2, because this form is what proves the
  document's own withdrawal wrong.
- **Appendix 9, §50.8.** "A 200 x 450 mm plate […] flat, rigid and 10 mm thick.
  One loading configuration […] on the centreline […] tested a total of five
  times […] The average peak load must exceed 75% of the value defined in the
  respective HF, within a displacement of 30 mm at a speed of 100 mm/min."
  PDF p. 58. The spec's one-line summary is accurate including the page.
- **Art. 7, §60.** Driver safety equipment is Art. 7 on PDF pp. 19–20 with 7.1
  through 7.5, the *"scarf, muff, or any loose clothes around the neck"* line in
  the preamble, and the *"For helmets with 8858-2010 Helmet M6 anchorages (HANS
  attachment points), the M6 anchorages cannot be used in karting"* line in 7.1.
  §60 was right to overrule its own brief's "Art. 3". Art. 3.7 (150 mm digits,
  20 mm stroke, 10 mm border, and the 220 mm rear plate being **Group 4 only**),
  Art. 3.11 transponder on the back of the seat: all confirmed.

Also confirmed and worth naming because they are the rows a later pass is most
likely to "fix" wrongly:

- Art. 4.13.1 Groups 1 & 2, 5-inch: max OD **280 front / 300 rear**, max width
  **135 / 215**, footnote *"maximum wheel dimensions, with a matching tyre fitted
  on the rim and an air pressure of 0.5 bar."* PDF p. 13. So §00 §3 is right that
  295 is neither the maximum nor a real tire.
- Art. 4.14: bead coupling **126.2 +0/−1**, flange external **136.2 minimum**,
  housing ≥10.0, balance radius 8. The bead-versus-flange conflation §00 §3 and
  §20.2.2 describe is real and the tolerance directions are as stated.
- Art. 4.15 is one sentence with no dimension. Confirmed.
- Art. 4.3's wall table: 50.0 → **1.9**, and *">28.0 → full"* as the last row.
  PDF p. 9. The four-keyway sentence and the chamfer sentence are verbatim.
- Art. 4.12.4's trigger, 4.12.2's 1.8 mm cable, 4.12.3's grinding/drilling/
  grooving clause, 4.12.5's "attached to the stub axle", 4.17's one sentence:
  all verbatim, all on the stated pages.
- Art. 8.9 *"KZ: 170.0 kg minimum"*, PDF p. 21. §20.9's mass arithmetic cites it
  correctly.
- Art. 5.9's *"In gearbox classes, the chain guard must cover the sprocket and
  the crown wheel down to the centre of the crown wheel axis"* and Art. 5.10's
  four clauses including *"not operate at a height of more than 45 cm from the
  ground"*: verbatim, PDF p. 17. §30's 450 ceiling and 131.6 mm of margin follow.
- Art. 9.10.1 in full, 9.12.1's Ø30 carburettor, 9.13.1's *"two ducts with a
  30.0 mm maximum diameter"*, 9.15.1, 9.16.1's TD n°2.7, 9.17's one-circuit
  sentence and its **OK-only** single-radiator limit, 9.18.1's *"The chain and
  sprockets are free"* and 9.18.2 making 219 compulsory in OK only: every one
  confirmed on the page cited. §30.0's two "this changes what the repo believed"
  claims are both correct.
- Art. 4.2.3's *"seat with four seat supports"*, 4.2.4, 4.2.5's list including
  *"engine bracket"*, *"radiator(s), holder"* and *"exhaust and exhaust silencer
  holder"*, 4.2.6's *"Flexible connections are permitted"*, 4.8.2's *"All seat
  stays must be bolted at each end. If they are not used, these seat stays must
  be removed"*: verbatim, PDF pp. 8 and 11. §30.7's and §40.3's argument for
  anchoring the radiator on the frame rather than on a stay rests on two articles
  that both say what they are quoted as saying.
- Art. 4.1.5's IDR clause, 4.1.2's ISO 4948/4949 chassis-material clause,
  4.5/4.5.1/4.5.2/4.5.4, 4.7's mandated tank position, 4.8/4.8.1, 4.10.1's
  six-item bodywork list, 4.10.2's 5 mm corner radius, 4.11, 5.2.1's M14 × 1.25
  spark-plug housing, 5.5, 5.6.1: all confirmed verbatim on the stated pages.
- §00 §4's catch that **Art. 9.5.4.1 does not exist** is correct: Art. 8.5.4.1
  (p. 21) says *"See Article 9.5.4.1"* and Art. 9's set is 9.5.4 and 9.5.4.2.
- §00 §5's photogrammetry **reproduces.** Re-measured independently: the rear
  rim face centres at x ≈ 120 px and the front at x ≈ 515 px on
  `tonykart_racer401T_product.png`, so the wheelbase is **395 px** against the
  stated 397 and the front-axle station is the stated 514.5. (705 − 514.5) ×
  1050/397 = 504 mm stands. My first attempt read 421 px because a gold-anodized
  brake and steering cluster sits just aft of the front wheel and contaminates a
  colour mask; the hub faces have to be found visually. Recorded so the next pass
  does not repeat it. See downgrade D3 for the tolerance the row is missing.

## 99.2 Corrections

Ranked. C1 and C2 change which article governs and which figure is real; C3–C9
are attribution and quotation defects that do not move a built number.

### C1 — KZ is Group 1, not Group 2. Three sections say otherwise and one of them "corrects" a correct citation into a wrong one.

**Art. 1 *Classification*, PDF p. 1, verbatim:**

> Group 1
> KZ              Cylinder capacity of 125 cm3
>
> Group 2
> KZ2               Cylinder capacity of 125 cm3
> OK                Cylinder capacity of 125 cm3
> OK-N              Cylinder capacity of 125 cm3
> OK-Junior         Cylinder capacity of 125 cm3
> OK-N Junior       Cylinder capacity of 125 cm3
> E-Karting Junior …
> E-Karting Senior …

**Group 1 contains exactly one class and it is KZ.** Group 2 does not contain KZ.

What the document says:

| where | claim | verdict |
| --- | --- | --- |
| §00 §5a heading | *"KZ is **Group 2**."* | false |
| §40 preamble | *"**KZ/KZ2 is Group 2** (PDF p. 1, "Article 9 Group 2 Regulations — KZ … KZ2"), which decides which of the duplicated Group 1 / Group 2 articles applies: fuel tank capacity is **Art. 9.3**, not Art. 8.3."* | false, and the quoted words are not on p. 1 — Art. 9's heading is *"ARTICLE 9: GROUP 2 REGULATIONS"* with no class list, and the class list on p. 1 says the opposite |
| §40.11 item 8 | *"`notes_controls` cites Art. 8.3 for the fuel tank minimum. 8.3 is the **Group 1** article (PDF p. 20); KZ/KZ2 is Group 2 (PDF p. 1) and the article is **9.3**."* | **inverted. `notes_controls` was right.** For a KZ the article is 8.3 |
| §20.1 | *"Art. 8.6 Brakes (Group 1 = KZ)"* / *"Art. 9.6 Brakes (Group 2 = KZ2)"* | **correct**, and it is the only section that is |

The Group 1 article set, PDF pp. 20–22, read in full:

    8.1     Chassis            Group 1 chassis may only be produced by a
                               manufacturer who has a homologated chassis in
                               Group 2
    8.1.1   Chassis dimensions See Article 9.1.1
    8.1.2   Chassis requirements   own text -- see below, it is NOT 9.1.2
    8.2     Rear axle          own text, word-for-word identical to 9.2
    8.3     Fuel tank capacity 8 litres minimum
    8.4     Bumpers            own text, identical to 9.4
    8.4.1   Short-circuit front bumper       See Article 9.4.1
    8.4.2.1 Short-circuit side bumper        See Article 9.4.2.1   <- see C5
    8.4.2.2 ... paired with wheel covers     See Article 9.4.2.2
    8.5     Bodywork           own text, identical to 9.5
    8.5.1   Material           See Article 4.10.2
    8.5.2   Front fairing      See Article 9.5.2
    8.5.3   Front panel        See Article 9.5.3
    8.5.4.1 Side bodywork      See Article 9.5.4.1   <- does not exist, §00 §4
                                                        already caught this one
    8.5.5.2 Rear wheel protection with wheel covers   See Article 9.5.5.2
            -- and there is NO 8.5.5.1
    8.6     Brakes             free in Group 1, subject to 4.12 et seq.
    8.7     Wheels             5-inch rims, homologated 5-inch tyres
    8.8     Data logging       free
    8.9     Mass of kart       KZ: 170.0 kg minimum
    8.10-17 engine, carburettor, intake silencer, ignition, exhaust, silencer,
            radiators, gearing -- all "See Article 9.1x"

**Most of the damage is nil, and that is worth stating plainly so this does not
become a rewrite.** 8.1.1 defers to 9.1.1, so every figure in §00 §3 is reachable
from the Group 1 article. 8.2 and 9.2 are identical text; 8.3 and 9.3 are both
*"8 litres minimum"*. 8.4/9.4, 8.5/9.5 identical. 8.5.2/8.5.3 defer to 9.5.2 and
9.5.3. Art. 5.3.1's radiator paragraph is headed *"In Groups 1 & 2"* and Art.
4.13.1's wheel table likewise, so §00 §5a's quote and §00 §3's ceilings are the
right paragraphs whatever the group label says. **No dimension in the document
changes because of C1.** Three things do:

1. **Art. 8.1.2 ≠ Art. 9.1.2, and the difference is load-bearing.** 8.1.2 reads
   in full: *"An anti-roll bar can be used. Extra seat stays are allowed between
   the rear axle brackets and the seat."* It carries **no** *"Anti-roll bars must
   only be connected to the main tubes of the chassis frame"* clause. §10.10
   gives that clause as the **Envelope** for `chassis_torsion_bar_front`/`_rear`.
   For a Group 1 KZ the constraint does not exist and the correct Envelope is
   *"Art. 8.1.2 — an anti-roll bar can be used"* plus Art. 4.2.5/4.2.6. §10.9's
   seat-stay citation survives intact, because 8.1.2 carries that sentence too.
2. **Group 1 has no 8.5.5.1.** Its bodywork list jumps from 8.5.4.1 to 8.5.5.2,
   i.e. rear wheel protection **with wheel covers**, → Art. 9.5.5.2. §50.11 and
   §00 §4 build the rear protection to **9.5.5.1** — 1.340 m minimum width, three
   200 mm clearance windows, 15–50 mm tire gap. 9.5.5.2 is a different part:
   three pieces, M8 bolts ≥75 mm apart, ≥20 mm radial clearance to the wheel,
   covers extending 20–30 mm beyond the outer plane of the rear wheels. See X9;
   this is not resolvable from the pinned PDF alone.
3. **§40's "correction" of `notes_controls` must be reverted**, and the reversion
   is the more useful output than the citation: it is the §7.2/§7.4 failure
   running in the opposite direction — a correct citation replaced by a wrong one
   *with the incident named in the same paragraph as justification*.

**The document also never decides whether the subject kart is KZ or KZ2**, and it
must. `CLAUDE.md` calls the project a KZ sim; `refs/frontend/`'s results booklets
are KZ2. If the answer is KZ2 then Group 2 is right, §00 §5a and §40 are right,
and §20.1's Group-1 framing for Art. 8.6 is the mislabel instead. **Either answer
is fine; asserting both is not.** One sentence in §00 §2 or §3 settles it, and it
should name the class, the group, and the article prefix that follows from it.

Corrected value: **state the class. If KZ, the prefix is Art. 8.x and only
Art. 8.1.2 and the 8.5.5.x set change any content.**

### C2 — Art. 9.4.1 does say 550.0 mm. The "Correction" in §00 §4 is itself wrong, it withdrew a correct cross-check, and it has propagated into §50.

**Art. 9.4.1 *Front bumper*, PDF p. 22**, the paragraph §00 §4's quote block
omits entirely:

> The front bumper consists of two elements: an upper bar with a minimum diameter
> of 16.0 mm and two corner bends with one constant radius. The straight length
> between the bends must be 375.0 mm minimum and 395.0 mm maximum.
>
> The bar must be fixed to two welded chassis frame attachments, which must be
> **550.0 mm apart** and centred on the kart's longitudinal axis.

and then, **PDF p. 23**, for the *lower* bar:

> The bar must be fixed to two welded chassis frame attachments, which must be
> **450.0 mm apart** and centred on the kart's longitudinal axis. The attachments
> must be horizontally and vertically parallel to the kart's axis and allow for a
> 50.0 mm insertion of the bar.

**Both spacings are in Art. 9.4.1: 550 for the upper bar, 450 for the lower.**

§00 §4's correction note states: *"The attachment spacing was quoted as 550.0 mm.
The text says 450.0 mm for the front bumper and 500.0 ± 5 mm for the side
bumpers. **There is no 550 anywhere in Art. 9.4.** §5b separately claims the OTK
M4 form's 550 mm tube spacing 'matches Art. 9.4.1' — it does not; the form's 450
is the one that matches, and the 550 is an unregulated second pair."*

Every sentence after the first is wrong. §5b's original claim was correct.

The OTK M4 form makes it exact, not merely reconcilable. `100/CA/20` p. 2,
rendered at 200 dpi (the drawing is a raster and `pdftotext` returns none of it):

    two vertical tubes labelled  acciaio (acier) Ø20x1.5   dimensioned  450
    two outer tubes labelled     acciaio (acier) Ø16x1.5   dimensioned  550

Art. 9.4.1 sets the **lower** bar at ≥Ø20.0 with attachments 450 apart and the
**upper** bar at ≥Ø16.0 with attachments 550 apart. The form's Ø20 pair is at
450 and its Ø16 pair is at 550. **The cross-check holds on both bars and on both
diameters simultaneously**, which is a far stronger result than §5b claimed and
the exact opposite of what §00 §4 withdrew.

Propagation, both to be reverted:

- §50.8, `bodywork_fairing_strut_l`/`_r`, spacing row: *"An earlier version of
  this row claimed it cross-checks Art. 9.4.1's '550.0 mm apart'; the article
  says 450.0 mm for the front bumper and 500.0 ± 5 mm for the side bumpers, and
  there is no 550 anywhere in Art. 9.4. The form's other pair, at 450, is the
  regulated one; this 550 pair is unregulated."* Revert. The Ø16 struts at 550
  are the upper bar's regulated pair.
- §50 preamble's **WITHDRAWN** block and §50.3's *"Demand 1 … superseded"* rest
  partly on the same paragraph. The **160 mm** half of that withdrawal is correct
  and stands — see below — so only the 550 sentence comes out.

**What was right in §00 §4's correction, and must not be un-corrected with it:**
the 160 mm figure. Art. 9.4.2 *Side bumpers*, PDF p. 23: *"Height of the upper
bar: 160.0 mm minimum from the ground (measured to the tube top)."* It is a
**side**-bumper rule. The front bumper's own bands are 200–250 (upper) and 70–110
(lower), both to the tube top, both PDF p. 23. §10.8's proof of the section
boundary is correct and its conclusion — a front bar with its tube top at 160 is
50 mm above the lower ceiling and 40 mm below the upper floor — is arithmetic.
§10.8's `nose_lower_z` 85 (top 95), `nose_upper_z` 217 (top 225),
`nose_lower_straight` 305, `nose_upper_straight` 385, `nose_lower_mounts` 450 and
`nose_upper_mounts` **550** are all correct against the article. §10 is the
section that got Art. 9.4.1 right and the front matter is the one that did not.

Corrected value: **`nose_upper_mounts` = 550 is `sourced` from Art. 9.4.1 (PDF
p. 22) as well as from the OTK M4 form, and §00 §4's third correction paragraph
is deleted.**

### C3 — §00 §4's Art. 9.4.1 quote is attributed to p. 23 and is truncated in a way that changes it.

The block is headed *"Art. 9.4.1 Front bumper (PDF p. 23)"*. The article begins
on **p. 22**; the upper bar's diameter floor, its 375–395 straight and its 550
attachment spacing are all on p. 22, and only the two height bands, the lower
bar and *"Front overhang: 350.0 mm minimum"* are on p. 23. §10.8 attributes it
correctly as *"PDF pp. 22–23"*.

The truncation is the part that did damage: the quote opens at *"Height: 200.0 mm
minimum and 250.0 mm maximum"* with no antecedent, hand-labels it `← upper bar`,
and then presents *"450.0 mm apart"* as though it were the article's only
attachment spacing. That is what made the 550 look invented. Same failure class
as the fairing's maximum width losing *"of the front wheel/front axle unit"*,
which §50.1 caught.

Corrected value: **p. 22–23, and quote the upper bar's own sentence.**

### C4 — §10.8 attaches Art. 9.5.2's 60.1 mm to the wrong gap.

§10.8: *"Vertical separation between the two bars at the front is 217 − 85 = 132
mm, which clears Art. 9.5.2's 'distance of 60.1 mm minimum between the 2 support
tubes of the clamps' (PDF p. 24) by 72 mm."*

Art. 9.5.2, PDF p. 24, in full: *"The front fairing assembly must comply with TD
No. 2.2, especially the distance of 60.1 mm minimum between the 2 support tubes
of the clamps as well as the 1 mm spacing between the hook clamps and the front
fairing mounting kits."* The two support tubes are the **mounting kit's** release
tubes, which is what §50.8 correctly builds as
`bodywork_fairing_kit_tube_fwd`/`_aft` at 65.0 mm fore-aft with a 1.0 mm hook
standoff. Art. 9.4.1 sets **no** separation between the two bumper bars; it
requires only that they be *"vertically aligned"* and *"connected by the front
bumper support"*.

The 132 mm is fine and so is §10.8's conclusion that the two bars are a fairing
mount. The citation attached to it is not, and it is the second-strongest
"article number recalled rather than located" in the document after C2.

Corrected value: **drop the 9.5.2 clearance claim from §10.8; the 60.1 mm lives
in §50.8 and is already there.**

### C5 — a second dangling FIA cross-reference, one article earlier, not recorded.

§00 §4 records that Art. 8.5.4.1 points at a nonexistent Art. 9.5.4.1. The same
defect sits in Art. 8.4: **Art. 8.4.2.1 *Short-circuit side bumper*, PDF p. 21,
says *"See Article 9.4.2.1"*** and Art. 9's set is **9.4.2** and **9.4.2.2** —
there is no 9.4.2.1. Both of Group 1's `.1` cross-references dangle and both
resolve the same way: to the unsuffixed article.

Corrected value: **the citation for a side bumper is Art. 9.4.2. Add it to §00
§4's existing note, which then covers a pattern rather than one incident.**

### C6 — §30.6.2's exhaust mass is the R2's, presented as both HFs'.

§30.6.2: *"mass | 1130 g minimum | `sourced` | both HFs"*.

`041-EZ-75` (KZ-R1) p. 9: *"Weight in gr **1132g** Minimum"*, volume 4022 cm³
±5%. `041-EZ-02` (KZ-R2): *"Weight in gramm 1130 gr. minimum"*, volume 3958 cm³
±5%. The cone table is transcribed from R1, so the row that accompanies it should
carry R1's figure.

Corrected value: **1132 g (R1) / 1130 g (R2).** The internal-volume check in
§30.6.2 — *"reproduces the HF's own stated internal volume at 1.07 mm (R1) and
1.29 mm (R2)"* — is against the right two volumes, 4022 and 3958.

### C7 — §00 §5a's Art. 5.3.2 quote substitutes a word and the substitution carries the argument.

§00 §5a: *"Art. 5.3.2 Water pump, same page: 'In Groups 1 & 2, the water pump
must be mechanically **[driven]**' — which is what makes the axle-driven toothed
belt the correct choice rather than a styling one."*

Art. 5.3.2, PDF p. 15, complete: *"In Groups 1 & 2, the water pump must be
mechanically **controlled either by the engine or by the rear wheel axle**."*

The bracketed substitution plus the truncation is what makes the sentence look
like it forces the axle drive. It does not — it permits either. §30.7's copy of
the quote is complete and its own argument for the axle is built on the parts the
trade sells (*"KZ water pump with HTD axle pulley and tooth belt"*) rather than on
the article, which is the correct construction. Only §5a's gloss is wrong.

Corrected value: **quote the clause in full; the axle drive is `sourced` from the
part, `permitted` by 5.3.2, not required by it.**

### C8 — §00 §4's page attribution for the pod datum.

*"All from the pinned PDF, Art. 9.5: 9.5.2/9.5.3/9.5.4 on PDF page 24, 9.5.5.1 on
PDF page 25."*

Art. 9.5.4's heading and its first three sentences are on p. 24, but the clauses
§4 and §50 actually depend on are all on p. **25**: the *"between 0mm and 40.0 mm
(inwards) from the plane defined by the outer front edge of the front wheel and
the outer front edge of the rear wheel"* datum, the 25–60 mm ground clearance,
both wheel gaps, the wet-weather plane and *"must be securely attached to the
side bumpers"*. §50.2's compliance table splits its 9.5.4 rows across 24 and 25
and is right.

Corrected value: **9.5.4 spans PDF pp. 24–25; the datum sentence is on p. 25.**

### C9 — §50's Art. 4.11 page range.

Given as *"PDF pp. 11–12"*. All of Art. 4.11 *Rear wheel protection* is on p. 11.
Page 12 carries **Art. 4.11.2**, *Rear wheel protection with wheel covers*, a
different article with different numbers (three parts, M8 bolts, wheel covers).
Citing 11–12 for 4.11 invites a later reader to pull 4.11.2's figures under
4.11's citation — and given C1 item 2, that is a live risk here specifically.

Corrected value: **Art. 4.11, PDF p. 11.**

## 99.3 Downgrades

Rows that could not be tied to a primary source in the sense §00 §1 defines, and
should therefore be tagged `estimated`. **This is expected output.** Each says
what would settle it.

### D1 — `track_rear` = 1400, §00 §3, tagged `sourced`

Basis given: *"equals the 1400.0 mm track-width maximum."* Art. 9.1.1 sources the
**ceiling**. It does not source the **choice** to sit on it, and the document's
own §20.3.4 quotes a `sourced` CRG setup guide giving rear track as 54"–55" =
**1371.6–1397 mm** — real karts run under the cap, and 1400 is not a setting the
guide offers.

This is the `length_overall = 1830 mm max` failure one step milder and in the
same table that diagnoses it: a chosen value wearing a regulation's authority.
By §00 §1's own rule the value is `estimated` (a choice) resting on a `sourced`
limit, exactly as `tire_front_diameter` 280 is relabelled two rows below.

What would settle it: nothing can make 1400 `sourced` as a *track*; relabel.
It stays frozen — `chassis.h` and the tire model are measured against it.

### D2 — `axle_diameter` = 50.0, §20.5, tagged `sourced`

Basis: *"Art. 9.2 'Maximum 50.0 mm outside diameter'; KZ runs the cap."* Same
shape as D1. The article is sourced; *"KZ runs the cap"* is an unsourced practice
claim doing the work. Note §20.5 handles the *wall* correctly — 2.5 mm is tagged
`estimated` against Art. 4.3's `sourced` 1.9 mm floor with its reasoning — so the
defect is only in the OD row.

What would settle it: any axle catalogue listing an outside diameter, or a
chassis HF section B that dimensions the axle. Otherwise relabel `estimated`,
reasoning "at the Art. 9.2 ceiling; 50 mm is the common KZ size".

### D3 — the 504 mm front overhang (§00 §5) and `column_rake` 36° (§40.2)

The arithmetic **reproduces** — see §99.1. Two things are missing from the rows.

1. **No tolerance.** The same image's rear tire measures 116 px, which at
   1050/395 = 2.658 mm/px is **308 mm** — above Art. 4.13.1's 300 mm maximum. So
   the image carries at least 3% of scale or perspective error in the vertical.
   §00 §5 records a related symptom (*"scaling that photo by tire diameter …
   implies a 1,195 mm wheelbase … the image has real perspective error"*) but the
   504 is then carried as if exact and the 1920 is stated to the millimetre.
   **±15 mm** is the honest band on the 504, and therefore on the 1920.
2. **The kart in the photograph is not a KZ.** It has no gear lever, no clutch
   lever and no rear wheel protection — visually confirmed at 3× on the crops.
   §00 §5 records the missing rear protection and correctly refuses to measure
   rear bodywork off it; it does not record that the whole kart is direct-drive
   trim, and **both** the front overhang and the steering column's rake are taken
   from it. The fairing and its mounting kit are the same homologated family
   across classes, so this is a caveat rather than a defect — but a row that says
   "measured on a KZ" when the kart has no shifter is the kind of claim this pass
   exists to catch.

What would settle it: one side elevation of a KZ with a scale in frame. Failing
that, add the tolerance and the provenance caveat and keep the values.

### D4 — §20.4's caster 18.0° and kingpin inclination 11.0°, tagged `sourced`

Basis: a KartPulse forum thread of angle-gauge readings on private chassis, and
*The NatSKA Guide to Karts and Karting* via a hobby blog. §00 §1 defines
`sourced` as *"there is a citation, and the citation was read rather than
recalled … For a part: the catalog listing or drawing."* A forum member's
measurement of his own kart is neither a drawing nor a catalogue, and the pinned
PDF has **zero** hits for caster, camber, chasse or Ackermann — §20.4 says so
itself.

The basis cells already contain everything an `estimated` tag requires: what was
measured, on what, the spread, and why the competing figures were rejected. Only
the tag is wrong. Same for the *"static camber 0°"* mechanism row, which is
`sourced` to the same blog.

What would settle it: a chassis HF, a manufacturer setup sheet in degrees, or the
CRG caster/camber chart annotated with angles — §20.4 notes the chart gives
positions and no degrees, which is the whole problem.

### D5 — §20.4.1's 92.96 mm scrub radius and §20.4.2's Ackermann construction rule

An academic paper (JETIR2501641) on a different vehicle, and the same hobby text.
Both are tagged as evidence in an open-conflict discussion rather than as spec
values, which is close to right, but *"the only **published** scrub radius for any
kart"* is doing citation work. §20.4.1's refusal to invent a middle is exactly
correct and should stay; the tags should read `estimated`.

What would settle it: nothing reachable. Leave the conflict recorded.

### D6 — §40's five `sourced(snippet)` figures

The knob-to-wheel gap, the reach rule, the 90° lever-to-rod rule, the bracket's
three positions and the clutch lever's 70 mm stroke, all tkart.it via search
summary. §40.10 already states the count and says the recheck pass treats them as
`estimated`. **Confirmed: treated as `estimated`.** No action beyond the tag
`snippet` already carries, which §00 §1 defines correctly.

### D7 — the OTK M4 row is confirmed but not text-extractable

Not a downgrade — a note, because it will otherwise become one. `pdftotext
-layout` on `cik_hf_frontfairing_otk_m4_100-CA-20.pdf` returns **no dimensions
at all**: only the two masses (1500 g bodywork, 1320 g support). The 1090 / 287 /
227 / Ø20×1.5 / Ø16×1.5 / 450 / 550 figures are a raster on page 2 and were read
here by rendering at 200 dpi. A future pass that greps the text and finds nothing
will conclude the form is undimensioned and downgrade six correct `sourced` rows.
The KG 505 (1029 / 317 / 203) and KG C2 (1360 / 187 / 177) figures **do** extract
as text.

## 99.4 Mislabels — limit vocabulary without a citation establishing a limit

The defect this document exists to prevent. Four findings, and the document
catches two of them itself.

| # | where | text | verdict |
| --- | --- | --- | --- |
| M1 | §00 §3, `track_rear` | *"equals the 1400.0 mm track-width maximum"* | **new.** A choice presented as a limit. D1 |
| M2 | §20.5, `axle_diameter` | *"KZ runs the cap"* under a `sourced` tag | **new.** D2 |
| M3 | §00 §3, `wheelbase` | flags `params.py`'s *"1050 mm max (KZ runs at the limit)"* | **confirmed correct.** Art. 9.1.1's range is 1010.0–1070.0 and the maximum is 1070. The value is fine, the word is not, and §00 §3 says exactly that |
| M4 | §40.11 item 7 | `params.py`'s header still reads `Overall length 1830 mm max` | **confirmed still outstanding.** It is the original defect, unfixed in the file |

Nothing else in the document writes an estimate in the vocabulary of a limit.
Specifically checked and clean: every "maximum"/"minimum" in §00 §4 traces to a
quoted article; §20.2.1's tire dispositions relabel rather than reword;
§20.8's closing claim (*"none is written in the vocabulary of a limit"*) holds
except for M2, which is in §20.5's own table; §30.6.2's `sourced (φN)` /
`sourced (φR)` tags name the form's own dimension letters, which is the strongest
citation style in the document and should be copied elsewhere; §50.2's compliance
table signs every margin toward the limit that binds and never confuses the two.

## 99.5 Unresolved cross-section disagreements

Nine. X1 is the one that will break a build.

### X1 — the right main rail's path, and with it the engine mount and the radiator brackets. 36 to 47 mm.

| | says |
| --- | --- |
| §10.3, §10.5, §10.6 item 5 | the rail is **straight** at **x = +310, z = +50** from y −48 to y −720. *"The rail as respecified is straight through the entire engine bay, so there is one number for both clamps."* Tube surface x 295…325 |
| §30.2 | reads `frame.py:_rail_path` **as built** — *"x_rail(y) = 285 + 0.125·(y + 100)"*, pinching 7.13° inboard — and states as a **requirement**: *"the right main rail's centerline must pass through (x 273.6, z 50.0) at y = −191.5 and (x 262.7, z 50.0) at y = −278.5"*. Clamp bodies authored at x 271.6…296.6 and 260.7…285.7 |

§30.2 says *"if the rail moves the clamps move with it and nothing has to be
re-derived by hand"* — but the numbers it publishes to `params.py` and to the
gates are absolute, and they are derived from the rail §10 deletes. A clamp bored
Ø30 on (273.6, 50) does not touch a tube centred on (310, 50): the bore's
outboard edge is at 288.6 and the tube's inboard surface at 295, so the pair is
**6.4 mm apart** at the front station and **32.3 mm** at the rear — which is the
12.10/22.90 mm gate finding this whole exercise was meant to close, moved rather
than fixed.

Same root cause, three more places:

- §30.7's `radiator_bracket_lower`/`_upper` rail ends at **(−276.4, −169.2, 50)**
  and **(−257.9, −316.8, 50)**, *"from `x_rail(y)` mirrored"*. §10.3's rail at
  those stations is at **−310**. 34 to 52 mm.
- §30.2's *"the clamps pierce the floor tray"* argument, and §30.7's for the
  radiator rods, are both computed against `chassis_floor_tray` at x ±280 from
  y +180 to −580 — the tray §10.7 respecifies to y +40…+760 with an hourglass
  half-width. Under §10.7's tray there is **no tray anywhere near** the engine
  bay or the radiator, so four `pierced` declarations §30.8 asks to **add** are
  declarations §10.9 would have gate 2 fail for the right reason.
- every clearance in §30 quoted against `chassis_floor_tray`, `chassis_rail_r` or
  the side bar's interpolated `side_path`.

Whoever merges these two sections has to pick one rail and re-derive the other
section's dependents. **§10's rail is the sourced one** — it comes from the CRG
plan drawing's constant outer half-width over y −735…−48 — and §30's is the
current `frame.py`. So the direction of the fix is: §30's clamp x spans, both
radiator bracket rail ends, and every §30 clearance against the rail or the tray
are recomputed at x = 310 and against the y +40…+760 tray.

### X2 — the pedal station. Three sections, three answers.

| section | pedal pivot / pad |
| --- | --- |
| §10.6 item 3 | mount pickup **(±259, +560, +50)** on `chassis_cross_front`'s leg, *"contacts the tube at 0 mm and gate 2 passes"* |
| §20.6.4 | brake pedal pad **(−75, +560, +90)**, from `pedal_separation`/2, `pedal_y`, `pedal_z` — i.e. the pre-§40 values |
| §40.5 | `pedal_pivot` **(±85, +610, +50)**, foot bar at (±85, +585, **+228**), `pedal_z` corrected from 90 to **228** (a 138 mm change), and a **demand for a new `chassis_cross_pedal`** at y +610, z +75, Ø≥22, x ±160 — a part §10 does not create anywhere |

§40.5 states the reason §10's pickup cannot serve: *"they cannot reach a pivot at
y +610 from there."* §10.5's `chassis_cross_front` loop does run to y +760, so
there is frame for a y +610 tube to belong to — §40.5 says so, citing the CRG
form's 250 mm front overhang — but §10 never adds it and §10.4's cross-member
table has no y +610 row.

§40.5 also flags a real conflict with itself worth carrying forward: a pivot at
z +50 is 19–44 mm **below** the tray's top surface, and Art. 4.6 forbids ribs
and wants a single element, so the tray cannot simply be notched around two
supports. §40 authors z +50 on the part photograph and hands the question to
§Chassis, which has not answered it.

### X3 — the master cylinder and its pushrod. 104 mm, and the shorter one is the sourced one.

| section | says |
| --- | --- |
| §20.6.4 | `brake_master_rear` (−150, **+430**, +120), `brake_master_front` (−105, +430, +120), `brake_pushrod` **135 mm** *"pedal arm eye to master piston eye"*, and explicitly **rejects** the sourced 490/525 mm OTK control rods as belonging to an OTK-style layout this kart does not have |
| §40.5 | pushrod clevis at (−85, +602, +105), pushrod **68 mm** `sourced` (OTK 0119.01 *"Push rod, BSM, 68 mm"*), *"so the cylinder's mouth is at y ≈ **+534**"* |

Both sections adopt the same CRG-style short-rod layout and then disagree by
104 mm about where the cylinder is. §40's 68 mm is `sourced` to a catalogue part;
§20's 135 mm is `derived` from a pedal geometry §40 replaces (X2). The bore
agrees at 22 mm in both, verified from `82/FR/11` — that half is settled.

One more half-resolution nobody noticed: §40.5 says the second Art. 4.12.2 link
*"is a pedal-side part that no section currently owns"*. §20.6.4 owns it —
`brake_pushrod_link`, 2.0 mm cable, with the correct note that 1.8 is a floor and
not a practice. §40 should point at it rather than open an item.

### X4 — the radiator's own figures. §00 §5a still carries the pre-§30 build.

| quantity | §00 §5a | §30.7.2 |
| --- | --- | --- |
| top edge | z **497**, *"3 mm of margin"* | z **407.9**, *"92 mm under the 500 ceiling, against 3 mm today"* |
| fore-aft span | y **−375 … −95** | y **−402.9 … −67.1** |
| outboard edge | x **−489**, 61 mm of margin | x **−490**, 60 mm of margin |
| core width | (implied 265) | **250**, `sourced` EM-01 |

§30 supersedes deliberately and says so. The problem is that **§00 §5a is the
contract every other section cites for the cooling envelope** — §50.3's Demand 2
and §50.5's half-space test are both written against §5a's 489, and §30.10's
provenance tally is written against 250/240/45°. §5a's table has to be updated or
explicitly marked as "as built, superseded by §30.7", or the next reader will
size a pod against a radiator that is 90 mm taller than the one being built.

### X5 — what the radiator hangs off. §00 §5a says the seat stays; §30 and §40 both say the rail.

§00 §5a: *"The radiator hangs off the seat **stays**, not off the shell."*

§30.7 (*"The anchor is `chassis_rail_l`, not the seat and not the stays"*) and
§40.3 both reject the stays on the same two articles, and **both articles say
what they are quoted as saying**: Art. 4.2.3 puts the welded attachment points
for *"radiator(s)"* on the frame (PDF p. 8), and Art. 4.8.2 makes a seat stay
*"bolted at each end"* and *"removed from the chassis frame and seat"* if unused
(PDF p. 11), so it is a removable member and not a mounting rail. Art. 5.3.1's
*"Radiators must be placed above the chassis frame"* points the same way.

§00 §5a is wrong and it is the file the other two cite. The prohibition it
states — no joint to `seat_shell`, ever — is correct and all three sections agree
on it; only the positive claim about the stays is the defect.

### X6 — front bumper overhang: 420 or 425.

§10.2 authors `overhang_front_bumper` = **420**, `estimated`, with 70 mm of
margin over Art. 9.4.1's 350 and its reasoning stated (84 mm behind the fairing's
front face, 29% into the fairing's 287 mm depth). §50.2's compliance table reads
**425** with *"+75"* of margin. 5 mm; one owner. §10 has the reasoning, so §50's
row should read 420.

### X7 — rear protection width: 1360 sourced, 1390 built, and §00 §5b does not record the departure.

§00 §5b: KG C2 `003-BR-48` at **1360**, and *"the KG C2 clears the 1,340 minimum
by 20 mm."* Confirmed from the form.

§50.11 specifies **1390**, `derived`, on a real argument: 1360 on *this* kart puts
the panel's edge at 680 against a rear tire inner edge at 485, leaving 195 mm
where Art. 9.5.5.1 wants a 200 mm clearance window. It also cites *"the rear
wheel protection must be in line with the outside of the rear wheels"*, verified
on p. 25.

Both are defensible; the document just never says in §5b that the panel it builds
is not the panel the form dimensions. One sentence in §5b, pointing at §50.11.

### X8 — fuel tank: 8.5 L or 9 L.

§40.6 specifies **8.5 L**, `sourced` to OTK `0073.EA`. §60's materials table
describes the tank under a *"`SERB 9Lt` left/right decal pair"* and says *"9
litres clears Art. 9.3's '8 litres minimum'"*. Cosmetic, but the livery names a
capacity the part does not have, and a reviewer reading §60 alone gets 9 L.

### X9 — which rear-protection article governs a Group 1 kart. Not resolvable from the pinned PDF.

Consequence of C1 item 2. Group 1's bodywork set (PDF pp. 20–21) lists 8.5.2,
8.5.3, 8.5.4.1 and **8.5.5.2** only — *rear wheel protection **with wheel
covers***, deferring to Art. 9.5.5.2. There is no 8.5.5.1. §00 §4 and §50.11
build to **9.5.5.1**.

And the general articles contradict each other: Art. 4.11 (p. 11) reads *"In
Groups 1, 2 & 3, it is mandatory to use a homologated rear wheel protection"*
while Art. 4.11.2 (p. 12) reads *"In Groups 1, 2 & 3 of FIA categories, it is
mandatory to use a homologated rear wheel protection **with wheel covers**"*.
Both are stated as mandatory and they describe different parts. Nothing in the
pinned PDF resolves it.

Recorded as unresolved. If the class is settled as **KZ2** (Group 2), the whole
question goes away — Art. 9.5.5.1 and 9.5.5.2 are both present in Group 2's own
set and 9.5.5.1 is the one §50 builds. Which is one more reason C1's first
sentence matters.

## 99.6 What I think is wrong that nobody asked about

### W1 — Art. 4.6 settles the *"impenetrable"* question and §10 declares it unanswerable, because §10's own quote stops one sentence short.

§10.7 spends a paragraph interpreting *"impenetrable"* against a clearance
aperture for the steering support, tags the reading `estimated`, and §10.12 lists
it: *"Art. 4.6 'impenetrable' vs a clearance aperture for the steering support |
**nothing in the pinned PDF resolves it**; a scrutineering bulletin would."*

Art. 4.6's next paragraph, PDF p. 10, verbatim:

> The floor tray may be perforated, but the holes must not have a diameter of
> more than 10 mm and they must be separated by four times their diameter as a
> minimum. In addition, two holes with a maximum diameter of 35 mm are allowed
> for steering column and/or gear shift lever access.
> The floor tray may be made of composite material.

§10.7's quote block ends at *"...preventing the driver's feet from sliding off the
floor tray"* — one sentence before the answer. **§40.1 quotes the same paragraph
correctly, bolds that clause, and uses it**: §40.2's second demand on §Chassis is
*"`chassis_floor_tray` must carry a Ø35 access hole centred on (0, +477) … Art.
4.6 permits exactly two such holes and this is one of them."* So one section has
the resolution and the other calls the question open, in the same document.

Three consequences beyond the tag:

1. The two permitted apertures are for the **steering column** and the **gear
   shift lever**. §10.7's `pierced` declaration is between the tray and
   `chassis_steering_hoop`'s **two legs**, which is a support and not a column.
   That declaration is not covered by the sentence and still needs its own
   argument — a narrower and more honest open item than the one §10.12 carries.
2. **Ø10 with 4× spacing is a hard cap on general perforation** and appears
   nowhere in the document. Any lightening pattern a later pass adds to the tray
   is constrained by it.
3. §40 claims one of the two Ø35 holes for the column and **nobody claims the
   other**, though the article names the gear shift lever. §40.4 routes the
   shifter outboard on `chassis_rail_r` and §40.4's shift rod crosses over the
   rail at y +88 at x 285, z 83.4 — which under §10.7's Art. 4.6 tray (edge at
   the rail centerline, top z 69) is **directly above the tray's edge**. Worth
   one measurement: either the rod clears the pan, or the second Ø35 hole is the
   one the article anticipated.

### W2 — the *"must not be able to retain water, gravel or any other substance"* clause is used for the fairing and ignored for the pods.

Art. 9.5.2 and Art. 9.5.4 both carry it, PDF p. 24. §50.8 uses the fairing's copy
well — it is why the vent is placed low on the rear wall rather than at the crown.
**Nothing anywhere uses the pod's copy**, and it is a real shape constraint on the
part it is missing from: a C-section pod whose mouth faces outboard, whose floor
sits 48 mm off the ground and whose two free edges are 147 mm apart laterally is a
trough. §50.10's section row and the returned-lip geometry of §50.7 both need to
answer it — a returned lip on the *lower* free edge is a gutter unless it is
drained or turned down. One row in §50.10, and it may change the lip's
orientation on one edge.

### W3 — every regulated tube in §10.8 is dimensioned by OD only, and the walls are sourced.

Art. 9.4.1 and 9.4.2 fix the bumper diameters and the OTK M4 form fixes the
**walls**: `acciaio Ø20x1.5` and `acciaio Ø16x1.5`. §50.8 records 1.5 mm for
both fairing-kit tubes. §10.8's `chassis_nose_hoop_lower` (Ø20),
`chassis_nose_hoop_upper` (Ø16), `chassis_front_bumper_support` (Ø16),
`chassis_side_bar_?` (Ø20), `chassis_side_bar_upper_?` (Ø20) and
`chassis_rear_bumper` (Ø20) carry **no wall at all**.

A tube with no wall exports as a solid rod, `genkart.sh`'s signed-volume assert
cannot tell the difference, and Art. 9.4's *"magnetic steel round tubing"* is the
word the whole family is built on. The 1.5 mm is `sourced` for the two fairing
tubes and is a defensible `estimated` for the bumpers by analogy with the same
manufacturer's kit. One row per bar, six rows.

### W4 — the headline 1920 mixes four manufacturers and never cites the article that makes that legal.

The number is OTK fairing depth 287 + CRG frame + KG C2 protection depth 187,
with a Gillard tube diameter and Vega tires. **Art. 9.5, PDF p. 24:** *"Combining
homologated bodywork elements is allowed. However, the two side pods must be used
together as a set."* §50.10 cites the second sentence to justify mirroring the
pods. Nobody cites the first, and it is the article that licenses the entire
mix-and-match the document's central derivation depends on. Without it a reviewer
reasonably asks whether the specified kart is a legal assembly at all — and the
answer is yes, in one sentence, on a page already quoted.

### W5 — §20.6.1's rear caliper count reads a "per wheel" heading as a total.

`82/FR/11`'s table is headed *"Nombre d'étriers / Number of calipers — **Par
roue / Per wheel**"* with 1 front and 1 rear. §20.6.1's table renders that as
*"rear calipers | 1"*, which is what §20 builds and is physically right for a
single axle-mounted disc — but the form's own heading says per wheel, and a later
pass reading the form cold will make it two. Same for *"front calipers | 2"*,
which is 1 per wheel × 2 wheels and happens to come out right. One parenthesis on
each row naming the heading.

### W6 — the Vega form filenames say XH3 and the forms are XH4.

`refs/kart-visual/run_vega_xh3-ant.pdf` and `run_vega_xh3-post.pdf` are
`047-TO-12` and `047-TO-14`, model *"XH4 CIK Option"*, and the 2024–2026
technical list confirms XH4 against those two numbers. The spec cites them by
homologation number throughout and is **correct** — but the filenames will
mislead, and they are in the same family as the captions already on the
do-not-trust list. Worth a rename or a line in the spec's source list.

### W7 — §30.4's 25° cylinder lean is the largest single `derived` load-bearing assumption in the document, and it is one photograph away from `sourced`.

Recorded because §30.10 asks for the right two things and this pass can confirm
the premise it rests on: the exhaust port angle **is** on the KZ-R1 HF and the
cone table **is** exact, so the packaging arithmetic that forces the lean has
sound inputs. What is not sourced is the lean's *direction*, and §30.4 says so.
The whole exhaust routing, the airbox's 60 mm lift, the carburettor's 12 mm drop
and the intake boot's four-point path all hang off it. It is the single highest
leverage open item in the document and §30.10's request — one side-on photograph
of a KZ engine in a chassis — is the cheapest thing on any list here.

## 99.7 One line on the shape of the result

Twelve corrections in 282 checked rows, and **two of them are the document
correcting itself wrongly** — §00 §4 withdrawing a correct 550 mm cross-check,
and §40.11 replacing a correct Art. 8.3 with a wrong Art. 9.3 while citing the
§7.4/§7.2 incident as the reason. Both were written by the mechanism that is
supposed to catch this class of error, in the vocabulary of having caught it.
The lesson is not that the mechanism failed; it is that **a correction is a
citation and gets checked like one.** A "WITHDRAWN" block deserves the same
re-read as the claim it withdraws, and neither of these two was re-read.

Against that, the form-backed core is exact: 30 cone diameters, 30 slant lengths,
16 chassis-form fields, 10 tire dimensions, 22 brake-form figures and 6 fairing
figures all reproduce digit for digit. The document is right about far more than
it is wrong about, and where it is wrong it is wrong about *which article*, not
about *which number* — with C1 and C2 the only two that reach a governing
article, and neither of them moving a built dimension.
