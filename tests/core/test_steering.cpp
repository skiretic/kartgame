#include "doctest.h"

#include <cstdio>

#include "core/steering.h"
#include "core/units.h"
#include "core/vec3.h"

// The front end is the one part of the vehicle where the *sign* of a number
// decides whether the thing on screen is a kart or a rollover, so most of what
// follows is aimed at signs and at limiting cases rather than at magnitudes.
//
// The three cases that separate the two jacking terms completely, and which
// would each have caught the likeliest error in the derivation:
//
//   * a vertical kingpin jacks by exactly zero, at any steer angle;
//   * caster alone is antisymmetric in steer direction — the two front wheels go
//     opposite ways, which is what twists the frame;
//   * inclination alone is symmetric — both fronts lift the chassis whichever way
//     you steer, and neither direction is preferred.
//
// Mixing up which term does which is the single most likely mistake in this
// file's subject, and no two of those three can hold at once for a derivation
// that has them the wrong way round.
//
// ## Why these tests print
//
// Several cases end in `printf`. doctest's `MESSAGE` only surfaces on failure or
// under `--success`, and the numbers below — the lock angles, the jacking in
// millimeters, the turn radius — are the acceptance evidence for issue #35 and
// have to be readable in an ordinary run. They are measurements the suite takes,
// not decoration.

using namespace kart::core;

static constexpr double DEGREES = PI / 180.0;

static double to_degrees(double radians) {
	return radians / DEGREES;
}

// A front end with one term at a time, for the limiting cases. Deliberately not
// `kz_front_geometry()` — a limiting case wants a geometry chosen to isolate the
// thing being tested, and reusing the shipping numbers would mean the tests stop
// meaning anything the day someone retunes the kart.
static SteeringGeometry isolated(double caster_degrees, double inclination_degrees) {
	SteeringGeometry geometry;
	geometry.caster = caster_degrees * DEGREES;
	geometry.kingpin_inclination = inclination_degrees * DEGREES;
	geometry.scrub_radius = 0.063;
	geometry.max_lock = 25.0 * DEGREES;
	geometry.ackermann = 1.0;
	geometry.wheelbase = 1.050;
	geometry.track_front = 1.105;
	return geometry;
}

// Where the perpendicular to one front wheel crosses the rear axle line, as a
// lateral coordinate on that line. This is what Ackermann geometry actually
// asserts: both wheels name the same point, and the kart therefore rotates about
// it rather than dragging a tire sideways.
//
// Origin at the middle of the wheelbase, front axle at z = -wheelbase/2 because
// forward is -Z. A wheel at `angle` points along (-sin, 0, -cos); the
// perpendicular to that through the kingpin reaches z = +wheelbase/2 after a
// distance wheelbase/sin(angle), which lands it at the x below.
static double turn_center_x(const SteeringGeometry &geometry, Side side, double angle) {
	const double kingpin_x = (side == Side::left ? -0.5 : 0.5) * geometry.track_front;
	return kingpin_x - geometry.wheelbase / std::tan(angle);
}

// The height the inside rear contact patch is lifted to, meters, taking the
// chassis as a rigid rectangle standing on the other three wheels.
//
// A contact patch that has moved **down** relative to the chassis has lengthened
// that corner's leg, so the leg change is `-contact_offset.y`. For a rigid
// rectangle the fourth corner's height is `e_front_inner + e_rear_outer -
// e_front_outer`, and the rear legs do not change, which leaves the difference
// of the two front terms. That difference is exactly the antisymmetric part of
// the jacking: the symmetric part — everything kingpin inclination contributes on
// its own — lifts both fronts equally, which pitches the kart and lifts nothing.
//
// This is the number that decides whether the wheel-lift ticket has anything to
// work with, and it is geometry only: no load transfer, no chassis flex. The
// real kart gets more from the first and less from the second.
static double rigid_inside_rear_lift(const SteeringOutput &output, bool turning_left) {
	const double inner = turning_left ? output.left.contact_offset.y : output.right.contact_offset.y;
	const double outer = turning_left ? output.right.contact_offset.y : output.left.contact_offset.y;
	return outer - inner;
}

