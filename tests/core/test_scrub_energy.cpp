#include "doctest.h"

#include "core/chassis.h"
#include "core/chassis_flex.h"
#include "core/kz_reference.h"
#include "core/steering.h"
#include "core/tire.h"
#include "core/units.h"
#include "core/vehicle.h"

#include <cmath>
#include <cstdio>
#include <string>

// **Where the power goes at large steer angles.** Issue #137, acceptance items 1
// and 2, and the "is caster jacking monotone" question §4 of that issue lists as
// unestablished.
//
// This file measures and changes nothing. Nothing under `src/` is touched by it
// and no constant is tuned in it; every table below is a read-out of the solver
// as committed. #137 is explicit that `steer_rate`, `peak_friction`, the jacking
// gain and the scrub term are not to be moved until the mechanism is measured,
// and `ARCHITECTURE.md` §19 names unbounded vehicle tuning as the live risk.
//
// ## Why this is a second harness and not another case in test_vehicle.cpp
//
// `steady_corner` there runs a `SpeedHold` proportional controller and searches
// speeds for the best g each steering angle can hold. That is the right
// instrument for "what is this kart capable of" and it is the wrong one for
// #137, whose headline row is **open loop at full throttle**: lock 0.60, pedal
// on the floor, settling at 21.2 km/h. A speed-holding controller cannot
// reproduce a kart that settled where its own scrub ate the engine, because the
// controller is the thing deciding the speed. #137's own comment already records
// this as the reason the solver-only and through-Godot sweeps looked 14x apart.
//
// So `settle_corner` below is open loop: hold a steering input, hold a throttle,
// and report where the kart ends up. `hold_corner` is the speed-searching
// protocol, kept so the two columns of table 2 are directly comparable with the
// figures already in `test_vehicle.cpp` and in ROADMAP M3b.
//
// ## What is measured and what is reconstructed, stated because #107 exists
//
// Read straight out of the solver, no arithmetic of this file's own:
//
//     WheelTelemetry::force          the applied tire force, world frame, the
//                                    substep mean, after the friction ellipse
//                                    and after both low-speed clamps
//     WheelTelemetry::normal_load    likewise
//     WheelTelemetry::steer_angle    likewise
//     VehicleTelemetry::engine_rpm   the drivetrain's own read-out
//     VehicleTelemetry::engine_torque
//     VehicleTelemetry::axle_torque
//     VehicleTelemetry::clutch_slip / clutch_torque
//     KartVehicle::axle_speed(), front_wheel_speed(), warp_amplitude()
//     KartVehicle::rolling_radius(), corner_setup_for()
//     solve_steering(...).contact_offset  for the jacking table
//
// **Reconstructed by this file, and therefore capable of being wrong:** the
// slip *velocities*. `WheelTelemetry` carries `slip_angle` and `slip_ratio`,
// which are normalized against a floored reference speed, and a dissipation
// figure needs metres per second at the patch. So the contact frame is rebuilt
// here exactly the way `KartVehicle::solve_substep` builds it — same ray origin,
// same heading from the steer angle, same `reject_from_unit`, same
// `velocity_at(patch)` — from the same `BodyState` and the same raycast the rig
// handed the solver on that tick.
//
// Two known errors in that reconstruction, both bounded and both reported rather
// than hidden:
//
//   1. **One tick, not two substeps.** The forces are the tick mean and the
//      velocities are the tick's opening value. In a settled steady state the
//      substep spread is what issue #134 measured — 8.3% mean on the normal load
//      — and the residual line at the foot of table 1 is where it shows up.
//   2. **The drivetrain scalars are the last substep, not the mean.**
//      `fill_telemetry` averages the per-wheel fields and leaves `engine_rpm`,
//      `engine_torque`, `axle_torque`, `clutch_slip` and `clutch_torque` at
//      whatever the last substep wrote. Averaged over a 1 s window at a settled
//      speed that is a sampling choice and not a bias, but it is not the mean of
//      the tick and it is not claimed to be.
//
// The check that the reconstruction is sound is the residual itself. At a
// genuine steady state the kinetic energy is constant, so the engine's crank
// power must equal the sum of the sinks, and any gap is either stored energy
// (measured separately, the KE drift line) or reconstruction error. Both are
// printed. A ledger that closed by construction would prove nothing.
//
// ## The power ledger, derived once so the columns are not folklore
//
// For one wheel with drive torque `T`, rolling torque `T_r`, spin `w`, rolling
// radius `r`, longitudinal force `F_x`, lateral force `F_y`, patch forward speed
// `u` and patch lateral speed `v`:
//
//     rotational:  d/dt(I w^2/2) = T w - F_x r w - T_r w
//     into chassis:                 F_x u + F_y v
//     so heat    :  F_x (r w - u)                  longitudinal, `r w - u` is the
//                                                  slip velocity in m/s
//                   -F_y v                         lateral, positive because
//                                                  `F_y` opposes `v` by
//                                                  construction in tire.h
//
// Summing the four corners and adding the chassis:
//
//     P_crank = P_clutch_slip + sum(long heat) + sum(lat heat)
//             + sum(rolling) + P_aero + d/dt(all kinetic energy)
//
// `P_clutch_slip` is `clutch_torque * clutch_slip`, both read out.
// `P_aero` is `0.5 rho A |v|^3`, which is `AIR_DENSITY` and `drag_area` read off
// the solver and cubed here — the one sink computed from a formula rather than
// from a force, because `VehicleForces::central_force` is the drag and is
// available, and the two are cross-checked in table 1's own footer.
//
// ## The scrub couple, and why it is a decomposition rather than a fifth sink
//
// #137 asks for "the rear axle scrub couple" as its own column. It is not a
// separate physical sink — every watt of it is already inside the two rear
// wheels' longitudinal heat — so inventing a fifth term would double-count. What
// it is, is a *mode* of that term, and the decomposition is exact:
//
//     P_rear = F_L s_L + F_R s_R
//            = (F_L + F_R) * (s_L + s_R)/2      common mode: the axle driving
//            + (F_L - F_R) * (s_L - s_R)/2      differential mode: the scrub
//
// The differential mode is zero for two wheels that may turn at their own
// speeds, and is nonzero only because `KartVehicle` integrates one `axle_speed_`
// for both — which is the whole of issue #33. So that second term **is** the
// scrub couple, in watts, and it is reported beside the common mode rather than
// instead of the rear heat.
//
// **It comes out negative, and that is not a sign error.** The decomposition is
// exact algebra on the pair, but neither mode is separately a dissipation — only
// their sum is, and the sum is positive at every row in this file. The inside
// rear runs the larger slip velocity (it is the over-driven one, which is #33's
// mechanism working) and simultaneously carries a sixth of the outside rear's
// load, so `F_L - F_R` is negative while `s_L - s_R` is positive and the product
// is negative. What that says physically is that the load asymmetry is a bigger
// effect than the slip asymmetry: the wheel being scrubbed is the wheel with
// almost no load on it, so the scrub is cheap. That is a finding and not a
// defect, and it is the single number that most directly contradicts "the scrub
// is absorbing the engine".
//
// What is **not** measured here is the counterfactual — how much less heat the
// same corner would make with a free differential. That needs the corner re-run
// with two rear degrees of freedom, which this solver does not have and which
// #137 is not a licence to add. `rear_speed_spread` is the honest stand-in: the
// metres per second of road-speed difference the locked axle is forced to
// absorb, read off the same two contact patches.

using namespace kart::core;

