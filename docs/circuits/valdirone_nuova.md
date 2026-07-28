# Valdirone Nuova

**1,375.13 m · 8 corners · 12.55 m of elevation · 44.82 s forward, 44.89 s reversed**

Closure **7.0 mm** by the brief's walk over the segment lengths exactly as published
(1.3 mm unrounded; 5.9e-13 m from the exact closed form). Final heading **−360.000000°**.
Longest straight **165.00 m**, longest uninterrupted full-throttle run **162.5 m**.
Elevation closes to **0.000000 m**. Sectors **14.351 / 15.232 / 15.237 s**, summing to
the lap because both come from one integration.

Base layout: **rhythm / Valdirone**. Grafts from all three runners-up.
Every number below is measured from the geometry in `final.json` by the same
pass that wrote `final_centerline.csv` and `final_speed.csv`. Section 11 re-derives every gate from
those three files alone.

---

## 1. The one relation that wrote this circuit

For a corner of centreline radius `R` turning through `theta` in a road of
width `W`, the widest arc the corridor allows is the circle tangent to both
outer edges with its apex on the inner edge:

```
rho = R + h * (1 + cos(theta/2)) / (1 - cos(theta/2)),    h = (W - 1.400) / 2
```

Derivation is in `synth/kart.py`. Put the vertex `V` where the two tangent
straights meet; the outer offset lines meet at `V'`, displaced `h/cos(theta/2)`
outward along the bisector; a circle of radius `rho` tangent to both offset
lines has its centre at `(rho - h)/cos(theta/2)` from `V`, so its apex is at
`(rho - h)/c - rho`; set that equal to the inner edge's apex at `R/c - (R - h)`
and solve. The line needs `2*sqrt(h(rho - R))` metres of straight before the
corner's own tangent point — which stays finite at 180 deg, where the
`tan(theta/2)` form does not, and is why a hairpin needs no approach straight to
reach its widest line.

**It reproduces 45 of the 49 line radii published across the four verify files
to within 0.1 m** once the width convention is matched. The four it misses are
the instrument verifier's numeric compound and esse solves, which no closed form
can reproduce and which this design handles explicitly instead.

The multiplier `(1+c)/(1-c)` is the whole story:

| direction change | 45° | 55° | 75° | 85° | 95° | 110° | 145° | 180° |
|---|---|---|---|---|---|---|---|---|
| multiplier | 25.3 | 16.70 | 8.68 | 6.61 | 5.17 | 3.69 | 1.79 | **1.00** |

**A corner is a direction change, not a radius.** At 9–14 m of road nothing
under about 80 deg can be slow whatever its radius: a 55 deg bend of 38 m radius
through 10 m of road has a 126 m line radius and is flat out at 143.9 km/h.
Three of the four source layouts sized their corners on the hug-the-inside-edge
path (`R + W/2 - 0.7`), which has no dependence on angle at all, and that single
error is the dominant reason all four verify files came back *major issues*.

Read forwards instead of as an obstacle it writes the corner list. **Every one
of the eight corners here changes direction by at least 85 deg.** None of them
is a straight pretending to be a corner, and all eight consequently carry a
mandatory axial run-off — which is the price, and it is paid.

## 2. The constraint nobody wrote down

On a loop that turns −360 deg net, `sum(lefts) − sum(rights) = 360`. A 180 deg
hairpin consumes half the budget on its own and every right-hander has to be
bought with an equal extra left. A matched esse is free, because it cancels.

That identity is why all four source layouts have 7–10 corners and why several
of them are flat kinks — the designers ran out of budget and spent what was left
on corners too shallow to corner. Here the budget goes:

```
lefts   T1 110  +  T3 180  +  T5 95  +  T6 85  +  T8 145   = 615
rights  T2  85  +  T4  85  +  T7 85                        = 255
                                              difference  = 360
```

Eight real corners plus one hairpin is close to the maximum a 1,375 m Grade 1
kart circuit can hold. A ninth means giving up the hairpin, and the hairpin is
the only thing on the circuit that produces a 43 m braking zone.

## 3. Which limit binds, and why it decides where a pass happens