TEST_CASE("the kingpin axis reads back the angles it was built from") {
	// A setup number that does not measure back is a small lie that costs an
	// afternoon, so the construction is asserted against what a person with a
	// gauge would see: caster in the side view, inclination in the front view.
	const SteeringGeometry geometry = isolated(18.0, 11.0);

	for (const Side side : { Side::left, Side::right }) {
		const Vec3 axis = kingpin_axis(geometry, side);
		CHECK(axis.length() == doctest::Approx(1.0));
		// Side view: the top leans rearward, which is +Z.
		CHECK(to_degrees(std::atan2(axis.z, axis.y)) == doctest::Approx(18.0));
		// Front view: the top leans inboard, +X on the left of the kart and -X on
		// the right.
		const double inboard = (side == Side::left) ? 1.0 : -1.0;
		CHECK(to_degrees(std::atan2(inboard * axis.x, axis.y)) == doctest::Approx(11.0));
		CHECK(axis.y > 0.0); // it points up, and everything below assumes so
	}
}

TEST_CASE("positive caster puts the contact patch behind the kingpin") {
	// Mechanical trail, which is what makes the steering self-center and which
	// this file never sets directly — it falls out of placing the contact patch
	// under the wheel center. Forward is -Z, so "behind" is a larger z.
	const SteeringGeometry geometry = isolated(18.0, 0.0);
	const double trail = FRONT_WHEEL_RADIUS * std::tan(geometry.caster);
	CHECK(trail > 0.0);
	CHECK(trail == doctest::Approx(0.0455).epsilon(0.01));
}

TEST_CASE("a vertical kingpin jacks by exactly zero") {
	// The case that catches a derivation built on the wrong axis. With no caster
	// and no inclination the kingpin is vertical, the contact patch sweeps a
	// horizontal circle, and the height cannot change — not to a tolerance, but
	// bit-for-bit, because every term of Rodrigues' formula that could contribute
	// a vertical component is identically zero.
	const SteeringGeometry geometry = isolated(0.0, 0.0);
	for (int step = -10; step <= 10; ++step) {
		const double angle = static_cast<double>(step) * 4.0 * DEGREES;
		CHECK(contact_offset(geometry, Side::left, angle).y == 0.0);
		CHECK(contact_offset(geometry, Side::right, angle).y == 0.0);
	}
}

TEST_CASE("caster alone is antisymmetric — one front rises, the other falls") {
	const SteeringGeometry geometry = isolated(18.0, 0.0);
	const double angle = 25.0 * DEGREES;

	const double left = contact_offset(geometry, Side::left, angle).y;
	const double right = contact_offset(geometry, Side::right, angle).y;

	// Steering left: the inside (left) patch goes down, lifting the chassis at
	// that corner; the outside goes up. Opposite signs is the whole point — it is
	// what twists the frame instead of merely raising it.
	CHECK(left < 0.0);
	CHECK(right > 0.0);

	// Reversing the steer swaps them.
	CHECK(contact_offset(geometry, Side::left, -angle).y > 0.0);
	CHECK(contact_offset(geometry, Side::right, -angle).y < 0.0);

	// The residue that is *not* antisymmetric comes from the mechanical trail:
	// the patch sits behind the kingpin as well as outboard of it, and rotating
	// that longitudinal offset about a raked axis contributes a small symmetric
	// term. It is real and it measures about a sixth of the antisymmetric part,
	// which is the claim worth pinning — it must not be the dominant one.
	const double forward = contact_offset(geometry, Side::left, angle).y;
	const double backward = contact_offset(geometry, Side::left, -angle).y;
	const double antisymmetric = (forward - backward) * 0.5;
	const double symmetric = (forward + backward) * 0.5;
	CHECK(std::fabs(symmetric) < std::fabs(antisymmetric) * 0.25);

	std::printf("\n[steering] caster 18 deg alone, 25 deg of lock:\n");
	std::printf("           left  %+7.3f mm   right %+7.3f mm\n", left * 1000.0, right * 1000.0);
	std::printf("           antisymmetric %+7.3f mm, symmetric residue %+7.3f mm\n",
			antisymmetric * 1000.0, symmetric * 1000.0);
}

