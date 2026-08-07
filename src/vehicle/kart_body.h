#ifndef KARTGAME_VEHICLE_KART_BODY_H
#define KARTGAME_VEHICLE_KART_BODY_H

#include "audio/engine_voice.h"
#include "audio/noise_voice.h"
#include "core/vehicle.h"
#include "core/vehicle_state.h"

#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <cstdint>

namespace kartgame {

// The boundary. ROADMAP M3b, issues #30 and #31.
//
// `src/core/vehicle.h` is the whole vehicle model and it cannot see Godot —
// ADR-0017. This class is the only thing standing between it and the engine, and
// it does exactly five things:
//
//   1. owns a `RigidBody3D` carrying the KZ mass properties (#30),
//   2. casts four rays and turns what they hit into `GroundQuery` (#31),
//   3. calls `KartVehicle::step` once per 120 Hz tick,
//   4. applies the `VehicleForces` that come back, and
//   5. publishes `VehicleTelemetry` (#43).
//
// There is no vehicle physics in this file and there must never be any. A number
// that belongs to the kart belongs in `src/core/`, where a test reaches it with
// no engine running. What lives here is engine convention — and engine convention
// is precisely what `tools/verify/contact_probe.gd` had to measure, because four
// separate beliefs about it turned out to be wrong.
//
// ## What this node does NOT own
//
// Its own mesh and its own collision shape. `scripts/game/proving_ground.gd`
// builds those and adds them as children, the same way it builds the ground and
// the markings, because they are asset plumbing rather than physics and because a
// C++ node that loads a gitignored `.glb` cannot be instantiated in a test. This
// node asserts in `_ready` that a `CollisionShape3D` child exists and says so
// loudly if it does not — a `RigidBody3D` with no shape falls through the world
// silently, which is a bad half-hour.
//
// ## The frames line up, and that is not a coincidence
//
// `src/core/chassis.h` puts the chassis origin on the ground, laterally centered,
// midway between the axles, with +X right, +Y up and +Z **rearward**
// (`FRONT_AXLE_Z` is -0.525, `REAR_AXLE_Z` is +0.525). Godot's -Z is forward, so
// Godot's +Z is rearward too. The core chassis frame and this node's local frame
// are the same frame, and no conversion happens anywhere in this file:
// `to_global`, `to_local` and the basis columns are the whole mapping.
//
// If that ever stops being true it will be invisible until the kart drives
// backwards — the same failure mode CLAUDE.md's coordinate note describes for the
// Blender exporter — so `_ready` checks the sign of the wheelbase rather than
// trusting it.
//
// ## The five engine conventions this file exists to get right
//
// Every one was measured, and three were believed backwards first. ADR-0033.
//
// **1. `apply_force`'s offset is from the body ORIGIN, in world coordinates.**
// Not from the center of mass. `VehicleForces::application_point` is already in
// that convention and is named for it, so this file passes it through untouched —
// no `- to_global(center_of_mass)`, ever. Subtracting the center of mass adds a
// second torque `(origin - com) x F`, and because the kart's origin and its
// contact patches are both at ground level, that doubled every pitch and roll
// moment in M3a at any center-of-mass height.
//
// **2. Jolt's own friction must be off.** `physics_material_override.friction` is
// set to 0.0 in `_ready`. The tire model owns every tangential newton, so anything
// Jolt contributes is counted twice. The combine rule is measured to be
// `min(a, b)`, so zero on this body is sufficient whatever the ground says.
//
// **3. The project's default damping is not zero.** Godot applies
// `linear_damp = 0.1` and `angular_damp = 0.1` from the project settings — 1 m/s^2
// at 10 m/s, about a quarter of this kart's acceleration there, arriving from
// nowhere. Both go to `DAMP_MODE_REPLACE` at 0.0 and drag is the solver's own
// `drag_area` term.
//
// **4. A ray that starts below a surface returns no hit at all.** Not a hit at
// distance zero — nothing, and `hit_back_faces` does not change it.
// `hit_from_inside` returns distance 0.0 with a zero-length normal, so it is not a
// depth query either. A wheel buried deepest in a curb reports "no contact"
// exactly when it is most loaded. Two things are done about it, in this order:
//
//   - the ray starts `RAY_START_LIFT` **above** the wheel center and the lift is
//     subtracted back off the reported distance, so the hub has to be buried
//     before the query origin is; and
//   - if the cast misses anyway, the last valid normal, surface and depth are
//     latched and re-served, because a corner that was in contact last tick and
//     has since moved *into* the surface has not become airborne.
//
// The latch is not a fix for tunneling. Issue #31's "curb strikes do not tunnel"
// cannot be satisfied by raycasts alone and this file does not claim it is.
//
// **5. `exclude` is required and free.** A ray cast from outside a collider hits
// that collider's own body, 240 times out of 240. Excluding this body's RID costs
// 1.23 us against 1.26 us over 20,000 rays.
//
// ## What is deliberately not read from the engine
//
// `PhysicsDirectBodyState3D.get_contact_impulse` reports 1.000371x the true
// impulse and lags one tick, invariant across 100x mass, 4x load and 60-240 Hz.
// Normal load comes from the suspension spring, which computes it from the ray
// length. Nothing here reads contact impulses.
//
// ## Time ratio is telemetry, and only telemetry
//
// `max_physics_steps_per_frame` clamps and does not bank: under a frame-rate
// collapse Godot runs its eight ticks and stops, simulation time falls behind at a
// measured 0.6476 of wall-clock, and it never catches up. A replay that counts
// ticks cannot see it. So this node measures it — which means it reads a wall
// clock, the one thing `ARCHITECTURE.md` §8 forbids the simulation to do.
//
// The rule that keeps both true: **`time_ratio` is written to telemetry and read
// by nothing else.** It never reaches `DriverInput`, never scales a force, and is
// never hashed. `_physics_process`'s `delta` is not used either — the solver is
// stepped with `1.0 / physics_ticks_per_second`, a constant, because a solver fed
// a float that wobbles in its last bits is not reproducible.
class KartBody : public godot::RigidBody3D {
	GDCLASS(KartBody, godot::RigidBody3D)

protected:
	static void _bind_methods();

public:
	KartBody();

