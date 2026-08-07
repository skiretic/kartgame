#include "vehicle/kart_body.h"

#include "core/chassis.h"
#include "core/steering.h"
#include "core/surface.h"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

// The two type conversions this file needs, and the only place either happens.
//
// `src/core/` cannot see `Vector3` (ADR-0017) and Godot cannot see `Vec3`, so
// every value crossing the boundary passes through one of these two functions.
// They are componentwise and they do not change frames: the chassis frame and
// this node's local frame are the same frame — see the header comment — so a
// conversion that also rotated something would be a second, invisible mapping.
inline Vector3 to_godot(const Vec3 &value) {
	return Vector3(static_cast<real_t>(value.x), static_cast<real_t>(value.y),
			static_cast<real_t>(value.z));
}

inline Vec3 to_core(const Vector3 &value) {
	return Vec3(static_cast<double>(value.x), static_cast<double>(value.y),
			static_cast<double>(value.z));
}

// Corner names, in `CORNER_COUNT` order. Used by `wheel_report` and by the
// `_ready` diagnostics, so there is one spelling of them.
constexpr const char *CORNER_NAME[CORNER_COUNT] = { "FL", "FR", "RL", "RR" };

// Every `StringName` in this file is a **function-local static**, never a
// namespace-scope one, and that is not a style preference.
//
// A namespace-scope `StringName` is constructed by the dynamic loader's static
// initializers, which run inside `dlopen` — before `kartgame_library_init` has
// bound godot-cpp's interface pointers. Constructing one there dereferences a
// null function pointer and Godot dies with SIGSEGV in `dyld4::callInitializer`
// before a single line of this class has run, which is a crash with no Godot
// frames in it at all and reads like a corrupt build.
//
// Function-local statics are initialized on first call instead, which is after
// the interface exists, and the initialization is thread-safe by the standard.
// The lookup cost is one guard variable check per call.

inline double clamp01(double value) {
	return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

inline double clamp_signed_one(double value) {
	return value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value);
}

} // namespace

// The wheelbase sign, checked at compile time as well as in `_ready`.
//
// `chassis.h` puts +Z rearward and Godot's -Z is forward, so the two frames
// agree — and the whole of this file's "no conversion happens anywhere" claim
// rests on it. A `static_assert` cannot be argued with by a running scene, and
// `_ready` re-checks the same relation through the solver's own ray origins,
// which is the form that would catch a `ray_origin()` edited the other way
// round.
static_assert(kz::REAR_AXLE_Z > kz::FRONT_AXLE_Z,
		"chassis.h's +Z must be rearward: the rear axle sits at a larger Z than the front. "
		"If this ever flips, the kart drives backwards and nothing else in this file changes.");

KartBody::KartBody() {
	// Built here rather than in `_ready` so that `vehicle()`, `mass_properties()`
	// and the tuning getters answer correctly before the node enters the tree.
	// `configure()` is pure arithmetic over `src/core/` constants — it touches no
	// engine state and is safe on the dummy instance ClassDB constructs during
	// registration.
	vehicle_.configure();
}