TEST_CASE("kingpin inclination alone is symmetric and always lifts the chassis") {
	const SteeringGeometry geometry = isolated(0.0, 11.0);
	const double angle = 25.0 * DEGREES;

	// Both wheels, both directions: the patch goes down relative to the chassis,
	// so the chassis goes up. This is why a car with a lot of inclination
	// self-centers under its own weight with the engine off, and why kart
	// steering is heavy in the pits and light on track.
	CHECK(contact_offset(geometry, Side::left, angle).y < 0.0);
	CHECK(contact_offset(geometry, Side::right, angle).y < 0.0);
	CHECK(contact_offset(geometry, Side::left, -angle).y < 0.0);
	CHECK(contact_offset(geometry, Side::right, -angle).y < 0.0);

	// Symmetric to machine precision: steering left and steering right cannot be
	// different amounts of lift, and if they are, the caster term has leaked in.
	CHECK(contact_offset(geometry, Side::left, angle).y ==
			doctest::Approx(contact_offset(geometry, Side::left, -angle).y).epsilon(1e-12));
	CHECK(contact_offset(geometry, Side::left, angle).y ==
			doctest::Approx(contact_offset(geometry, Side::right, angle).y).epsilon(1e-12));

	// And therefore it lifts no wheel: a rigid frame raised equally at both
	// fronts has pitched, not twisted.
	SteeringOutput output;
	output.left.contact_offset = contact_offset(geometry, Side::left, angle);
	output.right.contact_offset = contact_offset(geometry, Side::right, angle);
	CHECK(std::fabs(rigid_inside_rear_lift(output, true)) < 1e-12);

	std::printf("[steering] inclination 11 deg alone, 25 deg of lock: %+7.3f mm both fronts\n",
			contact_offset(geometry, Side::left, angle).y * 1000.0);
}

TEST_CASE("the two terms have the shapes the geometry says they should") {
	// Caster goes as sin(steer), inclination as 1 - cos(steer). Asserted by
	// dividing the measurement through by the claimed shape and checking the
	// quotient is the same number at every angle, which is a much stronger
	// statement than "it grows".
	const SteeringGeometry caster_only = isolated(18.0, 0.0);
	const SteeringGeometry inclination_only = isolated(0.0, 11.0);

	double caster_quotient = 0.0;
	double inclination_quotient = 0.0;
	for (int step = 1; step <= 5; ++step) {
		const double angle = static_cast<double>(step) * 5.0 * DEGREES;

		// Take the antisymmetric part of the caster case so the trail's symmetric
		// residue does not contaminate the shape being measured.
		const double forward = contact_offset(caster_only, Side::left, angle).y;
		const double backward = contact_offset(caster_only, Side::left, -angle).y;
		const double caster_scaled = ((forward - backward) * 0.5) / std::sin(angle);

		const double inclination_scaled =
				contact_offset(inclination_only, Side::left, angle).y / (1.0 - std::cos(angle));

		if (step == 1) {
			caster_quotient = caster_scaled;
			inclination_quotient = inclination_scaled;
		} else {
			// A few percent over a five-fold change in angle. Not exact, because
			// the wheel angle and the kingpin rotation are not the same angle and
			// they diverge with lock — that divergence is a real effect and it is
			// what the remaining error is.
			CHECK(caster_scaled == doctest::Approx(caster_quotient).epsilon(0.05));
			CHECK(inclination_scaled == doctest::Approx(inclination_quotient).epsilon(0.05));
		}
	}

	// The coefficients themselves: scrub * sin(caster) and -scrub * sin(kpi) *
	// cos(kpi), which is what the algebra gives when the trail is set aside.
	CHECK(caster_quotient ==
			doctest::Approx(-0.063 * std::sin(18.0 * DEGREES)).epsilon(0.05));
	CHECK(inclination_quotient ==
			doctest::Approx(-0.063 * std::sin(11.0 * DEGREES) * std::cos(11.0 * DEGREES))
							.epsilon(0.05));
}

TEST_CASE("the wheel angle and the kingpin rotation invert exactly") {
	// The forward map is a rotation and a projection; the inverse is bisection.
	// If the two disagree, every jacking figure is computed at the wrong angle.
	const SteeringGeometry geometry = kz_front_geometry();
	for (int step = -8; step <= 8; ++step) {
		const double wanted = static_cast<double>(step) * 3.0 * DEGREES;
		for (const Side side : { Side::left, Side::right }) {
			const double rotation = kingpin_rotation_for_wheel_angle(geometry, side, wanted);
			CHECK(wheel_angle_for_kingpin_rotation(geometry, side, rotation) ==
					doctest::Approx(wanted).epsilon(1e-12));
		}
	}

	// They are not the same angle, and it is worth knowing by how much: the
	// kingpin has to turn further than the wheel ends up pointing.
	const double lock = geometry.max_lock;
	const double rotation = kingpin_rotation_for_wheel_angle(geometry, Side::left, lock);
	CHECK(rotation > lock);
	std::printf("[steering] to point the wheel at %.2f deg the kingpin turns %.2f deg\n",
			to_degrees(lock), to_degrees(rotation));
}

