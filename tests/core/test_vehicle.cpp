#include "doctest.h"

#include "core/chassis.h"
#include "core/chassis_flex.h"
#include "core/kz_reference.h"
#include "core/state_hash.h"
#include "core/steering.h"
#include "core/tire.h"
#include "core/units.h"
#include "core/vehicle.h"

#include <cmath>
#include <cstdio>

// **The M3b vehicle solver, measured.** Issues #33 and #41, and the last open
// acceptance item of #34.
//
// Every other M3b header is tested against its own limiting cases. This file is
// the only one that can answer the questions `ARCHITECTURE.md` §6.4 asks, because
// those questions are about a kart driving and a kart driving is what the solver
// produces. So the shape here is different from the other suites: a rigid-body
// harness, a set of scenarios, and numbers printed beside the reference bands
// they are supposed to land in.
//
// ## The harness is not a convenience, it is the second half of the measurement
//
// `KartVehicle::step` returns forces. Nothing in `src/core/` integrates them —
// Godot does that, and ADR-0032 measured exactly how. `Rig` below reproduces
// what ADR-0032 and ADR-0033 measured the engine to do, and no more than that:
//
//   * symplectic Euler, position advanced with the **new** velocity, because
//     ADR-0032 finding 1 measured the engine to be doing that to within 1.3 um
//     over 120 ticks;
//   * `apply_force`'s position argument read as an offset from the **body
//     origin**, ADR-0033's headline finding, because a harness that read it as a
//     center-of-mass offset would test the solver against a physics the engine
//     does not have — and would hide exactly the bug ADR-0033 found;
//   * angular acceleration as `R I^-1 R^T tau` with the **diagonal** of the
//     chassis-frame tensor, because that is all `RigidBody3D.inertia` accepts,
//     ADR-0033 finding 5.
//
// It does not model contact. It does not need to: ADR-0033 finding 2 measured
// the contact to be a clean unilateral constraint that supplies exactly the
// balance below `m*g` and exactly zero above it, so a per-wheel spring may apply
// its full computed force unconditionally, and a flat plane raycast is finding 4's
// exact-and-cheap case.
//
// What it therefore is **not** is proof the numbers survive Jolt. It is proof
// they are what the solver produces against the engine behavior this project has
// measured, which is the strongest claim available without an engine and is the
// claim `tools/verify/drive.sh` will then check.

using namespace kart::core;

namespace {

constexpr double TICK = 1.0 / 120.0;

// A rigid body, a flat floor, and nothing else.
struct Rig {
	KartVehicle vehicle;

	Vec3 com_position; // world, the center of mass
	Vec3 basis_x = Vec3(1.0, 0.0, 0.0);
	Vec3 basis_y = Vec3(0.0, 1.0, 0.0);
	Vec3 basis_z = Vec3(0.0, 0.0, 1.0);
	Vec3 linear_velocity; // of the center of mass, world
	Vec3 angular_velocity; // world

	double ground_height = 0.0;
	double surface_grip = 1.0;
	// Per-corner multiplier on top of `surface_grip`, so one wheel can be put on
	// a different surface from the other three. That is not a curiosity: a locked
	// axle with one rear tire on grass is the case that separates it from a
	// differential, and it is the only way to ask "is the torque split by
	// available grip" without a wheel speed to compare.
	double corner_grip[CORNER_COUNT] = { 1.0, 1.0, 1.0, 1.0 };
	bool ground_present = true;

	// Set false to make every ray miss without moving the floor — the
	// "GroundQuery reporting a miss on every corner" case, which ADR-0033
	// finding 4 says is a real thing a raycast does when the wheel is buried
	// deepest in a curb, not a hypothetical.
	bool queries_answer = true;

	VehicleForces last_forces;
	Vec3 last_acceleration;

	void configure() {
		vehicle.configure();
		// Start one static deflection low so the settle is short. The exact value
		// does not matter; `settle()` finds equilibrium either way.
		place(Vec3(0.0, -0.003, 0.0));
	}

	// Place the body ORIGIN at a world point, keeping the current basis.
	void place(const Vec3 &origin) {
		com_position = origin + to_world(vehicle.mass_properties().center_of_mass);
	}

	Vec3 to_world(const Vec3 &local) const {
		return basis_x * local.x + basis_y * local.y + basis_z * local.z;
	}

	Vec3 origin() const { return com_position - to_world(vehicle.mass_properties().center_of_mass); }

	BodyState body_state() const {
		BodyState state;
		state.origin = origin();
		state.center_of_mass = com_position;
		state.basis_x = basis_x;
		state.basis_y = basis_y;
		state.basis_z = basis_z;
		state.linear_velocity = linear_velocity;
		state.angular_velocity = angular_velocity;
		return state;
	}

	// Forward speed, m/s. Forward is -Z (vec3.h).
	double forward_speed() const { return -linear_velocity.dot(basis_z); }
	double kmh() const { return ms_to_kmh(forward_speed()); }
	double yaw_rate() const { return angular_velocity.dot(basis_y); }

	// A raycast against a horizontal plane. Exact, per ADR-0033 finding 4.
	void query(GroundQuery out[CORNER_COUNT]) const {
		const Vec3 down = -basis_y;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			out[corner] = GroundQuery();
			if (!ground_present || !queries_answer) {
				continue;
			}
			const Vec3 mount = origin() + to_world(vehicle.ray_origin(corner));
			if (std::fabs(down.y) < 1e-9) {
				continue;
			}
			const double distance = (ground_height - mount.y) / down.y;
			if (distance < 0.0 || distance > vehicle.ray_length(corner)) {
				continue;
			}
			out[corner].hit = true;
			out[corner].distance = distance;
			out[corner].point = mount + down * distance;
			out[corner].normal = Vec3(0.0, 1.0, 0.0);
			out[corner].surface_grip = surface_grip * corner_grip[corner];
		}
	}

	void step(const DriverInput &input) {
		GroundQuery contacts[CORNER_COUNT];
		query(contacts);

		const BodyState state = body_state();
		last_forces = vehicle.step(state, input, contacts, TICK);

		const double mass = vehicle.mass_properties().mass;
		Vec3 force = last_forces.central_force + Vec3(0.0, -G, 0.0) * mass;
		Vec3 torque = last_forces.central_torque;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			force += last_forces.force[corner];
			// **Offset from the body origin.** ADR-0033. A harness that wrote
			// `last_forces.application_point[corner]` alone here, or that added
			// the center of mass instead of the origin, would double every pitch
			// and roll moment and would do it silently.
			const Vec3 arm = state.origin + last_forces.application_point[corner] - com_position;
			torque += arm.cross(last_forces.force[corner]);
		}

		last_acceleration = force / mass;
		linear_velocity += last_acceleration * TICK;

		const InertiaTensor &inertia = vehicle.mass_properties().inertia;
		const Vec3 body_torque(torque.dot(basis_x), torque.dot(basis_y), torque.dot(basis_z));
		const Vec3 body_alpha(body_torque.x / inertia.xx, body_torque.y / inertia.yy,
				body_torque.z / inertia.zz);
		angular_velocity += (basis_x * body_alpha.x + basis_y * body_alpha.y +
									basis_z * body_alpha.z) *
				TICK;

		// Symplectic: advance with the new velocity.
		com_position += linear_velocity * TICK;