	// How far above the wheel center each suspension ray starts, meters.
	//
	// Convention 4: the query origin has to stay above the surface for the cast to
	// return anything, and the wheel center is only 140 mm up. 60 mm of lift means
	// the hub itself must be 60 mm inside a curb before the cast blinds, which is
	// past the point where the chassis collision shape has already stopped it. The
	// lift is subtracted off the reported distance, so nothing downstream sees it:
	// `KartVehicle::ray_origin` and `ray_length` still define the geometry, and
	// this is only where the query starts.
	static constexpr double RAY_START_LIFT = 0.060;

	void _ready() override;
	void _physics_process(double p_delta) override;

	// --- driver input ---------------------------------------------------------
	//
	// **This class does not read `Input` and must never read it again.** ADR-0040:
	// a node that fetches global input cannot be told to stop, so anything upstream
	// of it — a menu, a pause, an overlay, a replay — has no authority over it. The
	// symptom that proved it was the tuning overlay, which cannot use the arrow keys
	// because they are the second binding on throttle, brake and steer and
	// `set_input_as_handled` cannot reach a singleton poll.
	//
	// Input arrives one of two ways and there is no third:
	//
	//   * `set_input` — a driver node hands over one tick of intent. `PlayerDriver`
	//     is the one that exists; `AIDriver`, `ReplayDriver` and `GhostDriver` fill
	//     the same struct at M6 and M7.
	//   * `input_driver` — a `Callable` that is asked for one tick of intent.