TEST_CASE("the inner wheel turns more sharply than the outer") {
	const SteeringGeometry geometry = kz_front_geometry();
	const SteeringOutput output = solve_steering(geometry, 1.0);

	// Full left lock: the left wheel is inside.
	CHECK(output.left.angle > output.right.angle);
	CHECK(output.left.angle == doctest::Approx(geometry.max_lock));

	const double inner = to_degrees(output.left.angle);
	const double outer = to_degrees(output.right.angle);
	CHECK(inner - outer > 5.0);

	// Mirrored at full right lock, exactly. A front end that steers differently
	// left and right is a bent chassis, not a design.
	const SteeringOutput mirrored = solve_steering(geometry, -1.0);
	CHECK(mirrored.right.angle == doctest::Approx(-output.left.angle));
	CHECK(mirrored.left.angle == doctest::Approx(-output.right.angle));

	std::printf("[steering] full lock: inner %.2f deg, outer %.2f deg, difference %.2f deg\n",
			inner, outer, inner - outer);
}

TEST_CASE("at true Ackermann both front wheels name the same turn center") {
	// The definition, asserted geometrically. Comparing two angles only says the
	// inner one is bigger; this says the kart can actually rotate about a point
	// instead of dragging a tire across the road.
	const SteeringGeometry geometry = kz_front_geometry();

	for (int step = 1; step <= 10; ++step) {
		const double input = static_cast<double>(step) / 10.0;
		const SteeringOutput output = solve_steering(geometry, input);
		const double from_left = turn_center_x(geometry, Side::left, output.left.angle);
		const double from_right = turn_center_x(geometry, Side::right, output.right.angle);
		CHECK(from_left == doctest::Approx(from_right).epsilon(1e-9));
		// And it is on the correct side: steering left puts the center to the
		// left, which is -X.
		CHECK(from_left < 0.0);
	}

	// Parallel steer, by contrast, cannot: the two wheels disagree about where
	// the kart is turning by the full width of the front track, and the
	// difference is the scrub a kart without Ackermann pays for at every corner.
	SteeringGeometry parallel = geometry;
	parallel.ackermann = 0.0;
	const SteeringOutput flat = solve_steering(parallel, 1.0);
	const double disagreement =
			std::fabs(turn_center_x(parallel, Side::left, flat.left.angle) -
					turn_center_x(parallel, Side::right, flat.right.angle));
	CHECK(disagreement == doctest::Approx(parallel.track_front).epsilon(1e-9));

	std::printf("[steering] parallel steer at full lock: the two fronts disagree about the\n");
	std::printf("           turn center by %.3f m — one full track width\n", disagreement);
}

TEST_CASE("the Ackermann fraction interpolates and can be exceeded") {
	SteeringGeometry geometry = kz_front_geometry();
	const double inner = geometry.max_lock;

	geometry.ackermann = 0.0;
	const double parallel = ackermann_outer_angle(geometry, inner);
	geometry.ackermann = 1.0;
	const double full = ackermann_outer_angle(geometry, inner);
	geometry.ackermann = 0.5;
	const double half = ackermann_outer_angle(geometry, inner);
	geometry.ackermann = 1.4;
	const double beyond = ackermann_outer_angle(geometry, inner);

	CHECK(parallel == doctest::Approx(inner));
	CHECK(full < parallel);
	CHECK(half == doctest::Approx((parallel + full) * 0.5));
	// Over-Ackermann is a real kart setting — the inner hole on the stub axle —
	// and it must not clamp at 1.0.
	CHECK(beyond < full);
}

