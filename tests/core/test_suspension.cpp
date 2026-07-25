#include "doctest.h"

#include "core/chassis_flex.h"
#include "core/pcg32.h"
#include "core/suspension.h"
#include "core/units.h"

#include <cstdio>

// What is worth asserting about a corner of a kart is that its numbers are the
// numbers a kart actually has, and that the integration does not blow up at the
// rate the sim runs at. Both of those are measurements, so most of these cases
// print a table as well as checking a bound — ROADMAP M3b's acceptance evidence
// is meant to be the numbers, not the fact that a test passed.
//
// The measurements are printed with `MESSAGE` so `tests/run.sh` output is the
// evidence, reproducible in five seconds with no engine.

using namespace kart::core;

namespace {

// Report an integration run.
struct Settling {
	double peak_force = 0.0;
	// The largest force *after* the corner has first come back below its static
	// load. The peak on the first substep is the initial condition, not an
	// overshoot, and quoting it would make the integrator look worse at every
	// rate equally — which is exactly what it did before this was separated out.
	double rebound_peak_force = 0.0;
	double settle_seconds = 0.0;
	bool diverged = false;
	int contact_losses = 0;
};

// One corner as a 1-DOF mass on its own contact spring, integrated the way
// ADR-0032 measured Godot to integrate: symplectic Euler, velocity first, then
// position with the new velocity. Using a different scheme here would make the
// stability number this test reports a statement about a solver nobody runs.
//
// `clamp_contact` off removes the "a tire cannot pull" clamp, which turns the
// corner into the linear oscillator whose stability the acceptance item is about.
// On, it is the real thing, and the real thing leaves the ground.
Settling run_corner(const CornerSuspension &corner, double corner_mass, double dt,
		double initial_compression, int steps, bool clamp_contact) {
	const double static_load = corner.setup.static_load();
	// Ray length is the state. Shorter is more compressed.
	double ray_length = corner.setup.rest_length - initial_compression;
	double velocity = 0.0; // Positive is the mount moving down, compressing.

	Settling result;
	double last_outside = 0.0;
	bool was_grounded = true;
	bool released = false;

	for (int step = 0; step < steps; ++step) {
		WheelContact contact;
		contact.hit = true;
		contact.ray_length = ray_length;
		contact.normal = Vec3(0.0, 1.0, 0.0);

		CornerState state;
		corner.step(contact, velocity, state);

		double force = state.normal_force;
		if (!clamp_contact) {
			// Rebuild the unclamped force so the linear case is genuinely linear.
			const double deflection = corner.setup.max_droop + state.compression;
			const double damping = velocity > 0.0 ? corner.setup.bump_damping
												  : corner.setup.rebound_damping;
			force = corner.setup.spring_rate * deflection + damping * velocity;
		}
		if (force > result.peak_force) {
			result.peak_force = force;
		}
		if (force < static_load) {
			released = true;
		}
		if (released && force > result.rebound_peak_force) {
			result.rebound_peak_force = force;
		}
		if (clamp_contact) {
			if (was_grounded && !state.grounded) {
				++result.contact_losses;
			}
			was_grounded = state.grounded;
		}

		// The corner mass is pushed up by the contact and down by gravity.
		const double acceleration = (force - static_load) / corner_mass;
		velocity -= acceleration * dt; // Positive velocity compresses, so up is negative.
		ray_length -= velocity * dt;

		if (!(std::isfinite(ray_length) && std::isfinite(velocity)) ||
				std::fabs(ray_length) > 100.0) {
			result.diverged = true;
			break;
		}
		if (std::fabs(force - static_load) > 0.02 * static_load) {
			last_outside = (step + 1) * dt;
		}
	}
	result.settle_seconds = last_outside;
	return result;
}

} // namespace