Six of the eight corners are limited by `drive_probe.gd`'s quarter-lock envelope
and only two — T1 and T2 — by the 1.86 g grip ceiling.

| corner | θ | R | W | line ρ | grip | lock | taken at | binding |
|---|---|---|---|---|---|---|---|---|
| T1 Ronda | 110° L | 57 | 12 | 76.56 | 134.5 | 141.8 | **134.5** | grip |
| T2 Lama | 85° R | 42 | 10 | 70.43 | 129.0 | 133.3 | **129.0** | grip |
| T3 Il Pozzo | 180° L | 15 | 14 | 21.30 | 71.0 | 52.0 | **52.0** | lock |
| T4 Il Ciglione | 85° R | 22 | 9 | 47.13 | 105.5 | 101.3 | **101.3** | lock |
| T5 Vigna | 95° L | 60→22 | 10 | 44.21 | 102.2 | 97.2 | **97.2** | lock |
| T6 Forbice A | 85° L | 32 | 11 | 47.87 | 106.4 | 102.3 | **102.3** | lock |
| T7 Forbice B | 85° R | 32 | 11 | 47.87 | 106.4 | 102.3 | **102.3** | lock |
| T8 Uscita | 145° L | 45→70 | 12 | 54.86 | 113.9 | 111.9 | **111.9** | lock |

That inverts the usual reading of the regression risk. Re-measure grip at 1.80 g
and two corners lose 1.6% of their apex speed; fix or re-measure #137 and six
corners move at once, including both overtaking corners' defensive lines.

It also decides where a pass can be made. The deepest braking zone this kart can
produce on any circuit whose longest straight the regulation caps at 200 m is
about 45 m. **This circuit produces 43.0 m of it exactly once.** Everywhere else
the mechanism has to be width and exit, so width is spent where it buys a second
line — 14 m at the hairpin, 12 m at T1 and T8 — and taken away where narrowness
is the point.

## 4. A lap, as a driver meets it

**The line, 12 m wide, +0.79%, sixth gear, 142.5 km/h.** The grid is behind you
on a single plane: there is no gradient change within 261 m of the line in
either direction, on purpose. 88 m to the turn-in.

**T1 Ronda — 110° left, 57 m, 12 m wide.** You brake for 2.5 m. That is not a
misprint: 137.2 to 134.5 km/h at 1.53 g is 2.5 m, and no kart circuit can do
better off a straight the regulation caps at 200 m. So T1 is not an out-braking
corner, it is a *holding* corner: 109.43 m of constant radius, 2.93 s at 1.86 g
on a 76.56 m line, and the only place on the lap where grip rather than steering
lock is what stops you. It is a **left** because the kart tips at 2.43 g left
against 2.81 g right, so this is the side where #32's inside rear goes light
first. If there is a kart alongside, section 6 below is what happens next.

**Il Banco — 127.72 m of shelf, and the crest.** 63.86 m out of T1 the road goes
over a 1,900 m convex curve and starts down at −4.60%. You are at 138.5 km/h
across it and 0.079 g light — which is all a legal kart circuit can ever take
off you, and section 5 explains why.

**T2 Lama — 85° right, 42 m, 10 m wide.** 9.5 m of braking from 142.5 to 129.0,
and then the only 30 mm vertical-faced kerb on the property that you meet above
120 km/h. At 1.86 g on a 70.43 m line you are at the grip ceiling and the apex
kerb is on the shortest path — you cannot avoid it, which is exactly what issue
#139 needs and what neither Pietrarossa's 0.45 g kink nor Valdirone's 0.78 g
bureaucrat could deliver. It is also the corner that makes the descent legal:
without it Il Banco, La Discesa and the hairpin approach merge into a 267 m
straight against a 200 m cap.

**La Discesa — 139.71 m at −4.60%, and the fastest point on the lap.** 96.7 m of
full throttle to 140.8 km/h. The road widens under you: 11 m, then 12 over 25 m,
then 13 over another 25, then 14 into the corner. At **d = 500.0** a resurfacing
joint runs across the road — 16 m after the brakes come on and 27.2 m before
turn-in, so the grip step arrives under a pedal that is already at threshold and
not before it. Article 12 forbids paint on the asphalt; a joint is not paint.

