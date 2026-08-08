#include "doctest.h"

#include "core/tire.h"
#include "core/kz_reference.h"
#include "core/units.h"

// What is worth asserting about a tire model is its *shape*, not its numbers:
// nobody has rig data for a kart slick, so the coefficients are chosen and the
// curve's properties are the thing that must not drift. Peak in the right place,
// grip falling with load, a friction ellipse that cannot be cheated, and a wheel
// off the ground producing nothing.

using kart::core::Tire;
using kart::core::TireCurve;
using kart::core::TireForce;
using kart::core::TireSlip;

static constexpr double NOMINAL_LOAD = 500.0;

TEST_CASE("the lateral curve peaks where a kart slick peaks") {
	TireCurve curve;
	const double peak = curve.peak_slip();
	// Around 8 degrees. A car tire peaks nearer 6 and a truck tire nearer 4; a
	// kart slick is grippy and reaches its peak fast, and if this ever drifts
	// past 12 degrees the kart will feel like it is driving on a mattress.
	CHECK(peak > kart::core::PI / 180.0 * 5.0);
	CHECK(peak < kart::core::PI / 180.0 * 12.0);
}

TEST_CASE("the longitudinal curve peaks earlier than the lateral one") {
	Tire tire;
	// True of real tires, and it is why locking a wheel costs so much more than
	// sliding one: peak braking is at a slip ratio around 0.1, and by the time
	// the wheel is fully locked most of it is gone.
	CHECK(tire.longitudinal.peak_slip() < tire.lateral.peak_slip());
	CHECK(tire.longitudinal.peak_slip() < 0.25);
}

TEST_CASE("force is zero at zero slip and odd in slip") {
	TireCurve curve;
	CHECK(curve.force(0.0, NOMINAL_LOAD) == doctest::Approx(0.0));
	// Odd symmetry: steering left and right cannot be different tires.
	CHECK(curve.force(0.1, NOMINAL_LOAD) == doctest::Approx(-curve.force(-0.1, NOMINAL_LOAD)));
}

TEST_CASE("force falls away past the peak rather than saturating") {
	TireCurve curve;
	const double peak_slip = curve.peak_slip();
	const double at_peak = curve.force(peak_slip, NOMINAL_LOAD);
	const double past_peak = curve.force(peak_slip * 4.0, NOMINAL_LOAD);
	CHECK(past_peak < at_peak);
	// But not to nothing. A tire at 30 degrees of slip is still doing real work,
	// and a model that drops to zero there makes a spin unrecoverable in a way
	// a real kart is not.
	CHECK(past_peak > at_peak * 0.5);
}

TEST_CASE("grip falls as vertical load rises") {
	TireCurve curve;
	// The property that makes weight transfer cost grip rather than merely move
	// it. Without it the inside-rear lift in ARCHITECTURE.md §6 would produce no
	// understeer at all and the kart would corner as if it were on rails.
	CHECK(curve.friction_at(NOMINAL_LOAD * 2.0) < curve.friction_at(NOMINAL_LOAD));
	CHECK(curve.friction_at(NOMINAL_LOAD * 0.5) > curve.friction_at(NOMINAL_LOAD));
	CHECK(curve.friction_at(NOMINAL_LOAD) == doctest::Approx(curve.peak_friction));

	// Doubling the load must still *increase* total force, even though the
	// coefficient fell. A load sensitivity steep enough to invert that would mean
	// pressing a tire harder into the road made it grip less in absolute terms,
	// which is not a tire, it is a bug.
	CHECK(curve.force(curve.peak_slip(), NOMINAL_LOAD * 2.0) >
			curve.force(curve.peak_slip(), NOMINAL_LOAD));
}

TEST_CASE("an overloaded tire degrades rather than inverting") {
	TireCurve curve;
	// 50x nominal is not physical; the point is that no load produces negative
	// friction, because negative friction is a tire that pushes the kart
	// sideways and it would appear first in a big landing.
	CHECK(curve.friction_at(NOMINAL_LOAD * 50.0) > 0.0);
	CHECK(curve.force(0.15, NOMINAL_LOAD * 50.0) > 0.0);
}

TEST_CASE("a wheel off the ground produces nothing") {
	Tire tire;
	TireSlip slip;
	slip.normal_load = 0.0;
	slip.slip_angle = 0.2;
	slip.slip_ratio = 0.5;
	const TireForce force = tire.evaluate(slip);
	CHECK(force.lateral == doctest::Approx(0.0));
	CHECK(force.longitudinal == doctest::Approx(0.0));
}

TEST_CASE("pure slip is unaffected by the friction ellipse") {
	Tire tire;
	TireSlip slip;
	slip.normal_load = NOMINAL_LOAD;
	slip.slip_angle = tire.lateral.peak_slip();

	const TireForce combined = tire.evaluate(slip);
	// With no longitudinal slip the ellipse has nothing to trade against, so the
	// lateral force must be exactly what the lateral curve alone says.
	CHECK(combined.lateral ==
			doctest::Approx(tire.lateral.force(slip.slip_angle, NOMINAL_LOAD)));
	CHECK(combined.longitudinal == doctest::Approx(0.0));
}

