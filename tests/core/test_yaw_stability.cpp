#include "doctest.h"

#include "core/chassis.h"
#include "core/kz_reference.h"
#include "core/steering.h"
#include "core/tire.h"
#include "core/units.h"
#include "core/vehicle.h"

#include <cmath>
#include <cstdio>

// **Where the steering goes.** Issue #137's remaining half, and the standing
// instrument for it. ADR-0072.
//
// This file measures and changes nothing. Nothing under `src/` is touched by it
// and no constant is tuned in it.
//
// ## What it is for
//
// ADR-0071 established that #137 is a spin rather than a scrub, and left the
// remaining half located but not explained: "near-pure lateral slip, drivetrain
// delivering zero torque, rear slip ratio 0.087 while rear slip angle runs
// -1.8 -> -84.1 deg". The AI reaches it at a tenth of lock while using 0.5177 of
// the lateral ceiling, and a controller cannot overshoot a limit it is at half
// of.
//
// The measurement below says what is actually happening, and it is not
// saturation at all:
//
// **From 29 km/h upward the kart answers 0.49 of the yaw rate its own front
// wheels are pointed at, at accelerations as low as 0.09 g, and the ratio does
// not move with speed.** A real understeer gradient is `L / (L + K v^2)` and
// therefore falls with speed; 0.489 at 29 km/h and 0.481 at 93 km/h is not a
// gradient, it is a factor.
//
// ## What is eating it, measured rather than argued
//
// The four-way split of the applied yaw moment -- front/rear crossed with
// lateral/longitudinal -- closes on the applied moment and says this, at
// 43.2 km/h and 0.05 of lock, which is 1.24 degrees of steering and 0.09 g:
//
//     Mz front lateral      +143.0 N m      the steering moment
//     Mz rear  lateral        -0.4 N m      the rear tires are doing NOTHING
//     Mz rear  longitudinal  -142.3 N m     the solid axle's scrub couple
//     Mz front longitudinal    -0.2 N m
//
// The front's steering moment is cancelled to within 0.3% by the **solid rear
// axle's longitudinal scrub couple**, and the rear tires' lateral force -- the
// thing that is supposed to balance a steering input on any vehicle -- carries
// 0.3% of it. The kart is not cornering by developing a rear slip angle. It is
// cornering by dragging one rear tire forward and the other backward, and the
// couple that produces is large enough to eat half the steering before any tire
// is anywhere near its limit.
//
// The attribution is measured and not inferred. Replacing each rear tire's
// longitudinal force with the pair's mean -- which deletes the couple while
// leaving the net thrust, the axle reaction torque and every lateral force
// exactly as they were -- moves the steering response:
//
//     km/h    lock    got/geo with couple    got/geo without
//     18.0    0.05          0.901                 1.002
//     28.8    0.05          0.489                 0.996
//     43.2    0.05          0.489                 0.988
//     72.0    0.05          0.488                 0.894
//     92.9    0.05          0.481                 0.718
//
// Without the couple the kart tracks its geometry at low speed and then washes
// out with speed, which is what an understeer gradient looks like. With it, the
// response is halved at every speed and the shape is gone.
//
// ## What this file does NOT claim
//
// **The couple is real physics and deleting it is not the fix.** A solid rear
// axle does resist yaw. The finding here is about its **magnitude**.
//
// **And it is not #224.** The obvious hypothesis is that the couple should be
// relieved by the inside rear unloading -- ARCHITECTURE.md §6 calls that the
// defining kart dynamic, and #224 measured twice that this kart lifts the inside
// **front** instead. That hypothesis was published to two issues before it was
// swept, and the sweep falsifies it: `frame_torsion` across the whole range
// `chassis_flex.h` records as published, 193.62 to 3,464 N m/deg, moves the
// response from **0.489 to 0.505**. Eighteen times the stiffness buys three
// percent. At 0.14 g there is almost no lateral load to redistribute, so a frame
// that redistributes harder has nothing to redistribute; wheel lift is a
// limit-handling mechanism and this is not a limit-handling defect. The sweep is
// a test case below so that nobody has to have the idea twice.
//
// What the magnitude does rest on is two things and no others: the rear tires
// run equal and opposite slip ratios of `half_track * yaw / v` -- +/-0.0096 at
// 43.2 km/h and 0.05 of lock -- and `tire.h`'s `longitudinal.stiffness = 12.0`
// with `shape = 1.65` turns +/-0.96% of slip ratio into +/-19% of peak force.
//
// The candidate worth someone's time is a **missing degree of freedom**:
// `vehicle.h` carries the rear axle as one rigid `axle_speed_`, and a real KZ
// axle is a steel tube whose torsional compliance is the primary kart setup
// variable. See ADR-0072.
//
// The couple's magnitude is pinned below so that a change to the tire's
// longitudinal curve, to the frame's torsion, or to the rear track cannot move
// it silently.
//
// ## The number was already on the screen, under a wrong explanation
//
// `test_vehicle.cpp`'s steering-step table has printed `yaw/Ack` of 0.483, 0.482
// and 0.478 for its three smallest inputs since it was written, and its own
// commentary explains the column away: *"the kart is not under-rotating, it is
// being asked for a radius no tire can hold"*. That is true of the bottom of
// that table -- full lock at 100 km/h asks for 28 g -- and it was applied to the
// whole of it. The top row asks for **1.62 g against 2.10 capable**, and the
// measurement here asks for **0.137 g**, which is 6.5% of capacity, and still
// returns 0.489. A demand-side explanation cannot survive a tenth of a g.
//
// Worth reading as a warning about tables: the row that motivates a caption is
// rarely the row that falsifies it.

