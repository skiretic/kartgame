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
`chassis_flex.h` could not see the lateral center-of-mass offset at all — **since
closed**: it had no production caller and was deleted, and the test file that
pinned it turned out to be measuring a kart 4.7% taller than the one the solver
configures;
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


## ADR-0035 — The audio boundary, measured, and why the generator loses

**Status.** Accepted. This is the measurement issue
[#81](https://github.com/skiretic/kartgame/issues/81) needed before any of it
could be built, and it **corrects `ARCHITECTURE.md` §12 and #81's own title**.

**Context.** #81 is titled "AudioStreamGenerator fed from C++ DSP on the audio
thread" and its third acceptance criterion is "no locks or allocation on the audio
thread". Those name two different architectures:

- **(a)** `AudioStreamGenerator` with `AudioStreamGeneratorPlayback::push_buffer`,
  where a ring buffer is filled from some *other* thread, typically `_process`;
- **(b)** a custom `AudioStream` / `AudioStreamPlayback` pair in the GDExtension
  overriding `_mix`, which the audio server may or may not call off the main
  thread.

Under (a) there is no audio thread to have a lock on and criterion 3 is vacuous.
This project's rule is that engine behavior is measured and not assumed — four M3a
defects were all plausible-but-wrong assumed behavior — so
`tools/verify/audio_probe.gd` asks the same way ADR-0032 and ADR-0033 do: one
question per case, an analytic prediction printed beside every measurement.

**The seven answers.**

1. **`_mix` runs off the main thread, so (b) is real.** Main thread
   `OS::get_thread_caller_id()` 1, `_mix` 22, distinct `std::this_thread::get_id()`
   hashes, exactly one mixer thread. True under CoreAudio and under the headless
   Dummy driver. Criterion 3 is load-bearing rather than vacuous.
2. **Block size is one constant; regularity is a property of the driver.**
   `p_frames` was **512 on every one of 684 calls** — min, median and max all 512,
   one distinct value. CoreAudio interval: 11609.2 us median against an analytic
   512/44100 = 11610.0, min 11495.7, max 11828.2. A block is **1.39 solver ticks**
   at 120 Hz, so the synth must interpolate its state across roughly one and a
   half ticks rather than holding it.
3. **`buffer_length` is a request, not a setting.** Capacity is
   `next_pow2(seconds * rate) - 1`, exact on all five sweep points: 0.02 gives
   1023, 0.05 gives 4095, 0.10 gives 8191, 0.20 gives 16383, 0.50 gives 32767.
   Asking for 0.10 s gets **0.1857 s**, 1.86x the latency named. Measured ring
   latency 172.7 ms. `AudioServer.get_output_latency()` returns **0.000000** under
   CoreAudio on 4.7.1/macOS, so the total is not readable from the engine at all.
4. **`_mix` runs concurrently with `_physics_process`, and is never re-entrant
   with itself.** 213 of 960 physics ticks overlapped a mix — 0.222 measured
   against 0.211 analytic — and the serialized hypothesis predicts exactly 0.00000
   on all three overlap statistics. `mix_reentrant` was **0** on every run. So the
   transport constraint is single-writer / single-reader, not general mutual
   exclusion.
5. **The audio thread is 3.6x-6.3x slower than the main thread.** A 24-partial
   `std::sin` stack: ~156 ns/frame offline, **576-1002 ns/frame inside `_mix`**. An
   integer-only control shows 1.4x-2.5x, **paired** with the floating-point ratio
   run for run (1.42 with 3.63, 2.50 with 6.28) — macOS is scheduling the mixer on
   a different core each run, with floating point consistently about 2.5x worse
   than integer on whichever one it lands on. This is why issue
   [#155](https://github.com/skiretic/kartgame/issues/155) exists: §15's audio
   budget is specified against main-thread time in a rendered frame, and audio
   spends none of that.
6. **The GDExtension `_mix` path survives registration and instantiation.** Three
   classes register, `_instantiate_playback` is called, 684 mix calls per 8 s. No
   `dyld4::callInitializer` crash: `ProbeStats` is namespace-scope but every member
   is a `constexpr`-constructible `std::atomic`, so it is constant-initialized, and
   every `String` and `StringName` is function-local — CLAUDE.md's trap, respected.
7. **Nothing blocks before the first frame.** All seven `AudioServer` calls from
   `_initialize` returned in 0-4 us and `ClassDB.instantiate` in 21-24 us. There is
   no audio analogue of the `Viewport.set_measure_render_time` hang that
   `shoot.sh` has to work around.

**Decision: (b), a custom `AudioStreamPlayback` overriding `_mix`.**

The number that decides it is **11.6 ms against 172.7 ms**. The generator's ring
costs 172.7 ms of measured latency at Godot's own default `buffer_length`; the
`_mix` path costs one 512-frame block. That is 15x on a channel §12 calls "a
primary feel channel", and a kart's rev response arriving a sixth of a second late
is not a feel channel at all.

Secondary, and it would have been discovered the hard way: the generator underran
**4-8 times before any deliberate stall**, invariant across a 6x change in ring
size (0.05, 0.10 and 0.30 s gave 8, 4 and 8 skips). That pattern is a startup
transient — the ring is empty at `play()` — rather than steady-state starvation,
but it means (a) clicks on every start unless it is pre-filled.

**The transport: a seqlock over `EngineAudioInput`, bounded retry.**

`audio_state.h` says the struct is "publishable by value", which invites
`std::atomic<EngineAudioInput>`. It must not be used: the struct is **72 bytes**
and `std::atomic<EngineAudioInput>::is_always_lock_free` is **false**, so
publishing by value takes a mutex out of libc++'s global table *inside `_mix`* —
precisely what criterion 3 forbids. That is the measurement that chooses the
transport.

Measured seqlock: publish 3.5 ns, once per 120 Hz tick, 4.3e-5 % of a core; read
4.6 ns, once per block, 0.0013 % of the budget. Under torture — continuous publish,
2048 reads per block — **1,751,040 reads with 0 torn**, while an unsynchronized
control tore **60,033 times**. At the honest rates both read zero, which is why the
probe prints the analytic expectation beside the count: one tear per ~841,000
reads, **0.37 per hour per kart**. Not never, which is the point of printing it.

Retry must be **bounded** — the probe uses 64 — because an unbounded spin on the
audio thread is a lock by another name. Counting "gave up" separately from "torn"
mattered: merging them reported 1022 phantom torn reads.

**What this corrects.**

`ARCHITECTURE.md` §12 names `AudioStreamGenerator` as the mechanism. Measurement
says that costs 15x the latency of the alternative, and #81's three acceptance
criteria cannot all be met by the architecture its own title names: criterion 3
only has meaning under (b), and criterion 1 — "no buffer underruns under load" — is
already violated by (a) at startup with no load at all. Issue
[#154](https://github.com/skiretic/kartgame/issues/154) carries the §12 amendment.

**What this does not settle.** Whether the synth fits the budget once §15's row is
restated. It currently does not — the 24-partial stack alone is 126-147% of the
figure for one kart on the real audio thread, and 45% with a wavetable —
[#152](https://github.com/skiretic/kartgame/issues/152) and
[#155](https://github.com/skiretic/kartgame/issues/155) hold that. Also untouched:
Godot's own mixing, bussing and 3D attenuation, and how many independent engine
voices M7 needs.

**Two surprises worth carrying forward.**

**The headless Dummy audio driver invalidates any figure taken under it.** It calls
`_mix` in bursts — median interval 166.9 us, maximum 98688.7 us — instead of on a
deadline, runs its audio clock at 0.967 of wall-clock, and puts the mixer on an
ordinary thread on an ordinary core, where the cost ratio is 1.0 against CoreAudio's
3.6-6.3. Any cost or regularity number measured with `--headless` is wrong in
**scale and in shape**. `audio_probe.sh` runs both drivers so the difference is on
the record rather than in somebody's memory.

**The optimizer ate the first seqlock benchmark**, which reported 0.0 ns — a number
that reads as a very fast transport rather than as no transport at all. It needed
an explicit `asm volatile` barrier. A benchmark that measures nothing looks exactly
like a benchmark that measures something excellent.


## ADR-0036 — The steering curve is a controller property, and `steering.h` keeps its position

**Status.** Accepted. This **qualifies** [ADR-0031](#adr-0031--the-m3a-vehicle-applies-its-own-forces-in-newtons)'s
removal of M3a's `STEER_SPEED_FALLOFF` rather than reversing it, and it comes from
a driving report rather than from a gate.

**Context.** The first session driving `scenes/game/test_track.tscn` produced one
sentence: *"you barely touch the steering and it goes way out of control."*

That is a feel report, and this project's rule is that a feel report becomes a
measurement before it becomes a change. The measurement is a new case in
`tests/core/test_vehicle.cpp` — a steering **step** at 100 km/h, which nothing here
had measured, because `steady_corner` averages the last second of a six-second hold
and discards anything that departed. A kart that snaps on turn-in and a kart that
turns in cleanly produce the *same row* in that table.

    input radius m  asked g yaw/Ack pk slip deg   kept %  IR load  stable
     0.05    48.67     1.62     0.483     1.01     96.9    356.4     yes
     0.20    12.55     6.27     0.478     6.37     95.6    134.3     yes
     0.50     5.29    14.88     0.322    18.25     91.1     56.8     yes
     0.60     4.47    17.60     0.283    25.06     80.6      0.0     yes
     0.70     3.88    20.26     0.258    40.39     66.8      0.0      NO
     0.75     3.65    21.58     1.487    71.70     15.7      0.0      NO
     1.00     2.80    28.06    33.825   160.31    -30.6      0.0      NO

**The decisive number is a pair of them, and neither is about the vehicle.**

At 100 km/h the tightest radius this kart can hold is `v^2 / (2.10 g)` = **37.5 m**,
which Ackermann puts at **0.065 of lock — 1.62 degrees**. And `project.godot` sets
the steer actions' **deadzone to 0.15**, while Godot's `get_action_strength` returns
the raw axis value above the deadzone with no rescaling.

So the smallest steering input a stick could physically produce was 0.15 of lock,
asking **4.8 g**. *The entire followable steering range was inside the deadzone.*
There was no stick position that produced a corner the kart could hold: the driver
got zero steering or a slide. That is not a difficult kart. It is an unreachable
one.

**Decision: `steer_gamma`, an exponent on the driver's input, default 3.0, applied
in `KartBody::gather_input` and nowhere else.**

    stick   lock    inner deg  radius m  asked g
     0.15   0.0034      0.08    713.57     0.11   the deadzone edge, was 4.8 g
     0.40   0.0640      1.60     38.14     2.06   the 100 km/h limit
     0.62   0.2383      5.96     10.61     7.41   Turn 2's 11 m hairpin
     1.00   1.0000     25.00      2.80    28.06   full lock, unchanged

The exponent is chosen from the second row — it puts the fastest corner on the track
at 40% of stick travel, where a thumb has resolution, instead of at 6.5% where it
has none. The third row is the check that it did not overshoot: a curve that made
fast corners comfortable by pushing the hairpin past the end of the stick would have
traded one unreachable corner for another.

**Why this does not contradict `steering.h`, which is the whole point.**

`src/core/steering.h` deleted M3a's `STEER_SPEED_FALLOFF` and argues that a real
kart's high-speed stability must be **emergent from caster and Ackermann** rather
than manufactured by an input aid. `drive_probe.gd`'s header restates it: full lock
now genuinely is full lock.

That argument is about **the vehicle**, and it survives untouched. The kart's
response to a given lock is identical at every speed, nothing in `src/core/` can see
`steer_gamma`, and the solver is not told that a fast kart should steer less. What
changed is the map from a thumb to a lock angle — and the geometry makes that map
the problem rather than the vehicle: a 1.05 m wheelbase needs 1.62 degrees for a
37.5 m corner, and a real driver places 1.62 degrees using **900 degrees of wheel
rotation** against a stick's 15 mm.

The rejected alternative is scaling `max_lock` down with speed. It is more drivable
and it is the thing `steering.h` forbids, because it makes the vehicle lie about what
it would do. If it is ever wanted it belongs beside auto-clutch and auto-shift as a
disclosed assist that #40 can switch off, not inside the steering solver.

**The curve is applied to the human path only, and the split is verified rather than
asserted.** `gather_input`'s `input_driver_` branch — what `drive_probe.gd`,
`track_probe.gd` and every still command drive through — is untouched. Those callers
pass **lock** fractions with a recorded sweep table attached to them, and curving
them would silently rewrite every figure in `drive_probe.gd`'s header and in ROADMAP
M3b while the results still looked plausible. A scripted run asks for a steering
angle; a driver asks for a stick position. `tools/verify/drive.sh` returns all four
scenarios with every §6.4 figure unmoved, which is the evidence that the two paths
really are separate.

`steer_gamma = 1.0` collapses them exactly.

**Two readings of the measurement were wrong before the `asked g` column existed**,
recorded because both are the shape of mistake ADR-0033 and issue #107 are about:

1. *"The kart does not rotate."* `yaw/Ack` below 1 is a grip-limited vehicle running
   wide. At 100 km/h **every** row in the sweep asks for more than 2.10 g, so the
   response is correct and the demand was the anomaly. A symptom was read as a
   defect because the demand was not printed beside it.
2. *"The solid rear axle is fighting the yaw because the inside rear never lifts."*
   The `IR load` column shows the inside rear reaching exactly 0.0 N from 0.60 of
   lock. Issue #32's mechanism engages, and it is not the cause of anything here.

**What this does not settle.** 3.0 is arithmetic, not feel — the first drive after it
reported "a bit more reasonable", which is not "right". The track carried a live knob
on `[` and `]` and issue #159 holds it alongside every other constant that can only
be set by driving. `steer_rate`'s 3.4 has never been judged by feel either, only
recorded, and it interacts with the curve.

*Superseded in mechanism, not in conclusion:* the `[` / `]` knob was a prototype of
the wrong shape — one constant, two keys, no record of what it was overriding — and
[ADR-0037](#adr-0037--tuning-is-an-audit-trail-that-happens-to-be-adjustable-and-a-preset-is-not-in-the-state-hash)
[ADR-0038](#adr-0038--the-scrub-band-is-measured-on-the-wrong-tire-and-saying-so-is-the-whole-design)
replaced it with the tuning registry, where `steer_gamma` is one row among fourteen
and is classified `Derived` with this ADR as its citation. Nothing above changes: the
exponent is still 3.0, still arithmetic rather than feel, and still unjudged.

And **issue #137 is unchanged.** The scrub is recontextualized rather than fixed:
past 0.65 of lock the kart still departs, and at full lock it still ends up
travelling backwards — `kept %` of -30.6. The curve makes 25 degrees harder to reach
by accident and changes nothing about what happens on arrival.

## ADR-0037 — Tuning is an audit trail that happens to be adjustable, and a preset is not in the state hash

**Status.** Accepted. This builds ROADMAP M3b's last unbuilt bullet and issue
[#159](https://github.com/skiretic/kartgame/issues/159)'s checklist, and it
**constrains** [ADR-0033](#adr-0033--the-contact-boundary-measured-and-the-lever-arm-it-caught)
rather than relaxing it: ADR-0033 refused to retune M3a's constants to restore a
figure, and this is the machinery that makes the same refusal enforceable now that
the constants are live.

**Context.** About a dozen constants in this project can only be settled by
driving or by listening, and until now every one of them needed a rebuild to
change. "Judged by feel" was therefore, in practice, "whatever the first guess
was" — issue #159 exists because that was true of `steer_rate`'s 3.4, which had
never been judged at all, only recorded.

The registry that landed carries fourteen, which is **more** than that checklist
and deliberately so: it also holds constants that do have a source, because the
contrast between the two is the thing being built. A registry containing only the
guesses would read as a list of numbers it is fine to turn, which teaches the
wrong lesson the first time somebody wants to turn something else.

The obvious fix is a slider panel with a save button. That fix is
`ARCHITECTURE.md` §19's *"vehicle tuning eats unbounded time"* risk, shipped.
§19's mitigation is telemetry landing with the vehicle and A/B against a replay;
it has nothing to say about a constant that has quietly stopped matching the paper
it came from. So what is built is an **audit trail that happens to be adjustable**,
and the four decisions below are what make it one rather than a settings file.
`src/core/tuning.h` holds the vocabulary, the file format and the audit
arithmetic, with no godot-cpp in it
([ADR-0017](#adr-0017--srccore-is-compiled-without-godot-cpp)), and
`src/tuning/tuning_registry.h` is the only thing between it and the engine.

### 1. Four provenance classes, not `has_source: bool`

Every tunable declares where its default came from, ordered most to least
defensible: `Sourced` (an external authority — a published measurement, a
manufacturer figure, a regulation), `Measured` (this repository measured it),
`Derived` (arithmetic on top of those), `Unsourced` (a guess, with the guess
labeled).

A boolean was the obvious design and `docs/REFERENCES.md` is what kills it. Two
rows of the table:

- `frame_torsion` is **193.62 N·m/deg** because Fu and Wang measured a real
  competition frame and published the number — WIT Trans. Built Env. Vol 91
  (2007), 193,620 N·mm/deg. That is evidence about karts.
- `max_lock` is **25° at the inner wheel** because REFERENCES.md §"Steering lock"
  records that **no CIK or KZ source for a maximum steer angle was found at all**.
  The figure is inherited from the bodywork clearance tables measured in issues
  [#109](https://github.com/skiretic/kartgame/issues/109) and
  [#110](https://github.com/skiretic/kartgame/issues/110) — this repository
  measuring its own generated kart.

A boolean files those in one bucket and calls both of them sourced. They are not
the same claim. The second is only ever as good as this project's own geometry: if
`params.py` moves a dimension, 25° moves with it, while Fu and Wang's number does
not move for anything. Four classes can say that in the saved file, in the overlay
and in a grep. One bit cannot say it anywhere.

This generalizes an existing convention rather than inventing one.
`src/core/kz_audio_reference.h` already carried half of it as four
`*_MEASURED = false` flags — `LIMITER_CHATTER_MEASURED`,
`SHIFT_TRANSIENT_MEASURED`, `SCRUB_SPECTRUM_MEASURED`, `WIND_SPECTRUM_MEASURED` —
sitting beside the constants they qualify, for exactly this reason. What that file
does for one subsystem's audio references, `Provenance` does for every tunable,
with the two middle classes it was missing.

Each row carries its citation **copied from the file the constant lives in**
rather than restated, which is `ARCHITECTURE.md` §5 item 10 applied to a header: a
second owner of a justification is how justifications rot.

### 2. A preset is a diff against the defaults, not a full state

A saved preset is a header and then **only the tunables that differ from their
declared default**, in declaration order. Nothing else.

The alternative — write all fourteen values every time — is what a settings file
does, and it destroys the property the system exists for. Under a full state every
preset looks the same shape whether it moved one number or all of them, and
answering "what has been tuned" needs a tool, a copy of the defaults, and a
decision about float equality. Under a diff:

- an **empty** preset means nothing was tuned, and says so by being empty;
- a **three-line** preset is the complete list of what was, with nothing to read
  between the lines;
- "what has been tuned away from its source, and by how much" is one `grep`,
  because each line carries its own default, its delta and its provenance class in
  a trailing comment.

That is what makes a preset reviewable in a pull request rather than only
runnable, and it is the direct answer to §19. The risk is not that somebody turns
a knob. It is that nobody can afterwards enumerate which knobs were turned.

The sparseness lives in the **file**. `TuningSet` in memory is always a complete
configuration, one value per tunable, because a set that knew only its overrides
could not answer "what is running" without also carrying the defaults it was built
from. Both sides quantize to 1e-6 on the way in, so "is this at its default" is an
integer comparison and a value nudged up and back down is default again rather
than almost-default.

A preset also names the defaults hash it was written against. One written before
somebody moved a default still says what it meant — every line carries its own key
and value — but it no longer means the same thing *relative* to the defaults, so
it loads with a stated warning rather than a refusal.

### 3. A defended default can be overridden, but explicitly and loudly

`Sourced` and `Measured` defaults are **defended**: `set_value` and `nudge` refuse
them and return the unchanged value until `acknowledge(id)` has been called for
that one tunable. Once moved, it is written to the preset with a leading `!` and
the citation it overrides:

    ! max_lock = 0.523599  # default 0.436332, +0.087266 (measured) OVERRIDE: REFERENCES.md 'Steering lock': NO CIK or KZ source was found...

The `!` is first on the line on purpose, so an override is visible in a diff hunk,
in a grep, and in a terminal that has wrapped the comment off the right-hand edge.

**The acknowledgement is per-tunable and per-session, and is never persisted.**
The rejected design is a global "expert mode", and it is rejected on a prediction
about how it would really be used: a global switch gets turned on in the first ten
minutes of the first tuning session and then defends nothing for the rest of the
project. A gate crossed once is not a gate. Fourteen gates, each crossed
deliberately and all of them reset when the process exits, are.

**A `Derived` default is deliberately not defended**, and this is the line the
whole scheme turns on. A derivation is arithmetic *this project* did on top of
other numbers — it is not evidence about the world, and defending it would protect
the project's own reasoning as though it were a published measurement.
`steer_gamma`'s 3.0 is four rows of arithmetic in
[ADR-0036](#adr-0036--the-steering-curve-is-a-controller-property-and-steeringh-keeps-its-position),
and that ADR closes by saying 3.0 is arithmetic rather than feel. It is the knob
this entire system exists to let somebody turn. Defending it would have been the
system defeating its own purpose on the first constant anybody reaches for.

`reset_value` is never refused either. Putting a sourced number back where the
source put it is not an override and must not need a ceremony.

### 4. A preset does not enter `StateHash`. This is the decision M3b asked to have written down

A preset changes the solver's constants, so two runs under different presets
produce different states — that is the entire point of it. The tempting move is to
mix the tuning into `StateHash` so a divergence is attributable. It is wrong, for
the same reason `state_hash.h` gives for quantizing: **a hash answers one
question, and a hash that answers two answers neither.**

- `StateHash` asks *"did these two runs of the same configuration diverge?"* Mix
  configuration in and every per-tick hash changes when an unrelated audio tunable
  moves, and a mismatch can no longer distinguish "the physics diverged" from
  "the config differed" — which are precisely the two things the harness exists to
  tell apart.
- `TuningSet::hash()` asks *"is this the configuration I think it is?"* One number
  over the whole set, key and value both, so appending or renaming a tunable
  changes the digest. Compared **once per run**, not once per tick.

Two numbers, two questions, and **the gate is the pair of them**:
`tools/verify/tuning.sh --check` round-trips presets and prints the
defended-override count first, and `drive_probe.gd` and every §6.4 measurement
assert `TuningSet::hash() == default_tuning_hash()` before recording a figure. A
tuned run cannot be quietly written down as a reference figure, because the
harness refuses to call it one.

This is **stronger** than mixing, and the distinction is worth stating plainly
because it is not obvious. Mixing would have made a tuned run produce a
*different* hash. Different is not loud: a different hash is found later, by
somebody comparing two runs and then working out which of two possible causes
produced it. An assertion at defaults, checked before the figure is written, means
a tuned run never reaches the table at all.

### The text format is hand-rolled integer arithmetic, and that is not fussiness

Values cross the file as six fixed decimals produced by digit-by-digit integer
conversion rather than by `snprintf("%f")`, because **`%f` respects the C locale**
and a decimal-comma machine would write `2,400000` — a file this parser rejects,
produced by the same code that reads it. That failure appears on somebody else's
machine and nowhere near here, which is the worst place a failure can live. The
hex digests are written by hand for the same class of reason, already on the
record: `KartStateHash::hex` pads by hand because GDScript's `String.pad_zeros`
counts only *digit* characters and mangles any hex string that begins with a
letter.

Values are quantized on the way **in**, not on the way out, so the file is the
truth: what a preset saves is exactly what it loads, and no float round-trips to a
neighbor.

### What this does not settle

**No value is changed by this ADR.** Every default in the table is the number that
was already in the file it cites; the rows exist so somebody can change them
deliberately. Choosing the values is the driver's job, not the machine's, and
`tools/verify/drive.sh` returns every §6.4 figure unmoved.

**The acknowledgement is a speed bump, not a permission system.** It stops an
absent-minded override and it records a deliberate one. It cannot stop somebody
editing a preset file by hand and it is not meant to — the `!` marker and the
defended-override count are what make the result visible afterwards, and
visibility is the whole mechanism.

**Three of issue #159's constants are still `constexpr` and not reachable live**,
so the checklist and the table are not yet the same list. #159 stays open as a
running checklist rather than a ticket that closes, and the rule is that a
constant which gets a row in one gets a line in the other.

**And the four feel-blocked tickets are still blocked.** #32, #38, #39 and #40
have acceptance criteria written in a driver's language. A knob that can now be
turned mid-session is a prerequisite for answering them, not an answer.

## ADR-0038 — The scrub band is measured on the wrong tire, and saying so is the whole design

**Status.** Accepted. Builds ROADMAP M8's "tire scrub from slip magnitude, wind
from speed" and issue
[#84](https://github.com/skiretic/kartgame/issues/84). It **extends**
[ADR-0037](#adr-0037--tuning-is-an-audit-trail-that-happens-to-be-adjustable-and-a-preset-is-not-in-the-state-hash)'s
provenance vocabulary rather than using it as written, and the extension is the
first decision below.

**Context.** `src/core/kz_audio_reference.h` carried
`SCRUB_SPECTRUM_MEASURED = false` and `WIND_SPECTRUM_MEASURED = false` for two
milestones, and `engine_synth.h` carried a `static_assert` that deliberately left
`EngineAudioInput::scrub` and `::speed_ms` unread because of them. `ARCHITECTURE.md`
§5 item 10 forbids modelling a real thing from memory or from prose, and a filter
shape is exactly the kind of thing a session invents from a plausible-sounding
intuition. So #84 could not be built the way #82's engine note was — that had four
hash-pinned recordings behind it and this had none.

The work therefore started with a sourcing pass whose entire job was to find one,
running concurrently with the layer structure and the drive signals, which do not
depend on it: slip magnitude per corner and road speed are solver truth and are
sourced regardless.

**It found something, and what it found is more awkward than either "yes" or
"no".** Eight recordings were analyzed, three of them license-clean CC0 field
recordings of rubber scrubbing on asphalt, separated from their engines by a
two-population power subtraction whose check is that the background population
lands on an engine and the event population does not. They agree on a band. Every
one of them is a **passenger-car radial** — 1996 Chrysler LHS, Nissan Maxima — and
a KZ runs 5-inch 10-inch slicks with no tread pattern at high pressure on a much
stiffer carcass. The only engine-free vehicle recording found, an indoor electric
kart track, turned out to be a negative result: its tone is present in 97% of
frames, which is a room and a motor. For wind, **no usable recording exists at
all** — the one on-vehicle hit is a parked car in a storm, which has no airspeed
and cannot be scaled — and what exists instead is a peer-reviewed open-access
figure of one-third-octave levels at seven road speeds under a **motorcycle
helmet**.

`docs/REFERENCES.md` §"Tire scrub and wind" has the corpus, the method, the
per-file tables and seven numbered reasons to be careful.

### 1. Two flags per quantity, because Sourced and Measured can disagree about one

ADR-0037 established four provenance classes precisely because
`has_source: bool` files "Fu and Wang measured a real competition frame" and "this
repository measured its own generated kart" in the same bucket. The same argument
recurses one level down, and the header is where it has to be answered:

```
inline constexpr bool SCRUB_SPECTRUM_MEASURED = true;
inline constexpr bool SCRUB_MEASURED_ON_KART_TIRE = false;

inline constexpr bool WIND_SPECTRUM_MEASURED = false;
inline constexpr bool WIND_SPECTRUM_SOURCED = true;
```

A spectrum *was* measured, to the same standard the engine ladders meet, and it is
**not a measurement of the object the game simulates**. One boolean cannot hold
that. For wind the two flags disagree in the other direction: this repository
measured nothing and an external authority published something, which is
ADR-0037's Sourced-versus-Measured distinction applied to a single quantity for
the first time in this codebase.

The registry rows follow from the flags rather than from a judgement call.
`scrub_center_hz`, `scrub_q` and `wind_speed_exponent` are `Provenance::Derived` —
arithmetic on a measurement of a different object — and Derived is **not
defended**, so they can still be turned by ear on F2 without an acknowledgement.
Classifying them `Measured` would have defended a number taken off a Chrysler.

### 2. The measurement changed the filter's *structure*, not just its constants

This is the part that would have been missed by treating the sourcing pass as a
source of numbers to paste in.

The recordings give slopes of **+9.7 and −14.0 dB per octave**, fitted over the
octave below the peak and the two octaves above it. A two-pole band-pass is
symmetric by construction. Measured over those same spans the candidates are:

| structure | peak | width | below | above |
| --- | --- | --- | --- | --- |
| 1 × BP(Q=6.40) | 1000 | 0.667 | +7.91 | −8.03 |
| 2 × BP(Q=3.14) | 1000 | 0.668 | +15.59 | −15.97 |
| **BP(Q=6.40) + one-pole LP at f0** | **1000** | **0.674** | **+7.21** | **−13.28** |
| the recordings | | 0.67 | +9.70 | −14.00 |

One section is 6 dB/octave short on the upper skirt. Two cascaded sections
overshoot both. Neither can be asymmetric at all. A one-pole low-pass **at the band
center** is flat below it and adds 6 dB/octave above it, which is the asymmetry the
recordings show and is the only one of the three that reproduces it.

**The lower skirt is knowingly left 2.5 dB/octave short.** Closing it needs a third
stage fitted to three recordings of two passenger cars, from lossy previews, by one
recordist in one session, of a tire this project does not use. That is false
precision, and §5 item 10's spirit cuts against it as hard as it cuts against
inventing the number. `tests/core/test_scrub_wind.cpp` asserts the asymmetry exists
with the right sign, the upper skirt to within 1 dB/octave, and the lower one as a
bound so that a change making it worse is caught.

### 3. The number the file originally refused to use turned out to be right

`WIND_SPEED_EXPONENT` was 2.0, and its comment argued at length that an
aeroacoustic dipole radiates with U⁶ for an amplitude exponent of 3, that 3 is what
a first guess reaches for, and that naming the dipole law would be exactly the
plausible-sounding intuition §5 item 10 exists to stop.

Brown and Gordon (2011) measured about 20 dB per doubling of road speed under a
helmet; Lower et al. (1994) measured 15.5 dB per doubling on the open road.
18.06 dB per doubling **is** an amplitude exponent of 3. The refusal was correct
and so was the number it refused — and 2.0 was quietly 6 dB per doubling too quiet
at speed. What changed is that it is cited now. That is the rule working, not the
rule being wrong: an uncited 3.0 and a cited 3.0 are different claims even when
they are the same float.

### 4. Three emitters, because they are not in the same place

Scrub is an `AudioStreamPlayer3D` at the rear axle: M8's acceptance criterion is
that opponents are locatable by ear, and a tire is where the kart is. Wind is a
plain `AudioStreamPlayer` — it is at the driver's head, it is not a source in the
world, and attenuating or Doppler-shifting it would be nonsense. There is one of it
and never twelve.

One class serves both with a `layer` property, because that difference is entirely
in how a scene mounts them. Separating them at the *mixer* rather than summing them
in the synth is also what issue
[#160](https://github.com/skiretic/kartgame/issues/160) needs: turning one layer
off must not require re-judging the others.

The seqlock moved to `src/audio/state_seqlock.h` on the way, because three
hand-copied copies of a transport is three places for the memory ordering to drift.
Nothing about the algorithm changed.

### 5. Cost was measured before the layers reached a scene

[#152](https://github.com/skiretic/kartgame/issues/152) records the harmonic stack
at **74.76% of real time for twelve voices**, which is M7's grid and is not a
budget with room in it. `tools/verify/scrub_cost_probe.gd` runs both layers inside
a real CoreAudio `_mix`, in two passes so that "what did scrub add" is answerable
separately from "what does the player's kart cost":

| | ns/frame |
| --- | --- |
| one engine voice, worst (quoted) | 1412.8 |
| one scrub layer, worst | 15.2 |
| the one wind layer, by difference | 19.5 |
| twelve scrub layers (M7) | 0.805% of real time |

Whatever M7 does about voice culling, it is not about this file. The probe's
`silent` cell is deliberately among the most expensive of the five, which is the
check that nothing short-circuits when the drive is zero — a layer that were cheap
exactly when nobody is listening would be the wrong shape.

**The wind pass doubles as the noise floor**, and that was not designed in.
`WindSynth` never reads `surface`, so its `fast corner grass` and
`fast corner asphalt` cells execute identical code; whatever they differ by bounds
how much of the scrub column's spread can be real. One run came back with 15%
between two cells running the same instructions.

### What this does not settle

**The layer is continuous and a real squeal is not.** The same recordings give 32
and 33 discrete events, median 85 ms, with the peak frequency moving 989–3600 Hz.
`kz_audio::SCRUB_EVENT_MEDIAN_S` records it and nothing reads it. Issue
[#161](https://github.com/skiretic/kartgame/issues/161) owns the event model, and
it is not built here because an event model needs a stick-slip onset criterion and
inventing one on top of a four-corner mean would be a mechanism nobody measured —
which is the rule that kept this whole file from existing until somebody went and
measured a band.

**The slip-angle axis is still unsourced.** §12 needs scrub driven by slip and
nothing in these recordings has a slip angle attached to it. REFERENCES.md item 17
records what the secondary literature claims and why it did not reach the header:
it came off search-result summaries of paywalled SAE work rather than off a table.

**Nothing has been judged by ear.** All thirteen audio rows in the registry are
placeholders in the sense `VOICE_GAIN`'s 0.18 is, #160 owns the mixing pass, and
#159's checklist has grown by eight.

## ADR-0039 — The master level is a bus, because three gains in series had no owner

**Status.** Accepted. Closes the measurement half of issue
[#160](https://github.com/skiretic/kartgame/issues/160) and unblocks the two
acceptance items of [#84](https://github.com/skiretic/kartgame/issues/84) that
were waiting on it. Extends
[ADR-0038](#adr-0038--the-scrub-band-is-measured-on-the-wrong-tire-and-saying-so-is-the-whole-design)
and settles the item
[ADR-0035](#adr-0035--the-audio-boundary-measured-and-why-the-generator-loses)
explicitly left open: "Godot's own mixing, bussing and 3D attenuation".

**Context.** The driver's report was not a balance complaint. It was:

> it could be the wind seems so loud because i have to turn the volume so high to
> actually hear the engine at even what i consider still too low

`e7b0eaa` had just set three gains against each other — `voice_gain` 0.18 to 0.30,
`wind_gain` 0.20 to 0.12, `scrub_gain` held at 0.45 — which is a ratio pass run
against a path that was under-level. Adjusting the ratio between two layers that
are both too quiet cannot fix a level, and the system volume was making up the
difference for all of them, wind included.

#160 named four things to measure before touching a gain. All four are measured
now, by two paths that never met: `tests/core/test_synth_headroom.cpp` in pure
C++ with no engine at all, and `tools/verify/audio_level_probe.gd` inside a real
CoreAudio mix. Where they overlap they agree to within 0.5 dB, which is the only
reason any of the numbers below are quoted as facts.

### 1. The prime suspect was innocent, and naming it early nearly cost the session

#160 predicted the loss was Godot's 3D attenuation: `UNIT_SIZE = 3.0` with the
listener at the chase camera, "a listener at 6 m is already at 0.5 of unit gain
and at 10 m is at 0.3". The arithmetic is right. It describes a distance the
chase camera is not at.

`chase_camera.gd` has `ARM_LENGTH = 3.4` and `ARM_HEIGHT = 1.05`, so the listener
sits **3.56 m** from the kart origin. Measured against Godot's own inverse-distance
law, clamped above at the `max_db` property's default of +3.0 dB:

| listener at | total dBFS | predicted | delta |
| --- | --- | --- | --- |
| 0.52 m (driver's head) | -0.04 | -0.19 | +0.15 |
| 3.00 m (`unit_size`) | -3.18 | -3.19 | +0.01 |
| **3.56 m (chase camera)** | **-4.71** | -4.67 | -0.03 |
| 10.00 m | -14.07 | -13.65 | -0.42 |
| 150.00 m (`max_distance`) | silence | -37.17 | — |

The law is exactly what the documentation says out to 6 m, diverges past 20 m as
the `max_distance` rolloff takes over, and cuts to silence at the cutoff. **The
chase camera costs 1.53 dB.** The near field is flat — 0.52 m, 1.0 m and 2.0 m are
the same level, because `max_db` clamps everything inside `unit_size`.

So attenuation is worth 1.53 dB of a 15-20 dB problem. Had the first move been to
raise `UNIT_SIZE`, it would have made no measurable difference and the session
would have concluded the synth was broken.

### 2. Two things nobody had measured at all, and one of them is in every scene

**The listener is whichever `Camera3D` is current.** `scripts/game/` contains no
`AudioListener3D`. Measured both ways: with the camera as listener the level swings
**20.7 dB** between 1 m and 20 m; with an `AudioListener3D` made current at the
emitter it is flat at every distance, to 0.00 dB. The second half is what makes
this actionable — the fix is one node, not a redesign.

The consequence is worse than the loudness. The cockpit view and the chase view
were hearing **two different mixes**, and every judgement ever made was made on
one of them. A mix that changes when the driver presses V is not a mix that can be
tuned, and this repository had been tuning it.

**`attenuation_filter_cutoff_hz` defaults to 5000 Hz, with
`attenuation_filter_db` at -24, and `engine_voice_rig.gd` never set either.**
Swept with sines, against the same player with the filter opened to 20.5 kHz:

| | 1 kHz | 4 kHz | 8 kHz | 12 kHz |
| --- | --- | --- | --- | --- |
| at 3.56 m | +0.16 | -1.22 | **-8.99** | -8.97 |
| at 20.00 m | +1.19 | -16.44 | -31.38 | -41.67 |

Every 3D emitter in this project has been low-passed since the engine note was
written. It is distance-scaled, so moving the listener to the driver's head
removes most of it for the player's own kart while leaving it for opponents —
which is the behavior a distance cue should have. **Left at its default
deliberately**, now that it is a measured default rather than an unexamined one.

**A positional player costs 3.19 dB against a non-positional one** at the same
gain and at unit distance, measured. ADR-0038 section 4 put the engine and scrub
on `AudioStreamPlayer3D` and the wind on a plain `AudioStreamPlayer` for reasons
that remain correct — so the wind layer was collecting 3.19 dB that nothing in
its gain, its speed law or its filter accounts for.

### 3. There is no headroom hiding inside the synth, and that is the load-bearing finding

The tempting story was that `engine_synth.h` normalizes its harmonic stack by the
**sum of partial amplitudes** rather than by power, so the RMS falls as roughly
`1/sqrt(N)` and a conservative constant could be raised. The first half is true and
larger than expected — the normalization costs **17.8 to 24.4 dB** of RMS across
the rev range. The conclusion drawn from it is false.

The partials start at phase zero and stay harmonically related, so they **do**
re-align once per fundamental period. Measured `peak / sum(a_i)` is **0.60 to
0.73**, not the few percent a random-phase sum would give. The bound is nearly
tight. At `gain = 1.0` the loudest cell peaks at **-0.70 dBFS and is already
1.24 dB inside the soft-clip knee** — `gain = 1.0` clips.

**So `voice_gain` holds 9.03 dB and no more**, and what is under it is a 15-20 dB
crest factor that is a property of a phase-aligned harmonic stack, not a number
anybody chose. No multiplication recovers it. That is why the master is not at the
stream: a master that lives where the clip knee lives has to fight it.

While measuring it, `engine_synth.h:246-252`'s claim that the stack "is bounded by
that level analytically" so that "only the unmeasured state layers stacking up can
reach the knee" was shown false — `ONPIPE_LEVEL_DB` puts the level at 1.4125, so
the analytic bound is 1.41 and the reasoning only holds for `gain <= 0.708`.

### 4. The decision: one bus, and the layer gains become balance

    Master
      +- Kart          master_gain_db = +9.5    <- the only level
           +- EngineVoice   voice_gain 0.30     <- balance
           +- ScrubVoice    scrub_gain 0.035
           +- WindVoice     wind_gain  0.20

`master_gain_db` is a registry row like any other, so F2 moves it and `tuning.sh`
audits it. That is the direct answer to #160's own drift note: the three gains
lived in `core/tuning.h`, `engine_voice_rig.gd` and the synth, and nothing owned
the sum.

The bus is built in code rather than as a `.tres` bus layout. A bus layout is a
binary resource that no reviewer can read in a diff, and this project's entire
audit story is that a number sits next to the argument for it.

**And the listener moves to the driver's head**, served by
`KartBody::driver_head_position()` from `chassis.h`'s lump table — a calibrated
position that will move when issue #107's seat geometry closes, which is exactly
why it is served rather than typed into a scene. Worth 4.5 dB, and worth more than
that for making the mix independent of the camera.

Measured end to end afterward, through the real bus chain:

| at the listener | before | after |
| --- | --- | --- |
| engine note | -33.08 dBFS | **-18.64 dBFS**, peak -4.60 |
| scrub, full slip | +16.3 dB re engine | -3.1 dB re engine |
| wind, 30 m/s | -6.8 dB re engine | -10.3 dB re engine |

### 5. Two layer gains moved on measurement rather than by ear

**`scrub_gain` 0.45 -> 0.035.** Full-slip scrub sat **16.7 dB** (core) and
**16.3 dB** (in-engine) *above* the engine note, and peaked at **1.052** — past
full scale, in the shipped configuration, today — `ScrubSynth` wrote its output
with no clamp where `EngineSynth` has `soft_clip`, and over a longer window the
peak is worse still, 1.25 to 1.34 depending on surface. 22 dB down leaves a
full-slip slide **3.1 dB under the engine at the listener** and 10.9 dB under it
from the chase camera. The 22 dB is measured; whether 3 dB is the right warning
margin is a preference and is owed an ear.

The clamp landed anyway, because a layer whose only protection is a gain nobody
has judged yet is not protected. It is `EngineSynth`'s own `soft_clip` and
`SOFT_CLIP_THRESHOLD` rather than a second idea of what a ceiling is, and it went
on `WindSynth` too. At the shipped gains it is idle, which is what a safety net
should be; the test that proves it is still wired turns both gains to the top of
their own F2 ranges and shows the output still cannot leave full scale.

**`wind_gain` 0.12 -> 0.030, and the plan for this one was wrong.** The intent was
to revert `e7b0eaa`'s 0.20 -> 0.12, because that change was made on a drive where
the engine was 15-20 dB under level and so measured too much wind where there was
too little of everything else. Then the wind chain was normalized by its own
filter's RMS gain — the defect `193d507` fixed for scrub and missed here — and
**that made the layer 13 to 20 dB louder at the same gain**, most at low speed,
because the excess is exactly the 2.5 dB per doubling the un-normalized two-pole
was contributing. 0.12 after the fix is *louder than the engine*.

So the number moved down rather than up, and it is not a judgement changing: the
layer moved underneath it. 0.030 puts wind 10.3 dB under the engine at 30 m/s,
which is roughly where 0.12 sat before. The same fix took the realized slope from
20.0-20.7 dB per doubling to **18.02 against a sourced 18.0**, and stopped
`wind_cutoff_per_ms` moving the level — winding that knob across its full F2 range
used to change the layer's RMS by a factor of 5.2, and now moves it 0.73%.

### What this does not settle

**Nothing here has been judged by ear.** Every figure above is a measurement and
the mix is a judgement. `master_gain_db`, the 6 dB scrub margin and `wind_gain`
are all on #159's checklist and all three are what the next drive is for.

**The crest factor is the real loudness ceiling.** Reducing it means dispersing
partial phase across the stack, which changes the note's timbre and therefore
re-opens #82's acceptance criterion. That is a separate ticket and a separate
drive, and it is the only change that buys loudness rather than moving where the
ceiling sits.

**`look_back` was bound in `project.godot` and printed in both HUDs since M3a and
nothing read it.** C and Triangle did nothing at all. The cockpit rig implements
it; the chase rig's own look-back is separate work with its own issue. It is the
failure CLAUDE.md's driving section opens with, one level worse — the key was
advertised rather than merely omitted.

---

## ADR-0040 — Input is handed to the vehicle, not fetched by it

**Status.** Accepted, and **not yet implemented** — this is a decision taken
ahead of the work rather than a record of it, which is unusual for this file and
is the point. It is the first structural item of `docs/GAMEDESIGN.md`'s planning
pass, and it precedes ROADMAP M3c. Extends
[ADR-0036](#adr-0036--the-steering-curve-is-a-controller-property-and-steeringh-keeps-its-position),
which established that the steering curve is a controller property, and it
finishes the sentence that ADR started.

**Context.** `src/vehicle/kart_body.cpp:585` opens with `Input *map =
Input::get_singleton()` and fills its own `DriverInput` from the action map. The
struct exists and is already the right shape. What is wrong is the arrow: **the
vehicle reaches out for input rather than being given it.**

*(This ADR was written calling the struct `VehicleInput`, and there is no such
type: it is `kart::core::DriverInput`, `src/core/vehicle_state.h:37`. Corrected
throughout. ADR-0041's header sketch had the same wrong name and is corrected
there too.)*

That works for exactly one configuration — one kart, one human, one process, now.
Five things this project has already committed to need input to arrive as *data*:

| Wants input as data | Milestone |
| --- | --- |
| A replay that re-sims to an identical state hash | M6 |
| A ghost, which is a recorded input stream played back | M6 |
| An AI driver emitting "the standard input struct" through the same path the player uses | M7 |
| A second kart in the same scene, and then eight | M11 |
| A headless gate that asserts the advertised controls are the real ones ([#169](https://github.com/skiretic/kartgame/issues/169)) | M3c |

Every one of those is a rewrite of the sim's hottest file if it arrives after the
systems that sit on top of it. It is one indirection now.

**And there is already a symptom in the tree.** CLAUDE.md records that the tuning
overlay cannot use the arrow keys, because they are the second binding on
throttle, brake and steer, and `KartBody` polls them through the `Input`
singleton where `set_input_as_handled` cannot reach. That is not a quirk of the
overlay. It is this ADR's problem observed from the other end: a node that
fetches global input cannot be told to stop, so anything upstream of it — a menu,
a pause, an overlay, a replay — has no authority over it.

**Decision.** `KartBody` exposes `set_input(const DriverInput &, uint64_t tick)`
and consumes whatever it was handed for the current tick. It never reads `Input`.
Four producers fill the same struct:

- `PlayerDriver` — reads the action map, and **owns the steering curve**, which
  moves out of `kart_body.cpp` with it. ADR-0036 already said that curve is a
  controller property and `steering.h`'s position is correct; the code kept it in
  the vehicle anyway. This is where it belongs.
- `AIDriver` — M7.
- `ReplayDriver` — M6, reading a recorded input stream.

*(This line named a `GhostDriver` alongside it and there is no such thing.
[ADR-0041](#adr-0041--a-replay-carries-its-whole-configuration-and-hashes-it-separately)
is later and decides it: a ghost is a **transform** stream, not an input stream,
because re-simulating something that is diverging from the live session by design
buys nothing and costs a second vehicle solve. A ghost is therefore not a producer
of `DriverInput` at all and never reaches this interface.
`src/session/kart_ghost.h` says so at its registration.)*

**Freshness is a tick stamp, not a convention.** A push model has one failure
mode: a producer that does not run, or runs after the body in tree order, leaves
the vehicle consuming last tick's input, and *that reads as a physics bug*. So the
tick the input was filled on travels with it; `KartBody` compares it against the
current tick and, on a mismatch, applies **neutral input and says so once**. A
stale-input bug that costs a session is a stale-input bug that announces itself.

*(Implemented as a second argument to `set_input` rather than as a field on
`DriverInput`, which is what this ADR said. Two reasons, both found while writing
it: `DriverInput` is passed straight into `KartVehicle::step`, so a field the
solver must ignore is a field that eventually gets hashed by accident and moves
every recorded state hash in the project — and a struct field defaults to zero and
can be forgotten, where a required argument cannot be.)*

Neutral rather than held-last is deliberate: holding the last input means a
crashed AI drives into a wall at full throttle, and worse, it means a divergence
between two runs is invisible in the hash for as long as the throttle is the same
either way.

**Alternatives, and why not.**

- *A mode enum on `KartBody`* — smaller diff today, and it keeps the vehicle
  knowing about input sources. Every new source then edits the sim's hottest
  file, and `src/vehicle/` keeps its dependency on Godot's `Input`.
- *An `InputSource` abstract base with a virtual `next()`* — the conventional OO
  answer, and it fights ADR-0017. `src/core/` is a godot-cpp-free zone; a player
  source needs the action map, so the interface either straddles that boundary or
  drags godot-cpp across it. It also puts a virtual dispatch in a 120 Hz path to
  solve a problem that a setter solves.

**Consequences.**

- The vehicle becomes testable without an engine in a way it currently is not:
  a fixed input stream in, a state hash out.
- Determinism gets cheaper rather than harder. A replay is the input struct per
  tick, and the struct is now the actual interface rather than an internal detail
  that happens to have a name.
- The shell can gate input, which is what makes a pause menu and the tuning
  overlay's key bindings tractable instead of a fight with the `Input` singleton.
- **A recorder taps what the solver was given, not what a producer sent.** This ADR
  did not mention the shift latch, and `KartBody` ORs `request_shift_up` /
  `request_shift_down` into the struct *after* both input branches have run. A
  recorder listening to the producer silently drops a shift the live run acted on —
  a divergence with no visible cause. `vehicle_state.h` now says so beside the
  struct.
- **`DriverInput::steer` became unambiguous, and it was not before.** The scripted
  branch assigned a *lock fraction* and the polled branch assigned the steering
  curve of a *stick position*, so the same stored number described two different
  steering angles depending on who filled it. That is fine as an asymmetry between
  a scripted run and a driven one, and fatal once a replay records the field. The
  curve moving out to `PlayerDriver` is what fixes it: every producer now hands over
  a lock fraction.
- `steering_curve` leaves `kart_body.cpp`. `drive.sh`'s figures must not move —
  the curve is applied to the same value in the same order, one node earlier —
  and that is an acceptance item, not an assumption. If any §6.4 figure moves,
  the move is a defect in this change and not a new measurement.

**What this does not decide.** What a replay records *besides* input — the
session configuration, the class, the assists, the tuning preset that ADR-0037
deliberately keeps out of `StateHash` — is a separate decision and a separate
ADR, owed before M6 writes a format. That is
[ADR-0041](#adr-0041--a-replay-carries-its-whole-configuration-and-hashes-it-separately).

---

## ADR-0041 — A replay carries its whole configuration, and hashes it separately

**Status.** Accepted, not yet implemented. Owed by
[ADR-0040](#adr-0040--input-is-handed-to-the-vehicle-not-fetched-by-it) and due
before ROADMAP M6 writes a format. Applies
[ADR-0037](#adr-0037--tuning-is-an-audit-trail-that-happens-to-be-adjustable-and-a-preset-is-not-in-the-state-hash)'s
separation rather than reversing it.

**Context.** M6 specifies a replay as "seed + input stream + tick count", and
that is sufficient only if everything *else* about the run is identical by
assumption. It is not. A session also carries a track and a layout, a session
type, a class, assists, a field, surface state, and a tuning preset — and
ADR-0037 deliberately keeps the preset out of `StateHash`, because a hash that
mixes configuration into simulation state cannot tell you *which* of the two
diverged. That decision is right and it leaves a hole: something has to carry the
configuration, or a replay recorded under a preset silently re-sims under the
defaults and reports a determinism failure that is nothing of the sort.

**Decision.** The replay carries its entire configuration inline, and fingerprints
it with a hash that is **not** `StateHash`.

```
header    format_version, build and extension API version
          track + layout + session type
          class, assists, field, surface
          tuning preset — the full diff, inline, not a path
          config_hash — over all of the above
          seed, tick_count
tick_hz   the physics rate the run was recorded at
body      DriverInput per tick, per kart
footer    state hash every N ticks
```

*(Two corrections to the sketch above, both found while implementing it. The
struct is `kart::core::DriverInput`; `VehicleInput` does not exist — same error as
ADR-0040. And `tick_hz` was missing entirely: a run recorded at 120 Hz and re-simmed
at 240 Hz has an identical `config_hash` and a completely different lap, which this
ADR's two-outcome scheme classifies as "a real determinism bug" and which is nothing
of the sort. It was first carried in the header with its own refusal reason, as a
workaround. Issue #174 then made the open call: it is a hashed `SessionConfig`
field now, the header's `tick_hz` line fills it, the `TickRate` refusal reason is
gone, and a rate mismatch refuses through the ordinary path below — naming
`tick_hz` and both values. Nothing had shipped, so no stored `config_hash`
was invalidated by the schema change.)*

Playback compares `config_hash` **first**. That ordering is the whole design:

- config mismatch → **refuse, and name the field that moved.** "This replay was
  recorded with `frame_torsion` at 210.0; the current default is 193.62" is a
  sentence a person can act on.
- config matches, state hash diverges → a **real determinism bug**, and now it is
  unambiguous, which is the property ADR-0037 was protecting.

**The preset goes in as a diff, not as a path.** ADR-0037's format is already a
diff against the sourced defaults, so it is small, and a path is a reference to a
file that can be edited or deleted afterwards — a replay that breaks when someone
tidies `user://tuning/` is a replay that cannot be trusted for anything.

**Text header, binary body.** The header is the part a person reads, diffs and
files a bug with, so it stays text in the style ADR-0037 already established. The
body is not: a 15-minute round is 108,000 ticks, and eight karts of uncompressed
float input is about 20 MB. Quantized to fixed point it is under 8 MB, and it is
never committed — replays live in `user://`.

**And the quantization has a determinism trap in it, which is the reason it is in
this ADR rather than left to the implementation.** If input is quantized *on
write*, the live run consumed full-precision values and the replay consumes
rounded ones, and the two diverge for a reason that looks exactly like a solver
bug. **The producer emits already-quantized input** — `PlayerDriver` rounds before
`KartBody` ever sees it — so the live run and the replay consume bit-identical
values by construction. This is the same class of defect as
[ADR-0033](#adr-0033--the-contact-boundary-measured-and-the-lever-arm-it-caught)'s
lever arm: correct arithmetic applied at the wrong point in the chain.

**A ghost is not a replay.** A replay re-sims; a ghost is drawn beside a live
session that is diverging from it by design, so re-simulating it buys nothing and
costs a second vehicle solve. A ghost is a **transform stream**, sampled and
interpolated, with the lap time and the sector splits in its header. Two formats,
because they are two problems — and M6's line item is written as though they were
one.

**And the ghost does not take this ADR's versioning policy — it takes
[ADR-0042](#adr-0042--a-save-always-loads-and-the-migration-tests-eat-real-old-files)'s.**
Refusing on a version mismatch is right for a replay because a replay is a
diagnostic artifact. A ghost is referenced by id from `profile.save` as half of
"best lap and ghost per track per layout per class", which makes it **user data**,
and ADR-0042's whole argument is that refusing to load user data is deleting it with
extra steps. A refused ghost is a deleted best lap. `src/core/ghost.h` migrates an
older ghost and only refuses one written by a newer build than can read it.

**Versioning.** `format_version` is refused across a mismatch, not migrated: a
replay is a diagnostic artifact, not user data, and silently migrating one
produces a plausible-looking run of something that never happened. The build and
extension API version are recorded and **warn** rather than refuse, because
cross-build determinism is not claimed — ROADMAP defers cross-platform bit
determinism explicitly, and a warning that says "this was recorded on a different
build" is the honest form of that.

**Where it lives.** The header, the hashing and the quantization are pure
arithmetic over plain structs and belong under `src/core/`, engine-free, held by
`tests/run.sh` per ADR-0017. Only file I/O sits on the Godot side.

**Consequences.**

- `SessionConfig` becomes a real type with a `hash()`, modeled on
  `TuningSet::hash()`. It is also exactly what the session runner in
  `ARCHITECTURE.md` §17 takes as its argument, so this ADR and that structure are
  the same object seen from two sides.
- The determinism harness gains a failure mode it did not have: *refused*, which
  is distinct from *passed* and from *diverged*, and CI has to treat it as such
  rather than folding it into a red.
- Saved presets become shareable with a replay attached, which is the first thing
  in this project that is worth sending to another person.

**What this does not decide.** How a career save is versioned and migrated — that
*is* user data and the opposite policy applies. That is
[ADR-0042](#adr-0042--a-save-always-loads-and-the-migration-tests-eat-real-old-files).

---

## ADR-0042 — A save always loads, and the migration tests eat real old files

**Status.** Accepted, not yet implemented. Pairs with
[ADR-0041](#adr-0041--a-replay-carries-its-whole-configuration-and-hashes-it-separately)
and deliberately takes the **opposite** policy on a version mismatch, because the
two files are not the same kind of thing.

**Context.** A replay is a diagnostic artifact and refusing to load a stale one
costs nobody anything. A career save is the record of an evening someone spent,
and `docs/GAMEDESIGN.md` puts a whole season behind it. Refusing to load it is
deleting it with extra steps.

**Decision.** One text format, versioned, with a written migration per bump.
**A save never fails to load because of its age.**

```
user://profile.save        text, diffable, in the family of the preset format
    version   = 3
    driver    = name, number, nationality, livery
    career    = class, season, round, standings
    bests     = per track, per layout, per class — lap time + a ghost id
    ...
user://ghosts/<id>.ghost   transform streams, referenced not inlined
user://settings.cfg        separate file, see below
```

Loading chains migrations — `v1 → v2 → v3` — and each one ships with a test. The
`version = 3` above is an illustration of a format that has been bumped twice, not
where this starts: the corpus holds exactly one real file and it is a v1.

*(The field list said `name, number, nationality` and omitted the livery, which
`docs/GAMEDESIGN.md` §8 has. §8 is the one that is right and `src/core/profile.h`
follows it.)*

**The test is the actual decision here.** A migration test that generates its own
v1 input tests nothing: it round-trips today's writer through today's reader and
passes forever, including on the day the migration is wrong. So `tests/data/saves/`
holds **real captured files**, one per historical version, checked in and never
regenerated. When v4 arrives, a real v3 file joins the corpus. The corpus only
grows.

**Settings live in their own file, and that is not tidiness.** If `profile.save`
is unreadable, a player still has to reach the menu, read the text, and use the
pad — which means the comfort options, the control bindings and the accessibility
settings in `ARCHITECTURE.md` §18 must load when the career does not. Coupling
them means a corrupt career save presents an unreadable menu for fixing it.

**Corruption is not a version problem and gets its own rule.** A file that does
not parse is **moved aside, not overwritten** — `profile.save.corrupt.1` — a fresh
profile is started, and the game says where the old one went. The failure mode
this exists to prevent is the ordinary one: something goes wrong at load, the
game starts fresh, the player plays for an hour, and the first successful *save*
destroys the evidence.

**Writes are atomic.** Write a temporary file, flush and close it, confirm its
length off disk, rename over the target. A save interrupted by a crash leaves the
previous save intact rather than a half-written one, and rename is the only
operation that gives that for free.

*(This said "fsync" and a power cut. **Godot's `FileAccess` cannot sync** — 68
methods, none of them `fsync`, `F_FULLFSYNC` or `O_SYNC`, and nothing in
`DirAccess` either, checked against `extension_api.json` rather than remembered. So
the guarantee is scoped to **process death**, which the rename genuinely does buy,
and a power cut is not covered: a close is not a sync, and without one POSIX does
not order the data against the directory entry. The wording mattered because an
implementer following it believes they bought durability they did not. An fsync
shim on the GDExtension side is owed a ticket. The length check before the rename
is the reachable part, and it is what catches a full disk —* `store_buffer`
*returning true says the calls were accepted, not that the bytes landed.)*

**Version bumps on every format change, and a migration may be the identity
function.** The alternative — bump only on incompatible changes — requires
somebody to correctly classify a change as compatible, at the moment they are
thinking about something else. An identity migration costs one line.

**Ghosts are referenced, not inlined.** They are transform streams, they are the
largest thing in the profile by an order of magnitude, and a profile that has to
be fully parsed to read a driver's name is a profile that gets slow exactly as
someone plays more.

**Alternative rejected: best-effort load** — parse what is recognized, discard
what is not, default what is missing. It costs nothing to write and it fails
silently in the one case that matters. Rename `standings` to `championship` and
every existing career resets to empty, with no error raised and nothing for a
player to point at. Silent data loss is worse than a refusal, and this ADR is
buying neither.

**Where it lives.** Parsing, versioning and the migration chain are pure string
and struct work and belong under `src/core/`, engine-free and covered by
`tests/run.sh` per ADR-0017 — the same place ADR-0041 puts the replay header.
Only the file I/O and the `user://` path resolution sit on the Godot side.

**Consequences.**

- `tests/data/saves/` becomes a permanent, append-only asset, and deleting
  anything from it is a defect rather than housekeeping.
- The save format is diffable, so "what changed in my career" is `git diff` on a
  copied file, and a bug report can carry a save inline.
- Multiple profiles are not designed in and not designed out; the format is a
  single profile per file, which makes a second one a path change rather than a
  schema change.

---

## ADR-0043 — The race rules are arithmetic, so they go where the tire model went

**Status.** Accepted, not yet implemented. Completes the structural set opened by
[ADR-0040](#adr-0040--input-is-handed-to-the-vehicle-not-fetched-by-it), and
supplies the `SessionConfig` that
[ADR-0041](#adr-0041--a-replay-carries-its-whole-configuration-and-hashes-it-separately)
requires.

**Context.** `docs/GAMEDESIGN.md` §4 puts four separate points tables in a single
race weekend, and three of them begin with 50 or 25:

| Table | Scale, to 8th |
| --- | --- |
| Qualifying Heat, position points | 50, 44, 41, 38, 36, 34, 32, 30 |
| Super Heat, position points | 90, 80, 72, 66, 60, 54, 50, 46 |
| Championship, heats and Super Heat classifications | 25, 22, 19, 17, 15, 13, 11, 9 |
| Championship, Final | 50, 44, 38, 34, 30, 26, 22, 18 |

`docs/REFERENCES.md` says the sharp thing about them: **collapse any pair and the
standings look entirely plausible and are simply wrong, forever, because there is
no render to disagree with it.** A wrong tire curve produces a kart that drives
badly and somebody notices in ten minutes. A wrong points scale produces a
championship table that is subtly not the real one and nobody ever notices at all.

**Decision.** The rules are pure arithmetic over plain structs, so they live where
the tire model, the gearbox and the tuning audit already live: `src/core/`,
engine-free, no godot-cpp on the include path, covered by `tests/run.sh` in
seconds with no engine per
[ADR-0017](#adr-0017--srccore-is-compiled-without-godot-cpp).

```
src/core/race_rules.h    the four scales, aggregation, tie-breaks, part-distance
src/core/session.h       SessionConfig + hash()  — ADR-0041's header type
src/core/standings.h     the season table, and promotion
```

**The scales are constants with their citation attached**, in the style
`kz_reference.h` already uses for the KZ2 reference figures — the article number
goes in the comment beside the number, because a scale with no provenance is
indistinguishable from a scale somebody adjusted for feel.

**And one test exists purely to catch the failure the sourcing warned about:**
assert the four scales are **pairwise distinct**. It is a strange-looking test and
it is the highest-value one in the file, because the defect it catches is a
copy-paste that produces working code.

*(It proves less than it looks like it does, and the simulation found out why.
`CHAMPIONSHIP_FINAL_POINTS` is **exactly twice** `CHAMPIONSHIP_HEAT_POINTS`, place
for place, for all eight places an 8-kart field has — the real scales diverge only
at 10th, 10 against 6. So the four are pairwise distinct and a `2 * heat_scale`
implementation would still pass. `race_rules.h` pins the doubling with its own test
so that the coincidence is recorded rather than relied on, and the same fact is why
a championship total cannot be decomposed back into positions: a half-credit Final
award and a full-credit heat award are the same number.)*

The rest of the suite is ordinary and mostly writes itself: a tie on equal points
resolves to the Qualifying Practice classification at every stage; part-distance
scaling gives zero under 2 laps, half under 75% of the scheduled distance and full
at or above it; a Final awards its fastest lap one extra championship point; and
at four rounds nothing is dropped, because the discard rule only engages at five
Competitions or more.

**Two rules in the module are ours and are marked as such in the source**, not
just here: truncating a 36-place scale to an 8-kart field, and promotion at top 3.
The first has an alternative that was rejected — rescaling the values to spread
across 8 — because real numbers exist and inventing a spread throws them away.

**Sequencing is arithmetic too, and it also goes in.** Which session runs next,
whether a round is complete, whether a season promotes — none of that touches the
engine. `src/core/` gets the progression as a pure state machine, and GDScript is
left with what only GDScript can do: load a scene, draw a table, move a focus
ring. That is a boundary this project already knows how to hold.

**Consequences.**

- The entire race format is testable before a single menu exists, and a season can
  be simulated end to end in a unit test in milliseconds — eight drivers, four
  rounds, a promotion — which is the cheapest possible way to find out that the
  structure in `GAMEDESIGN.md` §4 does not actually work.
- `SessionConfig` has one definition, shared by the session runner, the replay
  header and its hash. ADR-0041 and `ARCHITECTURE.md` §17 stop being two
  descriptions of the same object.
- The rules cannot read a node, a scene or a setting, which is the point: a
  classification is a list of driver ids and results, and it stays that way.

---

## ADR-0044 — UI text is English literals, and two rules keep the retrofit cheap

**Status.** Accepted. A small decision recorded because the alternative is that it
gets made by accident, forty screens in.

**Context.** Localization is post-demo in both `ARCHITECTURE.md` §20 and
`ROADMAP.md`, and on a portfolio piece it may never arrive at all. But every
string written from ROADMAP M3c onward is either a key or a literal, and switching
afterwards is a wide mechanical pass.

**Decision.** Plain English literals, American English per the project's own
convention — tire, meter, license, curb.

The alternative was keys and a CSV from day one. Rejected on three counts: a name
per string forever on a solo project; the code stops showing what the screen says,
which matters most in exactly the file where copy is being written; and Godot's
`tr()` returns the key unchanged when it is missing, so the failure mode is
**shipping a screen that reads `MENU_START_SESSION`**. Catching that wants a
verify gate, which is more work than the keys were saving.

**What makes the retrofit tractable, and it is worth writing down now so a later
session does not mistake it for a rewrite:** a Godot `Control` passes its `text`
property through `tr()` automatically. Converting a literal to a key is replacing
the string, not rewiring a call.

**Two rules, and they are the whole reason this ADR exists.** They cost nothing
today and they are the difference between an afternoon per screen and a rewrite:

1. **Never build a sentence by concatenation.** `"Round " + n + " of " + total` is
   three fragments no translator can reorder, and word order is exactly what
   changes between languages. Use one format string with placeholders —
   `"Round %d of %d"` — so the unit of text is a whole sentence.
2. **UI text lives on the scene and script side, never in `src/`.** That boundary
   already exists for an unrelated reason: `godot::String` decodes a bare
   `const char *` as Latin-1, so a non-ASCII character in a C++ literal arrives as
   mojibake, and everything in `src/` is ASCII by rule. Any language this would
   ever be translated into breaks that rule immediately, so translated text could
   not live there anyway.

**Consequence.** The cost is recorded rather than avoided: a localization pass has
to find every literal, and it is real work. It is bounded, it is mechanical, and
it only happens if localization does.

---

## ADR-0045 — Menus take pad, keyboard and mouse, and hover moves the focus ring

**Status.** Accepted, not yet implemented. Applies to ROADMAP M3c.

**Context.** This project is driven on a pad and every control failure it has had
came from an assumption about input that nothing checked — four in one day, which
is why [#169](https://github.com/skiretic/kartgame/issues/169) exists. Menus
inherit that history and add one of their own: **Godot's built-in `ui_up` /
`ui_down` default to the arrow keys, and the arrow keys are already the second
binding on throttle, brake and steer.** That collision is not hypothetical — it is
why the tuning overlay navigates on PgUp/PgDn and `[` `]` rather than the arrows,
recorded in CLAUDE.md.

**Decision, part one: menus declare their own actions.** `menu_up`, `menu_down`,
`menu_left`, `menu_right`, `menu_accept`, `menu_back`, generated into the InputMap
by the same script that generates the driving actions, so the serialization cannot
be typo'd. The engine's `ui_*` set carries roughly twenty default bindings that
this project did not choose and will not see until one misfires.

**Decision, part two: all three devices are supported** — pad, keyboard and mouse.
A desktop release with no pointer is a conspicuous omission, and the accessibility
commitments in `ARCHITECTURE.md` §18 already promise full remapping and adjustable
deadzones, which is most of the same work.

**And one rule makes that one interaction model rather than two: hovering moves
the focus ring.** The mouse does not get a second highlight state of its own. Move
the pointer over a row and that row *becomes* the focused row; click activates
whatever is focused. So every screen has exactly one selected thing, one visual
state to design, and one state to test — and the focus ring stays meaningful when
a hand goes back to the pad mid-screen.

The alternative, hover as a distinct state alongside focus, is what makes a screen
cost twice: two highlight treatments that must agree, a defined precedence when
they disagree, and a stale focus ring left behind the pointer.

**Supporting rules.**

- The cursor **hides on pad or keyboard input and reappears on mouse motion**, so
  a pad player never has an abandoned pointer sitting on the screen.
- Nothing is reachable by pointer alone. Every action has a focus path, which is
  what makes the pad complete rather than a courtesy.
- The storyboard was drawn assuming a focus ring and no cursor. That is still the
  primary read; the pointer is an additional way to move the same ring.

**Consequence.** [#169](https://github.com/skiretic/kartgame/issues/169)'s gate
extends to menus: whatever a screen says its controls are, something asserts they
exist in the InputMap and are read. The failure this project keeps having is not
a wrong binding — it is a **binding that is advertised and unread**, and a menu is
the easiest place in a codebase for that to happen quietly.

---

## ADR-0046 — `track.json` owns the whole track, and furniture is placed by distance

**Status.** **Implemented**, ROADMAP M5. The schema it decided is written out in
[`docs/TRACK_SCHEMA.md`](TRACK_SCHEMA.md), the executable copy is
`src/core/track.h`, and `data/tracks/valdirone_nuova.track.json` is the first
circuit authored into it. Three questions this entry did not settle turned up
during the implementation and are their own decisions:
[ADR-0048](#adr-0048--the-spline-stores-curvature-checks-position-and-interpolates-height-as-a-cubic),
[ADR-0049](#adr-0049--one-piece-of-concrete-gets-one-geometry-and-the-gentler-case-wins)
and [ADR-0050](#adr-0050--the-starting-grid-is-ours-and-it-is-namespaced-so-it-reads-that-way).
Extends `ARCHITECTURE.md` §11's "one definition, two consumers" to a third
consumer.

**Context.** §11 already argues the principle: the spline is authored once and
read by `gentrack.py` and by the C++ extension, so what you see and what you
collide with cannot drift apart. `docs/GAMEDESIGN.md` adds a third reader — the
session runner needs a start line, grid slots, sectors and checkpoints — and a
second circuit with reverse layouts. If the schema is written for one circuit run
one way, the second one changes it.

**Decision.** One file owns everything the track is.

```
meta        name, length, direction of travel
spline      control points: position, width, banking, elevation
surfaces    spans -> asphalt / curb / grass / dirt
furniture   start line, grid slots, sector marks, checkpoints,
            repair area, pit entry
layouts     forward, reverse — each naming its own sector order,
            grid placement and racing-line seed
```

**Furniture is placed by arc length from the start line, in meters — not by
control-point index.** This is the schema decision that matters most and it is
invisible until the day it bites: with indices, inserting one control point to
smooth a corner silently renumbers every checkpoint and sector behind it. Meters
also match how the regulations already think — a Final is 30 km, and a lap count
falls out of the circuit.

**Reverse is an authored layout, not a programmatic reversal.** Flipping the
spline is trivial and everything attached to it is not: curbs sit on the inside of
the corner they serve, run-off is sized for an approach speed that changes,
braking zones move, and a sector split that made sense clockwise lands mid-corner
counter-clockwise. Each layout names its own furniture and its own racing-line
seed. The geometry is shared; nothing else is assumed to be.

**Coordinates are Godot's** — meters, Y-up, -Z forward — and `gentrack.py`
converts once on read. The kart pipeline's convention is the opposite way round,
building toward Blender's +Y, and `export_yup` maps it. Doing the same here would
put a second conversion in the file that the *runtime* reads, and the runtime is
the consumer that must not have a transform bug. One conversion, in the tool, in
one place.

**Validation is part of the schema, not a later pass.** M5's accept already
requires rejecting a deliberately self-intersecting spline; this widens it,
because furniture can be wrong in ways geometry cannot: grid slots must fit on the
track surface, sector marks must be ordered and distinct, checkpoints must be
ordered and must close the loop, and no radius may be tighter than the kart can
physically take. A track that loads and cannot be raced is worse than one that
refuses to load.

**Versioning refuses rather than migrates**, opposite to
[ADR-0042](#adr-0042--a-save-always-loads-and-the-migration-tests-eat-real-old-files)
and for the same reason it applies there in reverse: `track.json` is authored
project data under version control, not user data. A schema bump is a commit that
edits the tracks in the same commit, and a loud failure is correct.

**Consequence for replays.** The track's content hash enters `SessionConfig` per
[ADR-0041](#adr-0041--a-replay-carries-its-whole-configuration-and-hashes-it-separately),
so a replay recorded before a corner was smoothed refuses to play back and says
which file moved — rather than re-simulating against different collision geometry
and reporting a determinism failure that is really a content change.

**Consequence for the grid.** Slot count is data, not a constant. The design's
eight karts is a number in a file, which is what lets the audio measurement it
comes from move it without a code change.

---

## ADR-0047 — The field is an authored roster of invented drivers

**Status.** Accepted, not yet implemented. Supplies the entry list that
`docs/GAMEDESIGN.md` §7 makes the whole of the fiction.

**Context.** Seven of the eight karts on the grid are rivals, and §7 commits to
carrying the entire fiction through entry lists, number panels, timing screens and
standings — no prose, no dialogue. That only works if the drivers on those screens
have texture. It also has to survive a replay: the field is part of the session,
so it is part of `SessionConfig` per
[ADR-0041](#adr-0041--a-replay-carries-its-whole-configuration-and-hashes-it-separately).

**Decision.** A hand-authored roster in `data/drivers.json`, roughly sixteen
drivers, eight drawn per season by the season seed. Each carries name,
nationality, race number, entrant, entrant nationality, equipment, livery, and a
**frozen** set of the difficulty parameters `ARCHITECTURE.md` §10 already names —
lookahead, throttle discipline, grip ceiling, mistake rate.

Frozen is what makes a rival. The driver who beat you in round 1 drives the same
way in round 4, and the standings do the storytelling that no text has to.

**The names are invented, and this needs saying because the sourcing makes it easy
to get wrong.** `docs/REFERENCES.md` records a real published entry list, and it
was read for its **shape**: "Surname, Forename", two nationality fields that
routinely differ, chassis/engine/tire as one slash-joined string, numbers issued
in per-category blocks. Those are conventions and they are exactly what to copy.
The people on it are not. Real karters' names and nationalities are not this
project's to ship, and a roster assembled by lifting an entry list would be
copying the one part of the reference that was never the point.

**Equipment is authored uneven, because real fields are.** Of 101 OK entries at
the sourced event, "KR / IAME / Maxxis" appears 51 times and the rest spread thin
across six chassis makes. A uniform-random assignment reads immediately as
generated; a dominant combination with a scattered tail reads as a paddock. The
tire make is uniform across the whole field, because it is a mandated control
tire, and getting that detail right costs one line.

**Alternative rejected: generation from seeded pools.** Infinite fields, zero
authoring, and no driver who means anything the second time you see them — which
is the only thing this system exists to produce. Sixteen drivers is an afternoon.

**Where it lives.** The file is repository data, versioned like `track.json` and
refusing rather than migrating on a schema bump. Parsing is engine-side; the rules
and the AI consume plain structs, so `src/core/` stays engine-free per ADR-0017.

**Consequences.**

- The player's own number is picked at profile creation from the free numbers in
  the class's block — 1xx for OK — which is a small thing that makes the entry
  list read as one document rather than as the player plus some AI.
- The seeded draw and the roster's content hash both enter `SessionConfig`, so a
  replay reproduces the field it was recorded against rather than whichever eight
  today's seed picks.
- Adding a driver is a data commit. Adding a *difficulty tier* is not — the tiers
  scale the roster's parameters rather than replacing them, so a season at a
  higher tier is the same rivals driving better.

---

## ADR-0048 — The spline stores curvature, checks position, and interpolates height as a cubic

**Status.** Accepted and implemented, ROADMAP M5.
[ADR-0046](#adr-0046--trackjson-owns-the-whole-track-and-furniture-is-placed-by-distance)
decided that `track.json` owns the whole track and said its spline carries
"control points: position, width, banking, elevation". Writing the loader turned
that one line into three questions it did not answer.

**Context.** A circuit is authored from a design whose corners are exact — a 15 m
hairpin is 15 m and a 110° corner is 110° — and it is consumed by a mesh
generator, a collider and a validator that all have to agree with each other to a
millimeter. Positions alone cannot express that: a corner sampled as a polyline
has a *radius that depends on the sampling rate*, and the radius is what decides
whether the corner is on the safe side of #137.

**Decision, one.** **A span between two control points is a circular arc of
constant curvature, and `distance_m` and `curvature_1pm` are normative.**
`position` and `heading_deg` are stored too, and the loader **verifies** them
against its own walk to 1 mm and 1e-4 rad and then never reads them again.

Storing both looks redundant and buys two things that are not. The file stays
readable and plottable without running an integrator — which is what makes a
diff of a corner change reviewable. And a hand edit that moves a control point
without fixing the curvature is a **load failure** rather than a road that
quietly bends somewhere else three hundred meters later. That check has already
paid for itself once: it is what caught a 0.0001 m span produced by a taper that
ended exactly where a segment began.

**Decision, two.** **Elevation interpolates as a cubic Hermite on
`(elevation_m, grade_pct)`, not linearly.**

This is not a smoothness preference, it is exactness. CIK-FIA Part I §7.2 fixes a
vertical curve's radius, so a legal grade change is a **parabola**; a parabola is
a cubic; and cubic Hermite through two endpoints with their two endpoint slopes
is the unique cubic matching four constraints, so it reproduces that parabola
*identically*. Valdirone's entire longitudinal profile — three vertical curves
over 1,375 m — therefore needs six control points and is exact everywhere between
them.

Linear would have needed a control point every few meters and would still have
been wrong in a way that is felt rather than seen: at 5 m spacing on the crest the
grade steps 0.26% at every point, which is 0.10 m/s of vertical velocity arriving
inside one tick at 140 km/h. That is a bump the road does not have, and it would
have been indistinguishable from a suspension defect.

The rule that replaced the obvious-but-vacuous check is worth recording. "Does the
elevation profile close over a lap" is **structurally true** and cannot fail:
elevation is stored per control point rather than integrated, so the spans'
height changes telescope to zero. What *can* fail is consistency — Hermite will
happily interpolate a 3 m climb across a span whose two ends both read flat, and
draw a hump the design does not contain with the collider agreeing. The check is
therefore that each span's mean grade lies between its two end grades, which is
exact for both shapes the schema allows.

**Decision, three.** **Validation is part of `load`, and `load` refuses.** Twenty
rules, listed in `docs/TRACK_SCHEMA.md` and implemented in `src/core/track.h`,
covering the geometry, the kart, the regulation's dimensions and the furniture. A
track that fails any of them does not load. A track that loads and cannot be
raced is worse than one that will not load: the failure otherwise surfaces three
hundred meters into a session, at a corner nobody can take, as a physics bug.

The rule the project had already got wrong twice is stated in the header where it
is implemented, because it will be got wrong a third time otherwise. §11 and
ADR-0046 both say "no radius the kart physically cannot take" and `track.json`
stores the **centerline**; written from those words the check rejects every
driveable circuit ever designed. Valdirone exceeds 1.86 g on its own centerline
radius at all eight corners — 3.66 g at Il Ciglione. Nobody drives the centerline.
The check is against the racing line's radius and the threshold is
`min(grip, lock)`, because six of the eight corners are limited by steering lock
and not by the tire.

**Consequences.**

- The two geometry consumers share no code — `src/core/track.h` is C++ inside the
  engine, `tools/blender/tracklib/geometry.py` is Python inside Blender — so
  §11's "what you see and what you collide with cannot drift apart" rests
  entirely on the rules above being written in arithmetic. `gentrack.py` writes
  the road's edges every 25 m into a committed manifest and
  `tools/verify/circuit.sh` compares them against the collider's. **Measured at
  0.034 mm worst disagreement over 56 stations.** Without that number the claim is
  a comment.
- The self-intersection gate needed two corrections nobody had written down. The
  clear-ground requirement is `6 + h_a + h_b` and not a flat constant — a flat
  14 m is only correct at the 8 m width floor and passes an illegal layout
  anywhere wider, which four independent circuit designs published. And the
  along-lap exclusion has to skip the **same continuous feature**, not just a
  40 m window: their version reported a hairpin's own two tangents, and this one
  reported the start/finish straight's own two ends at 90 m until same-feature
  pairs were excluded outright.
- The gate carries a negative control, `data/tracks/self_intersecting.track.json`
  — a circuit that closes, turns +360°, is 1,105 m long, has legal camber and its
  grid on a straight, and crosses itself. It breaks exactly one rule on purpose,
  and `circuit.sh` fails if it loads. Same principle as
  `input_push_probe.gd --break`.

---

## ADR-0049 — One piece of concrete gets one geometry, and the gentler case wins

**Status.** Accepted and implemented, ROADMAP M5. Settles a contradiction
[ADR-0046](#adr-0046--trackjson-owns-the-whole-track-and-furniture-is-placed-by-distance)
created and did not notice.

**Context.** ADR-0046 says the reverse layout is *authored* — "the geometry is
shared; nothing else is assumed to be" — and that each layout names its own
furniture. `docs/circuits/valdirone_nuova.json` takes that at its word and
specifies T8a's inside kerb as **30 mm rippled** forward, where it is an apex
kerb, and **25 mm flat** reversed, where the same edge is an exit kerb struck at
111.9 km/h with the corner tightening.

Both specifications are right about what that layout wants. One piece of concrete
cannot be two heights.

**Decision.** Where two layouts want different *geometry* in the same place, the
**gentler case is built**, once, and the deviation is recorded rather than
resolved silently.

That is not a new rule so much as the one the design already applies to run-off
and never generalized: every corner carries a forward approach speed and a
reverse approach speed, and *the larger drives the build*. T8 Uscita is approached
at 124.3 km/h forward and 142.2 km/h reversed — 1.31× the kinetic energy — so it
is built with 14 m of asphalt and 26 m of grass instead of the 10 plus 16 the
forward case alone needs. Nobody thought that was a contradiction. A kerb is the
same question with the sign flipped: the *gentler* geometry is the one that is
safe in both directions, exactly as the *larger* run-off is.

So `data/tracks/valdirone_nuova.track.json` builds T8a at 25 mm flat. Forward, the
apex kerb is 5 mm lower and smooth rather than rippled, which is a real loss of
feel and is the price. The design's own T3 exit kerb shows the author already
reasoning this way — it is specified flat *because* reversed it is an entry kerb
taken at 52 km/h.

**Decision, corollary.** The regulation's own cross-section ordering is the
geometry, and it is **track, verge, run-off** — not track, run-off. Part I §7.5:
1.80 m of compact verge along the whole length on both sides, then a run-off area
that "must grade to the verge without a negative slope".

Both consumers built the apron from the road edge first, so the apron and the
verge occupied the same 1.80 m band. Two consequences, one cosmetic and one not:
the verge vanished under the apron in every render, and the **collider had two
coplanar faces there** — the exact condition that makes a suspension raycast's
answer arbitrary along a whole boundary, which is the trap `ROAD_LIP` exists to
prevent on the other side of the same edge. The verge is now a collider as well
as a mesh, at `SURFACE_GRASS`, and the apron starts outboard of it.

**Consequences.**

- A layout may differ in *furniture* — sectors, checkpoints, grid, racing-line
  seed, pit stations — and in *sizing*, but not in a dimension of a physical
  object. `docs/TRACK_SCHEMA.md` says so where the `surfaces` array is described.
- Running 1.80 m wide at a corner now crosses grass before it reaches the asphalt
  apron. That is what the regulation describes and it makes running wide cost
  something, which is the correct direction; it is stated here because it will
  read as a bug the first time somebody feels it.
- The reverse layout still gets its own kerb *specification* in the design
  document. What it does not get is its own concrete.

---

## ADR-0050 — The starting grid is ours, and it is namespaced so it reads that way

**Status.** Accepted and implemented, ROADMAP M5.

**Context.** `docs/REFERENCES.md`'s circuit pass (#157) sourced nearly every
dimension a circuit has to Appendix No. 13 and the 2026 Part I text. It left one
gap and said so: **the starting grid**. CIK-FIA Part I §7.7 describes the starting
procedure and refers to **Appendix No. 10** for the grid; Appendices 9, 10 and 15
are referenced by the Part I text, are not published on fiakarting.com, and were
not located. Part I's own appendix section reads, in full, *"Annexes — Voir
fiakarting.com"*, which is the same trap that hid the track width in Appendix 13
for a milestone.

`ARCHITECTURE.md` §5 item 10, widened after ADR-0034: an externally-sourced
constant invented in a planning session is indistinguishable from a measured one
six months later, and this project has now paid for that twice — §6.4's lateral
band and the engine's harmonic ladder. The grid was about to be the third.

**Decision.** Pick the figures, say they are ours, and make the code say it at
every call site. Four numbers, in `kart::core::circuit::ours::`, each with a
`_SOURCED = false` beside it and each **derived from a clearance a reader can
check** rather than chosen:

| Figure | Value | What it is derived from |
| --- | --- | --- |
| Row pitch | 8.0 m | 4.37 kart lengths; 6.17 m of clear road behind the kart ahead in the same column |
| Stagger | 4.0 m | Exactly half the pitch, so no kart sits directly behind another and every kart has **2.17 m of clear road ahead of it** — which is the number that matters, because a KZ leaves the line on a clutch and a launch that bogs needs room that is not somebody's bumper |
| Column offset | ±3.0 m | On a 12 m start straight: 4.6 m of clear air between two 1.400 m karts abreast, 2.3 m from each outer edge to the white line |
| Front-row setback | 4.0 m | One stagger. Everything behind the front row is spent out of the 120–200 m the starting straight is allowed to be |

The separation matters more than the numbers. `circuit::` reads as "the FIA said
so" and `circuit::ours::` reads as "we picked this", at the call site, without a
comment. `src/core/circuit_reference.h` is the file, and it does for the circuit
what `kz_audio_reference.h` does for the engine note: one owner for every external
constant, with the unmeasured neighbours **named** rather than quietly filled in.

Two more figures live in the same namespace under the same rule. The
**superelevation runoff length** — the regulation says banking changes are "to be
made over an appropriate distance" and gives no distance; 20 m is 0.52 s at
140 km/h. And the **checkpoint spacing** — 100 m, which is not a regulation at all
because the FIA has no opinion on how a game validates a lap; it is the anti-cut
resolution, 7.3% of Valdirone's lap.

**Consequences.**

- Slot count is data and not a constant, so `KartTrack` *checks* rather than
  assumes eight: every slot has to fit on the road at its own station,
  `|lateral| + 0.700 + 0.120 ≤ width/2`, and has to be on a straight. A standing
  start on camber is not a standing start.
- Valdirone's grid of eight occupies 28.0 m of a 165 m start straight and leaves
  45.0 m of straight behind the last row. The design's own note said 49.0 m,
  counting only the pole column; the corrected figure is here.
- If Appendix 10 turns up, four constants change in one file and the derivations
  above say immediately what each change costs.

## ADR-0051 — A checkpoint is a mark, not a sector, and the timer keeps them apart

**Status.** Accepted, M5, issue #180.

**Context.** ADR-0046 put two lists in `track.json` and they answer different
questions. **Sector marks** are where the timing screen splits the lap: Valdirone
authors two, at 524 m and 902 m forward and 473 m and 862 m reversed, chosen as a
diagnostic partition of the circuit rather than as thirds. **Checkpoints** are the
anti-cut resolution: fourteen of them, every 98.2 m, and ADR-0050 says why 100 m
is ours rather than the FIA's.

`src/core/lap_timing.h` predates both. It held one array of marks and defined
`sector_count()` as `mark_count`, which is exactly correct while the two lists are
the same list — which they were, because nothing had authored either yet and
`SessionRunner` was cutting the lap into three equal thirds with a comment saying
it was a placeholder.

Handing a real circuit's marks to that timer reports Valdirone as a
**sixteen-sector** track. `LapRecord::sector_s` is `LAP_MAX_SECTORS` long, which is
eight, so the splits past the eighth were dropped by a bounds guard and the ones
that survived did not add up to the lap. Nothing crashed and nothing said
anything.

**Decision.** A mark carries a flag, `checkpoint_only`, and the two lists merge
into one ordered set through `LapMarks::from_stations`. **Every mark has to be
crossed in order or the lap was cut; only a mark that is not `checkpoint_only` may
put a split on the screen.**

Three details are load-bearing:

- **The flag is inverted.** `checkpoint_only` defaults to false, so zero
  initialization means "every mark starts a sector" — which is what a hand-built
  `LapMarks` and the existing authored-marks path already meant. Nothing that
  predates the flag changes behavior by acquiring it.
- **Coincident stations merge at a millimeter**, `LAP_MARK_MERGE_M`, and the
  *sector mark's* station wins. A checkpoint half a millimeter short of 524.0 that
  became the sector mark would put the split a hair off the number the design
  document specifies, and the schema already merges control points on the same
  rule for the same reason.
- **`from_stations` refuses rather than clamps.** Over capacity, over
  `LAP_MAX_SECTORS`, or a station off the lap all return an invalid `LapMarks` and
  the session is refused by name. A clamped set is a cut detector with holes in it,
  and the holes are exactly the checkpoints a driver could then skip for free.

`LAP_MAX_MARKS` goes from 16 to 32. Valdirone's merged set is 1 + 13 + 2 = **16**,
sitting precisely on the old ceiling, so a circuit with one more checkpoint than
this one would have been refused with an arithmetic reason nobody expects.

**A defect the new gate found, and it is the interesting one.** The lap arming
condition read *"a mark has been consumed in order, or — if the layout has only a
start line — half a circuit of travel"*. The marks a cut skips deliberately stay
owed, so a cut over the **first** mark of the lap left `next_mark_` at 1 for the
whole lap, and the lap never closed at all. `lap_timing.h` argues against exactly
that outcome three functions higher: *"a cut has to still complete its lap so that
the missed mark can invalidate it — swallowing the lap entirely would leave a
driver who cut a corner with no lap on the screen at all and no explanation."*
With three even sectors the first mark was 400 m past the line and nobody reached
it; with fourteen checkpoints it is 98 m past the line and anyone can. The two
witnesses are now an `||` rather than a fallback, and both are still required to
fail before a lap is swallowed.

**Consequences.**

- `SessionRunner.configure()` takes a **duck-typed course** rather than a
  `TrackLayout`. `TrackLayout` is a `Node3D` script and `KartTrack` is a
  `RefCounted` in the GDExtension; they cannot share a base class, so the contract
  is four required methods and three optional ones, checked by name at
  `configure()` and refused by name. Half a partition is refused outright: a course
  that publishes `sector_marks()` and not `checkpoints()` is a timing screen that
  reads right over a cut detector with 380 m holes in it.
- Track limits are measured against **the course's own width** where it has one.
  `TrackRibbon.TRACK_WIDTH` is 8.0 m and it is the *test track's* width; Valdirone
  runs 9.0 m to 14.0 m, so a constant would have struck out a lap for a wheel on
  asphalt the design widened on purpose, and let one through where the road is
  narrower — which is the silent direction.
- The timing screen's sector strip is `sector_count()` wide, which is the track's
  number. It was `best_sectors().size()`, so it drew one column until a valid lap
  existed and then jumped.
- `tools/verify/circuit.sh --case=timing` is the gate. It walks the timer round
  both layouts at a constant 22 m/s, where every split is the authored station over
  the speed in closed form, and carries its own negative control: a 40 m jump over
  a checkpoint that clears no sector mark. A timer that owed only its splits passes
  every other check in this file and fails that one.

## ADR-0052 — The paddock is a place, and the eight decisions that shape the shell

**Status.** Accepted, front-end design phase, issue #171. Design only — nothing
here is built, and the phase producing it deliberately writes no code.

**Context.** `GAMEDESIGN.md` §9 fixed the screen list and the storyboard fixed
what information lives on each screen. Between them they left eleven decisions
open, four of which block code rather than paint. This ADR records the ones now
settled. The visual pass itself (#171) stays blocked on reference photographs
and is not decided here — these are structure and rules, not looks.

**Decisions.**

1. **The paddock is a generated 3D environment, built in stages.** Not a 2D
   backdrop and not a one-shot vignette. It is produced by the same
   deterministic Blender pipeline as the kart — modules under `tools/blender/`,
   parameters single-owner, `--check` gate — because that pipeline is the
   project's way of making geometry and the paddock is geometry. Staged on
   purpose: stage 1 is a vignette (the kart on a stand, an awning, a parked
   camera, `look_env.gd` lighting), and the environment grows outward in later
   rounds — trailers, tire stacks, neighboring teams' setups. An earlier answer
   in this session chose the vignette as the *end state*; that choice was made
   against an inflated cost estimate ("weeks of Blender work" for what the
   pipeline does in days) and is superseded. The staging survives because it is
   how everything else here is built: a first pass that is wrong in useful ways,
   then rounds.

2. **There is no mode screen.** The three modes sit directly on the paddock.
   The flow loses a screen: boot → paddock → session setup → loading → driving.
   Storyboard plate 03 is deleted, and its one orphaned job — saying what a mode
   *is* in one line — moves onto the paddock's mode entries.

3. **An unbuilt mode is visible and says why it is unavailable** — "needs a
   field — M7" in the place a released build would put a difficulty hint.
   §13's rule that a stubbed mode reads worse than an absent one is about modes
   that *pretend*: a greyed row that does nothing, a grid that punts you. A card
   that states its precondition is a third thing, and it keeps the paddock from
   claiming the game is smaller than it is designed to be.

4. **Whether pausing invalidates the lap in progress is a player option**, and
   it lives with the assists. Default is on — invalidate — because pause is
   otherwise a free look at the corner ahead. The load-bearing consequence: a
   best lap set with pause-forgiveness enabled is **flagged on the saved
   record**, the same family as the assist flags, or every saved best silently
   absorbs an advantage the screen never mentions. A stored preference that can
   move a recorded number must leave a mark on the record it moved —
   `assist_settings.gd`'s rule, extended from validation figures to bests.

5. **The ghost delta is measured against the selected ghost and labeled as
   such.** In Practice today the ghost *is* the saved best, so the two candidate
   definitions produce the same number — which is exactly why the choice had to
   be written down before they diverge. The moment a ghost can come from another
   session, another layout's time, or another player, "delta to what I am
   racing" and "delta to my best" split, and the HUD follows the ghost it is
   drawing. Sector deltas against session best remain a separate channel, as
   `timing_hud.gd` already draws them.

6. **Settings are one grouped list, not tabs.** Section headers — comfort,
   controls, assists, audio — in a single scroll. A tab strip needs a second
   navigation axis on a pad; a list reuses the one interaction the tuning
   overlay already taught, d-pad up-down and left-right on a row. The menu's own
   `menu_*` actions apply, per the storyboard's settled note: the engine's
   `ui_*` defaults sit on the arrow keys, and the arrow keys are throttle,
   brake and steer.

7. **The driver's name is typed, in the sport's format.** Surname and forename
   are entered as two fields and rendered "Surname, Forename" everywhere the
   entry list does it. Empty is refused; beyond that the player may call
   themselves what they like — a standings table with "aaaaa" fourth is the
   player's own doing, and a name picker defending the fiction against its one
   human participant defends it from the wrong direction.

8. **Results list every lap**, scrollable, with best and the theoretical
   optimal pinned above the scroll and struck laps shown with their reason. A
   real classification sheet is the whole session on one page, and hiding rows
   from the person who drove them is the wrong kind of tidy. The optimal stays
   labeled for what it is — a sum of sector bests nobody drove.

**A second batch, settled in the same session, going through the remainder one
at a time:**

9. **The config hash is hidden, one keypress away.** Not on the default setup
   or results screens — a debug-family toggle, plus a "copy session info"
   action on results, puts it in a bug report when needed. A player never
   parses hex; a bug report never loses its best line.
10. **The standings table keeps both nationality columns *and* the equipment
    string.** The full real column set. The equipment string compresses to
    chassis / engine — the tire is a mandated control tire, uniform down the
    field, and drops to a footnote, which is effectively what the real sheet's
    uniformity already says.
11. **Practice cuts straight from loading to driving.** No countdown, no grid
    card. The kart is placed rolling at the pit exit, the first lap is an out
    lap and can never be a best. The runner does not pretend Practice has a
    race start.
12. **Boot refuses on a missing generated asset and prints the exact
    command.** No offer to run the generator — that bakes a Blender dependency
    into the shipped game, and the missing-asset state is a dev-checkout
    condition; a release build ships its assets.
13. **Pause and the session clock: one rule.** Practice has no session clock,
    so the question is moot there — the lap in progress is already covered by
    the invalidation option. In timed sessions the session clock keeps running
    under pause, as it does in the real thing: pausing costs track time.
14. **The loading tip line stays, corpus-gated.** Lines are authored from
    measured facts only — §6.4 figures, corner-specific behavior, class
    differences. If the pool is thinner than about twenty real lines at ship,
    the row is dropped rather than padded. The quality bar is part of the
    design, not an aspiration.
15. **Career creates the profile.** Picking Career with no profile runs the
    four-question first-run sequence as the career's own first step — signing
    an entry form is diegetic. No lock state, no detour.
16. **The first-run "where you are" panel is an invitation.** One authored
    line per empty slot, in the world's voice — "No entry filed — the OK season
    starts when you do" — rather than blank dashes (reads unfinished) or a
    hidden panel (the menu visibly restructures after one session).
17. **One setup screen for all four session types; a weekend renders it
    read-only where the round decides.** Circuit, layout and class show fixed;
    what a real entrant chooses — assists, tuning preset, ghost — stays live.
    One code path, and Qualifying still has the place you pick your preset.
18. **Audio is a settings section, and the probe reads the stored values.**
    Master, effects and engine sliders in the settings list like any player
    option, in `settings.cfg`; `audio_level_probe` reports against what is
    stored, so the audit still sees what the player moved. Player-facing and
    measurable are not in conflict — ADR-0039's Master-trim argument already
    depends on exactly this separation.

**Still open, both deliberately** — and one of the two has since moved: the
tuning overlay **stays in the shipped build as an option, provisionally**
(2026-07-28). The final call on where it surfaces — settings row, assist-family
toggle, or always-on-F2 — is still decided by driving, which has not happened;
what is settled is that it does not get cut while that evidence is missing. And
naming — deferred per §12 because it is cheap and late, and narrower than §12's
wording: the first circuit is already **Valdirone Nuova** (M5), so what remains
unnamed is the series and the second circuit.

**Consequences.**

- `GAMEDESIGN.md` §9 is updated to match; the storyboard artifact drops plate
  03, redraws the flow, and re-inks the settled marks.
- The pause option adds a field to the saved best-lap record (a flag set, like
  the assist flags) — filed as a ticket for after the design phase, not built
  now.
- The paddock environment gets a module plan in the kart pipeline's style,
  written from reference photographs before any module is coded. #171's
  reference gate applies to the paddock's geometry exactly as it does to the
  screens' typography.

## ADR-0053 — The shell is one scene, and the six decisions that make the front end buildable

2026-07-28. The mockups are approved — all ten screens of
`docs/mockups/frontend_family.html`, timing/results/standings at round 2 against
the real 2026 Genk documents, the other seven at round 1 — which ends the
question of what the front end looks like and opens the question of how it is
built. Six structural decisions, settled with Anthony one at a time, so the
build has a design instead of improvising one screen at a time.

**1. One shell scene, screens as a stack.** One root scene owns the 3D paddock
and a UI layer; every menu screen — setup, settings, standings, profile,
pause-adjacent paper screens — is a `Control` panel pushed and popped on a
stack. Only starting a session swaps to a track scene. The alternative, a scene
per screen, either reloads the paddock on every transition or duplicates it
behind each screen, and threads all state through an autoload. ADR-0052 already
decided modes have no screen of their own — modes *are* the paddock — so the
paddock persisting behind every menu is the design, not an optimization. The
demo definition's first line ("the game boots into itself") lands here:
`project.godot` finally sets a main scene, and it is the shell.

**2. Cross confirms, Circle backs, and menus are their own input context.**
PlayStation-standard on the DualSense Anthony holds. Driving already binds Cross
to shift-down; menus and driving are disjoint input contexts, so the overlap
never fires — and that separation is itself the decision: menu actions get their
own `[input]` entries, not reuse of driving actions. D-pad and left stick both
navigate, Enter/Esc are the keyboard pair. The advertised-controls-drift family
(four cases in one day) applies in full: `control_hints.gd` grows the menu
context, and the shell gate below checks pad-only reachability rather than
trusting the bindings list.

**3. The gate is a probe plus reviewed stills, never a hash.** A headless
`shell_probe.gd` walks the screen stack structurally: every screen reachable,
back always returns to where it came from, focus lands somewhere visible on
entry, and every control is reachable pad-only — the ADR-0040 reader-check
family, applied to menus. What the screens *look* like is judged by eye against
the mockups from `shoot.sh` stills, because ADR-0023 stands: stills are not
byte-stable, and a layout-engine diff against an HTML mockup would need a
tolerance so wide it gates nothing. Numbers gate structure; Anthony's eye gates
looks.

**4. The season calendar is data, and it is honest about what does not exist.**
A season file names its rounds and each round names a circuit and layout.
Rounds 1 and 2 are Valdirone Nuova forward and reverse; rounds 3 and 4 point at
the unbuilt second circuit and say so — shown in the calendar with the honest
"needs a circuit" label, refusing to start, exactly the ADR-0052 rule for
unbuilt modes. The all-Valdirone placeholder was rejected because a placeholder
that works tends to ship, and deferring the career screens was rejected because
standings is an approved mockup with real data structures
(`standings.h`, `ProfileCareer`) already behind it. The schema joins the
TRACK_SCHEMA family when authored: normative doc plus executable copy, load
refuses.

**5. Flags are generated, like everything else.** A national flag is a
published geometric construction — stripes, crosses, proportions — so a
deterministic generator under `tools/assets/` builds the roster's ~12 flags as
SVGs from recorded construction data, and complex emblems get a simplified mark
the way real timing screens simplify them. No third-party asset set, no license
column for one art file that everything else in the project generates. §5
item 10 applies: each flag's construction is sourced, not remembered.

**6. The build is a milestone, and it goes before M6.** M5f in ROADMAP.md:
shell scene, screen stack, theme tokens from FRONTEND.md, Liberation Sans
import, calendar schema, flag generator, paddock stage 1 (#188), and the
shell probe. It runs on data that already exists — profile, standings, roster —
and M6's race loop then lands into a finished shell instead of retrofitting
one, which is the same argument that put M3c before M4. Folding it into M6 was
rejected because visual iteration and a determinism harness are the two worst
things to interleave in one gate.

**Consequences.**

- ROADMAP.md gains the M5f section with the accept criteria; #171 closes on the
  approved mockups and the recorded references; the flag generator is a work
  item under #187; the pause-flag field (#186) is scheduled into M5f.
- Nothing here starts building. The design phase ends when Anthony says it
  does; this ADR exists so that when he does, the first commit has a plan to
  disagree with instead of a blank page.

## ADR-0054 — The kart is a KZ, which is Group 1, so Art. 8 governs it and not Art. 9

**Status:** accepted, 2026-07-29. Supersedes nothing; corrects an assumption that
had never been written down and so was never checked.

**Context.** `docs/KART_SPEC.md` was written against **Art. 9, Group 2**, in six of
its seven sections. A recheck pass aimed at primary sources found that wrong. Art.
1 of the pinned 2026 technical regulations, PDF p. 1:

> **Group 1** — KZ: Cylinder capacity of 125 cm3
> **Group 2** — KZ2, OK, OK-N, OK-Junior, OK-N Junior, …

This project has called its subject a KZ shifter kart since M0, in CLAUDE.md, in
ARCHITECTURE.md and in every commit. Art. **8.10** is headed *"KZ engine"*. So the
kart is a KZ, the kart is Group 1, and its article is **Art. 8**.

Nobody chose Art. 9. It was assumed, because Art. 9 is where the dimensions are
written out longhand and Art. 8 is two pages of cross-references — so a reader
searching the PDF for "1010.0 - 1070.0 mm" lands in Art. 9 and never sees that Art.
8.1.1 says the same thing for a different group.

**Decision.** The kart is a **KZ, Group 1, Art. 8**. Citations for delegated
clauses take the form `Art. 8.5.2 → 9.5.2`, which records both the governing
article and where the figure is actually written.

**Consequences, and the reason this is cheap.** Art. 8 delegates nearly everything:
8.5.1→4.10.2, 8.5.2→9.5.2, 8.5.3→9.5.3, 8.10→9.10, 8.11→9.12.1, 8.12→9.13.1,
8.13→9.14.1, 8.14→9.15.1, 8.15→9.16.1, 8.16→9.17, 8.17→9.18.1. Art. 8.1.1, 8.2 and
8.3 restate 9.1.1, 9.2 and 9.3 word for word, and Art. 5.3.1 and 4.13.1 are both
headed *"In Groups 1 & 2"*. **No dimension in the spec changes.** Five things do:

1. **Brakes are free.** Art. 8.6: *"Brakes are free in Group 1, but must comply
   with Articles 4.12 et seq. of the TR. They must be produced by a manufacturer
   with a valid brake homologation."* A KZ is **not required to have front brakes**.
   The spec models four because every real KZ has four, and that is now an
   `estimated` design choice carrying its reasoning rather than a requirement.
2. **Rear wheel protection delegates only to the wheel-cover variant**, 8.5.5.2 →
   9.5.5.2. Group 1 has no plain 8.5.5.1. The spec describes the plain protection,
   which is a KZ2 part. Open — see #197.
3. **No anti-roll-bar restriction.** 8.1.2 omits 9.1.2's *"Anti-roll bars must only
   be connected to the main tubes"*, so the torsion bar's Envelope is `none`.
4. **Five-inch rims are a rule, not a habit.** Art. 8.7: *"In Group 1, only 5-inch
   rims are allowed with CIK-FIA homologated 5-inch tyres."* `rim_diameter`'s
   comment was flagged as false in general; for Group 1 it is exactly true and now
   sourced.
5. **The 170 kg is sourced.** Art. 8.9: *"Total (incl. driver) KZ: 170.0 kg
   minimum"* — the figure the solver already uses, which had no citation until now.

Two dangling cross-references in the FIA's own text turned up on the way, both in
Art. 8: **8.5.4.1 → "Article 9.5.4.1"** and **8.4.2.1 → "Article 9.4.2.1"**, neither
of which exists. Cite 9.5.4 and 9.4.2. That makes three such dangles found in this
document's lifetime, so it is a property of the source rather than bad luck.

**Why it is recorded as a decision and not a bug fix.** An unwritten assumption
cannot be checked, and this one survived from M0 to M5 because it was never a
sentence anybody could disagree with. The spec's own §1 rule — an article number is
an externally-sourced constant — turns out to apply one level up: so is the
*article set*.

---

## ADR-0055 — The driver is a built part set, and gate 3 defends the volume he occupies

**Status:** accepted, 2026-07-29. Extends ADR-0044's gate contract (#192) with a
third gate. Supersedes nothing.

**Context.** #190 took the kart from 146 to 295 parts, all specified in
`docs/KART_SPEC.md`, and both #192 gates went green. Ten minutes of looking at the
result in Blender produced a list the gates cannot produce: a radiator hose across
the driver's back, a front panel raked 13.4° that reads as a wall, sidepods that
read as sealed pontoons, a steering wheel rim shaped like a clover. The summary that
mattered was *"I don't even see how a driver could actually sit in that."*

The cause is structural rather than a series of slips. **Gate 1 asserts no part is
inside another; gate 2 asserts every part touches something. Neither has any opinion
about the volume a seated human occupies, because that volume does not exist in the
build.** Measured against a capsule union built from §60.1.4's hard points, 29 parts
reach inside it — `radiator_hose_upper` 79.4 mm deep into the chest at
(8, −379, 394), and a cluster of chain parts at x 100–112 that ought to be outboard
of a seat shell that is only ±184 wide.

Two clauses of the regulation the spec is written against are *about the driver* and
are therefore unverifiable today:

> **Art. 9.5.3** It must not impede the normal functioning of the pedals or cover
> any part of the feet in the normal driving position.
>
> **Art. 9.5.4** No part of the side bodywork may cover any part of the driver
> seated in the normal driving position.

**Decision.** Three parts, and the third is the one that makes the other two worth
doing.

1. **The driver is built geometry** — nineteen `driver_*` parts, exported, with
   their own materials, from §60.1.4's hard points. Not a hidden collision proxy: a
   proxy satisfies a gate and leaves the cockpit rendering empty, and #199 says
   plainly that the seat, pedal and wheel relationship cannot be judged against
   nobody. It closes #17.
2. **The driver gets his own contact table**, `joints.DRIVER_CONTACTS`, with its own
   three-word vocabulary — `sits_on`, `grips`, `presses`. `joints.KINDS` stays
   closed: all nine of its kinds were drawn from a real joint on a kart, and a
   driver is none of them. He is not fastened to the chassis, carries no load
   through a fastener, and a hand around a rim is not a clamp. Widening `KINDS` to
   fit him would destroy the property that makes it worth having.
3. **Gate 3 owns every pair involving a `driver_*` part.** Gates 1 and 2 skip them
   entirely — gate 2's "every part touches a neighbor" is meaningless for a body
   that is not mounted to anything, and gate 1's overlap rule would fire on the six
   declared contacts. Intra-driver pairs are skipped outright, because one
   articulated body authored as nineteen segments interpenetrates at every
   anatomical joint by construction. A `Contact` does what a `Joint` does: it
   permits the overlap **and** requires contact within `CONTACT_TOLERANCE`, so a
   glove 40 mm off the rim fails as loudly as a hose through the spine.

`docs/kart_spec/60-driver-and-finishes.md` §60.1.6 is the contract — the part names,
the segments, the contacts and the gate's rules — written and committed *before* any
of the three pieces of work started, because #17's module, #200's gate and #194's
mass lumps all read it and none of them may rename a field.

**The rule that keeps this honest, and it is the important half.** *A finding that
cannot be adjudicated against a sourced figure gets a waiver and a ticket, not a
geometry change.* The knee splay of ±180 is `estimated` off one three-quarter action
photograph and the entire leg path hangs off it; six of the first 29 findings are in
that category. Spending a regulated clearance to fix a leg whose position is a guess
is how a real constraint gets consumed by a modeling error — the same failure shape
as `length_overall` = 1.830, where five panels were clamped to an unsourced estimate
because it was phrased as a limit.

**Consequences.**

- Two `params.py` figures are corrected here rather than in #194's own wave:
  `driver_shoulder_z` 0.470 → 0.608 and `driver_eye_z` 0.620 → 0.757. §60.1.4
  already publishes both as `derived`, and two conflicting seated heights in one
  file is exactly the drift the spec exists to stop. `scripts/look/kartview.gd`
  reads both off the manifest, so **the cockpit camera moves up 137 mm**; no §6.4
  figure reads either one. The cause was a single error — the hip joint placed at
  z = 0 instead of on the seat pan — which is why the two were 137 and 138 mm low
  while their *relative* gap of 150 matched the sourced 149.5 exactly.
- `driver_upper_arm` 0.290 and `driver_forearm` 0.260 are replaced by the sourced
  `driver_upper_arm` 0.368 and `driver_elbow_to_fist` 0.361. The old pair summed
  175.5 mm short *and* contained no hand at all, a forearm ending at the wrist.
- `driver_helmet_radius` is deleted. A helmet is an ellipsoid; 125 was right
  laterally and 90 mm short fore-aft.
- The driver builds behind `--set=driver=false`, so every §6.4 figure, every
  `drive.sh` scenario and every published still stays reproducible from the command
  that made it.
- **The arms are built at whatever angle actually closes.** §60.2.2 shows the reach
  does not close at a comfortable elbow, and a driver rendered with locked straight
  arms is the honest output of a cockpit that does not fit — far more useful than one
  whose arms were quietly lengthened to suit. The residual is reported as a number.

**Amendment, same day: the 79.4 mm figure above is a guess at a number this spec
does not publish, and the *clip* is the load-bearing part of it.** Two agents
measured the hose against the driver and got 79.4 and 94.8 mm at the same point.
Neither is wrong and the reconciliation is worth writing down, because #200's gate
is about to be built on one of them.

Both models put the torso axis in the same place — the two anchors differ by 4 mm —
so essentially the whole 15 mm spread is the **radius**: 162 against 173. And
**§60.1.4 publishes no fore-aft half-depth at all.** It publishes half-*breadths*,
which are lateral: 162 at the hip, 180 at the shoulder, ±227 at the shoulder's
outer surface. Both models substitute a lateral figure for a fore-aft one and
differ only in which lateral figure they picked. So the honest statement is not
"two valid approximations" — it is that every capsule depth quoted in this ADR is a
missing dimension being invented, and the next agent to pick a third radius will
report a fourth depth.

**A circular section is not merely imprecise, it is wrong, and it swallows the
seat.** Spending a 162-180 mm half-breadth fore-aft as well gives a ~346 mm chest.
Measured on the built mesh, `seat_shell` sits **99.32 mm inside** such a capsule at
(0, −362, 368); under this ADR's own radii the same point is 84.4 mm inside. So the
unclipped capsule fails the seat under either radius, which means it is not a model
of a driver — it is a model of a driver merged with his own seat.

The section is an **off-center ellipse**: lateral half-width from the breadths,
rearward half-depth bounded by the surface he leans on, forward half-depth free.

**What gate 3 actually needs from this ADR is the clip, not the radius.** Of the
three numbers taken across the hose fix, only the clipped one changed sign —
capsule intersected with the half-space forward of the seat back went **−35.71 mm
to +18.63 mm**, while both bare-capsule figures stayed negative for a hose that is
now demonstrably behind the seat back. A gate built on the bare capsule would have
reported the fixed hose as still failing.

Two further facts #200 must not rediscover:

1. **§60.1.1's rake plane and the built `seat_shell` disagree by up to 11.1 mm**, and
   not uniformly: at z 350 the formula gives y −358.9 against the mesh's −357.9, but
   at z 300 it gives −338.7 against −327.6. A gate 3 clipped on the *plane* and a
   gate 1 testing the *triangles* will disagree low down, with the plane the more
   conservative. Whichever surface a half-depth is measured from has to be named.
2. **§60.1.6's part list contradicts its own contact table, and this is a defect in
   the contract rather than in any build.** `DRIVER_CONTACTS` declares
   `driver_torso`/`seat_shell` and declares **no** `driver_rib_protector`/`seat_shell`
   row — so the torso is contractually the part that must reach the shell within
   `CONTACT_TOLERANCE`, which forces its rear half-depth to the full 78 mm. But the
   same subsection puts `driver_rib_protector` *over* the torso at z 250–450, which
   puts the protector behind the shell's surface, inside the seat, with nothing
   declared. That is a gate-3 failure or a waiver on day one. It is a decision about
   the part list — whether `driver_torso` is the clothed outer surface at 78 mm, or
   flesh-plus-overalls at ~60 with the protector owning the last 18 — and it needs
   settling before #200 is written. The 78 mm itself is the seat back **surface**,
   `derived` from §60.1.1's 22° rake and carrying **no** clothing allowance;
   §60.1.5's 6–8 mm of overalls and 12–18 mm of protector are 18–26 mm of it.

---

## ADR-0056 — This kart runs the plain rear wheel protection, which is the KZ2 part

**Status:** accepted, 2026-07-29. Closes the open item ADR-0054 item 2 left, and
issue #197.

**Context.** ADR-0054 established that KZ is Group 1 and Art. 8 governs it. Art. 8's
bodywork clauses delegate to Art. 9 one at a time, and for rear wheel protection
there is exactly one entry:

    8.5.5.2 Rear wheel protection with wheel covers   ->  See Article 9.5.5.2.

**Group 1 has no plain 8.5.5.1.** The unadorned protection `docs/KART_SPEC.md` §50
specifies — 1,340 mm minimum width, three 200 mm ground-clearance windows — is
Art. 9.5.5.1, a Group 2 / KZ2 part.

**Decision.** Keep the plain protection. Record it as a deliberate deviation with
its reasoning attached, `estimated` in front matter §1's sense, rather than papering
over it or switching parts.

**Why.**

1. **The reference corpus is KZ2 throughout.** The project's own race data is the
   Genk **KZ2** entry list, qualifying and final classification, and every reference
   photograph in `refs/kart-visual/` shows plain protection. No accessible
   photograph shows a KZ with wheel covers.
2. **The plain part is the one with a form behind it.** The 1,340 minimum is sourced
   through KG C2, `003-BR-48`, which is a plain rear protection. Switching to covers
   would trade `sourced` dimensions for `estimated` ones — the wrong direction for a
   document whose entire purpose is §1.
3. **It is a different part, not an addition.** Art. 9.5.5.2 has the covers
   extending 20–30 mm *beyond* the rear wheels' outer plane and 20–40 mm *above*
   their highest point. Both contradict rules the plain part obeys — 9.5.5.1's *"no
   higher than the rear wheels"* and *"in line with the outside of the rear wheels"*.
   So this is not geometry added to the current spec; it is a different silhouette on
   the widest and rearmost thing on the kart.

**Consequences.** No part moves and no dimension changes. Front matter §2b carries
the reasoning; §50.11 carries it again at the assembly, and both Envelope fields
read `Art. 9.5.5.1 (KZ2 part; Group 1 delegates only to 9.5.5.2 — deliberate
deviation, see #197)`. The two adjustable outer parts' contrast rule stays checked
against 9.5.5.1 and 4.11, which is where it already was.

**What would reopen this.** A photograph or a homologation form showing a Group 1
KZ running 9.5.5.2 covers. That is a `sourced` fact this project does not have, and
the deviation is recorded precisely so that finding one is a one-line change rather
than an archaeology exercise.

---

## ADR-0057 — Gate 3 clips the torso on the §60.1.1 rake plane, and the rib protector gets its own contact row

**Status:** accepted, 2026-07-29. Settles the two items ADR-0055's amendment left
open, which blocked issue #200.

**Context.** ADR-0055's amendment established that gate 3's driver volume cannot be
bare capsules — §60.1.4 publishes no fore-aft half-depth, both measuring agents
invented one from a lateral breadth, and an unclipped capsule swallows the seat
under either radius (84.4–99.32 mm). What the gate needs is the **clip**: of the
three numbers taken across the upper-hose fix, only the capsule-intersected-with-
the-half-space-forward-of-the-seat-back changed sign. Two things had to be settled
before the gate could be written.

**Decision 1 — the clip surface is the §60.1.1 rake plane, by formula.** The torso
volume is clipped to the half-space forward of

    y(z) = -365 + 0.404 * (365 - z)        # §60.1.1, tan 22°, `derived`

and not to the built `seat_shell` triangles. The two disagree by up to 11.1 mm and
not uniformly — at z 350 they are 1 mm apart, at z 300 the plane is 11.1 mm
*rearward* of the mesh, which is the shell's molded pan-to-back transition — and
the plane is the conservative choice there: it concedes less driver volume to the
seat, so a part that clears gate 3 clears it against the larger driver. The plane
is also analytic, which a gate wants: it cannot change when a bevel does. Whichever
number gate 3 reports for a torso finding, the surface it was clipped on is this
formula, named.

**Decision 2 — `driver_rib_protector`/`seat_shell` is a declared `sits_on`
contact, and the torso keeps its own row.** The contradiction was real: §60.1.6
put the protector *over* the torso at z 250–450 while `DRIVER_CONTACTS` declared
only the torso against the shell, which makes the protector's presence inside the
shell an undeclared overlap — a gate-3 failure on day one. But the two rows are
not redundant, because they touch in different places: the protector bears on the
shell across its z 250–450 band, and the torso — at 25° against the shell's 22° —
touches at the shell's **top edge**, above where the protector ends. Both contacts
are physically real and both are declared.

The torso's rear surface remains the seat's 22° chord as built (§60.1.6's own
derivation, 0.04 mm at the shell). The honest layering — flesh-plus-overalls at
~60 mm rear half-depth with the protector owning the outer 18 — is **surface**
work and belongs to #17's surface half, not to this contract: re-insetting the
torso moves no hard point and changes no contact, so the gate does not wait for
it. Until then the protector overlaps the shell by roughly its own thickness, and
the new contact row is what makes that overlap declared rather than waived.

**What would reopen this.** A sourced torso fore-aft depth (a real seated-depth
table read for the 50th-percentile male) would let the volume stop borrowing its
forward half-depth from judgment; and #17's surface pass, when it re-insets the
torso, should re-measure both contacts and delete this ADR's "until then"
paragraph from §60.1.6.

---

## ADR-0058 — The rib protector is worn under the suit, and the torso never re-inset

**Status:** accepted, 2026-07-29. Resolves the "until then" paragraph ADR-0057
left open, the opposite way ADR-0057 predicted.

**Context.** ADR-0057 expected #17's surface pass to re-inset the torso by the
protector's 12–18 mm, leaving the protector as the outermost layer over its
z 250–450 band. §60.1.8's measurement pass then read both driver photographs in
the repo — buntschu and panfilov — and neither shows a rib protector at all: the
torso is plain overalls in both frames, so the protection is worn **under** the
suit. Both under- and over-suit products exist and FIA 8870-2018 covers both,
but the photographic evidence this project has is for under, and §5 item 10 says
the photograph decides.

**Decision.** The protector recesses; the torso does not move. The torso's rear
face stays the seat's 22° chord (it is a declared contact at 0.02 mm and the
gate-3 rake plane clips there). `driver_rib_protector`'s outer face moves from
1 mm proud of the torso surface to ~1 mm inside it over its band, rear face
0.3 mm forward of the rake plane.

**Why the contact row survives.** The protector still bears on the shell —
through suit fabric compressed by the driver's weight, which is what physically
happens in a seat. Measured after the change: `driver_rib_protector`/`seat_shell`
gap **0.09 mm**, inside the 2.0 mm tolerance, so ADR-0057's declared `sits_on`
row stands unchanged. The protector's 12–18 mm overlap with the shell is gone —
gate 3 confirms zero findings for the pair — and containment is measured, not
assumed: 0 of 240 (low) / 0 of 1408 (high) protector vertices outside the torso,
minimum inset 0.26/0.28 mm.

**Consequence accepted deliberately:** `driver_rib_protector` never renders. It
stays a built, materialed (`protector_shell`), watertight, gate-checked part —
§60.1.6 argued an invisible part is half-pointless, and the answer is that this
one's job is the contact contract and Art. 7.5 compliance, not pixels. §60.1.6's
contact row and §60.1.7's build row record it.

**What would reopen this.** A photograph of a KZ driver wearing the protector
over the suit — one line to flip `RIB_PROTECTOR_PROUD` back and re-measure both
contacts.

## ADR-0059 — The gates run at both detail levels, and staleness is judged across them

**Status:** accepted, 2026-07-30. Extends ADR-0044's and ADR-0055's gate
contract (#192, #200). Closes #209.

**Context.** Every part is built twice — `Detail.low` for the shipped mesh,
`Detail.high` as the #19 bake source — but `check_assembly` ran once, on the
low context. The high pass was built, renamed out of the way, baked from, and
never measured. Nothing in `verify.sh` or `--check` ever gated it. So every
"measured at high detail" claim about gate output was wrong wherever it
appeared (the `joints.py` seed comments and #200's closure comment both said
it and were corrected), and the first deliberate high-detail gate run failed:
`chassis_steering_hoop` 10 triangle pairs inside `pedal_brake_clevis`,
reproduced on pristine `4267e03`, predating the driver work. Low detail was
green over it for its whole life — the arm's bend crown bulges
inboard-forward-down at high density (+6.9 mm over the straight surface) and
the low tessellation misses the clevis corner by 1.7 mm of x.

**Decision.** The honest option from #209, not the write-it-down-as-deliberate
option: a defect that exists only in the bake source still bakes its normals
onto the shipped low mesh, so the low-only scope was a hole, not a policy.

1. **`check_assembly` runs on the high context** whenever a run builds one
   (the bake stage), before `suffix_pass` renames it — the part names must
   still match `joints.py`. Gates 1–3 all run; fatal findings are fatal at
   either detail, and the failure names the detail it measured.
2. **Waiver staleness is the one judgment made across details.** Each gate
   pass records its undeclared-failing pairs; `check_stale_waivers` adjudicates
   once, against the union over every detail the run gated. A waiver is stale
   only when it covers no failing pair at *any* gated density — the bevel
   moves marginal pairs in and out of overlap between densities, and
   `stale_waivers` already grants exactly that tolerance to globs. Without the
   union, gating high would have struck low-measured waivers as "fixed" and
   demanded high-only waivers be deleted at low.
3. **Single-detail runs keep the old contract.** `rebuild()` (`--watch`,
   `--detail=high`) adjudicates staleness immediately against the one detail
   it built. A watch loop is a shape-review loop; it does not owe the other
   density a build.

**The fix the first gated run forced.** The hoop's corner could not stay at
x 170: the corridor between the rear master's body (ends y 555) and the
clevis's rear face (581.7) is 26.7 mm for an 18.4 mm tube footprint, and the
arm's line through it was already centered — moving the line rearward to
clear the clevis (blend 0.55) put the straight arm 29 triangle pairs into
`brake_master_rear` at low detail too. The line stays (0.618 is 0.59 extended
to the new corner), the corner moves outboard of the clevis's x extent
(`mid_x` 0.85 → 0.89 of the foot), and the bend radius drops 42 → 30 mm so
the fillet tangent lands outboard of the clevis edge. Measured after:
hoop/clevis gap 3.49 mm low, 3.24 mm high; hoop/`brake_master_rear` 3.59 mm
low, 3.93 mm high; zero intersecting pairs at either density.

**Cost measured, not estimated.** The high pass costs ~1.3 s total: parts
prepared 280 ms, gate 1 199 ms, gate 2 737 ms, gate 3 34 ms — against a
947 s full pipeline run. The first draft of this ADR blamed gate 3 for a
ten-minute stall that was actually the high-detail geometry build itself,
which runs gated or not. The gating is free at this scale.

**One row already needed the union.** The first fully-gated run excuses 48
driver findings at high against 47 at low: `drive_output_sprocket` reaches
2.77 mm into the torso only at high density, while at low only the shaft
member of #204's `drive_output_*` glob fails. A per-detail staleness judgment
over separate runs would have argued about that row from both sides.


## ADR-0060 — Livery art is rasterized into the existing UV atlas, in world space, by a new albedo stage

**Status:** accepted, 2026-07-31. The design for #189's last livery checkbox
(the V13-style wrap art); folds in the fix for #210. Implementation is the
next session's work — this records the decisions so the build starts from a
contract rather than a debate.

**Context.** The pipeline has a normal bake and no albedo path: 44 materials
carry plain base colors, the zones (`build.ZONES`) are face-level second
material slots, and the racing numbers landed as die-cut *geometry* (spec
§60.4.6) precisely because no stage could write a texture. The wrap art —
per-panel die-cut layouts, pinstripes, the pod name zone, sponsor blocks, the
suit's multi-panel colors — cannot be geometry, and #210 measured that the one
texture the pipeline does produce never reaches Godot: the exported glb has
zero images and no `normalTexture` on any material, so the export seam has to
be rebuilt for one map or two, and it should be two at once.

**Decision, in five parts.**

1. **No new UV machinery.** The art is authored as functions of *world
   position and part identity* — a stripe is a plane, a panel field is a box,
   a die-cut edge is a distance from a section curve — and rasterized into the
   existing smart-project atlas by walking each face's UV triangle with
   barycentric world interpolation. `uv_stage`'s single-layer rule and refusal
   of UV2 stand. The alternative — authored per-panel UV islands — rebuilds
   the unwrap for a layout problem the art functions do not have: V13's art is
   flat *in panel space*, and panel space is reachable from world space.
2. **numpy, from the pinned Blender's own bundle.** `build.checker_image`'s
   pixel-list write is unusable at 4096². numpy 2.3.4 ships inside Blender 5.2
   and is deterministic for this use; it stays inside the albedo stage rather
   than leaking into geometry modules.
3. **Stage order `geometry, uv, albedo, bake, lod, export`**, output
   `kart_albedo.png` beside `kart_normal.png`. The albedo is a fact about the
   art tables and the atlas alone, so it does not read the bake and the bake
   does not read it.
4. **The manifest hashes every texture, and the export embeds them.** #210's
   observability gap is that `kart.json` hashes the glb alone, so a glb with
   zero images passed every gate. The manifest grows a per-texture sha256 and
   the determinism gate compares them; the export stage is required to leave
   `normalTexture` (and now `baseColorTexture`) on the materials that earned
   them, verified by parsing the glb's own JSON chunk — the #210 acceptance
   command, run as a gate rather than a one-off.
5. **Art tables live with the livery tables in `build.py`**, keyed by the same
   roles `LIVERIES` already uses, so `--livery=` reaches the wrap art through
   the path that already reaches the base colors — and a livery still is
   reproducible from its command, which is the rule that makes renders
   evidence.

**Consequences.** The zones stay authoritative for *where* regulated fields
sit (they are measured face sets); the albedo paints within them rather than
replacing them. The suit's multi-panel colors (§60.3.8 finding 1) ride the
same stage. The numbers stay geometry — a film with thickness photographs as
one, and nothing about an albedo stage argues for flattening a part that
already works. `SKIP_IMPORT=1` gains a second stale-texture trap, which the
existing `STALE IMPORT` guard in `kartview.gd` should be extended to cover.

## ADR-0061 — The chain runs outboard of the engine, and the seat's 74 mm chain tunnel was clearing a fiction

**Status:** accepted, 2026-07-31. The corridor audit, run after the part-2 seat
was rejected twice: Anthony's read was that a seat needing a 74 mm engine-side
cutaway means the placements upstream of it are wrong, and he was right.

**The finding.** `chain_x` = +0.115 put the chain plane 285 mm inboard of where
the reference kart carries it. `tonykart_racer401T_p05.jpg` — the repo's only
top-down, scaled at 2.10 mm/px on the sourced 1050 mm wheelbase, front tire
diameter cross-checking to 2% — shows a **bare axle from the centerline out to
+368**, the Art. 5.9 chain guard plate at **+378..+462**, and the crown wheel
under it, between the right bearing hanger (+300) and the right hub (~+548).
TM KZ-R1 HF `041-EZ-75` p. 1 photographs the **drive side and the clutch side
as opposite ends of the engine** — clutch inboard, output sprocket outboard —
so the chain exits the cluster's outboard face and never enters the seat
corridor at all.

Every number downstream of +0.115 was internally consistent and collectively
wrong, which is why no gate caught it: the wave-3 joints analysis measured a
real 6.8 mm window between shell (+179) and clutch cover (+182), correctly
concluded a 32 mm guard could not fit, and then wrote *"the chain cannot pass
outboard of the shell whatever `chain_x` is"* — an argument whose hidden
premise, sprocket-emerges-inboard, nobody had sourced. The 74 mm
`SEAT_CHAIN_RELIEF` tunnel was that premise made fiberglass.

**Decided.**

1. **`params.chain_x` = +0.445**, `derived`: 15 mm outboard of the ignition
   cover's face at +430 (the cluster's outboard-most feature), inside p05's
   measured +414 ±25 band. `wheels.SPROCKET_X` moves with it and stays a
   must-equal literal (#112's hoist there is still open).
2. **The driveline flips faces.** Sprocket carrier boss at 393..428 on the
   case's outboard face (30 mm proud, 2 mm short of the ignition cover plane);
   output shaft Ø18 in the carrier to 430, Ø12 nose to 460; sprocket mid-nose
   at 445. The old 125 mm inboard cantilever is not mirrored — the real stack
   is short.
3. **The guard follows**: `CHAIN_GUARD_X` (0.431, 0.463), same wall-to-band
   offsets as before; its mounting stay reroutes as a diagonal to
   `chassis_cross_rear`'s top at x 296, beside the right bearing hanger,
   because the member ends at ±310 and a straight drop from 427 lands in air.
4. **`SEAT_CHAIN_RELIEF` shrinks from 74 mm to a 12 mm clutch scallop** over
   the same t band — which is what p05's real seat shows beside the clutch
   (its edge dips ~30 mm and it is a ~25 mm wider seat than our Tillett ML).

**Also convicted by the audit, deliberately not fixed here.**

- **`seat_y` is ~80 mm too far forward.** Tillett's own positioning sheet
  (fetched, read) measures the 13.5 cm KZ "axle to driver's back" gap **at
  axle height**; the derivation in `params.seat_y` applied it to the top of
  the reclined back. Correcting the station puts the shell top at ≈ −444;
  p05 independently measures the back top at −460 ±20 and the front rim at
  +95 against our −365/+240. Two sources, one direction. Not moved tonight
  because it cascades into the hard-coded driver datum chain
  (`driver_hip/shoulder/eye/helmet`), the corroborated 731/735 hip-to-pedal
  pair, the glove:rim gate row and the seat struts — that is the ground-up
  question and it gets its own ticket and decision.
- **Tillett dimension C was misread as a chord.** The 2024 chart's diagram
  puts C across the **front wings** (ML: 460); `seat_shell_rake`'s
  `sqrt(460² − 335²)` derivation is arithmetic on a misread dimension. The
  22° value survives on its photographic bracket (19-26°) and does not move;
  the docstring's justification must. p05's measured 412-452 front-wing width
  reconciles with C=460 exactly — the reference seat is not oversized, and
  `seat_width` A=325 internal at the hip bones was read correctly all along.

**Acquitted:** engine placement (built inboard face +240 vs measured +227
±15), radiator (x −240..−490 built vs ~−200..−540 measured, center y −235 vs
−215), `seat_z`, bearing hangers. §30's claim that p05 is "a high front
three-quarter with no seat in frame" was false of the file on disk and is
corrected in place — the caption in `sources.txt` was right.

## ADR-0062 — The rear protection's top edge is a measured profile, and the reference corpus informs the shape without dictating it

**Status:** accepted, 2026-07-31. Part 3 of the sculpt wave, accepted by eye
against the dead-rear reference.

**Context.** The rear protection was lofted at a constant 177 mm height over its
whole 1390 mm span — the KG C2 form's overall height applied everywhere — and it
read as two boxes and a bar. The reference photograph (`tonykart_rear_header.jpg`,
dead-rear, scaled 1.053 mm/px on the panel's own plane with the lobe crest
landing at |x| 597 against the hub's derived 592.5 as the lateral self-check)
shows a single molded part: a tall center crown, wide near-flat valleys, rounded
lobes over each tire. Measured: crown ~290 ±15, valley floor 168-175, lobe ~250,
outer end ~235 — the reference part is a different manufacturer's plain
protection, roughly 70 mm taller than the C2 everywhere except the valleys.

**Decided.**

1. **The top edge is `bodywork._rear_top_profile`**, a station table interpolated
   with per-span cubic smoothstep — zero slope at every control, so each station
   is its local extremum, the profile cannot overshoot between controls, and |x|
   symmetry gives a flat crown by construction. Crown authors as
   `tire_rear_diameter` (295): Art. 9.5.5.1's *"no higher than the rear wheels"*
   is a sourced ceiling and the measurement brackets it. Valley and lobe stations
   are `estimated` off the photograph; the lobe station is `derived` at
   `rear_hub_x`. Depth 187, the width argument, the bottom windows and their lift
   table are untouched and still read the C2 form.
2. **`rear_prot_height` = 177 stays as the C2 form's figure of record**, on
   `FIELD_COVERAGE_EXEMPT` — nothing derives from it, and the manifest still
   publishes what the sourced form says.
3. **Two joints deleted.** The silencer can and its clips were declared `pierced`
   through the panel; that was true of the flat 177 top (the top-front curve
   crossed the can's skin) and is false of the tall crown — the can now sits
   wholly inside the shell's hollow, which is what the photograph shows: only the
   outlet stubs proud of the valley. The Art. 5.10 outlet argument the joint
   carried moved to a comment at the declaration site.

**And the design-intent rule, stated once because it governs the rest of the
wave.** Anthony, at this sign-off: the kart must *look good and be reasonably
accurate to something real* — it must not replicate any manufacturer's part or
trade dress. Reference photographs are measured for typology and proportion
bands; regulation dimensions are the hard constraints; within them, liberties
are welcome where they read better. Spec and ADR language says "informed by
reference photography," not "matched to" a named product, and no manufacturer's
livery is ever copied.

## ADR-0063 — The fuel tank is a lofted saddle, not three boxes wearing a bevel

**Status:** accepted, 2026-07-31. Part 8 of the sculpt wave; the construction
rewrite the plan required an ADR for before build.

**Context.** `cockpit._fuel_tank` is the only box-derived part left of the
wave's eight: three `build.box` blocks (two flanks, one shortened center) plus
a bevel, with the steering-column notch made by the center block stopping
short. Every regulated fact about it is right — Art. 4.7's mandated position
drives all three `tank_center_*` coordinates, the capacity is sourced, the
feed/return fitting count is a scrutineering fact — and it still reads as
luggage, because a rotomolded polyethylene tank has no box anywhere in it.
Both references in the repo show the same body language
(`det_tonykart_401t_museum.jpg` front three-quarter,
`det_tonykart_stand_essen.jpg` rear-above, both translucent white moldings):
large rounded shoulders everywhere, flanks drafted inward toward the top, a
domed top surface falling toward a front filler cap, a molded recessed panel
sunk into the large face, and the column clearance as a smooth molded waist —
an absence the sections flow around, not a slot cut out of a prism.

**Decided.**

1. **The shell is one section loft along y** — rounded-rectangle sections
   (superellipse-family, the `_loft_shell`/`_catmull_rom` pattern the bodywork
   modules already use) walked through a station table: rear face, mid-body,
   shoulder, front face. Corner radius, draft and dome are properties of the
   section table, so low and high detail are the same shape at two densities
   and the counts come from `context.detail` as ADR-0059 requires.
2. **The column notch stays an absence by construction**: the center sections
   between the flank stations pull their top edge down and their walls apart
   where the column passes, exactly as the current three-block build does in
   spirit — no boolean, no declared joint, and gate 1 keeps catching a notch
   that stops clearing (`column_clear_y` 0.318 carries over: the notch clears
   the column's Ø20 *lower surface*, not its centerline).
3. **What may not move:** capacity (sourced), all three `tank_center_*`
   (derived from Art. 4.7's sentence), the `fuel_tank*` part names
   (`build.py`'s `FINISHES` glob-matches them), and the strap/floor-tray/column
   gate-1 and gate-2 relationships. The overall `tank_width/depth/height`
   envelope is `estimated` and may move a few millimeters where the molding's
   radii demand it, spec rows updating with the reasoning.
4. **Shape values are `estimated` off the two photographs, per ADR-0062's
   intent rule** — typology and proportion, no manufacturer's molding copied:
   shoulder radius read as a fraction of tank height (~0.15), draft a few
   degrees, recess panel depth a few millimeters. Translucency is a material
   question and stays flagged for the material pass, not this geometry wave.

**Consequences.** The tank joins the other seven parts on the section-loft
construction, the winding gate covers it as one watertight shell instead of
three, and the sticker-recess face gives the livery pass a real landing zone
when it comes.

**Amended at sign-off, same day.** Two changes out of Anthony's eye on the
built part, both inside this ADR's scope and one overriding its point 3.

1. **The straps anchor on their own tabs, not on the rails.** The first build
   ran each strap's feet out to the main rails — the rails sit 60–100 mm
   outboard of the flank at the strap stations, and the resulting fan-out was
   rejected twice. The webbing now drops **dead vertical** down the flank
   (measured 2 mm of x spread in the drop, the webbing's own thickness) onto
   four stamped steel L-tabs standing on the pan beside the tank
   (`fuel_tank_mount_{front,rear}_{l,r}`, 3 mm plate, foot flange bolted
   through the pan — Art. 4.6 permits holes to 10 mm). The tab plates stand
   against the flank's widest belt, embedded half a millimeter, so the four of
   them also locate the tank sideways. This **replaces** point 3's frozen
   strap/tray relationships: `fuel_tank_strap_* <-> chassis_rail_?`, the
   strap<->tray `pierced` declaration and the strap<->tray-edge clamp are all
   deleted; strap<->tab (clamped, per strap — a glob pair here is a
   cross-product and the gate demanded the front strap touch the rear tabs),
   tab<->tray (bolted) and tank<->tab (seated) replace them.
2. **Second shrink, with the volume bought back forward.** 240x235x212 read
   too big and too parallel-sided against both references. Width and height
   went down 5% (228/201), the draft deepened 0.93 -> 0.88, and the lost
   volume was recovered by growing depth 235 -> 240 entirely on the front face
   — the least-visible dimension and the one with 185 mm of slack to its
   Art. 4.7 clause; the rear face stays pinned 70 mm off the seat. Measured
   off the built mesh: 9.18 L shell, ~8.4 L inside a 3 mm wall. Ullage is
   gone — the molding holds the sourced 8.5 L brim-full, and this envelope is
   the floor: any further shrink argues with `tank_capacity` and becomes a
   smaller-capacity tank, which Art. 9.3's 8 L minimum bounds at ~6% more
   volume.

The Blender MCP carried both iterations: the built .blend opened live in the
GUI, flank/tray/volume measured off the mesh in place, Anthony's sign-off given
on the live viewport rather than a published board. `genkart.sh` remained the
only generator; the MCP session only ever read its output.

## ADR-0064

**A hose termination is a fitting, and the joint table has to say so.**

Status: accepted, 2026-08-01. Supersedes nothing; amends ADR-0029's radiator
attitude figures.

### The defect

Every hose on this kart ended by arriving at a casting: the swept tube ran up to
a point on a tank or a boss and stopped, with its last centimetre *inside* the
part it fed. Rendered, the hose dissolved into the aluminium with no port, no
collar and no clamp. Anthony found it by eye in the viewport -- *"where the
radiator hose attaches, it just blends right into the radiator, no clamp, no
actual connection point"*.

`joints.py` never objected, and the reason matters more than the fix. All six
terminations were declared `kind="routed"`, which is the kind for a hose that
*lies against* something. So the gate was asking whether the hose touched the
tank, and it did -- 18 mm inside it. **A gate that checks contact cannot
distinguish a joint from an overshoot.** The vocabulary had a word for what was
actually there and nobody used it.

### The root cause, which was worse than the symptom

Every hose's end control point was the boss's **centre**, not its mouth.
`WATER_OUTLET_LOCAL` is the middle of a Ø30 x 36 disc, so the top hose's last
vertex sat 18 mm inside its own outlet and the run left the boss heading
*backwards over the head*. Three joint rows existed to declare that overshoot --
hose against head, hose against core, hose against crankcase -- and each read as
a description of the kart rather than as the bug it was. The pump was worst: the
bottom hose ran up to a point on the drum's crown, so the port faced straight
down into the body.

### The decision

1. **`powertrain._hose_fitting` builds two parts per termination.** A `_neck`
   (collar, shank, barb ridge) cast or brazed into the casting, and a
   `_hose_clamp` -- band and screw housing in one mesh, because a worm clamp is
   one manufactured item and the housing is the whole silhouette tell. Without
   it a band reads as a ferrule.
2. **`PORT_MOUTHS` carries each port's mouth and outward axis.** One line per
   port. The hose leaves along that axis for `HOSE_PORT_LEAD` before it is
   allowed to bend, and starts `NECK_EXPOSED` out from the face so the collar
   stays visible under its cut end. A hose that begins bending inside its own
   clamp is the tell that the fitting was drawn on afterwards.
3. **The pump's ports are now the right two ports.** Axial suction on the end
   face, radial discharge on the flank. The inlet was on the crown, which is
   neither.
4. **The head's water outlet moved to the rear face.** It was 43 mm forward of
   `CYLINDER_AXIS_Y` while the radiator is rearward and to the left, so the hose
   left the boss forward and hairpinned back over the head. A Ø28 silicone hose
   does not turn like that.
5. **Thirteen joint rows were deleted**, every one of them by the gate's own
   measurement once the hoses stopped overshooting. They are the evidence the fix
   landed, not collateral.

### Consequence, recorded

Radiator tank ends are `radiator_thickness` x `RADIATOR_TANK_HEIGHT` = 40 x 22
and `NECK_ROOT_DIAMETER` is 30, so a port's collar overhangs into the fin block
and the end channel. That is declared `welded` rather than dodged: a kart
radiator is one furnace-brazed assembly. The steel clamp is a separate bolt-on
item and gets no such row -- it has to keep its distance, and `HOSE_PORT_LEAD` is
what buys it. The 250 x 40 tank faces fit the collar with room and were built and
rejected: they aim the lower hose into `chassis_seat_strut_front_l`.

It is also a third witness that `RADIATOR_TANK_HEIGHT` 22 mm is under-read.
`notes_radiator.md` §1 already says the 16 px it came from was foreshortened. Not
moved here -- that is a §30.7 change with the core's proportions hanging off it.

## ADR-0065

**#190's radiator cluster closed by attitude, not by placement.**

Status: accepted, 2026-08-01.

### The defect

The left rear side-bumper socket riser stood 64 mm inside `radiator_tank_low`,
and with it the core, the fin pack, the curtain and the lower side bar -- twelve
part-pairs, waived under #190 for milestones and outliving the ticket, which is
closed. `params.py` said the fix was that *"Spec §30.7 re-places the radiator"*.
The radiator was re-placed (z 320 -> 240) and it did not clear it.

### What was measured, and rejected

| move | clears | cost |
| --- | --- | --- |
| side-bar mount pair forward 48 mm | all twelve | front socket lands in `brake_master_bracket`; the master's own docstring records it boxed in -- steering hoop forward, this socket rearward, x spent by #201. Master rearward 121 mm breaks its `sourced` CRG layout; outboard 65 mm lands in the kingpin |
| radiator up 116 mm | all twelve | core top 387 -> 503 mm |
| lower bar under the tank | -- | bar lands inside `chassis_rail_l` and `bodywork_sidepod_l` |
| radiator 46 mm rearward alone | both sockets | `chassis_side_bar_l`/`radiator_tank_low` worsens 51 -> 132 pairs |

### The decision

**Move the attitude, not the placement.** Anthony's call, from the viewport:
stand the core up and take the bottom back. Rake is the lever because it moves
the low tank's bottom edge in *fore-aft* without moving the core's centre --
the fore-aft half-extent is `(height/2) sin(rake)`, 154 mm at 45 deg and 140 at
40, and 14 mm walks the tank out of the socket's y band. The other three numbers
compensate rather than fix:

    radiator_rake    45 deg  -> 40 deg from vertical   (inside the sourced 30-45)
    radiator_y      -0.235   -> -0.282                 (47 mm rearward)
    radiator_z       0.240   -> 0.270                  (30 mm up)
    radiator_height  0.435   -> 0.420                  (New-Line RS size M)

Standing it up drops the bottom into the lower side bar, so the height goes back
on; the height raises the top, so the core shortens. 420 is a catalogue size, not
an invented one -- the KZ family runs 420-470.

Measured after: sockets clear by 40.3 and 38.8 mm, lower side bar by 8.6 mm, zero
overlapping pairs. `radiator_z` 262 was the floor and left the bar 1.7 mm, which
is knife-edge and fires no gate; 270 is that floor plus margin. Four waivers
deleted -- three #190, and one #206 that went with the straighter upper hose,
which no longer passes through the driver's right arm. Known-open 30 -> 15.

### The price, paid deliberately

The core's top edge is **422 mm** against photogrammetry of 375 +/- 20 -- 28 mm
high, worse than the 13 mm the old solve carried. This is not a regulation
question: this is not a licensed FIA product and the 500 mm ceiling is a guide
here, not a wall. It is a **look** question, and it is recorded in
`radiator_z`'s docstring so the next person knows it was bought rather than
missed.