TEST_CASE("the four corners carry the kart's mass in the ratio the KZ figures give") {
	const ChassisGeometry geometry;
	double total = 0.0;
	double front = 0.0;

	MESSAGE("corner        static N   rate N/m   droop mm   rest m");
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const CornerSetup setup = corner_setup(geometry, corner);
		total += setup.static_load();
		if (corner == CORNER_FL || corner == CORNER_FR) {
			front += setup.static_load();
		}
		char line[160];
		std::snprintf(line, sizeof(line), "  %-10s %8.2f   %8.0f   %7.3f   %6.4f",
				corner == CORNER_FL ? "FL" : corner == CORNER_FR ? "FR"
						: corner == CORNER_RL							 ? "RL"
																		 : "RR",
				setup.static_load(), setup.spring_rate, setup.max_droop * 1000.0,
				setup.rest_length);
		MESSAGE(line);
	}

	// `max_droop` is derived from the load and the rate, so this is a real check
	// that the three agree and not a restatement of a constant.
	CHECK(total == doctest::Approx(175.0 * G).epsilon(1e-9));
	CHECK(front / total == doctest::Approx(geometry.front_mass_share).epsilon(1e-9));

	char summary[160];
	std::snprintf(summary, sizeof(summary), "  total %.2f N (175 kg x g = %.2f N), front share %.4f",
			total, 175.0 * G, front / total);
	MESSAGE(summary);
}

TEST_CASE("a kart's corner frequency is an order above a car's, and stable at 240 Hz") {
	const ChassisGeometry geometry;

	MESSAGE("corner   mass kg   f_n Hz   zeta bump   zeta rebound   w*dt 240   w*dt 120   c*dt/m");
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		const CornerSetup setup = corner_setup(geometry, corner);
		const double mass = geometry.corner_mass(corner);
		char line[220];
		std::snprintf(line, sizeof(line),
				"  %-6s %8.2f %8.2f %11.3f %14.3f %10.4f %10.4f %8.4f",
				corner == CORNER_FL ? "FL" : corner == CORNER_FR ? "FR"
						: corner == CORNER_RL							 ? "RL"
																		 : "RR",
				mass, setup.natural_frequency_hz(mass), setup.bump_damping_ratio(mass),
				setup.rebound_damping_ratio(mass), setup.stability_number(mass, 1.0 / 240.0),
				setup.stability_number(mass, 1.0 / 120.0),
				setup.damping_stability_number(mass, 1.0 / 240.0));
		MESSAGE(line);

		// A road car sits near 1.5 Hz. A kart has no spring but its tire, so it
		// sits an order higher, and a value that drifted into car territory would
		// mean somebody had reached for a remembered coil rate.
		CHECK(setup.natural_frequency_hz(mass) > 6.0);
		CHECK(setup.natural_frequency_hz(mass) < 20.0);

		// Symplectic Euler is stable below 2. ADR-0032 measured that Godot's
		// integrator is symplectic, and the solver has to match it.
		CHECK(setup.stability_number(mass, 1.0 / 240.0) < 0.5);
		CHECK(setup.stability_number(mass, 1.0 / 120.0) < 1.0);
		CHECK(setup.damping_stability_number(mass, 1.0 / 240.0) < 0.5);
	}
}

