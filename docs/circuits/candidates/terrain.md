# Cava Vecchia

**Lens: the land generates the circuit.** 1,340.97 m, seven corners, 11.42 m of
elevation, Grade 1. Verified numbers are in `terrain.json`; the walk and the
speed model are in `terrain_final.py` in this directory.

---

## 1. The site, and why it is a quarry and not a hill

A hillside is a bad site for a kart circuit and it took working the numbers to
see why. On an open slope every metre of height you gain has to be paid back
somewhere, the gradient is continuous, and the regulation's vertical-radius rule
then taxes you at every point where the slope changes — which on a natural
hillside is everywhere. You end up with a circuit that is mildly tilted
throughout and dramatic nowhere.

A quarry is the opposite, and it is the opposite in exactly the way the rule
rewards. A quarry is made of **flat benches connected by ramps**. The benches are
flat because a bench is a working platform — you cannot stand an excavator on a
slope — so they come with 1–2% of drainage fall and nothing else. The ramps are
straight and at a constant, chosen gradient, because a haul road is designed for
a loaded truck. And the gradient changes happen only at four places: the top and
the bottom of each ramp.

That is the whole shape of `R = V²/K`. A constant gradient costs nothing at any
speed; only the *changes* cost, and they cost `R·Δ` metres of road. So the ideal
form for a kart circuit with real elevation is: long flat sections at any speed
you like, connected by constant-gradient ramps at any speed you like, with the
four gradient changes placed at the four slowest points you can arrange. A quarry
hands you that geometry for free, because it was built by someone solving a
related problem — how to get 40 tonnes of stone up a hill without cooking the
brakes.

**Cava Vecchia** is a worked-out limestone quarry cut west into a hillside. Two
made-up levels survive:

- **The pan** — the old working floor, roughly 300 × 250 m of flat irregular
  platform, open to the east where the quarry was entered. Datum −10.8 to 0 m.
- **The bench** — a terrace along the western rim, 11.4 m higher, which carried
  the plant yard, the weighbridge and the stockpile apron.

And two ramps, which are not the same ramp and never were:

- **La Discesa** — the loaded haul road. Leaves the bench at its northern nose,
  breaks over the face crest at 3% for thirty metres, then falls at **8%** down
  the face to the pan. Loaded trucks came down it.
- **La Rampa** — the empty-return road. Shorter in plan, **7.81%** the other way,
  and it switchbacks at the top because a return ramp always does.

Everything else in this document follows from those two ramps.

---

## 2. Two results that shaped every decision

Both fall out of `R = V²/K` and both are worth stating because they are not
obvious and I have not seen them written down in the repo.

### 2.1 The regulation's minimum radius is a speed-independent cap on vertical g

Vertical acceleration through a curve of radius `R` at speed `v` is `v²/R`. The
regulation sets `R = V²/K` with `V` in km/h, so `R = (3.6v)²/K` and

```
a = v² / R = v² · K / (12.96 v²) = K / 12.96
```

The `v²` cancels. **At the legal minimum radius the vertical acceleration is
1.543 m/s² (0.157 g) concave and 1.157 m/s² (0.118 g) convex, at any speed
whatsoever.**

So the brief's instruction to "say how much extra normal load, roughly, from
V²/R" has an exact answer that does not depend on where you put the compression:
**+15.7% of normal load, +27.5 kgf on a 175 kg kart.** No legal kart circuit
anywhere can compress harder than that or lift lighter than −11.8%. What *does*
vary with speed is the **length** the transition occupies, and that is a plan-
geometry problem, not a feel problem.

Both compressions on this circuit are built at the minimum radius, and therefore
at exactly 0.157 g. There was no reason not to: the cap is the maximum, and going
above the minimum radius only makes the compression weaker and longer.

### 2.2 A minimum-radius crest is always about two seconds blind

For a crest where the sight distance exceeds the vertical curve length — which it
does for every realistic gradient change — the sight distance is

```
S = L/2 + C/(2Δ)     with  L = R·Δ = (V²/K)·Δ
C = (√(2·h_eye) + √(2·h_target))²  = 3.710 m for a 0.95 m eye and a 0.15 m target
```

and the sight *time* is `t = 3.6·V·Δ/(2K) + 3.6·C/(2·Δ·V)`. Minimising over Δ
gives `t_min = 2·3.6·√(C/4K)` — **independent of V**. For K = 15 that is
**1.79 s**.

