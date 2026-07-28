# Pietrarossa

**Lens: every corner provokes one named sim feature — and it still has to be a circuit.**

1,300.019 m, ten corners, 49.44 s, 8.47 m of elevation. Closes to 1.04 mm and
turns exactly −360.000000°.

---

## 1. The argument

`scripts/track/track_layout.gd` already proved half of this. Its four corners are
each sized from a row of `drive_probe.gd`'s lock sweep, each exists to provoke one
named behavior, and its header says outright that it is not a racing circuit. That
is the method. What it never had to answer is whether the method survives being
made to entertain someone for twenty-five laps.

The interesting discovery is that it survives *because* of the kart, not despite
it. Three measurements do almost all the design work:

- The best sustained lateral g lives at about **55 m** of racing-line radius, not
  20 m. A 19 m circle is held at 0.88 g.
- **A quarter of steering lock buys 32.64 m at 81.3 km/h**, and past a quarter
  lock the kart falls off a cliff — 1.02 g and 21.2 km/h at 0.60 lock, under full
  throttle. Issue #137, unresolved.
- **1.53 g of braking** means every braking zone on the circuit is 20–50 m.

Take those three seriously and the corner vocabulary is not a designer's choice,
it is a *ladder*. Pietrarossa's ten corners sit on eleven rungs of racing-line
radius — 114.3, 71.3, 54.8, 54.3, 42.8, 37.3, 34.3, 28.8, 25.8, 23.3, 20.3 m —
with no two rungs close enough that a driver would confuse them, and one named sim
question hung on each. The speeds come out at 143.9(limited to 126.9 in practice),
129.8, 112.9, 112.4, 97.5, 89.1, 84.0, 71.7, 64.3, 57.7, 49.6 km/h. That spread is
the whole design and it is why no corner is a wasted lap.

The third measurement has a consequence the brief states and I want to underline,
because it is the thing that stops this being a rig: **a KZ circuit has exactly two
real braking zones per lap.** Everything else is a momentum corner. So the two
overtaking places are not "a long straight into a heavy braking zone" — that
mechanism does not exist here. They are a 12 m-wide *decreasing-radius* first
corner where the defensive line is the slow line, and a 14 m-wide hairpin whose
exit is 126 m of uphill.

And one structural decision: the lap turns **net counter-clockwise**. Reversed,
every corner changes hand. On a kart that tips at 2.43 g left and 2.81 g right,
that is a physics change, not a cosmetic one — Il Curvone becomes a 150° right and
issue #32's inside-rear lift gets tested against a 15.6% larger margin at the same
1.84 g. Two geometries, one spline, and the second one is genuinely a different
experiment.

---

## 2. The lap, corner by corner, as a driver meets it

**Rettifilo Pietrarossa, 178 m.** The grid is on the last 24 m of it, climbing at
+1.6%. You cross the line at about 127 km/h, the road crests 36 m later and falls
away, and you see T1 from the top of the rise. 135.9 km/h at the board.

**T1 Curva del Banco — 66 m into 32 m, 112° left, 12 m wide.**
*Provokes: load transfer under combined braking and turning.*
Twenty-six metres of braking, and it runs out exactly at turn-in. Then the radius
halves under you. Every other corner on this circuit lets you finish the brake
before you commit; this one does not, and it is the first corner of the lap so you
meet it cold with a cold set of tires. 12 m of width because the regulation caps a
first corner at 12 and says "open as much as possible" — so it is 12, not 10.

**Allungo del Banco, 36.7 m,** then

**T2 Il Quarto — 30 m, 58° right.**
*Provokes: #137, the quarter-lock cliff, sat exactly on the boundary.*
The name is the measurement. Racing-line radius 34.3 m at 84.0 km/h against the
sweep's 32.64 m at 81.3 km/h holding 1.59 g: 1.7 m and 2.7 km/h of margin. Take it
right and it is the most satisfying corner on the lap. Arrive ten hot and you add
lock, and past a quarter of lock this kart scrubs to a walk. It is a **right** on
purpose — if it were a left, the 2.43 g rollover margin would be a second variable
in an observation that is supposed to be about steering lock alone.

**Discesa della Cava, 43.3 m in two gradients,** dropping off the bank at −3.2%
then easing to −0.8% for the compression. Then

