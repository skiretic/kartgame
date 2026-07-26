#include "doctest.h"

#include "core/chassis.h"
#include "core/chassis_flex.h"
#include "core/steering.h"
#include "core/tire.h"
#include "core/units.h"

#include <cstdio>

// **Issue #32's acceptance, as arithmetic.**
//
// `ARCHITECTURE.md` §6 calls the inside-rear lift the defining kart dynamic, and
// issue #32 says everything else in M3b is ordinary vehicle physics and this is
// the part that makes it a kart. Its acceptance criteria are written to be
// judged by looking at a render — "the inside rear wheel **visibly lifts**". This
// file settles the half that can be settled without pixels, and it is the half
// that decides whether the render is ever worth looking at.
//
// ## Why this file exists at all
//
// `steering.h` and `chassis_flex.h` were written independently and neither
// includes the other. One computes how far the caster geometry pushes a front
// contact patch down when the wheel is turned; the other computes what the four
// tire loads do when something pushes a corner down. Each was tested against its
// own limiting cases and each passes.
//
// Neither can answer the question the milestone is about. `steering.h` reports
// millimeters and has no idea what a millimeter is worth; `chassis_flex.h`
// reports the millimeters it *needs* and has no idea whether anything can supply
// them. The number that matters lives in the join, and a join is exactly what no
// single-file test covers.
//
// It also carries the one place a sign error survives everything else. The two
// files use opposite conventions, honestly and for good reasons of their own:
//
//   * `steering.h`: `contact_offset.y < 0` means the patch went **down**, which
//     lifts the chassis at that corner.
//   * `chassis_flex.h`: `geometric_offset > 0` **lifts the chassis** at that
//     corner.
//
// So the adapter is a negation, and if it were missing every test in both files
// would still pass while the kart lifted its outside rear — which is not a kart
// understeering, it is a kart on its roof. The first test below is that negation
// and nothing else.

using namespace kart::core;

namespace {

// The chassis geometry, with the center-of-mass height taken from the mass
// table rather than from `ChassisGeometry`'s default.
//
// `chassis_flex.h` defaults `com_height` to 0.23 and says in its comment that
// the figure is "derived in kart_debug_vehicle.gd from two masses". That was the
// best number available when it was written. `chassis.h` now derives 0.2197 from
// twenty-one, so the two are reconciled here rather than left to disagree by
// **4.7%** in a quantity that every threshold in this file divides by — which is
// a 4.5% error in the thresholds themselves. This comment said 1.4% for a
// milestone; the figure was wrong by more than three times, and it made the
// reconciliation look optional. Issue #129 is what the same defect did to
// `chassis_flex.h`, where the assertions pinned the default and nobody noticed.
ChassisGeometry kart_geometry() {
	ChassisGeometry geometry;
	const MassProperties properties = kz::kart_mass_properties();
	geometry.mass = properties.mass;
	geometry.com_height = properties.center_of_mass.y;
	geometry.front_mass_share = kz::STATIC_FRONT_SHARE;
	return geometry;
}

// Turn a steering input into the per-corner geometric offsets the load solver
// consumes. **This is the adapter, and it is the only place the sign lives.**
LoadCase load_case_at_steer(double steer_input) {
	LoadCase load_case;
	load_case.geometry = kart_geometry();

	const SteeringGeometry steering = kz_front_geometry();
	const SteeringOutput steered = solve_steering(steering, steer_input);

	load_case.geometric_offset[CORNER_FL] = -steered.left.contact_offset.y;
	load_case.geometric_offset[CORNER_FR] = -steered.right.contact_offset.y;
	// The rear wheels do not steer, so nothing jacks them. The rear rises
	// because the frame twists, which is what `chassis_flex.h` solves for.
	load_case.geometric_offset[CORNER_RL] = 0.0;
	load_case.geometric_offset[CORNER_RR] = 0.0;
	return load_case;
}

} // namespace

TEST_CASE("the jacking sign adapter lifts the chassis at the inside front") {
	// Turning left. `solve_steering` takes positive input as a left turn, and
	// `LoadCase::lateral_g` takes positive as transferring load to the right —
	// which is the same corner: turning left throws the mass outward, to the
	// right, and the left-hand wheels are the inside ones. The two files agree on
	// that without ever having been introduced.
	const SteeringOutput steered = solve_steering(kz_front_geometry(), 1.0);

	// The inside (left) patch goes down, the outside (right) patch comes up.
	CHECK(steered.left.contact_offset.y < 0.0);
	CHECK(steered.right.contact_offset.y > 0.0);

	const LoadCase load_case = load_case_at_steer(1.0);
	// So after the negation the chassis is lifted at the inside front and pulled
	// down at the outside front. Reversed, the kart would jack its outside rear
	// into the air in a corner, and both files' own suites would still be green.
	CHECK(load_case.geometric_offset[CORNER_FL] > 0.0);
	CHECK(load_case.geometric_offset[CORNER_FR] < 0.0);
}