void KartBody::_bind_methods() {
	// --- driver input ---------------------------------------------------------
	ClassDB::bind_method(D_METHOD("set_input_driver", "driver"), &KartBody::set_input_driver);
	ClassDB::bind_method(D_METHOD("get_input_driver"), &KartBody::get_input_driver);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "input_driver"), "set_input_driver",
			"get_input_driver");

	ClassDB::bind_method(D_METHOD("get_stale_input_ticks"), &KartBody::get_stale_input_ticks);

	ClassDB::bind_method(D_METHOD("request_shift_up"), &KartBody::request_shift_up);
	ClassDB::bind_method(D_METHOD("request_shift_down"), &KartBody::request_shift_down);

	// --- configuration --------------------------------------------------------
	ClassDB::bind_method(D_METHOD("set_auto_clutch", "enabled"), &KartBody::set_auto_clutch);
	ClassDB::bind_method(D_METHOD("is_auto_clutch"), &KartBody::is_auto_clutch);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_clutch"), "set_auto_clutch", "is_auto_clutch");

	ClassDB::bind_method(D_METHOD("set_auto_shift", "enabled"), &KartBody::set_auto_shift);
	ClassDB::bind_method(D_METHOD("is_auto_shift"), &KartBody::is_auto_shift);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_shift"), "set_auto_shift", "is_auto_shift");

	ClassDB::bind_method(D_METHOD("set_engine_voice_player", "path"),
			&KartBody::set_engine_voice_player);
	ClassDB::bind_method(D_METHOD("get_engine_voice_player"), &KartBody::get_engine_voice_player);
	ClassDB::bind_method(D_METHOD("set_scrub_voice_player", "path"),
			&KartBody::set_scrub_voice_player);
	ClassDB::bind_method(D_METHOD("get_scrub_voice_player"), &KartBody::get_scrub_voice_player);
	ClassDB::bind_method(D_METHOD("set_wind_voice_player", "path"),
			&KartBody::set_wind_voice_player);
	ClassDB::bind_method(D_METHOD("get_wind_voice_player"), &KartBody::get_wind_voice_player);
	ClassDB::bind_method(D_METHOD("set_shift_voice_player", "path"),
			&KartBody::set_shift_voice_player);
	ClassDB::bind_method(D_METHOD("get_shift_voice_player"), &KartBody::get_shift_voice_player);
	ClassDB::bind_method(D_METHOD("set_roll_voice_player", "path"),
			&KartBody::set_roll_voice_player);
	ClassDB::bind_method(D_METHOD("get_roll_voice_player"), &KartBody::get_roll_voice_player);
	ClassDB::bind_method(D_METHOD("engine_mount_position"), &KartBody::engine_mount_position);
	ClassDB::bind_method(D_METHOD("rear_axle_position"), &KartBody::rear_axle_position);
	ClassDB::bind_method(D_METHOD("driver_head_position"), &KartBody::driver_head_position);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "engine_voice_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						"AudioStreamPlayer3D,AudioStreamPlayer"),
			"set_engine_voice_player", "get_engine_voice_player");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "scrub_voice_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						"AudioStreamPlayer3D,AudioStreamPlayer"),
			"set_scrub_voice_player", "get_scrub_voice_player");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "wind_voice_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						"AudioStreamPlayer3D,AudioStreamPlayer"),
			"set_wind_voice_player", "get_wind_voice_player");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "shift_voice_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						"AudioStreamPlayer3D,AudioStreamPlayer"),
			"set_shift_voice_player", "get_shift_voice_player");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "roll_voice_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						"AudioStreamPlayer3D,AudioStreamPlayer"),
			"set_roll_voice_player", "get_roll_voice_player");

	ClassDB::bind_method(D_METHOD("set_jacking_enabled", "enabled"),
			&KartBody::set_jacking_enabled);
	ClassDB::bind_method(D_METHOD("is_jacking_enabled"), &KartBody::is_jacking_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "jacking_enabled"), "set_jacking_enabled",
			"is_jacking_enabled");

	ClassDB::bind_method(D_METHOD("set_drag_area", "value"), &KartBody::set_drag_area);
	ClassDB::bind_method(D_METHOD("get_drag_area"), &KartBody::get_drag_area);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag_area"), "set_drag_area", "get_drag_area");

	ClassDB::bind_method(D_METHOD("set_rolling_resistance", "value"),
			&KartBody::set_rolling_resistance);
	ClassDB::bind_method(D_METHOD("get_rolling_resistance"), &KartBody::get_rolling_resistance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rolling_resistance"), "set_rolling_resistance",
			"get_rolling_resistance");

	ClassDB::bind_method(D_METHOD("set_brake_torque_front", "value"),
			&KartBody::set_brake_torque_front);
	ClassDB::bind_method(D_METHOD("get_brake_torque_front"), &KartBody::get_brake_torque_front);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brake_torque_front"), "set_brake_torque_front",
			"get_brake_torque_front");

	ClassDB::bind_method(D_METHOD("set_brake_torque_rear", "value"),
			&KartBody::set_brake_torque_rear);
	ClassDB::bind_method(D_METHOD("get_brake_torque_rear"), &KartBody::get_brake_torque_rear);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brake_torque_rear"), "set_brake_torque_rear",
			"get_brake_torque_rear");

	ClassDB::bind_method(D_METHOD("set_steer_rate", "value"), &KartBody::set_steer_rate);
	ClassDB::bind_method(D_METHOD("get_steer_rate"), &KartBody::get_steer_rate);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steer_rate"), "set_steer_rate", "get_steer_rate");

	ClassDB::bind_method(D_METHOD("set_frame_torsion_nm_per_deg", "value"),
			&KartBody::set_frame_torsion_nm_per_deg);
	ClassDB::bind_method(D_METHOD("get_frame_torsion_nm_per_deg"),
			&KartBody::get_frame_torsion_nm_per_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frame_torsion_nm_per_deg"),
			"set_frame_torsion_nm_per_deg", "get_frame_torsion_nm_per_deg");

	ClassDB::bind_method(D_METHOD("set_peak_friction", "value"), &KartBody::set_peak_friction);
	ClassDB::bind_method(D_METHOD("get_peak_friction"), &KartBody::get_peak_friction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "peak_friction"), "set_peak_friction",
			"get_peak_friction");

	ClassDB::bind_method(D_METHOD("set_max_lock", "radians"), &KartBody::set_max_lock);
	ClassDB::bind_method(D_METHOD("get_max_lock"), &KartBody::get_max_lock);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_lock"), "set_max_lock", "get_max_lock");

	// --- lifecycle ------------------------------------------------------------
	ClassDB::bind_method(D_METHOD("set_spawn", "transform"), &KartBody::set_spawn);
	ClassDB::bind_method(D_METHOD("get_spawn"), &KartBody::get_spawn);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "spawn"), "set_spawn", "get_spawn");

	ClassDB::bind_method(D_METHOD("respawn"), &KartBody::respawn);
	ClassDB::bind_method(D_METHOD("engage", "gear", "road_speed_ms"), &KartBody::engage);

	// --- read-out -------------------------------------------------------------
	ClassDB::bind_method(D_METHOD("telemetry"), &KartBody::telemetry);
	ClassDB::bind_method(D_METHOD("wheel_report"), &KartBody::wheel_report);

	ClassDB::bind_method(D_METHOD("get_speed_ms"), &KartBody::get_speed_ms);
	ClassDB::bind_method(D_METHOD("get_throttle_input"), &KartBody::get_throttle_input);
	ClassDB::bind_method(D_METHOD("get_brake_input"), &KartBody::get_brake_input);
	ClassDB::bind_method(D_METHOD("get_steer_input"), &KartBody::get_steer_input);
	ClassDB::bind_method(D_METHOD("get_wheels_on_ground"), &KartBody::get_wheels_on_ground);
	ClassDB::bind_method(D_METHOD("get_steer_lock"), &KartBody::get_steer_lock);

	// Read-only properties, under M3a's names, so the HUD, the chase camera and
	// `drive_probe.gd` reach them the way they always have. An empty setter is
	// what makes a bound property read-only.
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_ms"), "", "get_speed_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "throttle_input"), "", "get_throttle_input");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brake_input"), "", "get_brake_input");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steer_input"), "", "get_steer_input");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "wheels_on_ground"), "", "get_wheels_on_ground");

	ClassDB::bind_method(D_METHOD("get_soft_cut_rpm"), &KartBody::get_soft_cut_rpm);
	ClassDB::bind_method(D_METHOD("get_hard_cut_rpm"), &KartBody::get_hard_cut_rpm);
	ClassDB::bind_method(D_METHOD("is_on_limiter"), &KartBody::is_on_limiter);
	ClassDB::bind_method(D_METHOD("get_rollover_threshold_g", "turning_left"),
			&KartBody::get_rollover_threshold_g);

	ClassDB::bind_method(D_METHOD("get_engine_rpm"), &KartBody::get_engine_rpm);
	ClassDB::bind_method(D_METHOD("get_engine_torque"), &KartBody::get_engine_torque);
	ClassDB::bind_method(D_METHOD("get_axle_torque"), &KartBody::get_axle_torque);
	ClassDB::bind_method(D_METHOD("get_axle_speed"), &KartBody::get_axle_speed);
	ClassDB::bind_method(D_METHOD("get_gear"), &KartBody::get_gear);
	ClassDB::bind_method(D_METHOD("get_clutch_slip"), &KartBody::get_clutch_slip);
	ClassDB::bind_method(D_METHOD("is_shifting"), &KartBody::is_shifting);
	ClassDB::bind_method(D_METHOD("is_over_rev"), &KartBody::is_over_rev);
	ClassDB::bind_method(D_METHOD("get_lateral_g"), &KartBody::get_lateral_g);
	ClassDB::bind_method(D_METHOD("get_longitudinal_g"), &KartBody::get_longitudinal_g);
	ClassDB::bind_method(D_METHOD("get_frame_warp"), &KartBody::get_frame_warp);
	ClassDB::bind_method(D_METHOD("get_time_ratio"), &KartBody::get_time_ratio);
}

// --- lifecycle ----------------------------------------------------------------

