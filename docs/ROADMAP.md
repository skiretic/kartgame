# Roadmap

Companion to `ARCHITECTURE.md`. Each milestone has a testable acceptance gate.

Effort is relative sizing, not calendar time. **M3b dominates** — vehicle feel is where the project lives, and telemetry arriving alongside it is the only defense against it eating everything.

---

## Tech demo definition

The demo is **M0 through M8**. It ships when all of this is true on macOS:

- A generated KZ shifter kart drives on a generated track, gamepad in hand, locked 60 fps
- It reads as realistic in motion — photoscan materials at correct scale, baked GI, AgX, motion blur
- Both cameras work: chase and in-kart cockpit with head stabilization and comfort options
- Lap timing and checkpoints work; you can race your own ghost
- One AI opponent drives a generated racing line through the same input path you use
- Telemetry, physics visualization, and tuning presets are live
- The physics validation suite passes against real KZ performance figures
- Engine audio is synthesized from RPM and load
- A recorded replay re-sims to an identical state hash
- The repo is public, MIT, LFS-clean, with attribution recorded

---

## M0 — Project foundation

Toolchain installed and verified 2026-07-24: Godot 4.7.1, Blender 5.2.0 LTS, SCons, git-lfs 3.7.1.

- `git init`, **Git LFS configured before any binary is committed** — retrofitting means rewriting history
- `.gitignore` including `assets/generated/`, `.gitattributes` with LFS patterns
- Remote wired to `github.com/skiretic/kartgame`
- Godot project, Forward+ renderer, version pinned
- `godot-cpp` + SCons GDExtension building and loading
- MIT `LICENSE`, `README.md`, `ATTRIBUTION.md`
- Input map with actions, DualSense enumerating over USB and Bluetooth
- GitHub Actions building the extension for macOS, Windows, Linux

**Accept:** a C++ function called from GDScript prints in-editor. DualSense sticks and analog triggers register. CI green on three platforms. `git lfs ls-files` shows tracking active before any binary exists.

Effort: S

---

## M1 — Look development

Deliberately before the vehicle. The look target is the largest open question in the plan, and fighting art direction while also fighting tyre curves is how projects stall.

- Test scene: flat asphalt plane at correct real-world scale, reference cube of known dimensions
- CC0 photoscan materials from Poly Haven / ambientCG at the fixed texel density
- AgX tonemapping, physical sun angle, PhysicalSkyMaterial, HDRI environment
- LightmapGI bake path end to end; SDFGI as the iteration fallback
- Reflection probes, SSAO, SSIL, subtle volumetric fog
- TAA, FSR2 configured
- **Motion blur compositor effect prototyped** — the one genuinely uncertain piece of §4, surfaced now rather than at M10
- `ATTRIBUTION.md` populated as assets land

**Accept:** a still of an empty asphalt plane under a physical sun reads as photographic. A test object at 80 km/h has motion blur that sells the speed. Scale verified against the reference cube.

Effort: M

---

## M2 — Blender pipeline

Proves the content path before anything depends on it.

- `tools/blender/genkart.py` — parametric KZ kart: frame tubes, floor tray, seat, wheels, engine and exhaust, steering wheel, **cockpit interior**, driver
- UV unwrap, lightmap UV2, normal bake, LOD chain via decimation
- glTF export, headless invocation, deterministic output from fixed parameters
- Godot import at correct scale, Y-up, -Z forward
- Rendered turntable for visual verification
- `bpy` version pinned to Blender 5.2 API

**Accept:** `blender --background --python tools/blender/genkart.py` produces a glTF that imports into Godot at correct scale with clean UVs and working LODs. Re-running with identical parameters produces an identical mesh.

Effort: M

---

## M3a — Sim scaffold and integration proof

De-risks the least-charted part of the plan: how Godot's physics hook, Jolt's stepping, and a custom force-based solver coexist.

- `_physics_process` at 120 Hz, Jolt confirmed as the active 3D physics engine
- C++ extension structure: `core/` math, PCG32, state hashing
- Physics debug visualization: shapes, contacts, normals, raycasts, centre of mass
- **Built-in `VehicleBody3D` driving the M2 kart mesh** — throwaway, purely to prove the integration boundary and give the camera and track work something real to sit on
- Debug free camera with frustum freeze

**Accept:** the kart mesh drives on generated collision geometry with no jitter. Debug draw is correct. Identical scenarios produce identical state hashes across repeated runs. The force-application and substepping boundary is understood and documented before M3b builds on it.

Effort: S

---

## M3b — Custom KZ vehicle

The feel milestone. `VehicleBody3D` is deleted here.

- Chassis `RigidBody3D`, 175 kg with driver, KZ mass properties
- Per-wheel raycasts, spring and damper, travel limits
- **Chassis torsional flex and inside-rear-wheel lift** (`ARCHITECTURE.md` §6) — the defining kart dynamic
- Solid rear axle, no differential
- Pacejka-style tyres, friction ellipse, load-sensitive grip
- Ackermann steering with caster jacking
- **KZ drivetrain** (§6.3): torque curve, 6-speed sequential, clutch, 50–80 ms shift with torque cut, engine braking, rev limiter, over-rev consequences
- Auto-clutch and auto-shift assists, on by default
- 240 Hz solver substepping inside the 120 Hz tick
- Surface types with grip multipliers
- **Telemetry ships with this milestone:** per-wheel load, slip angle, slip ratio, suspension travel, force vectors, RPM, torque, gear, clutch state — scrolling graphs
- Tuning presets saving to disk in a diffable text format
- **Physics validation scenarios** (§6.4): acceleration run, constant-radius skidpad, braking test