using namespace kart::core;

namespace {

constexpr double TICK = 1.0 / 120.0;

// params.py, `wheelbase`, via steering.h's `configure_kz`.
constexpr double WHEELBASE = 1.050;

double degrees(double radians) {
	return radians * 180.0 / PI;
}

// An explicit relative comparison. `doctest::Approx(x).epsilon(e)` compares
// against `e * (1.0 + max(|a|,|b|))`, so on a value of 0.03 an epsilon of 0.06 is
// a tolerance of 0.062 -- see CLAUDE.md, it passed a test that was wrong by 46%.
bool within_relative(double measured, double expected, double fraction) {
	return std::fabs(measured - expected) <= fraction * std::fabs(expected);
}

// A rigid body, a flat floor, and nothing else.
//
// A third copy of `test_vehicle.cpp`'s harness, for the reason
// `test_scrub_energy.cpp` gives for being the second: that file's fixture is in
// an anonymous namespace and is not this file's to change, and lifting it into a
// shared header would put a fixture three suites depend on into a fourth place.
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
	GroundQuery last_query[CORNER_COUNT];
	// The yaw moment actually applied this tick, N m about the body's up axis.
	// Kept so the four-way split below can be checked against it rather than
	// trusted -- a decomposition that does not sum to the thing it decomposes is
	// measuring its own arithmetic.
	double applied_yaw_moment = 0.0;

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

	// The angle between where the kart points and where it is going.
	double body_slip() const {
		if (linear_velocity.length() < 1e-6) {
			return 0.0;
		}
		return std::atan2(linear_velocity.dot(basis_x), forward_speed());
	}

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
		applied_yaw_moment = torque.dot(basis_y);

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

	// The applied yaw moment split four ways: front/rear crossed with
	// lateral/longitudinal, about the center of mass, N m. Positive is the
	// direction a positive steer input turns.
	//
	// The tire force is resolved onto the **body's** axes rather than each
	// wheel's, so "longitudinal" means along the kart and a steered front wheel's
	// lateral force contributes to both terms. That is the split the question
	// wants: what turns the kart, not what the tire thinks it is doing.
	void moment_split(double out[4]) const {
		for (int index = 0; index < 4; ++index) {
			out[index] = 0.0;
		}
		const VehicleTelemetry &telemetry = vehicle.telemetry();
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			const Vec3 arm = origin() + last_forces.application_point[corner] - com_position;
			const Vec3 tire = telemetry.wheel[corner].force;
			const Vec3 lateral = basis_x * tire.dot(basis_x);
			const Vec3 longitudinal = basis_z * tire.dot(basis_z);
			const bool front = corner == CORNER_FL || corner == CORNER_FR;
			out[front ? 0 : 2] += arm.cross(lateral).dot(basis_y);
			out[front ? 1 : 3] += arm.cross(longitudinal).dot(basis_y);
		}
	}
};

// One steering-response measurement, averaged over the last second of a six
// second hold at a fixed speed.
struct Response {
	double yaw = 0.0; // rad/s, what the kart did
	double geometric = 0.0; // rad/s, what the front wheels asked for
	double steer = 0.0; // rad, mean front steer angle
	double speed = 0.0; // m/s
	double lateral_g = 0.0;
	double body_slip = 0.0; // deg
	double moment[4] = { 0.0, 0.0, 0.0, 0.0 };
	double applied_moment = 0.0;
	bool departed = false;

	double ratio() const { return geometric > 1e-9 ? std::fabs(yaw) / geometric : 0.0; }
};