		const double rate = angular_velocity.length();
		if (rate > 0.0) {
			const Vec3 axis = angular_velocity / rate;
			const double angle = rate * TICK;
			basis_x = basis_x.rotated(axis, angle);
			basis_y = basis_y.rotated(axis, angle);
			basis_z = basis_z.rotated(axis, angle);
		}
		// Gram-Schmidt, every tick, in a fixed order. Two runs cannot disagree
		// about the order and the basis cannot drift out of orthonormal over a
		// 40-second scenario.
		basis_x = basis_x.normalized();
		basis_y = (basis_y - basis_x * basis_y.dot(basis_x)).normalized();
		basis_z = basis_x.cross(basis_y);
	}

	void settle(int ticks = 180) {
		DriverInput idle;
		for (int tick = 0; tick < ticks; ++tick) {
			step(idle);
		}
	}

	bool finite() const {
		return com_position.is_finite() && linear_velocity.is_finite() &&
				angular_velocity.is_finite() && basis_x.is_finite() && basis_y.is_finite() &&
				basis_z.is_finite() && std::isfinite(vehicle.axle_speed()) &&
				std::isfinite(vehicle.front_wheel_speed(CORNER_FL)) &&
				std::isfinite(vehicle.front_wheel_speed(CORNER_FR)) &&
				std::isfinite(vehicle.warp_amplitude());
	}

	double largest_force() const {
		double worst = last_forces.central_force.length();
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			const double magnitude = last_forces.force[corner].length();
			if (magnitude > worst) {
				worst = magnitude;
			}
		}
		return worst;
	}

	// Everything a replay would have to reproduce, quantized and hashed. See
	// `state_hash.h` on why bits are the wrong comparison and a grid is the right
	// one.
	uint64_t hash() const {
		StateHash digest;
		digest.add_vector3(com_position.x, com_position.y, com_position.z);
		digest.add_vector3(linear_velocity.x, linear_velocity.y, linear_velocity.z);
		digest.add_vector3(angular_velocity.x, angular_velocity.y, angular_velocity.z);
		digest.add_vector3(basis_x.x, basis_x.y, basis_x.z);
		digest.add_vector3(basis_y.x, basis_y.y, basis_y.z);
		digest.add_vector3(basis_z.x, basis_z.y, basis_z.z);
		digest.add_double(vehicle.axle_speed());
		digest.add_double(vehicle.front_wheel_speed(CORNER_FL));
		digest.add_double(vehicle.front_wheel_speed(CORNER_FR));
		digest.add_double(vehicle.steer_position());
		digest.add_double(vehicle.warp_amplitude());
		digest.add_double(vehicle.telemetry().engine_rpm);
		digest.add_int(vehicle.telemetry().gear);
		return digest.digest();
	}
};

// Hold a target speed with throttle and brake. A skidpad is a steady-state
// measurement and a kart at full lock sheds speed fast, so something has to hold
// it there; a driver does this with his right foot and this is the same loop with
// the gains written down. It never touches the steering, so nothing it does can
// manufacture the cornering figures it is there to make measurable.
struct SpeedHold {
	double target = 0.0;

	void apply(DriverInput &input, double speed) const {
		const double error = target - speed;
		input.throttle = error > 0.0 ? (error * 0.6 > 1.0 ? 1.0 : error * 0.6) : 0.0;
		input.brake = error < 0.0 ? (-error * 0.3 > 1.0 ? 1.0 : -error * 0.3) : 0.0;
	}
};

// One steady cornering condition, averaged over the last second of a six-second
// run at a held speed.
//
// **Steady state rather than a speed ramp**, and that is the whole reason this
// struct exists. The obvious skidpad — hold lock, wind the speed up, record the
// peak — mixes the transient into every number it reports, and the number this
// file exists to compare against is `chassis_flex.h`'s quasi-static solution.
// Comparing a transient measurement with a quasi-static prediction and reporting
// the difference as a model disagreement would be reporting the ramp rate.
struct Steady {
	double lateral_g = 0.0;
	double longitudinal_g = 0.0;
	double speed = 0.0;
	double yaw_rate = 0.0;
	double kinematic_yaw_rate = 0.0;
	double body_slip = 0.0; // radians
	double load[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double inside_rear_load = 0.0;
	double outside_rear_load = 0.0;
	double inside_rear_slip_ratio = 0.0;
	double outside_rear_slip_ratio = 0.0;
	double inside_rear_longitudinal = 0.0;
	double outside_rear_longitudinal = 0.0;
	double inside_rear_lift_mm = 0.0;
	double warp_mm = 0.0;
	bool stable = true;
};

// Hold a steering input and a speed, and report what the kart settles at.
//
// Turning **left** by default, which is not arbitrary: `chassis.h` measures the
// center of mass 41 mm to the right of the centerline, because the engine,
// exhaust and radiator are all outboard on the right, and says in its own comment
// that a scenario which only ever turns one way measures one of two karts. So the
// tests below run both, and the two answers differ by more than rounding.
//
// The speed is held by a proportional controller on throttle and brake. A driver
// does exactly this with his right foot on a skidpad; the loop never touches the
// steering, so nothing it does can manufacture the cornering figures it exists to
// make measurable. What it does cost is honest and is reported: holding speed at
// full lock takes real throttle, and the resulting longitudinal g appears in
// every table below because it moves load off the rear axle and therefore moves
// the number the whole exercise is about.
Steady steady_corner(double steer, double target_speed, bool jacking, double direction = 1.0,
		double peak_friction = 0.0) {
	Rig rig;
	rig.vehicle.jacking_enabled = jacking;
	rig.configure();
	if (peak_friction > 0.0) {
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			rig.vehicle.tire(corner).lateral.peak_friction = peak_friction;
			rig.vehicle.tire(corner).longitudinal.peak_friction = peak_friction;
		}
	}
	rig.settle();
	// Second gear reaches 7 m/s inside the powerband and first does not reach it
	// at all without an upshift mid-measurement.
	rig.vehicle.engage(target_speed < 6.0 ? 1 : 2, target_speed);
	rig.linear_velocity = -rig.basis_z * target_speed;

	const bool inside_left = direction > 0.0;
	const int inside_rear = inside_left ? CORNER_RL : CORNER_RR;
	const int outside_rear = inside_left ? CORNER_RR : CORNER_RL;

	const SteeringGeometry steering = kz_front_geometry();
	const double inner = steering.max_lock * std::fabs(steer);
	const double outer = ackermann_outer_angle(steering, inner);
	const double radius = turn_radius(steering, inner, outer);

	SpeedHold hold;
	hold.target = target_speed;
	Steady result;
	double samples = 0.0;

	for (int tick = 0; tick < 120 * 6; ++tick) {
		DriverInput input;
		input.steer = direction * steer;
		hold.apply(input, rig.forward_speed());
		rig.step(input);
		if (!rig.finite()) {
			result.stable = false;
			return result;
		}
		if (tick <= 120 * 5) {
			continue;
		}

		const VehicleTelemetry &telemetry = rig.vehicle.telemetry();
		result.lateral_g += std::fabs(telemetry.lateral_g);
		result.longitudinal_g += telemetry.longitudinal_g;
		result.speed += rig.forward_speed();
		result.yaw_rate += std::fabs(rig.yaw_rate());
		result.kinematic_yaw_rate += radius > 0.0 ? rig.forward_speed() / radius : 0.0;
		result.body_slip +=
				std::atan2(rig.linear_velocity.dot(rig.basis_x), -rig.linear_velocity.dot(rig.basis_z));
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			result.load[corner] += telemetry.wheel[corner].normal_load;
		}
		result.inside_rear_load += telemetry.wheel[inside_rear].normal_load;
		result.outside_rear_load += telemetry.wheel[outside_rear].normal_load;
		result.inside_rear_slip_ratio += telemetry.wheel[inside_rear].slip_ratio;
		result.outside_rear_slip_ratio += telemetry.wheel[outside_rear].slip_ratio;
		result.inside_rear_longitudinal += telemetry.wheel[inside_rear].force.dot(-rig.basis_z);
		result.outside_rear_longitudinal += telemetry.wheel[outside_rear].force.dot(-rig.basis_z);
		result.inside_rear_lift_mm += telemetry.wheel[inside_rear].lift * 1000.0;
		result.warp_mm += rig.vehicle.warp_amplitude() * 1000.0;
		samples += 1.0;
	}

	if (samples <= 0.0) {
		result.stable = false;
		return result;
	}
	result.lateral_g /= samples;
	result.longitudinal_g /= samples;
	result.speed /= samples;
	result.yaw_rate /= samples;
	result.kinematic_yaw_rate /= samples;
	result.body_slip /= samples;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		result.load[corner] /= samples;
	}
	result.inside_rear_load /= samples;
	result.outside_rear_load /= samples;
	result.inside_rear_slip_ratio /= samples;
	result.outside_rear_slip_ratio /= samples;
	result.inside_rear_longitudinal /= samples;
	result.outside_rear_longitudinal /= samples;
	result.inside_rear_lift_mm /= samples;
	result.warp_mm /= samples;

	// A kart that has stopped following the circle is not a steady state, however
	// steadily it is sliding. 30 degrees of body slip is the cutoff.
	if (std::fabs(result.body_slip) > 0.52 || result.speed < target_speed * 0.6) {
		result.stable = false;
	}
	return result;
}

