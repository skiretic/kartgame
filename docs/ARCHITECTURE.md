# Kart Racing Game — Architecture

Status: design, pre-implementation.
Host: macOS arm64 (M-series). Free, open source, non-commercial.

Pinned toolchain, installed and verified 2026-07-24:

| Tool | Version |
|---|---|
| Godot | 4.7.1 stable |
| godot-cpp | `master` @ `9c7567d2` — see [ADR-0016](DECISIONS.md#adr-0016--pin-godot-cpp-to-a-master-commit-because-no-47-branch-exists) |
| Blender | 5.2.0 LTS |
| SCons | 4.10.1 |
| git-lfs | 3.7.1 |

Pin these. Godot's 4.x line moves, and Blender's `bpy` API shifts between major versions — a 5.x script will not run unmodified on 4.x.

**Known host defect.** The *first* headless import of a cold project dies — on macOS inside MoltenVK, on Linux with SIGABRT. The GUI editor is unaffected and the second headless run is clean. It is an engine bug, not a project one — [ADR-0018](DECISIONS.md#adr-0018--macos-headless-imports-crash-in-moltenvk-ci-verifies-on-linux) has the detail, the workaround, and the amendment that widened it beyond macOS.

---

## 1. Locked decisions

| Area | Decision | Consequence |
|---|---|---|
| Engine | **Godot 4, Forward+ renderer** | MIT licensed, fully open source, no EULA, no royalty, contributors clone and build. Vulkan-based; native Metal backend on macOS since 4.4. |
| Languages | GDScript for game flow + **C++ GDExtension** for the sim | Fast iteration where it helps, native speed where it matters. §3. |
| Art direction | Somewhat realistic | Photoscan materials, correct scale, baked GI, AgX tonemap. §5 is the realism plan. |
| Driving feel | Grounded kart sim-lite | Per-wheel raycasts, slip-curve tires, weight transfer, kart-specific frame flex. §6. |
| Kart class | **KZ shifter, 125cc, 6-speed** | ~45 hp, ~175 kg with driver, ~140 km/h, ~2.5 g cornering. Adds gearbox, clutch lever, engine braking. Highest skill ceiling in real karting. §6.3. |
| First track | Fictional, plausible | Built to real kart-circuit design constraints. No real lap times to check against, so physics validates against published KZ performance figures instead. §6.4. |
| Physics | Jolt (built into Godot 4.4+), custom vehicle on top | Jolt owns collision. `VehicleBody3D` is not used — its raycast model is too coarse for kart feel. |
| Content | CC0 photoscans + procedural + AI fill | Photoreal materials free and legally clean; tracks and karts generated for precision; AI for gaps. §11. |
| Platforms | macOS, Windows, Linux/Steam Deck | **Android dropped.** |
| Cameras | Chase + in-kart cockpit + free/debug | Cockpit drives interior geometry, near plane, and comfort options. §7. |
| Multiplayer | Deferred | Sim built deterministic now. Ghosts and replays are the near-term payoff. §8. |
| Distribution | Free, open source | Public repo, no store revenue, no royalty exposure. §16. |

**North star:** the kart drives well and reads as real at speed. Everything is judged against that.

---

## 2. Why Godot, and what it costs

### What comes free

Renderer, GI, shadows, post-processing, particles, UI toolkit, audio engine, save/serialization, input mapping, scene system, animation, editor, physics integration, export templates, and a high-level multiplayer API. On the custom-Vulkan path, every one of those was work.

### What Godot does not give you

| Gap | Response |
|---|---|
| Weak `VehicleBody3D` | Replaced entirely by the custom model in §6. Never used. |
| No built-in motion blur | Compositor effect (custom render pass, 4.3+). Non-optional — motion blur is a primary speed cue at realistic art direction. §5. Built in M1; the velocity buffer a compositor effect needs is available on Godot's Metal backend, which was the open question. |
| Mediocre SSR | Reflection probes carry the load; SSR is a supplement on the track surface only. |
| No hardware ray tracing | Bake static GI instead. A fixed racetrack is the ideal case for baked lighting. |
| No adaptive-trigger or gamepad-gyro API | Small HID GDExtension if wanted. §9 — real cost, clearly bounded. |
| Streaming for large worlds is manual | A closed circuit fits in memory. Non-issue at this scope. |

### The realism ceiling, stated honestly

Godot lands below Unreal 5 on raw fidelity. For this specific game the gap is narrower than usual: a kart sits low, moves fast, and the sightlines are short. Material quality, correct scale, and motion cues matter far more here than GI sophistication.

---

## 3. Language split

| Layer | Language | Why |
|---|---|---|
| Vehicle model, tire solver, gearbox, substepping | **C++ GDExtension** | Tight numeric loop at 120–240 Hz. GDScript is too slow and too imprecise here. |
| Kart mesh + cockpit interior | **Blender Python** | Bevels, UV unwrap, normal baking, LOD decimation all come free. Writing those three in C++ would be months of work for a worse result. |
| Track visual mesh, curbs, lightmap UVs | **Blender Python** | Same reasoning. Reads `track.json`. |
| Track gameplay data — collision, checkpoints, racing line, material zones | **C++ GDExtension** | Reads the same `track.json`. One definition, two consumers, no drift between what you see and what you hit. |
| AI racing line + speed profile solver | **C++ GDExtension** | Numeric optimization. |
| Audio DSP / engine synthesis | **C++ GDExtension** | Per-sample work on the audio thread. |
| Game flow, race states, HUD, menus, settings | **GDScript** | Iteration speed wins; none of it is hot. |
| Camera rigs | **GDScript** | Cheap per-frame math, heavy tuning — fast reload is worth more than speed. |

Built with `godot-cpp` via SCons. Keep the extension boundary narrow and data-oriented — cross-language calls per-frame are fine, per-sample are not.

This also preserves the part of the original engine-first goal that survives: you still write real C++ for the systems that define the game.

---

## 4. Rendering configuration

Forward+ renderer. Not Mobile, not Compatibility.

| Setting | Choice | Reason |
|---|---|---|
| Tonemap | **AgX** | Handles highlight rolloff far better than Filmic or Reinhard. Single biggest one-line realism win. |
| GI | **LightmapGI baked** for static track, reflection probes for local specular | A racetrack is static geometry under a fixed sun — the exact case baking was made for. Higher quality and cheaper than SDFGI. The bake **cannot be scripted or run headlessly** — [ADR-0022](DECISIONS.md#adr-0022--lightmapgi-cannot-be-baked-from-a-script-the-bake-is-driven-through-the-editor) — so it is driven through the GUI editor and cannot be a CI step. |
| GI fallback | SDFGI during development | Bakes are slow; iterate with SDFGI and bake for review builds. **SDFGI over-bounces relative to the bake** and its cascade cannot resolve thin geometry, so tuning under it and shipping baked shifts the look. Iterate with it, judge with a bake. ADR-0022. |
| Shadows | Directional, 4 PSSM splits, tight split 0 | Split 0 tightness is set by cockpit self-shadowing (§7). |
| AA | TAA **or** FSR2 upscaling on weaker targets | Alternatives, not a stack: FSR2 is itself a temporal resolve, so enabling both resolves the frame twice. ADR-0021. TAA also stabilizes thin kart frame tubes, and it cleans up the motion blur pass's tap pattern for free. |
| SSAO + SSIL | On, subtle | SSIL adds contact bounce cheaply. |
| SSR | On, tuned so only the track contributes | Weak in Godot, and **not maskable** — `Environment.ssr_enabled` is the only switch, with no per-material or per-instance opt-in. "Track surface only" is therefore a tuning outcome, not a setting: roughness and fade values chosen so a near-horizontal glossy surface is the only thing that returns a visible reflection. Probes do the real work. |
| Fog | Depth fog on, low density | Aerial perspective is a strong realism cue and nearly free — but it is `fog_*` in depth mode, **not** volumetric fog, which is a 64 m froxel volume for light shafts and is neither. [ADR-0021](DECISIONS.md#adr-0021--two-of-architecturemd-4s-rendering-settings-named-the-wrong-feature). |
| Sky | PhysicalSkyMaterial, real sun angle | Time of day is a lever, not a decoration. |
| Motion blur | **Compositor effect, custom** | Godot has none built in. Required. Built and measured in M1 — [ADR-0019](DECISIONS.md#adr-0019--motion-blur-is-a-gather-along-godots-velocity-buffer-with-four-limits-stated) has the working version, its cost, and the four things it does not do. |
| DOF | Cockpit only, very subtle | Overdone DOF reads as fake immediately. |
| Decals | Skid marks, track grime | Godot decals are cheap and sell surface history. |

---

## 5. The realism plan

Realism is not an asset problem. In rough order of impact per hour spent:

1. **Correct real-world scale.** Godot is 1 unit = 1 meter. A kart is ~1.05 m wheelbase, ~1.4 m track width, ~0.28 m tall at the frame. Get this wrong and nothing else can save it — the brain reads scale before it reads shading.
2. **Texel density discipline.** Pick a standard (512 px/m for track surface, 256 px/m for props) and hold it everywhere. Mismatched density is the most common tell in amateur work.
3. **Photoscan materials.** CC0 scanned asphalt, concrete, curb paint, rubber. Real measured albedo and roughness beat anything hand-authored or generated.
4. **AgX tonemapping with proper exposure.** Physically-plausible light values, not eyeballed brightness.
5. **Baked GI plus reflection probes.** Static track, fixed sun.
6. **Motion cues.** Motion blur, speed-driven FOV, camera shake at the frame's natural frequency. A realistic still image that moves wrong reads as fake.
7. **Surface history.** Skid decals, marbles off the racing line, dirt at track edges, curb paint scuffed where cars actually clip it.
8. **Triplanar detail textures** on the track for close-up asphalt grain under the cockpit camera.
9. **Real kart reference data.** Mass 175 kg with driver, real torque curve, real tire compound behavior. §6. The figures live in `src/core/kz_reference.h` so the solver, the validation suite, and the tuning UI cannot each keep a different copy.
10. **Measured reference before it is modeled — for anything sourced from outside this project, not only for shape.** Item 3 says measured reality beats hand-authoring, and that applies to shape as much as to albedo. A generated part is modeled from photographs of the real one and the photographs are recorded in [`REFERENCES.md`](REFERENCES.md) — which is a numbered list precisely so a shape can be argued about against a source rather than against a recollection. Prose is not a substitute: issue #116 built a whole powertrain from accurate written sources and none of it looked like an engine, and the radiator was built wrong twice from a correctly-quoted "55° to the horizontal" because that sentence never named an axis. See ADR-0028 and ADR-0029.

    **This was written for geometry and the scope was too narrow — twice, expensively.** §6.4's lateral acceleration band was written from prose in a planning session, never sourced, and read as sustained when it described transient peaks; it was the project's substitute ground truth for two milestones and its upper half described a state the kart physically cannot occupy ([ADR-0034](DECISIONS.md#adr-0034--64s-lateral-band-was-a-peak-figure-being-read-as-a-sustained-one)). §12's engine harmonic stack was about to be built on the 1/n rolloff a synthesizer gives you by default, which measurement shows is roughly what a *chainsaw* does and nothing like a tuned two-stroke pipe. Neither is geometry, so neither was covered.

    The rule is therefore: **any constant, curve or band this project takes from the outside world is sourced in `REFERENCES.md` before it is depended on, with its measurement method named, or is labeled in place as unmeasured.** A figure whose provenance is "it sounded right" is the most expensive kind, because everything downstream is calibrated against it — `tire.h`'s `peak_friction` is unsourced and every deformable-surface grip multiplier is a quotient of it ([#128](https://github.com/skiretic/kartgame/issues/128)).

Note items 1, 2, 4, 6, 9, and 10 cost almost nothing and outrank every asset decision. Item 10 is the cheapest of the lot and was the last one learned — and then had to be learned a second time, wider.

---

## 6. Vehicle model

Godot's `RigidBody3D` with Jolt underneath. `VehicleBody3D` is explicitly not used.

### What makes a kart a kart

Not a small car:

- **Solid rear axle, no differential.** Both rear wheels are locked together.
- **No suspension.** The chassis frame itself flexes.
- Therefore the frame must **lift the inside rear wheel** in a corner, or the locked axle scrubs and the kart pushes straight on. That wheel lift *is* the differential.

Model it explicitly: stiff spring rates, a chassis torsional-flex term, and caster-induced jacking from steering angle. Get it right and it feels like a kart. Skip it and it feels like a shopping trolley.

### Components

| Part | Model |
|---|---|
| Chassis | `RigidBody3D`, **175 kg with driver** (KZ class minimum), inertia from box approximation, CoM slightly rearward |
| Wheels | Per-wheel raycast from mount point; shapecast where curb robustness demands it |
| Suspension | Spring + damper, split bump/rebound, travel limits, plus the flex and jacking terms above |
| Tires | Simplified Pacejka: lateral by slip angle, longitudinal by slip ratio, combined through a friction ellipse, grip falling with normal load |
| Weight transfer | **Emergent** from body and suspension forces. Never faked with a bias term. |
| Steering | Ackermann geometry plus caster jacking |
| Drivetrain | **6-speed sequential, hand shifter, clutch lever, engine braking** — a real subsystem, see §6.3 |
| Brakes | All-wheel, rear-biased, for gameplay readability |
| Aero | Light drag, negligible downforce |
| Surfaces | asphalt / curb / grass / dirt → grip multiplier, roughness, particle and audio hooks |

### 6.3 KZ drivetrain

KZ is a shifter kart. The gearbox is a real subsystem, not a parameter.

Reality being modeled:

- 125cc two-stroke, ~45–50 hp, peak around 13,000 rpm, **narrow powerband** — roughly 9,000–14,000 rpm usable
- **6-speed sequential**, hand shifter on the right
- **Clutch lever** on the wheel, used for launches; racing upshifts are clutchless
- **Engine braking is heavy** on a two-stroke. Lifting the throttle decelerates hard enough that on some corners it shapes entry more than the brakes do. Miss this and the kart feels like it's coasting on ice.
- Over-revving on a downshift is a real mistake worth modeling

Implementation:

| Element | Model |
|---|---|
| Engine | Torque curve lookup against rpm, scaled by throttle |
| Transmission | Gear ratios × primary reduction × final drive (sprocket) → wheel torque |
| Clutch | Slip element with a torque capacity; locks above a threshold |
| Shifting | 50–80 ms shift time with a torque cut |
| Engine braking | Negative torque curve against rpm when throttle is closed |
| Rev limiter | Soft cut approaching redline; over-rev on downshift is possible and punished |

Input: shift up and shift down as discrete actions (paddle-style on a gamepad), clutch on an analog axis where one exists. Auto-clutch and auto-shift exist as assists — without them, a player new to shifter karts cannot complete a lap.

### 6.4 Validating the physics without a real circuit

A fictional track means no published lap time to check against. Real KZ performance figures substitute as the validation targets:

| Metric | KZ reference |
|---|---|
| Top speed | ~135–145 km/h, gearing dependent |
| 0–100 km/h | ~3.5 s |
| Lateral, sustained (skidpad) | ~1.5–2.0 g |
| Lateral, transient peak | ~2.0–2.5 g |
| Peak braking | ~1.5–2.0 g |
| Weight | 175 kg minimum including driver |
| Usable powerband | ~9,000–14,000 rpm |

**The two lateral rows are two different quantities, and the split is not pedantry.** A steady-state corner is bounded by the kart's own geometry: it tips about the line joining its outside contact patches, at `arm / CoM height`, which for the mass table in `src/core/chassis.h` is **2.43 g turning left** and 2.81 g turning right — the two differ because 27 kg of engine, exhaust and radiator put the center of mass 41 mm right of the centerline. Nothing sustains more lateral acceleration than it tips at, so a *sustained* band topping out at 2.5 g asks for a state the kart cannot occupy; and FIA Karting Art. 8.1.1 caps overall width at 1400 mm, which this kart's rear track already is, so no legal geometry fixes it either. A *transient* peak may exceed the threshold freely, because tipping takes time: 2.5 g held for 0.2 s rolls this kart 0.4° and lifts the inside wheels 4 mm, while the same 2.5 g held for 2.6 s puts it over. Published kart figures near 2.5 g are peak-channel readings from steering-wheel-mounted loggers whose accelerometer sits ~0.6 m ahead of the center of mass, where turn-in yaw acceleration adds several tenths of a g the chassis never felt. This table said "peak lateral acceleration" for two milestones while `kz_reference.h` labeled the same two constants "steady-state skidpad", and every measurement taken against them was sustained. [ADR-0034](DECISIONS.md#adr-0034--64s-lateral-band-was-a-peak-figure-being-read-as-a-sustained-one) has the arithmetic and the sourcing, including which edge of the sustained band is weakly sourced.

Build instrumented test scenarios that measure each — straight-line acceleration, constant-radius skidpad for lateral g, braking-distance test — and assert against these ranges in CI. **The skidpad measures the sustained row; only a transient probe may be judged against the peak row.** This *is* the physics regression suite from §14, and it replaces the lap-time ground truth a real circuit would have given you.

### Integration rate

`_physics_process` at **120 Hz** (`physics/common/physics_ticks_per_second`), with the vehicle solver substepping internally to **240 Hz**. Tire models go unstable at 60 Hz. This is not tunable-down.

### Assists

Steering aid, stability control, ABS-lite, **auto-clutch, auto-shift**. Tunable sliders. Auto-clutch and auto-shift default *on* — a KZ shifter is genuinely hard, and an unassisted first lap ends in a stall. The rest default off.

---

## 7. Cameras

### Chase

Spring arm with velocity look-ahead, speed-driven FOV, lateral-G roll, and a wall-collision raycast so geometry never clips the near plane.

### In-kart cockpit

- **Head stabilization is mandatory.** Raw chassis rotation on a cockpit camera is nauseating and unreadable at speed. Partially decouple pitch and roll (~0.2–0.4 of chassis rotation), keep yaw mostly locked to chassis, add look-into-corner yaw from steering and velocity, plus a small G-force positional offset with spring return.
- **Cockpit geometry required:** nose, floor tray, steering wheel animated by input, driver arms on 2-bone IK to the wheel.
- Interior detail is an LOD the chase camera never sees.
- **Near plane 0.02–0.05 m.** Check depth precision at that near plane early.
- **Shadow split 0 tightened** for cockpit self-shadowing; splits differ per rig.
- Per-rig FOV and motion blur strength.
- Rear-look button. Virtual mirror deferred.

### Free / debug

Detached flight camera with frustum freeze for inspecting culling from outside.

**Rule:** cameras read sim state, never write it. A camera that influences the sim breaks replays.

---

## 8. Determinism, ghosts, replays

The vehicle sim lives in your C++ extension, so you control the part that matters.

1. `_physics_process` is already fixed-timestep. Never read wall-clock in sim code.
2. Gameplay state is a pure function of `(seed, input stream, tick count)`.
3. **PCG32**, explicitly seeded per system. Gameplay RNG and presentation RNG are separate streams — a particle spawn must never advance a gameplay generator.
4. Stable iteration order everywhere.
5. Jolt with fixed substeps and stable body ordering.
6. Verification harness: hash sim state every N ticks; a replay re-sim that diverges fails the test. Runs in CI.

Storage:

- **Replay** = seed + input stream + tick count. Tiny, reproduces everything.
- **Ghost** = transform stream *plus* input stream. Transforms replay robustly across physics retuning; inputs catch determinism regressions. Store both — cheap and diagnostic.

Documented limit: same-binary reproduction is expected, cross-platform bit-determinism is not. Godot and Jolt make no such guarantee. If cross-platform rollback netcode ever matters, that is a fixed-point rewrite of the vehicle solver.

---

## 9. Input

Godot's input map, action-based, fully remappable. Gamepad support is built on the SDL controller database — Xbox, PlayStation, Switch, and generic pads all enumerate.

### DualSense on macOS

Sticks, buttons, and analog triggers work out of the box. That covers testing.

The interesting features do not:

| Feature | Status in Godot |
|---|---|
| Sticks, buttons, analog triggers | Works, no effort |
| Rumble | `Input.start_joy_vibration`; macOS support has historically been patchy — verify early |
| **Adaptive triggers** | **Not exposed.** Requires a HID GDExtension sending DualSense feature reports. |
| Gyro / accelerometer | Not exposed for gamepads. Same extension. |

Adaptive triggers are worth real consideration despite the cost: R2 resistance rising with engine load, L2 gaining a hard detent at lock-up threshold. That puts the tire model in your finger instead of on the HUD, and it surfaces §6 better than any UI could. Scheduled as its own milestone so it can be cut without disturbing anything else.

**Design rule:** all of it is a probed capability. No gameplay logic may assume analog triggers, rumble, or haptics exist. Haptics are presentation outputs driven by sim state — outside the deterministic sim, unable to affect replays.

---

## 10. AI opponents

AI emits **the same input struct a human does** — steering, throttle, brake. Never forces, never teleports. Determinism holds and the AI is bound by identical physics.

- **Racing line:** generated from the track spline by curvature minimization. Midpoint-and-smooth first; iterative gradient descent when it matters.
- **Speed profile:** quasi-static solver — max cornering speed from curvature and lateral grip, then backward and forward passes to place braking and acceleration points.
- **Controller:** pure-pursuit steering to a lookahead point, PID on target speed.
- **Difficulty:** lookahead distance, throttle discipline, grip ceiling, mistake injection rate.
- **Racecraft:** lateral offset from the line via local occupancy sampling.
- **No rubberbanding** — ruled out by the sim-lite direction.

---

## 11. Content pipeline

### CC0 photoscans — the realism backbone

| Source | Content | License |
|---|---|---|
| **Poly Haven** | HDRIs, PBR materials, models | CC0, unambiguous |
| **ambientCG** | PBR materials, scanned surfaces | CC0, unambiguous |
| Fab free tier (Megascans) | Scanned materials and props | **Check the license before use** — Epic's terms are not CC0 and have carried engine restrictions |

Prefer CC0 sources. For an open source project, license clarity is worth more than a marginally better scan. Record provenance for every imported asset in `ATTRIBUTION.md`, even for CC0 where it is not legally required.

### Procedural — precision where it counts

Karts and tracks are generated because they need exact proportions and clean topology. This is precisely where AI mesh generation fails.

Geometry generation runs in **Blender via `bpy`**, headless:

```bash
blender --background --python tools/blender/genkart.py -- --out assets/generated/kart.glb
```

| Script | Output |
|---|---|
| `genkart.py` | Parametric KZ kart — frame tubes, floor tray, seat, wheels, engine and exhaust, **cockpit interior**, steering wheel, driver. UV unwrap, normal bake, LOD chain, glTF. |
| `gentrack.py` | Reads `track.json`. Track surface, curbs, run-off, barriers, UV-per-meter, lightmap UV2, LODs, glTF. |

Blender gives bevels, unwrapping, normal baking, and decimation for free. All three would be substantial C++ work for a worse result.

### `track.json` — one definition, two consumers

The track spline is authored once and read twice:

```
track.json  ──┬──►  gentrack.py (Blender)  ──►  visual mesh, curbs, UVs, LODs  ──► glTF
              └──►  C++ GDExtension        ──►  collision, checkpoints,
                                                 racing line, material zones
```

Contains control points with position, width, banking, and elevation, plus surface-type spans and start-grid placement. Because both consumers read the same file, what you see and what you collide with cannot drift apart.

### Track design constraints

The first track is fictional, so plausibility has to be enforced deliberately rather than inherited from a real layout:

- 1,000–1,500 m lap length (typical for a KZ circuit)
- Minimum corner radius sane against the speed the physics actually reaches in that gear
- Run-off proportional to approach speed
- At least two genuine overtaking spots — a long straight into a heavy braking zone
- Elevation change, both for looks and because it loads the tires
- Validation pass: closed loop, no self-intersection, no radius the kart physically cannot take

### AI-generated — gap filling

Decals, grime and wear overlays, background props, signage and banners, skybox variants where an HDRI does not fit. Local ComfyUI with SDXL or Flux, seamless tiling, delight, then normal and roughness extraction.

For meshes (Hunyuan3D, TRELLIS, Stable Fast 3D), assume a retopology pass in Blender. Output is dense triangle soup with poor auto-UVs. Acceptable for background props, never for anything the player sees up close.

### Import conventions

- glTF 2.0 as the interchange format; `.blend` imports directly if Blender is installed
- 1 unit = 1 meter, Y-up, -Z forward
- Texel density standard fixed and enforced (§5)
- VRAM compression on import; source files kept out of the runtime path

---

## 12. Audio

Racing audio is a primary feel channel, not decoration.

- **Engine note synthesized live**, not sampled. `AudioStreamGenerator` fed from C++: harmonic stack with fundamental driven by RPM, per-harmonic gain envelopes shaped by load, noise layer, comb-filtered exhaust resonance. Sample sets always give themselves away at transitions; synthesis does not.
- **Tire scrub:** filtered noise modulated by slip magnitude — falls straight out of §6 for free.
- **Wind:** speed-driven filtered noise.
- **Surface-dependent** impacts and rolling.
- Godot's built-in 3D audio for attenuation, Doppler, and panning.

---

## 13. Project structure

```
kartgame/
  project.godot
  kartgame.gdextension      entry symbol + per-platform library paths
  SConstruct                wraps godot-cpp's own build
  addons/
  third_party/
    .gdignore               keeps Godot's importer out of the subtree
    godot-cpp/              git submodule, pinned by commit
  bin/                      built extension libraries (gitignored)
  src/                      C++ GDExtension (SCons + godot-cpp)
    vehicle/                chassis, suspension, tires, gearbox, clutch
    track/                  track.json parsing, collision, checkpoints,
                            racing line, material zones
    ai/                     racing line solver, speed profile, driver
    audio/                  DSP graph, engine synthesis
    input/                  optional HID layer (DualSense)
    core/                   math, PCG32, state hashing, KZ reference figures.
                            No godot-cpp anywhere below this line — ADR-0017.
  scenes/
    game/                   race, menu, results
    kart/                   kart scene, cockpit interior
    track/                  track instances
    ui/                     HUD, menus, settings
  scripts/                  GDScript game flow
    render/                 compositor effects and their compute shaders —
                            ADR-0020
    look/                   look-development scenes, built from parameters
    util/
  assets/
    materials/              CC0 photoscans (Git LFS)
    hdri/                   CC0 environments (Git LFS)
    generated/              tool output, gitignored, reproducible
  data/
    track.json              spline definition — single source of truth
  tools/
    blender/                genkart.py, gentrack.py (bpy, headless)
    bake/                   lightmap and asset bake entry points
    assets/                 CC0 downloaders — third-party binaries arrive by
                            script with pinned checksums, never by hand
    shots/                  renders a scene to a PNG at fixed parameters, so a
                            still is reproducible from the command that made it
    verify/
  tests/
  docs/
```

---

## 14. Testing

| Layer | Approach |
|---|---|
| C++ units | doctest or Catch2 over `src/core/` — genuinely engine-independent, because nothing in `src/core/` includes godot-cpp ([ADR-0017](DECISIONS.md#adr-0017--srccore-is-compiled-without-godot-cpp)). Vehicle math, tire curves, and the spline solver live there for exactly this reason. |
| C++ integration | `tools/verify/verify_extension.gd` headless — asserts the things unit tests cannot see: that the extension loads, that its API version matches the engine, that float precision agrees across the boundary |
| Lightmap preflight | `tools/bake/preflight.gd` headless — a bake reports "no meshes" for four unrelated causes, so this names the offending node instead. The bake itself cannot be a CI step (ADR-0022); its preconditions can be. |
| Determinism | Replay re-sim state-hash comparison (§8). The most valuable test in the project. |
| Physics regression | Fixed input scripts producing recorded lap times; fail the build if a tuning change shifts them unexpectedly |
| Visual | Golden-image comparison on a fixed camera and fixed time of day |
| GDScript | gdUnit4 or GUT for game flow and race rules |
| CI | GitHub Actions: build the extension for macOS/Windows/Linux, run C++ units and the determinism harness headless. Headless verification runs on **Linux**, because the macOS runner hits the MoltenVK defect above. |

---

## 15. Performance budgets

Target 60 fps = 16.6 ms. Stated up front so regressions are visible rather than gradual.

| Subsystem | Budget |
|---|---|
| Vehicle sim (all karts, substepped) | 2.0 ms |
| Jolt collision | 1.5 ms |
| AI | 0.5 ms |
| Rendering (all passes) | 10.0 ms |
| Audio | 0.5 ms |
| Game logic, UI | 1.0 ms |
| Headroom | 1.1 ms |

Steam Deck at 800p is the honesty check — it will find the parts of §4 that are too expensive.

---

## 16. Repository and licensing

Free and open source, so the repo is the deliverable, not just the build.

- **Public git repository** at `github.com/skiretic/kartgame`, MIT, with LFS configured before the first binary landed.
- **Git LFS from the first commit**, before any binary lands. Retrofitting LFS means rewriting history. Track `*.png *.jpg *.exr *.hdr *.ktx *.glb *.blend *.wav *.ogg`.
- `assets/generated/` is gitignored and fully reproducible from tools — generated output never enters version control.
- **License split:** code under MIT or Apache-2.0; assets under CC0 or CC-BY as their sources require. State both in `README.md`.
- `ATTRIBUTION.md` listing every third-party asset with its source and license. Do this continuously — reconstructing provenance later is miserable.
- Godot being MIT means contributors clone and build with no account, no EULA, and no 100 GB download.
- Distribution: GitHub Releases, and itch.io if wanted. macOS notarization and Windows signing are only worth it if unsigned-binary friction becomes a real complaint.

---

## 17. Game rules and modes

Underspecified in the first draft. Needed for a coherent race:

- **Track limits:** off-track detection, lap invalidation in time trial, penalty or slowdown in a race
- **Respawn:** rescue after leaving the world or beaching, with a rejoin cooldown
- **Kart-to-kart collision response:** tuned separately from world collision — realistic rigid-body contact between karts reads as chaotic and unfair, so bumping needs its own damping and impulse clamping
- **Damage:** none. Karts don't crumple and it adds nothing here.
- **Race flow:** grid, countdown, racing, finish, results
- **Time trial:** ghost against personal best, sector splits, lap validation
- **Weather and time of day:** a strong realism lever and cheap in Godot — sun angle, cloud cover, wet track with a grip multiplier feeding §6. Post-demo, but the material and physics hooks are designed in now.

---

## 18. Accessibility and comfort

Cockpit view makes several of these load-bearing rather than optional:

- FOV slider per camera rig
- Camera shake and head-motion intensity sliders, including full off
- Motion blur toggle
- Horizon-lock option for the cockpit camera
- Full input remapping, adjustable deadzones and response curves
- Assist sliders (§6) doubling as difficulty accessibility
- Colorblind-safe HUD, scalable UI

---

## 19. Risks

| Risk | Mitigation |
|---|---|
| Custom force application fights Jolt's solver | Applying per-wheel forces to a `RigidBody3D` from `_physics_process`, while substepping internally at 240 Hz, is the least-charted part of this plan — Godot's integration hook and Jolt's internal stepping have to agree. M3a proves the integration with the built-in vehicle before the custom solver is layered on. |
| Vehicle tuning eats unbounded time | Telemetry lands *with* the vehicle (M3b), never after. Replay-based A/B. Timebox and move on. |
| KZ gearbox makes it unplayable for newcomers | Auto-clutch and auto-shift default on (§6.3). A shifter kart is hard enough that an unassisted first lap ends in a stall. |
| "Realistic" is a moving target | §5 is an ordered checklist. Work top-down and stop when it reads right at speed, not when it's perfect in a screenshot. |
| Godot version churn | Pin the Godot version in `README.md`. Upgrade deliberately between milestones, never mid-milestone. |
| Determinism drift | State-hash harness at M6, in CI from that point. |
| Repo bloat from photoscans | Git LFS from commit one. Generated content gitignored. |
| Asset license contamination | `ATTRIBUTION.md` updated at import time. Prefer CC0 sources over marginally better restricted ones. |
| ~~Motion blur compositor effect proves hard~~ | **Closed at M1.** It was not hard; the surprises were elsewhere and are recorded in [ADR-0019](DECISIONS.md#adr-0019--motion-blur-is-a-gather-along-godots-velocity-buffer-with-four-limits-stated). Two of its four stated limits become real work later — sky rotation blur, and object silhouettes at M7. |

---

## 20. Deferred

Netcode (Godot's high-level multiplayer API is a reasonable starting point), cross-platform bit determinism, weather and time of day, virtual mirror, photo mode and replay cameras, additional tracks and karts, championship structure, split-screen, localization.