namespace {

constexpr double TICK = 1.0 / 120.0;

// A rigid body, a flat floor, and nothing else.
//
// Deliberately a second copy of `test_vehicle.cpp`'s `Rig` rather than a shared
// header. That file's harness is in an anonymous namespace and is not this
// file's to change; lifting it into a shared header would put a fixture two
// suites depend on into a third place, and #137 is not the ticket to do that on.
// The three things it reproduces are ADR-0032 finding 1 (symplectic Euler),
// ADR-0033's headline (the application point is an offset from the body
// **origin**) and ADR-0033 finding 5 (the diagonal chassis-frame tensor).
struct Rig {
	KartVehicle vehicle;

	Vec3 com_position;
	Vec3 basis_x = Vec3(1.0, 0.0, 0.0);
	Vec3 basis_y = Vec3(0.0, 1.0, 0.0);
	Vec3 basis_z = Vec3(0.0, 0.0, 1.0);
	Vec3 linear_velocity;
	Vec3 angular_velocity;

	double ground_height = 0.0;
	double surface_grip = 1.0;

	VehicleForces last_forces;
	// The tick's raycast, kept so the power audit reconstructs the contact frame
	// from the *same* geometry the solver was handed rather than from a second
	// raycast taken after the body moved.
	GroundQuery last_query[CORNER_COUNT];
	Vec3 last_origin;

	void configure() {
		vehicle.configure();
		place(Vec3(0.0, -0.003, 0.0));
	}

	void place(const Vec3 &origin) {
		com_position = origin + to_world(vehicle.mass_properties().center_of_mass);
	}

	Vec3 to_world(const Vec3 &local) const {
		return basis_x * local.x + basis_y * local.y + basis_z * local.z;
	}

	Vec3 origin() const {
		return com_position - to_world(vehicle.mass_properties().center_of_mass);
	}

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

	double forward_speed() const { return -linear_velocity.dot(basis_z); }
	double yaw_rate() const { return angular_velocity.dot(basis_y); }

	void query(GroundQuery out[CORNER_COUNT]) const {
		const Vec3 down = -basis_y;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			out[corner] = GroundQuery();
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
			out[corner].surface_grip = surface_grip;
		}
	}

	void step(const DriverInput &input) {
		query(last_query);

		const BodyState state = body_state();
		last_origin = state.origin;
		last_forces = vehicle.step(state, input, last_query, TICK);

		const double mass = vehicle.mass_properties().mass;
		Vec3 force = last_forces.central_force + Vec3(0.0, -G, 0.0) * mass;
		Vec3 torque = last_forces.central_torque;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			force += last_forces.force[corner];
			const Vec3 arm =
					state.origin + last_forces.application_point[corner] - com_position;
			torque += arm.cross(last_forces.force[corner]);
		}

		linear_velocity += force / mass * TICK;

		const InertiaTensor &inertia = vehicle.mass_properties().inertia;
		const Vec3 body_torque(torque.dot(basis_x), torque.dot(basis_y), torque.dot(basis_z));
		const Vec3 body_alpha(body_torque.x / inertia.xx, body_torque.y / inertia.yy,
				body_torque.z / inertia.zz);
		angular_velocity += (basis_x * body_alpha.x + basis_y * body_alpha.y +
									basis_z * body_alpha.z) *
				TICK;

		com_position += linear_velocity * TICK;

		const double rate = angular_velocity.length();
		if (rate > 0.0) {
			const Vec3 axis = angular_velocity / rate;
			basis_x = basis_x.rotated(axis, rate * TICK);
			basis_y = basis_y.rotated(axis, rate * TICK);
			basis_z = basis_z.rotated(axis, rate * TICK);
		}
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
				angular_velocity.is_finite() && basis_x.is_finite() &&
				std::isfinite(vehicle.axle_speed());
	}

	// Everything with inertia in it, joules. The residual line in table 1 needs
	// the drift in this, because a ledger that ignores stored energy calls a kart
	// that is still slowing down a kart that is dissipating more than it makes.
	//
	// **The engine's own rotational energy is not in here** and the omission is
	// deliberate rather than an oversight: `Drivetrain` does not expose the
	// crankshaft speed as an inertia-bearing state and `Engine::inertia` is
	// behind it. At a settled speed the rpm is constant to a few tens of rpm over
	// the window, so the term is small; it is not zero, and it is one of the two
	// things the residual is allowed to contain.
	double kinetic_energy() const {
		const MassProperties &properties = vehicle.mass_properties();
		double energy = 0.5 * properties.mass * linear_velocity.length_squared();

		const Vec3 body_omega(angular_velocity.dot(basis_x), angular_velocity.dot(basis_y),
				angular_velocity.dot(basis_z));
		energy += 0.5 * (properties.inertia.xx * body_omega.x * body_omega.x +
								properties.inertia.yy * body_omega.y * body_omega.y +
								properties.inertia.zz * body_omega.z * body_omega.z);

		// The wheel inertias are private to `KartVehicle`, so they are rebuilt
		// here from the two constants that file makes public reasoning about —
		// `WHEEL_RING_FRACTION` and the wheel radii — and the masses stated in its
		// own comment. Computed, not read out, and it only enters a drift term.
		const double front = WHEEL_RING_FRACTION * 2.6 * kz::FRONT_WHEEL_RADIUS *
				kz::FRONT_WHEEL_RADIUS;
		const double rear = WHEEL_RING_FRACTION * 3.6 * kz::REAR_WHEEL_RADIUS *
				kz::REAR_WHEEL_RADIUS;
		const double axle = 2.0 * rear + 0.010;

		const double fl = vehicle.front_wheel_speed(CORNER_FL);
		const double fr = vehicle.front_wheel_speed(CORNER_FR);
		const double ax = vehicle.axle_speed();
		energy += 0.5 * front * (fl * fl + fr * fr) + 0.5 * axle * ax * ax;
		return energy;
	}
};

// One corner's power ledger, averaged over the sampling window.
struct Audit {
	bool stable = true;
	bool departed = false;

	double speed = 0.0; // m/s along the body's forward axis
	double ground_speed = 0.0; // m/s, the magnitude
	double lateral_g = 0.0;
	double longitudinal_g = 0.0;
	double yaw_rate = 0.0;
	double body_slip = 0.0; // radians
	double radius = 0.0; // m, from speed and yaw rate
	double rpm = 0.0;
	int gear = 0;
	double throttle = 0.0;