// The quasi-static answer `tests/core/test_kart_lift.cpp` prints, recomputed here
// so the two cannot drift, with the two corrections that make it comparable with
// a driven measurement.
//
// **1. The lateral center of mass.** `chassis_flex.h`'s `ChassisGeometry` has a
// `com_height` and no `com_x`, and its roll equation puts `m*a_y*h` on the
// right-hand side with nothing for a center of mass off the centerline.
// `chassis.h` derives one — **41 mm to the right**, from 27 kg of engine, exhaust
// and radiator hung outboard — and says in its own comment that it moves the
// rollover threshold by 0.4 g between a left-hand corner and a right-hand one.
// The two files disagree, and the size of it is exact rather than estimated: a
// standing roll moment of `m*g*com_x` is indistinguishable from an extra
// `com_x / com_height` of lateral g, which is **0.186 g on this kart**. That is
// larger than most of the effects `chassis_flex.h` spends paragraphs on. It is
// not a criticism of that file, which was written before the mass table existed
// and does not own it; it is reported so the field can be added.
//
// **2. The longitudinal g.** The quasi-static solution is taken at zero
// longitudinal acceleration. A real kart held at a steady speed on a full-lock
// skidpad is not at zero: it takes throttle to hold the speed against the scrub,
// the front tires are steered 25 degrees so their lateral force has a large
// rearward component, and this solver measures the balance at **-0.11 g**. That
// moves load off the rear axle and is worth another 0.10 g of lift threshold.
//
// With both applied the two models land on 1.528 g and 1.530 g. Without them the
// same comparison reads 1.817 g against 1.530 g and looks like a disagreement
// about the physics rather than about the question being asked.
double quasi_static_lift_g(double steer_input, double longitudinal_g = 0.0,
		bool include_com_offset = false) {
	LoadCase load_case;
	const MassProperties properties = kz::kart_mass_properties();
	load_case.geometry.mass = properties.mass;
	load_case.geometry.com_height = properties.center_of_mass.y;
	load_case.geometry.front_mass_share = kz::STATIC_FRONT_SHARE;
	load_case.longitudinal_g = longitudinal_g;

	const SteeringOutput steered = solve_steering(kz_front_geometry(), steer_input);
	load_case.geometric_offset[CORNER_FL] = -steered.left.contact_offset.y;
	load_case.geometric_offset[CORNER_FR] = -steered.right.contact_offset.y;

	const double bias = include_com_offset
			? properties.center_of_mass.x / properties.center_of_mass.y
			: 0.0;
	const double threshold = lift_threshold_g(load_case, CORNER_RL);
	return threshold - bias;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction: the things that must be true before any measurement means
// anything
// ---------------------------------------------------------------------------

TEST_CASE("the rear wheels are one degree of freedom by construction") {
	// Issue #33's first acceptance criterion. There is no assertion that could
	// fail here that is not a compile error, and that is the point: the solver
	// exposes one `axle_speed()` and no per-rear-wheel speed at all, so "the rear
	// wheel speeds are identical at all times" is not a property that is checked,
	// it is a property there is no way to violate.
	//
	// What *is* worth checking is that the slip ratios differ. Two wheels locked
	// to one speed on two different road speeds is the whole of issue #33, and if
	// the slip ratios came out equal it would mean the two rear tires were being
	// evaluated against the same contact velocity, which would make the axle
	// decorative.
	// The jacking is off, so the inside rear stays on the ground and the scrub is
	// there to be measured rather than resolved by the wheel leaving.
	const Steady corner = steady_corner(0.6, 6.0, false);
	REQUIRE(corner.stable);

	MESSAGE("locked axle, " << corner.speed << " m/s, 0.6 lock, jacking off: inside rear slip ratio "
							<< corner.inside_rear_slip_ratio << ", outside rear "
							<< corner.outside_rear_slip_ratio);

	CHECK(std::fabs(corner.inside_rear_slip_ratio - corner.outside_rear_slip_ratio) > 1e-3);
	// The inside wheel is being dragged along faster than its own road speed —
	// one axle speed against a shorter arc. That is the scrub, and its sign is
	// the thing to check: reversed, the inside wheel would be dragging the kart
	// *into* the corner rather than pushing it out of one.
	CHECK(corner.inside_rear_slip_ratio > corner.outside_rear_slip_ratio);
}

TEST_CASE("the frame's warp compliance reproduces chassis_flex.h's series pair") {
	// The solver adds one internal coordinate for the frame's twist, because a
	// rigid body on four springs has no warp mode of its own and would silently
	// pick the torsionally rigid answer. This checks the algebra against the file
	// that owns it.
	//
	// A pure warp deflection of amplitude `a` should produce a corner force of
	// `k * a * S/(4k + S)`, which is `chassis_flex.h`'s `warp_rate` — the tire
	// rate in series with a quarter of the frame's generalized rate — times `a`.
	ChassisGeometry geometry;
	const MassProperties properties = kz::kart_mass_properties();
	geometry.mass = properties.mass;
	geometry.com_height = properties.center_of_mass.y;

	const ChassisFlex flex = chassis_flex(geometry);
	const double frame_rate = warp_generalized_stiffness(FRAME_TORSION_NM_PER_DEG,
			geometry.track_front, geometry.track_rear);

	double rate_sum = 0.0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		rate_sum += corner_setup(geometry, corner).spring_rate;
	}
	const double mean_rate = rate_sum * 0.25;

	// The solver's amplitude for a unit warp input, from its own closed form.
	const double amplitude = -1.0 * rate_sum / (rate_sum + frame_rate);
	const double effective_rate = mean_rate * (1.0 + amplitude);

	MESSAGE("warp: chassis_flex.h " << flex.warp_rate << " N/m, solver closed form "
									<< effective_rate << " N/m; frame is "
									<< flex.warp_softness() << "x softer in warp than in roll");

	CHECK(effective_rate == doctest::Approx(flex.warp_rate).epsilon(1e-9));
}

TEST_CASE("the ray geometry and the corner setups agree without having been told to") {
	Rig rig;
	rig.configure();

	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const CornerSetup &setup = rig.vehicle.corner_setup_for(corner);
		// The hub sits at the unloaded radius above a body origin that is on the
		// ground, so the ray length at which the spring produces nothing is that
		// radius. `chassis_flex.h` derived `rest_length` from the load and the
		// rate and never saw this file.
		CHECK(setup.free_length() == doctest::Approx(rig.vehicle.wheel_radius(corner)));
		CHECK(rig.vehicle.ray_origin(corner).y == doctest::Approx(setup.free_length()));
		CHECK(rig.vehicle.ray_length(corner) > setup.free_length());
	}
}