TEST_CASE("combined slip costs lateral grip") {
	Tire tire;
	TireSlip cornering;
	cornering.normal_load = NOMINAL_LOAD;
	cornering.slip_angle = tire.lateral.peak_slip();

	TireSlip cornering_and_braking = cornering;
	cornering_and_braking.slip_ratio = -tire.longitudinal.peak_slip();

	const double pure = tire.evaluate(cornering).lateral;
	const double mixed = tire.evaluate(cornering_and_braking).lateral;

	// The whole reason to combine through an ellipse: a tire asked for all of its
	// grip in two directions at once cannot deliver both. This is trail braking,
	// and it is where a lap time is found or lost.
	CHECK(mixed < pure);
	CHECK(mixed > 0.0);
}

TEST_CASE("the friction ellipse cannot be exceeded") {
	Tire tire;
	TireSlip slip;
	slip.normal_load = NOMINAL_LOAD;
	slip.slip_angle = tire.lateral.peak_slip();
	slip.slip_ratio = tire.longitudinal.peak_slip();

	const TireForce force = tire.evaluate(slip);
	const double magnitude =
			std::sqrt(force.lateral * force.lateral + force.longitudinal * force.longitudinal);
	const double capacity = tire.lateral.friction_at(NOMINAL_LOAD) * NOMINAL_LOAD;

	// This comment used to say the bound "is not a perfect circle" because "the
	// longitudinal curve has its own peak friction". It does not. `Tire::Tire()`
	// overrides only `stiffness`, `shape` and `curvature`; `peak_friction`,
	// `nominal_load` and `load_sensitivity` are left at their defaults on both
	// axes, so the two capacities are identical at every load and the ellipse is
	// exactly a circle. The assertion below passed, but for a reason that was not
	// true, which is worse than a failing test.
	//
	// So it is asserted as what it is: a circle, to within float noise rather
	// than to within a 5% slop that was covering for an eccentricity that is not
	// there. If the longitudinal axis is ever given its own peak friction — real
	// tires have one, usually a little above lateral — this assertion is where
	// that change announces itself.
	CHECK(tire.longitudinal.friction_at(NOMINAL_LOAD) ==
			doctest::Approx(tire.lateral.friction_at(NOMINAL_LOAD)));
	CHECK(magnitude == doctest::Approx(capacity).epsilon(1e-9));
	CHECK(force.utilization > 1.0);
}

TEST_CASE("surface grip scales both axes") {
	Tire tire;
	TireSlip asphalt;
	asphalt.normal_load = NOMINAL_LOAD;
	asphalt.slip_angle = 0.1;
	asphalt.slip_ratio = 0.05;

	TireSlip grass = asphalt;
	grass.surface_grip = 0.4;

	const TireForce on_asphalt = tire.evaluate(asphalt);
	const TireForce on_grass = tire.evaluate(grass);

	CHECK(on_grass.lateral == doctest::Approx(on_asphalt.lateral * 0.4).epsilon(1e-6));
	CHECK(on_grass.longitudinal == doctest::Approx(on_asphalt.longitudinal * 0.4).epsilon(1e-6));
}

TEST_CASE("the model can produce the cornering force the KZ figures require") {
	// The whole envelope in one assertion, and it caught a real error the first
	// time it ran: with no downforce the achievable lateral g is exactly the
	// friction coefficient, because four tires at `mu * N` sum to `mu * m * g`.
	// A peak friction of 1.75 therefore could not reach §6.4's 2.0-2.5 g band no
	// matter what the chassis did, and this is what said so — before any of it
	// was wired to a kart.
	Tire tire;
	const double per_tire_load = kart::core::kz::STATIC_LOAD_PER_TIRE_N;
	TireSlip slip;
	slip.normal_load = per_tire_load;
	slip.slip_angle = tire.lateral.peak_slip();

	const double per_tire_force = tire.evaluate(slip).lateral;
	const double total = per_tire_force * 4.0;
	const double achievable_g = total / (175.0 * kart::core::G);

	CHECK(achievable_g > 2.0);
	CHECK(achievable_g < 3.2);
}

