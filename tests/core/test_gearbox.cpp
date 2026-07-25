#include "doctest.h"

#include "core/gearbox.h"
#include "core/engine.h"
#include "core/kz_reference.h"
#include "core/units.h"

// The gearing is the single most checkable claim in the drivetrain and the one
// most likely to be quietly wrong, because every ratio is plausible on its own
// and only the product of all four means anything. So the headline test here is
// closed form: tooth counts from a parts catalog, a rear tire of known radius,
// the rev limiter, and a top speed that either lands inside kz_reference.h's
// range or does not.

using kart::core::Engine;
using kart::core::Gearbox;
using kart::core::ShiftState;

// Rear slick rolling radius, meters. 275 mm across the tread on a KZ rear, so
// 0.1375 m unloaded — the figure the M3b acceptance criteria are quoted against.
static constexpr double ROLLING_RADIUS = 0.1375;

TEST_CASE("the ratios come from tooth counts and step down properly") {
	const Gearbox gearbox;

	MESSAGE("primary " << Gearbox::primary_reduction() << ":1, final drive "
					   << gearbox.final_drive() << ":1 (" << gearbox.engine_sprocket_teeth
					   << "/" << gearbox.axle_sprocket_teeth << ")");

	CHECK(Gearbox::primary_reduction() == doctest::Approx(75.0 / 18.0));
	CHECK(Gearbox::gear_ratio(1) == doctest::Approx(33.0 / 13.0));
	CHECK(Gearbox::gear_ratio(6) == doctest::Approx(25.0 / 27.0));
	// Sixth really is an overdrive. It looks like a typo and it is not — a 4.17
	// primary has to be given back somewhere.
	CHECK(Gearbox::gear_ratio(6) < 1.0);

	for (int gear = 1; gear <= kart::core::kz::GEAR_COUNT; ++gear) {
		MESSAGE("gear " << gear << ": ratio " << Gearbox::gear_ratio(gear)
						<< ", total " << gearbox.total_ratio(gear));
	}

	for (int gear = 1; gear < kart::core::kz::GEAR_COUNT; ++gear) {
		CHECK(gearbox.total_ratio(gear) > gearbox.total_ratio(gear + 1));
		const double step = gearbox.total_ratio(gear) / gearbox.total_ratio(gear + 1);
		MESSAGE("step " << gear << "->" << (gear + 1) << ": " << step);
		// A dog box with a step above about 1.45 drops the engine out of a
		// 2,600 rpm powerband on every shift, and one below 1.1 is a gear nobody
		// would fit. First to second is always the tall one.
		CHECK(step > 1.10);
		CHECK(step < 1.45);
	}

	CHECK(gearbox.total_ratio(0) == doctest::Approx(0.0));
}

TEST_CASE("sixth gear tops out inside the KZ reference range") {
	const Gearbox gearbox;
	const Engine engine;

	const double top_ms = gearbox.road_speed(engine.hard_cut_rpm, 6, ROLLING_RADIUS);
	const double top_kmh = kart::core::ms_to_kmh(top_ms);

	MESSAGE("sixth at the " << engine.hard_cut_rpm << " rpm cut: " << top_kmh
							<< " km/h (" << top_ms << " m/s)");
	MESSAGE("first at the same cut: "
			<< kart::core::ms_to_kmh(gearbox.road_speed(engine.hard_cut_rpm, 1, ROLLING_RADIUS))
			<< " km/h");
	MESSAGE("sixth at peak power (" << kart::core::kz::PEAK_POWER_RPM << " rpm): "
									<< kart::core::ms_to_kmh(gearbox.road_speed(
											   kart::core::kz::PEAK_POWER_RPM, 6, ROLLING_RADIUS))
									<< " km/h");

	// ARCHITECTURE.md §6.4 and kz_reference.h: 135-145 km/h, gearing dependent.
	// This is four numbers multiplied together — 75/18, 25/27, 25/18 and a
	// 0.1375 m radius — and there is nowhere to hide if one of them is wrong.
	CHECK(top_kmh > kart::core::kz::TOP_SPEED_MIN_KMH);
	CHECK(top_kmh < kart::core::kz::TOP_SPEED_MAX_KMH);

	// First gear has to be short enough to launch and long enough not to be
	// useless. 50-55 km/h at the limiter is where a shifter kart's first gear
	// lands.
	const double first_kmh =
			kart::core::ms_to_kmh(gearbox.road_speed(engine.hard_cut_rpm, 1, ROLLING_RADIUS));
	CHECK(first_kmh > 45.0);
	CHECK(first_kmh < 60.0);
}

