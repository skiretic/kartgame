# Valdirone

**Lens: the lap as a composition.** 1,367.222 m, counter-clockwise, eight named
corners, 47.08 s estimated, 7.393 m of elevation, closes to 0.48 mm.

---

## 1. The one relation that designed this circuit

Before any of the rhythm argument, here is the number the whole layout is built on,
because it is the thing that turns a plausible-looking plan view into a circuit that
drives.

A kart does not drive the centerline. It drives the widest arc the road allows. For a
corner of centerline radius `R`, width `W` and direction change `theta`, that arc has
radius

```
R_line = R + e * (1 + k) / (k - 1)      e = (W - 1.400) / 2      k = sec(theta / 2)
```

(1.400 m is the FIA Karting Art. 8.1.1 maximum kart width, already recorded in
`ARCHITECTURE.md` §6.4.) The multiplier `(1+k)/(k-1)` is brutally non-linear in the
angle:

| theta | 30° | 45° | 60° | 90° | 100° | 120° | 140° | 180° |
|---|---|---|---|---|---|---|---|---|
| multiplier | 57.7 | 25.3 | 13.9 | 5.83 | 4.60 | 3.00 | 2.04 | 1.00 |
| opening at W = 10 m | +248 m | +109 m | +60 m | +25 m | +20 m | +13 m | +8.8 m | +4.3 m |

So at kart-circuit width, **a corner under about 80° of direction change is a
line-choice problem, not a grip problem, whatever its radius.** T2 on this lap is
90 m through 45° at 9 m: line radius 186 m, 0.78 g at 136 km/h. T7 is 28 m through
90° at 10 m: line radius 53 m, and it is a genuine 112 km/h grip event driven at 85.
Same road width, same kart, completely different corner — and the difference is the
angle, not the radius.

That is why Valdirone has seven corners above 80° and exactly one deliberate kink.
It is also why the kink exists at all: without it, T1's exit to the hairpin braking
point is a 355.6 m straight, and CIK-FIA Appendix 13 caps a straight at 200 m. The
regulation put a corner there. The corner is honest about being a regulation.

The formula needs one clamp or it lies. The line's tangent length is
`R_line * tan(theta/2)`, and that has to fit inside the corner's own tangent plus the
shorter of the two adjacent straights. Without the clamp it returns a 220 m line
radius for T6a, a compound entry arc which physically has no approach straight at
all. With it, T6a returns 60 m and T6b returns 22 m — which is precisely why a
decreasing-radius compound is hard: **the entry arc denies the apex arc its width.**

---

## 2. The composition

Three movements, near-identical in time and nothing alike in tempo.

| | distance | time | avg | corners | shifts | what it is |
|---|---|---|---|---|---|---|
| S1 | 0 – 538.0 m | 15.049 s | 128.7 km/h | T1, T2 | 2 | the breath, then the fall |
| S2 | 538.0 – 953.0 m | 16.048 s | 93.1 km/h | T3, T4, T5 | 11 | the bowl |
| S3 | 953.0 – 1367.2 m | 15.398 s | 96.9 km/h | T6, T7, T8 | 5 | the decision |

A sector delta on the HUD therefore means something specific. Sector 1 has exactly
one corner that can cost you time, so a red S1 is T1's exit and nothing else.
Sector 2 is the gearbox — eleven shifts against two — so a red S2 is a hairpin exit
or a gear taken too early up the climb. Sector 3 is placement: four direction
changes in 414 m, and a red S3 means a line, not a speed.

### Where the driver breathes

Twice, at opposite ends of the lap, and both are corners rather than straights.

**T1**, 99.484 m of constant radius held for 2.98 s at 1.39 g. This is the longest
continuous load anywhere on the circuit — the next longest is T8b at 2.63 s — and it
is the only place the sim can be asked whether the inside rear actually lifts, which
is issue #32's whole acceptance test. The HUD needs 0.5 s of continuous g to credit a
sustained figure; T1 gives it six times over.