TEST_CASE("the corner settles rather than diverging, at 240 Hz and at 120 Hz") {
	const ChassisGeometry geometry;
	CornerSuspension corner;
	corner.setup = corner_setup(geometry, CORNER_RL);
	const double mass = geometry.corner_mass(CORNER_RL);
	const double static_load = corner.setup.static_load();

	MESSAGE("linear release from 10 mm of extra compression, no contact clamp:");
	MESSAGE("  rate      initial N   rebound peak N   overshoot of static   settle s");
	for (const double rate : { 240.0, 120.0, 60.0 }) {
		const double dt = 1.0 / rate;
		const Settling settling = run_corner(corner, mass, dt, corner.setup.max_droop + 0.010,
				static_cast<int>(rate * 4.0), false);
		char line[220];
		std::snprintf(line, sizeof(line), "  %6.0f Hz %10.1f %15.1f %20.3fx %10.4f%s", rate,
				settling.peak_force, settling.rebound_peak_force,
				settling.rebound_peak_force / static_load, settling.settle_seconds,
				settling.diverged ? "   DIVERGED" : "");
		MESSAGE(line);
		CHECK_FALSE(settling.diverged);
		if (rate >= 120.0) {
			// The acceptance item: it must settle at the rate the sim runs at,
			// not merely at some rate.
			CHECK(settling.settle_seconds < 0.5);
		}
	}

	MESSAGE("real contact, released from the bump stop (the wheel leaves the ground):");
	const Settling real = run_corner(corner, mass, 1.0 / 240.0,
			corner.setup.max_droop + corner.setup.max_travel, 240 * 4, true);
	char line[200];
	std::snprintf(line, sizeof(line),
			"  peak %.0f N (%.1fx static), %d contact losses, settled by %.3f s",
			real.peak_force, real.peak_force / static_load, real.contact_losses,
			real.settle_seconds);
	MESSAGE(line);
	CHECK_FALSE(real.diverged);
	// A kart corner has 1.8 mm of droop, so any real disturbance throws the wheel
	// off the ground. That is the behavior, not a bug, and it is why issue #32's
	// wheel lift is such an abrupt event.
	CHECK(real.contact_losses > 0);
	CHECK(real.settle_seconds < 2.0);
}

TEST_CASE("a corner on flat ground with no input does not jitter") {
	// Issue #31's first acceptance item. Two runs: the closing speed taken from
	// the chassis body, which is what the solver will do, and the closing speed
	// differenced from a noisy ray length, which is what it would do if nobody
	// had looked.
	const ChassisGeometry geometry;
	CornerSuspension corner;
	corner.setup = corner_setup(geometry, CORNER_RL);
	const double static_load = corner.setup.static_load();
	const double dt = 1.0 / 240.0;

	MESSAGE("  rear corner, 497.7 N static, 2000 substeps at 240 Hz on flat ground:");
	MESSAGE("  ray noise mm   chassis velocity   differenced + 40 Hz filter");
	for (const double noise_amplitude : { 1.0e-6, 1.0e-5, 5.0e-5 }) {
		double clean_low = static_load;
		double clean_high = static_load;
		{
			WheelContact contact;
			contact.hit = true;
			contact.normal = Vec3(0.0, 1.0, 0.0);
			Pcg32 noise(20260725u, 31u);
			for (int step = 0; step < 2000; ++step) {
				contact.ray_length = corner.setup.rest_length +
						noise.next_range(-noise_amplitude, noise_amplitude);
				CornerState state;
				// The chassis is not moving, and the chassis knows it.
				corner.step(contact, 0.0, state);
				clean_low = state.normal_force < clean_low ? state.normal_force : clean_low;
				clean_high = state.normal_force > clean_high ? state.normal_force : clean_high;
			}
		}
		const double clean_spread = clean_high - clean_low;

		double differenced_spread = 0.0;
		{
			WheelContact contact;
			contact.hit = true;
			contact.normal = Vec3(0.0, 1.0, 0.0);
			Pcg32 noise(20260725u, 31u);
			RaySpeed estimator;
			double low = static_load;
			double high = static_load;
			for (int step = 0; step < 2000; ++step) {
				contact.ray_length = corner.setup.rest_length +
						noise.next_range(-noise_amplitude, noise_amplitude);
				const double speed = estimator.update(contact.ray_length, dt);
				CornerState state;
				corner.step(contact, speed, state);
				if (step < 10) {
					continue; // The filter has to prime.
				}
				low = state.normal_force < low ? state.normal_force : low;
				high = state.normal_force > high ? state.normal_force : high;
			}
			differenced_spread = high - low;
		}

		char line[240];
		std::snprintf(line, sizeof(line),
				"  %12.4f %11.2f N %5.2f%% %13.2f N %6.2f%%", noise_amplitude * 1000.0,
				clean_spread, 100.0 * clean_spread / static_load, differenced_spread,
				100.0 * differenced_spread / static_load);
		MESSAGE(line);

		// The differenced path is measurably worse at every noise level, which is
		// the whole point of taking the velocity from the chassis. If this ever
		// stops being true, the filter has turned the damper into a spring.
		CHECK(differenced_spread > clean_spread);
	}

	// **The remaining jitter is the spring, not the damper**, and it does not
	// come out in this file. A kart tire is a 277 kN/m spring, so a tenth of a
	// millimeter of raycast noise is 28 N — 5.6% of the corner's static load —
	// with the velocity taken exactly from the chassis and the damper
	// contributing nothing. Issue #31's "no jitter" therefore has a hard
	// dependency on the raycast's own precision, and that is a Godot-side
	// number: how far the kart is from the world origin, whether the ray starts
	// inside its own collider, and Jolt's contact tolerance. Recorded here as a
	// measured bound rather than left to be discovered while driving.
	{
		WheelContact contact;
		contact.hit = true;
		contact.normal = Vec3(0.0, 1.0, 0.0);
		CornerState state;
		contact.ray_length = corner.setup.rest_length - 1.0e-4;
		corner.step(contact, 0.0, state);
		const double per_tenth_mm = state.normal_force - static_load;
		char line[200];
		std::snprintf(line, sizeof(line),
				"  0.1 mm of ray error is %.1f N on this corner (%.2f%% of static)"
				" from the spring alone", per_tenth_mm, 100.0 * per_tenth_mm / static_load);
		MESSAGE(line);
		CHECK(per_tenth_mm == doctest::Approx(corner.setup.spring_rate * 1.0e-4));
	}

	// A perfectly clean ray gives a perfectly constant force. Stated separately
	// because it is the only part of "no jitter" that can be checked exactly.
	{
		WheelContact contact;
		contact.hit = true;
		contact.ray_length = corner.setup.rest_length;
		contact.normal = Vec3(0.0, 1.0, 0.0);
		double first = 0.0;
		for (int step = 0; step < 1000; ++step) {
			CornerState state;
			corner.step(contact, 0.0, state);
			if (step == 0) {
				first = state.normal_force;
			}
			CHECK(state.normal_force == doctest::Approx(first).epsilon(1e-15));
		}
		CHECK(first == doctest::Approx(static_load).epsilon(1e-12));
	}
}

