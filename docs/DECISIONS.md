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

**Amended 2026-07-25 — this is not macOS-only.** The first full CI run after the
matrix fix showed the Linux runner failing the same way: `godot --headless --import`
exiting 134, `Aborted (core dumped)`, after the editor had loaded. Different signal from
the macOS `EXC_BAD_ACCESS`, same operation and the same cold-run-only behavior.

The workaround already existed and CI simply was not using it — the job imported once
and treated a non-zero exit as failure, while `verify.sh` had imported twice since M0.
The job now calls `verify.sh` instead of keeping a second copy of its steps in YAML,
which is the general lesson: a workaround duplicated into CI is a workaround that will
be absent from one of the two copies.

Two claims above are therefore wrong and are corrected rather than deleted. Decision 1
said macOS "does not run the headless gate" as though Linux were unaffected; Linux needs
the same treatment. And `verify.sh`'s comment called the warm import "redundant on Linux,
costs about a second" — it is load-bearing there too. Removing the double import when the
upstream fix lands must be verified on both platforms.

**Worth keeping.** Godot's own crash handler symbolizes against the wrong symbol table
and prints nonsense frames — `SDL_GetPerformanceFrequency + 4158164`,
`mvk::SPIRVToMSLConverter::convert`. The MoltenVK frame in that garbage was real and
was dismissed as noise for most of the session. macOS writes an honest report to
`~/Library/Logs/DiagnosticReports/*.ips`; read that instead. `lldb` is no help here,
because it cannot parse the stripped official binary.

---

The entries below date from the M1 look-development session on **2026-07-25**.

---

## ADR-0019 — Motion blur is a gather along Godot's velocity buffer, with four limits stated

**Status:** Accepted. Resolves the `ARCHITECTURE.md` §19 risk "motion blur compositor
effect proves hard".

**Context.** §4 lists motion blur as non-optional and §19 says to prototype it in M1
rather than M10, because it is load-bearing for the realism target and finding out
late is expensive. Godot ships no motion blur at all, so the whole thing is a custom
render pass.

**Decision.** A `CompositorEffect` (`scripts/render/motion_blur.gd`) running a compute
shader (`motion_blur.glsl`) at `EFFECT_CALLBACK_TYPE_POST_TRANSPARENT`. It sets
`needs_motion_vectors`, reads the engine's velocity buffer, and integrates color
along each pixel's velocity vector, scaled by a shutter angle.

**It works, and the uncertain parts turned out fine.** The velocity buffer is
populated and readable from a compositor effect on Godot's Metal backend on Apple
Silicon — which was the actual risk, given that the same host has an unrelated Metal
defect in ADR-0018. Velocity is `RG16F`, holding a screen-space UV displacement since
the previous frame, and it carries both camera and object motion.

**Four things the design did not anticipate.** Each is recorded because each changes
either the code or a later milestone.

### 1. The color buffer cannot be copied, so one effect is two dispatches

A gather blur reads neighboring pixels while writing this one, so it cannot run in
place, and the obvious fix is to copy the color buffer aside first. Godot's color
buffer is created with `SAMPLING | COLOR_ATTACHMENT | STORAGE | INPUT_ATTACHMENT` and
*neither* copy bit, so `texture_copy()` refuses it in both directions:

    Source texture requires the `RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT`
    to be set to be retrieved.

What the buffer can do is be sampled and be written as a storage image, which is
enough: pass one samples color and writes the blurred result to a scratch texture the
effect owns, pass two samples the scratch texture and writes it back. Same bandwidth a
copy would have cost, one extra dispatch, and no dependency on usage bits the engine
does not promise.

### 2. Compositor effects run *before* the TAA resolve — and that is an advantage

Compositor effects sit inside the 3D pass, so the last available callback is still
earlier than Godot's post-processing. That means the blur is applied to a pre-TAA
image, which is the opposite of the textbook order.

Established rather than assumed: a debug mode writes uncorrelated noise every frame,
and the standard deviation of a flat region of the result is measured. If nothing
temporal ran afterwards the noise would survive intact.

| Configuration | Std. deviation of a flat region |
|---|---|
| TAA off | 45.15 |
| TAA on | 16.44 |

TAA averages the effect's output, so it demonstrably runs after it.

The expectation was that this would be a problem. It is the reverse. A fixed tap
pattern leaves visible discrete ghosts along a high-contrast trailing edge, and TAA —
integrating over frames with different jitter — cleans them up for free. The blur
looks *better* with TAA on than off.

**The consequence that matters:** shutter angle must be tuned with TAA in its final
state, because TAA's history accumulation lengthens the effective blur beyond what the
shutter setting alone would give.