TEST_CASE("turn radius at full lock is a kart's, not a car's") {
	const SteeringGeometry geometry = kz_front_geometry();
	const SteeringOutput output = solve_steering(geometry, 1.0);
	const double radius = turn_radius(geometry, output.left.angle, output.right.angle);

	// A kart turns inside essentially anything else on four wheels. Under 3.5 m
	// to the middle of the rear axle, which is a circle a road car needs three
	// times the room for.
	CHECK(radius > 2.0);
	CHECK(radius < 3.5);

	// Straight ahead is not a division by zero.
	const SteeringOutput straight = solve_steering(geometry, 0.0);
	CHECK(std::isfinite(turn_radius(geometry, straight.left.angle, straight.right.angle)));

	std::printf("[steering] turn radius at full lock: %.3f m to the mid-rear-axle\n", radius);
}

TEST_CASE("jacking at full lock, in millimeters") {
	// The number the wheel-lift ticket lives or dies on, so it is measured rather
	// than described. Negative is the contact patch moving down, which lifts the
	// chassis at that corner.
	const SteeringGeometry geometry = kz_front_geometry();
	const SteeringOutput output = solve_steering(geometry, 1.0);

	const double inside = output.left.contact_offset.y;
	const double outside = output.right.contact_offset.y;
	const double lift = rigid_inside_rear_lift(output, true);

	CHECK(inside < 0.0); // chassis up at the inside front
	CHECK(outside > 0.0); // chassis down at the outside front
	CHECK(lift > 0.0);

	// Millimeters, not microns and not meters. A front end producing a tenth of a
	// millimeter here could not lift anything and would mean the scrub radius or
	// the caster had been lost somewhere; a centimeter and a half is what the
	// geometry gives and it is the right order for a kart whose inside rear is
	// visibly off the ground through a slow corner.
	CHECK(lift > 0.008);
	CHECK(lift < 0.030);

	std::printf("[steering] jacking at full lock (- lifts the chassis at that corner):\n");
	std::printf("           inside front  %+7.3f mm\n", inside * 1000.0);
	std::printf("           outside front %+7.3f mm\n", outside * 1000.0);
	std::printf("           rigid-frame inside rear lift %+7.3f mm\n", lift * 1000.0);
}

TEST_CASE("jacking rises with steering angle and never reverses") {
	// Issue #35's second acceptance item. Monotone all the way to the stop, which
	// also rules out the geometry folding over at high lock — the caster term goes
	// as sin(steer) and would turn back on itself past 90 degrees.
	const SteeringGeometry geometry = kz_front_geometry();
	double previous = 0.0;
	for (int step = 1; step <= 50; ++step) {
		const double input = static_cast<double>(step) / 50.0;
		const double lift = rigid_inside_rear_lift(solve_steering(geometry, input), true);
		CHECK(lift > previous);
		previous = lift;
	}

	// Half lock gives a little over half the lift, because the dominant term is
	// sin(steer) and 12.5 degrees is a small enough angle that the sine has
	// barely started to bend. Stated as a range because it is not linear and
	// pretending otherwise would be the sort of claim this suite exists to stop.
	const double half = rigid_inside_rear_lift(solve_steering(geometry, 0.5), true);
	CHECK(half > previous * 0.45);
	CHECK(half < previous * 0.60);

	std::printf("[steering] inside rear lift: %.3f mm at half lock, %.3f mm at full\n",
			half * 1000.0, previous * 1000.0);
}

TEST_CASE("both jacking terms scale the way the sources say they do") {
	// Two claims from the setup literature, checked against the model rather than
	// repeated: more caster jacks more, and moving the front wheels outboard —
	// which is exactly a larger scrub radius — jacks more.
	const SteeringGeometry base = kz_front_geometry();

	SteeringGeometry more_caster = base;
	more_caster.caster = 22.0 * DEGREES;
	SteeringGeometry wider = base;
	wider.scrub_radius = base.scrub_radius + 0.010;

	const double reference = rigid_inside_rear_lift(solve_steering(base, 1.0), true);
	CHECK(rigid_inside_rear_lift(solve_steering(more_caster, 1.0), true) > reference);
	CHECK(rigid_inside_rear_lift(solve_steering(wider, 1.0), true) > reference);

	// And the direction of the whole effect reverses with a negative scrub
	// radius, which is the failure mode of choosing too much inclination for the
	// spindle arm: the kart would jack the wrong corner.
	SteeringGeometry reversed = base;
	reversed.scrub_radius = -base.scrub_radius;
	CHECK(rigid_inside_rear_lift(solve_steering(reversed, 1.0), true) < 0.0);

	std::printf("[steering] 10 mm more scrub radius: %.3f mm of lift, up from %.3f mm\n",
			rigid_inside_rear_lift(solve_steering(wider, 1.0), true) * 1000.0,
			reference * 1000.0);
}