void KartBody::_ready() {
// The group `scripts/game/telemetry_panel.gd` looks in. Non-persistent: this
	// node is built in code and the group is a runtime fact, not something a
	// scene file should carry.
	static const StringName TELEMETRY_GROUP("telemetry_source");
	add_to_group(TELEMETRY_GROUP);

	// Godot enables physics processing for an overridden `_physics_process`
	// automatically, but only via the same `NOTIFICATION_READY` this override is
	// part of. Asked for explicitly, because a kart that silently never ticks is
	// indistinguishable from a kart whose solver produces nothing.
	set_physics_process(true);

	// --- convention 8: the frames line up ------------------------------------
	//
	// The `static_assert` above checks `chassis.h`. This checks the solver's own
	// ray geometry, which is the thing this file actually casts along: if
	// `ray_origin()` were ever built the other way round the assert would still
	// pass and every wheel would be on the wrong end of the kart.
	if (vehicle_.ray_origin(CORNER_RL).z <= vehicle_.ray_origin(CORNER_FL).z) {
		UtilityFunctions::push_error(
				"KartBody: the solver's rear ray origin is not rearward of its front one. "
				"The chassis frame and Godot's disagree about which way +Z points, and the "
				"kart will drive backwards. See kart_body.h, 'The frames line up'.");
	}

	// --- what this node does not own -----------------------------------------
	//
	// A `RigidBody3D` with no shape falls through the world in silence. Said
	// loudly instead.
	bool has_shape = false;
	for (int index = 0; index < get_child_count(); ++index) {
		if (Object::cast_to<CollisionShape3D>(get_child(index)) != nullptr) {
			has_shape = true;
			break;
		}
	}
	if (!has_shape) {
		UtilityFunctions::push_error(
				"KartBody '", get_name(),
				"' has no CollisionShape3D child. A RigidBody3D with no shape falls through "
				"the world without reporting anything. The shape is the scene's to add - see "
				"kart_body.h, 'What this node does NOT own'.");
	}

	// --- convention 4 of the mass properties: no literals ---------------------
	const MassProperties &properties = vehicle_.mass_properties();
	set_mass(static_cast<real_t>(properties.mass));
	set_center_of_mass_mode(CENTER_OF_MASS_MODE_CUSTOM);
	set_center_of_mass(to_godot(properties.center_of_mass));
	// Godot's `inertia` is a `Vector3`: the diagonal only, with the body's local
	// axes assumed to be its principal axes. They are not — the kart's mass is
	// not symmetric about its centerline — and `chassis.h` measures the largest
	// off-diagonal term at 3.6% of the largest diagonal one. An approximation of
	// known size, stated here because the engine gives no way to do better.
	set_inertia(Vector3(static_cast<real_t>(properties.inertia.xx),
			static_cast<real_t>(properties.inertia.yy),
			static_cast<real_t>(properties.inertia.zz)));

	// --- convention 2: Jolt's own friction is off ----------------------------
	//
	// The tire model owns every tangential newton. The combine rule is measured
	// to be `min(a, b)`, so zero here is sufficient whatever the ground says.
	Ref<PhysicsMaterial> material;
	material.instantiate();
	material->set_friction(0.0);
	set_physics_material_override(material);

	// --- convention 3: the project's default damping is not zero -------------
	//
	// 0.1 linear and 0.1 angular arrive from the project settings — 1 m/s^2 at
	// 10 m/s, about a quarter of this kart's acceleration there. Drag is the
	// solver's own `drag_area` term and nothing else.
	set_linear_damp_mode(DAMP_MODE_REPLACE);
	set_linear_damp(0.0);
	set_angular_damp_mode(DAMP_MODE_REPLACE);
	set_angular_damp(0.0);
	// A sleeping body is a body whose contact is not being solved, and a kart
	// waiting on a grid is exactly the case that would trip the sleep threshold.
	set_can_sleep(false);

	// --- the fixed step ------------------------------------------------------
	//
	// Read once from the project setting, never from `_physics_process`'s
	// `delta`: a solver fed a float that wobbles in its last bits is not
	// reproducible. `Engine.physics_ticks_per_second` can be moved at run time by
	// a probe, and a solver stepped at 1/120 while Godot ticks at 1/240 would run
	// at half speed without saying so, so the two are compared here.
	const double ticks_per_second = static_cast<double>(ProjectSettings::get_singleton()->get_setting(
			"physics/common/physics_ticks_per_second", 120));
	step_dt_ = ticks_per_second > 0.0 ? 1.0 / ticks_per_second : 1.0 / 120.0;
	// Qualified: `src/core/engine.h` also declares an `Engine`, and it is the KZ
	// motor. Two very different things with one name, and the compiler says so.
	const int running_hz = godot::Engine::get_singleton()->get_physics_ticks_per_second();
	if (static_cast<double>(running_hz) != ticks_per_second) {
		UtilityFunctions::push_warning(
				"KartBody: physics_ticks_per_second is ", running_hz,
				" at run time but ", ticks_per_second,
				" in the project settings. The solver is stepped with the project value, so "
				"simulated time will run at ", ticks_per_second / static_cast<double>(running_hz),
				" of wall-clock.");
	}

	// The gravity the solver *predicts* with. It never applies it — Godot does —
	// but a prediction that used a different number from the engine's would
	// disagree with the body it is predicting, and `vehicle.h` says the
	// suspension is the axis most sensitive to exactly that.
	const double gravity_magnitude = static_cast<double>(
			ProjectSettings::get_singleton()->get_setting("physics/3d/default_gravity", 9.8));
	const Vector3 gravity_direction = ProjectSettings::get_singleton()->get_setting(
			"physics/3d/default_gravity_vector", Vector3(0.0, -1.0, 0.0));
	vehicle_.gravity = to_core(gravity_direction.normalized() * static_cast<real_t>(gravity_magnitude));

	// Where `respawn()` goes, unless a caller has already said. Compared against
	// the identity rather than carrying a "was it set" flag, because an identity
	// spawn is not a pose anything meaningful is ever placed at.
	if (spawn_ == Transform3D()) {
		spawn_ = get_global_transform();
	}

	resolve_engine_voice();
	resolve_noise_voice(scrub_voice_path_, scrub_voice_, "scrub_voice_player");
	resolve_noise_voice(wind_voice_path_, wind_voice_, "wind_voice_player");
	resolve_noise_voice(shift_voice_path_, shift_voice_, "shift_voice_player");
	resolve_noise_voice(roll_voice_path_, roll_voice_, "roll_voice_player");

	// The shift layer needs `Gearbox::shift_time`, and the solver owns it.
	//
	// **Pushed here rather than compiled into the synth**, which is the join this
	// project keeps failing to make: `shift_audio.h` carries a default so a synth
	// nobody tells is right rather than zero, and without this line that default
	// would be the only value it ever had -- a knob moved in `gearbox.h` and a
	// gearshift that kept the old duration, silently. Same family as `settings.cfg`
	// storing `assist_auto_shift` that no scene ever loaded.
	if (shift_voice_.is_valid()) {
		shift_voice_->set_shift_time(vehicle_.drivetrain.gearbox.shift_time);
	}

	wall_start_usec_ = Time::get_singleton()->get_ticks_usec();
	tick_count_ = 0;
	// Not 1.0. Nothing has been measured yet, and 1.0 is precisely the value that
	// asserts the simulation is keeping up.
	time_ratio_ = kart::core::TIME_RATIO_UNMEASURED;
}

void KartBody::_physics_process(double p_delta) {
	// `p_delta` is deliberately unused. See the header note: the solver is
	// stepped with `step_dt_`, a constant, and the difference between the two is
	// the whole of `time_ratio` below.
	(void)p_delta;

	const DriverInput input = gather_input();

	GroundQuery contacts[CORNER_COUNT];
	query_ground(contacts);

	const BodyState body = read_body_state();
	const VehicleForces forces = vehicle_.step(body, input, contacts, step_dt_);

	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		// **Untouched.** `application_point` is already a world offset from the
		// body ORIGIN, which is `apply_force`'s convention. Subtracting the
		// center of mass here adds a second torque `(origin - com) x F`, and
		// because this kart's origin and its contact patches are both at ground
		// level that doubles every pitch and roll moment. ADR-0033, and
		// `tools/verify/kart_body_probe.gd` measures it.
		apply_force(to_godot(forces.force[corner]), to_godot(forces.application_point[corner]));
	}
	apply_central_force(to_godot(forces.central_force));
	apply_torque(to_godot(forces.central_torque));

	last_input_ = input;

	publish_engine_audio();

	// --- time ratio, telemetry and nothing else ------------------------------
	//
	// **The rule: this number is written to telemetry and read by nothing else.**
	// It never reaches `DriverInput`, never scales a force, and is never hashed.
	// It exists because `max_physics_steps_per_frame` clamps and does not bank —
	// under a frame-rate collapse Godot runs its eight ticks and stops,
	// simulation time falls behind, and a replay that counts ticks cannot see it.
	// Measuring it is the one place this file reads a wall clock, which
	// `ARCHITECTURE.md` §8 otherwise forbids the simulation to do.
	++tick_count_;
	const uint64_t elapsed_usec = Time::get_singleton()->get_ticks_usec() - wall_start_usec_;
	if (elapsed_usec > 0) {
		time_ratio_ = static_cast<double>(tick_count_) * step_dt_ /
				(static_cast<double>(elapsed_usec) * 1.0e-6);
	}
}

// --- the ground ---------------------------------------------------------------

