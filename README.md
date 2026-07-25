# kartgame

A physically-grounded KZ shifter kart racing sim. Free, open source, built in the open.

**Status: design phase.** The architecture is written; no engine code exists yet. See [`docs/ROADMAP.md`](docs/ROADMAP.md) for what is being built and in what order.

---

## What this is

A racing sim built around one idea: **a kart is not a small car.** A KZ shifter kart has a solid rear axle, no differential, and no suspension — the chassis frame itself flexes and lifts the inside rear wheel through a corner. That wheel lift *is* the differential. Model it and the kart rotates on its nose; skip it and the axle scrubs and you have a shopping trolley.

Everything else follows from taking that seriously:

- Per-wheel suspension raycasts, Pacejka-style tyre model, load-sensitive grip, emergent weight transfer
- 6-speed sequential gearbox, clutch, heavy two-stroke engine braking, ~45 hp at 13,000 rpm
- Solver substepping at 240 Hz inside a 120 Hz fixed tick
- Validated against real KZ performance figures — ~140 km/h, ~3.5 s to 100 km/h, 2.0–2.5 g lateral — as an automated test suite, not by feel

Plus a cockpit view, deterministic replays and ghosts, generated tracks and karts, and AI that drives through the same input path a human does.

## Tech

| | |
|---|---|
| Engine | Godot 4.7.1, Forward+ renderer |
| Physics | Jolt (Godot built-in) for collision; custom vehicle solver |
| Languages | GDScript for game flow, C++ GDExtension for the sim |
| Content | Blender 5.2 LTS Python for generated geometry, CC0 photoscans for materials |
| Platforms | macOS, Windows, Linux / Steam Deck |

## Why Godot and not Unreal

Unreal renders better with less effort, and that gap is real. But Unreal's source sits under Epic's EULA, not an open source licence — engine source cannot live in a public repo, and every contributor needs an Epic account and a 100 GB download before they can build. Godot is MIT end to end: clone, build, run, fork the engine itself if you need to. For a project whose point is being open source, that decided it.

The full reasoning, including the calls that were reversed and why, is in [`docs/DECISIONS.md`](docs/DECISIONS.md).

## Documentation

| Document | Contents |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System design — rendering, physics, vehicle model, determinism, content pipeline, budgets |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Milestones M0–M10, each with a testable acceptance gate |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Architecture decision log, including superseded decisions |
| [`ATTRIBUTION.md`](ATTRIBUTION.md) | Third-party asset provenance and licences |

## Building

Not yet buildable — M0 is in progress. Instructions land with it.

Toolchain, pinned:

```
Godot    4.7.1 stable
Blender  5.2.0 LTS
SCons    (godot-cpp build)
git-lfs  3.7.1
```

## Licence

Code: [MIT](LICENSE).

Assets: per their source, recorded in [`ATTRIBUTION.md`](ATTRIBUTION.md). Third-party material is CC0 wherever possible — for an open source project, licence clarity beats a marginally better scan.