	// Hand this body one tick of driver intent, stamped with the physics tick the
	// producer filled it on.
	//
	// **The tick is a required argument rather than a field on `DriverInput`, and
	// that is a deliberate deviation from ADR-0040's wording.** The ADR says the
	// struct carries the tick. Two reasons it does not: `DriverInput` is passed
	// straight into `KartVehicle::step`, so a field the solver must ignore is a
	// field that eventually gets hashed by accident and moves every recorded state
	// hash in the project; and a struct field defaults to zero and can be forgotten,
	// where a required argument cannot be.
	//
	// Not bound to GDScript. Every producer this file anticipates is C++ or reaches
	// the body through `input_driver` below, and a bound `push_input(Dictionary)`
	// that nothing calls is an advertised API with no reader — which is the failure
	// this project keeps having in its input layer, one level up.
	void set_input(const kart::core::DriverInput &p_input, uint64_t p_tick);

	// A `Callable` returning one tick of driver intent, or an invalid Callable.
	//
	// This is M3a's shape, kept deliberately, and ADR-0040 does not remove it:
	// `tools/verify/drive_probe.gd`'s scenarios are open-loop functions of the tick
	// number, and a "set the axes and hold them" API would make every scenario push
	// values across the boundary itself for no gain. `tools/shots/shoot.gd`'s
	// constant-throttle still is the same mechanism with a lambda that ignores the
	// tick. The arrow ADR-0040 objects to is the one that reached the *global*
	// `Input` singleton; an injected producer, pulled or pushed, is already under
	// the caller's authority.
	//
	// The Dictionary it returns is read with defaults, so M3a's three keys still
	// work unchanged:
	//
	//     { "throttle": 0..1, "brake": 0..1, "steer": -1..1,
	//       "clutch": 0..1, "shift_up": bool, "shift_down": bool }
	//
	// `shift_up` and `shift_down` are **edges** here as everywhere else — true for
	// exactly the tick the request is made. `vehicle_state.h` says why: a level
	// makes a held button shift once per tick through the whole gearbox.
	//
	// **A valid Callable wins over pushed input, and that was measured rather than
	// chosen.** With the push taking precedence, every `drive.sh` scenario ran to
	// the same state hash and 0.2 m of travel: the scene's `PlayerDriver` was
	// pushing the neutral input of a keyboard nobody was touching, and the
	// scenario's Callable was never consulted. A Callable is only ever assigned
	// deliberately, so it is the explicit override.
	void set_input_driver(const godot::Callable &p_driver);
	godot::Callable get_input_driver() const;

	// How many ticks have consumed neutral input because what was pushed was stale.
	//
	// ADR-0040's one failure mode: a producer that does not run, or runs after the
	// body in tree order, leaves the vehicle consuming last tick's input, and *that
	// reads as a physics bug*. Neutral rather than held-last is deliberate — holding
	// the last input means a crashed AI drives into a wall at full throttle, and it
	// means a divergence between two runs is invisible in the hash for as long as
	// the throttle agrees either way.
	//
	// The node says so once on the console. This counter is how a gate asserts it,
	// because "it printed a warning" is not something a headless run can check.
	uint64_t get_stale_input_ticks() const;

	// Latch a shift request from code, for a UI button or a test. Consumed and
	// cleared by the next tick, so a caller cannot lose one by asking between
	// physics ticks.
	void request_shift_up();
	void request_shift_down();

	// --- configuration, exposed because §19 names unbounded tuning as the risk --

	// Auto-clutch and auto-shift. #40: both default **on**, because an unassisted
	// first lap in a KZ ends in a stall on the grid.
	void set_auto_clutch(bool p_enabled);
	bool is_auto_clutch() const;
	void set_auto_shift(bool p_enabled);
	bool is_auto_shift() const;

	// Caster jacking on or off. Not a tuning knob — the instrument issue #32's
	// second acceptance criterion is measured with. False zeroes the front
	// geometric offsets and changes nothing else, so the difference between two
	// runs is the jacking and nothing else.
	void set_jacking_enabled(bool p_enabled);
	bool is_jacking_enabled() const;