void KartBody::query_ground(GroundQuery r_contacts[CORNER_COUNT]) {
	Ref<World3D> world = get_world_3d();
	PhysicsDirectSpaceState3D *space =
			world.is_valid() ? world->get_direct_space_state() : nullptr;
	if (space == nullptr) {
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			r_contacts[corner] = GroundQuery();
			contact_[corner].grounded = false;
			contact_[corner].latched = false;
		}
		return;
	}

	const Transform3D transform = get_global_transform();
	// Chassis down, **not** world down. #124 is exactly this bug in the debug
	// draw: a ray along world down is wrong the moment the kart rolls, and a
	// rolled kart is the case the whole suspension model exists for.
	const Vector3 down = -transform.basis.get_column(1).normalized();

	// One query object for the whole tick, reused across the four corners.
	// `PhysicsRayQueryParameters3D` is a `RefCounted`, so building four of them
	// per tick would be 480 allocations a second to ask four questions.
	Ref<PhysicsRayQueryParameters3D> query;
	query.instantiate();
	// Convention 5: a ray cast from outside a collider hits that collider's own
	// body, 240 times out of 240. Excluding this body costs 1.23 us against
	// 1.26 us over 20,000 rays, which is inside the noise.
	TypedArray<RID> exclude;
	exclude.push_back(get_rid());
	query->set_exclude(exclude);

	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		CornerContact &stored = contact_[corner];

		// Convention 4, first half: start the cast `RAY_START_LIFT` above the
		// wheel center so the hub has to be that far inside a surface before the
		// query origin is, and subtract the lift straight back off. Nothing
		// downstream sees it — `ray_origin` and `ray_length` still define the
		// geometry.
		const Vector3 from = to_global(
				to_godot(vehicle_.ray_origin(corner)) + Vector3(0.0, KartBody::RAY_START_LIFT, 0.0));
		const double length = KartBody::RAY_START_LIFT + vehicle_.ray_length(corner);

		query->set_from(from);
		query->set_to(from + down * static_cast<real_t>(length));
		const Dictionary result = space->intersect_ray(query);

		if (!result.is_empty()) {
			const Vector3 point = result["position"];
			const Vector3 normal = result["normal"];
			// Along the ray rather than as a distance from the origin. For a
			// straight cast the two agree; the projection is what stays correct
			// if a future caller ever offsets the ray sideways.
			double distance = static_cast<double>((point - from).dot(down)) -
					KartBody::RAY_START_LIFT;
			// A hit at less than the lift means the wheel center is already
			// under the surface. Clamped rather than allowed negative: zero is
			// full compression, which the bump stop in `suspension.h` already
			// handles, and a negative ray length is not a length.
			if (distance < 0.0) {
				distance = 0.0;
			}

			// The metadata key a collider carries to say what it is made of.
			// Absent means asphalt, which is `SURFACE_ASPHALT == 0` and therefore
			// also what a default-constructed `GroundQuery` says — `surface.h`
			// chose zero for asphalt for exactly this reason.
			static const StringName META_SURFACE_TYPE("surface_type");
			int surface = SURFACE_ASPHALT;
			Object *collider = result["collider"];
			if (collider != nullptr && collider->has_meta(META_SURFACE_TYPE)) {
				surface = static_cast<int>(collider->get_meta(META_SURFACE_TYPE, SURFACE_ASPHALT));
			}

			stored.grounded = true;
			stored.latched = false;
			stored.distance = distance;
			stored.point = point;
			stored.normal = normal;
			stored.surface = surface;
			stored.surface_grip = grip_for(surface);
			stored.valid = true;
		} else {
			// Convention 4, second half. A miss is two different things and they
			// have to be told apart:
			//
			//   * the wheel is **buried** — the query origin has ended up below
			//     the surface, which is exactly when the corner is most loaded,
			//     and `hit_from_inside` is not a depth query so there is nothing
			//     to ask; or
			//   * the wheel is **airborne**, or the ground has genuinely gone
			//     away underneath it.
			//
			// The discriminator is geometric and needs no extra state: the last
			// valid contact defines a plane, and the query origin is either below
			// it (buried, so latch) or above it (there is nothing there, so
			// report the miss honestly). Latching unconditionally would float the
			// kart over a hole forever.
			const bool below_last_plane =
					stored.valid && (from - stored.point).dot(stored.normal) < 0.0;
			if (below_last_plane) {
				stored.grounded = true;
				stored.latched = true;
				// `distance`, `point`, `normal`, `surface` and `surface_grip` are
				// deliberately left at their latched values.
			} else {
				stored.grounded = false;
				stored.latched = false;
			}
		}

		GroundQuery &out = r_contacts[corner];
		out.hit = stored.grounded;
		out.distance = stored.distance;
		out.point = to_core(stored.point);
		out.normal = to_core(stored.normal);
		out.surface = stored.surface;
		out.surface_grip = stored.surface_grip;
	}
}

DriverInput KartBody::gather_input() {
	// The `input_driver` Dictionary's keys, read with defaults so that M3a's
	// three keys still work unchanged. `kart_body.h` documents the shape.
	static const StringName KEY_THROTTLE("throttle");
	static const StringName KEY_BRAKE("brake");
	static const StringName KEY_STEER("steer");
	static const StringName KEY_CLUTCH("clutch");
	static const StringName KEY_SHIFT_UP("shift_up");
	static const StringName KEY_SHIFT_DOWN("shift_down");

	DriverInput input;

	// **The Callable wins when there is one, and that ordering is a decision.**
	//
	// An `input_driver` is only ever assigned deliberately, by a probe or by a
	// still command; a `PlayerDriver` is scene furniture that every driveable scene
	// builds. Ordering it the other way round is not a style preference — it was
	// measured. With the pushed input taking precedence, `drive.sh` ran all six
	// scenarios to an identical state hash of `f2159f215039a647` and 0.2 m of
	// travel, because the driver node was pushing the neutral input of a keyboard
	// nobody was touching and the scenario's Callable was never consulted. Six
	// scenarios agreeing perfectly is what a determinism gate looks like when it
	// has stopped measuring anything.
	//
	// So a scripted producer is an explicit override of whatever the scene wired,
	// and the driver node goes on pushing into a value that is ignored.
	if (input_driver_.is_valid()) {
		const Variant answer = input_driver_.call();
		if (answer.get_type() == Variant::DICTIONARY) {
			const Dictionary values = answer;
			input.throttle = clamp01(static_cast<double>(values.get(KEY_THROTTLE, 0.0)));
			input.brake = clamp01(static_cast<double>(values.get(KEY_BRAKE, 0.0)));
			// Not rate limited here. `steering.h` owns the rate limit and the
			// solver applies it every substep, so a scenario and a keyboard reach
			// the front wheels through the same one — which is what makes a
			// scripted run and a driven one the same experiment.
			input.steer = clamp_signed_one(static_cast<double>(values.get(KEY_STEER, 0.0)));
			input.clutch = clamp01(static_cast<double>(values.get(KEY_CLUTCH, 0.0)));
			input.shift_up = static_cast<bool>(values.get(KEY_SHIFT_UP, false));
			input.shift_down = static_cast<bool>(values.get(KEY_SHIFT_DOWN, false));
		}
	} else if (pushed_tick_ != NO_TICK) {
		// **Pushed input, and only if it is this tick's.** ADR-0040's freshness rule:
		// a producer that does not run, or that runs after this body in tree order,
		// would otherwise leave the solver consuming last tick's intent, and that
		// reads as a physics bug rather than as a wiring bug.
		//
		// `PlayerDriver` sets its physics priority ahead of this node's so tree order
		// cannot decide it. The check below is what makes that a fact rather than a
		// hope.
		const uint64_t tick = godot::Engine::get_singleton()->get_physics_frames();
		if (pushed_tick_ == tick) {
			input = pushed_input_;
		} else {
			// Neutral, not held-last. Holding the last input means a crashed producer
			// drives into a wall at full throttle, and it means a divergence between
			// two runs stays invisible in the state hash for as long as the throttle
			// happens to agree either way.
			++stale_input_ticks_;
			if (!warned_stale_input_) {
				warned_stale_input_ = true;
				WARN_PRINT(vformat("KartBody '%s': input was stamped tick %d and this is "
								   "tick %d, so this tick and any like it run on neutral "
								   "input. A driver node is disabled, gone, or running "
								   "after the body. ADR-0040.",
						get_name(), static_cast<int64_t>(pushed_tick_),
						static_cast<int64_t>(tick)));
			}
		}
	}
	// The remaining case — nothing pushed and no Callable — leaves every axis at
	// zero. That is a kart on track with no driver attached: it keeps simulating
	// and coasts, which is exactly what a free camera or a scene that has not
	// wired a `PlayerDriver` yet gets. It is silent on purpose, because "no
	// producer at all" is a scene setup a person can see, where "a producer that
	// went stale" is not.

	// A latched request survives whichever path ran, so a caller that asked
	// between two physics ticks cannot lose one. Consumed here, once.
	if (shift_up_pending_) {
		input.shift_up = true;
		shift_up_pending_ = false;
	}
	if (shift_down_pending_) {
		input.shift_down = true;
		shift_down_pending_ = false;
	}

	return input;
}

