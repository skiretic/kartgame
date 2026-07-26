# kartgame

A physically-grounded KZ shifter kart racing sim. Free, open source, built in the open.

**Status: M3b, the vehicle.** The C++ extension builds and loads on macOS, Windows, and Linux. Look development is done — physical sun, AgX, baked GI, motion blur. A KZ kart is generated from a parameter block in Blender and imports into Godot at exact scale, byte-identically between runs.

The vehicle solver is engine-free C++ under `src/core/`, held by 180 test cases and 266,816 assertions that run in seconds with no engine at all, and it now drives: `src/vehicle/kart_body.cpp` is the `RigidBody3D` that casts the suspension rays, calls the solver and applies the forces. The kart settles within 2 µm of its predicted rest height and its static corner loads sum to `m·g` to 0.02 N.

It does not yet drive *well*, and that is recorded rather than smoothed over — past a quarter of steering lock it scrubs off most of its speed ([#137](https://github.com/skiretic/kartgame/issues/137)), and the judgements the milestone turns on cannot be made without engine sound and a driving HUD ([#138](https://github.com/skiretic/kartgame/issues/138)). See [`docs/ROADMAP.md`](docs/ROADMAP.md) for what is being built and in what order, and [`docs/DECISIONS.md`](docs/DECISIONS.md) for what earlier sessions got wrong.

---

## What this is

A racing sim built around one idea: **a kart is not a small car.** A KZ shifter kart has a solid rear axle, no differential, and no suspension — the chassis frame itself flexes and lifts the inside rear wheel through a corner. That wheel lift *is* the differential. Model it and the kart rotates on its nose; skip it and the axle scrubs and you have a shopping trolley.

Everything else follows from taking that seriously:

- Per-wheel suspension raycasts, Pacejka-style tire model, load-sensitive grip, emergent weight transfer
- 6-speed sequential gearbox, clutch, heavy two-stroke engine braking, ~45 hp at 13,000 rpm
- Solver substepping at 240 Hz inside a 120 Hz fixed tick
- Validated against real KZ performance figures — ~140 km/h, ~3.5 s to 100 km/h, 1.5–2.0 g sustained lateral against 2.0–2.5 g transient — as an automated test suite, not by feel. Those two lateral bands were one band for two milestones, until it turned out its upper half described a state the kart physically cannot hold: it tips at 2.43 g ([ADR-0034](docs/DECISIONS.md))

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

Unreal renders better with less effort, and that gap is real. But Unreal's source sits under Epic's EULA, not an open source license — engine source cannot live in a public repo, and every contributor needs an Epic account and a 100 GB download before they can build. Godot is MIT end to end: clone, build, run, fork the engine itself if you need to. For a project whose point is being open source, that decided it.

The full reasoning, including the calls that were reversed and why, is in [`docs/DECISIONS.md`](docs/DECISIONS.md).

## Documentation

| Document | Contents |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | System design — rendering, physics, vehicle model, determinism, content pipeline, budgets |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Milestones M0–M10, each with a testable acceptance gate |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Architecture decision log, including superseded decisions |
| [`ATTRIBUTION.md`](ATTRIBUTION.md) | Third-party asset provenance and licenses |

## Building

```bash
git clone --recurse-submodules https://github.com/skiretic/kartgame.git
cd kartgame
scons target=editor          # the build the Godot editor loads
tools/verify/verify.sh       # asserts the extension loaded and is wired correctly
```

Then open the project in Godot. The default scene is a gamepad probe that prints what
the C++ extension reports about itself and shows live controller state.

Already cloned without `--recurse-submodules`? `git submodule update --init --recursive`.

`scons target=template_release` builds what an exported game loads. Everything lands in
`bin/`, which is gitignored — the repository ships source, not binaries.

Toolchain, pinned:

```
Godot      4.7.1 stable
godot-cpp  master @ 9c7567d2   (no 4.7 release branch exists upstream — ADR-0016)
Blender    5.2.0 LTS
SCons      4.10.1
git-lfs    3.7.1
```

### macOS: a known engine crash

`godot --headless --import` segfaults inside MoltenVK on macOS 26 and 27. It is an
upstream Godot defect, reproducible with godot-cpp's own example extension and with
Godot 4.5.2, and unrelated to this project. The GUI editor and headless *game* mode are
both unaffected, so normal development needs no workaround.

The crashing run still seeds `.godot/` before dying, so `tools/verify/verify.sh` imports
twice and ignores the first result. CI runs the headless gate on Linux. Detail and the
full elimination trail are in [ADR-0018](docs/DECISIONS.md#adr-0018--macos-headless-imports-crash-in-moltenvk-ci-verifies-on-linux).

## License

Code: [MIT](LICENSE).

Assets: per their source, recorded in [`ATTRIBUTION.md`](ATTRIBUTION.md). Third-party material is CC0 wherever possible — for an open source project, license clarity beats a marginally better scan.
