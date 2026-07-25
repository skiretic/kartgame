#include "doctest.h"

#include "core/engine.h"
#include "core/kz_reference.h"
#include "core/units.h"

#include <cmath>

// The engine curve is not fitted to a dyno trace — nobody publishes one with
// numbers on the axes — so what has to be asserted is every published figure the
// curve claims to reproduce: peak power where kz_reference.h says, a powerband
// no wider than kz_reference.h's, an engine that is genuinely dead below it, and
// a rev limiter that removes drive without pretending it can stop a crankshaft
// the road is turning.
//
// These tests print their measurements. They are the acceptance evidence for
// issues #36 and #39, and a number that only exists inside a CHECK is a number
// nobody can quote.

using kart::core::Engine;
using kart::core::WATTS_PER_HP;
using kart::core::rpm_to_rads;

TEST_CASE("peak power lands where the KZ reference says it does") {
	const Engine engine;
	const double peak_rpm = engine.peak_power_rpm();
	const double peak_hp = engine.power(peak_rpm) / WATTS_PER_HP;

	MESSAGE("peak power " << peak_hp << " hp at " << peak_rpm << " rpm");

	// Power is computed from this file's own torque curve as P = T * omega and
	// never tabulated, so this is a statement about the curve rather than about a
	// constant somebody typed twice.
	CHECK(std::fabs(peak_rpm - kart::core::kz::PEAK_POWER_RPM) < 400.0);
	CHECK(peak_hp > kart::core::kz::PEAK_POWER_HP * 0.95);
	CHECK(peak_hp < kart::core::kz::PEAK_POWER_HP * 1.05);
}

TEST_CASE("the powerband is as narrow as a KZ's") {
	const Engine engine;
	const double peak_torque_rpm = engine.peak_torque_rpm();
	const double peak_torque = engine.peak_torque();

	double low = 0.0;
	double high = 0.0;
	REQUIRE(engine.powerband(0.8, low, high));
	MESSAGE("peak torque " << peak_torque << " Nm at " << peak_torque_rpm << " rpm");
	MESSAGE(">=80% of peak torque from " << low << " to " << high
										 << " rpm, width " << (high - low));

	// The whole band has to sit inside kz_reference.h's usable range. The upper
	// end is the one that catches a curve drawn too optimistically: an engine
	// still making 80% of peak torque at 15,000 rpm is not a KZ, it is a
	// four-stroke with a kart bolted to it.
	CHECK(low >= kart::core::kz::POWERBAND_MIN_RPM);
	CHECK(high <= kart::core::kz::POWERBAND_MAX_RPM);

	double narrow_low = 0.0;
	double narrow_high = 0.0;
	REQUIRE(engine.powerband(0.9, narrow_low, narrow_high));
	MESSAGE(">=90% of peak torque from " << narrow_low << " to " << narrow_high
										 << " rpm, width " << (narrow_high - narrow_low));
	// An engine builder quoted on KartPulse puts a KZ's usable band at "say 2,500
	// rpm" against 9,000-12,000 for a single-speed kart. Within a few hundred rpm
	// of that is the target; much wider and the gearbox stops mattering, which is
	// the same as saying the kart stops being a KZ.
	CHECK((narrow_high - narrow_low) < 3000.0);
	CHECK((narrow_high - narrow_low) > 2000.0);
}

TEST_CASE("below the powerband the engine is dead") {
	const Engine engine;
	const double peak = engine.peak_torque();

	for (double rpm = 6000.0; rpm <= 9000.0; rpm += 1000.0) {
		MESSAGE(rpm << " rpm: " << engine.wide_open_torque(rpm) << " Nm, "
					<< (100.0 * engine.wide_open_torque(rpm) / peak) << "% of peak");
	}

	// "Dead" quantified: under half of peak torque at 8,000 rpm, and under 65% at
	// 9,000 where the pipe is only starting to work. Issue #36 asks for a kart
	// that feels dead below 9,000 and this is the number that says whether it
	// does — a driver who lets a KZ drop to 8,000 has half the engine left.
	CHECK(engine.wide_open_torque(8000.0) < peak * 0.5);
	CHECK(engine.wide_open_torque(9000.0) < peak * 0.65);
	// But not nothing. It has to be able to pull away from the pit lane.
	CHECK(engine.wide_open_torque(6000.0) > peak * 0.25);
}