**T3 Il Pozzo — 180° left, 15 m, 14 m wide. The corner.** 43.0 m of braking from
140.8 to 52.0 km/h on a descent — 42.74 m by the grade-corrected 1.53 g figure,
which is within 2 m of the deepest braking zone this kart can produce anywhere.
Five downshifts. First gear at the apex at **13,736 rpm**, 98% of the limiter,
so first is *available* and cannot be *held*: the upshift comes inside the first
10 m of the climb and getting it wrong costs the whole ramp. That is #38's
clutch question and #40's auto-shift question in one corner.

At 180 deg the multiplier is exactly 1.00, so the racing line is 21.30 m and
**nothing opens it**. The corridor forces the radius instead of the driver
assuming it — which is why the racing judge said keep this corner exactly as it
is, and it is imported unchanged from Pietrarossa. The compression at the apex
is the only vertical curve inside a corner arc on the circuit and it is argued
as an article 7.2 exception rather than reported as compliant: 9.20% of grade
break is the entire descent-to-climb transition and there is nowhere else it
fits. It is *concave*, so it adds 0.053 g at the slowest point rather than
taking it away.

**La Rampa — 161.02 m at +4.60%, five upshifts.** This is what a bad hairpin
exit costs, and it costs it for 161 m. You reach 125.4 km/h.

**T4 Il Ciglione — 85° right, 22 m, 9 m wide, over the crest.** The road crests
34.3 m before the braking point over an 1,800 m curve; sight distance to the
turn-in is 75.9 m, 2.18 s. The corner arrives late — it is *not* hidden, and
saying so is the point: at 125 km/h a convex break needs at least 1,042 m of
radius and article 7.2 wants it clear of the braking section, so a genuinely
blind crest before a 120 km/h braking zone is not legal on a kart circuit at
all. What you get is 14.5 m of braking from 125.4 to 101.3 — under three kart
lengths — so the entry is set by lifting in fourth and the pedal is a
correction. That is issue #39. The corner is 9 m wide *deliberately*: at the 8 m
floor the same 22 m centreline gives a 43.7 m line and 97.6 km/h, at 12 m it
gives 58.4 m and 112.6, and 101.3 is the number that makes the entry a lift.
The axis of the direction change points straight on into the cut face the climb
was excavated from, so the run-off is free from the landform.

**T5 Vigna — 95° left, 60 m closing to 22 m, 10 m wide.** Decreasing radius with
no straight between the arcs, so the entry arc denies the apex arc its entry
width and the compound runs on a 44.21 m line at 97.2 km/h. Commit early, arrive
pinned to the inside, and you are on a 28.80 m line whose quarter-lock ceiling is
71.4 km/h — 25.9 km/h below the speed the corner is taken at — and the kart
scrubs. **This is the #137 corner, and it only bites a bad line.** Alone on the
circuit you never meet the cliff here. Defending a place you meet it every lap.

**Le Forbici — T6 left, 38.43 m, T7 right. Same radius, same angle, same kerb.**
32 m and 85 deg each, 1.35 s of arc each, 11 m wide, opposite hands, about a
second apart. The kart tips at 2.43 g left and 2.81 g right because 27 kg of
engine sits 41 mm right of the centreline; that is 15.6% of margin at the same
1.72 g and no driver can resolve it across half a lap. Back to back is the only
way, and it is the one claim in the instrument document immune to its own
line-model defect because both halves inherit the same error and the reading is
a ratio.

You cannot take either half on its free line. Crossing the 9.6 m of usable road
in 38.43 m at 102.3 km/h takes 1.352 s and demands `4y/t² = 2.14 g` against the
kart's 1.86; at 1.0 g you get 4.48 m of the 9.6. So both corners are driven from
mid-track and the pair is capped at 47.87 m instead of the 63.74 m either would
have alone. That is an analytic, falsifiable number.