BodyState KartBody::read_body_state() const {
	const Transform3D transform = get_global_transform();

	BodyState state;
	state.origin = to_core(transform.origin);
	// World, already transformed. `RigidBody3D::center_of_mass` is in the body's
	// local frame, which — see the header — is the chassis frame.
	state.center_of_mass = to_core(to_global(get_center_of_mass()));
	state.basis_x = to_core(transform.basis.get_column(0));
	state.basis_y = to_core(transform.basis.get_column(1));
	state.basis_z = to_core(transform.basis.get_column(2));
	// Godot's `linear_velocity` is the velocity of the center of mass, which is
	// what `BodyState` asks for and what `velocity_at` differences against.
	state.linear_velocity = to_core(get_linear_velocity());
	state.angular_velocity = to_core(get_angular_velocity());
	return state;
}

// --- driver input --------------------------------------------------------------

void KartBody::set_input(const DriverInput &p_input, uint64_t p_tick) {
	pushed_input_ = p_input;
	pushed_tick_ = p_tick;
}

void KartBody::set_input_driver(const Callable &p_driver) {
	input_driver_ = p_driver;
}

Callable KartBody::get_input_driver() const {
	return input_driver_;
}

uint64_t KartBody::get_stale_input_ticks() const {
	return stale_input_ticks_;
}

void KartBody::request_shift_up() {
	shift_up_pending_ = true;
}

void KartBody::request_shift_down() {
	shift_down_pending_ = true;
}

// --- configuration -------------------------------------------------------------

void KartBody::set_auto_clutch(bool p_enabled) {
	vehicle_.drivetrain.assists.auto_clutch = p_enabled;
}

bool KartBody::is_auto_clutch() const {
	return vehicle_.drivetrain.assists.auto_clutch;
}

void KartBody::set_auto_shift(bool p_enabled) {
	vehicle_.drivetrain.assists.auto_shift = p_enabled;
}

bool KartBody::is_auto_shift() const {
	return vehicle_.drivetrain.assists.auto_shift;
}

void KartBody::set_engine_voice_player(const NodePath &p_path) {
	engine_voice_path_ = p_path;
	// Re-resolved immediately when the node is already in the tree, so that setting
	// the path from a script after `_ready` works rather than silently doing nothing
	// until the next scene load.
	if (is_inside_tree()) {
		resolve_engine_voice();
	}
}

NodePath KartBody::get_engine_voice_player() const {
	return engine_voice_path_;
}

void KartBody::set_scrub_voice_player(const NodePath &p_path) {
	scrub_voice_path_ = p_path;
	if (is_inside_tree()) {
		resolve_noise_voice(scrub_voice_path_, scrub_voice_, "scrub_voice_player");
	}
}

NodePath KartBody::get_scrub_voice_player() const {
	return scrub_voice_path_;
}

void KartBody::set_wind_voice_player(const NodePath &p_path) {
	wind_voice_path_ = p_path;
	if (is_inside_tree()) {
		resolve_noise_voice(wind_voice_path_, wind_voice_, "wind_voice_player");
	}
}

NodePath KartBody::get_wind_voice_player() const {
	return wind_voice_path_;
}

void KartBody::set_shift_voice_player(const NodePath &p_path) {
	shift_voice_path_ = p_path;
	if (is_inside_tree()) {
		resolve_noise_voice(shift_voice_path_, shift_voice_, "shift_voice_player");
		// Re-pushed on every resolve and not only at `_ready`, because a scene may
		// mount this player after the body has entered the tree -- `EngineVoiceRig`
		// does exactly that.
		if (shift_voice_.is_valid()) {
			shift_voice_->set_shift_time(vehicle_.drivetrain.gearbox.shift_time);
		}
	}
}

NodePath KartBody::get_shift_voice_player() const {
	return shift_voice_path_;
}

void KartBody::set_roll_voice_player(const NodePath &p_path) {
	roll_voice_path_ = p_path;
	if (is_inside_tree()) {
		resolve_noise_voice(roll_voice_path_, roll_voice_, "roll_voice_player");
	}
}

NodePath KartBody::get_roll_voice_player() const {
	return roll_voice_path_;
}

// One lump's position, by name.
//
// Looked up by name rather than by index. The table is edited — the lead ballast
// was added to it after the fact, and issue #107's calibration moved four driver
// rows — and an index would silently start pointing at the exhaust.
//
// A missing row is reported and not silently (0,0,0), which is a legal-looking
// position at the kart's origin — on the floor, on the centerline — and for the
// engine would sound like the motor had been moved into the driver's lap. The
// caller passes what it is for so the message says which emitter lost its home.
static Vector3 lump_position(const char *p_name, const char *p_purpose) {
	for (int index = 0; index < kart::core::kz::KART_LUMP_COUNT; ++index) {
		const kart::core::MassLump &lump = kart::core::kz::KART_LUMPS[index];
		if (lump.name != nullptr && std::strcmp(lump.name, p_name) == 0) {
			return to_godot(lump.position);
		}
	}
	UtilityFunctions::push_error(
			String("KartBody: chassis.h's lump table has no row named '") + p_name +
			"'. " + p_purpose);
	return Vector3();
}

Vector3 KartBody::engine_mount_position() const {
	return lump_position("engine", "The engine note has nowhere to come from.");
}

// Where the scrub emitter goes: the middle of the rear axle, on the ground.
//
// From `chassis.h`'s own `REAR_AXLE_Z` rather than from a number in a scene, for
// the reason `engine_mount_position` gives — the lump table is edited and a scene
// holding its own copy of a dimension is a copy that stops matching the solver.
//
// **One emitter for four contact patches, and the position is the compromise that
// admits it.** `publish_engine_audio` takes the mean of the four corners' slip
// angles and says why; a single sound source for that mean belongs between the
// wheels rather than at one of them. At the rear rather than the middle because
// the rear is where a kart's slide starts and where a driver's attention is.
Vector3 KartBody::rear_axle_position() const {
	// Blender +Y forward maps to Godot -Z, and `chassis.h` already works in the
	// Godot convention: negative Z is forward, so the rear axle is the positive one.
	return Vector3(0.0f, 0.0f, static_cast<real_t>(kart::core::kz::REAR_AXLE_Z));
}