### 3. Nothing without motion vectors blurs, and that includes the sky

The sky writes no motion vectors, so it is untouched. Under pure translation that is
correct — the sky genuinely does not move. Under **rotation it is wrong**, and a kart
corners constantly: at 45 deg/s and 60 fps the sky should smear about 7 px at 1080p
and instead stays perfectly sharp.

Not fixed here. The fix is known and bounded — cache the previous frame's
view-projection, and for pixels at the far plane derive velocity by reprojecting the
view ray through it rather than reading the velocity buffer. It needs
`access_resolved_depth` and one more sampler. Scheduled against M10 polish, or earlier
if cornering stills show it reading as a defect rather than as an absence.

### 4. A gather blur cannot smear an object past its own silhouette

This is inherent, not a bug: the pass can only collect color already on screen at
this pixel, so a fast object against a still background blurs *inside* its outline and
stops dead at the edge. A real exposure bleeds the object out over the background.

Demonstrated with a box crossing the view at 300 km/h: its shaded side face smears
convincingly into its lit face, and its silhouette stays sharp.

It does not matter yet. Camera motion gives every pixel on screen a velocity, so the
ego-motion case — which is nearly all of what a racing game shows — has no such
problem. It starts to matter at **M7**, when there are opponents to be passed at a
closing speed. The fix is the standard tile-max / neighbor-max reconstruction filter
(McGuire et al.): dilate the velocity field so pixels *near* a fast object also gather
along its velocity. That is three extra passes and is deliberately not being tuned
against a yellow cube in M1.

**Cost.** Measured by A/B on the same scene at 3840x2160, where the pass is large
enough to rise above frame-time noise; the 1920x1080 figure is that divided by the
pixel ratio, not an independent measurement.

| Configuration | Frame time at 4K | Delta |
|---|---|---|
| No blur | 7.5 ms | — |
| Adaptive taps, capped at 32 | 9.6 ms | +2.1 ms |
| Fixed 16 taps | 9.8 ms | +2.3 ms |
| Fixed 64 taps | 13.9 ms | +6.4 ms |

Roughly **0.5 ms at 1080p**, against the 10 ms rendering budget in §15.

Tap count follows blur length at about one tap per two pixels rather than being fixed.
That is both cheaper and better than a fixed count: a 3 px blur with 32 taps samples
the same texel repeatedly, and a 60 px blur with 16 taps leaves 4 px between taps,
which a hard edge turns into sixteen ghosts.

**Rejected.** Doing the blur after tonemapping in a full-screen shader, which would run
after TAA but would blur display-referred color — highlights would smear gray instead
of staying bright, which is the single most recognizable tell of a cheap motion blur.
Also rejected: implementing the full reconstruction filter now, for the scheduling
reason in point 4.

---

## ADR-0020 — Rendering effects are GDScript, not C++

**Status:** Accepted. Extends `ARCHITECTURE.md` §3, which does not cover rendering.

**Context.** §3 splits the project between C++ for the simulation and GDScript for game
flow, and ADR-0015 records the reasoning. Neither mentions render passes, because at
design time there were none. The motion blur effect forced the question.

**Decision.** Compositor effects and any future render passes are GDScript.

**Reasoning.** The split in ADR-0015 is about where the work happens. For a compositor
effect essentially all of it happens on the GPU: the CPU side is fetching a few RIDs,
building two uniform sets, and issuing two dispatches — call it twenty engine calls per
frame. Moving that to C++ would save microseconds of a budget measured in milliseconds.
What it would cost is the reason the rule exists at all: a shader parameter that can be
changed and seen in one run is worth far more here than execution speed, and look
development is nothing but that loop.

The `.glsl` compute shader is where the actual work is, and it is the same file either
way.

**Consequence.** `src/` stays what §13 says it is — vehicle, track, AI, audio — and
does not accumulate a rendering directory. If a future effect does enough CPU-side work
per frame to show up in a profile, moving that one effect is a contained change,
because the shader does not move with it.

**Also worth recording.** `Viewport.set_measure_render_time(true)` hangs the process
before the first frame on Godot's Metal backend on this host — no output, no crash.
`tools/shots/shoot.sh` therefore reports wall-clock frame time with vsync disabled by
default and puts GPU timestamps behind `--timing=true`. Same class of thing as
ADR-0018: an upstream macOS-specific defect worked around rather than diagnosed.

---

## ADR-0021 — Two of `ARCHITECTURE.md` §4's rendering settings named the wrong feature

**Status:** Accepted. Corrects `ARCHITECTURE.md` §4.