TEST_CASE("slide rises with slip where utilization falls") {
	// The defect this pair of fields was split to fix. `utilization` peaks at 1.0
	// and comes back down past the peak, so a locked wheel looked like a gripping
	// one to both of the consumers tire.h names — the telemetry panel and §12's
	// scrub audio. `slide` is monotone, which is what those two actually want.
	Tire tire;
	CHECK(tire.longitudinal_peak_slip == doctest::Approx(tire.longitudinal.peak_slip()));
	CHECK(tire.lateral_peak_slip == doctest::Approx(tire.lateral.peak_slip()));

	double previous_slide = -1.0;
	double utilization_at_peak = 0.0;
	double utilization_when_locked = 0.0;

	for (double ratio = 0.05; ratio <= 1.0001; ratio += 0.05) {
		TireSlip slip;
		slip.normal_load = NOMINAL_LOAD;
		slip.slip_ratio = ratio;
		const TireForce force = tire.evaluate(slip);

		// Monotone, strictly. This is the whole property.
		CHECK(force.slide > previous_slide);
		previous_slide = force.slide;

		if (ratio > tire.longitudinal_peak_slip && utilization_at_peak == 0.0) {
			utilization_at_peak = force.utilization;
		}
		utilization_when_locked = force.utilization;
	}

	// At the peak, slide is 1.0 by construction.
	TireSlip at_peak;
	at_peak.normal_load = NOMINAL_LOAD;
	at_peak.slip_ratio = tire.longitudinal_peak_slip;
	CHECK(tire.evaluate(at_peak).slide == doctest::Approx(1.0));

	// A fully locked wheel is far past it.
	TireSlip locked;
	locked.normal_load = NOMINAL_LOAD;
	locked.slip_ratio = -1.0;
	const TireForce sliding = tire.evaluate(locked);
	CHECK(sliding.slide > 9.0);
	// ...and this is the number that was being reported as "how much grip is in
	// use" for a wheel that has completely let go.
	CHECK(sliding.utilization == doctest::Approx(0.612).epsilon(1e-2));
	CHECK(utilization_when_locked < utilization_at_peak);
}

// **Issue #243.** A tire held at its longitudinal peak slip ratio has no lateral
// reserve, and the arithmetic that makes that true is one line of `tire.h`.
//
// `normalized(peak_slip)` is 1.0 — that is the *definition* of the peak, found by
// the bisection in `peak_slip()`. So `longitudinal_fraction` in `evaluate` is
// exactly 1 there, `demand` is `sqrt(1 + lateral_fraction^2)`, and the lateral
// force is divided by it for any slip angle at all. There is no slip angle small
// enough to escape it.
//
// This is not a defect in `tire.h` — a tire really does have nothing left when it
// is at its longitudinal limit, and the ellipse is doing its job. It is here
// because `KartVehicle::rear_traction_torque` *converges the rear axle onto that
// exact slip ratio* whenever the engine outruns the surface, and that is what
// #243's departure is: the rear axle deliberately parked at zero lateral reserve
// with the throttle open. Measured on a plane at 130 km/h, grip 0.18: throttle 0.4
// holds slip ratio 0.029 and 0.6 degrees of body slip; throttle 0.6 reaches 0.078
// climbing past 0.139 and spins to 144.5 degrees.
//
// The scaling is asserted rather than described so that a future change to the
// combined-slip law has to move this test on its way past.
TEST_CASE("a tire at its longitudinal peak keeps no lateral reserve") {
	Tire tire;
	const double load = 500.0;
	const double peak = tire.longitudinal_peak_slip;
	CHECK(peak > 0.0);

	// The definition of the peak, restated as the thing `evaluate` relies on.
	CHECK(tire.longitudinal.normalized(peak) == doctest::Approx(1.0).epsilon(1e-6));

	// Pure lateral at a small slip angle: the ellipse does not bind, so the force
	// is exactly what the lateral curve says.
	const double angle = 0.02; // rad, well inside the linear range
	TireSlip pure;
	pure.normal_load = load;
	pure.slip_angle = angle;
	const TireForce alone = tire.evaluate(pure);
	CHECK(alone.utilization < 1.0);

	// The same slip angle with the wheel sitting on its longitudinal peak.
	TireSlip combined = pure;
	combined.slip_ratio = peak;
	const TireForce at_peak = tire.evaluate(combined);

	// `demand` is sqrt(1 + f^2) where f is the lateral fraction, so the lateral
	// force comes back divided by that and by nothing else.
	const double fraction = alone.lateral /
			(tire.lateral.friction_at(load) * load);
	const double expected = alone.lateral / std::sqrt(1.0 + fraction * fraction);
	CHECK(at_peak.lateral == doctest::Approx(expected).epsilon(1e-9));
	CHECK(at_peak.utilization >= 1.0);

	// And the cost is not academic at large slip angles, which is where a kart
	// that has started to rotate actually is. At 10 degrees the lateral force a
	// tire on its longitudinal peak can still make is a fraction of what the same
	// tire makes coasting, and that fraction is the yaw damping the kart has left.
	TireSlip loose;
	loose.normal_load = load;
	loose.slip_angle = 10.0 * 3.14159265358979323846 / 180.0;
	const double coasting = tire.evaluate(loose).lateral;
	loose.slip_ratio = peak;
	const double driven = tire.evaluate(loose).lateral;
	CHECK(driven < coasting * 0.75);
	CHECK(driven > 0.0);
}