Cava Vecchia's two crests measure **1.86 s** (Il Salto) and **1.97 s** (Il
Ciglione), within 10% of the theoretical floor, at 114 km/h and 56 km/h
respectively. A crest built to the regulation minimum is inherently about two
seconds blind, and it feels the same however fast you are going over it. That is
why this circuit's slow blind moment and its fast blind moment read as the same
kind of event.

*(A numerical sight check on the un-rounded piecewise profile — a sharp grade
break with no vertical curve at all — gives 38 m and 24 m rather than 58.7 m and
30.8 m. The built curve rounds the break, so the honest bracket is 38–59 m at Il
Salto and 24–31 m at Il Ciglione. I report both because the difference is the
vertical curve doing its job.)*

---

## 3. The consequence: where the elevation is allowed to be

Working `R·Δ` for an 8% gradient change at each of this kart's characteristic
speeds:

| Speed at the break | Convex, `L = (V²/15)·0.08` | Concave, `L = (V²/20)·0.08` |
|---|---|---|
| 143.9 km/h | 110 m | 83 m |
| 125 km/h | 83 m | 63 m |
| 100 km/h | 53 m | 40 m |
| 66.7 km/h | 24 m | 18 m |
| 56.4 km/h | 17 m | 13 m |

The 8% face and the 7.81% ramp both change gradient twice. Four gradient changes
of roughly 8%, and the road available at each of them decides where the corners
go:

- **Top of the face (Il Salto)** — 43 m of transition available inside a 31 m
  lead-in band, so the break has to be taken at about 114 km/h. That means the
  corner before it must be **slow**. → **T2 Il Ciglio, 67 km/h.**
- **Bottom of the face (the compression)** — 73 m of transition, taken at
  135 km/h, which is affordable only because it sits on a 124 m run of straight
  with no corner in it. → **La Conca, and T3 turns in 39 m later.**
- **Foot of the ramp (the second compression)** — 58 m of transition at
  118 km/h, needing 29 m of clear straight before the break. → **Piede della
  Rampa is 62.17 m long for exactly this reason and no other.**
- **Top of the ramp (Il Ciglione)** — 18 m of transition, which only fits at
  56 km/h. At 118 km/h the same break would need 62 m and there are 28 m of
  corner arc. → **the switchback is at the top of the ramp because that is the
  only place on the circuit the ground is allowed to do this.**

And the corollary: **the fast half of the lap is dead flat.** T3, the pan
straight, T4, Il Piazzale and T5 are all at 0.00% longitudinal, not because flat
was wanted but because at 125–138 km/h nothing else fits in the space. They are
drained by 2.0% cross-fall alone, which is what the 1.5–3.0% rule is actually
for.

---

## 4. A lap, as a driver

You are on the bench, in sixth, doing about 133 km/h at the line, with the pit
wall on your right and the toe of the old highwall on your left.

**T1 Il Traverso** — 55 m radius, 45° right, 12 m wide, 119 km/h. The first
corner is 45° because that is what makes it *the first corner* under art 7.6, and
12 m because that is the article's maximum. It is a positioning corner, not a
passing one: a small lift, a lot of road, and eight karts on lap one can go
through it three abreast without anyone dying, which is the entire point of the
rule. The bench is at −0.70% throughout and nothing changes gradient here,
because a grade break 113 m after a standing start is a break in an acceleration
section.

**Il Banco** — 154 m of bench, back to 137 km/h. On your right, over the pit
wall, you can see the whole pan 11 m below and most of the circuit on it. This is
the best view on the site and it is free: the bench is above everything.

**T2 Il Ciglio** — 22 m radius, 45° right, 11 m wide, **137 → 67 km/h in 37 m**.
The biggest speed change on the lap. The design line radius is 26.8 m, which puts
it right on the quarter-lock boundary — #137's edge. Arrive 10 km/h hot, add
lock, find the scrub, and you are scrubbing on the rim of an 11 m rock face. It
is a real passing place and it is the one where a race gets thrown away rather
than won, because of what happens next.