**T8 Uscita — 145° left, 45 m opening to 70 m, 12 m wide.** 156.21 m of arc at
111.9 km/h: **1.80 g held for 5.03 s**, by a wide margin the longest continuous
lateral load anywhere in this exercise — Pietrarossa's Curvone was 4.17 s and
Valdirone's T1 was 2.98 s. If the inside rear lifts and stays lifted, this is
where it is legible. Its exit owns 165 m of start straight, so what you lose
here you lose at the only place on the lap where a kart can get alongside. And
reversed it is the first corner of the lap, closing instead of opening, entered
30 km/h faster.

**77 m to the line at +0.79%.**

## 5. Elevation: three bands, three curves, one profile

| band | length | grade |
|---|---|---|
| A — the bench: start straight, T1, Il Banco, and the whole infield | 960.61 m | **+0.787%** |
| B — La Discesa: from the crest through T2 to the hairpin apex | 289.44 m | **−4.600%** |
| C — La Rampa: from the hairpin apex to the top of the climb | 125.07 m | **+4.600%** |

`gA` is not chosen — it is what closes the loop given the other two, and it is
inside article 7.2's 2% cap for a starting straight with 1.2 points to spare.
**Every grade break sits exactly on a segment boundary**, so the per-segment
grade list and the vertical-curve schedule describe the same road by
construction. That is the discipline Sablière did not keep: it published two
profiles that disagreed by 2.09 m, one of which missed closure by 0.601 m.

| # | at | Δ | shape | V fwd / rev | R min fwd / rev | R chosen | factor | L | a_vert |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 261.29 | −5.387% | convex | 138.5 / 132.5 | 1279.6 / 1169.9 | 1900 | 1.48× / 1.62× | 102.36 | 0.079 g |
| 2 | 550.74 | +9.200% | concave | 52.0 / 52.0 | 135.1 / 135.1 | 400 | 2.96× / 2.96× | 36.80 | 0.053 g |
| 3 | 675.81 | −3.813% | convex | 114.7 / 122.0 | 876.3 / 992.6 | 1800 | 2.05× / 1.81× | 68.63 | 0.065 g |

**All three are legal in both directions at the same K.** Vertical curvature is
invariant under `s -> L-s`, so a crest is a crest driven either way — K does not
swap and only the speed changes. Valdirone swapped K and declared six curves
legal in reverse when one was not; Cava Vecchia kept K and never ran one of its
breaks backwards at all, and has a 13.21 m overlap. The binding direction is
different for each curve here, which is exactly why both have to be run.

Radii are chosen as a **stated multiplier** over `V²/K` — at least 1.3× outside
the sections article 7.2 names and at least 1.9× inside them — rather than at
the minimum or by the adverb "adequately". That policy is Valdirone's and it is
the single most valuable thing it produced.

The range, 12.55 m driven, is **derived**: it is what 125.07 m of climbing road
at 4.60% plus 675 m of infield at 0.787% pays back, and 4.60% is the steepest
band whose crest curve (102.36 m at 1,900 m) still fits inside the 127.72 m
shelf with 12.7 m of clearance to T1's exit. Steepen the bands and the curve
grows faster than the shelf does.

And what elevation is *for*, here, is braking distance and sight line — not
grip. At the legal minimum radius `a_vert = v²/R = v²/(V²/K) = K/12.96` m/s²
**regardless of speed**: 0.157 g in any compression, 0.118 g over any crest, and
article 7.2 then says increase R further. The three curves here deliver 0.079,
0.053 and 0.065 g — about 6.5% of static at worst. `ARCHITECTURE.md` §11's
"elevation change, both for looks and because it loads the tires" is wrong for a
kart circuit, and the arithmetic that shows it is one line long.

## 6. Two overtaking places, and neither claim is a guess

**T3 Il Pozzo — the primary one, and the only place braking is the mechanism.**

- **Tow** 159 m: 96.7 m of full throttle plus T2's 62.31 m taken flat at 129.0.
- **Braking** 43.0 m, 140.8 → 52.0 km/h on −4.60%.
- **Width** 14 m, tapered up from 11 over the last 75 m — in the geometry, as
  three segments, not in a note string.
- **Two lines, computed on the inner-half corridor.** The racing line is
  21.30 m. The defender's line is the largest arc that fits the inner half —
  radius 14.30 m about a centre at `R − W/4` — and its quarter-lock ceiling is
  **33.9 km/h against the corner's own 52.0**. Defending the inside of Il Pozzo
  costs 18.1 km/h and puts the kart 18 km/h *past* #137's boundary.