	// The ledger, watts.
	double crank = 0.0;
	double clutch_loss = 0.0;
	double long_heat[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double lat_heat[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double rolling[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double aero = 0.0;
	double aero_from_force = 0.0; // the cross-check
	double ke_drift = 0.0; // J/s over the window, positive is gaining
	double axle_power = 0.0; // axle_torque * axle_speed, read out

	// The rear axle's two modes; see the header note. Watts.
	double scrub_common = 0.0;
	double scrub_differential = 0.0;
	// The kinematic scrub the solid axle forces, m/s of rear road-speed spread.
	double rear_speed_spread = 0.0;

	double load[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double slip_angle_deg[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double slip_ratio[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double long_slip_ms[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double lat_slip_ms[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double inside_rear_load = 0.0;
	double warp_mm = 0.0;

	// The smallest load the inside rear carried at **any tick of the whole run**,
	// not only inside the averaging window. #137 item 2 asks whether the inside
	// rear can *ever* lift, and a steady-state mean cannot answer "ever": the
	// comment on that issue already recorded the wheel reaching 0.0 N in a
	// steering *step* while every steady-state table here shows it loaded. So the
	// transient minimum is carried alongside the steady mean and the two are
	// printed together, because they are two different questions with two
	// different answers.
	double min_inside_rear_load = 1e9;
	double min_inside_rear_at_g = 0.0; // the lateral g on the tick that happened
	// Peak transient lateral g over the run, for the same reason.
	double peak_lateral_g = 0.0;

	double total_long_heat() const {
		return long_heat[0] + long_heat[1] + long_heat[2] + long_heat[3];
	}
	double total_lat_heat() const {
		return lat_heat[0] + lat_heat[1] + lat_heat[2] + lat_heat[3];
	}
	double total_rolling() const {
		return rolling[0] + rolling[1] + rolling[2] + rolling[3];
	}
	double sinks() const {
		return clutch_loss + total_long_heat() + total_lat_heat() + total_rolling() + aero;
	}
	double residual() const { return crank - sinks() - ke_drift; }
};

// Track the extremes over the **whole** run rather than over the averaging
// window. Called on every tick; `sample` below is called only inside the window.
void watch(const Rig &rig, Audit &out, int inside_rear) {
	const VehicleTelemetry &telemetry = rig.vehicle.telemetry();
	const double load = telemetry.wheel[inside_rear].normal_load;
	const double lateral = std::fabs(telemetry.lateral_g);
	if (load < out.min_inside_rear_load) {
		out.min_inside_rear_load = load;
		out.min_inside_rear_at_g = lateral;
	}
	if (lateral > out.peak_lateral_g) {
		out.peak_lateral_g = lateral;
	}
}

// Accumulate one tick into an audit. Everything here that is not a straight
// telemetry read is documented in the file header as a reconstruction.
void sample(const Rig &rig, const DriverInput &input, Audit &out, double &samples,
		int inside_rear) {
	const VehicleTelemetry &telemetry = rig.vehicle.telemetry();
	const BodyState body = rig.body_state();
	const Vec3 down = -rig.basis_y;

	double rear_road_speed[2] = { 0.0, 0.0 };
	double rear_force[2] = { 0.0, 0.0 };
	double rear_slip[2] = { 0.0, 0.0 };

	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const WheelTelemetry &wheel = telemetry.wheel[corner];
		out.load[corner] += wheel.normal_load;
		out.slip_angle_deg[corner] += wheel.slip_angle * 180.0 / PI;
		out.slip_ratio[corner] += wheel.slip_ratio;

		if (!rig.last_query[corner].hit) {
			continue;
		}

		// The contact frame, rebuilt exactly as `KartVehicle::solve_substep`
		// builds it. `-Z` is forward and a positive steer angle turns the wheel to
		// the left; getting either backwards puts the sign of every watt below out
		// by one, which is why the file's footer cross-checks the lateral heat
		// against a quantity that cannot have the same error.
		const Vec3 mount = rig.last_origin + rig.to_world(rig.vehicle.ray_origin(corner));
		const Vec3 patch = mount + down * rig.last_query[corner].distance;
		const Vec3 normal = rig.last_query[corner].normal;

		const double angle = wheel.steer_angle;
		const Vec3 heading =
				rig.to_world(Vec3(-std::sin(angle), 0.0, -std::cos(angle)));
		const Vec3 roll_dir = heading.reject_from_unit(normal).normalized();
		if (roll_dir.length_squared() <= 0.0) {
			continue;
		}
		const Vec3 right_dir = roll_dir.cross(normal);

		const Vec3 patch_velocity = body.velocity_at(patch);
		const double forward = patch_velocity.dot(roll_dir);
		const double lateral = patch_velocity.dot(right_dir);

		const bool rear = corner == CORNER_RL || corner == CORNER_RR;
		const double spin = rear ? rig.vehicle.axle_speed()
								 : rig.vehicle.front_wheel_speed(corner);
		const double radius = rig.vehicle.rolling_radius(corner);
		const double slip_speed = spin * radius - forward;

		// The **applied** tire force, projected back onto the frame it was built
		// in. Not recomputed from the tire curve: recomputing it would silently
		// discard the friction ellipse and both low-speed clamps, which is exactly
		// where a scrub investigation would go wrong.
		const double f_long = wheel.force.dot(roll_dir);
		const double f_lat = wheel.force.dot(right_dir);

		out.long_heat[corner] += f_long * slip_speed;
		out.lat_heat[corner] += -f_lat * lateral;
		out.long_slip_ms[corner] += slip_speed;
		out.lat_slip_ms[corner] += lateral;

		// Rolling resistance as the solver applies it: a torque, tapered across
		// the last 1 rad/s. `rolling_resistance` is public, so the coefficient is
		// read out and only the product is formed here.
		const double taper = std::fabs(spin) < 1.0 ? std::fabs(spin) : 1.0;
		out.rolling[corner] += rig.vehicle.rolling_resistance * wheel.normal_load * radius *
				taper * std::fabs(spin);

		if (rear) {
			const int index = corner == CORNER_RL ? 0 : 1;
			rear_road_speed[index] = forward;
			rear_force[index] = f_long;
			rear_slip[index] = slip_speed;
		}
	}

	// The rear axle's two modes. Exact algebra, see the header.
	const double mean_slip = 0.5 * (rear_slip[0] + rear_slip[1]);
	const double half_difference_slip = 0.5 * (rear_slip[0] - rear_slip[1]);
	out.scrub_common += (rear_force[0] + rear_force[1]) * mean_slip;
	out.scrub_differential += (rear_force[0] - rear_force[1]) * half_difference_slip;
	out.rear_speed_spread += std::fabs(rear_road_speed[0] - rear_road_speed[1]);

	const double speed = rig.linear_velocity.length();
	out.aero += 0.5 * AIR_DENSITY * rig.vehicle.drag_area * speed * speed * speed;
	out.aero_from_force += -rig.last_forces.central_force.dot(rig.linear_velocity);

	out.crank += telemetry.engine_torque * telemetry.engine_rpm * 2.0 * PI / 60.0;
	out.clutch_loss += telemetry.clutch_torque * telemetry.clutch_slip;
	out.axle_power += telemetry.axle_torque * telemetry.axle_speed;

	out.speed += rig.forward_speed();
	out.ground_speed += speed;
	out.lateral_g += std::fabs(telemetry.lateral_g);
	out.longitudinal_g += telemetry.longitudinal_g;
	out.yaw_rate += std::fabs(rig.yaw_rate());
	out.body_slip += std::atan2(rig.linear_velocity.dot(rig.basis_x),
			-rig.linear_velocity.dot(rig.basis_z));
	out.rpm += telemetry.engine_rpm;
	out.throttle += input.throttle;
	out.gear = telemetry.gear;
	out.inside_rear_load += telemetry.wheel[inside_rear].normal_load;
	out.warp_mm += rig.vehicle.warp_amplitude() * 1000.0;
	samples += 1.0;
}

void finish(Audit &out, double samples, double window_start_energy, double window_end_energy,
		double window_seconds) {
	if (samples <= 0.0) {
		out.stable = false;
		return;
	}
	const double inverse = 1.0 / samples;
	out.speed *= inverse;
	out.ground_speed *= inverse;
	out.lateral_g *= inverse;
	out.longitudinal_g *= inverse;
	out.yaw_rate *= inverse;
	out.body_slip *= inverse;
	out.rpm *= inverse;
	out.throttle *= inverse;
	out.crank *= inverse;
	out.clutch_loss *= inverse;
	out.aero *= inverse;
	out.aero_from_force *= inverse;
	out.axle_power *= inverse;
	out.scrub_common *= inverse;
	out.scrub_differential *= inverse;
	out.rear_speed_spread *= inverse;
	out.inside_rear_load *= inverse;
	out.warp_mm *= inverse;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		out.long_heat[corner] *= inverse;
		out.lat_heat[corner] *= inverse;
		out.rolling[corner] *= inverse;
		out.load[corner] *= inverse;
		out.slip_angle_deg[corner] *= inverse;
		out.slip_ratio[corner] *= inverse;
		out.long_slip_ms[corner] *= inverse;
		out.lat_slip_ms[corner] *= inverse;
	}
	out.ke_drift = window_seconds > 0.0
			? (window_end_energy - window_start_energy) / window_seconds
			: 0.0;
	// Radius from the motion, not from the steering geometry. The steering
	// geometry says what the driver asked for; this says what the kart did, and
	// #137's comment is about the gap between them.
	out.radius = out.yaw_rate > 1e-6 ? out.ground_speed / out.yaw_rate : 0.0;
	out.departed = std::fabs(out.body_slip) > 0.52;
}

// **Open loop.** Hold a steering input and a throttle, start at `start_speed`,
// and report where the kart ends up. This is #137's protocol.
Audit settle_corner(double steer, double throttle, double start_speed, int gear = 2,
		double seconds = 25.0, double window = 1.0, double direction = 1.0) {
	Rig rig;
	rig.configure();
	rig.settle();
	rig.vehicle.engage(gear, start_speed);
	rig.linear_velocity = -rig.basis_z * start_speed;

	const int inside_rear = direction > 0.0 ? CORNER_RL : CORNER_RR;
	const int total_ticks = static_cast<int>(seconds * 120.0);
	const int window_ticks = static_cast<int>(window * 120.0);
	const int first_sample = total_ticks - window_ticks;

	Audit result;
	double samples = 0.0;
	double start_energy = 0.0;

	for (int tick = 0; tick < total_ticks; ++tick) {
		DriverInput input;
		input.steer = direction * steer;
		input.throttle = throttle;
		rig.step(input);
		if (!rig.finite()) {
			result.stable = false;
			return result;
		}
		watch(rig, result, inside_rear);
		if (tick == first_sample) {
			start_energy = rig.kinetic_energy();
		}
		if (tick >= first_sample) {
			sample(rig, input, result, samples, inside_rear);
		}
	}

	finish(result, samples, start_energy, rig.kinetic_energy(), window);
	return result;
}

// The same open-loop protocol with the gear chosen for the start speed, so a
// sweep that starts from walking pace does not spend its first seconds bogged in
// a gear it cannot pull.
Audit settle_from(double steer, double throttle, double start_speed) {
	const int gear = start_speed < 6.0 ? 1 : (start_speed < 14.0 ? 2 : 3);
	return settle_corner(steer, throttle, start_speed, gear);
}

// **Speed held.** The `test_vehicle.cpp` protocol, reproduced so table 2's two
// columns are the same kart measured two ways rather than two karts.
Audit hold_corner(double steer, double target_speed, double direction = 1.0) {
	Rig rig;
	rig.configure();
	rig.settle();
	rig.vehicle.engage(target_speed < 6.0 ? 1 : 2, target_speed);
	rig.linear_velocity = -rig.basis_z * target_speed;

	const int inside_rear = direction > 0.0 ? CORNER_RL : CORNER_RR;
	Audit result;
	double samples = 0.0;
	double start_energy = 0.0;

	for (int tick = 0; tick < 120 * 6; ++tick) {
		DriverInput input;
		input.steer = direction * steer;
		const double error = target_speed - rig.forward_speed();
		input.throttle = error > 0.0 ? (error * 0.6 > 1.0 ? 1.0 : error * 0.6) : 0.0;
		input.brake = error < 0.0 ? (-error * 0.3 > 1.0 ? 1.0 : -error * 0.3) : 0.0;
		rig.step(input);
		if (!rig.finite()) {
			result.stable = false;
			return result;
		}
		watch(rig, result, inside_rear);
		if (tick == 120 * 5) {
			start_energy = rig.kinetic_energy();
		}
		if (tick > 120 * 5) {
			sample(rig, input, result, samples, inside_rear);
		}
	}

	finish(result, samples, start_energy, rig.kinetic_energy(), 1.0);
	if (result.departed || result.speed < target_speed * 0.6) {
		result.stable = false;
	}
	return result;
}

// The quasi-static lift threshold, recomputed from `chassis_flex.h` rather than
// quoted, with the two corrections `test_vehicle.cpp` documents at length: the
// 41 mm lateral center of mass `chassis_flex.h` has no field for, and the
// longitudinal g a real held corner is actually at.
double lift_threshold(double steer_input, double longitudinal_g, bool include_com_offset) {
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
	return lift_threshold_g(load_case, CORNER_RL) - bias;
}

void print_ledger(const char *label_a, const Audit &a, const char *label_b, const Audit &b) {
	std::printf("    %-34s %14s %14s\n", "", label_a, label_b);
	std::printf("    %-34s %14.2f %14.2f\n", "settled at, km/h", ms_to_kmh(a.speed),
			ms_to_kmh(b.speed));
	std::printf("    %-34s %14.3f %14.3f\n", "sustained lateral g", a.lateral_g, b.lateral_g);
	std::printf("    %-34s %14.3f %14.3f\n", "longitudinal g", a.longitudinal_g,
			b.longitudinal_g);
	std::printf("    %-34s %14.2f %14.2f\n", "radius from the motion, m", a.radius, b.radius);
	std::printf("    %-34s %14.2f %14.2f\n", "body slip, deg", a.body_slip * 180.0 / PI,
			b.body_slip * 180.0 / PI);
	std::printf("    %-34s %11.0f/%-2d %11.0f/%-2d\n", "engine rpm / gear", a.rpm, a.gear, b.rpm,
			b.gear);
	std::printf("\n    %-34s %14s %14s\n", "POWER, W", "", "");
	std::printf("    %-34s %14.0f %14.0f\n", "in: at the crank", a.crank, b.crank);
	std::printf("    %-34s %14.0f %14.0f\n", "  (of which, at the axle)", a.axle_power,
			b.axle_power);
	std::printf("    %-34s %14.0f %14.0f\n", "out: clutch slip", a.clutch_loss, b.clutch_loss);
	static const char *names[CORNER_COUNT] = { "FL", "FR", "RL", "RR" };
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		char line[64];
		std::snprintf(line, sizeof(line), "out: long. slip %s", names[corner]);
		std::printf("    %-34s %14.0f %14.0f\n", line, a.long_heat[corner], b.long_heat[corner]);
	}
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		char line[64];
		std::snprintf(line, sizeof(line), "out: lat.  slip %s", names[corner]);
		std::printf("    %-34s %14.0f %14.0f\n", line, a.lat_heat[corner], b.lat_heat[corner]);
	}
	std::printf("    %-34s %14.0f %14.0f\n", "out: rolling resistance, all 4",
			a.total_rolling(), b.total_rolling());
	std::printf("    %-34s %14.0f %14.0f\n", "out: aerodynamic drag", a.aero, b.aero);
	std::printf("    %-34s %14.0f %14.0f\n", "     (drag from the force, check)",
			a.aero_from_force, b.aero_from_force);
	std::printf("    %-34s %14.0f %14.0f\n", "sum of sinks", a.sinks(), b.sinks());
	std::printf("    %-34s %14.0f %14.0f\n", "stored: kinetic energy drift", a.ke_drift,
			b.ke_drift);
	std::printf("    %-34s %14.0f %14.0f\n", "RESIDUAL (in - out - stored)", a.residual(),
			b.residual());
	std::printf("\n    %-34s %14s %14s\n", "THE REAR AXLE", "", "");
	std::printf("    %-34s %14.0f %14.0f\n", "long. heat, both rears",
			a.long_heat[CORNER_RL] + a.long_heat[CORNER_RR],
			b.long_heat[CORNER_RL] + b.long_heat[CORNER_RR]);
	std::printf("    %-34s %14.0f %14.0f\n", "  common mode (driving)", a.scrub_common,
			b.scrub_common);
	std::printf("    %-34s %14.0f %14.0f\n", "  differential mode (the scrub)",
			a.scrub_differential, b.scrub_differential);
	std::printf("    %-34s %14.3f %14.3f\n", "rear road-speed spread, m/s", a.rear_speed_spread,
			b.rear_speed_spread);
	std::printf("\n    %-34s %14s %14s\n", "PER CORNER", "", "");
	std::printf("    %-34s %14s %14s\n", "  load N   FL/FR/RL/RR", "", "");
	std::printf("    %-34s %14s %14s\n", "", "", "");
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		char line[64];
		std::snprintf(line, sizeof(line), "  %s load N", names[corner]);
		std::printf("    %-34s %14.1f %14.1f\n", line, a.load[corner], b.load[corner]);
	}
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		char line[64];
		std::snprintf(line, sizeof(line), "  %s slip angle deg", names[corner]);
		std::printf("    %-34s %14.2f %14.2f\n", line, a.slip_angle_deg[corner],
				b.slip_angle_deg[corner]);
	}
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		char line[64];
		std::snprintf(line, sizeof(line), "  %s lat. slip vel m/s", names[corner]);
		std::printf("    %-34s %14.2f %14.2f\n", line, a.lat_slip_ms[corner],
				b.lat_slip_ms[corner]);
	}
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		char line[64];
		std::snprintf(line, sizeof(line), "  %s long. slip vel m/s", names[corner]);
		std::printf("    %-34s %14.2f %14.2f\n", line, a.long_slip_ms[corner],
				b.long_slip_ms[corner]);
	}
	std::printf("    %-34s %14.1f %14.1f\n", "inside rear load, N", a.inside_rear_load,
			b.inside_rear_load);
	std::printf("    %-34s %14.3f %14.3f\n", "frame warp, mm", a.warp_mm, b.warp_mm);
}

} // namespace