**T3 Il Curvone — 50 m, 150° left, 11 m wide.**
*Provokes: #32, the inside rear lifting and staying lifted.*
This is the corner the circuit is built around. 130.9 m of constant radius at
112.9 km/h — **4.17 seconds** at the kart's own best sustained lateral g. The
sweep's two full-throttle rows settle at 58.49 m / 1.84 g and 50.95 m / 1.82 g,
and this corner's racing line is 54.8 m, between them. Nothing else on the lap
holds one steady state long enough for "visibly lifts" to be a judgement rather
than a guess; the next longest is the hairpin at 3.19 s and a third the speed. The
gradient is −0.8% all the way through and does not change once, which is the
regulation's own instruction for curved sections and also just correct: a bump
mid-corner would contaminate the reading.

**Ingresso delle Esse, 20 m,** and then the flick:

**T4 Le Esse — 50 m right, 16 m, 50 m left. 112.4 km/h both ways.**
*Provokes: the rollover asymmetry between a left and a right.*
Same radius, same racing line, same speed, same 1.82 g, opposite hands, 0.9 s
apart. Twenty-seven kilograms of engine sits 41 mm right of the centerline and the
kart tips at 2.43 g left against 2.81 g right. That is a 15.6% difference in margin
and no driver can detect it across half a lap. Back to back is the only way. The
kerbs are identical both sides, because the whole thing is a comparison.

**Uscita delle Esse, 22 m** — and this is where the lap changes character. You go
from 112 km/h to 64 in 22 metres.

**T5 Il Frantoio — 21 m, 122° left, 11 m wide.**
*Provokes: #40, the difference between assists on and off.*
64.3 km/h held for 2.51 s, which on this gearbox is exactly the 2nd/3rd boundary,
and the exit is onto a −3.6% descent. With auto-shift on, the box picks by rpm
alone and hunts across the apex. With auto-clutch on, the bog that a wrong choice
produces is hidden from you. Turn both off and the corner has one right answer —
hold second, let the engine braking set the entry — and several wrong ones that
cost you the whole descent. The racing line here is 25.8 m, inside the quarter-lock
boundary, so you are driving in the scrub; that is deliberate and it is not a
repeat of T2. T2 asks whether you can stay on the right side of the cliff. T5 asks
what the gearbox does once you are past it and have to drive out.

**Discesa del Frantoio, 81.2 m at −3.6%** — the steepest thing on the circuit, and
it is here rather than on a straight because at 65–95 km/h a 286 m vertical radius
is legal where the same change at the crest would need 1,113 m.

**T6 La Vite — 19 m, 81° right.**
*Provokes: caster jacking, isolated.*
The lowest lateral g of any real corner on the lap — about 1.02 g — at close to the
highest steering angle. That combination is the only way to separate the jacking
term from load transfer, because at T2 the same quarter of lock is applied at
1.59 g and anything the chassis does there could be either. **T2 and T6 are one
instrument with two readings, and T6 is the control.** It is also the corner where
the run-off requirement turns on by one degree: 81° against the regulation's 80°
threshold.

**Il Fondovalle, 70 m, dead flat,** the bottom of the circuit, 7.9 m below the
start line.

**T7 La Staccatina — 24.5 m, 83° right.**
*Provokes: #39, engine braking shaping entry.*
You arrive at 102.9 km/h and the corner wants 71.7. At 1.53 g that is **14.0 m** of
braking — under two kart lengths — so in practice you set the entry by lifting in
second and the brake is a correction, not a tool. The name is the joke: the little
brake. It is the only corner here whose entry you can get wrong without ever
touching the pedal, which is the whole of what #39 asks.

**Rettifilo del Fondo (a), 102.7 m,** climbing at 0.689% — almost nothing, and
deliberately so: this is the fastest part of the circuit and the regulation says
changes of gradient there should be avoided altogether. There are none, through the
kink or the braking zone.

**T8 Il Ciglio — 110 m, 34° right, taken flat.**
*Provokes: #139, curb strikes at speed.*
1.63 seconds at 1.11 g, entering at 120.7 km/h and leaving at 132.2, with the
throttle open the whole way. There is a 30 mm vertical-faced kerb on the inside and
you cannot take this corner without using it. That is #139's open half — "a wheel
driven at it at 100+ km/h does not pass through it" — happening once a lap, and in
a 30 km Final, twenty-five times, which is also the repeated-crossing cost
measurement the issue still owes. It has a second job: its 34° breaks a 220 m run
into two straights of 102.7 m and 51.9 m, so the tow is legal under the 200 m cap.

**Rettifilo del Fondo (b), 51.9 m.** Peak 133.4 km/h. Brake.