TEST_CASE("the jacking lever is the spindle arm, not the scrub radius") {
	// This test was written to confirm something and disproved it instead, which
	// is the reason it is worth keeping. The claim it started from was the one the
	// setup literature makes: more kingpin inclination, more jacking. The
	// measurement says the diagonal lift barely moves at all, once inclination is
	// changed the way a kingpin insert changes it — that is, with the spindle arm
	// fixed, so the scrub radius falls as the inclination rises.
	//
	// The algebra behind the measurement, which is worth having written down: the
	// antisymmetric part of the jacking has coefficient
	//
	//     tan(caster) * (scrub_radius + wheel_radius * tan(inclination))
	//
	// and the bracket is exactly the spindle arm, because the scrub radius *is*
	// the spindle arm less `wheel_radius * tan(inclination)`. The inclination
	// cancels. What actually levers the chassis is the perpendicular distance from
	// the kingpin axis to the wheel, and the scrub radius is only that distance
	// as it appears where the axis reaches the ground.
	//
	// So: inclination sets steering weight, self-centering and camber gain, and
	// caster and the spindle arm set the wheel lift. Reported rather than
	// asserted-and-forgotten, because a setup screen built on the folk version
	// would give the player a slider that does nothing.
	const SteeringGeometry base = kz_front_geometry();
	const double reference = rigid_inside_rear_lift(solve_steering(base, 1.0), true);

	double smallest = reference;
	double largest = reference;
	for (int degrees = 0; degrees <= 16; degrees += 4) {
		SteeringGeometry swept = base;
		swept.kingpin_inclination = static_cast<double>(degrees) * DEGREES;
		swept.scrub_radius = scrub_radius_from_spindle(
				FRONT_SPINDLE_OFFSET, FRONT_WHEEL_RADIUS, swept.kingpin_inclination);
		const double lift = rigid_inside_rear_lift(solve_steering(swept, 1.0), true);
		if (lift < smallest) {
			smallest = lift;
		}
		if (lift > largest) {
			largest = lift;
		}
		std::printf("[steering] inclination %2d deg, spindle arm held: scrub %.1f mm, lift %.3f mm\n",
				degrees, swept.scrub_radius * 1000.0, lift * 1000.0);
	}
	// Four percent across the whole plausible range of the angle. Against the
	// caster sweep below, which moves it by a third.
	CHECK(largest - smallest < reference * 0.05);

	// Caster, by contrast, is the lever that matters, and so is the spindle arm.
	SteeringGeometry less_caster = base;
	less_caster.caster = 12.0 * DEGREES;
	SteeringGeometry shorter_arm = base;
	shorter_arm.scrub_radius = base.scrub_radius - 0.020;
	const double caster_effect = rigid_inside_rear_lift(solve_steering(less_caster, 1.0), true);
	const double arm_effect = rigid_inside_rear_lift(solve_steering(shorter_arm, 1.0), true);
	CHECK(caster_effect < reference * 0.75);
	CHECK(arm_effect < reference * 0.85);

	std::printf("[steering] caster 18 -> 12 deg: %.3f -> %.3f mm; spindle arm 20 mm shorter: %.3f mm\n",
			reference * 1000.0, caster_effect * 1000.0, arm_effect * 1000.0);
}

TEST_CASE("the scrub radius is what the spindle and the inclination leave") {
	// Not an independent number: the kingpin axis walks outboard as it descends
	// to the ground, and what is left of the spindle arm is the scrub. More
	// inclination therefore means less scrub and less jacking, which is exactly
	// the trade the setup guides describe when they say inclination counteracts
	// caster.
	CHECK(scrub_radius_from_spindle(0.090, 0.140, 0.0) == doctest::Approx(0.090));
	CHECK(scrub_radius_from_spindle(0.090, 0.140, 11.0 * DEGREES) ==
			doctest::Approx(0.0628).epsilon(0.01));
	CHECK(scrub_radius_from_spindle(0.090, 0.140, 11.0 * DEGREES) <
			scrub_radius_from_spindle(0.090, 0.140, 5.0 * DEGREES));
	// Enough inclination takes it negative, and the model must let it, because
	// that is a real thing a badly set up front end does.
	CHECK(scrub_radius_from_spindle(0.090, 0.140, 40.0 * DEGREES) < 0.0);

	CHECK(kz_front_geometry().scrub_radius == doctest::Approx(0.0628).epsilon(0.01));
}