**The climb**, 186.7 m at up to +5.00%, 1st through 5th, one steering input. Twelve
seconds after the busiest 45 m on the lap.

### The busiest 30 seconds

d = 496 to d = 1010, about 24 s. It runs: peak speed 141.3 km/h → 45.0 m of braking
with five downshifts → 140° hairpin in first gear → 186.7 m climb with five upshifts →
brake off a crest into T4 → 32.5 m to cross the road into T5 → 75 m → T6's compound.
Thirteen gear changes and six direction changes in 514 m. Everything either side of
it is one input at a time.

### Does the lap end so the next one starts well

T8 is a compound increasing-radius left, 45 m opening to 60 m, 124.354 m of arc taken
from 95 to 122 km/h. Its exit owns 164.649 m of straight — the 77.150 m run to the
line plus the 87.499 m from the line to T1. Six km/h lost at that exit is 0.17 s by
T1's braking point and about 1.1 km/h of entry speed into the only place on the lap
you can pass. Over the 25 laps of a 30 km Final that is 4.3 s, which is the gap
between second and fifth.

So the last corner of lap *n* is the first thing that matters on lap *n+1*. That is
the intended reading, and it is why the third sector ends at the start line rather
than at T8b's exit tangent — the sector delta has to include the straight the exit
paid for.

---

## 3. Corner by corner, as a driver meets it

**T1 – Ronda** · 57 m · −100° · 12 m wide · 5th · 120 km/h
You cross the line at 132 km/h in fifth, take it to 138.1, and lift rather than brake
— the braking zone is 12.0 m, because there is no such thing as a heavy braking zone
on a kart circuit. Then three seconds of nothing but holding it. The corner is 12 m
wide because two karts fit: at 1.400 m each with a metre to each edge and three
metres between, that is 5.8 m of the 12. If you are the one on the inside you are on
a 65.3 m line radius pulling 1.73 g; the kart around your outside is on 81.4 m
pulling 1.39. Under the friction ellipse that leaves you 0.367 of your longitudinal
capability against their 0.665. **They have 1.8× the drive available for the whole
99.5 m of the corner.** You will not hold it to the exit, and the exit feeds 156.7 m
of shelf.

**T2 – Il Filo** · 90 m · +45° · 9 m · 6th · 136 km/h
Downhill at −1.92%, sixth gear, 0.78 g. It is a corner in the way a doorway is a
corner. Take it wrong and you lose about a tenth; take it right and you were going to
be flat anyway. It exists because the road between T1 and the hairpin would otherwise
be 355.6 m long and the maximum legal straight is 200 m.

**T3 – Il Pozzo** · 16.5 m · −140° · 12 m · 1st · 52 km/h
The best 45 metres on the circuit. You come out of Il Filo at 136 and keep going
downhill to 141.3 km/h at d = 497, the fastest point on the lap. Then 45.0 m of
braking to 52 km/h with five downshifts in it, into a 12 m entry at the bottom of a
7.4 m bowl. The two lines through it are genuinely different: the racing line's radius
is 27.3 m with a 67.5 km/h quarter-lock ceiling, the defensive half-width line is
21.9 m with a 53.5 km/h ceiling — **14.0 km/h apart, and the defensive one is only
1.5 km/h above the corner's own apex speed.** Defend here and you are on the edge of
#137 for the whole corner. Then the exit is 186.7 m of climb through five upshifts on
a powerband 5,000 rpm wide, which is a drag race you have already lost.

**T4 – Rampa** · 30 m · −90° · 10 m · 4th · 100 km/h
The climb flattens 63.2 m before you turn in, over a 1,300 m convex vertical curve.
Your 11.8 m braking zone starts inside the run-out of that transition, so the tires
unload exactly where you want them loaded. First half of the esse.