	// The solver's own tuning, forwarded. Each is a real quantity with a derivation
	// in `src/core/vehicle.h`; they are reachable from here so a tuning question can
	// be asked against a graph, which §19 names as the only defense against tuning
	// by feel.
	void set_drag_area(double p_value);
	double get_drag_area() const;
	void set_rolling_resistance(double p_value);
	double get_rolling_resistance() const;
	void set_brake_torque_front(double p_value);
	double get_brake_torque_front() const;
	void set_brake_torque_rear(double p_value);
	double get_brake_torque_rear() const;
	void set_steer_rate(double p_value);
	double get_steer_rate() const;
	void set_frame_torsion_nm_per_deg(double p_value);
	double get_frame_torsion_nm_per_deg() const;

	// The tire's peak friction, all four corners at once. One number rather than
	// four because the question it exists to answer is #120's — "what would this
	// have to be for §6.4's lateral band to be reachable" — and that is a question
	// about the tire, not about a corner.
	void set_peak_friction(double p_value);
	double get_peak_friction() const;

	// Maximum steer angle at the inner front wheel, radians. `get_steer_lock()`
	// below reads the same number and is the read-out the HUD uses; this is the
	// writable half, added for the tuning registry.
	//
	// **Defended** in `src/core/tuning.h`, and the definition says why: this
	// figure is the angle the bodywork clearance tables in issues #109 and #110
	// were measured at, not a regulation, so raising it steers the front wheels
	// into geometry that was checked against it.
	void set_max_lock(double p_radians);
	double get_max_lock() const;

	// --- lifecycle ------------------------------------------------------------

	// Where `respawn()` puts the kart. Applied immediately, as M3a's did.
	void set_spawn(const godot::Transform3D &p_transform);
	godot::Transform3D get_spawn() const;

	// Back to the grid: body placed at the spawn, both velocities zeroed, **and the
	// solver reset**. One call rather than two, because a body teleported without
	// its solver reset keeps an axle speed from wherever it used to be and launches
	// itself off the line.
	void respawn();

	// Put the kart in a gear at a road speed with the driveline already turning, and
	// set the body's linear velocity to match along its own -Z. The braking and
	// skidpad scenarios start mid-lap rather than on the grid, and so does a
	// respawn during a session.
	void engage(int p_gear, double p_road_speed_ms);

	// --- read-out -------------------------------------------------------------

	// Issue #43's Dictionary, exactly as `scripts/game/telemetry_panel.gd` documents
	// it and `kart::core::VehicleTelemetry` defines it: `wheel` is an Array of four
	// Dictionaries in FL, FR, RL, RR order, keyed by `WheelTelemetry`'s own field
	// names, and the scalars sit beside it under theirs. The panel finds this by
	// looking for a node in the `telemetry_source` group with a `telemetry()`
	// method, which `_ready` joins — so adding the panel to a scene is the whole
	// integration and this node never learns the panel exists.
	//
	// It allocates. That is why it is a call and not a per-tick push: the panel
	// samples at 120 Hz only while it is open, and nothing else in the frame pays
	// for a Dictionary that nobody is reading.
	godot::Dictionary telemetry() const;

	// Per-wheel geometry for `scripts/debug/physics_draw.gd`, in the same corner
	// order. Keys:
	//
	//     name, origin (world, the wheel center), direction (world, unit, down the
	//     ray), length, radius, contact (bool), latched (bool), point (world),
	//     normal (world), lift, travel, load, slip_angle, slip_ratio, utilization,
	//     steer_angle, surface (SurfaceType), force (world, N)
	//
	// `direction` is served rather than assumed because #124 is exactly that bug:
	// the debug draw pointed its suspension rays along **world** down instead of
	// chassis down, so every ray it drew was wrong the moment the kart rolled.
	// `latched` is separate from `contact` because a latched contact is a wheel
	// that is buried rather than one that is resting, and the two are identical in
	// every other read-out.
	godot::Array wheel_report() const;

	// The scalars M3a's callers read as properties, kept under the same names so
	// the HUD, the chase camera and `drive_probe.gd` need no relearning.
	double get_speed_ms() const;
	double get_throttle_input() const;
	double get_brake_input() const;
	double get_steer_input() const; // normalized, rate limited, -1..1
	int get_wheels_on_ground() const;