- **Exit** 161 m of 4.60% climb through five upshifts from 52 km/h.

That inner-half construction is the correction that matters. Sablière computed
its defensive penalties from the inside-*edge* radius, which made them 2× to 30×
too large and, at its own T1, made the defender's line **wider** than the
attacker's — the thesis inverted by an arithmetic slip.

**T1 Ronda — the second one, and it is a friction-ellipse pass.**

The braking zone is 2.5 m, and this document does not pretend otherwise. The
move is made by being alongside at turn-in and by what the ellipse does over
109.43 m of arc. Two 1.400 m karts fit inside 12 m with 1.0 m to each edge and
2.6 m between them. At a common 121.5 km/h — the defender's own ceiling on the
inner half —

| | line radius | lateral | longitudinal left, `sqrt(1-(g/1.86)²)` |
|---|---|---|---|
| attacker, full width | 76.56 m | 1.52 g | **0.579** |
| defender, inner half | 62.49 m | 1.86 g | **0.037** |

The attacker has fifteen times the drive available, for 2.93 s. And T1's exit
owns 127.72 m of shelf into T2, whose exit owns La Discesa into the only real
braking zone on the lap — so the two overtaking places are 400 m apart and they
are the same fight.

T8 Uscita is listed third: 8.0 m of braking, a 12 m entry, an 11.8 km/h
defensive penalty and 5.03 s of arc to hold it, feeding the 165 m start
straight.

## 7. Reversed, designed

44.885 s against 44.820 s — 65 milliseconds — and almost nothing else is the
same. The reverse profile was *run*, not asserted.

- **The two compounds trade jobs.** Vigna closes forward and opens reversed;
  Uscita opens forward and closes reversed. Each direction gets exactly one
  dive-bomb and one drive-away corner. That is Sablière's structural insight,
  and here it is measured: reversed, Uscita is entered at **142.2 km/h with a
  20.5 m braking zone**, against 124.3 km/h and 8.0 m forward.
- **Reverse spot 1 — T8 Uscita, the first corner.** 165 m of start straight,
  the reverse lap's top speed, 20.5 m of braking (19.7 m analytic) into a 12 m
  entry, and the corner *closes* 70 m → 45 m so a kart thrown at the open first
  half runs out of road in the second. Racing line 54.86 m, defender 46.28 m,
  100.1 km/h against 111.9. It does not exist forward.
- **Reverse spot 2 — T3 Il Pozzo off La Rampa.** 161.02 m of −4.60% descent to
  134.4 km/h, 38.5 m of braking, the same 14 m entry and the same hard-bounded
  line. The forward version has the deeper zone; the reverse version has the
  longer consequence — its exit is the 289.44 m descent band driven as a climb.
- **Sector marks translate, and that was measured rather than assumed.** The
  reverse layout's own equal-time marks are at reverse-station 473.0 and 862.0
  → 14.963 / 14.985 / 14.937 s, a 48 ms spread, and those stations are within
  0.1 m and 10.9 m of the mirrors of the forward marks. It holds *on this lap*
  because the time is distributed almost symmetrically about the hairpin. It is
  not a rule.
- **The pit lane does not reverse, and that is asphalt.** Forward, T8 and T1 are
  both lefts so the racing line runs down the right edge and the left is free;
  reversed they are both rights and the free edge is the right one. Worse, a
  22 deg branch is a 158 deg merge driven the other way, over article 7.4's
  30 deg cap, on either edge. The reverse layout gets **its own two stubs**.
  Sablière found this; nobody else checked it.
- **T4 loses its job** reversed — a downhill 85 deg left with a 4.5 m zone,
  testing nothing in particular. The reverse layout says so rather than
  inventing a replacement.

## 8. Sectors

| | station | time | mean | gear changes | what a delta means |
|---|---|---|---|---|---|
| S1 | 0 → 524.0 | **14.351 s** | 132.9 km/h | 4 | T1's exit, or the descent |
| S2 | 524.0 → 902.0 | **15.232 s** | 95.9 km/h | 9 | the gearbox: the hairpin or the ramp |
| S3 | 902.0 → 1375.13 | **15.237 s** | 112.1 km/h | 5 | the crossing, or Uscita's exit |