TEST_CASE("there is no snap anywhere across the input range") {
	// Issue #35's third acceptance item, measured. Sweep the input past both
	// stops and look at the output and its first difference. A jump would show as
	// a first difference far larger than its neighbors; a kink in the interior
	// would show in the second difference.
	const SteeringGeometry geometry = kz_front_geometry();
	const int samples = 4001;
	const double span = 3.0; // -1.5 to 1.5, so both stops are inside the sweep
	const double step = span / static_cast<double>(samples - 1);

	double previous_left = 0.0;
	double previous_jack = 0.0;
	double previous_angle_difference = 0.0;
	double previous_jack_difference = 0.0;
	double largest_angle_step = 0.0;
	double largest_jack_step = 0.0;
	double largest_interior_curvature = 0.0;

	for (int index = 0; index < samples; ++index) {
		const double input = -1.5 + step * static_cast<double>(index);
		const SteeringOutput output = solve_steering(geometry, input);
		const double left = output.left.angle;
		const double jack = output.left.contact_offset.y;

		if (index > 0) {
			const double angle_difference = left - previous_left;
			const double jack_difference = jack - previous_jack;
			if (std::fabs(angle_difference) > largest_angle_step) {
				largest_angle_step = std::fabs(angle_difference);
			}
			if (std::fabs(jack_difference) > largest_jack_step) {
				largest_jack_step = std::fabs(jack_difference);
			}
			// The steering stops are a genuine break in slope — that is what a
			// stop is — so curvature is measured strictly inside them.
			if (index > 1 && std::fabs(input) < 0.99) {
				const double curvature = std::fabs(angle_difference - previous_angle_difference);
				if (curvature > largest_interior_curvature) {
					largest_interior_curvature = curvature;
				}
				const double jack_curvature = std::fabs(jack_difference - previous_jack_difference);
				if (jack_curvature > largest_interior_curvature) {
					largest_interior_curvature = jack_curvature;
				}
			}
			previous_angle_difference = angle_difference;
			previous_jack_difference = jack_difference;
		}
		previous_left = left;
		previous_jack = jack;
	}

	// No step anywhere exceeds what one sample of travel can account for. The
	// wheel cannot move faster than the input does, so `max_lock * step` bounds
	// it, and a jump of any size would blow straight through this.
	CHECK(largest_angle_step < geometry.max_lock * step * 1.001);
	CHECK(largest_jack_step < 1e-4);

	// And in the interior the slope changes by a thousandth of the slope itself
	// per sample, which is curvature, not a corner. The place this could have
	// gone wrong is dead center, where the inner and the outer wheel swap roles.
	CHECK(largest_interior_curvature < largest_angle_step * 0.01);

	// Past the stop the output is frozen, not folded back: an input of 5 and an
	// input of 1 are the same steering.
	const SteeringOutput at_stop = solve_steering(geometry, 1.0);
	const SteeringOutput far_past = solve_steering(geometry, 5.0);
	CHECK(far_past.left.angle == doctest::Approx(at_stop.left.angle));
	CHECK(far_past.left.contact_offset.y == doctest::Approx(at_stop.left.contact_offset.y));

	std::printf("[steering] sweep of %d samples, -1.5 to 1.5 input:\n", samples);
	std::printf("           largest angle step %.3e rad, largest jacking step %.3e m\n",
			largest_angle_step, largest_jack_step);
	std::printf("           largest interior curvature %.3e, %.4f%% of the step\n",
			largest_interior_curvature, 100.0 * largest_interior_curvature / largest_angle_step);
}

