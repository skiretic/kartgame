#ifndef KARTGAME_VEHICLE_KART_BODY_H
#define KARTGAME_VEHICLE_KART_BODY_H

#include "core/vehicle.h"
#include "core/vehicle_state.h"

#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

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

	// A `Callable` returning one tick of driver intent, or an invalid Callable to
	// read the input map instead.
	//
	// This is M3a's shape, kept deliberately: `tools/verify/drive_probe.gd`'s
	// scenarios are open-loop functions of the tick number, and a "set the axes and
	// hold them" API would make every scenario push values across the boundary
	// itself for no gain. `tools/shots/shoot.gd`'s constant-throttle still is the
	// same mechanism with a lambda that ignores the tick.
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
	void set_input_driver(const godot::Callable &p_driver);
	godot::Callable get_input_driver() const;

	// Stop reading the input map without detaching the body. The free camera turns
	// this off so the kart keeps simulating while WASD flies the camera; it has no
	// effect when `input_driver` is valid, because that path never touches `Input`.
	void set_process_input_enabled(bool p_enabled);
	bool is_process_input_enabled() const;

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

	// The front wheels' actual steer angle at full input, radians — `steering.h`
	// owns it and the HUD used to read a GDScript constant for it.
	double get_steer_lock() const;

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

	// The `input_driver` Callable, or the input map. Consumes the latched edges.
	kart::core::DriverInput gather_input();

	// This body's pose and motion, in `vehicle_state.h`'s vocabulary.
	kart::core::BodyState read_body_state() const;

	kart::core::KartVehicle vehicle_;

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
	bool process_input_enabled_ = true;
	bool shift_up_pending_ = false;
	bool shift_down_pending_ = false;

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