// **Speed held**, because the open-loop full-throttle protocol cannot measure a
// steering response: the kart accelerates until it is at its lateral limit and
// then every lock reports the same saturated answer. `test_scrub_energy.cpp`'s
// `settle_corner` is the right instrument for "where does a held input end up"
// and the wrong one for "what does the steering do".
Response steering_response(double lock, double target_speed, double torsion = 0.0) {
	Rig rig;
	rig.configure();
	if (torsion > 0.0) {
		// `configure()` is idempotent by design so a test can move this. See its
		// comment in `vehicle.h`.
		rig.vehicle.frame_torsion_nm_per_deg = torsion;
		rig.vehicle.configure();
	}
	rig.settle();
	rig.vehicle.engage(target_speed < 6.0 ? 1 : (target_speed < 14.0 ? 2 : 4), target_speed);
	rig.linear_velocity = -rig.basis_z * target_speed;

	Response out;
	double samples = 0.0;
	for (int tick = 0; tick < 120 * 6; ++tick) {
		DriverInput input;
		input.steer = lock;
		const double error = target_speed - rig.forward_speed();
		input.throttle = error > 0.0 ? std::fmin(error * 0.6, 1.0) : 0.0;
		input.brake = error < 0.0 ? std::fmin(-error * 0.3, 1.0) : 0.0;
		rig.step(input);
		if (!rig.finite()) {
			out.departed = true;
			return out;
		}
		if (std::fabs(degrees(rig.body_slip())) > 45.0) {
			out.departed = true;
		}
		if (tick < 120 * 5) {
			continue;
		}
		const VehicleTelemetry &telemetry = rig.vehicle.telemetry();
		double split[4];
		rig.moment_split(split);
		for (int index = 0; index < 4; ++index) {
			out.moment[index] += split[index];
		}
		out.applied_moment += rig.applied_yaw_moment;
		out.yaw += rig.yaw_rate();
		out.steer += 0.5 *
				(telemetry.wheel[CORNER_FL].steer_angle + telemetry.wheel[CORNER_FR].steer_angle);
		out.speed += rig.linear_velocity.length();
		out.body_slip += degrees(rig.body_slip());
		samples += 1.0;
	}

	for (int index = 0; index < 4; ++index) {
		out.moment[index] /= samples;
	}
	out.applied_moment /= samples;
	out.yaw /= samples;
	out.steer /= samples;
	out.speed /= samples;
	out.body_slip /= samples;
	out.geometric = out.speed * std::tan(std::fabs(out.steer)) / WHEELBASE;
	out.lateral_g = std::fabs(out.yaw) * out.speed / G;
	return out;
}

} // namespace

TEST_CASE("the yaw moment split sums to the yaw moment that was applied") {
	// The instrument before the finding. A decomposition that does not close is
	// measuring its own arithmetic, and every number in this file rests on it.
	const Response run = steering_response(0.10, 12.0);
	const double sum = run.moment[0] + run.moment[1] + run.moment[2] + run.moment[3];
	// The applied moment carries `central_torque` and the four normal forces'
	// moments as well, which are out of plane on flat ground; what must agree is
	// the in-plane tire part, and at a settled speed the applied moment is near
	// zero because the yaw rate is constant.
	CHECK(std::fabs(sum) < 12.0);
	CHECK(std::fabs(run.applied_moment) < 12.0);
}

TEST_CASE("the kart answers half the yaw rate its front wheels are pointed at") {
	// #137's remaining half, as one number. **These are the defect's figures and
	// they are pinned so it cannot move silently, not because they are right.**
	// When the couple below is relieved they must all rise toward 1.0 and this
	// test has to be rewritten -- which is the point of pinning them.
	std::printf("\n    #137: STEERING RESPONSE against the steering geometry\n");
	std::printf("    %8s %7s %10s %9s %9s %9s %8s\n", "km/h", "lock", "steer deg", "yaw got",
			"yaw geo", "got/geo", "lat g");

	struct Case {
		double speed;
		double lock;
		double expected_ratio;
	};
	// 18 km/h is first gear and the couple has not taken over yet; from 29 km/h
	// up the ratio is flat, which is the finding.
	const Case cases[] = {
		{ 5.0, 0.05, 0.901 },
		{ 8.0, 0.05, 0.489 },
		{ 12.0, 0.05, 0.489 },
		{ 12.0, 0.10, 0.493 },
		{ 20.0, 0.05, 0.488 },
		{ 25.8, 0.05, 0.481 },
		{ 25.8, 0.10, 0.467 },
	};

	for (const Case &item : cases) {
		const Response run = steering_response(item.lock, item.speed);
		std::printf("    %8.1f %7.2f %10.2f %9.3f %9.3f %9.3f %8.3f\n",
				ms_to_kmh(item.speed), item.lock, degrees(std::fabs(run.steer)),
				std::fabs(run.yaw), run.geometric, run.ratio(), run.lateral_g);
		CHECK_FALSE(run.departed);
		CHECK(within_relative(run.ratio(), item.expected_ratio, 0.03));
	}

	// The shape, not just the values: a real understeer gradient is
	// `L / (L + K v^2)` and must fall with speed. This one does not, and that is
	// what says it is a factor rather than a gradient. 29 km/h against 93 km/h,
	// a 3.2x speed range and a 10x range in lateral acceleration.
	const double slow = steering_response(0.05, 8.0).ratio();
	const double fast = steering_response(0.05, 25.8).ratio();
	std::printf("    ratio at 29 km/h %.3f, at 93 km/h %.3f -- a gradient would have\n"
				"    fallen by roughly 10x across that span\n",
			slow, fast);
	CHECK(within_relative(fast, slow, 0.05));
}