TEST_CASE("throttle scales between engine braking and the full curve") {
	const Engine engine;
	const double rpm = 11000.0;

	const double closed = engine.torque(rpm, 0.0);
	const double half = engine.torque(rpm, 0.5);
	const double open = engine.torque(rpm, 1.0);

	MESSAGE("at " << rpm << " rpm: closed " << closed << " Nm, half " << half
				  << " Nm, open " << open << " Nm");

	// Full throttle must return the published curve exactly — the blend adds the
	// drag back before scaling and takes it off afterwards precisely so that a
	// dyno figure survives the trip.
	CHECK(open == doctest::Approx(engine.wide_open_torque(rpm)));
	CHECK(closed == doctest::Approx(-engine.drag_torque(rpm)));
	CHECK(half > closed);
	CHECK(half < open);
	// And somewhere in between the engine is doing exactly enough to turn itself.
	// That crossing is what a driver feels as the on-off point.
	bool crosses = false;
	for (double throttle = 0.0; throttle <= 1.0; throttle += 0.01) {
		if (engine.torque(rpm, throttle) > 0.0) {
			crosses = true;
			break;
		}
	}
	CHECK(crosses);
}

TEST_CASE("engine braking rises with rpm and is a third of peak torque") {
	const Engine engine;
	MESSAGE("closed-throttle drag: 6,000 rpm " << engine.drag_torque(6000.0)
											   << " Nm, 12,000 rpm " << engine.drag_torque(12000.0) << " Nm");

	CHECK(engine.drag_torque(12000.0) > engine.drag_torque(6000.0));
	// Two-stroke, closed 30 mm slide. In absolute terms it is a small number —
	// which is the point, because ARCHITECTURE.md §6.3's "engine braking shapes
	// corner entry more than the brakes" is produced by the gearing multiplying
	// it, not by the engine being unusually draggy. See test_drivetrain.cpp for
	// the deceleration this actually produces.
	CHECK(engine.drag_torque(12000.0) > engine.peak_torque() * 0.2);
	CHECK(engine.drag_torque(12000.0) < engine.peak_torque() * 0.4);
}

TEST_CASE("the rev limiter is a soft cut and it removes drive only") {
	const Engine engine;

	const double before = engine.torque(engine.soft_cut_rpm - 100.0, 1.0);
	const double middle = engine.torque(
			(engine.soft_cut_rpm + engine.hard_cut_rpm) * 0.5, 1.0);
	const double after = engine.torque(engine.hard_cut_rpm + 200.0, 1.0);

	MESSAGE("soft cut " << engine.soft_cut_rpm << " rpm, hard cut "
						<< engine.hard_cut_rpm << " rpm; torque before " << before
						<< " Nm, mid-taper " << middle << " Nm, past the cut " << after << " Nm");

	CHECK(before > 0.0);
	CHECK(middle > 0.0);
	CHECK(middle < before);
	// Past the hard cut, full throttle produces exactly the closed-throttle drag:
	// the ignition has stopped firing and the engine is being turned, not turning
	// anything. That is what bouncing off a limiter feels like, and it is also
	// why the limiter cannot save an over-rev.
	CHECK(after == doctest::Approx(-engine.drag_torque(engine.hard_cut_rpm + 200.0)));
}

TEST_CASE("an over-rev costs the engine something") {
	Engine engine;
	const double healthy = engine.peak_torque();

	CHECK(engine.over_rev(engine.hard_cut_rpm + 1.0));
	CHECK_FALSE(engine.over_rev(engine.hard_cut_rpm - 1.0));

	// 2,000 rpm past the cut for a fifth of a second — a badly judged downshift,
	// caught quickly.
	const double dt = 1.0 / 240.0;
	for (int substep = 0; substep < 48; ++substep) {
		engine.accumulate_over_rev(engine.hard_cut_rpm + 2000.0, dt);
	}
	MESSAGE("0.2 s at 2,000 rpm over the cut: damage " << engine.damage()
													  << ", peak torque now " << engine.peak_torque() << " Nm (was " << healthy << ")");

	CHECK(engine.damage() > 0.0);
	CHECK(engine.peak_torque() < healthy);
	// A silent clamp is what issue #36 asks not to have. This is the opposite:
	// the engine is measurably worse afterwards and stays that way until
	// something repairs it.
	engine.repair();
	CHECK(engine.peak_torque() == doctest::Approx(healthy));
}

TEST_CASE("the curve is finite and well behaved everywhere") {
	const Engine engine;
	for (double rpm = 0.0; rpm <= 20000.0; rpm += 25.0) {
		for (double throttle = 0.0; throttle <= 1.0; throttle += 0.25) {
			const double value = engine.torque(rpm, throttle);
			CHECK(std::isfinite(value));
			// No engine anywhere makes more than the peak of its own curve plus
			// the idle governor, and nothing may make more braking torque than
			// the drag model says.
			CHECK(value <= engine.peak_torque() + engine.idle_torque_nm + 1e-9);
			CHECK(value >= -engine.drag_torque(rpm) - 1e-9);
		}
	}
	// Past the last table point the curve is held at zero rather than
	// extrapolated. An extrapolated straight line would go negative and read as
	// engine braking, which would make a catastrophic over-rev feel like a
	// downshift.
	CHECK(engine.wide_open_torque(17000.0) == doctest::Approx(0.0));
}