**Il Salto** — the road tips. −0.70% to −3.00% at 100 km/h, then **−3.00% to
−8.00% at 114 km/h**, and at the second break the road ahead disappears: 38–59 m
of sight, 1.86 s, and the kart goes 0.118 g light as it goes over. Turn-in for T3
is 82 m past the crest and you cannot see it. Then 82 m of 8% face at full
throttle, sixth gear, **135 km/h at the bottom**.

**La Conca** — the compression. 8% to flat, 73 m of vertical curve at the legal
minimum, **+0.157 g — +27.5 kgf**. The fastest point on the lap is also the most
loaded, and it arrives 39 m before you have to turn.

**T3 Il Ventaglio** — 62 m radius, 90° right, 125 km/h, 97 m of arc. A brush of
the brakes, 2.8 s of steady 1.85 g, and a kart that has just been squashed into
the road and is shedding that load as it turns. It is out in the middle of the
pan because it is the corner that needs the most run-off on the circuit and the
pan is the one side of the site with no rock face on it.

**Rettilineo del Fondo** — 157 m, flat, **138 km/h**. The pan floor, open ground
both sides, spectator banking on the east where the quarry was entered.

**T4 Il Frantoio** — 26 m radius, 85° right, **14 m wide**, 80 km/h. The
overtaking corner, and it is not made of braking distance: 33 m is all this kart
has from 138 km/h. It is made of width. Fourteen metres is six more than the
regulation floor and it exists to hold two lines. The extra width is also a
#137 mitigation — at the 8 m minimum the same centerline radius gives a 29.3 m
line and about 1.42 g, which is the wrong side of quarter lock; at 14 m it is
32.3 m and 1.59 g, which is the sweep's own 81.3 km/h row almost exactly.

**Il Piazzale into T5 La Pozza** — 64 m of apron, then a 65 m radius, 70° **left**
that you enter at 113 km/h and leave at 128. The load *builds* through it rather
than being set at turn-in. This is the #32 corner: the only sustained left in the
forward lap, 1.86 g against a 2.43 g rollover threshold — margin 1.31×, the
tightest on the circuit — held for 2.2 s, which is four times the HUD's 0.5 s
sustained-g gate. If the inside rear is going to lift anywhere, it lifts here.

The T4 → T5 pair is why the pass at T4 sticks. T4 is a right, T5 is a left, 64 m
apart. The defender who took the inside at T4 exits on the left of the apron,
which is the wrong side for a left-hander that is on the throttle for 79 m. It is
a switchback and it resolves rather than dangling.

**La Sponda, T6 Il Sasso** — 34 m radius, 75° right, 91 km/h, and the pan is now
falling at −0.50% towards its sump. The settling pond is behind the barrier on
your left. T6 is the least dramatic corner on the circuit and the one that most
decides the lap, because its exit runs 32 m and then hits the second compression.

**Piede della Rampa** — flat to **+7.81%** at 118 km/h, 58 m of vertical curve,
+0.157 g again. The kart is squashed into the road exactly as it starts to climb,
and whatever traction you find there sets your speed at the top of the ramp
132 m later, which sets whether the pass at T7 is available.

**La Rampa** — 132 m of 7.81% climb, third into fourth into fifth, **130 km/h at
the top**. And you cannot see the top: sight distance is 62 m at 1190 m, 44 m at
1210 m. You brake uphill, which is 1.61 g effective against 1.53 g flat, so the
zone is 33 m and the braking point is **later than it looks**. A driver who uses
his flat-ground reference brakes early and hands over the inside.

**T7 Il Ciglione** — 18 m radius, 90° right in two halves, 56 km/h, 31 m of
sight at turn-in. The grade break sits at the junction of the two halves:
**+7.81% to −0.70%, 8.51%, on the 211.7 m radius the rule allows at 56 km/h.**
The kart goes 0.118 g light — the legal maximum unloading, −20.7 kgf — at the
apex of the tightest corner on the lap, while turning at about 0.97 g on a locked
rear axle with no suspension. The design line radius is 22.8 m, which is **past**
quarter lock and into #137's scrub. That is deliberate and section 6 explains it.

Then it crests, the whole quarry opens out below you, and you fall down the start
straight at −0.70% for 188 m.

---

## 5. The two gradients, driven both ways

The only thing on this circuit that repeats is the gradient, and it repeats on
purpose. **8% down and 7.81% up, in the same lap, and they are two different
problems:**