	// The `AudioStreamPlayer3D` (or `AudioStreamPlayer`) whose stream is an
	// `EngineVoiceStream`. Issues #81, #82.
	//
	// A path set by the scene rather than a node this class creates, for the same
	// reason the collision shape is: the note comes from the engine mount, which is
	// a position only the scene knows, and a boundary that placed its own emitter
	// would be deciding where the engine is.
	//
	// Empty is legal and silent. Every headless probe runs that way — `drive.sh`
	// switches the HUD and both cameras off already — and a solver that needed an
	// audio device to step would be a solver no gate could run.
	void set_engine_voice_player(const godot::NodePath &p_path);
	godot::NodePath get_engine_voice_player() const;

	// The other two layers. Issue #84, and the same contract as above in every
	// respect: empty is legal and silent, the path is resolved once, and the stream
	// is held rather than looked up per tick.
	//
	// **Three paths and not one, because the three emitters are not in the same
	// place.** The engine note comes from the mount 600 mm behind the driver's right
	// ear, the scrub comes off four contact patches, and the wind is at the
	// driver's head and is not a source in the world at all. `scrub_wind.h` has the
	// argument; the consequence here is that a scene mounts three players and this
	// class publishes the same `EngineAudioInput` to each.
	//
	// **Five now, not three.** #83's shift-and-clutch layer and #85's surface
	// rolling are two more `NoiseVoiceStream`s, and they go in two more places: the
	// gearbox is at the engine and the rolling noise is at the contact patches. All
	// five are `NoiseVoiceStream` bar the engine note, and all five receive the same
	// `EngineAudioInput` -- which is exactly why the struct carries `shifting`,
	// `clutch_slip`, `speed_ms` and `surface` alongside the rpm.
	void set_scrub_voice_player(const godot::NodePath &p_path);
	godot::NodePath get_scrub_voice_player() const;
	void set_wind_voice_player(const godot::NodePath &p_path);
	godot::NodePath get_wind_voice_player() const;
	void set_shift_voice_player(const godot::NodePath &p_path);
	godot::NodePath get_shift_voice_player() const;
	void set_roll_voice_player(const godot::NodePath &p_path);
	godot::NodePath get_roll_voice_player() const;

	// **The steering curve used to live here and now lives on `PlayerDriver`.**
	// ADR-0036 said it was a controller property and gave the derivation;
	// ADR-0040 moved the code to match, because the curve belongs to whichever
	// producer is holding a stick and means nothing to a replay or an AI. The
	// derivation, the deadzone arithmetic and the 100 km/h sweep it is anchored on
	// are all in `src/vehicle/player_driver.h`, which is also where `tuning.h`'s
	// `steer_gamma` row now points.

	// Where the engine sits in the body's own frame, meters. The place to put the
	// emitter, served rather than retyped.
	//
	// `src/core/chassis.h`'s lump table is the single owner of every mass and every
	// position on this kart, and it puts the engine at (0.319, 0.150, 0.190) — 319 mm
	// right of centerline and 190 mm behind the origin, which is where a KZ's engine
	// is and is why the center of mass is 41 mm right. A scene that retyped those
	// three numbers would be a second owner, and the failure mode is silent: the
	// engine note would drift away from the engine the day somebody moved the lump.
	godot::Vector3 engine_mount_position() const;

	// Middle of the rear axle, on the ground, in the body frame. Where the scrub
	// emitter goes -- one source for a four-corner mean, so it belongs between the
	// wheels rather than at one of them.
	godot::Vector3 rear_axle_position() const;

	// The driver's head, in the body frame, meters. Served from the same lump table
	// and for the same reason as `engine_mount_position`.
	//
	// **This is where the listener goes**, and issue #160 is why it exists. Godot
	// falls back to the current `Camera3D` when no `AudioListener3D` is current,
	// which was measured: the level swings 20.7 dB as the chase camera moves from
	// 1 m to 20 m, and an explicit listener pins it flat at every distance. With the
	// listener on the camera, the cockpit view and the chase view hear two different
	// mixes and only one of them was ever judged by ear.
	//
	// `chassis.h` puts "driver head and helmet" at (0.000, 0.560, 0.128). That is a
	// calibrated position rather than a measured one -- the header explains that the
	// driver's fore-aft position is solved for from anthropometric segment fractions
	// because issue #107 leaves the seat geometry untrustworthy -- so it will move
	// when #107 closes. Serving it means the listener moves with it instead of a
	// scene holding a stale copy.
	godot::Vector3 driver_head_position() const;