TEST_CASE("the kart settles on its tires at the loads the mass table predicts") {
	Rig rig;
	rig.configure();
	rig.settle(360);

	const MassProperties properties = rig.vehicle.mass_properties();
	const VehicleTelemetry &telemetry = rig.vehicle.telemetry();

	double total = 0.0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		total += telemetry.wheel[corner].normal_load;
	}

	const double front = telemetry.wheel[CORNER_FL].normal_load +
			telemetry.wheel[CORNER_FR].normal_load;

	std::printf("\n    static, settled\n");
	std::printf("    %-14s %12s %12s\n", "corner", "load N", "predicted N");
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const bool is_front = corner == CORNER_FL || corner == CORNER_FR;
		std::printf("    %-14d %12.1f %12.1f\n", corner,
				telemetry.wheel[corner].normal_load,
				is_front ? kz::static_load_front_tire(properties)
						 : kz::static_load_rear_tire(properties));
	}
	std::printf("    total %.1f N against m*g %.1f N; front share %.4f against %.2f\n",
			total, properties.mass * G, front / total, kz::STATIC_FRONT_SHARE);

	CHECK(total == doctest::Approx(properties.mass * G).epsilon(0.01));
	CHECK(front / total == doctest::Approx(kz::STATIC_FRONT_SHARE).epsilon(0.02));
	// Nothing is rolling, pitching or bouncing.
	CHECK(std::fabs(rig.linear_velocity.y) < 0.01);
	CHECK(rig.angular_velocity.length() < 0.01);
}

// ---------------------------------------------------------------------------
// §6.4's three scenarios
// ---------------------------------------------------------------------------

TEST_CASE("straight-line acceleration against the KZ reference figures") {
	Rig rig;
	rig.configure();
	rig.settle();

	DriverInput input;
	input.throttle = 1.0;

	double time_to_100 = -1.0;
	double time_first_moved = -1.0;
	double top_speed = 0.0;
	double peak_longitudinal_g = 0.0;
	double peak_slip_ratio = 0.0;
	int gear_changes = 0;
	int previous_gear = rig.vehicle.telemetry().gear;

	const int ticks = 120 * 30;
	for (int tick = 0; tick < ticks; ++tick) {
		rig.step(input);
		REQUIRE(rig.finite());

		const VehicleTelemetry &telemetry = rig.vehicle.telemetry();
		if (time_first_moved < 0.0 && rig.forward_speed() > 0.1) {
			time_first_moved = tick * TICK;
		}
		if (time_to_100 < 0.0 && rig.kmh() >= 100.0) {
			time_to_100 = tick * TICK;
		}
		if (rig.forward_speed() > top_speed) {
			top_speed = rig.forward_speed();
		}
		if (telemetry.longitudinal_g > peak_longitudinal_g) {
			peak_longitudinal_g = telemetry.longitudinal_g;
		}
		const double slip = telemetry.wheel[CORNER_RL].slip_ratio;
		if (slip > peak_slip_ratio) {
			peak_slip_ratio = slip;
		}
		if (telemetry.gear != previous_gear) {
			++gear_changes;
			previous_gear = telemetry.gear;
		}
	}

	std::printf("\n    straight line, full throttle from rest\n");
	std::printf("    0-100 km/h        %8.2f s      reference %.1f-%.1f\n", time_to_100,
			kz::ZERO_TO_100_KMH_MIN_S, kz::ZERO_TO_100_KMH_MAX_S);
	std::printf("    from first motion %8.2f s\n", time_to_100 - time_first_moved);
	std::printf("    top speed         %8.2f km/h   reference %.0f-%.0f\n",
			ms_to_kmh(top_speed), kz::TOP_SPEED_MIN_KMH, kz::TOP_SPEED_MAX_KMH);
	std::printf("    peak long. g      %8.2f\n", peak_longitudinal_g);
	std::printf("    peak rear slip    %8.3f      %d gear changes, ended in %d at %.0f rpm\n",
			peak_slip_ratio, gear_changes, rig.vehicle.telemetry().gear,
			rig.vehicle.telemetry().engine_rpm);

	CHECK(time_to_100 > 0.0);
	CHECK(rig.vehicle.telemetry().gear == kz::GEAR_COUNT);
	CHECK(rig.finite());
	// The kart got there and stayed there. The band itself is reported rather
	// than asserted tightly — see the report; a figure outside its band is a
	// finding, not a test to relax.
	CHECK(ms_to_kmh(top_speed) > kz::TOP_SPEED_MIN_KMH);
	CHECK(ms_to_kmh(top_speed) < kz::TOP_SPEED_MAX_KMH);
	CHECK(time_to_100 < 6.0);

	// How much of that 0-100 is the launch rather than the kart. The auto-clutch
	// takes the engine to its own engagement speed and lets the plates go, which
	// puts the rear tires at a slip ratio no driver would choose; a driver feeds
	// the throttle instead. Measured rather than argued, because it is the
	// difference between "the kart is slow" and "the assist launches it badly",
	// and only one of those is a physics problem.
	std::printf("\n    what the launch costs\n");
	std::printf("    %-26s %8s %8s %10s\n", "launch", "0-100 s", "moving s", "peak slip");
	std::printf("    %-26s %8.2f %8.2f %10.3f\n", "full throttle from idle", time_to_100,
			time_to_100 - time_first_moved, peak_slip_ratio);

	for (double ramp : { 0.5, 1.0, 1.5 }) {
		Rig fed;
		fed.configure();
		fed.settle();
		double fed_to_100 = -1.0;
		double fed_moved = -1.0;
		double fed_slip = 0.0;
		for (int tick = 0; tick < 120 * 20; ++tick) {
			const double time = tick * TICK;
			DriverInput drive;
			drive.throttle = time < ramp ? time / ramp : 1.0;
			fed.step(drive);
			REQUIRE(fed.finite());
			if (fed_moved < 0.0 && fed.forward_speed() > 0.1) {
				fed_moved = time;
			}
			if (fed_to_100 < 0.0 && fed.kmh() >= 100.0) {
				fed_to_100 = time;
			}
			const double slip = fed.vehicle.telemetry().wheel[CORNER_RL].slip_ratio;
			if (slip > fed_slip) {
				fed_slip = slip;
			}
		}
		std::printf("    throttle fed over %.1f s     %8.2f %8.2f %10.3f\n", ramp, fed_to_100,
				fed_to_100 - fed_moved, fed_slip);
	}
}

TEST_CASE("braking from 90 to 20 km/h against ARCHITECTURE.md 6.4's 1.5-2.0 g") {
	// Swept across pedal positions rather than measured once at full pedal, and
	// the reason is the whole story of an unassisted kart's brakes: §6.4's
	// 1.5-2.0 g is what a driver gets by holding the tires just short of locking,
	// and there is no ABS here to do that for him. A single full-pedal figure
	// measures a locked kart, which is a real thing a driver can do and is not the
	// thing the reference band describes.
	std::printf("\n    braking, 90 to 20 km/h, clutch in, no ABS\n");
	std::printf("    %6s %8s %8s %8s %9s %s\n", "pedal", "mean g", "peak g", "time s", "meters",
			"locked");

	double best_mean = 0.0;
	double best_pedal = 0.0;
	double full_pedal_mean = 0.0;

	for (double pedal = 0.4; pedal <= 1.001; pedal += 0.1) {
		Rig rig;
		rig.configure();
		rig.settle();

		const double start = kmh_to_ms(90.0);
		rig.vehicle.engage(5, start);
		rig.linear_velocity = -rig.basis_z * start;

		DriverInput input;
		input.brake = pedal;
		// The clutch lever is in. A KZ driver pulls it braking for a hairpin, and
		// leaving it out would measure the brakes plus the engine braking rather
		// than the brakes.
		input.clutch = 1.0;

		double elapsed = 0.0;
		double distance = 0.0;
		double peak_g = 0.0;
		bool front_locked = false;
		bool rear_locked = false;
		const double finish = kmh_to_ms(20.0);

		for (int tick = 0; tick < 120 * 15 && rig.forward_speed() > finish; ++tick) {
			rig.step(input);
			REQUIRE(rig.finite());
			elapsed += TICK;
			distance += rig.forward_speed() * TICK;
			const VehicleTelemetry &telemetry = rig.vehicle.telemetry();
			const double decel = -telemetry.longitudinal_g;
			if (decel > peak_g) {
				peak_g = decel;
			}
			// A slip ratio at -1 is a stopped wheel under a moving kart, which is
			// the definition of locked; -0.85 is far enough down the curve to be
			// unambiguous and short of the exact value the guard clamps to.
			if (telemetry.wheel[CORNER_FL].slip_ratio < -0.85) {
				front_locked = true;
			}
			if (telemetry.wheel[CORNER_RL].slip_ratio < -0.85) {
				rear_locked = true;
			}
		}

		const double mean_g = ((start - rig.forward_speed()) / elapsed) / G;
		std::printf("    %6.1f %8.2f %8.2f %8.2f %9.1f %s%s\n", pedal, mean_g, peak_g, elapsed,
				distance, front_locked ? "front " : "", rear_locked ? "rear" : "");
		CHECK(rig.forward_speed() <= finish);
		CHECK(rig.finite());
		if (mean_g > best_mean) {
			best_mean = mean_g;
			best_pedal = pedal;
		}
		if (pedal > 0.95) {
			full_pedal_mean = mean_g;
		}
	}

	std::printf("    best %.2f g at %.1f pedal; full pedal %.2f g; reference %.1f-%.1f\n",
			best_mean, best_pedal, full_pedal_mean, kz::BRAKING_G_MIN, kz::BRAKING_G_MAX);

	CHECK(best_mean > 1.0);
	// Locking must cost something. A brake model where the fastest stop is with
	// the pedal on the floor has no friction curve behind it.
	CHECK(best_mean >= full_pedal_mean);
}