|  | Il Salto, down | La Rampa, up |
|---|---|---|
| Entered at | 67 km/h out of a slow corner | 91 km/h out of a medium corner |
| Left at | 135 km/h | 130 km/h |
| Gear | 4th → 6th, throttle pinned | 3rd → 5th, throttle pinned |
| Ends in | a compression, +0.157 g, then a fast 90° | a blind crest, −0.118 g, then a hairpin |
| Braking | none — you are accelerating | 33 m uphill at **1.61 g** effective |
| Sight | 38–59 m over the brow at the top | 31–62 m through the whole braking zone |

Braking downhill into a slow corner with 1.53 g of threshold and no suspension
never happens in the forward lap — and that is the reverse layout's whole
character.

---

## 6. Why T7 is deliberately past the cliff

The test track already has the corner that walks straight into #137 and stops:
an 11 m hairpin at 21 km/h, built to fail legibly. Cava Vecchia does not need a
second one and would be a worse circuit for having it.

What it does instead is put **one** corner at 22.8 m of design line radius —
about 1.06 g, just past quarter lock, in the region where the kart begins to
scrub rather than turn — and then make that corner the hardest thing on the lap
for reasons that have nothing to do with #137: blind, uphill, at the end of the
longest tow, with the rear going light over a crest at the apex. A driver who
finds the last few degrees of lock that still pay gets the corner. A driver who
grabs for more finds the scrub, uphill, where he has no speed to recover with.

That makes #137 part of the racing rather than a defect you read about in a
ticket. It also couples the design to an unresolved problem, which is a risk I am
taking knowingly: if #137 is fixed by making the tire model behave past quarter
lock, T7 becomes an ordinary 56 km/h hairpin and the lap loses its most
distinctive corner. If it is fixed by reducing available lock, T7 may become
untakeable and the radius has to grow.

Every other corner on the circuit is on the safe side or squarely on the boundary
— T2 at 26.8 m and T4 at 32.3 m are both within a whisker of the sweep's
32.64 m / 1.59 g / 81.3 km/h row — so if the vehicle changes, six of the seven
move by a few km/h and one moves a lot.

---

## 7. Drainage, which is what the camber rule is for

A circuit on a flat field has no answer to "where does the water go". This one
does, and it changed two design decisions.

**The bench cross-falls 2.0% *into* the hill**, never over the crest of the face
— draining over a face crest is how you undercut it and lose the bench. The water
runs to a toe drain at the foot of the upper highwall, then along the bench's own
−0.70% along-fall to its northern end, and down a cut-off channel beside La
Discesa. That −0.70% is the bench's drainage grade, and it is also the start
straight's gradient, which had to be inside art 7.2's 2% cap. Both constraints
are satisfied by the same number, which is not a coincidence: a bench is graded
at 1–2% for the same reason a starting straight is capped at 2%.

**The pan straights are longitudinally flat and drained purely by 2.0%
cross-fall.** That is the 1.5–3.0% rule doing exactly its job, and it is why no
straight on this circuit is flat in section even where it is flat in profile.

**Everything on the pan ends up at the foot of La Rampa**, which is the lowest
point on the lap at −10.80 m and is the quarry's own sump. The settling pond
behind the barrier on the outside of T6 is what gives T5 (La Pozza) and La Sponda
their names. The one part of the circuit that must never flood is the one part
that is 11 m above everything else, and that is where the grid is.

---

## 8. Why the start is on the bench and not on the pan

The pan is flatter, bigger and closer to the road. The bench won on three counts
and the first is regulation.

1. **Art 7.2 caps the starting straight at 2% gradient, and art 7.6 / Appendix 13
   want it 120–200 m long.** The bench is a made-up level terrace — that is what
   a bench *is* — and it holds a 188.26 m straight at −0.70% without any
   earthworks at all. The pan straight also qualifies (157.48 m at 0.00%) but the
   pan is where the water goes, and you do not put a paddock in a sump.
2. **The pit lane needs ground beside the track that the racing line does not
   use.** The bench's stockpile apron, on the outboard side at the face crest, is
   the widest made-up ground on the site. Both pit junctions are on the *right* of
   the road, which works because T7 and T1 are both right-handers: a kart exiting
   T7 tracks out to the **left**, and a kart entering T1 turns in from the
   **left**. Neither the deceleration lane nor the exit lane crosses the racing
   line, which art 7.4 requires and which is genuinely hard to arrange on a 10 m
   road.