TEST_CASE("the ratio chain agrees with a karter's own arithmetic") {
	// The best check available on a gearing model is somebody else's number for a
	// setup nobody here chose. A KartPulse thread has a driver running a TM KZ10
	// on 20-tooth sprockets front and rear — a 1:1 final drive, road-race gearing
	// — and another poster telling him what that means: "if you're running 20T
	// front and back, you can't be anything like 14,000 in G6. You should be
	// close to 160 km/h in G5 at that RPM."
	//
	// Nothing in this file was fitted to that. The primary reduction is a pair of
	// part numbers, the gear ratios are tooth counts from a dealer's catalog, and
	// the rolling radius is a tire size. If the chain is right the answer falls
	// out; if any one link is wrong it does not.
	Gearbox road_race;
	road_race.engine_sprocket_teeth = 20;
	road_race.axle_sprocket_teeth = 20;

	const double fifth = kart::core::ms_to_kmh(road_race.road_speed(14000.0, 5, ROLLING_RADIUS));
	const double sixth = kart::core::ms_to_kmh(road_race.road_speed(14000.0, 6, ROLLING_RADIUS));
	const double sixth_at_130 = road_race.engine_rpm_at(kart::core::kmh_to_ms(130.0), 6, ROLLING_RADIUS);

	MESSAGE("20/20 sprockets at 14,000 rpm: " << fifth << " km/h in fifth, " << sixth
											  << " km/h in sixth; his observed 130 km/h is "
											  << sixth_at_130 << " rpm in sixth");

	CHECK(fifth > 155.0);
	CHECK(fifth < 175.0);
	// And the other half of what he was told: 130 km/h on that gearing is nowhere
	// near the limiter in sixth, which is why the engine was not being asked for
	// what its owner thought it was.
	CHECK(sixth_at_130 < 11000.0);
}

TEST_CASE("road speed and engine speed are inverses of each other") {
	const Gearbox gearbox;
	for (int gear = 1; gear <= kart::core::kz::GEAR_COUNT; ++gear) {
		const double speed = gearbox.road_speed(12000.0, gear, ROLLING_RADIUS);
		CHECK(gearbox.engine_rpm_at(speed, gear, ROLLING_RADIUS) == doctest::Approx(12000.0));
	}
}

TEST_CASE("a sprocket change moves the top speed the way a mechanic expects") {
	Gearbox gearbox;
	const Engine engine;
	const double baseline = gearbox.road_speed(engine.hard_cut_rpm, 6, ROLLING_RADIUS);

	gearbox.axle_sprocket_teeth += 2; // more teeth on the axle is a shorter gear
	const double shorter = gearbox.road_speed(engine.hard_cut_rpm, 6, ROLLING_RADIUS);
	CHECK(shorter < baseline);

	gearbox.axle_sprocket_teeth -= 2;
	gearbox.engine_sprocket_teeth += 1; // more teeth on the engine is a taller gear
	const double taller = gearbox.road_speed(engine.hard_cut_rpm, 6, ROLLING_RADIUS);
	CHECK(taller > baseline);

	MESSAGE("18/25 " << kart::core::ms_to_kmh(baseline) << " km/h, 18/27 "
					 << kart::core::ms_to_kmh(shorter) << " km/h, 19/25 "
					 << kart::core::ms_to_kmh(taller) << " km/h");
}

TEST_CASE("shifting is sequential and cannot be rushed") {
	Gearbox gearbox;
	const double dt = 1.0 / 240.0;

	CHECK(gearbox.gear() == 0);
	gearbox.step(dt, true, false);
	CHECK(gearbox.shifting());
	CHECK(gearbox.target_gear() == 1);
	CHECK(gearbox.gear() == 0); // not yet — the dogs are still moving

	// A second request during the shift is dropped, not queued. Queue it and a
	// player who taps the paddle twice ends up a gear further along than they
	// meant, which is how a kart gets described as shifting on its own.
	gearbox.step(dt, true, false);
	CHECK(gearbox.target_gear() == 1);

	while (gearbox.shifting()) {
		gearbox.step(dt, false, false);
	}
	CHECK(gearbox.gear() == 1);

	// No skipping: six requests from first reach sixth and stop there.
	for (int shift = 0; shift < 8; ++shift) {
		gearbox.step(dt, true, false);
		while (gearbox.shifting()) {
			gearbox.step(dt, false, false);
		}
	}
	CHECK(gearbox.gear() == kart::core::kz::GEAR_COUNT);

	// And down to neutral, one at a time, with a floor.
	for (int shift = 0; shift < 10; ++shift) {
		gearbox.step(dt, false, true);
		while (gearbox.shifting()) {
			gearbox.step(dt, false, false);
		}
	}
	CHECK(gearbox.gear() == 0);
}

TEST_CASE("the shift cut is 50-80 ms and the torque really goes away") {
	Gearbox gearbox;
	gearbox.force_gear(3);
	const double dt = 1.0 / 240.0;

	CHECK(gearbox.torque_scale() == doctest::Approx(1.0));

	gearbox.step(dt, true, false);
	int substeps = 1;
	CHECK(gearbox.torque_scale() == doctest::Approx(0.0));
	while (gearbox.shifting()) {
		gearbox.step(dt, false, false);
		CHECK(gearbox.torque_scale() == doctest::Approx(gearbox.shifting() ? 0.0 : 1.0));
		++substeps;
	}

	const double cut_seconds = substeps * dt;
	MESSAGE("shift cut " << (cut_seconds * 1000.0) << " ms over " << substeps
						 << " substeps at 240 Hz");

	// ARCHITECTURE.md §6.3 asks for 50-80 ms. At 240 Hz that is 12 to 19
	// substeps, so the quantization the solver imposes is well inside the band —
	// which is worth knowing, because a 20 ms shift at 120 Hz would be three
	// ticks and unfeelable.
	CHECK(cut_seconds >= 0.050);
	CHECK(cut_seconds <= 0.080);
	CHECK(gearbox.gear() == 4);
	CHECK(gearbox.torque_scale() == doctest::Approx(1.0));
}