**T9 Il Tornante — 14 m, 180° left, 14 m wide.**
*Provokes: #38, clutch modulation off a slow exit.*
The slowest point on the circuit, 49.6 km/h, 3.19 s of it, and the biggest braking
event on the lap: 39.4 m needed in the 51.9 m available. Twelve and a half metres
of margin, so late braking is on and a lock-up puts you in the gravel that sits on
the axis of the corner because 180° makes run-off there mandatory. The exit is
**126.2 m of +2.8% climb** — this is the one place on the lap where the kart is low
enough in the rev range that the clutch is a control rather than a launch device,
and the only place where a bogged exit is paid for immediately and then again for
six more seconds up the hill.

**Salita del Tornante, 126.2 m,** climbing.

**T10 La Cesta — 38 m, 52° left, uphill.**
*Provokes: threshold braking on a surface transition.*
The resurfacing joint crosses the road at 1,179.5 m — eight metres before turn-in.
Everything before it is new asphalt; everything from there through the entry and
the apex is twenty seasons older and polished by karts. 11.9 m of braking from
118.9 to 97.5 km/h, uphill, with the grip step arriving *under maximum pedal* and
then staying under the kart while it turns in. It is a short zone because every
kart braking zone is short, and I would rather say that plainly than pretend
otherwise. What makes it a test is not its length but that the step arrives after
the brake is already at threshold. (Article 12 forbids paint on the asphalt. A
resurfacing joint is not paint.)

**Uscita della Cesta, 78 m,** back over the line at 123 km/h.

---

## 3. What each corner shows that no other one does

| Corner | Racing-line R | Speed | The one thing | Why nothing else shows it |
|---|---|---|---|---|
| T1 | 71.3 → 37.3 m | 135.9 → 89.1 | combined braking + turning | the only decreasing radius on the lap |
| T2 | 34.3 m | 84.0 | #137 on the boundary | 1.7 m outside the measured quarter-lock row |
| T3 | 54.8 m | 112.9 | #32 inside-rear lift | 4.17 s of steady state; next longest is 3.19 s at a third the speed |
| T4 | 54.3 m ×2 | 112.4 | rollover asymmetry | the only back-to-back matched pair |
| T5 | 25.8 m | 64.3 | #40 assists on/off | the only corner sitting on a shift boundary long enough to argue with |
| T6 | 23.3 m | 57.7 | caster jacking | lowest g at highest lock — the control against T2 |
| T7 | 28.8 m | 71.7 | #39 engine braking | 14.0 m of braking needed: entry is a lift, not a pedal |
| T8 | 114.3 m | 126.9 | #139 curb at speed | the only kerb a driver meets above 120 km/h |
| T9 | 20.3 m | 49.6 | #38 clutch modulation | the only corner slow enough for the clutch to be a control |
| T10 | 42.8 m | 97.5 | threshold braking on a surface change | the only surface transition inside a braking zone |

---

## 4. Elevation, and why it is where it is

One hill, 8.47 m of range, and every metre of the drama is in a slow section —
which is not taste, it is arithmetic. A 2% gradient change at the crest needs a
1,113 m vertical radius and 22.3 m of road to lay it on. The same 2.4% change at
65 km/h needs 286 m and 6.9 m. So the descent off the bank is on the 20 m link
after T2 and the 81 m link after T5, and the fast parts of the circuit —
Il Curvone, Le Esse, the whole back straight through the kink and into the hairpin
braking zone — have **no gradient change at all**.

All thirteen vertical curves fit in the segments that have to hold them, with
margin. The tightest is at the start-line crest: 22.3 m of curve on a 36 m + 64 m
straight, ending 47 m before T1's braking point.

The profile is scaled by a single factor of 0.80, and that number is not aesthetic.
The closest pair of track sections on this layout is 20.56 m apart in plan. At the
unscaled profile they were **9.83 m apart vertically**, with 12.1 m of clear ground
between the asphalt edges — which is not a run-off, it is a cliff with a road on
top. 0.80 brings it to 8.11 m over 10.6 m of clear ground. That is still a terrace
and still needs a retaining wall and barriers on both sides; it is the circuit's
biggest construction item and I have said so in `risks`.

**This is the finding I would most want carried back into the repository.** The
regulations cap gradient *change* (vertical radius) and run-off *slope*. Neither
prevents two bits of track twenty metres apart in plan from being ten metres apart
vertically. It is the binding constraint on any layout with real elevation, it is
what actually shaped this design, and it is written down nowhere in
`ARCHITECTURE.md`, `REFERENCES.md` or the brief.