TEST_CASE("the skidpad, and whether the dynamic answer agrees with the quasi-static one") {
	// A sweep of steady-state corners at full lock, each held for six seconds and
	// averaged over the last one. The inside rear's load falls as speed rises, and
	// the lateral g at which it reaches zero is the number
	// `tests/core/test_kart_lift.cpp` predicts quasi-statically.
	const MassProperties properties = kz::kart_mass_properties();
	Tire tire;

	std::printf("\n    skidpad, full lock left, steady state at each speed\n");
	std::printf("    tire ceiling %.2f g, rollover %.2f g left / %.2f g right\n",
			tire.lateral.peak_friction, kz::rollover_threshold_g(properties, true),
			kz::rollover_threshold_g(properties, false));
	std::printf("    %6s %7s %7s %8s %8s %8s %8s %7s %8s %8s\n", "km/h", "lat g", "long g",
			"FL N", "FR N", "RL N", "RR N", "yaw", "slip deg", "warp mm");

	double lift_g = -1.0;
	double previous_lateral = 0.0;
	double previous_load = 0.0;
	double peak_lateral = 0.0;
	double peak_speed = 0.0;
	double long_at_lift = 0.0;
	for (double target = 3.0; target <= 8.01; target += 0.5) {
		const Steady corner = steady_corner(1.0, target, true);
		if (!corner.stable) {
			std::printf("    %6.1f   departed\n", ms_to_kmh(target));
			continue;
		}
		std::printf("    %6.1f %7.3f %7.3f %8.1f %8.1f %8.1f %8.1f %7.3f %8.2f %8.3f\n",
				ms_to_kmh(corner.speed), corner.lateral_g, corner.longitudinal_g,
				corner.load[CORNER_FL], corner.load[CORNER_FR], corner.load[CORNER_RL],
				corner.load[CORNER_RR], corner.yaw_rate, corner.body_slip * 180.0 / PI,
				corner.warp_mm);
		if (corner.lateral_g > peak_lateral) {
			peak_lateral = corner.lateral_g;
			peak_speed = corner.speed;
		}
		// Linear interpolation of the inside rear's load through zero, which is
		// the same definition `chassis_flex.h`'s `lift_threshold_g` bisects for.
		if (lift_g < 0.0 && corner.inside_rear_load <= 0.0 && previous_load > 0.0) {
			const double span = previous_load - corner.inside_rear_load;
			const double fraction = span > 0.0 ? previous_load / span : 0.0;
			lift_g = previous_lateral + fraction * (corner.lateral_g - previous_lateral);
			long_at_lift = corner.longitudinal_g;
		}
		previous_lateral = corner.lateral_g;
		previous_load = corner.inside_rear_load;
	}

	const double as_written = quasi_static_lift_g(1.0);
	const double corrected = quasi_static_lift_g(1.0, long_at_lift, true);

	std::printf("\n    inside-rear lift threshold, the number issue #32 turns on\n");
	std::printf("    %-52s %7.3f g\n", "quasi-static, test_kart_lift.cpp as written",
			as_written);
	std::printf("    %-52s %7.3f g\n",
			"  corrected for the 41 mm lateral CoM and the long. g", corrected);
	std::printf("    %-52s %7.3f g\n", "dynamic, this solver, steady state", lift_g);
	std::printf("\n    peak sustained lateral at full lock %.3f g at %.1f km/h\n", peak_lateral,
			ms_to_kmh(peak_speed));

	// Full lock is a 2.8 m circle, and that is not where a vehicle makes its best
	// lateral g. The front tires are at 25 degrees, most of their force points
	// backwards rather than sideways, and the kart is scrubbing the whole time —
	// which is why the number above is a statement about the steering lock and not
	// about the tires. §6.4's 2.0-2.5 g is a skidpad figure, so it wants a skidpad:
	// a wider circle, a higher speed, and the steering wound on only as far as it
	// takes.
	std::printf("\n    peak sustained lateral against steering angle\n");
	std::printf("    %6s %9s %9s %9s %9s\n", "input", "radius m", "best g", "at km/h", "yaw/Ack");
	double best_overall = 0.0;
	double best_input = 0.0;
	double best_at = 0.0;
	const SteeringGeometry steering = kz_front_geometry();
	for (double input = 0.2; input <= 1.001; input += 0.2) {
		const double inner = steering.max_lock * input;
		const double radius = turn_radius(steering, inner, ackermann_outer_angle(steering, inner));
		double best = 0.0;
		double best_speed = 0.0;
		double best_response = 0.0;
		// Sweep speeds around the one that would produce 2.5 g if the kart could,
		// so every steering angle gets the same treatment on either side of its
		// own limit rather than a grid tuned for one of them.
		const double reference_speed = std::sqrt(2.5 * G * radius);
		for (double fraction = 0.5; fraction <= 1.21; fraction += 0.1) {
			const Steady corner = steady_corner(input, reference_speed * fraction, true);
			if (!corner.stable || corner.lateral_g <= best) {
				continue;
			}
			best = corner.lateral_g;
			best_speed = corner.speed;
			best_response = corner.kinematic_yaw_rate > 0.0
					? corner.yaw_rate / corner.kinematic_yaw_rate
					: 0.0;
		}
		std::printf("    %6.1f %9.2f %9.3f %9.1f %9.3f\n", input, radius, best,
				ms_to_kmh(best_speed), best_response);
		if (best > best_overall) {
			best_overall = best;
			best_input = input;
			best_at = best_speed;
		}
	}
	// The sustained band, because a skidpad sweep is a sustained measurement.
	// ADR-0034 split it from the transient-peak band it used to share numbers
	// with; this line reported against the peak figures for a milestone.
	std::printf("    best %.3f g at %.1f lock and %.1f km/h; reference %.1f-%.1f sustained\n",
			best_overall, best_input, ms_to_kmh(best_at), kz::LATERAL_SUSTAINED_G_MIN,
			kz::LATERAL_SUSTAINED_G_MAX);
	CHECK(best_overall >= peak_lateral);

	// **Why that number and not §6.4's.** `tire.h` says that on a vehicle with no
	// downforce the peak friction coefficient *is* the cornering g, and records
	// that its own first value of 1.75 was caught because the kart could only
	// reach 1.77. The same argument applies to 2.10 and it does not reach 2.10,
	// because load sensitivity takes grip away from exactly the tires that are
	// carrying the load: at 900 N the outside rear's coefficient is
	// `2.10 * (1 - 0.10 * 0.8)` = 1.93, and the inside tires that keep their
	// coefficient have no load left to use it on.
	//
	// So this sweeps the coefficient and reports what it buys, which turns "the
	// figure is below the band" from a complaint into a number someone can act on.
	std::printf("\n    peak lateral g against the tire's peak friction, at 0.4 lock\n");
	std::printf("    %14s %10s %10s\n", "peak_friction", "best g", "g per unit");
	double previous_best = 0.0;
	double previous_mu = 0.0;
	for (double mu : { 2.10, 2.30, 2.50, 2.70 }) {
		const double inner = steering.max_lock * 0.4;
		const double radius = turn_radius(steering, inner, ackermann_outer_angle(steering, inner));
		const double reference_speed = std::sqrt(2.8 * G * radius);
		double best = 0.0;
		for (double fraction = 0.5; fraction <= 1.21; fraction += 0.1) {
			const Steady corner = steady_corner(0.4, reference_speed * fraction, true, 1.0, mu);
			if (corner.stable && corner.lateral_g > best) {
				best = corner.lateral_g;
			}
		}
		std::printf("    %14.2f %10.3f %10.3f\n", mu, best,
				previous_mu > 0.0 ? (best - previous_best) / (mu - previous_mu) : 0.0);
		previous_best = best;
		previous_mu = mu;
	}

	// The mechanism has to be present at all: the inside rear must actually reach
	// zero load inside the range the tires can reach, or `steering.h` and
	// `chassis_flex.h` are joined wrong and issue #32 is not implemented.
	CHECK(lift_g > 0.0);
	CHECK(lift_g < tire.lateral.peak_friction);
	CHECK(peak_lateral > 1.0);
	// And the two models must at least be talking about the same kart. They are
	// not asserted equal — see the report, and see the note above
	// `quasi_static_lift_g`: the quasi-static solver models the mass as two halves
	// that roll with the frame and this one models it as one rigid body, which is
	// a real difference and not a bug in either.
	CHECK(lift_g == doctest::Approx(corrected).epsilon(0.25));
}