Marks are constrained to straights and then chosen to minimise the spread, which
is 0.886 s — 2.0% of the lap. **They are not equal and are not published as
equal.** Sablière reported all three of its sectors as exactly 14.306 s summing
to its claimed lap time to four decimal places, which is lap/3 dressed as a
measurement. The three times here sum to 44.820 s and the lap is 44.820 s
because both come from one integration of `final_speed.csv`.

Sector 1's mark sits 3.2 m before Il Pozzo's turn-in and 40 m into its braking
zone, so the whole hairpin falls in sector 2 and the gearbox sector is
unambiguous: nine gear changes against four and five.

## 9. What the verifiers caught, and what I did about it

**The dominant error — racing-line radii computed on the hug-the-inside-edge
path.** All four verify files. Fixed by deriving `rho` from first principles,
verifying it against 45 of the 49 published radii, and — critically — treating
compounds and esses explicitly rather than pretending a closed form covers them.
The two compounds are evaluated at the *tightest* arc with the *whole* direction
change (Valdirone's verifier's construction), and Le Forbici is capped by the
half-corridor because the crossing arithmetic says it must be.

**Defensive-line penalties computed from the inside-edge radius** (Sablière,
major; the error inverted its thesis). Fixed: every defender figure here is the
largest arc that fits the *inner half* of the corridor, radius computed about a
centre at `R − W/4` with usable width `W/2 − 1.400`. The penalties come out at
7.0 to 26.8 km/h, which is smaller than Sablière claimed and larger than nothing.

**Two elevation profiles that disagreed by 2.09 m** (Sablière, major). Fixed
structurally: every grade break is on a segment boundary, so the per-segment
grades and the curve schedule cannot drift. Both close to 0.000000 m and both
ranges are published (13.31 m tangent, 12.55 m driven) because the curves cut
the crest and fill the sag and a reader needs to know which is which.

**Reverse vertical curves checked with the wrong constant** (Valdirone, major)
**and one break never checked backwards at all** (Cava Vecchia, major, 13.21 m
of overlap). Fixed: same K both ways, reverse speed from a reverse profile that
was actually run, and the results published side by side. Curve 1 is tighter
forward, curve 3 is tighter reversed.

**Braking arithmetic wrong by 2.3× at the tightest constraint in the design**
(Cava Vecchia, major, `+1.5 m` of margin that was actually `−3.48 m`). Fixed by
measuring every braking zone off the integrated profile and cross-checking it
against the analytic grade-corrected figure: T3 43.0 m against 42.74 m, T2 9.5
against 9.15, T4 14.5 against 14.12, reversed T8 20.5 against 19.7.

**A pit lane peeling into the racing line of a left-hander** (Pietrarossa,
major) **and an exit lane with no room to merge** (Pietrarossa, major). Both
junctions here are on the edge the line does not use, and the reasoning is
stated in the direction that can be checked: T8b and T1 are both lefts, the line
exits and enters on the right, so the left edge is clear. The forward exit lane
rejoins 26.0 m before T1's turn-in and that is called out as the uncomfortable
part rather than defended.

**A 76.6% face between two live sections that the plan-view separation rule
passed silently** (Pietrarossa, major). Fixed by adding the test that does not
exist anywhere in the brief, `ARCHITECTURE.md` or `REFERENCES.md`: the slope
between two sections must not exceed article 7.5's own 10% cap on run-off
up-slope. This circuit's worst is **8.95%** — 2.21 m of height across 24.72 m of
clear ground, between La Discesa and the T4 approach — and it is reported in
`measured` because otherwise nobody would know to look.

**The 200 m straight cap met in the letter and defeated in effect** (Sablière,
major; Valdirone ran 354.5 m of continuous full throttle). Reported three ways
and not once flatteringly: longest geometric straight **165.00 m**; longest
uninterrupted full-throttle run **162.5 m**; longest run with no braking event
**318.7 m**, of which 156.21 m is T8's arc at 1.80 g. By the regulation judge's
own test — merge straights across any corner of centreline radius ≥ 80 m — the
figure is 165.00 m, because no corner here has a centreline radius above 70 m.
For comparison: Valdirone 354.5, Sablière 380, Pietrarossa 219.9, Cava Vecchia
196.9.