**T5 – Sella** · 34 m · +85° · 9 m · 4th · 98 km/h
32.470 m from T4's exit, opposite hand. Crossing the full 9.5 m of usable road in
that distance at 104 km/h demands **3.07 g**; the kart has 1.86. At 1.0 g you get
3.10 m of the 9.5. So both corners get driven from near mid-track, and the pair costs
**0.64 s** against the same two radii taken in isolation (T4 falls from a 114.1 km/h
ceiling to 100.0, T5 from 117.8 to 100.3). That is the two-corner linked sequence, and
its compromise is not a matter of taste — it is 3.07 against 1.86.

**T6 – Vigna** · 60 m → 22 m · −35° then −85° · 4th → 2nd · 64 km/h
It tightens, and the entry arc takes your width away. On a half-width line the apex
radius is 36.2 m and the quarter-lock ceiling is 86.2 km/h — 22 km/h of margin, easy.
Commit early and you arrive pinned to the inside on a 22.0 m line whose ceiling is
**53.8 km/h against a 64 km/h apex**. That is 10.2 km/h past quarter lock and the kart
scrubs. This is the one corner on the circuit that finds #137, and it only finds it
for a bad entry. Second gear, and the only place on the lap you use it.

**T7 – Chiave** · 28 m · +90° · 10 m · 3rd · 85 km/h
You come over a 650 m crest 15 m before turn-in. Sight distance over a convex curve is
`sqrt(2R) * (sqrt(h_eye) + sqrt(h_target))`; at R = 650 with a 0.75 m eye height and a
0.15 m kerb that is 45.2 m, which at 80 km/h is 2.03 s. **It does not hide the apex —
it hides where you brake.** Then the sacrifice: this corner holds 112.0 km/h and its
entry is limited to about 95 by T6b's exit, and you drive it at 85 because the exit
has to put you on the right of the road for T8. That costs 0.196 s across its 43.982 m
of arc. It is the only third-gear corner on the lap.

**T8 – Uscita** · 45 m → 60 m · −45° then −85° · 4th → 6th · 95 → 122 km/h
It opens. 124.354 m of arc with the throttle coming in progressively, and the exit
owns 164.6 m of straight. Get it wrong and you have already lost the next lap.

---

## 4. Proving no two corners ask the same question

| corner | the question | why nothing else asks it |
|---|---|---|
| T1 | can you hold 1.39 g for three seconds with a kart alongside? | longest load on the lap (2.98 s); next is 2.63 s |
| T2 | how little can you disturb the kart at 136 km/h? | the only corner under 0.8 g; the only one that is a straight in disguise |
| T3 | five downshifts and first gear in 45 m | the only 1st-gear corner; the only 45 m braking zone |
| T4 | brake with the tires unloading over a crest | the only corner whose braking zone sits in a vertical transition |
| T5 | cross the road when the road does not let you | the only corner whose entry demands more g than the kart has |
| T6 | can you be patient when it tightens? | the only decreasing-radius compound; the only #137 exposure |
| T7 | give up 27 km/h now to be right in 200 m | the only corner driven 27 km/h below its own ceiling on purpose |
| T8 | when exactly does the throttle go down? | the only increasing-radius compound; the only exit that owns a straight |

Gear coverage, from `gearbox.h`'s tooth counts and a 0.1475 m rear radius:

| gear | band | corner |
|---|---|---|
| 1 | 34.1 – 53.0 | T3 apex (52 km/h, 12,300 rpm) |
| 2 | 47.7 – 74.2 | T6b apex (64) |
| 3 | 57.7 – 89.7 | T7 (85, 13,270 rpm) |
| 4 | 70.5 – 109.6 | T4 (100), T5 (98), T6a (100), T8a (95) |
| 5 | 82.7 – 128.7 | T1 (120, 13,050 rpm), T8b exit (122) |
| 6 | 93.4 – 145.3 | T2 (136), the straights |