// Where the listener goes, and where a cockpit camera sits. Issue #160.
//
// The head lump's center, not an eye point. An eye offset would be a number with
// no source: `chassis.h` has a 260 mm helmet box and nothing inside it, and §5
// item 10 is explicit that a real dimension is pulled from a reference rather than
// reasoned out. The center of the helmet is wrong for a camera by a few
// centimeters and is right for a listener, which is the load-bearing use.
Vector3 KartBody::driver_head_position() const {
	return lump_position("driver head and helmet",
			"The listener and the cockpit camera have nowhere to sit.");
}

void KartBody::set_jacking_enabled(bool p_enabled) {
	vehicle_.jacking_enabled = p_enabled;
}

bool KartBody::is_jacking_enabled() const {
	return vehicle_.jacking_enabled;
}

void KartBody::set_drag_area(double p_value) {
	vehicle_.drag_area = p_value;
}

double KartBody::get_drag_area() const {
	return vehicle_.drag_area;
}

void KartBody::set_rolling_resistance(double p_value) {
	vehicle_.rolling_resistance = p_value;
}

double KartBody::get_rolling_resistance() const {
	return vehicle_.rolling_resistance;
}

void KartBody::set_brake_torque_front(double p_value) {
	vehicle_.brake_torque_front = p_value;
}

double KartBody::get_brake_torque_front() const {
	return vehicle_.brake_torque_front;
}

void KartBody::set_brake_torque_rear(double p_value) {
	vehicle_.brake_torque_rear = p_value;
}

double KartBody::get_brake_torque_rear() const {
	return vehicle_.brake_torque_rear;
}

void KartBody::set_steer_rate(double p_value) {
	vehicle_.steer_rate = p_value;
}

double KartBody::get_steer_rate() const {
	return vehicle_.steer_rate;
}

void KartBody::set_frame_torsion_nm_per_deg(double p_value) {
	vehicle_.frame_torsion_nm_per_deg = p_value;
}

double KartBody::get_frame_torsion_nm_per_deg() const {
	return vehicle_.frame_torsion_nm_per_deg;
}

void KartBody::set_peak_friction(double p_value) {
	// All four corners at once. #120's question — "what would this have to be for
	// §6.4's lateral band to be reachable" — is a question about the tire, not
	// about a corner, and a per-corner setter would invite an answer that is
	// really a balance change.
	//
	// Both curves, because `TireCurve::peak_friction` is per axis and a tire whose
	// lateral peak moved while its longitudinal one did not is not a grippier
	// tire, it is a differently shaped friction ellipse. `refresh_peaks()` is not
	// optional: `Tire` caches where each curve peaks, and `tire.h` states that a
	// caller mutating a coefficient must refresh or `slide` goes silently stale.
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		Tire &tire = vehicle_.tire(corner);
		tire.lateral.peak_friction = p_value;
		tire.longitudinal.peak_friction = p_value;
		tire.refresh_peaks();
	}
}

double KartBody::get_peak_friction() const {
	return vehicle_.tire(CORNER_FL).lateral.peak_friction;
}

void KartBody::set_max_lock(double p_radians) {
	// Straight through to the geometry: `SteeringGeometry` caches nothing, so
	// there is no counterpart to `Tire::refresh_peaks()` to forget here.
	//
	// This is one of the two **defended** tunables in `core/tuning.h`, and it is
	// worth restating why at the place it is written rather than only in the
	// table. `docs/REFERENCES.md` §"Steering lock" records that no CIK or KZ
	// source for a maximum steer angle was found: 25 degrees is inherited from
	// the bodywork clearance tables measured in issues #109 and #110, applied to
	// the *inner* wheel so no front wheel ever exceeds the angle those clearances
	// were measured at. Raising it past 25 degrees therefore does not merely
	// change the feel — it steers the front wheels into bodywork that was checked
	// against this number.
	vehicle_.steering().max_lock = p_radians;
}

double KartBody::get_max_lock() const {
	return vehicle_.steering().max_lock;
}

// --- lifecycle -----------------------------------------------------------------

void KartBody::set_spawn(const Transform3D &p_transform) {
	spawn_ = p_transform;
}

Transform3D KartBody::get_spawn() const {
	return spawn_;
}

void KartBody::respawn() {
	set_global_transform(spawn_);
	set_linear_velocity(Vector3());
	set_angular_velocity(Vector3());
	// **And the solver.** A body teleported without its solver reset keeps an
	// axle speed from wherever it used to be and launches itself off the line —
	// which is one call rather than two precisely so a caller cannot forget.
	vehicle_.reset();
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		contact_[corner] = CornerContact();
	}
	last_input_ = DriverInput();
	shift_up_pending_ = false;
	shift_down_pending_ = false;
}

void KartBody::engage(int p_gear, double p_road_speed_ms) {
	vehicle_.engage(p_gear, p_road_speed_ms);
	// Along the kart's own forward, which is its -Z. Set on the body rather than
	// left to the solver, because the solver does not integrate the body — Godot
	// does — and a driveline turning at 30 m/s under a stationary chassis is one
	// enormous slip ratio.
	set_linear_velocity(-get_global_transform().basis.get_column(2).normalized() *
			static_cast<real_t>(p_road_speed_ms));
	set_angular_velocity(Vector3());
}

// --- the engine note ------------------------------------------------------------

void KartBody::resolve_engine_voice() {
	engine_voice_.unref();
	if (engine_voice_path_.is_empty()) {
		return;
	}

	Node *node = get_node_or_null(engine_voice_path_);
	if (node == nullptr) {
		UtilityFunctions::push_warning(
				"KartBody: engine_voice_player '", engine_voice_path_,
				"' resolves to nothing. The kart will be silent.");
		return;
	}

	// Both player types, because the two are siblings rather than parent and child
	// in Godot's hierarchy and there is no common base that carries `get_stream`.
	// `AudioStreamPlayer3D` is what the kart scene uses — the note comes from the
	// engine mount and pans with the look-back camera — and the plain player is
	// accepted because a probe or a menu has no reason to place a voice in space.
	Ref<AudioStream> stream;
	if (AudioStreamPlayer3D *player_3d = Object::cast_to<AudioStreamPlayer3D>(node)) {
		stream = player_3d->get_stream();
	} else if (AudioStreamPlayer *player = Object::cast_to<AudioStreamPlayer>(node)) {
		stream = player->get_stream();
	} else {
		UtilityFunctions::push_warning(
				"KartBody: engine_voice_player '", engine_voice_path_,
				"' is a ", node->get_class(),
				", not an AudioStreamPlayer3D or AudioStreamPlayer. The kart will be silent.");
		return;
	}

	engine_voice_ = stream;
	if (engine_voice_.is_null()) {
		// Said loudly, because this is the failure mode with no other symptom. A
		// player whose stream is the wrong resource plays happily and silently, and
		// the first guess when a kart is quiet is always the synth.
		UtilityFunctions::push_warning(
				"KartBody: '", engine_voice_path_,
				"' has no EngineVoiceStream. Its stream is ",
				stream.is_null() ? String("empty") : stream->get_class(),
				". The kart will be silent, and nothing else will report it.");
	}
}