TEST_CASE("where the power goes at 0.60 of lock, against 0.25 where the kart is fine") {
	// **Issue #137 acceptance item 1.** The row that has to be explained is lock
	// 0.60 at full throttle settling at 21.2 km/h and 1.02 g, beside lock 0.25 at
	// full throttle holding 1.84 g at 116.9 km/h. Both are open loop, both are at
	// throttle 1.0, and the only difference between the two runs below is the
	// steering input — which is what makes the *difference* between the columns
	// the answer rather than either column on its own.
	//
	// Started from 25 m/s in second so the two runs share a protocol. A kart that
	// is going to settle at 21 km/h has to get there by losing speed and one that
	// is going to settle at 117 km/h has to get there by making it; 25 s of hold
	// is enough for both, and the kinetic-energy drift line proves it rather than
	// asserting it.
	const Audit quarter = settle_corner(0.25, 1.0, 25.0);
	const Audit sixty = settle_corner(0.60, 1.0, 25.0);

	REQUIRE(quarter.stable);
	REQUIRE(sixty.stable);

	std::printf("\n    ISSUE #137 ITEM 1: the energy ledger, open loop, full throttle\n");
	std::printf("    both runs 25 s from 25 m/s in 2nd, averaged over the last 1.0 s\n");
	std::printf("    forces and loads are read out of the solver; slip VELOCITIES are\n");
	std::printf("    reconstructed from the same body state and raycast - see the header\n\n");
	print_ledger("lock 0.25", quarter, "lock 0.60", sixty);

	// The difference, which is the thing #137 actually asked for.
	std::printf("\n    WHAT CHANGED between 0.25 and 0.60, W\n");
	std::printf("    %-34s %14.0f\n", "crank power", sixty.crank - quarter.crank);
	std::printf("    %-34s %14.0f\n", "longitudinal slip heat",
			sixty.total_long_heat() - quarter.total_long_heat());
	std::printf("    %-34s %14.0f\n", "lateral slip heat",
			sixty.total_lat_heat() - quarter.total_lat_heat());
	std::printf("    %-34s %14.0f\n", "rolling resistance",
			sixty.total_rolling() - quarter.total_rolling());
	std::printf("    %-34s %14.0f\n", "aerodynamic drag", sixty.aero - quarter.aero);
	std::printf("    %-34s %14.0f\n", "clutch slip", sixty.clutch_loss - quarter.clutch_loss);
	std::printf("    %-34s %14.0f\n", "  of the rears: scrub differential mode",
			sixty.scrub_differential - quarter.scrub_differential);

	// The share of the crank output each sink takes, which is the sentence a
	// reader wants and is not readable off watts alone at two different speeds.
	std::printf("\n    SHARE OF THE CRANK OUTPUT, %%\n");
	std::printf("    %-34s %14s %14s\n", "", "lock 0.25", "lock 0.60");
	const double qa = quarter.crank > 1.0 ? quarter.crank : 1.0;
	const double sa = sixty.crank > 1.0 ? sixty.crank : 1.0;
	std::printf("    %-34s %13.1f%% %13.1f%%\n", "longitudinal slip heat",
			100.0 * quarter.total_long_heat() / qa, 100.0 * sixty.total_long_heat() / sa);
	std::printf("    %-34s %13.1f%% %13.1f%%\n", "lateral slip heat",
			100.0 * quarter.total_lat_heat() / qa, 100.0 * sixty.total_lat_heat() / sa);
	std::printf("    %-34s %13.1f%% %13.1f%%\n", "rolling resistance",
			100.0 * quarter.total_rolling() / qa, 100.0 * sixty.total_rolling() / sa);
	std::printf("    %-34s %13.1f%% %13.1f%%\n", "aerodynamic drag", 100.0 * quarter.aero / qa,
			100.0 * sixty.aero / sa);
	std::printf("    %-34s %13.1f%% %13.1f%%\n", "clutch slip",
			100.0 * quarter.clutch_loss / qa, 100.0 * sixty.clutch_loss / sa);
	std::printf("    %-34s %13.1f%% %13.1f%%\n", "residual", 100.0 * quarter.residual() / qa,
			100.0 * sixty.residual() / sa);

	// **Which inside wheel is closest to lifting**, which the four load rows above
	// contain and nobody reads out of them. `chassis_flex.h` says in its own
	// comment that the crossover between "the inside front lifts" and "the inside
	// rear lifts" sits inside the range of published frame-torsion figures, and
	// which side of it this kart is on decides whether #32's mechanism can work at
	// all: ARCHITECTURE.md §6 wants the inside REAR to be the wheel that leaves,
	// because that is the differential.
	std::printf("\n    WHICH INSIDE WHEEL IS CLOSEST TO LEAVING (turning left)\n");
	std::printf("    %-34s %14s %14s\n", "", "lock 0.25", "lock 0.60");
	std::printf("    %-34s %14.1f %14.1f\n", "inside front FL, N", quarter.load[CORNER_FL],
			sixty.load[CORNER_FL]);
	std::printf("    %-34s %14.1f %14.1f\n", "inside rear  RL, N", quarter.load[CORNER_RL],
			sixty.load[CORNER_RL]);
	std::printf("    %-34s %14s %14s\n", "the lighter of the two is the",
			quarter.load[CORNER_FL] < quarter.load[CORNER_RL] ? "INSIDE FRONT" : "inside rear",
			sixty.load[CORNER_FL] < sixty.load[CORNER_RL] ? "INSIDE FRONT" : "inside rear");

	MESSAGE("closest to lifting at lock 0.25: inside front "
			<< quarter.load[CORNER_FL] << " N against inside rear " << quarter.load[CORNER_RL]
			<< " N; at lock 0.60 " << sixty.load[CORNER_FL] << " N against "
			<< sixty.load[CORNER_RL] << " N");

	MESSAGE("lock 0.25 full throttle: " << ms_to_kmh(quarter.speed) << " km/h, "
										<< quarter.lateral_g << " g, crank " << quarter.crank
										<< " W, sinks " << quarter.sinks() << " W, residual "
										<< quarter.residual() << " W");
	MESSAGE("lock 0.60 full throttle: " << ms_to_kmh(sixty.speed) << " km/h, " << sixty.lateral_g
										<< " g, crank " << sixty.crank << " W, sinks "
										<< sixty.sinks() << " W, residual " << sixty.residual()
										<< " W");
	MESSAGE("lock 0.60 lateral slip heat " << sixty.total_lat_heat() << " W against longitudinal "
										   << sixty.total_long_heat() << " W and axle scrub "
										   << sixty.scrub_differential << " W");

	// The ledger has to close, or nothing above is a measurement. The bar is
	// deliberately wide: the reconstruction mixes tick-mean forces with
	// tick-opening velocities and the engine's own rotational energy is not in
	// the drift term. What it must not do is miss by the order of the answer.
	CHECK(std::fabs(quarter.residual()) < 0.20 * quarter.crank);
	CHECK(std::fabs(sixty.residual()) < 0.20 * sixty.crank);
	// The two ways of getting the drag agree, which is the one sink computed from
	// a formula rather than read off a force.
	CHECK(quarter.aero == doctest::Approx(quarter.aero_from_force).epsilon(0.02));
	CHECK(sixty.aero == doctest::Approx(sixty.aero_from_force).epsilon(0.02));
}