---

## 5. Reversed

49.29 s against 49.44 s — within a tenth, and almost nothing else is the same.

The headline is the hands. Il Curvone becomes a 150° **right** held for 4.17 s, so
#32's inside-rear lift is tested against 2.81 g instead of 2.43 g: the same corner,
the same speed, the same lateral g, a 15.6% larger margin. That is the cleanest A/B
these two layouts can offer and it costs one authored layout.

T1 reverses from decreasing to increasing radius — entered at 101.7 km/h into the
32 m half and released into the 66 m half onto the main straight. Forward it is the
hardest corner on the circuit; reversed it is the best exit. Le Esse are approached
from the slow side at 85.1 km/h instead of the fast side at 118.2, so they stop
being a flick and become an acceleration zone. And the Salita del Tornante becomes
a 126 m descent at −2.8% into the hairpin, which is the best braking zone on the
circuit in either direction: gravity working for the kart and against the brakes.

The count changes too, and this surprised me. Forward there are **two** braking
zones over 20 m. Reversed there are **six** (24.9, 35.8, 32.7, 20.0, 17.5,
18.8 m). Reversed is the busier, more mistake-prone lap; forward is the one that
rewards a clean Curvone. That is two circuits, not one circuit and its mirror.

Reverse overtaking: into the hairpin off the reversed Salita (126.2 m, peak
128.1 km/h, 35.8 m of downhill braking), and into T7 La Staccatina off the reversed
Rettifilo del Fondo (102.7 m, peak 133.6 km/h, 32.7 m of braking) — a spot that
**does not exist forward**, where the same corner needs 14.0 m and is taken on a
lift.

What breaks is in the JSON. The short version: all eleven kerb spans are on the
wrong side, T7's run-off is under-built for the reverse layout's 133.6 km/h
approach while T8's is over-built for its 96.2, and T10's resurfacing joint moves
from eight metres before turn-in to eight metres after the exit — so the reverse
layout has no surface-transition braking test at all and has to source one from the
hairpin zone. That last one means the two layouts genuinely disagree about where
the asphalt changes, which is a case ADR-0046's surface spans have to support
per-layout and, as the ADR is written, may not.

One claim I expected to make and could not: I assumed the sector marks would land
mid-corner reversed. I measured it, and they don't — 815 m and 429 m reversed are
both on straights and give 16.26 / 16.80 / 16.23 s, which is usable. The reverse
lap wants its own marks at 434 m and 808 m for 16.40 / 16.42 / 16.47, and the real
reason is subtler: the mirrored mark puts both halves of the asymmetry test in the
same sector where forward it splits them. Still an authored-layout problem, just
not the one I was going to assert.

---

## 6. If you are about to drive it for the first time

- **Do not trust T2.** It is 1.7 m of radius and 2.7 km/h away from the corner that
  ends your lap. Ten km/h hot and you will be adding lock and wondering why the
  kart stopped.
- **Il Curvone is a commitment, not a corner.** Four seconds at one radius. Get the
  entry wrong and you have four seconds to think about it. This is the corner where
  you should be watching the inside rear.
- **Le Esse are one input.** Right then left, 0.9 s apart. If you treat them as two
  corners you will be late for the second one and you will miss the thing they
  exist to show you.
- **The infield is second gear and it is a decision.** T5 sits on the 2nd/3rd
  boundary. Drive it once with the assists on and once with them off; that is the
  entire content of issue #40 and it is 2.5 seconds long.
- **The hairpin exit is the lap.** 126 metres of uphill. Bog it and you lose the
  climb, La Cesta and the run to the line.
- **The tow into the hairpin is real.** 220 m with a flat-out kink in the middle.
  Sit behind someone through Il Ciglio and you will be alongside at the board.
- **Both overtaking places are made of width and exit, not of braking.** Twenty-six
  metres into T1 and thirty-nine into the hairpin is all you get. If you are
  planning to out-brake someone from fifty metres back, you have the wrong vehicle.

---

## 7. Files

- `instrument.json` — the layout, the measured numbers, and every claim above in
  machine-readable form.
- Working files, kept in a private subdirectory because four agents share this
  scratchpad: `instrument_work/final.py` (frozen layout + the brief's walk verbatim),
  `instrument_work/check.py` (vertical curves, closest pairs with height difference,
  sector splits), `instrument_work/opt3.py` (the layout solve),
  `instrument_work/gen.py` (emits the JSON).
