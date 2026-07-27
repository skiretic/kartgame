# Game design

Companion to `ARCHITECTURE.md`, which is the technical design, and `ROADMAP.md`,
which is the plan. This file answers the question neither of them asks: **what
does a person do with this, and why do they open it a second time.**

Status: design. Nothing here is built. Sections marked **SOURCING** hold figures
that are being pulled from the FIA Karting regulations and are placeholders until
they are; nothing built on them until they land, per `ARCHITECTURE.md` §5 item 10.

`ARCHITECTURE.md`'s north star is unchanged and this file sits under it: *the kart
drives well and reads as real at speed.* Everything below exists to give that
driving somewhere to happen, and none of it outranks it.

---

## 1. The gap this closes

`ROADMAP.md`'s tech demo definition is a list of eleven capabilities. A person
appears in none of them. Read it back as a session: the game has no main scene —
`project.godot` sets none, and a scene path is typed on a command line — no menu,
no pause, no results, and no reason to drive lap 40 that was not also true of lap
2.

The sim is the asset. The loop is the gap.

---

## 2. The session is the atom

One structure, and the three modes are arrangements of it.

```
Session   = track + layout + session type + rule set + entry list
Round     = ordered list of sessions at one venue        (a race weekend)
Season    = ordered list of rounds + a points table      (a championship)
Career    = ordered list of seasons, one per class
```

A session is the only thing the game actually runs. Everything above it is data
and a results ledger. Time trial is one session with a ghost and no field; a
championship is a list of lists. There is no second code path for "race mode".

**This is the reason the front end is being designed before M4 rather than
arriving with M6.** `ROADMAP.md` M6 currently reads "race states: grid, countdown,
racing, finished, results" — a state machine for one mode. Written that way it
will absorb one mode's assumptions (there is a field; there is a grid; a lap can
be invalidated) and a championship becomes a retrofit against it. Written as a
session runner it costs the same and the other two modes are configuration.

Session types, all four running the same runner:

| Type | Field | Grid from | Produces |
|---|---|---|---|
| Practice | none | — | best lap, ghost |
| Qualifying | none, or field on track | — | a lap time, which orders the next grid |
| Heat | full | qualifying result | a finishing order |
| Final | full | heat result | a finishing order, and points |

Practice **is** time trial. It is reachable straight from the menu with a track
picked, and it is also the first session of a weekend. One session type, two doors.

---

## 3. Modes

**Practice / Time Trial.** One track, one layout, no field. Your own ghost, sector
deltas, lap validation. The tuning overlay is live here and this is where a preset
gets made — ADR-0037's audit trail is a game mechanic in this mode, not just a
debug aid. It is also the only mode that is fully playable before M7 exists,
which makes it the first mode built.

**Race weekend.** One venue, the session list in §4. Standalone from the menu,
and the unit a season is made of.

**Career.** Two classes, one season each. §5.

There is no free-roam and no "quick race against nobody". A session with no field
and no clock is the proving ground, which is a tool and stays one.

---

## 4. The weekend

The shape is **FIA Karting's**, not WSK's. Sourced in `REFERENCES.md` under
*Race format and sporting regulations*; the regulations are the 2026 General and
Specific Prescriptions.

The real thing, verbatim from the Specific Prescriptions Art. 18: *"Any FIA
Karting Championship Competition shall comprise Free Practice, Qualifying
Practice, Qualifying Heats, Super Heat(s) and a final phase."*

**The session between the heats and the Final is a Super Heat.** *Prefinal* is
WSK's word. The two series are not interchangeable — the FIA adds position points
and the highest total wins; WSK adds penalty points and the lowest total wins.

### What the real weekend costs in time

A session is specified as a **distance**, so the lap count changes per circuit:
Qualifying Practice is one 6-minute session, each Qualifying Heat 15 km, the Super
Heat 20 km, the Final 30 km for Seniors. On a 1,200 m circuit at kart pace that is
roughly 12, 17 and 25 laps — **about an hour of track time per round**, before
practice. A four-round season would be most of a weekend, not most of an evening.

### The compression, stated as a number

Distances are scaled to **1/4**, and the three-heat structure is cut to one:

| Session | FIA | Here | Laps at 1,200 m |
|---|---|---|---|
| Practice | not fixed | open, until you leave | — |
| Qualifying Practice | 6 min | 3 min | ~3 timed |
| Qualifying Heat | 15 km, and there are three | 3.75 km, and there is one | 4 |
| Super Heat | 20 km | 5 km | 5 |
| Final | 30 km | 7.5 km | 7 |

**The lap column is a ceiling and it used to be written as a floor.** The
regulations specify a distance and the race runs the *minimum number of full laps
necessary for reaching it*, so 3,750 m on a 1,200 m circuit is 4 laps and not
3.125 and not 3. The first draft of this table said ~3, ~4 and ~6, which is one
lap short on every race, and it rounded 3.75 km to "4 km" — which is what made the
short count hard to spot, because 4 km really would be 4 laps. Both are corrected
above. `src/core/session.h` computes 3,750 m from the FIA figure and the scale, and
`tests/core/test_lap_timing.cpp` derives the lap counts rather than restating them.

**So the round is 15.80 minutes and the season is 63.20, not 15 and 60.** 16 racing
laps at the 48 s pace this section assumes, plus 3 minutes of qualifying. The
one-evening constraint this whole design was built to is overshot by 3.2 minutes.

**Open, and it is a design decision rather than a bug:** move
`SESSION_DISTANCE_SCALE` a little below 0.25 to land the season back under an hour,
or accept 63 minutes and stop claiming 60. Nothing is blocked on it — the test
asserts the real figure either way, so whichever is chosen, the document and the
code will agree.

**Three things were cut and the cuts are the design, not an oversight:** two of
the three Qualifying Heats, three quarters of every race distance, and the
multi-group heat structure the regulations use above 36 entries — with an 8-kart
field only the regulations' Case A applies, where all drivers run every heat.
A reviewer should be able to see that the real structure was read and then
deliberately compressed. That is a different thing from never having looked.

**Open:** three races per round (Heat, Super Heat, Final) may be one too many for
a 15-minute round. The alternative is Qualifying Practice → Heat → Final, which
loses the Super Heat's separate points table and one link in the grid chain. Not
decided; decide it by playing it.

### The grid chain

Each session's result forms the next session's grid, which is the whole point of
the structure:

```
Qualifying Practice   fastest lap        ->  Heat grid
Heat                  position points    ->  Super Heat grid
Super Heat            position points    ->  Final grid
Final                 championship points
```

Ties break on the Qualifying Practice classification at every stage, per the
regulations.

### Four points tables, and none of them is the others

This is the single easiest thing in the design to get wrong, because three of the
four scales start with 50 or 25 and a collapsed version looks entirely plausible.
The real scales are used, truncated to the 8-kart field:

| Table | Scale, to 8th | Used for |
|---|---|---|
| Qualifying Heat position points | 50, 44, 41, 38, 36, 34, 32, 30 | ordering the Super Heat grid |
| Super Heat position points | 90, 80, 72, 66, 60, 54, 50, 46 | ordering the Final grid |
| Championship, the intermediate classification and the Super Heat | 25, 22, 19, 17, 15, 13, 11, 9 | the standings |
| Championship, Final | 50, 44, 38, 34, 30, 26, 22, 18 | the standings |

The first two are added *within* a round. The last two are added *across* the
season, and note that **championship points come from three classifications per
round, not just the Final** — so a bad Final does not erase a good day. Plus one
championship point for the fastest lap of the Final.

The three are the **intermediate classification after the heats**, the **Super
Heat** and the **Final**, which is `REFERENCES.md`'s wording and is the precise
one. This row read "heats + Super Heat classifications" and that phrasing produced
three different readings between this section, ADR-0043 and the brief written from
them. It matters because the heats produce *one* classification between them
however many heats there are: cutting the FIA's three heats to one changes no
championship total, and `race_rules.h` has a test proving exactly that.

And a property of the scales that no document had noticed until a season was
simulated: **the Final scale is exactly twice the heat scale, place for place, for
all eight places an 8-kart field has.** The real scales only diverge at 10th. Two
consequences, both pinned by tests in `race_rules.h`: a `2 * heat_scale`
implementation would pass every test this field size can write, so the
pairwise-distinctness check below proves less than it looks like it does; and a
half-credit Final award is numerically identical to a full-credit heat award, so a
championship total cannot be decomposed back into positions.