TEST_CASE("load transfer alone cannot lift the inside rear on this kart") {
	// The premise of the whole ticket, restated as a test. With the wheels
	// straight there is nothing to excite the warp mode, and the inside rear
	// stays down past any lateral acceleration the tires can produce.
	LoadCase straight = load_case_at_steer(0.0);
	const double rear_lift = lift_threshold_g(straight, CORNER_RL);

	// `tire.h` peaks at a friction coefficient of 2.10, and with no downforce the
	// achievable lateral g is bounded by it — lower still once load sensitivity
	// is accounted for. So a threshold above 2.10 is a threshold the kart cannot
	// reach by gripping harder.
	Tire tire;
	CHECK(rear_lift > tire.lateral.peak_friction);

	// And raising grip is not the escape, because the kart tips first. This is
	// ADR-0031's argument, recomputed from the mass table.
	const MassProperties properties = kz::kart_mass_properties();
	CHECK(rear_lift > kz::rollover_threshold_g(properties, true));
}

TEST_CASE("caster jacking lifts the inside rear inside the usable lateral range") {
	// The join. Full lock, and the question is whether the millimeters
	// `steering.h` produces are worth more than the millimeters
	// `chassis_flex.h` needs.
	LoadCase locked = load_case_at_steer(1.0);
	const double rear_lift = lift_threshold_g(locked, CORNER_RL);

	// It must happen below the tire's own ceiling, or it never happens.
	Tire tire;
	CHECK(rear_lift < tire.lateral.peak_friction);

	// It must happen below the rollover threshold, or the kart is on its side
	// before the wheel comes up and the "lift" is a crash.
	const MassProperties properties = kz::kart_mass_properties();
	CHECK(rear_lift < kz::rollover_threshold_g(properties, true));

	// And it must be the *inside rear*. A model that lifts the inside front is
	// the shopping trolley issue #32 warns about — the locked axle keeps both
	// rear tires planted, scrubs, and the kart pushes wide.
	LoadCase at_lift = locked;
	at_lift.lateral_g = rear_lift + 0.05;
	const CornerLoads loads = solve_corner_loads(at_lift);
	CHECK(loads.normal_force[CORNER_RL] <= 0.0);
	CHECK(loads.normal_force[CORNER_FL] > 0.0);
}

TEST_CASE("the lift is progressive with steering angle") {
	// Issue #35 asks that jacking "scales with steering angle as caster geometry
	// predicts", and issue #32 wants the kart to rotate on the lifted wheel
	// rather than switch onto it. Both want the threshold to fall smoothly as
	// lock is wound on, with no step.
	double previous = 99.0;
	for (int step = 2; step <= 10; ++step) {
		LoadCase load_case = load_case_at_steer(static_cast<double>(step) * 0.1);
		const double threshold = lift_threshold_g(load_case, CORNER_RL);
		CHECK(threshold <= previous);
		previous = threshold;
	}
}

TEST_CASE("report the inside-rear lift") {
	// The evidence issue #32 is closed with, printed rather than asserted.
	const MassProperties properties = kz::kart_mass_properties();
	Tire tire;

	std::printf("\n    steering.h and chassis_flex.h, joined\n");
	std::printf("    tire ceiling %.2f g, rollover %.2f g turning left\n",
			tire.lateral.peak_friction, kz::rollover_threshold_g(properties, true));
	std::printf("    %-8s %10s %10s %12s %14s\n",
			"input", "inner deg", "jack mm", "lifts at g", "lift at 2.0 g mm");
	for (int step = 0; step <= 10; ++step) {
		const double input = static_cast<double>(step) * 0.1;
		const SteeringOutput steered = solve_steering(kz_front_geometry(), input);
		LoadCase load_case = load_case_at_steer(input);
		const double threshold = lift_threshold_g(load_case, CORNER_RL);

		// The antisymmetric part of the front jacking, which is the component
		// that reaches the warp mode. The symmetric part just raises the kart.
		const double antisymmetric =
				(load_case.geometric_offset[CORNER_FL] - load_case.geometric_offset[CORNER_FR]) * 0.5;

		load_case.lateral_g = 2.0;
		const CornerLoads at_two_g = solve_corner_loads(load_case);

		std::printf("    %-8.1f %10.2f %10.3f %12.3f %14.2f\n",
				input,
				steered.left.angle * 180.0 / PI,
				antisymmetric * 1000.0,
				threshold,
				at_two_g.lift(CORNER_RL) * 1000.0);
	}
	CHECK(properties.mass > 0.0);
}