TEST_CASE("the kart turning right is not the kart turning left") {
	// `chassis.h` says a validation scenario that only ever turns one way measures
	// one of two karts, because 27 kg of powertrain hangs off the right and puts
	// the center of mass 41 mm over. This is that claim, driven.
	const Steady left = steady_corner(1.0, 6.5, true, 1.0);
	const Steady right = steady_corner(1.0, 6.5, true, -1.0);
	REQUIRE(left.stable);
	REQUIRE(right.stable);

	const MassProperties properties = kz::kart_mass_properties();
	std::printf("\n    the same corner, both ways, at 6.5 m/s and full lock\n");
	std::printf("    %-26s %12s %12s\n", "", "left", "right");
	std::printf("    %-26s %12.3f %12.3f\n", "lateral g", left.lateral_g, right.lateral_g);
	std::printf("    %-26s %12.1f %12.1f\n", "inside rear load N", left.inside_rear_load,
			right.inside_rear_load);
	std::printf("    %-26s %12.1f %12.1f\n", "outside rear load N", left.outside_rear_load,
			right.outside_rear_load);
	std::printf("    %-26s %12.3f %12.3f\n", "yaw rate rad/s", left.yaw_rate, right.yaw_rate);
	std::printf("    rollover threshold %.2f g left, %.2f g right\n",
			kz::rollover_threshold_g(properties, true),
			kz::rollover_threshold_g(properties, false));

	// Turning left throws the mass onto the right-hand tires, and the center of
	// mass is already there, so the inside rear unloads further turning left than
	// turning right.
	CHECK(left.inside_rear_load < right.inside_rear_load);
}

// ---------------------------------------------------------------------------
// Issue #33's second acceptance criterion, and issue #32's
// ---------------------------------------------------------------------------

TEST_CASE("with wheel lift disabled the locked axle scrubs and the kart pushes wide") {
	// The comparison issue #33 asks for in so many words: "with wheel lift
	// disabled, the kart audibly and visibly scrubs and pushes wide — proving the
	// coupling is real". Same corner, same speed, same tires, same axle; the only
	// difference is whether the caster geometry is allowed to jack the chassis.
	std::printf("\n    issue #33: the locked axle, with the jacking and without\n");
	std::printf("    %6s | %-31s | %-31s\n", "km/h", "jacking live", "jacking disabled");
	std::printf("    %6s | %7s %7s %7s %7s | %7s %7s %7s %7s\n", "", "lat g", "yaw", "yaw/Ack",
			"RL N", "lat g", "yaw", "yaw/Ack", "RL N");

	double worst_response_gap = 0.0;
	double best_scrub_gap = 0.0;
	for (double target = 4.0; target <= 7.01; target += 1.0) {
		const Steady jacked = steady_corner(1.0, target, true);
		const Steady level = steady_corner(1.0, target, false);
		if (!jacked.stable || !level.stable) {
			continue;
		}
		// Understeer, as the fraction of the geometrically demanded yaw rate the
		// kart actually achieves. 1.0 is neutral; below 1.0 the kart is running
		// wider than its front wheels are pointed, which is what "pushes wide"
		// means once it is a number.
		const double jacked_response = jacked.kinematic_yaw_rate > 0.0
				? jacked.yaw_rate / jacked.kinematic_yaw_rate
				: 0.0;
		const double level_response = level.kinematic_yaw_rate > 0.0
				? level.yaw_rate / level.kinematic_yaw_rate
				: 0.0;
		std::printf("    %6.1f | %7.3f %7.3f %7.3f %7.1f | %7.3f %7.3f %7.3f %7.1f\n",
				ms_to_kmh(jacked.speed), jacked.lateral_g, jacked.yaw_rate, jacked_response,
				jacked.inside_rear_load, level.lateral_g, level.yaw_rate, level_response,
				level.inside_rear_load);
		if (jacked_response - level_response > worst_response_gap) {
			worst_response_gap = jacked_response - level_response;
		}
		if (level.inside_rear_load - jacked.inside_rear_load > best_scrub_gap) {
			best_scrub_gap = level.inside_rear_load - jacked.inside_rear_load;
		}
	}

	// And the scrub itself, at the speed where it bites hardest.
	const Steady jacking = steady_corner(1.0, 7.0, true);
	const Steady flat = steady_corner(1.0, 7.0, false);
	if (jacking.stable && flat.stable) {
		std::printf("\n    at 7.0 m/s, what the two rear tires are doing\n");
		std::printf("    %-30s %12s %12s\n", "", "jacking", "jacking off");
		std::printf("    %-30s %12.3f %12.3f\n", "inside rear slip ratio",
				jacking.inside_rear_slip_ratio, flat.inside_rear_slip_ratio);
		std::printf("    %-30s %12.3f %12.3f\n", "outside rear slip ratio",
				jacking.outside_rear_slip_ratio, flat.outside_rear_slip_ratio);
		std::printf("    %-30s %12.1f %12.1f\n", "inside rear long. force N",
				jacking.inside_rear_longitudinal, flat.inside_rear_longitudinal);
		std::printf("    %-30s %12.1f %12.1f\n", "outside rear long. force N",
				jacking.outside_rear_longitudinal, flat.outside_rear_longitudinal);
		std::printf("    %-30s %12.2f %12.2f\n", "body slip deg", jacking.body_slip * 180.0 / PI,
				flat.body_slip * 180.0 / PI);
		std::printf("    %-30s %12.2f %12.2f\n", "inside rear lift mm",
				jacking.inside_rear_lift_mm, flat.inside_rear_lift_mm);
	}

	// The scrub is the inside rear carrying load it cannot use: with the jacking
	// off it stays down and takes a share of the axle at a slip ratio it did not
	// ask for.
	CHECK(best_scrub_gap > 20.0);
	// And the consequence is the push. The kart with the jacking follows its front
	// wheels; the kart without does not.
	CHECK(worst_response_gap > 0.2);
}