Both were written from a general understanding of what a renderer offers rather
than from what Godot 4 actually exposes, and both would have produced a setting
that quietly does nothing.

### Volumetric fog is not the aerial perspective control

§4 said "Volumetric fog: On, low density — aerial perspective is a strong realism
cue and nearly free." Those are two different features in Godot.

`Environment.volumetric_fog_*` is a froxel volume, and its depth is bounded by
`volumetric_fog_length`, which defaults to 64 m. It is what produces light shafts
and localized haze, and it is genuinely not cheap. Aerial perspective — distant
things going pale and blue — happens over hundreds of meters to kilometers, and
in Godot that is `Environment.fog_*` with `fog_mode = FOG_MODE_DEPTH` plus
`fog_aerial_perspective`. It is nearly free, as §4 claimed; it just is not the
setting §4 named.

**Decision.** Depth fog is on and carries aerial perspective, tuned against a
2 km ground plane. Volumetric fog is a separate, later decision, scoped to light
shafts, and is off until something wants them.

This mattered immediately: the look-dev ground plane was originally 400 m square,
and its edge 200 m from the camera read as a hard horizon line. That is fixed by
geometry (the plane is now 2 km) and by depth fog, and neither is what a 64 m
froxel volume would have touched.

### TAA and FSR2 are alternatives, not a stack

§4 said "AA: TAA, plus FSR2 upscaling on weaker targets". FSR2 *is* a temporal
resolve — it consumes the same motion vectors TAA does and produces an
antialiased image as a side effect of upscaling. Enabling both asks the frame to
be temporally resolved twice.

**Decision.** One or the other. `Viewport.use_taa` is set only when
`scaling_3d_mode` is not FSR2. Native resolution gets TAA; upscaled targets get
FSR2 and no TAA.

**Consequence for M10.** "TAA plus FSR2 on weaker targets" implied the Steam Deck
would get both and therefore look no worse. It gets FSR2 instead of TAA, so the
temporal quality on that target is FSR2's, not TAA's, and it has to be judged on
its own rather than assumed.

**Measured, so the expectation is calibrated.** At 1080p with FSR2 at 0.67 render
scale, the mean absolute difference from a native render is about 2.2 / 255 on
this test scene — small, but the scene is nearly all high-frequency asphalt,
which is the hardest case for an upscaler and the fairest one to judge it on.
FSR2 was not measurably faster here, because a flat plane and a dozen boxes are
not pixel-bound on an M1 Pro. That number only becomes meaningful on the target
it exists for, which is the M10 Steam Deck pass.

---

## ADR-0022 — `LightmapGI` cannot be baked from a script; the bake is driven through the editor

**Status:** Accepted, as a workaround for a missing API. Qualifies `ARCHITECTURE.md`
§4's "LightmapGI baked" and §14's CI plan.

**Context.** §4 chooses baked lightmaps because a racetrack is static geometry under a
fixed sun, which is the exact case baking was made for. The plan assumed the bake could
be automated the way every other tool in this project is.

**It cannot.** `LightmapGI.bake()` is not in the scripting API at all in Godot 4.7:

| Checked | Result |
|---|---|
| `ClassDB.class_get_method_list("LightmapGI")` at runtime | 44 methods, every one a property accessor. No `bake`. |
| godot-cpp's bundled `extension_api.json` for 4.7 | Same. So it is absent from GDScript, `@tool` scripts, `EditorScript` **and** GDExtension. |
| `godot --help` standalone tools | `--script`, `--import`, `--export-*`, `--doctool`, `--dump-*`, `--benchmark`. No bake. |
| Engine source at `4.7-stable` | `editor/scene/3d/lightmap_gi_editor_plugin.cpp` calls `lightmap->bake(...)`; `LightmapGI::_bind_methods` never registers it. |
| Upstream | [godot-proposals#8656](https://github.com/godotengine/godot-proposals/issues/8656) asks for it, still open. |

Headless is doubly impossible regardless: `LightmapperRD` needs a rendering device and
`--headless` has none.

**Decision.** `tools/bake/bake.sh` launches the **GUI editor** and drives the bake
plugin from `tools/bake/editor_bake.gd`: select the `LightmapGI` node — which is what
makes the plugin's `edit()` learn what to bake, and skipping it makes the bake a silent
no-op — locate the toolbar button structurally as the only control owning an
`EditorFileDialog` filtering `*.lmbake`, and emit that dialog's `file_selected`. That is
exactly what a click does, minus the file browser. Finding the button by structure
rather than by its English label is deliberate.

This is editor internals, not an API, and it is labelled as such in the file. The manual
click sequence is in `bake.sh`'s header as the fallback for when it breaks.

**Four things UV2 actually requires**, only the first of which is obvious:

1. `PrimitiveMesh.add_uv2 = true`. A `BoxMesh` has **zero** UV2 vertices without it.
   It also sets `lightmap_size_hint` at a fixed 5 texels/m.
2. `gi_mode = GI_MODE_STATIC`.
3. The scene script must be `@tool`. The bake reads the tree *in the editor*, so a
   scene that builds itself in `_ready()` at runtime has nothing to bake.
4. **`owner` must be non-null.** `LightmapGI::_find_meshes_and_lights` skips any node
   with no owner as "maybe a helper". Everything a `@tool` script builds is unowned, so
   the bake walks past provably correct geometry and reports `BAKE_ERROR_NO_MESHES` —
   *the same error as having no UV2 at all*. This cost three failed bakes before it was
   found.

**The trap worth its own paragraph.** Setting `LightmapGI.camera_attributes` to the
scene's `CameraAttributesPhysical` — the obvious reading of a property documented as
specifying exposure to bake at — renders **every lightmapped surface pure white**. It
does not set the bake's exposure; it writes `baked_exposure` into the `.lmbake`, and the
renderer then scales the lightmap by `camera_exposure / baked_exposure`. Measured
`baked_exposure = 3.2552e-05`, exactly the normalization for f/16, 1/100 s, ISO 100 —
which is also what the camera applies, so the ratio is one and unscaled physical
radiance reaches the tonemapper. Leave it null.

**SDFGI over-bounces relative to the bake**, which matters because §4 makes SDFGI the
iteration fallback. On the same scene: SDFGI puts a strong, uniform mauve over an entire
wall with no falloff, because its 0.2 m cascade cannot resolve a 0.3 m wall. The bake
puts a red band on the soffit nearest the red wall and a gradient across the back wall —
weaker, and spatially correct. **Tuning under SDFGI and shipping baked will shift the
look.** Iterate with it, judge with a bake.

**Bake cost**, on a 7-mesh test scene resolving to one 512x512 slice:

| Quality | Time |
|---|---|
| Low | 0.7 s |
| Medium | 1.2 s |
| High | 3.1 s |
| Ultra | 11.0 s |

Ultra is the first level free of the denoiser's low-frequency mottle, so it is the
default. The first-ever bake took 20.9 s including SPIR-V to MSL compilation.

**What bites at M5, recorded now because a flat test scene hides all of it:**

- **A 1,000–1,500 m track cannot be one mesh.** At the fixed 5 texel/m hint a
  12 x 1500 m surface asks for 7,502 px on its long axis, and `texel_scale = 2.0`
  doubles that past the 4,096 default and near the 16,384 ceiling. `gentrack.py` has to
  segment the surface. `tools/bake/preflight.gd` errors on exactly this.
- **`lightmap_size_hint == 0` silently bakes at 64x64** regardless of mesh size. An
  imported glTF track with no hint bakes at one texel per ~20 m and looks like a
  lighting bug rather than a configuration error.
- **The Metal driver already complains at 11 s** — `ERROR: timeout waiting for fence`
  at ultra but not at high. The bake completes and is correct, but a multi-minute track
  bake risks a driver reset. Same family as ADR-0018.
- **The bake cannot be a CI step**, needing both a GUI editor and a GPU. `preflight.gd`
  can be and should be: it catches every cause of a failed bake headlessly and names the
  offending node.
- **Ambient double-counts.** With a lightmap present, `Environment` sky ambient has to
  be turned off or the sky is counted twice. Exactly the reflection probe bug in
  ADR-0021, in a second place.
- **A still stops being fully described by its command.** `shoot.sh --gi=baked` depends
  on a bake that happened earlier under its own parameters. That is this project's
  reproducibility rule quietly breaking, and it needs an answer before M5 — most likely
  recording the bake parameters into the still's caption the way camera parameters
  already are.

**Bake products are committed, on LFS**, and `*.lmbake` is added to `.gitattributes`
for it. §16 says generated output stays out of version control and the first attempt
here followed that, which was wrong: a scene stores a hard `ext_resource` reference to
its `.lmbake`, so ignoring the bake leaves every fresh clone with a scene that cannot
load. Reproducing one is also not something a build can do — it needs the GUI editor and
GPU time, per this ADR — so the §16 rule's premise, that generated output is cheaply
regenerable, does not hold for bakes.

The size question is deferred rather than answered: this test scene's products are
412 KB, and a full track bake at M5 is a different order of magnitude. If it becomes a
problem the answer is smaller lightmaps or a release-time bake step, not an ignored
reference the repository is silently broken without.