Truncating a 36-place scale to 8 places is this project's decision, not the
regulations'. The alternative — rescaling the values to spread across 8 — was
rejected because it invents numbers where real ones exist.

### Track limits, which are ours

The regulations define leaving the track and then attach **no penalty to it**.
General Prescriptions Art. 2.14 B: *"If the four wheels of a kart are outside
these lines, the kart is considered as having left the track."* The 5-second
penalty in Art. 2.24 is for *Incidents*, and the Incident list penalizes having
*"forced another Driver out of the track"* — never going off by yourself.

So `ARCHITECTURE.md` §17's "lap invalidation in time trial" is **our rule, not the
FIA's**, and this design labels it as ours rather than implying a citation it does
not have. Real enforcement appears to live in a per-event Race Director Event
Notes document that could not be obtained; that gap is recorded in `REFERENCES.md`
rather than filled in from Formula 1's "lasting advantage" language, which is a
different rulebook entirely.

Two things the regulations *do* give us and which are worth taking because they
are cheap and correct: qualifying penalties delete the driver's **fastest** lap of
the session rather than the offending one, and a kart restarted with outside help
is disqualified from that session — which is a real rule that our respawn already
half-implements.

---

## 5. The career

Two classes: **OK** into **KZ2**. Both names are the current FIA designations, and
both are sourced — `REFERENCES.md`, *The class ladder*.

Two corrections that the sourcing forced, and they are worth stating because both
were wrong in the first draft of this file:

- **The kart this repository simulates is a KZ2, not a KZ.** The Technical
  Regulations put KZ at 170.0 kg with driver and KZ2 at 175.0 kg; this project
  models 175. `kz_reference.h` already carries that correction in a comment and
  gives the reason — KZ2 is the class anyone racing a shifter is actually in, KZ
  being the international top tier. `ARCHITECTURE.md` §1 has not caught up and
  still says "KZ shifter".
- **The entry class has no clutch at all.** The first draft said "centrifugal
  engagement". FIA direct-drive classes are push-start with no clutch and no
  starter of any kind. *Rotax Max* and *TaG* are not FIA designations — neither
  string appears anywhere in the Technical Regulations — and the only centrifugal
  clutch in the whole rulebook belongs to Mini, 60 cc, engaging at 3,500 rpm.

**The gearbox is not the only promotion, and that is what makes the ladder work.**
`ARCHITECTURE.md` §19 lists "KZ gearbox makes it unplayable for newcomers" as a
risk and answers it by defaulting the assists on. A career answers it better, and
hands over five things at once:

| | OK | KZ2 |
|---|---|---|
| Transmission | direct drive | 6-speed sequential |
| Clutch | **none** | hand-operated, manual |
| Brakes | **rear only**, by regulation | four-wheel, with adjustable bias |
| Engine braking | none to speak of | on every downshift |
| Rev limit | 16,000 rpm | not regulated |
| Minimum weight with driver | 150.0 kg | 175.0 kg |
| Minimum driver age | 14 | 15 |
| Starter | push | push |

Front brakes matter here as much as the gears do. Trade press and coaches describe
the two classes as driven differently in a consistent way: direct drive runs
**rounder, wider lines and defends its minimum corner speed**, because falling off
the pipe has no recovery — there is no downshift to reach for. The shifter
**brakes later, apexes later, leans on the curbs, and trades entry speed for exit
launch**. One coach's account of why the momentum class teaches: *"if you make a
mistake in an LO206, it's a lot more noticeable to the driver … You can feel the
low-powered kart fall on its face."*

So the career's first season teaches the circuit with an instrument that punishes
every error visibly, and the promotion changes how a corner is taken rather than
just adding a button. The assists stay, defaulted on, and turning them off is a
choice a returning player makes rather than a wall a new one hits.

- **Season = 4 rounds, and that is sourced rather than picked.** The 2026 FIA
  Karting European Championship for OK is run over four Competitions. Round ≈ 15
  minutes, season ≈ 60 minutes — one evening, which was the constraint.
- **Nothing is dropped.** The regulations discard a driver's worst results only at
  five Competitions or more, so at four rounds every result counts. A rule we get
  to keep by accident of season length.