TEST_CASE("a wheel off the ground carries nothing and reports how far off it is") {
	const ChassisGeometry geometry;
	CornerSuspension corner;
	corner.setup = corner_setup(geometry, CORNER_RL);

	WheelContact contact;
	contact.hit = true;
	contact.normal = Vec3(0.0, 1.0, 0.0);
	CornerState state;

	// Exactly at full droop the force is zero, not negative.
	contact.ray_length = corner.setup.free_length();
	corner.step(contact, 0.0, state);
	CHECK(state.normal_force == doctest::Approx(0.0));
	CHECK_FALSE(state.grounded);
	CHECK(corner.lift_height(contact, state) == doctest::Approx(0.0));

	// 20 mm past it, which is what issue #32's inside rear should look like.
	contact.ray_length = corner.setup.free_length() + 0.020;
	corner.step(contact, 0.0, state);
	CHECK(state.normal_force == doctest::Approx(0.0));
	CHECK(corner.lift_height(contact, state) == doctest::Approx(0.020).epsilon(1e-12));

	// And the ray missing entirely.
	contact.hit = false;
	corner.step(contact, 0.0, state);
	CHECK(state.normal_force == doctest::Approx(0.0));
	CHECK_FALSE(state.grounded);
}

TEST_CASE("the normal force is never negative, at any velocity") {
	// `tire.h` guards its own input, but that guard should never be the thing
	// that catches this: a negative normal load reaching the tire model is a tire
	// that accelerates the kart sideways, and by then it has already been through
	// the chassis force sum.
	const ChassisGeometry geometry;
	CornerSuspension corner;
	corner.setup = corner_setup(geometry, CORNER_FL);

	Pcg32 draw(7u, 32u);
	for (int trial = 0; trial < 5000; ++trial) {
		WheelContact contact;
		contact.hit = draw.next_double() > 0.1;
		contact.ray_length = corner.setup.rest_length + draw.next_range(-0.05, 0.10);
		contact.normal = Vec3(0.0, 1.0, 0.0);
		CornerState state;
		state.geometric_offset = draw.next_range(-0.03, 0.03);
		corner.step(contact, draw.next_range(-40.0, 40.0), state);
		CHECK(state.normal_force >= 0.0);
		CHECK(std::isfinite(state.normal_force));
	}
}