Nine upshifts and nine downshifts per lap, and the pattern is different every time:
one down at T1, none at T2, five down at T3, four up on the climb, one down at T4,
none at T5, two down through T6, one up at T6b's exit, one up at T7's, two up through
T8.

---

## 5. Elevation, and why it is all in the slow part

`R = V²/K` is not a suggestion, it is a tax on speed. Compare the two big gradient
changes on this lap:

| | at the crest (d = 243) | at the bowl bottom (d = 629) |
|---|---|---|
| gradient change | 2.517% | 6.917% — **2.7× larger** |
| speed | 130.2 km/h | 97.0 km/h |
| minimum radius | 1,130 m (convex) | 471 m (concave) |
| radius used | 1,600 m | 900 m |
| straight it eats | 40.3 m | 62.3 m |

Nearly three times the gradient change for one and a half times the length, purely
because the kart is 33 km/h slower. That is the whole argument, and it is why the
range is 7.393 m with the low point at the hairpin and the high point 300 m earlier
on a straight.

Six vertical curves, all sized between 1.3× and 2.5× their regulation minimum. The
tightest fit is VC3 at the top of the climb (1,300 m against 971 m required, ending
30.7 m before T4's turn-in, where the braking zone is 11.8 m) — and that fit is the
reason the climb straight is 186.7 m rather than the 200 m the regulation would allow.

Segment 21 and segment 0 both carry +0.60%, deliberately, so there is no vertical
curve within 150 m of the start line in either direction and eight karts do a standing
start on one plane.

---

## 6. Reversed, it is a different piece

Not a flipped one. Three things change that are not geometry.

**Every corner changes hands, and this kart is not symmetric.** It tips at 2.43 g left
and 2.81 g right, because 27 kg of engine sits 41 mm right of the centerline. Forward,
the two longest-loaded corners are lefts, so the inside rear unloads early and the kart
rotates — `ARCHITECTURE.md` §6 is explicit that the wheel lift *is* the differential.
Reversed they are rights at the same g against a 16% higher threshold, the inside rear
stays down, and the locked axle scrubs. **Forward is the layout where the kart rotates;
reverse is the layout where you have to unload the axle yourself, on the kerbs.**

**The compounds invert.** T8 opens forward and is an exit-discipline corner at the end
of the lap. Reversed it is the *first* corner off the start straight, it closes (60 m
to 45 m), and it is entered at 132 km/h instead of 106 — 26 km/h faster on the same
asphalt, and it is the hardest corner on either layout. Meanwhile T6, forward's #137
detector, becomes an opening corner out of the infield's slowest point: the detector
disappears and a throttle-timing problem replaces it.

**The bowl reverses and gets better.** Forward the hairpin is approached down 127.1 m
of shelf at −1.92% and braked in 45.0 m. Reversed it is approached down the climb
straight — 63.2 m level, 76.3 m at −5.00%, then 47.2 m at +1.92% — so the kart
accelerates downhill to about 138 km/h and brakes *uphill*, where gravity adds
0.19 m/s² and the zone is 41.5 m. A longer approach into a shorter, more stable
braking zone. The reverse hairpin is the better overtaking corner of the two
directions, and the forward layout cannot have it, because the climb has to be a climb.

Reverse overtaking spots: **T3**, off the 186.7 m reversed climb, with the same 14.0
km/h line-ceiling difference and an exit that now feeds a *climbing* shelf; and **T1**,
now the last corner rather than the first, where the same 12 m of width that let two
karts run side by side forward now lets a defender wreck their own exit onto the start
straight. Forward T1 is entry-critical. Reversed it is exit-critical. Same asphalt,
opposite question.

All six vertical curves swap profile when you reverse, and because K = 15 convex against
K = 20 concave, the convex requirement is always the binding one — so sizing every curve
to its convex minimum in the forward direction makes the reverse layout legal for free.
Checked: reverse minima 847.4 / 627.8 / 728.5 / 767.7 / 317.6 / 627.5 m against
1,600 / 900 / 1,300 / 1,200 / 650 / 1,200 m specified. Six for six.

What genuinely breaks is in `rhythm.json`'s `reverse.what_breaks`: T8's run-off
(sized for 106 km/h, faces 132), the exit kerbs, the sector marks, and the fact that
the reverse layout's first-corner requirement is what sets T8's width to 12 m — a
width decision on a corner 1,200 m into the *forward* lap, made for the reverse one.
That is the concrete rebuttal to `GAMEDESIGN.md` §10's "a spline direction plus a new
racing line".

---

## 7. If you are about to drive it for the first time

- **T1 is a lift, not a brake.** Twelve metres of braking zone. If you are stamping on
  the pedal you have already lost the corner, and then three seconds of it to think
  about.
- **The hairpin is the whole lap.** Forty-five metres of braking, five downshifts,
  first gear. Take the wide line even with someone inside you — their ceiling is
  53.5 km/h and yours is 67.5, and the climb settles it.
- **In the esse you cannot have both.** T4's exit and T5's entry want opposite sides
  of the road and there are 32 metres between them. Pick the middle and stop fighting
  it.
- **Vigna tightens.** If you are turning in early because it looked like a 60 m corner,
  it is a 22 m corner and you will find out at the apex.
- **Chiave is where you give something up.** It will hold 112. Drive it at 85 and put
  the kart on the right of the road, because Uscita's exit is worth four seconds over
  a Final.
- **The lap ends on the throttle, not the brakes.** Uscita opens. Wait for it.

---

## 8. What I do not trust about this

Stated plainly, and expanded in `rhythm.json`'s `risks`:

1. **47.08 s is optimistic.** Quasi-static point mass, friction ellipse, no
   combined-slip loss at turn-in, no #137 scrub, and an acceleration model asymptotic
   to 143.9 km/h when the kart is actually rev-limited there. Expect 51–55 s from the
   real solver. The absolute number does not matter; the sector *balance* does, and it
   could drift.
2. **Every corner speed here rests on a confounded experiment.** `drive_probe.gd`'s
   three quarter-lock rows differ in throttle as well as in lock. See
   `outside_scope_concerns` — it is the largest single source of error in the table and
   it is not mine to fix.
3. **T2 is a bureaucrat.** Seven working corners and one that exists because a straight
   cannot be 355 m long.
4. **The sacrifice at T7 is close to break-even in isolation** — 0.196 s given up
   against 0.17 s recovered. It clearly pays only when someone is behind you. I did not
   manufacture a sequence where it obviously pays, because at kart speeds and kart
   straight lengths it generally does not.
5. **418 × 344 m for 1,367 m of track is a low density**, and a minimum separation of
   30.92 m against a 14 m requirement means the layout is not working the site hard.

---

## 9. Reproducing the numbers

Every figure in `rhythm.json`'s `measured` block came out of the brief's own `walk()`
at 0.25 m step, run on the segment list exactly as it appears in the file:

```
total_length_m   1367.2232
closure_error_m  0.000478
final_heading    -360.000000
min separation   30.9236 m   between d = 541.20 and d = 581.27
longest straight 186.684 m   (Climb, lower + middle + upper)
bounding box     418.5 x 344.3 m
elevation close  -0.00001 m  (sum of grade_fraction * segment_length)
```

The self-intersection test is every pair of samples more than 40 m apart along the
circular lap, at 0.5 m sampling. The worst pair is the hairpin's own turn-in and exit
tangent points — 40.3 m apart along the lap and 30.92 m apart in plan — which is the
correct answer for a 16.5 m hairpin and confirms nothing else on the circuit comes
near itself.

The elevation closure is exact because a symmetric vertical curve rejoins both
tangents, so the piecewise-linear tangent profile gives the true elevation at every
grade break and the sum over the lap is the closure.