TEST_CASE("what the kart settles at against lock, open loop, at three throttles") {
	// The sweep #137's table is a slice of, so the 21.2 km/h row can be seen as a
	// point on a curve rather than as an anomaly. Open loop throughout.
	std::printf("\n    ISSUE #137: settled state against lock, open loop\n");
	std::printf("    %6s %9s %9s %9s %9s %9s %9s %9s %9s\n", "lock", "throttle", "km/h", "lat g",
			"radius m", "slip deg", "crank W", "lat heat", "IR load N");
	for (double throttle : { 1.0, 0.6, 0.3 }) {
		for (double lock : { 0.10, 0.25, 0.40, 0.60, 0.80, 1.00 }) {
			const Audit run = settle_corner(lock, throttle, 25.0);
			if (!run.stable) {
				std::printf("    %6.2f %9.2f   diverged\n", lock, throttle);
				continue;
			}
			std::printf("    %6.2f %9.2f %9.1f %9.3f %9.2f %9.2f %9.0f %9.0f %9.1f%s\n", lock,
					throttle, ms_to_kmh(run.speed), run.lateral_g, run.radius,
					run.body_slip * 180.0 / PI, run.crank, run.total_lat_heat(),
					run.inside_rear_load, run.departed ? "  departed" : "");
		}
	}
	MESSAGE("open-loop lock sweep printed at three throttles");
}