TEST_CASE("the bump stop is firm and continuous") {
	const ChassisGeometry geometry;
	CornerSuspension corner;
	corner.setup = corner_setup(geometry, CORNER_FL);

	WheelContact contact;
	contact.hit = true;
	contact.normal = Vec3(0.0, 1.0, 0.0);
	CornerState state;

	const double at_limit = corner.setup.rest_length - corner.setup.max_travel;
	contact.ray_length = at_limit;
	corner.step(contact, 0.0, state);
	const double force_at_limit = state.normal_force;

	contact.ray_length = at_limit - 1.0e-6;
	corner.step(contact, 0.0, state);
	const double just_past = state.normal_force;

	// Continuous across the limit: the extra rate acts on the excess only, so a
	// micron past the stop is a fraction of a newton past the force. A step here
	// is a bang the driver feels and the integrator eats in one substep.
	CHECK(just_past - force_at_limit < 5.0);
	CHECK(just_past > force_at_limit);

	contact.ray_length = at_limit - 0.005;
	corner.step(contact, 0.0, state);
	const double incremental_rate = (state.normal_force - force_at_limit) / 0.005;
	char line[240];
	std::snprintf(line, sizeof(line),
			"  front corner: %.0f N at the %.0f mm travel limit (%.1fx static), %.0f N"
			" 5 mm past it; rate past the stop %.0f N/m = %.1fx the spring",
			force_at_limit, corner.setup.max_travel * 1000.0,
			force_at_limit / corner.setup.static_load(), state.normal_force,
			incremental_rate, incremental_rate / corner.setup.spring_rate);
	MESSAGE(line);
	// Firm enough that a curb does not run the ray out of length. Issue #31. The
	// rate is what matters, not the force: the spring is already at 3.5 kN by the
	// time the stop is reached, so a force ratio would look reassuring at any
	// stop rate at all.
	CHECK(incremental_rate == doctest::Approx(corner.setup.spring_rate *
									 (1.0 + corner.bump_stop_ratio))
									 .epsilon(1e-9));
}

TEST_CASE("a positive geometric offset lifts the chassis") {
	// The sign of `geometric_offset` is the contract `steering.h` is written
	// against, and it is a sign, so it gets a test rather than a comment.
	const ChassisGeometry geometry;
	CornerSuspension corner;
	corner.setup = corner_setup(geometry, CORNER_FL);

	WheelContact contact;
	contact.hit = true;
	contact.ray_length = corner.setup.rest_length;
	contact.normal = Vec3(0.0, 1.0, 0.0);

	CornerState neutral;
	corner.step(contact, 0.0, neutral);

	CornerState jacked;
	jacked.geometric_offset = 0.005;
	corner.step(contact, 0.0, jacked);

	// More force at the same ray length means the corner is pushing the chassis
	// up harder, which is what "lifts the chassis" has to mean.
	CHECK(jacked.normal_force > neutral.normal_force);
	CHECK(jacked.normal_force - neutral.normal_force ==
			doctest::Approx(corner.setup.spring_rate * 0.005).epsilon(1e-12));
}

TEST_CASE("a contact normal off vertical carries less of the spring force") {
	const Vec3 down(0.0, -1.0, 0.0);
	CHECK(normal_scale(Vec3(0.0, 1.0, 0.0), down) == doctest::Approx(1.0));
	// 30 degrees of bank.
	const Vec3 banked = Vec3(0.5, std::sqrt(3.0) / 2.0, 0.0).normalized();
	CHECK(normal_scale(banked, down) == doctest::Approx(std::sqrt(3.0) / 2.0).epsilon(1e-12));
	// A wall does not delete the corner's load; it floors it.
	CHECK(normal_scale(Vec3(1.0, 0.0, 0.0), down) == doctest::Approx(0.1));
}
