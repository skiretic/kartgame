# Front end — art direction and design language

Companion to `GAMEDESIGN.md` §9, which owns *what* the screens are, and to the
storyboard, which owns hierarchy. This file owns *how it looks*, and every
claim in it traces to a reference recorded in `REFERENCES.md` under *The front
end and the paddock* (F1–F6) or to a regulation. Where a choice is ours it is
labeled ours. ADR-0052 is the decision record; issue #171 is the ticket this
file exists to close; #187 is the asset bill.

Status: **built at M5f** (2026-08-06). Nine of the ten screens exist and are
gated by `tools/verify/shell.sh`; standings is deferred with the championship.
The language here is the source the build was made from -- where a screen and
this file disagree, this file is right and the screen is a bug.

---

## 1. Two layers, two languages

The front end is a **place with documents laid over it** (ADR-0052 decision 1):

- **The place** — the generated 3D paddock. Its look is owned by the same
  pipeline and lighting rules as everything else rendered (`look_env.gd`,
  physical light units), not by this file. What this file owns about it is the
  *content list*, §5.
- **The documents** — every interactive surface. These are styled as the
  sport's own paperwork, because §7 of GAMEDESIGN already establishes that
  documents *are* the fiction: an entry list, timing screens, classification
  sheets, standings.

The documents split into two families with opposite grounds, and the split is
sourced, not aesthetic:

| Family | Ground | Members | Source |
|---|---|---|---|
| **Screen documents** — live, glanceable | dark | live timing strip, driving HUD timing, pause overlay, loading | F5/F6: the real live-timing product is dark with state-colored cells |
| **Paper documents** — records, read at rest | light | results, standings, entry list, session setup, settings, profile | the published classification sheet and entry list are print documents; a PDF is white |

A screen document glows over the world; a paper document is a sheet the world
holds. The paddock menu overlays are paper. The one deliberate exception is
the driving HUD's dash cluster, which stays a **positive LCD** per the HUD
section of REFERENCES.md — it is an instrument, not a document.

## 2. Palette

Derived pattern (F6, the deployed FIA Karting timing page): one **deep
institutional color** for chrome (their navy `rgb(0,48,98)`), one **vivid
accent** doing all the live work (their green `rgb(0,226,147)` — the progress
bar and the title accent), and **state colors that are states, not
decoration**: green = improvement, red = slower, purple = session best
(motorsport convention, visible in F5's cells), yellow reserved for flags.

Ours, placeholder until the series is named (naming is open on purpose):

    --series-deep:    a dark saturated blue-black   (chrome, screen-document ground)
    --series-accent:  one vivid color, not FIA's green (live values, progress, focus)
    --paper:          warm white                     (paper-document ground)
    --panel-yellow:   #d7c354                        (Art. 3.7; sampled from F3/F4 —
                                                      three lightings bracket it,
                                                      provisional, REFERENCES.md)

Rules, all sourced from what the references do:

- **One accent.** Everything live uses it; nothing decorative does.
- **State colors never appear except as state.** A green header is a lie.
- **Yellow belongs to panels and flags** and is never a UI accent, or a yellow
  sector flag becomes invisible.
- The FIA's exact navy/green pair is **not** used: that is their trade dress.
  The pattern is taken; the values are ours. Same rule as the HUD's H1–H4.

## 3. Typography

- **Tabular numerals everywhere a number can change or align.** Every timing
  figure, gap, points cell. Non-negotiable; it is what makes F5's tables read
  as instruments.
- **Times as `m:ss.mmm`**, deltas signed, negative ahead (storyboard, settled).
- **Number panels: black on yellow in a metric substitute for Arial**
  (Liberation Sans, OFL) — Art. 3.7 names Arial and Arial does not ship. #187.
  Panel dimensions re-verified against the 2026 text before geometry.
- **Paper documents** set in a plain grotesque, dense, small caps for column
  heads — the register of a federation PDF, not of a game menu.
- **Screen documents** may use one display weight for the single enormous
  figure each screen is allowed (the current split in F5 is the model: one
  number owns the screen, everything else is small).
- No italic anywhere a number lives. Italic is for the one place the fiction
  speaks (§4, the invitation line).

## 4. Layout language

From F5/F6 and the race-format section of REFERENCES.md:

- **The timing grid's columns are the real ones**: `No | Driver | Nat |
  Entrant | Nat | Equipment` for entry contexts; `Pos | No | Driver | Last |
  Best | Gap | Interval | S1 S2 S3` for live contexts. **Gap and Interval are
  two columns**, gap-to-leader and gap-to-ahead — F6 carries both and
  collapsing them is the kind of error #171 exists to prevent.
- **Kart state is a word from the real list**: On track, Pit in, Pit out,
  Stopped, Out. Not invented synonyms.
- **A progress-lap bar** — the accent filling along the row as a kart
  completes its lap (F6's `progress_lap`) — is the live screen's signature
  motion and costs nothing.
- **A session tree down the left** of any results context (F5): Qualifying,
  Heat, Super Heat, Final as siblings under the round. It is how the real
  results site organizes a weekend and it maps exactly onto the session
  runner.
- **Names render "Surname, Forename"** everywhere (sourced, race-format
  section); the driver's own name gets no special typography in a
  classification — the fiction is that the sheet does not care who you are.
- Paper documents carry **page furniture**: a document title block (event,
  session, circuit, date), rule lines, and a footer stamp. The register of
  the real sheet — to be verified against an actual FIA PDF when the browser
  session fetches one; until then the furniture is INFERRED and marked so.

## 5. The paddock's content list (F1, F2)

What the vignette and its growth stages contain, from photographs:

- **Stage 1 — the vignette:** kart nose-up on a chrome wheeled stand with
  basket tray; modular interlocking tile floor; branded floor mat under the
  kart; tent interior walls (white PVC), sponsor-print backdrop wall; LED
  strip under the header rail; a tool table with a paper-towel roll.
- **Later stages:** second kart bay, tire stacks, transport cases, awning
  exterior and guy lines, neighbor tents, trailer, the asphalt lane between
  tent rows; pre-grid furniture (F2): canopy tents, barrier banners.
- Every prop goes through the generator pipeline with the kart's determinism
  rules. Module plan and effort per stage: its own ticket, after the outdoor
  reference gap closes.

## 6. Per-screen notes

| Plate | Family | The one large thing | Notes |
|---|---|---|---|
| 01 boot | screen | wordmark | series name pending; failure text is words, not a code |
| 02 paddock | paper over the place | none — the place is the hero | menu cards as paper index cards; "where you are" as an entry-form extract |
| 03 setup | paper | the track map | reads as an entry form being filled; config readback as the form's office-use box |
| 04 loading | screen | circuit name | progress as named steps; tip line in the fiction's voice |
| 06 driving | screen + instrument | the delta | timing strip top-left, dash is the LCD instrument, ghost delta opposite timing |
| 07 results | paper | best lap time | classification sheet with session tree; every lap; struck laps with reason |
| 08 standings | paper | the player's row | entry-list columns; promotion line as the sheet's footnote |
| 09 pause | screen | "PAUSED" | dark overlay, the world visible behind; consequence line on the timing row |
| 10 settings | paper | none | one grouped list; register of a scrutineering form |
| 11 profile | paper | the number panel itself | the panel rendered at regulation proportion is the screen's identity element |

## 7. What round 1 of the mockups tests

The first HTML mockup covers the strongest-referenced family — results,
standings, live timing strip — and exists to be wrong in useful ways. It
tests: the two-family split, the placeholder palette, tabular-numeral
hierarchy, the real column sets, and the session tree. It deliberately does
not test the paddock (outdoor references still open) or the driving HUD (built
and separately sourced).

Open before round 2, and recorded in REFERENCES.md: the printed FIA sheet's
typography (browser route), the outdoor paddock wide shot, the panel yellow's
measured value.

## 8. Approval, and the build blueprint (M5f)

2026-07-28: the full family — all ten screens of
`docs/mockups/frontend_family.html` — is **approved**. Section 7's open items
closed on the way (typography verified off the real PDFs, paddock swept, panel
yellow sampled at #d7c354 provisional). This section is how the mockups become
Godot, per ADR-0053; the milestone is ROADMAP M5f and nothing builds until the
design phase ends.

### Scene shape

    ShellRoot (Node3D)                  the main scene in project.godot
    ├─ Paddock (Node3D)                 generated modules, #188 staged
    ├─ ShellCamera (Camera3D)           parked framings per screen, not a free cam
    └─ UI (CanvasLayer)
       └─ ScreenStack (Control)         push/pop; owns focus and the back rule
          ├─ BootScreen … ProfileScreen one Control scene per approved plate

    Starting a session swaps to the track scene; everything else stays inside
    ShellRoot. Screens never talk to each other — they push, pop, and read/write
    the same data the probes already exercise (profile, settings, standings).

### Input

Menus are their own input context with their own actions (`ui_confirm` family,
not reuse of driving actions): Cross confirm, Circle back, d-pad + left stick
navigate, Enter/Esc on keyboard. `control_hints.gd` grows the menu context.
Focus is always visible and always lands somewhere on screen entry —
`shell_probe.gd` checks it rather than trusting the scene.

### Theme

One Theme resource carries §2's tokens; screens use the tokens and never a raw
hex. Liberation Sans regular/bold from the fetch script; tabular figures on
anything that lines digits up. The accent stays the azure placeholder until the
livery round picks the final hue — one token to move.

### The calendar

`data/seasons/` joins the data directory: a season names its rounds, a round
names a circuit + layout. Schema doc plus executable copy, load refuses, same
family as TRACK_SCHEMA. Rounds without a built circuit are declared, drawn with
the honest label, and refuse to start (ADR-0053 §4).

### Flags

Generated SVGs from recorded construction data (ADR-0053 §5), one script under
`tools/assets/`, deterministic, roster-driven — it reads `data/drivers.json`
and refuses a nationality it has no construction for.