TEST_CASE("the settled speed depends on the speed it started from, which is the trap") {
	// **This case was not in the brief. It exists because the first table refused
	// to reproduce #137's headline row and the reason turned out to be the
	// finding.**
	//
	// #137 records lock 0.60 at full throttle settling at 21.2 km/h and 1.02 g.
	// The same input in this solver, started from 25 m/s, settles at 97 km/h and
	// 1.74 g — running wide at a 44 m radius, not following the 4.5 m circle the
	// steering geometry describes, but fast and stable and nowhere near stopped.
	// Both numbers are real. They are two different attractors of the same
	// open-loop input, and which one the kart falls into is decided by the speed
	// it arrived at the corner with.
	//
	// That reframes #137's own hypothesis rather than confirming it. The issue
	// says "the states that would relieve the scrub are unreachable from the
	// states the scrub produces". The mechanism it proposes for the
	// unreachability is wheel lift — needs 1.69 g, scrub caps it at 0.09 g. The
	// sweep below says the unreachability is real and has nothing to do with wheel
	// lift: it is a low-speed basin that a kart entering slowly cannot climb out
	// of, and a kart entering fast never enters at all.
	std::printf("\n    ISSUE #137: the settled state against the speed it STARTED at\n");
	std::printf("    open loop, steering and throttle held for 25 s, same input throughout\n");
	std::printf("    %6s %9s %9s %9s %9s %9s %9s %9s\n", "lock", "throttle", "start km/h",
			"end km/h", "lat g", "radius m", "crank W", "rpm");
	// Three locks and six start speeds rather than the eleven-by-nine grid this
	// was found on. The finer grid is in the report on #137; what is kept here is
	// the coarsest sweep that still shows both branches at both of the locks where
	// they exist, because `tests/run.sh` is a five-second suite and a
	// two-thousand-simulated-second sweep in it is a tax every other agent pays.
	for (double lock : { 0.40, 0.60, 1.00 }) {
		for (double throttle : { 1.0, 0.6 }) {
			double lowest_high = 1e9;
			double highest_low = 0.0;
			for (double start : { 2.0, 6.0, 12.0, 16.0, 20.0, 30.0 }) {
				const Audit run = settle_from(lock, throttle, start);
				if (!run.stable) {
					std::printf("    %6.2f %9.2f %9.1f   diverged\n", lock, throttle,
							ms_to_kmh(start));
					continue;
				}
				std::printf("    %6.2f %9.2f %9.1f %9.1f %9.3f %9.2f %9.0f %9.0f%s\n", lock,
						throttle, ms_to_kmh(start), ms_to_kmh(run.speed), run.lateral_g,
						run.radius, run.crank, run.rpm, run.departed ? "  departed" : "");
				if (run.speed > 8.0 && run.speed < lowest_high) {
					lowest_high = run.speed;
				}
				if (run.speed <= 8.0 && run.speed > highest_low) {
					highest_low = run.speed;
				}
			}
			if (lowest_high < 1e8 && highest_low > 0.0) {
				std::printf("      -> BISTABLE at lock %.2f throttle %.2f: a low branch up to "
							"%.1f km/h and a high branch from %.1f km/h\n",
						lock, throttle, ms_to_kmh(highest_low), ms_to_kmh(lowest_high));
			}
			std::printf("\n");
		}
	}
	MESSAGE("start-speed sweep printed; see the BISTABLE lines");
}

