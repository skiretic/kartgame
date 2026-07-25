#include "doctest.h"

#include "core/clutch.h"

#include <cmath>

// What matters about a clutch model is not that it produces a number but that it
// produces the *right kind* of number: a capacity that is a function of the
// lever and nothing else, a transmitted torque that is exactly that capacity
// while the plates are moving, a lock that can be broken by torque as well as
// made by speed, and no chatter at 240 Hz.

using kart::core::Clutch;

TEST_CASE("the lever has free play at the top and is fully clamped before the bar") {
	const Clutch clutch;

	CHECK(clutch.engagement(1.0) == doctest::Approx(0.0));
	CHECK(clutch.engagement(0.9) == doctest::Approx(0.0));
	CHECK(clutch.engagement(clutch.free_play) == doctest::Approx(0.0));
	CHECK(clutch.engagement(clutch.bite_end) == doctest::Approx(1.0));
	CHECK(clutch.engagement(0.0) == doctest::Approx(1.0));

	// Monotone across the working range, and half-clamped in the middle of it —
	// the smoothstep is symmetric, so the bite point sits where a driver's hand
	// expects it.
	double previous = 0.0;
	for (double lever = 1.0; lever >= 0.0; lever -= 0.01) {
		const double value = clutch.engagement(lever);
		CHECK(value >= previous - 1e-12);
		previous = value;
	}
	const double middle = (clutch.free_play + clutch.bite_end) * 0.5;
	CHECK(clutch.engagement(middle) == doctest::Approx(0.5));

	MESSAGE("capacity: lever 1.0 " << clutch.capacity_at(1.0) << " Nm, 0.7 "
								   << clutch.capacity_at(0.7) << " Nm, " << middle << " "
								   << clutch.capacity_at(middle) << " Nm, 0.0 "
								   << clutch.capacity_at(0.0) << " Nm");
	CHECK(clutch.capacity_at(0.0) == doctest::Approx(clutch.capacity));
	// Out-of-range input from a miscalibrated axis must not produce a
	// negative-capacity clutch, which would drive the kart backwards.
	CHECK(clutch.capacity_at(-0.5) == doctest::Approx(clutch.capacity));
	CHECK(clutch.capacity_at(1.5) == doctest::Approx(0.0));
}

TEST_CASE("locking needs converged speeds and a torque that fits") {
	const Clutch clutch;
	const double dt = 1.0 / 240.0;
	const double inertia = 0.0055;

	// Speeds together, small torque: locks.
	CHECK(clutch.can_lock(0.5, 20.0, 0.0, inertia, dt));
	// Speeds together, torque past the capacity at that lever: does not. This is
	// the condition that lets a driver hold the clutch at a slip under power
	// instead of the plates welding themselves shut the moment the speeds meet.
	CHECK_FALSE(clutch.can_lock(0.5, 20.0, 0.6, inertia, dt));
	// Sign of the slip must not matter — a clutch works the same on the overrun.
	CHECK(clutch.can_lock(-0.5, -20.0, 0.0, inertia, dt));

	// The condition that a fixed slip threshold got wrong. A full-capacity clutch
	// can absorb 45 N·m for a substep, which is 34 rad/s of engine-side slip, so
	// 5 rad/s — what a first-gear launch leaves behind after every substep —
	// counts as locked and 200 rad/s does not.
	const double window = clutch.capacity * dt / inertia;
	MESSAGE("at full clamp the lock window is " << window << " rad/s of slip at 240 Hz");
	CHECK(clutch.can_lock(5.0, 20.0, 0.0, inertia, dt));
	CHECK_FALSE(clutch.can_lock(200.0, 20.0, 0.0, inertia, dt));
	// And a half-clamped clutch has a proportionally smaller window, which is why
	// this scales without a second tuning constant.
	const double middle = (clutch.free_play + clutch.bite_end) * 0.5;
	CHECK_FALSE(clutch.can_lock(window * 0.75, 20.0, middle, inertia, dt));
}

TEST_CASE("a slipping clutch transmits its capacity, against the slip") {
	const Clutch clutch;
	const double dt = 1.0 / 240.0;
	const double inertia = 0.0055;

	// A launch: the engine is 1,000 rad/s faster than the driveline, so the
	// convergence clamp is nowhere near binding and the clutch transmits exactly
	// what the plates can hold.
	const double driving = clutch.slipping_torque(0.5, 1000.0, inertia, dt);
	CHECK(driving == doctest::Approx(clutch.capacity_at(0.5)));
	CHECK(driving > 0.0);

	// The overrun: the road is turning the engine, and the torque reverses.
	const double overrun = clutch.slipping_torque(0.5, -1000.0, inertia, dt);
	CHECK(overrun == doctest::Approx(-driving));

	// A fully released lever transmits nothing at all, no matter the slip.
	CHECK(clutch.slipping_torque(1.0, 1000.0, inertia, dt) == doctest::Approx(0.0));
}

TEST_CASE("the convergence clamp stops the clutch chattering at 240 Hz") {
	const Clutch clutch;
	const double dt = 1.0 / 240.0;
	const double inertia = 0.0055;

	// A slip small enough that a full-capacity impulse would overshoot through
	// zero. Without the clamp the sign flips every substep and the clutch buzzes;
	// with it, the torque is exactly the amount that brings the engine to the
	// driveline speed and no more.
	const double slip = 1.5;
	const double clamped = clutch.slipping_torque(0.0, slip, inertia, dt);
	MESSAGE("slip " << slip << " rad/s: capacity " << clutch.capacity_at(0.0)
					<< " Nm, transmitted " << clamped << " Nm");
	CHECK(clamped < clutch.capacity_at(0.0));
	CHECK(clamped == doctest::Approx(inertia * slip / dt));

	// And the engine really does land on the driveline speed rather than past it.
	const double after = slip - (clamped / inertia) * dt;
	CHECK(std::fabs(after) < 1e-9);
}