3. **Spectators.** From the bench you can see the entire circuit except the bench
   itself. That is free from the landform and it is worth more than a flatter
   paddock.

Grid: eight slots, two staggered columns, 4.0 m pitch and 2.6 m lateral offset,
occupying 1326.0–1338.0 m. Lights 12.0 m ahead of row 1 at 3.0 m up, inside art
7.7's 10–15 m and 2.5–3.5 m. **The pitch and offset are invented** — REFERENCES.md
records that CIK-FIA Appendices 9, 10 and 15 could not be located, so grid
spacing is still unsourced and these are the least defensible numbers in the
whole document.

---

## 9. What the reverse layout taught me, which is the most useful thing here

**ADR-0046 makes reverse an authored layout because curbs, run-off and sector
marks do not reverse meaningfully. It does not mention elevation. Elevation is
the thing that actually breaks.**

`R = V²/K` is speed-dependent, and speed is direction-dependent. The crest at the
top of Il Salto is taken at 114 km/h forwards and **134 km/h backwards**, because
backwards you arrive at it having climbed at full throttle instead of having just
left a 67 km/h corner. Same break, same 7.30% of gradient change, but the
required radius goes from 864 m to 1,201 m and the transition from 43 m to 60 m.

The first two versions of this layout were **illegal in reverse** and legal
forwards. The first missed by 0.3 m on a half-length; the second put the
following break's transition 11.4 m inside the reverse braking zone for T2.
Neither is visible from the forward direction and neither can be fixed by
authoring, because it is geometry.

The fix went into the **built ribbon**, not the layout: Il Salto's 7.30% total
change was split into a 2.30% band and a 5.00% band with a 31 m lead-in between
them, and the bench approach was lengthened from 30 m to 42 m. That cost about
26 m of lap length. Both directions now pass all six vertical curves.

The lesson for `track.json` is concrete: **the vertical-radius validation has to
run once per authored layout, not once per ribbon**, and it has to run before the
geometry is committed rather than after. Section 11 of this document lists what
else the ADR's validation pass is missing.

### The reverse lap, briefly

Every corner changes hand — T1, T2, T3, T4, T6 and T7 become lefts, T5 becomes a
right. So the forward lap's single 2.43 g-threshold corner becomes six of them,
and the hardest sustained load moves to **T3-reversed**: a 90° left at 125 km/h
held for 2.8 s at 1.85 g, margin 1.31× against 1.52× the same corner forwards.
If you want to see the inside rear lift, drive it backwards.

The gradients swap jobs. La Rampa becomes a 132 m descent at −7.81% straight into
a compression, so you brake downhill at 1.45 g effective *while* the road adds
15.7% of load in the middle of the zone — the reverse layout's best passing
place, and it does not exist forwards because forwards it is a climb. Il Salto
becomes an 82 m climb at +8.00% entered at 132 km/h, cresting at 134, and you
then brake **over the top of a hill on the rim of an 11 m face** for a 67 km/h
left. The forward lap has no equivalent moment; the forward lap's version of that
piece of road is the fastest thing on the circuit and nothing happens on it.

Reverse measures 1,340.97 m, closure 0.0157 m, heading −360.000°, 43.90 s.

---

## 10. What I would tell someone about to drive it for the first time

- **The circuit is 44 seconds of two flat halves joined by two hills.** Do not
  look for elevation in the fast bits; there isn't any, and there can't be.
- **Brake later than you think at the top of the ramp.** You are going uphill,
  which is 5% more retardation than the flat reference your right foot has
  learned. The corner is also invisible until you are in it. Both errors point
  the same way: everyone brakes too early at T7 on their first ten laps.
- **T2 is a trap dressed as an overtaking spot.** It is a real move and it is the
  easiest one to attempt. It is also the corner whose exit is punished for the
  next 240 m, over a crest you cannot see past and down a face at 8%. Get it
  wrong and you have thrown away sector 2 before it starts.
- **The compression at the bottom of Il Salto is worth 15% more grip and it lasts
  about a second.** Use it. It arrives 39 m before you turn into a 90° corner at
  125 km/h, which is the only place on the lap where you have more grip than you
  expect.
- **T5 will lift the inside rear.** It is the only sustained left, the load
  builds through it rather than at turn-in, and the margin to the rollover
  threshold is 1.31×. That is the corner the vehicle model is being judged on.