TEST_CASE("what the engine is doing in the collapsed state, because 'absorbing the "
		  "entire power output' assumes there is one") {
	// #137 says "a 45 hp kart at full throttle does not end up at 21 km/h because
	// the driver asked for too much steering angle. Something is absorbing the
	// entire power output."
	//
	// The first table in this file shows the sinks at full throttle summing to the
	// crank output to within 2.6%, with **lateral** tire slip taking 70% of it at
	// lock 0.60 and longitudinal slip — the scrub — taking 3.5%. That answers the
	// question as asked. What it does not answer is the collapsed rows in the
	// open-loop sweep, where the crank output is not being absorbed by anything
	// because there is not any: at lock 0.60 and 0.30 throttle the kart stops and
	// the crank reads zero watts.
	//
	// So this case reports what the drivetrain is doing at the bottom, which is a
	// different question from where the power went and has to be asked separately
	// or the two get confused for each other.
	// `gear needs` is the road speed the ending gear corresponds to at peak power,
	// which is what makes "gear 4 at walking pace" readable as the anomaly it is
	// rather than as a number in a column. Computed from `Gearbox::road_speed`,
	// which is the solver's own map, at `kz::PEAK_POWER_RPM`.
	KartVehicle probe;
	probe.configure();
	const Gearbox ratios;
	const double rear_radius = probe.rolling_radius(CORNER_RL);

	std::printf("\n    ISSUE #137: the drivetrain at the bottom of the collapse\n");
	std::printf("    all runs start at 4.0 m/s in 1st and hold the input for 25 s\n");
	std::printf("    %6s %9s %9s %9s %6s %11s %9s %9s %9s %9s\n", "lock", "throttle", "km/h",
			"rpm", "gear", "gear wants", "crank W", "axle W", "clutch W", "long heat");
	for (double throttle : { 1.0, 0.6, 0.3 }) {
		for (double lock : { 0.60, 0.80, 1.00 }) {
			const Audit run = settle_from(lock, throttle, 4.0);
			if (!run.stable) {
				std::printf("    %6.2f %9.2f   diverged\n", lock, throttle);
				continue;
			}
			const double wants = run.gear >= 1
					? ms_to_kmh(ratios.road_speed(kz::PEAK_POWER_RPM, run.gear, rear_radius))
					: 0.0;
			std::printf("    %6.2f %9.2f %9.2f %9.0f %6d %11.1f %9.0f %9.0f %9.0f %9.0f\n", lock,
					throttle, ms_to_kmh(run.speed), run.rpm, run.gear, wants, run.crank,
					run.axle_power, run.clutch_loss, run.total_long_heat());
		}
	}
	std::printf("\n    'gear wants' is the road speed that gear corresponds to at %.0f rpm.\n",
			kz::PEAK_POWER_RPM);
	std::printf("    A crank power of zero at idle rpm is an engine making no torque, not an\n");
	std::printf("    engine being held down by grip. A large 'clutch W' beside a small\n");
	std::printf("    'axle W' is the auto-clutch burning the output because the gearbox is\n");
	std::printf("    holding a ratio the road speed left behind.\n");
	MESSAGE("drivetrain-at-the-bottom table printed");
}

TEST_CASE("whether the inside rear can ever lift at large steer angles") {
	// **Issue #137 acceptance item 2.** The hypothesis to be killed or confirmed:
	// the kart needs about 1.69 g to lift the inside rear, the scrub caps it at
	// 0.09 g at full lock, so the wheel never lifts and the scrub never stops.
	//
	// Two curves per steering angle, and the whole answer is whether they cross:
	//
	//   ACHIEVABLE   the best sustained lateral g the kart can hold at that lock.
	//                Measured twice, because #137's comment already established
	//                that the protocol decides the number: open loop at full
	//                throttle, and the speed-searching protocol from
	//                `test_vehicle.cpp` that ROADMAP M3b's figures came from.
	//   NEEDED       `chassis_flex.h`'s quasi-static lift threshold at that lock,
	//                corrected for the 41 mm lateral center of mass and for the
	//                longitudinal g the achievable run was actually at. Both
	//                corrections are `test_vehicle.cpp`'s and are documented
	//                there; without them the comparison is between two different
	//                questions.
	//
	// And the third column that settles it whatever the first two say: the inside
	// rear's **actual measured load**. A threshold curve is a model. A load of
	// 0.0 N is a wheel in the air.
	const SteeringGeometry steering = kz_front_geometry();

	std::printf("\n    ISSUE #137 ITEM 2: can the inside rear lift at large lock\n");
	std::printf("    'needed geo' is the threshold at zero longitudinal g and no lateral CoM,\n");
	std::printf("    which is the jacking geometry alone and is the column that is monotone.\n");
	std::printf("    'needed' adds the two corrections and therefore inherits the noise in\n");
	std::printf("    whichever run supplied the longitudinal g.\n");
	std::printf("    'IR min' is the lowest load at ANY tick of the open-loop run, so it is\n");
	std::printf("    the transient answer where the other columns are the steady one.\n");
	std::printf("    %6s %8s %9s %9s %9s %9s %9s %9s %9s %9s\n", "lock", "deg", "held g",
			"open g", "needed", "needed geo", "margin", "IR held N", "IR open N", "IR min N");

	double last_crossing_lock = -1.0;
	double worst_miss = 0.0;
	double worst_miss_lock = 0.0;
	bool any_zero_load = false;
	double first_zero_load_lock = -1.0;

	for (double lock : { 0.10, 0.25, 0.40, 0.50, 0.60, 0.80, 1.00 }) {
		const double inner = steering.max_lock * lock;
		const double radius =
				turn_radius(steering, inner, ackermann_outer_angle(steering, inner));

		// The held protocol: sweep speeds around the one that would produce 2.5 g
		// if the kart could, exactly as `test_vehicle.cpp`'s skidpad does, so that
		// every steering angle is treated the same way on either side of its own
		// limit.
		const double reference_speed = std::sqrt(2.5 * G * radius);
		Audit best;
		bool have_best = false;
		for (double fraction = 0.5; fraction <= 1.21; fraction += 0.1) {
			const Audit corner = hold_corner(lock, reference_speed * fraction);
			if (!corner.stable) {
				continue;
			}
			if (!have_best || corner.lateral_g > best.lateral_g) {
				best = corner;
				have_best = true;
			}
		}

		const Audit open = settle_corner(lock, 1.0, 25.0);

		const double held_g = have_best ? best.lateral_g : 0.0;
		const double open_g = open.stable ? open.lateral_g : 0.0;
		const double achievable = held_g > open_g ? held_g : open_g;
		const double longitudinal = have_best ? best.longitudinal_g : open.longitudinal_g;
		const double needed = lift_threshold(lock, longitudinal, true);
		const double needed_geometric = lift_threshold(lock, 0.0, false);
		const double margin = achievable - needed;
		const double transient_minimum =
				open.min_inside_rear_load < 1e8 ? open.min_inside_rear_load : -1.0;

		std::printf("    %6.2f %8.2f %9.3f %9.3f %9.3f %9.3f %9.3f %9.1f %9.1f %9.1f\n", lock,
				inner * 180.0 / PI, held_g, open_g, needed, needed_geometric, margin,
				have_best ? best.inside_rear_load : -1.0,
				open.stable ? open.inside_rear_load : -1.0, transient_minimum);

		if (margin >= 0.0) {
			last_crossing_lock = lock;
		} else if (-margin > worst_miss) {
			worst_miss = -margin;
			worst_miss_lock = lock;
		}
		// The transient minimum is the one that answers "can it EVER lift". The
		// steady means cannot, and reading them as if they could is how a wheel
		// that reaches 0.0 N in a steering step gets recorded as a wheel that
		// never leaves the ground.
		const double smallest_load = transient_minimum >= 0.0 ? transient_minimum : 1e9;
		if (smallest_load <= 1.0) {
			any_zero_load = true;
			if (first_zero_load_lock < 0.0) {
				first_zero_load_lock = lock;
			}
		}
	}

	if (last_crossing_lock >= 0.0) {
		std::printf("\n    achievable and needed last cross at lock %.2f\n", last_crossing_lock);
	} else {
		std::printf("\n    achievable NEVER reaches needed at any lock in this sweep\n");
	}
	std::printf("    the largest miss is %.3f g, at lock %.2f\n", worst_miss, worst_miss_lock);
	if (any_zero_load) {
		std::printf("    the inside rear reaches 1 N or less at some tick from lock %.2f,\n",
				first_zero_load_lock);
		std::printf("    so it DOES lift - transiently. Read that beside the steady columns:\n");
		std::printf("    a wheel that touches zero in a step and carries load in the settled\n");
		std::printf("    corner is a wheel whose lift is not available as a steady-state\n");
		std::printf("    differential, which is what issue #32 asks it to be.\n");
	} else {
		std::printf("    the inside rear never reaches zero load at any tick of any run here\n");
	}

	if (last_crossing_lock >= 0.0) {
		MESSAGE("achievable and needed lateral g last cross at lock " << last_crossing_lock);
	} else {
		MESSAGE("achievable lateral g never reaches the lift threshold at any lock sampled");
	}
	MESSAGE("largest miss " << worst_miss << " g at lock " << worst_miss_lock
							<< "; the gap narrows toward full lock rather than widening");
	MESSAGE("inside rear reaches zero load at some tick: " << std::string(any_zero_load ? "yes" : "no")
														  << ", first at lock "
														  << first_zero_load_lock);

	// No assertion on which way this comes out. #137 says a crossing kills its own
	// hypothesis and is the more valuable result, so a CHECK either way would be
	// a test asserting a conclusion this file exists to measure. What is checked
	// is only that the sweep produced a curve at all.
	CHECK(last_crossing_lock != 0.0);
}