- Career = 2 seasons ≈ 2 hours, and it finishes. It is not a live-service ladder.
- **Promotion at top 3 in the final standings.** Not the title. A 4-round season is
  short enough that one bad final can end a title run on variance rather than on
  driving, and the point of the career is to reach the shifter, not to gate it
  behind a perfect season. This said "on a 20-point scale", which is a number that
  appears nowhere in this design — the Final pays 50 for a win and 18 for eighth.
  The real figures make the argument stronger, not weaker: a win-to-eighth swing in
  one Final is **32 points**, against a title margin of **29** in the four-round
  season `race_rules.h` simulates.
- **Losing is real.** Finish outside the top 3 and the season does not promote you.
  Re-run it. Best laps, ghosts and tuning presets persist across the re-run; the
  standings do not.

**OK or OK-N?** OK is the FIA Championship class; OK-N is a national class,
introduced in 2023, deliberately cheaper — 1,000 rpm less, no power valve, 5 kg
heavier. Either is defensible. **OK** is taken, because the season structure
already sourced in §4 is the European Championship for OK, and because a career
that starts in a national class and ends in a Group 2 international class is
crossing two ladders rather than climbing one.

**Cost, stated plainly.** The second class is a second vehicle configuration: no
gearbox, no clutch model at all, rear brakes only, a different torque curve with a
wider usable band, a 16,000 rpm limiter, and 25 kg less. `ROADMAP.md`'s post-demo
list calls the single-speed variant "trivial once the gearbox exists". That is
partly true of the drivetrain — deleting a gearbox is easier than adding one — and
not true of the brakes, the audio, the geometry or the tuning defaults. It is the
largest new line item this design adds, and per §1's own standard it either lands
finished or the career ships with one class and says so.