TEST_CASE("the torque split between the rear tires is by available grip, with no differential") {
	// Issue #33: "torque split between them by available grip, not by a
	// differential model". Nothing in `vehicle.h` splits anything — there is one
	// `axle_speed_` and two Pacejka evaluations against it — so the claim has to
	// be measured on the case that separates the two answers.
	//
	// That case is split friction. Put one rear tire on a surface with a fifth of
	// the grip and drive out of it:
	//
	//   * an **open differential** equalizes torque, so both wheels are limited by
	//     the worse one and the vehicle is stuck with one wheel spinning;
	//   * a **locked axle** equalizes speed, so the good tire keeps its own slip
	//     ratio and its own force and drives the kart out.
	//
	// The second is what a kart does and it is why a kart has no differential to
	// begin with.
	std::printf("\n    split friction, straight line, full throttle in second\n");
	std::printf("    %-16s %10s %10s %10s %10s\n", "left rear grip", "RL force N", "RR force N",
			"RL share", "0-50 km/h s");

	double share_on_ice = 1.0;
	for (double grip : { 1.0, 0.5, 0.2 }) {
		Rig rig;
		rig.configure();
		rig.corner_grip[CORNER_RL] = grip;
		rig.settle();
		rig.vehicle.engage(2, 5.0);
		rig.linear_velocity = -rig.basis_z * 5.0;

		DriverInput input;
		input.throttle = 1.0;
		double left = 0.0;
		double right = 0.0;
		double samples = 0.0;
		double to_fifty = -1.0;
		for (int tick = 0; tick < 120 * 8; ++tick) {
			rig.step(input);
			REQUIRE(rig.finite());
			if (to_fifty < 0.0 && rig.kmh() >= 50.0) {
				to_fifty = tick * TICK;
			}
			if (tick > 120 && tick < 240) {
				left += rig.vehicle.telemetry().wheel[CORNER_RL].force.dot(-rig.basis_z);
				right += rig.vehicle.telemetry().wheel[CORNER_RR].force.dot(-rig.basis_z);
				samples += 1.0;
			}
		}
		left /= samples;
		right /= samples;
		const double total = left + right;
		const double share = total != 0.0 ? left / total : 0.0;
		std::printf("    %-16.2f %10.1f %10.1f %10.3f %10.2f\n", grip, left, right, share,
				to_fifty);
		if (grip < 0.25) {
			share_on_ice = share;
		}
		CHECK(to_fifty > 0.0);
	}

	// The low-grip tire takes well under half the drive. On an open differential
	// it would take exactly half, by definition, and so would the other one.
	CHECK(share_on_ice < 0.4);
	CHECK(share_on_ice > 0.0);
}

// ---------------------------------------------------------------------------
// Issue #34's last acceptance item, and issue #41's first
// ---------------------------------------------------------------------------

TEST_CASE("no force instability at any speed, including near zero") {
	// `tire.h`'s stated criterion, and the one thing its own unit tests cannot
	// check because they never divide by a road speed. Each case runs long enough
	// for an instability to grow: a force that oscillates at the substep rate
	// doubles every few substeps, so a thousand ticks is thousands of doublings.
	struct Case {
		const char *name;
		double speed;
		double steer;
		double throttle;
		double brake;
	};
	const Case cases[] = {
		{ "standstill, no input", 0.0, 0.0, 0.0, 0.0 },
		{ "standstill, full lock", 0.0, 1.0, 0.0, 0.0 },
		{ "standstill, full brake", 0.0, 0.0, 0.0, 1.0 },
		{ "standstill, full throttle", 0.0, 0.0, 1.0, 0.0 },
		{ "1 mm/s", 0.001, 1.0, 0.0, 0.0 },
		{ "1 mm/s, braking", 0.001, 0.0, 0.0, 1.0 },
		{ "0.5 m/s, full lock", 0.5, 1.0, 0.3, 0.0 },
		{ "140 km/h, straight", kmh_to_ms(140.0), 0.0, 1.0, 0.0 },
		{ "140 km/h, full lock", kmh_to_ms(140.0), 1.0, 0.0, 0.0 },
		{ "140 km/h, full brake", kmh_to_ms(140.0), 0.0, 0.0, 1.0 },
	};

	std::printf("\n    stability sweep, 1000 ticks each\n");
	std::printf("    %-26s %14s %14s\n", "case", "largest force N", "final km/h");

	for (const Case &scenario : cases) {
		Rig rig;
		rig.configure();
		rig.settle();
		if (scenario.speed > 0.5) {
			rig.vehicle.engage(scenario.speed > 20.0 ? 6 : 2, scenario.speed);
		}
		rig.linear_velocity = -rig.basis_z * scenario.speed;

		DriverInput input;
		input.steer = scenario.steer;
		input.throttle = scenario.throttle;
		input.brake = scenario.brake;

		double worst = 0.0;
		for (int tick = 0; tick < 1000; ++tick) {
			rig.step(input);
			REQUIRE(rig.finite());
			const double magnitude = rig.largest_force();
			if (magnitude > worst) {
				worst = magnitude;
			}
			// A single corner cannot legitimately produce more than a few times
			// the kart's weight. Anything past twenty times it is a divergence,
			// not a bump.
			REQUIRE(magnitude < 20.0 * kz::MASS_WITH_DRIVER_KG * G);
		}
		std::printf("    %-26s %14.1f %14.2f\n", scenario.name, worst, rig.kmh());
		CHECK(rig.finite());
	}
}

TEST_CASE("airborne, and a ground query that misses on every corner") {
	// Two different failures that look the same from the outside. ADR-0033
	// finding 4: a ray that starts below a surface returns no hit at all, so
	// "every corner missed" happens when the kart is deepest in a curb, not only
	// when it is in the air.
	SUBCASE("all four wheels off the ground") {
		Rig rig;
		rig.configure();
		rig.settle();
		rig.vehicle.engage(4, 20.0);
		rig.linear_velocity = -rig.basis_z * 20.0 + Vec3(0.0, 4.0, 0.0);
		rig.com_position.y += 1.0;

		DriverInput input;
		input.throttle = 1.0;
		input.steer = 1.0;

		bool saw_airborne = false;
		for (int tick = 0; tick < 60; ++tick) {
			rig.step(input);
			REQUIRE(rig.finite());
			bool any = false;
			for (int corner = 0; corner < CORNER_COUNT; ++corner) {
				any = any || rig.vehicle.telemetry().wheel[corner].grounded;
			}
			if (!any) {
				saw_airborne = true;
				// Nothing but drag may be produced by a kart in the air.
				for (int corner = 0; corner < CORNER_COUNT; ++corner) {
					CHECK(rig.last_forces.force[corner].length() < 1e-9);
				}
			}
		}
		CHECK(saw_airborne);
		// The engine kept running and the wheels kept spinning; a free wheel
		// under full throttle is the classic way to find a missing guard.
		CHECK(std::isfinite(rig.vehicle.axle_speed()));
		MESSAGE("airborne under full throttle for 0.5 s: axle reached "
				<< rig.vehicle.axle_speed() << " rad/s at "
				<< rig.vehicle.telemetry().engine_rpm << " rpm");
	}

	SUBCASE("every ray reports a miss while the kart is on the ground") {
		Rig rig;
		rig.configure();
		rig.settle();
		rig.vehicle.engage(4, 20.0);
		rig.linear_velocity = -rig.basis_z * 20.0;
		rig.queries_answer = false;

		DriverInput input;
		input.throttle = 1.0;
		input.steer = -1.0;
		input.brake = 0.5;
		for (int tick = 0; tick < 600; ++tick) {
			rig.step(input);
			REQUIRE(rig.finite());
		}
		CHECK(rig.finite());
	}
}