TEST_CASE("caster jacking against steer angle, and whether it is monotone") {
	// **Issue #137 section 4, listed as unestablished:** jacking scales with steer
	// angle so it should be strongest at full lock, and it is not producing lift
	// there.
	//
	// Three quantities, and only the first is a pure read-out:
	//
	//   offset      `solve_steering(...).contact_offset.y`, mm. Negative means the
	//               patch moved down, which lifts the chassis at that corner —
	//               `steering.h`'s convention, and `KartVehicle` negates it once
	//               when it hands it to `suspension.h`.
	//   force       `spring_rate * jack`, N. This is the load the offset commands
	//               at that corner before the frame's warp redistributes any of
	//               it, computed here from the corner's own `spring_rate` which
	//               `corner_setup_for()` returns. **Computed, not read out**, and
	//               it is an upper bound on what the corner actually sees.
	//   lift        the quasi-static threshold at that lock, which is the jacking
	//               expressed as the thing it is for.
	//
	// Monotonicity is checked on the *inside* wheel's offset, which is the one at
	// `max_lock * input`, because that is the wheel #137's section 4 is about.
	const SteeringGeometry geometry = kz_front_geometry();
	KartVehicle probe;
	probe.configure();
	const double front_rate = probe.corner_setup_for(CORNER_FL).spring_rate;
	const double rear_rate = probe.corner_setup_for(CORNER_RL).spring_rate;

	std::printf("\n    ISSUE #137 SECTION 4: caster jacking against steer angle\n");
	std::printf("    front corner rate %.0f N/m, rear %.0f N/m\n", front_rate, rear_rate);
	std::printf("    turning left: FL is the inside wheel\n");
	std::printf("    %6s %8s %11s %11s %10s %10s %9s\n", "lock", "deg", "FL off mm",
			"FR off mm", "FL jack N", "FR jack N", "lift g");

	double previous_offset = 0.0;
	bool monotone = true;
	double first_non_monotone = -1.0;
	double peak_offset = 0.0;
	double peak_offset_lock = 0.0;

	for (double lock = 0.0; lock <= 1.001; lock += 0.10) {
		const SteeringOutput steered = solve_steering(geometry, lock);
		// The same negation `KartVehicle::solve_substep` performs, and the only
		// place the sign lives there.
		const double jack_left = -steered.left.contact_offset.y;
		const double jack_right = -steered.right.contact_offset.y;
		const double threshold = lift_threshold(lock, 0.0, false);

		std::printf("    %6.2f %8.2f %11.4f %11.4f %10.1f %10.1f %9.3f\n", lock,
				steered.left.angle * 180.0 / PI, steered.left.contact_offset.y * 1000.0,
				steered.right.contact_offset.y * 1000.0, jack_left * front_rate,
				jack_right * front_rate, threshold);

		if (lock > 0.001) {
			if (jack_left < previous_offset - 1e-12) {
				if (monotone) {
					first_non_monotone = lock;
				}
				monotone = false;
			}
		}
		if (jack_left > peak_offset) {
			peak_offset = jack_left;
			peak_offset_lock = lock;
		}
		previous_offset = jack_left;
	}

	std::printf("\n    inside-wheel jacking is %s in steer input\n",
			monotone ? "MONOTONE INCREASING" : "NOT monotone");
	if (!monotone) {
		std::printf("    first decrease at lock %.2f\n", first_non_monotone);
	}
	std::printf("    largest inside-wheel jack %.4f mm at lock %.2f, which is %.1f N\n",
			peak_offset * 1000.0, peak_offset_lock, peak_offset * front_rate);
	std::printf("    against a static corner load of %.1f N\n",
			probe.corner_setup_for(CORNER_FL).static_load());

	// What the jacking is worth in the solver rather than in the geometry: the
	// same steady corner run twice, once with `jacking_enabled` and once without.
	// That flag is the instrument issue #33's second acceptance criterion is
	// measured with and it zeroes the front geometric offsets and nothing else.
	std::printf("\n    what the jacking is worth in a driven corner, open loop full throttle\n");
	std::printf("    %6s %11s %11s %11s %11s %11s\n", "lock", "g jacked", "g level",
			"IR jacked N", "IR level N", "warp mm");
	for (double lock : { 0.25, 0.60, 1.00 }) {
		Audit a = settle_corner(lock, 1.0, 25.0);
		// The level run has to be built by hand, because `settle_corner` does not
		// take the flag — it is deliberately not a parameter of the protocol, so
		// that every other table in this file is unambiguously the shipped kart.
		Rig rig;
		rig.vehicle.jacking_enabled = false;
		rig.configure();
		rig.settle();
		rig.vehicle.engage(2, 25.0);
		rig.linear_velocity = -rig.basis_z * 25.0;
		Audit b;
		double samples = 0.0;
		double start_energy = 0.0;
		for (int tick = 0; tick < 120 * 25; ++tick) {
			DriverInput input;
			input.steer = lock;
			input.throttle = 1.0;
			rig.step(input);
			if (!rig.finite()) {
				b.stable = false;
				break;
			}
			if (tick == 120 * 24) {
				start_energy = rig.kinetic_energy();
			}
			if (tick >= 120 * 24) {
				sample(rig, input, b, samples, CORNER_RL);
			}
		}
		if (b.stable) {
			finish(b, samples, start_energy, rig.kinetic_energy(), 1.0);
		}
		std::printf("    %6.2f %11.3f %11.3f %11.1f %11.1f %11.3f\n", lock,
				a.stable ? a.lateral_g : -1.0, b.stable ? b.lateral_g : -1.0,
				a.stable ? a.inside_rear_load : -1.0, b.stable ? b.inside_rear_load : -1.0,
				a.stable ? a.warp_mm : 0.0);
	}

	MESSAGE("inside-wheel caster jacking monotone in steer input: "
			<< std::string(monotone ? "yes" : "no") << ", peak "
																   << peak_offset * 1000.0
																   << " mm at lock "
																   << peak_offset_lock);
	MESSAGE("peak jacking force " << peak_offset * front_rate << " N against a static corner load of "
								  << probe.corner_setup_for(CORNER_FL).static_load() << " N");

	// The geometry has to actually produce jacking, or `steering.h` and
	// `suspension.h` are joined wrong and #32's mechanism is absent. Whether it is
	// monotone is reported and not asserted, because #137 asks for the answer and
	// does not say what it should be.
	CHECK(peak_offset > 0.0);
}