	// The front wheels' actual steer angle at full input, radians — `steering.h`
	// owns it and the HUD used to read a GDScript constant for it.
	double get_steer_lock() const;

	// The ignition cut's two thresholds, rpm, and whether it is currently cutting.
	//
	// `engine.h` owns both numbers and the taper between them: from
	// `soft_cut_rpm` the ignition starts dropping sparks and drive torque falls
	// linearly to nothing at `hard_cut_rpm`. Served rather than duplicated,
	// because `scripts/ui/driving_hud.gd` marks the limiter on its tachometer and
	// a HUD constant that has to be kept in step with the solver's by hand is one
	// copy of a single-owner number too many — which is the mistake the corner
	// text made with the steering lock until `get_steer_lock()` was added.
	//
	// `is_on_limiter` is deliberately **not** `is_over_rev`. One is the engine
	// doing its job at the top of the range; the other is past the hard cut and is
	// damage `engine.h` accumulates against the engine's health. A driver needs to
	// hear and see the difference, so the HUD gives them separate lamps.
	double get_soft_cut_rpm() const;
	double get_hard_cut_rpm() const;
	bool is_on_limiter() const;

	// The lateral g at which this kart tips, in the direction it is turning.
	//
	// Two numbers and not one, because the kart is not laterally symmetric: 27 kg
	// of engine, exhaust and radiator hang off the right, putting the center of
	// mass 41 mm right of the centerline, so it tips at 2.4336 g turning left
	// against 2.8061 turning right. `chassis.h` derives both from the same lump
	// table. An instrument that drew one symmetric limit would be showing a driver
	// a kart that does not exist — which is issue #129's defect in a different
	// place, and #129 was three disagreeing copies of exactly this number.
	double get_rollover_threshold_g(bool p_turning_left) const;

	// Drivetrain and chassis, individually, for the hot paths that do not want the
	// Dictionary.
	double get_engine_rpm() const;
	double get_engine_torque() const;
	double get_axle_torque() const;
	double get_axle_speed() const;
	int get_gear() const;
	double get_clutch_slip() const;
	bool is_shifting() const;
	bool is_over_rev() const;
	double get_lateral_g() const;
	double get_longitudinal_g() const;
	double get_frame_warp() const;

	// Simulated seconds per wall-clock second. Telemetry only — see the header
	// note. Never hashed, never fed back.
	double get_time_ratio() const;

	// --- the escape hatch -----------------------------------------------------

	// Direct access to the solver, for a probe that needs to reach past the
	// properties above. Not bound to GDScript.
	kart::core::KartVehicle &vehicle() { return vehicle_; }
	const kart::core::KartVehicle &vehicle() const { return vehicle_; }

private:
	// Fill `GroundQuery` for every corner from four raycasts, applying the lift and
	// the latch, in this frame's world transform.
	void query_ground(kart::core::GroundQuery r_contacts[kart::core::CORNER_COUNT]);

	// This tick's input: what was pushed if it is current, otherwise the
	// `input_driver` Callable, otherwise neutral. Consumes the latched edges.
	kart::core::DriverInput gather_input();

	// This body's pose and motion, in `vehicle_state.h`'s vocabulary.
	kart::core::BodyState read_body_state() const;

	// Find the voice named by `engine_voice_player` and hold its stream. Called from
	// `_ready`, so a scene that reparents its audio after that gets what it asks for
	// only by setting the path again — which is deliberate, because resolving a
	// NodePath every tick to serve a pointer that never changes is 120 lookups a
	// second for nothing.
	void resolve_engine_voice();