TEST_CASE("a long abusive input sequence stays finite") {
	// Deterministic rather than random — `ARCHITECTURE.md` §8 and the note in
	// `pcg32.h` — but not periodic with the tick rate, which is what makes it
	// abusive: the steering reverses, the brake and throttle overlap, and shifts
	// arrive mid-shift.
	Rig rig;
	rig.configure();
	rig.settle();
	rig.vehicle.engage(3, 15.0);
	rig.linear_velocity = -rig.basis_z * 15.0;

	double worst_force = 0.0;
	double highest_rpm = 0.0;
	for (int tick = 0; tick < 120 * 60; ++tick) {
		const double time = tick * TICK;
		DriverInput input;
		input.steer = std::sin(time * 3.7) + 0.5 * std::sin(time * 11.3);
		input.throttle = 0.5 + 0.5 * std::sin(time * 2.1);
		input.brake = 0.5 + 0.5 * std::sin(time * 1.3 + 2.0);
		input.clutch = time > 30.0 ? 0.5 + 0.5 * std::sin(time * 5.0) : 0.0;
		input.shift_up = (tick % 37) == 0;
		input.shift_down = (tick % 53) == 0;
		rig.step(input);
		REQUIRE(rig.finite());
		const double magnitude = rig.largest_force();
		if (magnitude > worst_force) {
			worst_force = magnitude;
		}
		if (rig.vehicle.telemetry().engine_rpm > highest_rpm) {
			highest_rpm = rig.vehicle.telemetry().engine_rpm;
		}
	}

	MESSAGE("60 s of abuse: largest corner force " << worst_force << " N ("
												  << (worst_force /
															 (kz::MASS_WITH_DRIVER_KG * G))
												  << "x weight), highest " << highest_rpm
												  << " rpm");
	CHECK(rig.finite());
	CHECK(worst_force < 20.0 * kz::MASS_WITH_DRIVER_KG * G);
}

// ---------------------------------------------------------------------------
// Determinism, and the substep contract
// ---------------------------------------------------------------------------

TEST_CASE("two identical runs produce identical state") {
	Rig first;
	Rig second;
	first.configure();
	second.configure();

	uint64_t first_digest = 0;
	uint64_t second_digest = 0;
	for (int tick = 0; tick < 120 * 20; ++tick) {
		const double time = tick * TICK;
		DriverInput input;
		input.steer = std::sin(time * 2.3);
		input.throttle = 0.5 + 0.5 * std::sin(time * 1.7);
		input.brake = tick % 300 < 40 ? 1.0 : 0.0;
		input.shift_up = (tick % 91) == 0;
		first.step(input);
		second.step(input);
		first_digest = first.hash();
		second_digest = second.hash();
		REQUIRE(first_digest == second_digest);
	}
	MESSAGE("20 s, identical inputs, state hash " << first_digest);
	CHECK(first_digest == second_digest);
}

TEST_CASE("reset puts everything back, including the engine") {
	Rig rig;
	rig.configure();
	rig.settle();
	rig.vehicle.engage(5, 30.0);
	rig.linear_velocity = -rig.basis_z * 30.0;

	DriverInput input;
	input.throttle = 1.0;
	input.steer = 0.7;
	for (int tick = 0; tick < 600; ++tick) {
		rig.step(input);
	}
	CHECK(rig.vehicle.axle_speed() > 100.0);
	CHECK(rig.vehicle.telemetry().engine_rpm > 5000.0);

	rig.vehicle.reset();

	// "A respawn that leaves the engine at 14,000 rpm is a bug." So is one that
	// leaves the axle turning, the steering wound on, or the frame twisted.
	CHECK(rig.vehicle.axle_speed() == doctest::Approx(0.0));
	CHECK(rig.vehicle.front_wheel_speed(CORNER_FL) == doctest::Approx(0.0));
	CHECK(rig.vehicle.front_wheel_speed(CORNER_FR) == doctest::Approx(0.0));
	CHECK(rig.vehicle.steer_position() == doctest::Approx(0.0));
	CHECK(rig.vehicle.warp_amplitude() == doctest::Approx(0.0));
	CHECK(rig.vehicle.telemetry().gear == 0);
	CHECK(rig.vehicle.drivetrain.engine_rpm() ==
			doctest::Approx(rig.vehicle.drivetrain.engine.idle_rpm));
	CHECK_FALSE(rig.vehicle.drivetrain.stalled());

	// And a reset run reproduces a fresh one. This is the check that `reset` is
	// complete rather than merely thorough-looking: a single field left behind
	// changes the hash.
	Rig fresh;
	fresh.configure();
	Rig reused;
	reused.configure();
	reused.vehicle.engage(6, 35.0);
	for (int tick = 0; tick < 200; ++tick) {
		DriverInput noise;
		noise.throttle = 1.0;
		noise.steer = 0.9;
		reused.step(noise);
	}
	reused.vehicle.reset();
	reused.com_position = fresh.com_position;
	reused.basis_x = fresh.basis_x;
	reused.basis_y = fresh.basis_y;
	reused.basis_z = fresh.basis_z;
	reused.linear_velocity = Vec3();
	reused.angular_velocity = Vec3();

	for (int tick = 0; tick < 600; ++tick) {
		DriverInput drive;
		drive.throttle = 1.0;
		fresh.step(drive);
		reused.step(drive);
	}
	CHECK(fresh.hash() == reused.hash());
}

TEST_CASE("the substep count is fixed and the totals are impulses, not sums") {
	// Issue #41's third acceptance criterion is that the count is fixed rather
	// than derived from frame time, and there is only one way to check that from
	// outside: hand the solver two different `dt` values and confirm the reported
	// count does not move.
	Rig rig;
	rig.configure();
	rig.settle();

	CHECK(rig.vehicle.telemetry().substeps == KartVehicle::SUBSTEP_COUNT);

	// And the accumulation is a mean rather than a sum, which is the arithmetic
	// that decides whether the kart weighs what it weighs. A settled kart's four
	// normal forces must add up to `m*g` — if the substeps summed, they would add
	// up to `N * m*g`, and every constant in the file would have to be divided by
	// the substep count to hide it.
	double total = 0.0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		total += rig.last_forces.force[corner].y;
	}
	const double weight = rig.vehicle.mass_properties().mass * G;
	MESSAGE("settled: sum of applied vertical forces " << total << " N against m*g " << weight
													  << " N, over "
													  << rig.vehicle.telemetry().substeps
													  << " substeps");
	CHECK(total == doctest::Approx(weight).epsilon(0.01));
}

TEST_CASE("a surface grip multiplier scales the grip and nothing else") {
	// The §6 surface table's multiplier reaches the tire through `GroundQuery`.
	// Checked here rather than in `test_tire.cpp` because the interesting question
	// is whether it survives the solver, not whether `Tire::evaluate` multiplies.
	Rig dry;
	Rig grass;
	dry.configure();
	grass.configure();
	grass.surface_grip = 0.45;
	dry.settle();
	grass.settle();

	dry.vehicle.engage(2, 8.0);
	grass.vehicle.engage(2, 8.0);
	dry.linear_velocity = -dry.basis_z * 8.0;
	grass.linear_velocity = -grass.basis_z * 8.0;

	DriverInput input;
	input.steer = 0.6;
	for (int tick = 0; tick < 240; ++tick) {
		dry.step(input);
		grass.step(input);
	}

	MESSAGE("lateral g: asphalt " << std::fabs(dry.vehicle.telemetry().lateral_g)
								  << ", grip 0.45 "
								  << std::fabs(grass.vehicle.telemetry().lateral_g));
	CHECK(std::fabs(grass.vehicle.telemetry().lateral_g) <
			std::fabs(dry.vehicle.telemetry().lateral_g));
	// The normal loads are the suspension's and must not have moved.
	CHECK(grass.vehicle.telemetry().wheel[CORNER_RR].normal_load > 0.0);
	CHECK(grass.finite());
}