TEST_CASE("steering through center is smooth and lands on zero") {
	const SteeringGeometry geometry = kz_front_geometry();
	const SteeringOutput centered = solve_steering(geometry, 0.0);
	CHECK(centered.left.angle == 0.0);
	CHECK(centered.right.angle == 0.0);
	CHECK(centered.left.contact_offset.y == doctest::Approx(0.0));
	CHECK(centered.right.contact_offset.y == doctest::Approx(0.0));

	// The slope of each wheel's angle against input is the same on both sides of
	// center. This is the swap between inner and outer, and if the Ackermann
	// correction were linear in the angle rather than vanishing quadratically,
	// there would be a visible kink here on a wheel that never left center.
	const double tiny = 1e-4;
	const double rising = (solve_steering(geometry, tiny).left.angle -
								  solve_steering(geometry, 0.0).left.angle) /
			tiny;
	const double falling = (solve_steering(geometry, 0.0).left.angle -
								   solve_steering(geometry, -tiny).left.angle) /
			tiny;
	CHECK(rising == doctest::Approx(falling).epsilon(1e-3));
	CHECK(rising == doctest::Approx(geometry.max_lock).epsilon(1e-3));
}

TEST_CASE("steering-induced camber leans the outside wheel into the corner") {
	// Not part of the contract and not consumed yet, but it comes free with the
	// rotation and it is a real thing: under lock the outside front gains
	// negative camber, which is a large part of why a kart bites on turn-in.
	const SteeringGeometry geometry = kz_front_geometry();
	const SteeringOutput output = solve_steering(geometry, 1.0);

	const double inner_rotation =
			kingpin_rotation_for_wheel_angle(geometry, Side::left, output.left.angle);
	const double outer_rotation =
			kingpin_rotation_for_wheel_angle(geometry, Side::right, output.right.angle);
	const double inner = steer_induced_camber(geometry, Side::left, inner_rotation);
	const double outer = steer_induced_camber(geometry, Side::right, outer_rotation);

	// Positive is the top leaning outboard. The outside wheel goes the other way.
	CHECK(outer < 0.0);
	CHECK(inner > 0.0);
	// Zero steer is zero camber change — static camber is not modeled here.
	CHECK(steer_induced_camber(geometry, Side::left, 0.0) == doctest::Approx(0.0));

	std::printf("[steering] camber at full lock: inside %+.2f deg, outside %+.2f deg\n",
			to_degrees(inner), to_degrees(outer));
}

TEST_CASE("the rate limit moves at its rate and then stops") {
	// A pure function of its arguments and a `dt` handed in from outside. Nothing
	// here reads a clock; ARCHITECTURE.md §8.
	const double dt = 1.0 / 120.0;
	// Input units per second, carried over from the M3a debug vehicle's
	// `STEER_RATE`. That constant is documented as radians per second and is
	// applied to a normalized input, so this is the number it actually is.
	const double rate = 3.4;

	double input = 0.0;
	for (int tick = 0; tick < 4; ++tick) {
		input = rate_limited_steering(input, 1.0, rate, dt);
	}
	CHECK(input == doctest::Approx(4.0 * rate * dt));

	// It arrives exactly rather than oscillating around the target, which matters
	// because a value that never quite settles is a value the determinism hash
	// can never agree on.
	for (int tick = 0; tick < 200; ++tick) {
		input = rate_limited_steering(input, 1.0, rate, dt);
	}
	CHECK(input == 1.0);

	// Symmetric, and it does not overshoot a target it is already past.
	CHECK(rate_limited_steering(0.5, -1.0, rate, dt) < 0.5);
	CHECK(rate_limited_steering(0.5, 0.5, rate, dt) == 0.5);
	CHECK(rate_limited_steering(0.0, 0.001, rate, dt) == doctest::Approx(0.001));

	// Time to full lock from center, which is the number a driver feels.
	std::printf("[steering] rate limit %.1f /s reaches full lock in %.0f ms\n", rate,
			1000.0 / rate);
}

TEST_CASE("a degenerate geometry does not produce a NaN") {
	// A default-constructed `SteeringGeometry` is all zeros, including the
	// wheelbase the Ackermann relation divides by. Anything that reaches the
	// solver before it has been configured — a scene half loaded, a test half
	// written — must get a boring answer rather than a NaN that then lives in the
	// chassis state forever.
	const SteeringGeometry empty;
	const SteeringOutput output = solve_steering(empty, 1.0);
	CHECK(std::isfinite(output.left.angle));
	CHECK(std::isfinite(output.right.angle));
	CHECK(output.left.contact_offset.is_finite());
	CHECK(output.right.contact_offset.is_finite());
	CHECK(output.left.angle == doctest::Approx(0.0));
	CHECK(output.left.contact_offset.y == doctest::Approx(0.0));
}