void KartBody::resolve_noise_voice(const NodePath &p_path, Ref<NoiseVoiceStream> &r_stream,
		const char *p_property) {
	r_stream.unref();
	if (p_path.is_empty()) {
		return;
	}

	Node *node = get_node_or_null(p_path);
	if (node == nullptr) {
		UtilityFunctions::push_warning(
				"KartBody: ", p_property, " '", p_path,
				"' resolves to nothing. That layer will be silent.");
		return;
	}

	// Both player types, for the reason `resolve_engine_voice` gives -- and here the
	// plain player is not a concession to probes but the production case: the wind
	// layer is at the driver's head and must not be spatialized at all, so it is
	// mounted on an `AudioStreamPlayer` while the scrub is on an
	// `AudioStreamPlayer3D` at the kart.
	Ref<AudioStream> stream;
	if (AudioStreamPlayer3D *player_3d = Object::cast_to<AudioStreamPlayer3D>(node)) {
		stream = player_3d->get_stream();
	} else if (AudioStreamPlayer *player = Object::cast_to<AudioStreamPlayer>(node)) {
		stream = player->get_stream();
	} else {
		UtilityFunctions::push_warning(
				"KartBody: ", p_property, " '", p_path, "' is a ", node->get_class(),
				", not an AudioStreamPlayer3D or AudioStreamPlayer. That layer will be silent.");
		return;
	}

	r_stream = stream;
	if (r_stream.is_null()) {
		// Loudly, because this failure has no other symptom: a player whose stream is
		// the wrong resource plays happily and silently.
		UtilityFunctions::push_warning(
				"KartBody: '", p_path, "' has no NoiseVoiceStream. Its stream is ",
				stream.is_null() ? String("empty") : stream->get_class(),
				". That layer will be silent, and nothing else will report it.");
	}
}

// One tick of engine state, published through the stream's seqlock.
//
// **This is the only place `VehicleTelemetry` becomes `EngineAudioInput`.**
// `audio_state.h` puts the mapping at the boundary rather than in `src/core/` on
// purpose, so that the solver keeps having no opinion about who is listening.
//
// Wait-free: `publish` is two counter stores and a struct copy, measured at 3.5 ns.
// Nothing here can make the physics tick wait on the audio thread, which is the
// property that matters — the reverse, an audio thread waiting on physics, is what
// the seqlock's bounded retry handles.
void KartBody::publish_engine_audio() {
	// Any one of the three is reason enough to build the struct. A scene with only
	// a scrub layer -- a replay viewer, a probe -- is legal, and requiring the
	// engine voice to exist before the tires could be heard would be a coupling
	// nothing asked for.
	if (engine_voice_.is_null() && scrub_voice_.is_null() && wind_voice_.is_null() &&
			shift_voice_.is_null() && roll_voice_.is_null()) {
		return;
	}

	const VehicleTelemetry &source = vehicle_.telemetry();
	const kart::core::Engine &engine = vehicle_.drivetrain.engine;

	kart::core::EngineAudioInput audio;
	audio.rpm = source.engine_rpm;

	// `load` is not `throttle`, and `audio_state.h` explains at length why: the
	// measured on/off-throttle constraint was extracted by splitting frames on the
	// sign of df0/dt, which is an *engine* state and not a pedal. Off the pipe, wide
	// open throttle makes very little torque and should not sound like it makes a
	// lot.
	//
	// The denominator is what the engine could make at this rpm, so `load` is 1.0
	// when the engine is doing everything it can *here* rather than everything it
	// could anywhere. At 3,000 rpm a KZ at full throttle is working flat out and
	// making very little, which is exactly the sound being asked for.
	const double capacity = engine.wide_open_torque(source.engine_rpm);
	const double ratio = capacity > 0.0 ? source.engine_torque / capacity : 0.0;
	audio.load = ratio < 0.0 ? 0.0 : (ratio > 1.0 ? 1.0 : ratio);

	audio.throttle = last_input_.throttle;

	// Negative crank torque is the road driving the engine. Clamped to zero load
	// above and reported as a state here, because the measurement behind the ladder
	// tilt is a two-state observation and nothing in `kz_audio_reference.h`
	// describes an in-between. This flag is what makes #39's engine braking audible.
	audio.trailing = source.engine_torque <= 0.0;

	audio.gear = source.gear;
	audio.shifting = source.shifting;
	audio.over_rev = engine.over_rev(source.engine_rpm);
	// Between the soft and hard cuts, where the ignition is dropping sparks and
	// `limiter_scale` is below 1. Distinct from `over_rev`, which is past the hard
	// cut: one is the engine doing its job and the other is a mistake, and a driver
	// has to be able to hear which.
	audio.on_limiter = engine.limiter_scale(source.engine_rpm) < 1.0 &&
			!engine.over_rev(source.engine_rpm);

	audio.clutch_slip = source.clutch_slip;
	audio.speed_ms = source.speed_ms;

	// Scrub, aggregated across the corners. The mean of the four rather than the
	// maximum: one wheel kerbing is not the sound of a kart sliding, and a maximum
	// would make it one.
	//
	// Slip *angle* only, and normalized against the tire's own peak. The longitudinal
	// slip ratio is deliberately left out of v1 — a locked wheel and a spinning wheel
	// are different noises, and §12's scrub layer has no measured spectrum for either
	// (`SCRUB_SPECTRUM_MEASURED` is false), so adding a second unsourced driver to an
	// unsourced filter is two guesses stacked. Issue #83 owns it.
	double scrub_sum = 0.0;
	int scrub_count = 0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const WheelTelemetry &wheel = source.wheel[corner];
		if (!wheel.tire_contact) {
			continue;
		}
		const double normalized = std::fabs(wheel.slip_angle) / SCRUB_REFERENCE_SLIP_RAD;
		scrub_sum += normalized > 1.0 ? 1.0 : normalized;
		++scrub_count;
	}
	audio.scrub = scrub_count > 0 ? scrub_sum / static_cast<double>(scrub_count) : 0.0;

	// One surface for the whole kart, from a wheel that is actually on something.
	// Four surfaces would be right and there is one scrub layer, so the first wheel
	// with contact wins. A kart straddling a curb reports one of the two rather than
	// an average of an enum, which is what averaging a `SurfaceType` would produce.
	//
	// Read from `contact_` and not from `WheelTelemetry`, which has no `surface`
	// field: the surface is an input to the solver rather than an output of it, so it
	// lives on `GroundQuery` and this class's own copy of what it served is the
	// authoritative record of it.
	audio.surface = 0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		if (contact_[corner].grounded) {
			audio.surface = contact_[corner].surface;
			break;
		}
	}

	// The same struct to all five. `noise_voice.h` says why they share a payload
	// rather than each getting its own: the fields are computed once here, and a
	// second payload type would be a second place for the aggregation rule above to
	// be restated.
	//
	// **Nothing was added to `EngineAudioInput` for #83 or #85**, and that is worth
	// recording rather than being quietly pleased about. The struct already carried
	// `shifting`, `gear`, `clutch_slip`, `throttle`, `speed_ms` and `surface`,
	// every one of them filled from `VehicleTelemetry` above, because whoever wrote
	// it wrote the fields the tickets would need and said so in the comments. The
	// POD rule -- no pointer, no container, nothing with a non-trivial copy,
	// because it crosses a thread boundary -- is therefore untouched.
	if (engine_voice_.is_valid()) {
		engine_voice_->publish(audio);
	}
	if (scrub_voice_.is_valid()) {
		scrub_voice_->publish(audio);
	}
	if (wind_voice_.is_valid()) {
		wind_voice_->publish(audio);
	}
	if (shift_voice_.is_valid()) {
		shift_voice_->publish(audio);
	}
	if (roll_voice_.is_valid()) {
		roll_voice_->publish(audio);
	}
}

// --- read-out -------------------------------------------------------------------

