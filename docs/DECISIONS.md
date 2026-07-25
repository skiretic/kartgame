# Decision log

Architecture decisions, in the order they were made. Superseded entries are kept rather than deleted — the reasoning that led somewhere wrong is part of the record, and the reason a decision was reversed is usually more useful than the decision itself.

All entries below date from the design session on **2026-07-24**.

---

## ADR-0001 — Native code and Vulkan, not web technology

**Status:** Superseded by [ADR-0009](#adr-0009--godot-4-over-both-a-custom-vulkan-engine-and-unreal-5)

**Context.** Initial planning leaned toward a web-first TypeScript stack because Node was already installed and it ships everywhere cheaply.

**Decision.** Native compiled code, GPU rendering through Vulkan. No browser runtime.

**Consequences.** Set the platform baseline: MoltenVK on macOS, native Vulkan elsewhere. Ruled out the browser as a target permanently.

**Rejected.** Three.js/WebGL with Tauri and Capacitor wrappers — rejected on performance ceiling and because the goal was to work close to the metal.

---

## ADR-0002 — Build a custom engine rather than use an existing one

**Status:** Superseded by [ADR-0009](#adr-0009--godot-4-over-both-a-custom-vulkan-engine-and-unreal-5)

**Context.** With the scope question framed as "what does finished look like", the answer chosen was engine-first: build a renderer, ECS, and tooling, with the kart game as the proving application.

**Decision.** Write a custom C++20 Vulkan engine. Render graph, bindless resources, GPU-driven culling, hot-reloading shaders, in-game profiler.

**Consequences.** Produced a complete engine architecture — since superseded, but the sim-side thinking in it survived the migration intact.

**Rejected.** Godot and Unreal, on the grounds that using them would defeat the stated point of the project.

---

## ADR-0003 — Clustered forward+ rendering, not deferred

**Status:** Superseded by [ADR-0008](#adr-0008--drop-android) and [ADR-0009](#adr-0009--godot-4-over-both-a-custom-vulkan-engine-and-unreal-5)

**Context.** Android was a target at the time. Tile-based mobile GPUs make a fat G-buffer a bandwidth catastrophe, and MoltenVK lacks geometry shaders, usable tessellation, and ray tracing.

**Decision.** Clustered forward+ with quality tiers, no deferred path.

**Consequences.** Shaped the entire renderer around the weakest target. When Android was dropped this constraint evaporated, and shortly after so did the custom renderer.

**Lesson worth keeping.** The most constrained platform silently dictates architecture for every other platform. Confirm the target list is real *before* designing around it — this decision cost the most rework of anything in the session.

---

## ADR-0004 — Jolt for world collision, custom solver for the vehicle

**Status:** Accepted. Survived every subsequent reversal.

**Context.** Vehicle physics splits cleanly into two jobs: world collision (broadphase, mesh collision, CCD, contact solving) and vehicle dynamics (suspension, tire grip, weight transfer). The first is solved, boring, and roughly two months of subtle tunneling and jitter bugs to reimplement. The second is game feel.

**Decision.** Jolt owns collision. The tire and suspension model is written by hand.

**Consequences.** Skips the part no player notices while keeping authorship of the part that defines the game. The tire model can be replaced without touching collision. Later validated by Godot shipping Jolt in core since 4.4 — the decision survived the engine change for free.

**Rejected.** Bullet (weaker determinism, dated API); Jolt's own `VehicleConstraint`, which bounds feel to what its controller exposes; writing everything, which spends two months on plumbing that adds nothing kart-specific.

---

## ADR-0005 — Deterministic fixed-tick simulation, netcode deferred

**Status:** Accepted

**Context.** Online multiplayer was wanted but the transport and authority model was not yet decidable. Choosing wrong early is expensive; ignoring it entirely is worse.

**Decision.** Build the sim deterministic now — fixed tick, state as a pure function of seed plus input stream, separated gameplay and presentation RNG streams, stable iteration order. Pick a netcode model later.

**Consequences.** Ghosts and replays fall out immediately rather than being retrofitted. A state-hash harness in CI guards the property. The cost is discipline that has to hold from the first tick, since "we'll fix determinism later" rarely survives contact.

**Known limit.** Same-binary reproduction is expected; cross-architecture bit-determinism is not. Cross-platform rollback would require a fixed-point rewrite of the vehicle solver.

---

## ADR-0006 — Hybrid content generation

**Status:** Superseded by [ADR-0010](#adr-0010--cc0-photoscans-lead-procedural-for-precision-ai-for-gaps)

**Decision.** Bake authored-feeling content (tracks, karts, key materials) offline; generate filler (foliage, crowd, sky, debris) at runtime.

**Superseded because** the art direction moved to realistic, and hand-authored procedural materials do not reach photoreal. See ADR-0010.

---

## ADR-0007 — Realistic art direction

**Status:** Accepted

**Context.** The initial choice was stylized-modern. It was later revised to "at least somewhat realistic."

**Decision.** Realism, pursued through an explicitly ordered checklist rather than by accumulating effects.

**Consequences.** The ordering matters more than the target. Correct real-world scale, texel density discipline, AgX tonemapping, motion cues, and real reference data outrank every asset-source decision and cost almost nothing. This directly forced ADR-0009 and ADR-0010 — realism is expensive to build from scratch and cheap to inherit.

---

## ADR-0008 — Drop Android

**Status:** Accepted

**Context.** Android was the constraint that had shaped the entire renderer (ADR-0003), and the only target requiring a second input scheme and UI layout.

**Decision.** macOS, Windows, and Linux/Steam Deck only. No Android, no iOS.

**Consequences.** Reopened deferred rendering, a fuller post-processing chain, and mesh shaders — all of which became moot two decisions later, but the point stands: one target was setting the ceiling for all of them.

---

## ADR-0009 — Godot 4 over both a custom Vulkan engine and Unreal 5

**Status:** Accepted. Supersedes ADR-0001, ADR-0002, ADR-0003.

**Context.** Three requirements arrived that pulled against the custom engine: the game should look realistic, cost nothing, and be open source. Reaching realism from scratch means personally writing global illumination, area lights, volumetrics, temporal upscaling, high-quality shadow filtering, layered materials, and a VFX system.

**Decision.** Godot 4, Forward+ renderer.

**Why not Unreal 5.** With the project free and non-commercial, the 5% royalty above $1M never applies — that argument is void. The deciding factor is different: Unreal's source is *available*, not *open*. It sits under Epic's EULA, cannot be placed in a public repository, and requires every contributor to hold an Epic account and download 100+ GB before building. Godot is MIT end to end — clone, build, run, fork the engine itself. For a project whose purpose includes being open source, that is decisive rather than merely preferable.

Secondary: the dev machine is an M1 Pro with 16 GB. Unreal's editor is heavy there, Nanite and Lumen are both weaker on Apple Silicon than on Windows, and iteration time compounds badly for a solo developer.

**Accepted cost.** Godot's fidelity ceiling is below Unreal's, and that gap is real. It is narrower for this specific game — a kart sits low, moves fast, and has short sightlines, so material quality, scale, and motion cues matter more than GI sophistication.

**What survived the migration.** ADR-0004 and ADR-0005 entirely, plus the vehicle model, camera design, AI approach, and audio synthesis plan. What was lost was the Vulkan renderer work, which was the correct thing to lose.

---

## ADR-0010 — CC0 photoscans lead, procedural for precision, AI for gaps

**Status:** Accepted. Supersedes ADR-0006.

**Context.** The original premise was that all content would be generated. Realism (ADR-0007) puts that under real pressure.

**Decision.**

- **Materials:** CC0 photoscans from Poly Haven and ambientCG. Real measured albedo and roughness beat anything hand-authored or AI-generated.
- **Karts and tracks:** generated, because they need exact proportions and clean topology.
- **AI generation:** gap-filling only — decals, wear overlays, background props, signage.

**Why AI mesh generation is not used for anything visible.** Open image-to-3D models produce dense triangle soup with poor auto-UVs. Acceptable for background props after a retopology pass, never for a kart the player sits inside.

**Hardware note.** 16 GB unified memory makes local Flux slow and tight and SDXL sluggish, which reinforces treating AI generation as gap-fill rather than a pipeline to lean on.

**License stance.** Prefer CC0 over marginally better restricted sources. Fab/Megascans is deliberately avoided — its terms are not CC0 and have carried engine-use restrictions.

---

## ADR-0011 — KZ shifter class, not single-speed TaG

**Status:** Accepted

**Context.** The physics envelope had been assumed as a 125cc single-speed TaG with a centrifugal clutch — a real class, and the simpler one.

**Decision.** KZ: 125cc two-stroke, ~45–50 hp, 6-speed sequential with a hand shifter and clutch lever, ~175 kg with driver, ~140 km/h, cornering near 2.5 g.

**Consequences.** The gearbox becomes a real subsystem — ratios, clutch slip, 50–80 ms shifts with torque cut, engine braking, rev limiting, over-rev consequences. Heavy two-stroke engine braking shapes corner entry more than the brakes do on some corners, which changes how the whole vehicle is driven. Auto-clutch and auto-shift default on, because an unassisted first lap in a shifter kart ends in a stall.

Highest skill ceiling in real karting, and it makes the drivetrain worth modeling rather than a constant.

---

## ADR-0012 — Blender Python for geometry generation, not C++

**Status:** Accepted

**Context.** Kart and track meshes were to be generated in a C++ GDExtension. Blender turns out to be fully scriptable headless via `bpy`.

**Decision.** Geometry generation moves to Blender Python, run headless and exported as glTF.

**Consequences.** Bevels, UV unwrapping, lightmap UV2, normal baking, and LOD decimation all come free — three of those would be substantial C++ work for a worse result. Output can be rendered and inspected visually rather than debugged blind. The GDExtension shrinks to vehicle, AI, audio DSP, and track gameplay data.

**Costs.** A Blender version dependency, pinned to 5.2 LTS since `bpy` shifts between major versions. Hard-surface auto-retopology is poor, so kart geometry is built clean from parameters rather than retopologized.

---

## ADR-0013 — `track.json` as single source of truth

**Status:** Accepted

**Context.** Splitting geometry generation (Blender) from gameplay data (C++) creates two consumers of one track definition, and therefore an opportunity for them to drift apart.

**Decision.** One `data/track.json` holding control points with position, width, banking, elevation, plus surface spans and start grid. Blender reads it for the visual mesh; the C++ extension reads it for collision, checkpoints, racing line, and material zones.

**Consequences.** What you see and what you collide with cannot diverge. Track authoring is text, so it diffs and reviews cleanly.

---

## ADR-0014 — Fictional circuit, validated against performance data

**Status:** Accepted

**Context.** A real circuit would supply published lap times — the single best available check that the physics is right. A fictional one supplies none.

**Decision.** Fictional layout built to real kart-circuit constraints: 1,000–1,500 m, minimum radius sane against achievable speed, run-off proportional to approach speed, at least two genuine overtaking spots, real elevation change.

**Consequence — the important half.** Losing lap-time ground truth means replacing it. Validation moves to instrumented scenarios asserting against published KZ figures: ~135–145 km/h top speed, ~3.5 s to 100 km/h, 2.0–2.5 g lateral on a constant-radius skidpad, 1.5–2.0 g braking. These run in CI as the physics regression suite.

A fictional track is only acceptable *because* that substitute exists. Without it, the physics would have nothing external to be wrong against.

---

## ADR-0015 — GDScript for game flow, C++ GDExtension for the sim

**Status:** Accepted

**Decision.**

| C++ GDExtension | GDScript |
|---|---|
| Vehicle solver, tires, gearbox, substepping | Game flow, race states |
| Track gameplay data | HUD, menus, settings |
| AI racing line and speed profile | Camera rigs |
| Audio DSP and engine synthesis | |

**Reasoning.** The vehicle solver runs a tight numeric loop at 240 Hz — GDScript is too slow and too imprecise. Camera rigs are cheap per-frame math but heavy on tuning, so fast reload is worth more than execution speed there.

**Consequence.** Keeps real systems programming at the center of the project after ADR-0009 removed the custom engine, while inheriting everything Godot does well. The extension boundary stays narrow and data-oriented — per-frame cross-language calls are fine, per-sample are not.

---

The entries below date from the M0 implementation session on **2026-07-24**, and each
one records something the design session had assumed rather than checked.

---

## ADR-0016 — Pin `godot-cpp` to a `master` commit, because no 4.7 branch exists

**Status:** Accepted

**Context.** `ARCHITECTURE.md` and the M0 gate both assumed the ordinary GDExtension
setup: add `godot-cpp` at the release branch matching the pinned engine. That branch
does not exist. Upstream `godot-cpp` has release branches through `4.5` and tags
through `godot-4.5-stable`, and nothing for 4.6 or 4.7 — even though Godot itself has
shipped 4.6, three 4.6 patch releases, and 4.7.1.

What upstream does have is a `master` branch under daily development whose bundled
`gdextension/extension_api.json` reports `4.7.stable`, alongside archived
`extension_api-4-3.json` through `extension_api-4-6.json` selectable with SCons'
`api_version` option. So `master` is the supported path for 4.7; there is simply no
stable tag on it.

**Decision.** `third_party/godot-cpp` is a git submodule pinned to commit `9c7567d2`.
No branch is recorded in `.gitmodules`, so `git submodule update --remote` cannot
silently advance it. Bindings are generated from the bundled 4.7 API rather than from
a locally dumped one, so CI needs no Godot install to build the extension.

**Why not the alternatives.** Vendoring the source drops the ability to see upstream
history and rebase onto fixes. Tracking `master` as a branch means every clone can get
a different compiler input, which is the opposite of a pinned toolchain. Using the
`4.5` branch with `api_version=4.5` would mean giving up 4.6 and 4.7 engine features
to gain a version tag, which is the wrong trade.

**Consequence.** Moving the pin is a deliberate, reviewable commit, which is what
`ARCHITECTURE.md` §19 asks for under "Godot version churn". The cost is that the pin
sits on a development branch with no stability promise, so an engine upgrade means
re-testing the extension rather than trusting a version match. Verified in practice:
the extension builds and loads against 4.7.1 with bindings generated from the 4.7.0
API, which is the expected within-minor compatibility.

**Also worth recording:** the bundled API being 4.7.0 while the engine is 4.7.1 was the
first suspect for a crash that turned out to be ADR-0018. Rebuilding against 4.7.1's
own dumped `extension_api.json` changed nothing. The version skew is genuinely benign.

---

## ADR-0017 — `src/core/` is compiled without godot-cpp

**Status:** Accepted. Corrects a claim in `ARCHITECTURE.md` §14.

**Context.** The testing plan called for "doctest or Catch2 on the vehicle math, tire
curves, spline solver — engine-independent, fast". That is not free. A tire model
written against `godot::Vector3` needs godot-cpp linked into the test binary, which
means generated bindings, a GDExtension interface pointer that only exists inside a
running engine, and a test suite that is no longer engine-independent in any useful
sense. The claim was aspirational, and left alone it would have quietly become false
the first time a solver file included a Godot header.

**Decision.** `src/core/` is plain C++ with no godot-cpp includes: math types, PCG32,
state hashing, unit conversions, and the KZ reference figures. Everything numeric and
testable lives there. The rest of `src/` may use godot-cpp freely, and is the thin
layer that marshals between Godot types and core types.

M0 puts `units.h` and `kz_reference.h` there and exercises the boundary through
`KartCore.kmh_to_ms()`, so the rule has a working example before there is anything
complicated to test.

**Consequence.** The unit-test binary compiles against `src/core/` alone — no engine,
no bindings, no fixture. That is what makes the M3b tire and gearbox math testable in
milliseconds instead of through a running scene. The cost is one conversion at the
boundary, which is deliberate: it is the same narrow, data-oriented edge ADR-0015
already asked for, and having it in one place makes it cheap to keep narrow.

**Rejected.** Letting the solver use `godot::Vector3` throughout and linking godot-cpp
into the tests. It works, it is less code today, and it makes the tests slow, fragile,
and dependent on the exact engine version — which is precisely what a fixed-point
rewrite for cross-platform determinism (ADR-0005) would later have to unpick.

---

## ADR-0018 — macOS headless imports crash in MoltenVK; CI verifies on Linux

**Status:** Accepted, as a workaround for an upstream defect.

**Context.** `godot --headless --import` segfaults on this machine. `EXC_BAD_ACCESS`,
null dereference at address `0x8`, inside MoltenVK converting SPIR-V to Metal Shading
Language — in `--headless`, where there should be no shader compilation at all.

Diagnosing it consumed most of the M0 session, so the elimination order is worth
recording:

| Suspected | Test | Result |
|---|---|---|
| Our C++ | Register no classes | No crash. Registration implicated. |
| Our class shape | `Object` vs `RefCounted`, abstract vs concrete | Crashes in every combination. |
| godot-cpp/engine ABI skew | Diff `gdextension_interface.json`, engine 4.7.1 vs godot-cpp `master` | Byte-identical. Not an ABI mismatch. |
| API version skew | Rebuild against 4.7.1's own dumped `extension_api.json` | Still crashes. |
| **Our project at all** | Build and run **godot-cpp's own upstream test extension** | **Crashes identically.** Not our code. |
| Godot 4.7 specifically | Godot 4.5.2 with matched godot-cpp `4.5` branch | Crashes identically. Not version-specific. |
| Rendering driver | `--rendering-driver opengl3`, `--display-driver headless`, `--audio-driver Dummy` | No effect. |
| The editor at all | GUI editor, and headless *game* mode | **Both clean.** Headless *editor* only. |

The host is macOS 27 beta. Upstream has the same crash filed against macOS 26 for Godot
4.5, 4.5.1, and 4.7, so this is an existing engine defect that the beta has not fixed
rather than anything introduced by the beta.

**Decision.**

1. **CI runs headless verification on Linux, not macOS.** macOS still builds the
   extension on every push; it just does not run the headless gate.
2. **Locally, `tools/verify/verify.sh` imports twice.** The crashing cold run seeds
   `.godot/` before it dies, so the warm run is clean. The first exit code is ignored.
3. **The GUI editor is unaffected**, so day-to-day development needs no workaround.

**Consequence.** M0's acceptance gate is met on Linux in CI and interactively on macOS,
but not headlessly on macOS. That is a real hole and it is stated rather than papered
over: a macOS-only regression in the headless path would not be caught by CI. Revisit
when the upstream fix lands — the double-import in `verify.sh` should be deleted, not
kept as folklore.

**Worth keeping.** Godot's own crash handler symbolizes against the wrong symbol table
and prints nonsense frames — `SDL_GetPerformanceFrequency + 4158164`,
`mvk::SPIRVToMSLConverter::convert`. The MoltenVK frame in that garbage was real and
was dismissed as noise for most of the session. macOS writes an honest report to
`~/Library/Logs/DiagnosticReports/*.ips`; read that instead. `lldb` is no help here,
because it cannot parse the stripped official binary.
