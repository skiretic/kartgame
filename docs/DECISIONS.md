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

---

The entries below date from the M2 Blender-pipeline session on **2026-07-25**. Four of
the five correct something `ROADMAP.md` M2 or an M2 issue had assumed rather than
checked against the engine.

---

## ADR-0023 — Godot stills are not reliably bit-identical, so image checks need a tolerance

**Status:** Accepted. Corrects `ARCHITECTURE.md` §14's golden-image plan, issue #22's
acceptance criteria, and the *method* used in [ADR-0021](#adr-0021--two-of-architecturemd-4s-rendering-settings-named-the-wrong-feature).

**Context.** M2's gate requires that re-running the generator with identical parameters
produces an identical mesh. Issue #22 extends that to the turntable: "two runs with
identical parameters produce identical images, which makes it a regression check as well
as a review aid." That is a good idea and it turns out not to be available.

**Measured.** The same `shoot.sh` command on the unchanged M1 look-dev scene, repeated:

| Configuration | Runs | Distinct images |
|---|---|---|
| 1920x1080-class, settle 32 | 6 | 2 |
| 1920x1080-class, settle 128 | 6 | **4** |
| 640x640, every effect on | 6 | 1 |
| 640x640, temporal effects off | 6 | 1 |

Where two runs differ, they differ on about **half the frame** by a mean of 2/255 and a
maximum of 18/255. No input changes between runs.

Two things in that table are worth more than the headline. **More settle frames made it
worse**, which says the divergence accumulates rather than converging — so it is a
temporal-accumulation effect, not a first-frame race. And it did not reproduce at all at
640x640 across eighteen runs in three configurations, so it is intermittent and appears
to be sensitive to resolution and machine load. Root cause is not diagnosed; it is
recorded as an observation rather than explained, and issue #102 carries the
investigation.

**Decision.**

1. **The mesh determinism gate is a byte hash, and it holds.** `genkart.sh --check`
   generates twice and compares SHA-256. This is the M2 acceptance criterion and it is
   exact — the glTF *and* the Cycles normal bake are byte-identical between runs.
2. **Image comparison uses a tolerance, never a hash.** `tools/shots/compare.gd` reports
   the changed-pixel count, the maximum channel delta and the mean delta, and passes below
   stated thresholds. A hash-based golden-image test would fail roughly one run in six for
   no reason, and a flaky gate gets disabled rather than fixed. It is written in GDScript
   rather than Python so it adds no dependency the project does not already have.

   **The statistic that works is magnitude, not area** — which was not the first guess.
   Measured across three pairs:

   | Pair | Changed | Max delta | Mean over changed |
   |---|---|---|---|
   | Same command twice, identical case | 0% | 0 | — |
   | Same command twice, drift case | 49.7% | 15 | 2.73 |
   | Asphalt ground vs checker ground | 51.9% | 255 | 9.82 |

   The drift and a deliberate whole-ground material swap touch the same *fraction* of the
   frame, about half of it, so no fraction threshold can separate them. Magnitude
   separates them cleanly. Hence thresholds of max delta 24 and mean 4.0, with the
   fraction limit off by default.

   **Stated because it is a real limit:** a change smaller than the noise floor is not
   detectable this way. A light 1% brighter looks exactly like a repeat of the same
   command. That is the concrete cost of not knowing the root cause, and the reason
   issue #102 is worth doing rather than living with.
3. **The turntable turns the temporal effects off by default.** Judging silhouette and
   scale is not a question about temporal resolve, and it removes most of the variance.

**The uncomfortable part, stated rather than buried.** ADR-0021 concluded that
`fog_light_energy` is inert while `fog_aerial_perspective` is 1.0, and its evidence was
"verified by sweeping energy across 1, 300 and 900 and getting byte-identical renders."
Byte-identical renders were not a safe signal to draw that from — about five runs in six
are byte-identical whether anything changed or not. The conclusion is still believed
correct, and it is independently supported by what the feature does, but the method that
established it was unsound and would not have detected a small real difference. Every
future look-dev A/B in this project goes through the tolerance comparison, which reports
a number, instead of through `cmp`, which reports a boolean that is right most of the
time.

---

## ADR-0024 — The kart is unwrapped at the track's texel density, not a prop's

**Status:** Accepted. Resolves an ambiguity in `ARCHITECTURE.md` §5 item 2.

**Context.** §5 item 2 fixes texel density at "512 px/m for track surface, 256 px/m for
props" and says to hold it everywhere, because mismatched density is the most common tell
in amateur work. Issue #18 says to unwrap the kart "at the texel density standard fixed
in M1". M1 fixed 512 px/m — for the track. A kart is neither a track surface nor a prop,
and the standard as written does not say which it is.

**Decision.** The kart is unwrapped at **512 px/m**, the track figure.

**Reasoning.** The density standard exists to serve the camera, and the cockpit camera
sits closer to the kart than to anything else in the game — ARCHITECTURE.md §7 puts its
near plane at 0.02–0.05 m and gives the interior its own LOD precisely because the player
looks at it from centimeters away. A prop at 256 px/m is something seen at trackside
distance. Applying the prop figure to the object the player is sitting inside would put
the lowest density in the game on the most closely examined surface, which inverts the
intent.

**Cost, checked rather than assumed.** The kart's total surface area is roughly 10 m². At
512 px/m that is about 2.6 Mpx of texture, which fits inside a single 2048² atlas
(4.2 Mpx) with room to spare. So the higher density is affordable and the decision costs
nothing but the choice.

**Consequence.** `params.texel_density` is 512.0 and the unwrap derives island scale from
it. A future prop pass gets its own 256 px/m and the two must not be averaged into one
compromise number, which is the failure mode §5 is warning about.

---

## ADR-0025 — The kart carries no lightmap UV2, because a kart moves

**Status:** Accepted. Corrects `ROADMAP.md` M2 and issue #18.

**Context.** `ROADMAP.md` M2 lists "UV unwrap, lightmap UV2, normal bake, LOD chain via
decimation", and issue #18 asks for "a second UV channel for lightmap baking" with "no
overlapping islands in UV2, which silently corrupts a bake".

**A kart cannot be lightmapped.** `LightmapGI` bakes light into a texture parameterized by
a mesh's second UV channel, which requires the mesh to be in a fixed place relative to the
light. The kart is the one object in the game that is never in a fixed place. Godot's
answer for moving geometry is `LightmapGI.generate_probes_subdiv`: the bake also stores a
grid of light probes, and dynamic objects are lit by interpolating those. That path reads
no UV2 at all.

Checked, not assumed: `LightmapGI` exposes `generate_probes_subdiv`, and
`GeometryInstance3D.GIMode` has a `GI_MODE_DYNAMIC` distinct from `GI_MODE_STATIC`.

**Where the claim came from.** `ARCHITECTURE.md` §11's table is right — it lists "UV
unwrap, normal bake, LOD chain, glTF" for `genkart.py` and lists lightmap UV2 only for
`gentrack.py`, which is the script whose output genuinely is static. The ROADMAP entry and
issue #18 picked up UV2 from the track's list. It is a copy-paste, not a design decision,
and building it would have cost an unwrap pass and four bytes per vertex for a channel
nothing reads.

**Decision.** `genkart.py` generates UV1 only. Issue #18 is rescoped to the UV1 unwrap and
the texel-density check, and its UV2 criteria are struck.

**The trap found while establishing this, which is the more useful half.** An imported
kart arrives with `gi_mode = GI_MODE_STATIC`, because Godot's scene importer defaults
`meshes/light_baking` to 1, which is Static. So out of the box the kart is a mesh *flagged
for lightmap baking* that *has no UV2* — which is exactly the configuration ADR-0022
recorded as reporting `BAKE_ERROR_NO_MESHES`, indistinguishably from having no meshes at
all. The fix is not to add UV2. It is to set the kart's GI mode to Dynamic so it is lit by
probes, and that has to happen somewhere, or the first track bake at M5 fails with an
error message pointing at the wrong thing. Issue #103 carries it.

---

## ADR-0026 — Godot generates its own mesh LODs; a decimated glTF chain is not read

**Status:** Accepted. Corrects issue #20.

**Context.** Issue #20 asks for "automatic LOD generation by decimation, ratios chosen
against on-screen size" and, as an acceptance criterion, "verify Godot picks up the chain
on import". It cannot, and the second half of that sentence is not achievable as written.

| Checked | Result |
|---|---|
| `GLTFDocument.get_supported_gltf_extensions()` | 13 extensions, no `MSFT_lod`. glTF's LOD extension is not supported. |
| Scene import options on a generated `.glb` | `meshes/generate_lods=true`, on by default. |
| `ImporterMesh` | Has `generate_lods`, `get_surface_lod_count`, `get_surface_lod_size`. |

So Godot builds its own LOD chain at import time by simplifying each surface, and there is
no channel by which a chain decimated in Blender could be handed to it. Exporting four
decimated copies of the kart would produce four separate meshes that Godot would then
generate LODs *for*, which is worse than doing nothing.

**Decision.**

1. **Distance LOD is Godot's**, from `meshes/generate_lods`. It is free, it is already on,
   and it is per-surface rather than per-object, which is better than whole-kart
   decimation would have been.
2. **The interior LOD is a visibility range, not a decimation level.** ARCHITECTURE.md §7
   wants the cockpit interior culled from the chase view. That is
   `GeometryInstance3D.visibility_range_begin` / `_end` on the interior node, which is why
   `kartlib` builds the interior into its own group rather than merging it into the body
   mesh. A decimated copy would not have helped: the requirement is "does not render from
   that camera", not "renders more cheaply".
3. **Decimation stays in the pipeline but changes purpose.** `params.lod_ratios` remains,
   for the case Godot's simplifier handles badly. Thin swept tubes are the obvious
   candidate — a 30 mm tube at 12 segments has little to give up before it collapses — so
   the ratios exist as an override to reach for if the automatic chain visibly breaks the
   frame, not as the primary mechanism. Issue #20 is rescoped accordingly.

**Worth stating plainly:** this milestone's LOD work got smaller because the engine
already does it. That is the ADR-0009 trade working as intended — the cost of Godot is a
fidelity ceiling, and the return is that a chunk of planned work turns out to be a
checkbox. Verifying that is cheaper than building it.

---

## ADR-0027 — Generated assets cannot carry Godot import settings

**Status:** Accepted. A structural consequence of `ARCHITECTURE.md` §16, found in M2 and
due at M5.

**Context.** §16 gitignores `assets/generated/` because it is reproducible from tools. A
Godot asset's import settings live in a sibling `.import` file, so ignoring the directory
ignores those too — they are regenerated at defaults on every fresh clone.

For the kart that is nearly harmless: the defaults are wrong in one place (ADR-0025's
`gi_mode`) and that is fixable in the scene that instantiates it. For the **track at M5 it
is not**, because the track needs settings that are emphatically not the defaults: static
lightmap baking, a `lightmap_texel_size` matched to the density standard, and the
segmentation ADR-0022 already flagged. A track that silently imports at
`lightmap_size_hint = 0` bakes at 64x64 and looks like a lighting bug rather than a
configuration error — ADR-0022 recorded exactly that.

**Decision.** Import configuration for generated assets is version-controlled in
`project.godot`'s `[importer_defaults]` section, or applied by a tool at generation time —
never left in an ignored `.import` file. Which of the two, and for which assets, is
deferred to M5 where the real requirement is; issue #104 carries the decision so it is a
ticket rather than a sentence in an ADR.

**Note.** This is the same shape as ADR-0022's finding about `.lmbake` files, and the
opposite resolution. There, the generated product had to be committed because a scene held
a hard reference to it and it could not be cheaply reproduced. Here the product stays
ignored and only its *configuration* is committed. The rule §16 states — generated output
stays out of version control — keeps needing a qualifier, and the qualifier is always
whether something other than the generator depends on the artifact existing.


## ADR-0028 — A KZ engine has no cooling fins, and issue #116 asked for some

**Status.** Accepted. Supersedes the reasoning in `powertrain.py`'s deleted `RIB_COUNT`.

**Context.** Issue #116 found that the M2 powertrain read as a stack of primitives rather
than as castings, and listed what was wrong. Its first item asked for cylinder fins that
"taper, wrap the barrel's curve, and vary in depth front to back", against the four flat
uniform plates the module built.

Both are wrong, in opposite directions. The class this project models is KZ — ADR-0011 —
and a KZ 125 is **water-cooled through the cylinder, the head and the crankcase**. It has
no cooling fins at all. What the module had was four full-perimeter plates standing 10 mm
proud on every side of a box barrel, which in a render reads as exactly the thing the
issue then asked for more of: an air-cooled 100 cc barrel. The reference photographs make
the contrast plain — an installed Vortex KZ cylinder is a smooth sand casting, round in
plan, on a square base flange, and a Kosmic TS28 next to it is a fin stack.

The module's own docstring had this right and its geometry did not. It said "deliberately
*ribs*, not cooling fins ... a KZ engine is water-cooled ... so it has no fins", and then
built four objects that were fins in everything but name. Prose asserting a constraint is
not the same as geometry honoring it, and nothing in the pipeline compares the two.

**Decision.** The cylinder is a **round water jacket on a square base flange**, with no
fins and no ribs. Casting character comes from the shapes that are actually there: the
flange step and its four stud nuts, the draft on the jacket, the exhaust port flange, and
the diameter step where the head sits down inside the barrel's crown. Surface-level casting
texture is issue #19's normal bake, not geometry.

**Consequence.** #116's cylinder bullet is not implementable as written and the issue is
amended rather than followed. The acceptance evidence for that item is the reference, not
the issue text — which is ARCHITECTURE.md §5 item 3 doing the job it exists for.

**What this does not settle.** Nothing here checks that a module's commentary matches its
output. The winding gate catches inverted faces because signed volume is computable; there
is no equivalent for "this docstring says no fins". Four fins survived two milestones
behind a docstring saying there were none.


## ADR-0029 — The radiator core sits in the plane of a second seat's back

**Status.** Accepted.

**Context.** Kart practice states the radiator's mounting angle as **55° to the
horizontal** — 45° below 20 °C ambient, up to 60° above 30 °C, adjusted in 5° steps and
checked by holding a phone against the core. That figure is well sourced and, on its own,
it is not enough to build from: it names an angle without naming an axis, and three
different axes are consistent with it. Two versions of the radiator were built from it and
both were wrong, one leaning the core sideways out over the sidepod with the fin face
pointing outboard, and one doing the same with the two core dimensions swapped.

The correction, from photographs the project owner supplied (R4 and R5 in
`docs/REFERENCES.md`): the rake is about the kart's **lateral** axis, not its fore-and-aft
axis. The big fin face points **forward**, the way the driver does, reclined backwards.

Which makes the angle recognizable. `seat_back_angle` is 0.610 rad — 35° from vertical,
i.e. 55° from horizontal. **It is the same angle.** The radiator core occupies the plane a
second seat's back would occupy, immediately outboard of the driver's, and everything about
its orientation follows from that one sentence.

**Decision.** The radiator's rake is derived: `seat_back_angle + radiator_rake_delta`, with
the delta defaulting to zero. `radiator_lean` and `radiator_yaw` are deleted. The core is
built in a frame of its own — local +x the face normal, +y inboard, +z up the slant — and
transformed into the world once; the basis is right-handed so the transform cannot invert
the winding.

**Consequence.** The 55° is stated once, in a parameter that already existed for the seat,
so the two cannot drift. Ambient-temperature tuning is one `--set` on the delta. The three
radiator extents stop meaning axis-aligned things and their docstrings say so explicitly,
because the ambiguity in `radiator_width` alone has now produced three wrong builds.

**What this costs.** Correctly placed, the core needs volume that `bodywork_sidepod_r` and
`cockpit.py`'s shifter lever currently occupy — 10 and 3 intersecting part pairs. Neither
is fixed by moving the radiator: pushing it outboard far enough to clear the lever leaves
it hanging past the side bar on 245 mm of bracket. The sidepod is issue #110 and two of its
intersections with the engine predate this work entirely.

**The general lesson, which is the reason this is an ADR and not a comment.** A sourced
scalar is not a sourced geometry. "55° to the horizontal" was quoted accurately from a good
source, recorded in `REFERENCES.md`, and still produced two wrong parts, because the axis
it applies to was never written down. Any angle in this project that does not name its axis
and its sign should be treated as unbuilt.


## ADR-0030 — Jolt is named in `project.godot`, because "DEFAULT" is not Jolt

**Status.** Accepted.

**Context.** `ARCHITECTURE.md` §1 says Jolt owns collision, and §19 names the
force-application boundary between a custom vehicle and Jolt's stepping as the
least-charted part of the plan. All of that was written on the assumption that
Godot 4.4+ having Jolt "built in" meant Jolt was in use. It was not.
`physics/3d/physics_engine` was left at `DEFAULT`, and on 4.7.1 the default 3D
engine is still `GodotPhysics3D`. Every physics measurement taken before M3a was
taken against the wrong engine — which happens to be harmless, because none had
been taken.

The engine in use is not directly readable: `PhysicsServer3D.get_class()` returns
the wrapper's name under either engine, `ClassDB.class_exists("JoltPhysicsServer3D")`
is false, and Jolt's own `physics/jolt_physics_3d/*` settings are registered
whether or not it is active. What does distinguish them is the RID leak report at
shutdown, which names the concrete types — `GodotBody3D` and `GodotSpace3D`
against `JoltBody3D` and `JoltSpace3D`. That is how this was found, by accident,
in an unrelated probe.

**Decision.** `physics/3d/physics_engine` is set to `"Jolt Physics"` explicitly,
and `tools/verify/verify_extension.gd` asserts two things: that the setting says
so, and that the *running* server behaves like Jolt. The behavioral half is a
fresh space's default solver iteration count, which is **8 under Jolt and 16
under GodotPhysics3D** — the only difference found among the eight space
parameters.

**Consequence.** A future engine release that changes Jolt's default iteration
count will fail this check. That is the intended direction of the error: the
assertion is a claim about the running server, so it should break loudly and be
re-measured rather than quietly stop meaning anything.

**What this does not settle.** Nothing here checks that a setting named in
`project.godot` is honored at all — if the engine silently fell back, only the
behavioral half would catch it, and only for this one parameter. The general
problem, that a project setting is a request rather than a fact, has no cheap
general answer.


## ADR-0031 — The M3a vehicle applies its own forces, in newtons

**Status.** Accepted. Scoped to M3a; M3b's solver supersedes the file this
describes, not the reasoning.

**Context.** ROADMAP M3a drives the M2 kart mesh with Godot's built-in
`VehicleBody3D`, deliberately throwaway, to prove that a force-driven body, a
120 Hz `_physics_process` and Jolt's stepping agree. Driving it through
`VehicleBody3D.engine_force` and `.brake` did not survive measurement.

`engine_force` is documented as a force and behaves as a wheel torque that then
passes through the wheel's own friction solver. Calibrated against two steady
states it implied two different conversion factors — about 3.1x the requested
force at one throttle setting and 0.2x at another — so there is no constant to
write down. A separate defect compounded it: Godot applies a project-default
`linear_damp` of 0.1 to every rigid body, worth a full 1 m/s² at 10 m/s, which is
about a quarter of the kart's acceleration there and was silently absorbing the
difference between the model's predicted 0-100 time and its measured one.

**Decision.** `engine_force` and `brake` stay at zero. Drive and braking forces
are applied directly with `apply_force` at each wheel's contact patch, in
newtons. `linear_damp` and `angular_damp` are set to `DAMP_MODE_REPLACE` at zero,
and drag is the one explicit `DRAG * v²` term the file documents.

**Consequence.** Every constant in `kart_debug_vehicle.gd` is a real quantity
checkable against §6.4 by hand, and the measured figures land where they should:
136.9 km/h top speed, 0-100 km/h in 3.40 s, 1.98 g mean braking from 90 to
20 km/h. Applying force at the contact patch rather than at the center of mass
also gets squat and dive out of the geometry for free. And the shape is the one
M3b needs — per-wheel forces on a Jolt body from `_physics_process` — so M3a now
proves the integration M3b depends on rather than proving `VehicleBody3D`'s.

**What it costs.** Longitudinal tire slip. The wheel model no longer limits drive
force, so this kart cannot spin its wheels; `TRACTION_LIMIT` stands in for a
friction circle until there is a tire model to ask.

**The one figure still outside its band, stated rather than tuned away.**
Sustained lateral acceleration measures **1.76 g** against §6.4's 2.0-2.5 g. It
is not a grip constant that is too low — raising it makes the kart **roll over**
instead of cornering harder. The kart's own geometry sets that ceiling: half the
rear track over the center-of-mass height, 0.5925 / 0.23, is 2.58 g, and
`VehicleWheel3D` reaches it before the tires let go. Closing the gap needs the
inside-rear lift and caster jacking of §6, which is M3b — not a larger number
here.


## ADR-0032 — The force-application boundary, measured

**Status.** Accepted. This is issue #28, and `ARCHITECTURE.md` §19's first risk.

**Context.** §19 says applying per-wheel forces to a `RigidBody3D` from
`_physics_process`, while substepping internally at 240 Hz, is the least-charted
part of the plan, and that M3b is built on sand if Godot's integration hook and
Jolt's stepping do not agree. That was written as a risk because nobody had
measured it. `tools/verify/integration_probe.gd` measures it: one body per
candidate approach, in free space, no gravity, no contacts, no damping, 100 N on
10 kg for 120 ticks, against the analytic answer.

    analytic continuous     v  10.000000   x   5.000000
    discrete symplectic     v  10.000000   x   5.041667
    discrete explicit       v  10.000000   x   4.958333

    physics_process, force     v   9.999998   x   5.041668   symplectic
    integrate_forces, force    v   9.999998   x   5.041668   symplectic
    4 sub-applications of F/4  v   9.999998   x   5.041668   symplectic
    impulse F*dt each tick     v   9.999998   x   5.041668   symplectic
    one force, tick 0 only     v   0.083333   x   0.083333   —

**What the numbers say.**

1. **The integrator is symplectic Euler.** Position advances with the *new*
   velocity, so after N steps it is `a·dt²·N(N+1)/2`, not `N(N-1)/2`. The two
   differ by one step of `a·dt²` — 83 mm over this run — and the measurement
   lands on the first to within 1.3 µm, which is single-precision drift over 120
   additions and not a third possibility. A solver that predicts its own motion,
   as the tire model will when it substeps, has to use the same scheme or its
   prediction disagrees with the body it is predicting.
2. **`_physics_process` and `_integrate_forces` are the same thing** for force
   application. Identical to the last digit. `_integrate_forces` is still where
   contact state must be read, but there is no accuracy argument for moving force
   application into it, and `_physics_process` is where input arrives.
3. **Applications accumulate within a tick.** Four calls of `F/4` equal one call
   of `F`. This is the answer M3b actually needed: a 240 Hz solver cannot make
   the engine step twice, so its substeps sum their forces and apply the total
   once. Nothing is lost by that — it is what "apply a force for one tick" means.
4. **`apply_central_impulse(F·dt)` equals `apply_central_force(F)`.** Interchange-
   able, so the choice is about which unit the calling code is written in.
5. **Forces are not sticky.** One application at tick 0 produced exactly one
   step's worth of velocity, `a·dt` = 0.0833 m/s, and nothing after. Every tick
   that wants force must apply it again. A solver that computed forces only when
   its inputs changed would silently coast.

**Decision.** M3b's solver substeps internally at 240 Hz, accumulates the forces
and torques its substeps produce, and applies the totals once per 120 Hz tick
from `_physics_process`, in newtons, at the contact points — which is what
`kart_debug_vehicle.gd` already does at M3a per ADR-0031, and is therefore
already under test by `tools/verify/drive.sh`.

**Consequence.** §19's first risk is closed as measured rather than as mitigated.
The probe stays in the tree so the same four questions can be re-asked of a
future engine version in one command, which matters because every answer here is
an engine behavior and not a specification.

**What this does not settle.** Everything with a contact in it. This measures a
body in free space; the wheel raycasts, the friction solver and the resting
contact behavior are not covered, and a force applied at a contact point that
Jolt is simultaneously resolving is a harder question. It also does not address
`max_physics_steps_per_frame` (8 by default): under a frame-rate collapse Godot
runs several physics ticks per frame and then *stops*, so simulation time falls
behind wall-clock. That is invisible in a deterministic replay, which counts
ticks, and very visible to a driver — it belongs to M3b's telemetry.

**Note on precision.** Velocity came back as 9.999998 rather than 10.0 after 120
single-precision additions. The engine is a single-precision build, which
`KartCore.build_info()` reports and `verify_extension.gd` asserts. That is fine
for reproduction on one binary, which is the only thing §8 promises, and it is
the mechanism behind why `state_hash.h` quantizes before hashing rather than
comparing bits.


## ADR-0033 — The contact boundary, measured, and the lever arm it caught

**Status.** Accepted. This is the half of the force-application boundary
[ADR-0032](#adr-0032--the-force-application-boundary-measured) explicitly left
open, and it **corrects ADR-0031 and CLAUDE.md on a point both stated as
settled**.

**Context.** ADR-0032 measured a `RigidBody3D` in free space and closed
`ARCHITECTURE.md` §19's first risk. Its own "what this does not settle" section
named what was left: "the wheel raycasts, the friction solver and the resting
contact behavior are not covered, and a force applied at a contact point that
Jolt is simultaneously resolving is a harder question." Every one of those is
load-bearing for issue #31. `tools/verify/contact_probe.gd` asks them the same
way — one body per question, an analytic prediction printed beside every
measurement.

**The finding that matters most was not one of the questions.**

`apply_force`'s `position` argument is **an offset from the body's origin, in
global coordinates**. It is not an offset from the center of mass. Godot's
documentation says so in one sentence. This project believed the opposite,
recorded it in CLAUDE.md's trap list as a hard-won fact, wrote it into ADR-0031,
and implemented it in `kart_debug_vehicle.gd`.

The measurement is decisive and needs no interpretation. Move a body's center of
mass to (0, 0.5, 0), pass an offset of exactly zero, and apply a force:

    spin: offset zero, com +0.5y
      measured dw over one tick              ( -0.00000010   0.00000000   0.37878785)
      A: world offset from center of mass    (  0.00000000   0.00000000   0.00000000)   err 0.378787845
      B: world offset from body origin       (  0.00000000   0.00000000   0.37878790)   err 0.000000118

A center-of-mass convention predicts no rotation at all. The body rotates.

**What it cost.** Passing `contact - to_global(center_of_mass)` applies the force
at `origin + contact - com`, so the torque about the center of mass is

    (contact - com) x F  +  (origin - com) x F

The second term is not noise. The kart mesh's origin sits on the ground and so
does every contact patch, so those two arms have the *same* vertical component,
and the **pitch and roll moments came out exactly doubled — at any center-of-mass
height**. Measured on the kart's own geometry, 5.079 rad/s of pitch per tick
where 2.540 was intended, a ratio of 1.99996. Yaw was worse than doubled rather
than doubled: the longitudinal arms differ in sign, so a lateral tire force
produced a yaw moment that should not exist.

**What fixing it changed, measured rather than assumed.** `tools/verify/drive.sh`
before and after, same scenarios, same constants:

| | Before | After | KZ reference (§6.4) |
|---|---|---|---|
| Top speed | 136.9 km/h | 136.9 km/h | 135-145 |
| 0-100 km/h | 3.40 s | **2.88 s** | 3.0-3.5 |
| Braking, 90-20 km/h mean | 1.98 g | **1.53 g** | 1.5-2.0 |
| Lateral, sustained | 1.76 g | **1.84 g** | 2.0-2.5 |

**ADR-0031's headline conclusion survives, and is refined.** It said the 1.76 g
lateral figure could not be fixed with more grip because the kart rolls over
first, and that closing the gap needs the inside-rear lift and caster jacking of
M3b. The doubled roll moment was worth 0.08 g of the shortfall — real, and a
small fraction of it. The gap is still mostly what ADR-0031 said it was.

**What is no longer true is every other number in ADR-0031.** M3a's constants
were calibrated against a doubled pitch lever, so 0-100 and braking both moved
when it was corrected, and 0-100 is now outside its band. Those constants are
**not being re-tuned**: `kart_debug_vehicle.gd` is deleted at M3b, `ARCHITECTURE.md`
§19 names unbounded vehicle tuning as a risk, and re-tuning a stand-in to restore
a pretty number is how that risk lands. The numbers above are recorded as what a
corrected M3a stand-in does. M3b's solver is calibrated against the corrected
boundary and its figures are the ones that count.

**The seven answers.**

1. **Resting contact is exact; the read-out is not.** Boxes and spheres at 17.5,
   175 and 1750 kg rest at exactly their geometric height — penetration
   0.00000000 m, residual speed under 3e-9 m/s — at 120 and 240 Hz. A wheel
   spring may compute its length from a raycast with no rest-height correction.
   But `PhysicsDirectBodyState3D.get_contact_impulse` reports **1.000371x** the
   true impulse and **lags one tick**, invariant across 100x mass, 4x load,
   60-240 Hz, gravity on or off, and 1- against 4-point manifolds. The motion is
   ground truth; the API is not. Normal load comes from the spring, never from
   there.
2. **The contact is a clean unilateral constraint.** Below `m*g` the measured
   acceleration is 0.00000 against a free-body prediction of `(k-1)g`, and the
   contact supplies exactly the balance. Above it the contact supplies **zero on
   every tick** and the acceleration is `(k-1)g` to five digits. The body leaves
   within 1 tick at `k >= 1.5` and 5 ticks at `k = 1.01`. A per-wheel spring may
   apply its full computed force unconditionally.
3. **Jolt's friction must be switched off on the chassis.** A custom tire model
   owns every tangential newton, so anything Jolt contributes is double-counted.
   `physics_material_override.friction = 0.0` on the vehicle body is sufficient
   whatever the ground is, because the combine rule is measured to be
   `min(a, b)` — 0.25/0.50 gives 0.24998, 1.00/0.50 gives 0.49999, and only
   `min` fits. Two further reasons: at high mu the friction solver is only good
   to about 10%, and a lateral force at the patch tips the body onto a corner and
   inflates the normal load to 8x `m*g`.
4. **Raycasts are exact and cheap, and one case is a trap.** Distance and normal
   are correct to 0.00000000 over 240 ticks at 60 m/s — no tunneling, no speed
   dependence. `exclude` is required, because a ray from outside a collider hits
   its own body 240 times out of 240, and it is free: 1.23 us against 1.26 us
   over 20,000 rays. The trap: **a ray originating below a surface returns no hit
   at all**, and `hit_back_faces` does not change that. `hit_from_inside` returns
   a hit at distance 0.000000 with a normal of (0,0,0), so it is not a depth
   query. A wheel buried in a curb reports "no contact" exactly when it is
   deepest, so the spring must latch its last valid normal and depth, and issue
   #31's "curb strikes do not tunnel" cannot be satisfied by raycasts alone.
5. **Angular response is exact once the offset convention is right.**
   `alpha = I^-1 (r x F)` to 4e-7 rad/s for a diagonal tensor, and to 4.4e-7 for
   a genuinely non-diagonal world tensor — body rolled 45 degrees, `inertia` set
   to (2, 5, 11), matched against `R I^-1 R^T tau`. The offset is a **world**
   vector; reading it as body-local is off by 1.70 rad/s in the rolled case.
6. **ADR-0032's conclusion 3 survives a contact unchanged.** Four applications of
   `F/4` against one of `F`, on a body resting on a plane, both sliding and
   crossing the airborne transition: identical to all nine printed digits. The
   240 Hz solver may accumulate its substep forces and apply the totals once per
   120 Hz tick, on the ground as well as in the air.
7. **`max_physics_steps_per_frame` clamps and does not bank.** Under an induced
   stall Godot ran exactly 8.0000 ticks per frame against an owed 12.35, and
   simulation time advanced at 0.6476 of wall-clock against an analytic 0.6476.
   It never catches up. The kart runs in slow motion under a frame-rate collapse
   and **a tick-counting replay cannot see it**, so M3b's telemetry must publish
   `simulated_ticks / (wall_seconds * hz)`. It is the one defect invisible to
   every other check in this project.

**Consequence.** Issue #31 has its answers before a line of suspension code runs.
The probe stays in the tree so the same questions can be re-asked of a future
engine version in one command.

**What this does not settle.** `continuous_cd` on the chassis is very likely
inert — Jolt's movement threshold is 0.75 of the body's own extent per step,
about 0.41 m for the chassis box, and the kart covers 0.32 m per step at its top
speed — but that is an argument, not a measurement, and it is filed rather than
concluded. Kart-to-kart contact is untouched and belongs to M7.


## ADR-0034 — §6.4's lateral band was a peak figure being read as a sustained one

**Status.** Accepted. This is issue
[#120](https://github.com/skiretic/kartgame/issues/120), and it changes a
validation target rather than a model.

**Context.** M3b's solver measures 1.86 g of best sustained lateral acceleration
against `ARCHITECTURE.md` §6.4's 2.0-2.5 g. ROADMAP recorded the figure outside
its band deliberately, and #120 named three possible levers: raise grip, change
the chassis geometry, or decide the target is wrong.

The first two were already closed. Grip is measured not to work — raising
`peak_friction` from 2.10 to 2.70 buys 0.265 g with collapsing returns, which
re-measures ADR-0031's conclusion with the real solver. Geometry is closed by
regulation: FIA Karting Art. 8.1.1 caps overall width at 1400 mm and
`tools/blender/kartlib/params.py:56` already sets the rear track to exactly that,
so there is no legal width left to find.

**The third lever did not need an external source to settle. The project
contradicts itself.** The same two constants are labeled two incompatible ways:

    docs/ARCHITECTURE.md:187   | Peak lateral acceleration | ~2.0-2.5 g |
    src/core/kz_reference.h:43 // Steady-state skidpad, slicks, dry asphalt.
                               LATERAL_G_MIN = 2.0    LATERAL_G_MAX = 2.5

The document says peak. The header the document feeds says steady-state skidpad.
Every measurement taken against it — M3a's, M3b's, ADR-0033's before-and-after
table — is a sustained number. The project has been asserting a peak figure
against a sustained measurement for two milestones.

**And read as sustained, the band asks for a state the kart cannot occupy.**
Recomputed from `src/core/chassis.h`'s twenty-one lump table:

    mass 175.000  com (0.0409  0.2197  0.0838)
    rollover left   2.4336 g
    rollover right  2.8061 g

The band's top edge, 2.5 g, is **above the left-turn tipping point**. A kart
sustaining it is on two wheels. Nothing sustains more lateral acceleration than
it tips at, so the upper half of a sustained 2.0-2.5 g band is unreachable by
construction, in one of the two directions, on any legal kart geometry.

**Decision.** §6.4 carries two lateral rows instead of one: a **sustained**
band of 1.5-2.0 g, which the skidpad scenario is judged against, and a
**transient peak** band of 2.0-2.5 g, which only a transient probe may be judged
against. `kz_reference.h` splits its constants to match and each consumer is
repointed at the one it actually measures.

**Why a peak may exceed the tipping threshold and a sustained figure may not.**
Tipping is not instantaneous. Roll inertia about the outside contact line is
`Izz + m d^2` = 13.20 + 175 x 0.5782^2 = 71.69 kg m^2, and the excess roll moment
at lateral `a` is `m (a - t) g h`:

| held at | 0.2 s | 0.5 s | 1.0 s | to the point of no return |
|---|---|---|---|---|
| 2.5 g | 0.40 deg, 4 mm lift | 2.5 deg, 23 mm | 10.0 deg, 93 mm | 2.60 s |
| 2.8 g | 2.2 deg, 21 mm | 13.8 deg, 128 mm | 55.2 deg, 439 mm | 1.11 s |

2.5 g through a corner entry costs 0.4 degrees of roll. 2.5 g around a skidpad
puts the kart on its side. That is the whole distinction, computed from this
kart's own numbers.

**Why published kart figures cluster where they do.** Practitioners make the same
split explicitly — "to have a kart that actually sustains 2G's for a turn is very
rare. Most of the time you have spike g's that averaged do produce between
1,6-2g's", with transients "sometimes up to 2,5G's". There is also a measurement
artifact that inflates every logged kart lateral number: a MyChron or Alfano
mounts on the steering wheel, roughly 0.6 m ahead of the center of mass, so the
signal is `a_y + yaw_accel * x` — 10 rad/s^2 of turn-in yaw acceleration
contributes 0.6 g the chassis never felt. For scale, a 1983 ground-effect
Formula 1 car skidpads at 1.68 g.

**Where the sustained band's edges come from, and how solid each is.** The top,
2.0 g, is anchored on this kart's own geometry: it sits 18% below the 2.4336 g
left-turn threshold, so every value in the band is sustainable with margin, which
the old band's top was not. That is the strong half. The bottom, 1.5 g, is
weaker and is recorded as weak: it comes from Lot and Dal Bianco's
lab-measured, telemetry-validated kart solving to 1.456-1.477 g steady state, and
this project has reached that only through a third-party re-encoding of a
paywalled paper nobody here has read. It is a plausibility floor, not a
measurement. `kz_reference.h` already says these ranges are a plausibility gate
rather than a fit target and that is the spirit the band keeps.

**What this does not claim.** That the model is now correct. 1.86 g landing
inside a restated band is a target being fixed, not a solver being validated, and
the two should not be confused when reading the M3b table.

**A correction to ROADMAP's own diagnosis, which was directionally right and
quantitatively loose.** M3b records that "the kart runs out of ability to use
grip at its own rollover threshold". At 1.86 g the kart is at **76% of its
tipping point**, which is not close. What actually binds is load sensitivity
amplified by lateral transfer, and the rollover threshold enters as the
*denominator* of the transfer term — a narrower or taller kart loses grip to
transfer faster at the same `a`. Rollover only becomes the binding constraint
above roughly `peak_friction` 2.63, which is why the returns collapse where they
do. The ROADMAP sentence is restated accordingly.

**What it opened.** Four tickets, filed rather than left in this prose:
[#128](https://github.com/skiretic/kartgame/issues/128), because `tire.h`'s
`peak_friction = 2.10` was justified by pointing at the very band this ADR
relabels, and every deformable-surface grip multiplier is a quotient of it;
[#129](https://github.com/skiretic/kartgame/issues/129), because three
disagreeing rollover thresholds turned up while checking this one, and the one in
`chassis_flex.h` cannot see the lateral center-of-mass offset at all;
[#130](https://github.com/skiretic/kartgame/issues/130), four citations pointing
at reference files that no longer exist; and
[#131](https://github.com/skiretic/kartgame/issues/131), the identical peak-versus-mean
confusion in the braking row.

**The general lesson, which is the expensive one.** `ARCHITECTURE.md` §5 item 10
requires a real part to be modeled from photographs rather than from prose, and
records that rule for *generated geometry*. The performance envelope is not
geometry, so it escaped the rule — and it is the project's substitute ground
truth, per ADR-0014. `git log -S` puts this band in the initial docs commit,
written from prose in a planning session and never sourced. The rule wants
widening to cover any externally-sourced constant, not just any generated shape.