Dictionary KartBody::telemetry() const {
	const VehicleTelemetry &source = vehicle_.telemetry();

	// Exactly the keys `scripts/game/telemetry_panel.gd` documents, which are
	// exactly `VehicleTelemetry`'s own field names. Nothing invented, nothing
	// renamed: the panel reads every key with a default, so a renamed key does
	// not fail loudly, it draws a zero forever.
	Array wheels;
	wheels.resize(CORNER_COUNT);
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const WheelTelemetry &wheel = source.wheel[corner];
		Dictionary entry;
		entry["normal_load"] = wheel.normal_load;
		entry["slip_angle"] = wheel.slip_angle;
		entry["slip_ratio"] = wheel.slip_ratio;
		entry["suspension_travel"] = wheel.suspension_travel;
		entry["lift"] = wheel.lift;
		entry["utilization"] = wheel.utilization;
		entry["steer_angle"] = wheel.steer_angle;
		entry["force"] = to_godot(wheel.force);
		entry["grounded"] = wheel.grounded;
		// Issue #136: `grounded` and `tire_contact` are two questions, not one.
		// `grounded` is carrying load; `tire_contact` is touching the road at all.
		// A rebound damper can cancel the spring on a tire that never left the
		// ground, which is real behavior — measured at 98.2 mm/s of extension on
		// the front corner — and the panel drew "all four down" for it until both
		// were published.
		entry["tire_contact"] = wheel.tire_contact;
		// The latch, which `wheel_report()` has always served and this has not.
		// A latched contact is a wheel **buried** in a surface rather than one
		// resting on it: ADR-0033 finding 4 measured that a ray starting below a
		// surface returns no hit at all, so the boundary re-serves the last valid
		// plane. Identical to a real contact in every other field here, which is
		// the same "two states, one read-out" defect #136 was filed about.
		entry["latched"] = contact_[corner].latched;
		wheels[corner] = entry;
	}

	Dictionary out;
	out["wheel"] = wheels;
	out["engine_rpm"] = source.engine_rpm;
	out["engine_torque"] = source.engine_torque;
	out["axle_torque"] = source.axle_torque;
	out["axle_speed"] = source.axle_speed;
	out["gear"] = source.gear;
	out["clutch_slip"] = source.clutch_slip;
	out["clutch_torque"] = source.clutch_torque;
	out["shifting"] = source.shifting;
	out["over_rev"] = source.over_rev;
	out["speed_ms"] = source.speed_ms;
	out["lateral_g"] = source.lateral_g;
	out["longitudinal_g"] = source.longitudinal_g;
	out["frame_warp"] = source.frame_warp;
	out["substeps"] = source.substeps;
	// The one field the solver cannot fill: it is simulated seconds per
	// wall-clock second and the solver may not read a clock. Filled here.
	out["time_ratio"] = time_ratio_;
	return out;
}

Array KartBody::wheel_report() const {
	const VehicleTelemetry &source = vehicle_.telemetry();
	const Transform3D transform = get_global_transform();
	const Vector3 down = -transform.basis.get_column(1).normalized();

	Array out;
	out.resize(CORNER_COUNT);
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const WheelTelemetry &wheel = source.wheel[corner];
		const CornerContact &stored = contact_[corner];

		Dictionary entry;
		entry["name"] = String(CORNER_NAME[corner]);
		entry["origin"] = to_global(to_godot(vehicle_.ray_origin(corner)));
		// Served rather than assumed. #124 is the bug where the debug draw
		// pointed its rays along world down instead of chassis down, so every ray
		// it drew was wrong the moment the kart rolled.
		entry["direction"] = down;
		entry["length"] = vehicle_.ray_length(corner);
		entry["radius"] = vehicle_.wheel_radius(corner);
		entry["contact"] = stored.grounded;
		// Separate from `contact` because a latched contact is a wheel that is
		// buried rather than one that is resting, and the two are identical in
		// every other read-out here.
		entry["latched"] = stored.latched;
		entry["point"] = stored.point;
		entry["normal"] = stored.normal;
		entry["lift"] = wheel.lift;
		// Issue #136. `contact` above is the **raycast's** answer, latched when a
		// wheel is buried; this is what the suspension did with it. The debug draw
		// wants both, because a corner whose ray found ground while the spring
		// carries nothing is exactly the state the draw exists to make visible.
		entry["tire_contact"] = wheel.tire_contact;
		entry["travel"] = wheel.suspension_travel;
		entry["load"] = wheel.normal_load;
		entry["slip_angle"] = wheel.slip_angle;
		entry["slip_ratio"] = wheel.slip_ratio;
		entry["utilization"] = wheel.utilization;
		entry["steer_angle"] = wheel.steer_angle;
		entry["surface"] = stored.surface;
		entry["force"] = to_godot(wheel.force);
		out[corner] = entry;
	}
	return out;
}

double KartBody::get_speed_ms() const {
	return vehicle_.telemetry().speed_ms;
}

double KartBody::get_throttle_input() const {
	return last_input_.throttle;
}

double KartBody::get_brake_input() const {
	return last_input_.brake;
}

double KartBody::get_steer_input() const {
	// The rate-limited position the solver is actually steering with, not the
	// raw axis: `steering.h` owns the limit, so this is the number that
	// corresponds to where the front wheels are.
	return vehicle_.steer_position();
}

int KartBody::get_wheels_on_ground() const {
	const VehicleTelemetry &source = vehicle_.telemetry();
	int count = 0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		if (source.wheel[corner].grounded) {
			++count;
		}
	}
	return count;
}

double KartBody::get_steer_lock() const {
	// `steering.h` owns it. The HUD used to read a GDScript constant that had to
	// be kept in step with it by hand.
	return kz_front_geometry().max_lock;
}

double KartBody::get_soft_cut_rpm() const {
	return vehicle_.drivetrain.engine.soft_cut_rpm;
}

double KartBody::get_hard_cut_rpm() const {
	return vehicle_.drivetrain.engine.hard_cut_rpm;
}

bool KartBody::is_on_limiter() const {
	// Asked of the engine rather than compared against the thresholds here, so
	// that the taper's shape stays in one place. `limiter_scale` is 1.0 below the
	// soft cut and falls linearly to 0.0 at the hard cut; anything below 1.0 means
	// sparks are being dropped.
	const kart::core::Engine &engine = vehicle_.drivetrain.engine;
	return engine.limiter_scale(vehicle_.telemetry().engine_rpm) < 1.0;
}

double KartBody::get_rollover_threshold_g(bool p_turning_left) const {
	return kart::core::kz::rollover_threshold_g(vehicle_.mass_properties(), p_turning_left);
}

double KartBody::get_engine_rpm() const {
	return vehicle_.telemetry().engine_rpm;
}

double KartBody::get_engine_torque() const {
	return vehicle_.telemetry().engine_torque;
}

double KartBody::get_axle_torque() const {
	return vehicle_.telemetry().axle_torque;
}

double KartBody::get_axle_speed() const {
	// Straight off the solver rather than out of telemetry: it is one number by
	// construction — issue #33 — and there is no accessor that could return two.
	return vehicle_.axle_speed();
}

int KartBody::get_gear() const {
	return vehicle_.telemetry().gear;
}

double KartBody::get_clutch_slip() const {
	return vehicle_.telemetry().clutch_slip;
}

bool KartBody::is_shifting() const {
	return vehicle_.telemetry().shifting;
}

bool KartBody::is_over_rev() const {
	return vehicle_.telemetry().over_rev;
}

double KartBody::get_lateral_g() const {
	return vehicle_.telemetry().lateral_g;
}

double KartBody::get_longitudinal_g() const {
	return vehicle_.telemetry().longitudinal_g;
}

double KartBody::get_frame_warp() const {
	return vehicle_.telemetry().frame_warp;
}

double KartBody::get_time_ratio() const {
	return time_ratio_;
}

} // namespace kartgame