TEST_CASE("the solid axle's scrub couple is what cancels the steering moment") {
	// The mechanism. At 0.09 g every tire is deep in its linear range, so this is
	// not saturation and no combined-slip law is involved.
	std::printf("\n    #137: THE YAW BALANCE in the linear range\n");
	std::printf("    %8s %7s | %9s %9s %9s %9s | %8s\n", "km/h", "lock", "Mz F lat",
			"Mz F lon", "Mz R lat", "Mz R lon", "lat g");

	// The share of the front's steering moment that the **couple alone** carries.
	// It is essentially all of it up to 43 km/h and then the rear tires start
	// taking a share as the rear slip angle finally becomes worth something.
	struct Case {
		double speed;
		double couple_share_min;
	};
	const Case cases[] = { { 8.0, 0.85 }, { 12.0, 0.85 }, { 20.0, 0.45 } };

	for (const Case &item : cases) {
		const Response run = steering_response(0.05, item.speed);
		std::printf("    %8.1f %7.2f | %9.1f %9.1f %9.1f %9.1f | %8.3f\n",
				ms_to_kmh(item.speed), 0.05, run.moment[0], run.moment[1], run.moment[2],
				run.moment[3], run.lateral_g);

		const double steering = run.moment[0];
		const double couple = run.moment[3];
		const double rear_lateral = run.moment[2];

		// Every tire is in its linear range: 0.06 g at 29 km/h, 0.39 g at 72.
		// Nothing in this table is anywhere near a limit, which is what rules out
		// every saturation explanation including the one `test_vehicle.cpp`'s own
		// `yaw/Ack` table reaches for -- see the header comment.
		CHECK(run.lateral_g < 0.45);

		// The couple opposes the steering moment rather than adding to it...
		CHECK(steering * couple < 0.0);
		// ...and carries this much of it on its own.
		CHECK(std::fabs(couple) > item.couple_share_min * std::fabs(steering));

		// And the two of them together balance it, which is the statement that
		// there is nothing else in the yaw equation at a settled speed. 12% of a
		// 108-245 N m moment, so it is a real bound and not a formality.
		CHECK(std::fabs(steering + rear_lateral + couple) < 0.12 * std::fabs(steering));
	}

	// **The rear tires do essentially nothing at 43 km/h**, which is the sentence
	// the whole finding turns on: on any vehicle with a differential, a steering
	// input is balanced by the rear axle's lateral force, and here it carries
	// 0.3% of it.
	const Response mid = steering_response(0.05, 12.0);
	CHECK(std::fabs(mid.moment[2]) < 0.05 * std::fabs(mid.moment[0]));
}