**And one figure it does not get.** No manufacturer publishes a power output for
any FIA-homologated OK or KZ engine — IAME's own specification tables print
`max-power: –` for both, TM publishes none, and the FIA publishes performance
figures for Superkart and nothing else. `kz_reference.h`'s 45 hp is cited to a
dyno thread and documented as the conservative class-legal figure; OK has no
equivalent, and the secondary "around 35 hp" in circulation is quoted for OK and
OK-N interchangeably, which cannot be true of two classes separated by three
regulated power reductions. **OK's torque curve will be a shape, not a citation.**
It goes on [#159](https://github.com/skiretic/kartgame/issues/159) the day it is
written, marked unsourced, and no probe reports a figure derived from it as a
validated one.

---

## 6. The field

**Grid size is set by a measurement, not by taste.**

`src/audio/engine_voice.h` records 6.23% of real time per voice at the synth's
worst operating point, worst block 11.06%, and twelve voices at 74.76%. A full FIA
Karting grid is roughly 34 karts. That is not available and never was.

The sharper version of the constraint: **the synth's worst case is idle, not the
rev limit** — the harmonic stack fills to a frequency ceiling, so it carries 191
partials at 2,000 rpm against 33 at the soft cut. The one moment when every kart in
the field is simultaneously at low rpm is the standing start. *(INFERRED from the
measured per-voice figure — the coincidence of the audio worst case with the grid
has not itself been probed, and it should be before a field size is committed.)*

Working number: **8 karts including the player**, and even that is 49.8% of the
audio thread's real-time budget at the worst operating point. Distance-culled
voices or a cheap far-field voice are a prerequisite for the field, not a polish
item.

Note also [#155](https://github.com/skiretic/kartgame/issues/155): `ARCHITECTURE.md`
§15 budgets audio at 0.5 ms against the frame clock, and the per-voice figure above
is a fraction of the audio thread's own real-time budget on a different core. Those
two numbers are not comparable, and the ticket is open.

---

## 7. Identity, and how little of it there is

The fiction is **a named driver in a real-shaped race series**, and it is carried
entirely by documents the sport already produces:

- an **entry list**
- **number panels** on the kart
- **timing screens** — sector splits, gaps, a classification
- **standings** after each round

The first two are sourced from the real documents rather than imagined, and both
came out different from the obvious guess.

**The entry list**, read off the 2025 European Championship at Portimão rather
than described: the columns are `No. | Driver | Nat | Entrant | Nat | Equipment`.

```
101 | Turney, Joe            | GBR | KR Motorsport Srl | ITA | KR / IAME / Maxxis
302 | Toviggino, Maximo      | ARG | CRG               | ITA | CRG / IAME / Maxxis
303 | Cuman, Nicolo          | ITA | Forza Racing      | GBR | Exprit / TM Kart / Maxxis
```

Three things a version built from imagination gets wrong: the name is
**"Surname, Forename"**; there are **two** nationality fields, the driver's and
the entrant's, and they routinely differ; and chassis, engine and tire are **one
slash-joined string**, not three columns. The tire make is uniform down the whole
field because it is a mandated control tire, so chassis and engine are the only
variables — which is exactly the texture that makes a generated entry list read as
real. Numbers come in per-category blocks (101–197 for OK, a separate 3xx block
for wild cards) and are fixed for the season.

**The number panels are regulated geometry, not a decal choice.** Technical
Regulations Art. 3.7: black, **Arial**, on a yellow background, at least 15 cm
high with a 2 cm stroke and a 1 cm yellow border, fitted on the **front panel, the
rear, and both sides towards the rear of the bodywork** — four positions. Plus the
driver's name and nationality flag at 3 cm on the front of the side pods. The kart
currently has none of this, and it is a real content job against a real spec.
*(Those centimeters are quoted from the 2023 edition; the 2026 technical
regulations could not be reached. Re-verify before baking them into geometry.)*

There is no written narrative, no dialogue, no cutscene and no rival grudge text.
A rival is a name, a number, a livery and a frozen set of M7's difficulty
parameters — lookahead, throttle discipline, grip ceiling, mistake rate. Consistency
across a season is what makes a rival; the player supplies the story.

This is deliberate. Prose is the cheapest thing to add and the first thing to eat a
sim project's remaining time, and it competes with the only part of this repository
a reviewer will actually be impressed by.

**SOURCING** — entry list fields and race number conventions.

---

## 8. What persists

One profile:

- driver name, number, nationality, livery choice
- career state: class, season, round, standings
- best lap and ghost per track per layout per class
- tuning presets (`user://tuning/`, ADR-0037's format, already built)
- settings: comfort, controls, assists

And one thing this list omitted, which turns out to dominate the disk: **replays**.
ADR-0041 puts them in `user://` and they are measured at **7.79 MB each** for a
15-minute round of eight karts, against 212 kB for *every* ghost a whole career
produces. A player who records a few sessions fills more disk than the rest of the
game combined, so somebody has to own deleting them. Nothing does yet.

**Career state is not in the determinism hash.** ADR-0037 already settled the
equivalent question for tuning presets: `StateHash` asks whether two runs of the
same configuration diverged, and mixing configuration into it makes a mismatch
un-diagnosable. A championship standings table is configuration of the same kind.

---

## 9. The front end

Screens, in the order a first-time player meets them:

```
boot  ->  paddock (main menu)  ->  mode  ->  session setup  ->  loading
      ->  grid / countdown  ->  driving  ->  results  ->  standings  ->  paddock
```

Plus: pause (resume, restart session, controls, quit), settings (comfort per
`ARCHITECTURE.md` §18, controls, assists), and a profile screen.

The menu is the paddock — diegetic, per the fiction level in §7 — and it is the
subject of the storyboard that accompanies this doc.

**The storyboard settles hierarchy and information design, and settles nothing
about how any of it looks.** It is wireframes on purpose.
[#171](https://github.com/skiretic/kartgame/issues/171) is the visual design
pass, and it is blocked on references rather than on effort: paddocks, live
timing screens, classification sheets, number panels on a moving kart. The
telemetry HUD is the worked example of doing this properly — its layout came off
a photograph and contradicted reasoning in five separate places, starting with
the fact that a real kart dash is a *positive* LCD rather than glowing white on
black. A front end assembled from taste will look fine in isolation and read as a
hobby project beside a photograph of the real thing.

**The shell owns no simulation state.** It picks a configuration, hands it to the
session runner, and reads a result back. That is what keeps M6's determinism work
from having to reason about menus.

---

## 10. Track content, and the bill

A season of 4 rounds across one circuit is the same race four times.

- `ROADMAP.md` M5 builds one fictional circuit, 1,000–1,500 m.
- **Reverse layout is real karting practice and costs a spline direction plus a new
  racing line.** Two geometries fill a 4-round calendar.
- Two distinct circuits is therefore the content target, not four. The season reads
  its rounds from data, so a 3-round season ships against what exists and a longer
  calendar is a content drop rather than a code change.

`scenes/game/test_track.tscn` is not a circuit and does not become one. It is an
instrument — four corners, each sized to provoke one named behavior — and it stays
that.

---

## 11. Consequences

For `ARCHITECTURE.md`:

- §17 is titled "Game rules and modes" and names no modes. It grows §3's three.
- §15's audio budget row is already wrong (#155) and the field size in §6 depends on
  it being right.
- §11's track design constraints gain the reverse layout as a first-class case.

For `ROADMAP.md`:

- The tech demo definition gains a front end. It currently ships a game with no
  main scene.
- The shell and the session runner land before M4, ahead of the cameras.
- M6's "race states" become a session runner.
- The single-speed class becomes a real milestone item rather than a post-demo aside.

---

## 12. Open, and owed a ticket

- Field size against a measured standing start, not an inferred one.
- Voice culling or a far-field voice, prerequisite to any field at all.
- The single-speed vehicle configuration, sourced. §5's **SOURCING** block.
- Three races per round or two — §4's open question, decided by playing it.
- Number-plate dimensions are quoted from the 2023 technical regulations because
  the 2026 edition could not be reached. Re-verify before geometry.
- In-race track-limits enforcement is unsourced at the regulation level and our
  rule is therefore ours. Recorded in `REFERENCES.md`.
- [#170](https://github.com/skiretic/kartgame/issues/170) holds the questions for
  a former professional karter — the things a regulation cannot contain. It blocks
  nothing here; the structure does not move if a physics number does.
- What the circuit and the series are called. Deferred deliberately; naming is
  cheap and late.

---

## 13. Build order, and what finished means

**Each phase finishes before the next begins.** Not a scheduling preference — the
standard this whole design is held to. A stubbed mode reads worse than an absent
one, because it advertises an intention the code does not honor, which is the same
failure as a control that is printed on screen and unread.

When scope has to be cut, **cut a whole phase from the bottom**. Never cut the
finish across all of them.

### A. Shell and Practice — ROADMAP M3c

Input becomes data ([ADR-0040](DECISIONS.md#adr-0040--input-is-handed-to-the-vehicle-not-fetched-by-it)),
then boot, paddock, session setup, pause, settings, profile, and Practice end to
end.

**Done means:** the game is launched by double-clicking it, not by typing a scene
path. A person who has never seen the repository reaches a moving kart from the
boot screen without being told how, sets a lap time, sees it recorded, quits, and
finds it still there. `drive.sh`'s four §6.4 figures are unmoved by the input
change. The on-screen control list is asserted against the real bindings
([#169](https://github.com/skiretic/kartgame/issues/169)).

**Not done if:** the menu is a placeholder. A gray panel with three buttons is a
claim the game does not honor, and the command line is more honest than that.
[#171](https://github.com/skiretic/kartgame/issues/171) is the visual pass and it
lands inside this phase.

### B. Race weekend — after M7

Blocked on two things that are not the weekend: AI that races, and voice culling.

**Done means:** Qualifying Practice, Heat, Super Heat and Final run in sequence,
each session's result forming the next grid, on the real position-point scales.
Eight karts hold 60 fps *and* fit the audio budget at a standing start, measured
rather than assumed.

**Not done if:** the field punts you. A grid of karts the AI cannot race is a
worse artifact than a time trial with no grid at all.

### C. Championship — blocked on `gentrack.py`

Second circuit, reverse layouts, four rounds, standings that persist.

**Done means:** a four-round season completes with points from all three
classifications per round, and the standings survive a quit.

**Not done if:** the calendar is one circuit four times.

### D. Career — the second class

OK into KZ2, promotion at top 3, a career that ends.

**Done means:** the single-speed class is recognisably a different machine —
its own torque curve, its own synth voice, rear brakes only, its own tuning
defaults with declared provenance, measured against its own regulation figures the
way §6.4 measures the KZ2.

**Not done if:** it is a KZ2 with the gearbox flagged off wearing a different
name. If that is all it can be, the career ships with one class and says so.