**Accept:** the kart is driveable and unmistakably not a car — the inside rear wheel visibly lifts in a corner and the kart rotates on it. Engine braking is felt on corner entry. Validation scenarios land inside the KZ reference ranges: ~140 km/h top speed, ~3.5 s to 100, 2.0–2.5 g lateral, 1.5–2.0 g braking. Presets round-trip. No tyre-force instability at any speed.

Effort: XL

---

## M4 — Cameras

- Chase rig: spring arm, look-ahead, FOV kick, lateral-G roll, wall raycast
- **Cockpit rig** with head stabilization, look-into-corner yaw, G-force offset with spring return
- Cockpit interior from `genkart.py`: steering wheel animated by input, driver arms on 2-bone IK
- Interior LOD culled from chase view
- Near plane 0.02–0.05 m validated for depth precision
- Shadow split 0 tightened, per-rig split configuration
- Per-rig FOV and motion blur strength
- Rear-look button
- Comfort options: FOV, shake intensity, head-motion intensity, horizon lock, motion blur toggle

**Accept:** both views are comfortable across a sustained session — the cockpit does not induce nausea and the horizon stays readable at speed. No depth artifacts on cockpit geometry. Wheel and hands track input correctly.

Effort: M

---

## M5 — Track generation

- `data/track.json` schema: control points with position, width, banking, elevation, surface spans, start grid
- `tools/blender/gentrack.py` — surface, kerbs, run-off, barriers, UV-per-metre, triplanar detail blending, lightmap UV2, LODs
- C++ consumer: collision, checkpoints, racing line, material zones from the same file
- Runtime scatter: seeded Poisson-disk props and foliage
- Lightmap bake integration
- First fictional circuit built to §11 constraints: 1,000–1,500 m, two real overtaking spots, elevation change
- Validation: closed loop, no self-intersection, no radius the kart physically cannot take

**Accept:** a track authored in `track.json` is driveable end to end with correct collision, materials, scatter, and a clean lightmap bake. Visual mesh and collision agree everywhere. Validation rejects a deliberately self-intersecting spline.

Effort: L

---

## M6 — Race loop and determinism

- Checkpoint volumes, lap counting, lap and sector timing
- Race states: grid, countdown, racing, finished, results
- Track-limits detection, lap invalidation, respawn with rejoin cooldown
- Replay recording: seed + input stream + tick count
- Ghost recording: transform stream plus input stream; playback and rendering
- **Determinism harness:** state hash every N ticks, replay re-sim must match. Into CI from here on.
- HUD: speed, RPM, **gear**, lap, position, timing, sector deltas
- Settings menu, save and load of settings and best laps

**Accept:** a recorded lap re-sims to an identical state hash. The ghost plays back frame-accurate. The harness fails loudly when determinism is deliberately broken in a test.

Effort: M

---

## M7 — AI

- Racing-line generation by curvature minimization from the track spline
- Quasi-static speed profile with backward and forward braking-point passes, **gear-aware** — a KZ's narrow powerband means the profile has to know what gear the exit is taken in
- Pure-pursuit steering, PID speed control, shift logic, emitting the standard input struct
- Difficulty scaling: lookahead, throttle discipline, grip ceiling, mistake injection
- Local occupancy sampling for overtaking and avoidance
- Kart-to-kart collision response tuned separately from world collision

**Accept:** one AI runs clean competitive laps through the same physics and input path as the player, shifting sensibly, and races around the player rather than through them. Difficulty tiers produce measurably different lap times. Contact between karts is firm without being chaotic.

Effort: L

---

## M8 — Audio

- `AudioStreamGenerator` fed from C++ DSP on the audio thread
- **Engine synthesis:** RPM-driven harmonic stack, load-shaped envelopes, noise layer, comb-filtered exhaust resonance. A 125cc two-stroke at 13,000 rpm is a distinctive, unforgiving target — thin and screaming, with an audible transition on and off the pipe.
- Shift and clutch sounds driven by drivetrain state
- Tyre scrub from slip magnitude, wind from speed
- Surface-dependent impacts and rolling
- 3D attenuation, Doppler, panning

**Accept:** the engine note tracks RPM and load with no audible stepping, and coming on the pipe is audible. Tyre scrub signals grip loss before the visuals do. Opponents are locatable by ear.

Effort: L

**Tech demo complete at this gate.**

---

## M9 — DualSense haptics *(optional, isolated)*

Standalone so it can be cut without disturbing anything.

- HID GDExtension sending DualSense feature reports over USB and Bluetooth
- Adaptive triggers: R2 resistance scaling with engine load, L2 detent at lock-up threshold
- Rumble from surface roughness, kerb strikes, wheel lock, gear engagement
- Capability probe with graceful fallback on every other pad

**Accept:** trigger resistance tracks the tyre model. The game plays identically and correctly on an Xbox pad with none of it.

Effort: M

---

## M10 — Polish and platforms

- Skid decals, marbles off-line, dirt at track edges, kerb scuffing
- Particles: tyre smoke, dust, debris, two-stroke exhaust haze
- Weather and time of day, wet-track grip multiplier feeding the tyre model
- Steam Deck tuning against the §15 budgets at 800p
- Windows and Linux verified beyond CI — actually played on each
- Accessibility pass (`ARCHITECTURE.md` §18)
- Public release: GitHub Releases, README build instructions, contribution guide

**Accept:** all three platforms hold 60 fps at target resolution. A stranger can clone the repo and build it from the README alone.

Effort: L

---

## Post-demo, unscheduled

Netcode via Godot's high-level multiplayer API, cross-platform bit determinism, virtual mirror, photo mode and replay cameras, additional tracks and karts, other kart classes (TaG single-speed is a trivial variant once the gearbox exists), championship structure, split-screen, localization.