TEST_CASE("what the frame's torsion does to the steering response") {
	// #224's lever, swept across the range `chassis_flex.h` records as published
	// rather than across an invented one. 193.62 is Fu and Wang (2007), the
	// default; Sampayo et al. (2021) quote **1,051-3,464** from a different test,
	// and `tuning.h` exposes the whole span as `frame_torsion`, step 10, so this
	// is reachable from the live tuning overlay while driving.
	//
	// The point of the sweep is that it says what the lever is worth **before**
	// anybody moves it by feel. It is a measurement, not a recommendation: no
	// value below is adopted and the default is unchanged.
	std::printf("\n    #224: STEERING RESPONSE against frame torsion, 43.2 km/h, 0.05 lock\n");
	std::printf("    %12s %10s %10s %10s %10s %10s\n", "N m/deg", "got/geo", "Mz F lat",
			"Mz R lat", "Mz R lon", "IR load N");

	double baseline = 0.0;
	for (double torsion : { 193.62, 400.0, 800.0, 1051.0, 2000.0, 3464.0 }) {
		Response run = steering_response(0.05, 12.0, torsion);
		std::printf("    %12.2f %10.3f %10.1f %10.1f %10.1f\n", torsion, run.ratio(),
				run.moment[0], run.moment[2], run.moment[3]);
		if (torsion < 200.0) {
			baseline = run.ratio();
		}
		CHECK_FALSE(run.departed);
	}

	// The finding, as one assertion: **stiffening the frame across its own
	// published range does not recover the steering.** Whatever relieves the
	// scrub couple, this lever alone is not it at 0.14 g, and a session spent
	// sweeping it by feel would have been spent for nothing.
	const double stiff = steering_response(0.05, 12.0, 3464.0).ratio();
	std::printf("    193.62 N m/deg gives %.3f, 3464 gives %.3f\n", baseline, stiff);
	CHECK(baseline > 0.40);
	CHECK(stiff < 0.75);
}

TEST_CASE("a kart at slip recovers when the driver straightens the wheel") {
	// The other half of the story, and the reason #137's remaining half is a
	// **response** defect and not an unrecoverable-vehicle defect: provoked into
	// a slide with eight tenths of lock at 93 km/h, the kart comes back every
	// time the wheel is straightened and the throttle shut, from as far as 60
	// degrees of body slip. Worth protecting -- a change that fixes the steering
	// and costs this has made the kart worse.
	std::printf("\n    #137: RECOVERY from a provoked slide\n");
	std::printf("    %10s %12s %12s %10s\n", "released", "peak after", "ended", "km/h");

	for (double release_at : { 15.0, 30.0, 45.0, 60.0 }) {
		Rig rig;
		rig.configure();
		rig.settle();
		rig.vehicle.engage(5, 25.8);
		rig.linear_velocity = -rig.basis_z * 25.8;

		bool released = false;
		double worst_after = 0.0;
		double final_slip = 0.0;
		for (int tick = 0; tick < 120 * 8; ++tick) {
			DriverInput input;
			if (!released && std::fabs(degrees(rig.body_slip())) >= release_at) {
				released = true;
			}
			if (!released) {
				input.steer = 0.80;
				input.throttle = 1.0;
			}
			rig.step(input);
			REQUIRE(rig.finite());
			if (!released) {
				continue;
			}
			final_slip = std::fabs(degrees(rig.body_slip()));
			if (final_slip > worst_after) {
				worst_after = final_slip;
			}
		}
		std::printf("    %10.1f %12.1f %12.1f %10.1f\n", release_at, worst_after, final_slip,
				ms_to_kmh(rig.linear_velocity.length()));

		// It over-rotates a little further before it comes back -- 10 to 15
		// degrees -- and then it comes back to straight.
		CHECK(worst_after < release_at + 20.0);
		CHECK(final_slip < 5.0);
	}
}

TEST_CASE("transient steering does not depart at the condition the AI departs in") {
	// ADR-0071 records the AI departing at a tenth of lock, full throttle,
	// 93 km/h, at 0.5177 of the lateral ceiling. Held open loop, that condition
	// is **stable** -- 3.2 degrees of body slip -- and so is a slalom through it
	// at three tenths of lock at every frequency a driver can produce. So the
	// AI's departure is not the vehicle's open-loop answer to its own input, and
	// #233 must be measured against this rather than assumed downstream of #137.
	std::printf("\n    #137: SLALOM at 93 km/h, throttle 1.0\n");
	std::printf("    %8s %8s %12s\n", "amp", "Hz", "max bslip");

	for (double amplitude : { 0.10, 0.20, 0.30 }) {
		for (double hertz : { 0.5, 1.0, 1.5, 2.0 }) {
			Rig rig;
			rig.configure();
			rig.settle();
			rig.vehicle.engage(5, 25.8);
			rig.linear_velocity = -rig.basis_z * 25.8;

			double worst = 0.0;
			for (int tick = 0; tick < 120 * 8; ++tick) {
				DriverInput input;
				input.steer = amplitude * std::sin(2.0 * PI * hertz * tick * TICK);
				input.throttle = 1.0;
				rig.step(input);
				REQUIRE(rig.finite());
				worst = std::fmax(worst, std::fabs(degrees(rig.body_slip())));
			}
			std::printf("    %8.2f %8.1f %12.1f\n", amplitude, hertz, worst);
			CHECK(worst < 30.0);
		}
	}
}