**Sector times that were `lap/3` dressed as measurements** (Sablière, minor but
telling). Fixed: 14.351 / 15.232 / 15.237, unequal, from one integration.

**Width tapers that existed only in prose so every check ran against constant
widths** (Sablière, minor). Fixed: eight width changes, each a segment boundary
with a `width_taper_in_m` field, and the corridor used for every line-radius and
separation figure is the tapered one.

**A corner sold as sitting on #137's boundary that was nowhere near it**
(Pietrarossa T2 and T6, major; Valdirone T4 mislabelled by 5.1 km/h). Fixed by
publishing both ceilings for every corner and letting the binding one define
`cliff_relation`. Six corners sit on the lock ceiling *by construction*, which is
honest; the "past the cliff" case is reserved for the **defender's** line, where
it belongs — Vigna's defensive line is 25.9 km/h past it, Il Pozzo's is 18.1.

**Claims about clearances that measured out at 8× the stated figure**
(Valdirone's invented 18.4 m, actually 152.6 m). Every distance quoted here is
computed from the walk.

## 10. What is still weak

The second forward overtaking place is a friction-ellipse pass into a 2.5 m
braking zone, and there is **no slipstream model anywhere in the project** — so
the tow that gets a kart alongside at T1 does not currently exist. That is the
same criticism the racing judge made of Valdirone and it is not fully fixed. It
is not a track problem.

Every corner speed rests on a lock sweep whose three quarter-lock rows differ in
throttle as well as radius, so radius, speed, load and throttle are confounded.
Six of eight corners are limited by that interpolation.

Le Forbici's crossing is 38.43 m and the link into Vigna is only 28.29 m, which
makes T4 and Vigna nearly a compound; the closure solve wanted those lengths and
they are the tightest links on the lap.

The lap time is a point-mass estimate with a friction ellipse and no gearbox.
44.82 s is a floor; expect 48–52 s from a real solver, and the sector balance
will move with it.


## 11. Receipt

Re-derived from `final.json` and the two CSVs alone, by a script that shares no
code with the one that built them.

| gate | measured | |
|---|---|---|
| closure error < 1.0 m | 0.007013 m (published lengths) | PASS |
| total length 1,100–1,400 m | 1,375.13 m | PASS |
| final heading, one loop | −360.000000° | PASS |
| longest straight ≤ 200 m | 165.00 m | PASS |
| start line → T1 ≥ 50 m | 88.00 m | PASS |
| last corner → line ≥ 70 m | 77.00 m | PASS |
| starting straight 120–200 m | 165.00 m | PASS |
| starting straight gradient ≤ 2% | +0.787% | PASS |
| camber on straights 1.5–3.0% | 2.0, 2.5% | PASS |
| banking in corners 0–10% | 3–8% | PASS |
| adverse camber | none | PASS |
| width ≥ 8 m | 9.0 m minimum | PASS |
| first corner ≥ 45°, 8–12 m | 110°, 12.0 m | PASS |
| min separation ≥ 14 m (>40 m apart) | 29.271 m | PASS |
| width-aware 6+(wa+wb)/2 slack | +9.271 m | PASS |
| slope between two sections ≤ 10% | 8.95% | PASS |
| elevation closes ≤ 0.05 m | 0.000000 m | PASS |
| every vertical curve legal, both directions | 1.48/1.62×, 2.96/2.96×, 2.05/1.81× | PASS |
| sector times sum to the lap | 44.820 = 44.820 s | PASS |
| no corner within 0.2 g of its rollover threshold | closest 0.57 g | PASS |

Site: 336.6 × 401.9 m = 13.53 ha, 101.6 m of lap per hectare — against Valdirone
94.9, Cava Vecchia 93.2, Sablière 104.0, Pietrarossa 151.7 (which is
unbuildable). 21.8 laps in a 30 km Final; capacity by L/28 is 49.1 karts, capped
at 36, against a grid of 8.