- **Above quarter lock this kart stops turning and starts scrubbing** (#137), and
  exactly one corner asks you to go there: T7. Find the least lock that makes the
  apex. More is not more.
- **Drive it backwards on day two.** It is a genuinely different circuit, not the
  same one mirrored, and six of its seven corners load the 2.43 g side instead of
  the 2.81 g one.

---

## 11. Things I think are wrong outside my own scope

Full detail is in `terrain.json` under `outside_scope_concerns`; the four that
matter most:

1. **`ADR-0046`'s spline schema cannot express the constraint that now binds the
   elevation.** A list of per-control-point elevations cannot say "this grade
   break is laid on a 916.8 m vertical radius" — the radius becomes whatever the
   interpolation happens to produce. REFERENCES.md itself quotes art 11 saying
   the longitudinal profile may be given as *"either vertical circular curves or
   a series of centreline levels at intervals of not less than 10 metres"*: the
   regulation offers both representations and the ADR picked the one that cannot
   express the rule. 10 m sampling also cannot resolve this circuit's shortest
   vertical curve, which is 18.0 m — two samples across the whole thing.

2. **`ADR-0046`'s validation list is now incomplete.** Since #157 landed, four
   more items are regulation numbers with hard values and none is listed:
   longest straight ≤ 200 m (**merged across collinear control points** — two
   straight sections either side of a zero-curvature point are one straight for
   this rule, which is the subtle part), starting straight 120–200 m, start line
   to first corner ≥ 50 m where "corner" means ≥ 45° of direction change, and
   `R = V²/K` at every gradient change **in both layout directions**.

3. **`ARCHITECTURE.md` §11 still contradicts the measured kart.** It defines a
   genuine overtaking spot as *"a long straight into a heavy braking zone"*. This
   kart's longest possible braking zone is **44.0 m**, and that figure is in the
   same repository. My brief knows this and says so in Part 2; §11 has not been
   updated, and the two documents now disagree about what an overtaking spot is.

4. **`track_layout.gd`'s racing-line rule is right for its case and would be
   badly wrong generalised.** *"With the 8 m road width the widest line through
   an 11 m centerline radius is about 15 m"* — i.e. `R + W/2` — is exactly correct
   for a 180° corner and wrong by a factor of five for a shallow one. The same
   construction on this circuit's T2 (22 m, 45°, 11 m wide) gives 26.8 m, while
   the arc actually tangent to the outer edge at entry and exit and touching the
   inner edge at the apex — `ρ = (Ro·sec(A/2) − Ri)/(sec(A/2) − 1)` — gives 143 m.
   The rule is not labelled 180°-specific, and what follows from it is how much
   lock a corner asks for, which is how you decide whether it is the safe side of
   #137. It also does not subtract the kart's own 1.40 m from the usable width.

Plus one arithmetic slip in each direction: **REFERENCES.md gives the 21.2 km/h
concave radius as 23 m where 21.2²/20 = 22.47 → 22 m** (the only wrong cell in
that table, and it is the row you read to size a compression at the cliff speed);
and **my brief gives the 81.3 km/h concave radius as 331 m where REFERENCES.md
correctly has 330 m**.

---

## 12. Verification, reproduced

Run `python3 terrain_final.py` in this directory. The walk is the brief's,
verbatim, at 0.25 m, on segment lengths rounded to 0.01 m.

```
total length      1340.97 m
closure error     0.0170 m          (this is the 0.01 m rounding, nothing else)
final heading     360.0000 deg
longest straight  196.92 m          (Il Salto, begins 327.5 m; cap is 200)
min separation    33.93 m           (1231.6 m vs 1271.8 m; requirement is 14)
elevation closure -0.000037 m
elevation range   11.420 m          (min -10.80, max +0.62, datum = start line)
bounding box      399.4 x 360.4 m
estimated lap     44.01 s           optimistic floor; expect 47-49 s driven
```

All six vertical curves pass forwards; all six pass in reverse. Straight runs,
merged across collinear segments: 196.92 / 194.34 / 188.26 / 157.48 / 153.87 /
63.88 / 37.59 m. Start line → first corner 113.13 m (≥ 50). Last corner → start
line 75.13 m (≥ 70). Starting straight 188.26 m (120–200) at −0.70% (≤ 2%).