	// The same for the two noise layers. One function, because the only thing that
	// differs is which path and which reference, and two near-identical copies of
	// the warning text is how one of them ends up naming the wrong property.
	void resolve_noise_voice(const godot::NodePath &p_path, godot::Ref<NoiseVoiceStream> &r_stream,
			const char *p_property);

	// Map this tick's `VehicleTelemetry` onto `EngineAudioInput` and publish it.
	// The only place that mapping exists. See the definition for why `load` is not
	// `throttle`.
	void publish_engine_audio();

	// Slip angle at which the scrub layer is driven flat out, radians.
	//
	// **A tunable, and it has to be, because the thing it feeds is unmeasured.**
	// `kz_audio::SCRUB_SPECTRUM_MEASURED` is false: §12 specifies scrub as filtered
	// noise driven by slip and no recording in the corpus isolates a tire from the
	// engine running over the top of it. So this number cannot be sourced and is not
	// pretending to be.
	//
	// 0.20 rad, about 11.5 degrees, chosen from the tire model rather than from the
	// sound: it is past where `tire.h`'s lateral curve peaks, so the noise is at full
	// drive by the time the tire is genuinely sliding rather than merely working.
	static constexpr double SCRUB_REFERENCE_SLIP_RAD = 0.20;

	kart::core::KartVehicle vehicle_;

	godot::NodePath engine_voice_path_;
	godot::Ref<EngineVoiceStream> engine_voice_;
	godot::NodePath scrub_voice_path_;
	godot::Ref<NoiseVoiceStream> scrub_voice_;
	godot::NodePath wind_voice_path_;
	godot::Ref<NoiseVoiceStream> wind_voice_;
	godot::NodePath shift_voice_path_;
	godot::Ref<NoiseVoiceStream> shift_voice_;
	godot::NodePath roll_voice_path_;
	godot::Ref<NoiseVoiceStream> roll_voice_;

	// What was served to the solver last tick, per corner: the latch's storage and
	// the debug-draw getters' source.
	struct CornerContact {
		bool grounded = false;
		bool latched = false;
		double distance = 0.0;
		godot::Vector3 point;
		godot::Vector3 normal;
		double surface_grip = 1.0;
		int surface = 0;
		bool valid = false; // has ever hit, so the latch has something to serve
	};
	CornerContact contact_[kart::core::CORNER_COUNT];

	godot::Callable input_driver_;
	bool shift_up_pending_ = false;
	bool shift_down_pending_ = false;

	// What a driver node last handed over, and the tick it stamped it with.
	//
	// `NO_TICK` rather than 0, because 0 is a real physics frame — the first one —
	// and a sentinel that collides with a legal value is a freshness check that
	// passes once for free at exactly the moment a scene is starting up.
	static constexpr uint64_t NO_TICK = UINT64_MAX;
	kart::core::DriverInput pushed_input_;
	uint64_t pushed_tick_ = NO_TICK;
	bool warned_stale_input_ = false;
	uint64_t stale_input_ticks_ = 0;

	// The last input actually handed to the solver, for the HUD getters above.
	kart::core::DriverInput last_input_;

	godot::Transform3D spawn_;

	// The fixed solver step, seconds. Read once from
	// `physics/common/physics_ticks_per_second` in `_ready`, never from
	// `_physics_process`'s `delta`. See the header note on why.
	double step_dt_ = 1.0 / 120.0;

	// Wall-clock bookkeeping for `time_ratio`. Telemetry only.
	//
	// Starts at `TIME_RATIO_UNMEASURED`, not at 1.0. Before the first window
	// closes this node has not measured anything, and 1.0 is the specific value
	// that means "the simulation is keeping up" — publishing it unmeasured is a
	// plausible lie in the one read-out that exists to catch a defect nothing else
	// in this project can see. `vehicle_state.h` made the same change to the
	// struct's own default for the same reason.
	uint64_t wall_start_usec_ = 0;
	uint64_t tick_count_ = 0;
	double time_ratio_ = kart::core::TIME_RATIO_UNMEASURED;
};

} // namespace kartgame

#endif // KARTGAME_VEHICLE_KART_BODY_H
