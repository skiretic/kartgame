#include "doctest.h"

#include "core/drivetrain.h"
#include "core/kz_reference.h"
#include "core/units.h"

#include <cmath>

// These are scenario tests. Each one steps the drivetrain at the 240 Hz the
// solver will actually use, against a deliberately crude vehicle, and reports a
// number in the units ARCHITECTURE.md §6.4 quotes — km/h, seconds, g. The crude
// vehicle is the point: if a measurement here depends on the tire model or the
// chassis, it is not a measurement of the drivetrain.

using kart::core::Drivetrain;
using kart::core::DrivetrainInput;
using kart::core::DrivetrainOutput;

static constexpr double SUBSTEP = 1.0 / 240.0;
static constexpr double ROLLING_RADIUS = 0.1375;

// A kart reduced to one degree of freedom.
//
// It rolls without slip, so there is no tire model here and no wheel spin: the
// tractive force is simply capped at what a pair of kart slicks can hold. That
// cap is 2,100 N, which is `tire.h`'s 2.10 peak friction coefficient against the
// ~1,000 N a KZ carries on its rear axle statically. It ignores the rearward
// weight transfer that a real launch produces — which raises the real limit —
// so every acceleration figure below is a conservative one.
//
// The important line is the effective mass. `reflected_inertia` comes back from
// the drivetrain and is divided by the tire radius squared to become apparent
// mass at the contact patch, which is precisely the term ARCHITECTURE.md's §6.3
// gearbox makes vary by a factor of seven between first and sixth.
struct Bench {
	Drivetrain drive;

	double speed = 0.0; // m/s
	double distance = 0.0; // m
	double axle_inertia = 0.05; // kg·m², axle, hubs, wheels and brake disc
	double mass = kart::core::kz::MASS_WITH_DRIVER_KG;
	double traction_limit = 2100.0; // N
	double drag_area = 0.7; // Cd·A, m², kart plus a seated driver
	double rolling_resistance = 0.012;

	DrivetrainOutput last;

	DrivetrainOutput step(double throttle, double clutch, bool up = false, bool down = false) {
		DrivetrainInput input;
		input.throttle = throttle;
		input.clutch = clutch;
		input.shift_up = up;
		input.shift_down = down;
		input.axle_speed = speed / ROLLING_RADIUS;
		input.dt = SUBSTEP;

		last = drive.step(input);

		double tractive = last.axle_torque / ROLLING_RADIUS;
		if (tractive > traction_limit) {
			tractive = traction_limit;
		}
		if (tractive < -traction_limit) {
			tractive = -traction_limit;
		}

		const double aero = 0.5 * 1.2 * drag_area * speed * std::fabs(speed);
		const double rolling = rolling_resistance * mass * kart::core::G *
				(speed > 0.01 ? 1.0 : 0.0);
		const double effective_mass =
				mass + (axle_inertia + last.reflected_inertia) / (ROLLING_RADIUS * ROLLING_RADIUS);

		speed += (tractive - aero - rolling) / effective_mass * SUBSTEP;
		if (speed < 0.0) {
			speed = 0.0;
		}
		distance += speed * SUBSTEP;
		return last;
	}

	double kmh() const { return kart::core::ms_to_kmh(speed); }

	void place_at(double kmh_speed, int gear) {
		speed = kart::core::kmh_to_ms(kmh_speed);
		drive.engage(gear, speed / ROLLING_RADIUS);
	}
};

TEST_CASE("a full acceleration run with the assists on is stable and lands on the reference figures") {
	Bench bench;
	bench.drive.assists.auto_clutch = true;
	bench.drive.assists.auto_shift = true;

	double time_to_100 = -1.0;
	double time_first_moved = -1.0;
	double highest_rpm = 0.0;
	int shifts = 0;
	int previous_gear = 0;
	bool saw_over_rev = false;

	for (int substep = 0; substep < 240 * 25; ++substep) {
		const DrivetrainOutput out = bench.step(1.0, 0.0);
		if (time_first_moved < 0.0 && bench.speed > 0.1) {
			time_first_moved = substep * SUBSTEP;
		}

		CHECK(std::isfinite(out.engine_rpm));
		CHECK(std::isfinite(out.axle_torque));
		CHECK(std::isfinite(bench.speed));
		if (out.engine_rpm > highest_rpm) {
			highest_rpm = out.engine_rpm;
		}
		if (out.over_rev) {
			saw_over_rev = true;
		}
		if (out.gear != previous_gear) {
			++shifts;
			previous_gear = out.gear;
		}
		if (time_to_100 < 0.0 && bench.kmh() >= 100.0) {
			time_to_100 = substep * SUBSTEP;
		}
	}

	MESSAGE("terminal speed " << bench.kmh() << " km/h in gear " << bench.last.gear
							  << " at " << bench.last.engine_rpm << " rpm");
	MESSAGE("0-100 km/h in " << time_to_100 << " s from a standing start with the engine at idle ("
							 << (time_to_100 - time_first_moved)
							 << " s from the moment it moves); " << shifts
							 << " gear changes; highest engine speed " << highest_rpm << " rpm");

	// The kart reaches the top of sixth, and the speed it reaches is the
	// gearing's, not the harness's: the engine runs out of revs before the drag
	// model runs out of power.
	CHECK(bench.last.gear == kart::core::kz::GEAR_COUNT);
	CHECK(bench.kmh() > kart::core::kz::TOP_SPEED_MIN_KMH);
	CHECK(bench.kmh() < kart::core::kz::TOP_SPEED_MAX_KMH);

	// Five upshifts from first, plus the assist selecting first out of neutral.
	CHECK(shifts == kart::core::kz::GEAR_COUNT);

	// Nothing diverged, nothing over-revved, and the engine never went anywhere
	// the limiter should have stopped it going. This is the stability claim: the
	// reflected inertia is what keeps a locked driveline from letting the axle
	// change speed instantly, and without it this run does not survive first gear.
	CHECK_FALSE(saw_over_rev);
	CHECK(highest_rpm <= bench.drive.engine.hard_cut_rpm + 1.0);
	CHECK_FALSE(bench.drive.stalled());
	CHECK(bench.drive.engine.damage() == doctest::Approx(0.0));

	// A KZ does 0-100 km/h in 3.0-3.5 s (kz_reference.h). This run is slower than
	// that and both reasons are the harness rather than the drivetrain: about
	// 0.6 s of it is the engine winding up from idle before the clutch takes
	// anything, where a real driver sits on the line at launch revs, and the
	// 2,100 N traction cap ignores the rearward weight transfer that a 1.2 g
	// launch produces and which in reality raises the limit by a quarter. Timed
	// from the moment the kart moves it lands on the reference range, and that is
	// the figure to compare. Asserted loosely on purpose — the real 0-100 belongs
	// to the vehicle scenarios in tools/verify/drive.sh, not here.
	CHECK((time_to_100 - time_first_moved) > 3.0);
	CHECK((time_to_100 - time_first_moved) < 4.2);
}

TEST_CASE("speed never falls except across a shift, and each shift costs something") {
	Bench bench;
	double previous = 0.0;
	double worst_loss = 0.0;
	bool losses_only_while_shifting = true;

	for (int substep = 0; substep < 240 * 12; ++substep) {
		const DrivetrainOutput out = bench.step(1.0, 0.0);
		const double delta = bench.speed - previous;
		if (delta < 0.0) {
			if (!out.shifting) {
				losses_only_while_shifting = false;
			}
			if (-delta > worst_loss) {
				worst_loss = -delta;
			}
		}
		previous = bench.speed;
		// During the cut the axle gets nothing at all. Not less — nothing.
		if (out.shifting) {
			CHECK(out.axle_torque == doctest::Approx(0.0));
			CHECK(out.reflected_inertia == doctest::Approx(0.0));
		}
	}

	MESSAGE("worst per-substep speed loss " << (worst_loss * 240.0) << " m/s per second");
	CHECK(losses_only_while_shifting);
}

TEST_CASE("the shift cut is felt: torque is gone for 50-80 ms") {
	Bench bench;
	bench.drive.assists.auto_shift = false;
	bench.drive.assists.auto_clutch = false;
	bench.place_at(90.0, 4);

	// Settle, then ask for fifth.
	for (int substep = 0; substep < 24; ++substep) {
		bench.step(1.0, 0.0);
	}
	CHECK(bench.last.axle_torque > 0.0);

	int cut_substeps = 0;
	bench.step(1.0, 0.0, true, false);
	while (bench.last.shifting) {
		CHECK(bench.last.axle_torque == doctest::Approx(0.0));
		++cut_substeps;
		bench.step(1.0, 0.0);
	}

	const double cut_ms = cut_substeps * SUBSTEP * 1000.0;
	MESSAGE("upshift 4->5 under power: " << cut_ms << " ms with no torque at the axle");
	CHECK(cut_ms >= 50.0);
	CHECK(cut_ms <= 80.0);
	CHECK(bench.last.gear == 5);
	CHECK(bench.last.axle_torque > 0.0);
}

TEST_CASE("closed-throttle engine braking shapes corner entry") {
	// Second gear at 60 km/h, throttle shut. §6.3 says this should be able to
	// matter more than the brakes on some corners; brakes are worth 1.5-2.0 g
	// (kz_reference.h), so anything above about 0.2 g is a real contribution to
	// how a corner is entered rather than a rounding error.
	Bench second;
	second.drive.assists.auto_clutch = false;
	second.drive.assists.auto_shift = false;
	second.place_at(60.0, 2);
	second.drag_area = 0.0; // measure the engine alone, not the air
	second.rolling_resistance = 0.0;

	const double before = second.speed;
	second.step(0.0, 0.0);
	const double decel_g = (before - second.speed) / SUBSTEP / kart::core::G;

	const double rigid_g = -second.last.axle_torque / ROLLING_RADIUS /
			kart::core::kz::MASS_WITH_DRIVER_KG / kart::core::G;

	MESSAGE("second gear, 60 km/h, throttle shut: " << second.last.engine_rpm
													<< " rpm, crank drag " << (-second.last.engine_torque) << " Nm, "
													<< (-second.last.axle_torque) << " Nm at the axle");
	MESSAGE("  deceleration " << decel_g << " g (" << rigid_g
							  << " g if the rotating inertia is ignored)");

	CHECK(second.last.axle_torque < 0.0);
	CHECK(decel_g > 0.2);
	CHECK(decel_g < 0.6);

	// Sixth gear at the same road speed, for the "far stronger in low gears" half
	// of issue #39. Same kart, same speed, same closed throttle: the only
	// difference is the ratio, and the ratio appears twice — once in the engine
	// speed it produces and once in the torque multiplication.
	Bench sixth;
	sixth.drive.assists.auto_clutch = false;
	sixth.drive.assists.auto_shift = false;
	sixth.place_at(60.0, 6);
	sixth.drag_area = 0.0;
	sixth.rolling_resistance = 0.0;

	const double sixth_before = sixth.speed;
	sixth.step(0.0, 0.0);
	const double sixth_g = (sixth_before - sixth.speed) / SUBSTEP / kart::core::G;

	MESSAGE("sixth gear, 60 km/h, throttle shut: " << sixth.last.engine_rpm << " rpm, "
												   << sixth_g << " g");
	CHECK(sixth_g > 0.0);
	CHECK(decel_g > sixth_g * 2.0);

	// And the failure to check against: with no engine braking at all the kart
	// coasts, and §6.3 says that feels like ice. Quantified — the aerodynamic and
	// rolling losses alone at 60 km/h.
	Bench coasting;
	coasting.drive.assists.auto_clutch = false;
	coasting.drive.assists.auto_shift = false;
	coasting.place_at(60.0, 0);
	const double coast_before = coasting.speed;
	coasting.step(0.0, 1.0);
	const double coast_g = (coast_before - coasting.speed) / SUBSTEP / kart::core::G;
	MESSAGE("neutral, clutch in, 60 km/h: " << coast_g << " g from drag alone");
	CHECK(decel_g > coast_g * 3.0);
}

TEST_CASE("dumping the clutch from rest stalls the engine") {
	Bench bench;
	bench.drive.assists.auto_clutch = false;
	bench.drive.assists.auto_shift = false;
	bench.drive.gearbox.force_gear(1);

	// Lever fully released, a third of the throttle, kart stationary. First gear
	// multiplies the axle's resistance by 14.7 back at the crank and the engine
	// has nothing at idle to fight it with.
	int substeps = 0;
	for (; substeps < 240 && !bench.drive.stalled(); ++substeps) {
		bench.step(0.3, 0.0);
	}

	MESSAGE("clutch dumped at rest in first: engine dead after "
			<< (substeps * SUBSTEP) << " s, having moved the kart " << bench.distance << " m");
	// Issue #38: stalling has to be possible with the assists off, or the
	// auto-clutch defaulting on is not protecting the player from anything.
	CHECK(bench.drive.stalled());
	CHECK(bench.speed < 1.0);

	// And a push start puts it back.
	bench.drive.push_start();
	CHECK_FALSE(bench.drive.stalled());
	CHECK(bench.drive.engine_rpm() == doctest::Approx(bench.drive.engine.idle_rpm));
}

TEST_CASE("a launch is possible by hand, and it takes clutch modulation") {
	Bench bench;
	bench.drive.assists.auto_clutch = false;
	bench.drive.assists.auto_shift = false;
	bench.drive.gearbox.force_gear(1);

	double peak_slip = 0.0;
	double rpm_at_bite = 0.0;

	for (int substep = 0; substep < 240 * 4; ++substep) {
		const double time = substep * SUBSTEP;
		// Rev it against an open clutch, then feed the lever in over a second and
		// a half. This is the whole skill: too fast and it bogs or stalls, too
		// slow and the plates cook.
		double lever = 1.0;
		if (time > 0.5) {
			lever = 1.0 - (time - 0.5) / 1.5;
			if (lever < 0.0) {
				lever = 0.0;
			}
		}
		const DrivetrainOutput out = bench.step(1.0, lever);
		if (std::fabs(out.clutch_slip) > peak_slip) {
			peak_slip = std::fabs(out.clutch_slip);
			rpm_at_bite = out.engine_rpm;
		}
		CHECK_FALSE(bench.drive.stalled());
	}

	MESSAGE("hand launch: " << bench.kmh() << " km/h after 4 s, peak clutch slip "
							<< peak_slip << " rad/s at " << rpm_at_bite << " rpm");

	CHECK(peak_slip > 100.0); // the slip is real, and visible in telemetry
	CHECK(bench.kmh() > 45.0);
	CHECK(bench.drive.clutch_locked());
}

TEST_CASE("with the auto-clutch on, a launch needs nothing but throttle") {
	Bench bench;
	bench.drive.assists.auto_clutch = true;
	bench.drive.assists.auto_shift = false;
	bench.drive.gearbox.force_gear(1);

	double slipping_rpm = 0.0;
	int slipping_substeps = 0;

	for (int substep = 0; substep < 240 * 4; ++substep) {
		// Note the clutch input: fully released, which with the assist off would
		// have stalled it in the test above. The assist is doing all the work.
		const DrivetrainOutput out = bench.step(1.0, 0.0);
		CHECK_FALSE(bench.drive.stalled());
		if (!bench.drive.clutch_locked()) {
			slipping_rpm += out.engine_rpm;
			++slipping_substeps;
		}
	}

	const double average_launch_rpm =
			slipping_substeps > 0 ? slipping_rpm / slipping_substeps : 0.0;
	MESSAGE("auto-clutch launch: " << bench.kmh() << " km/h after 4 s, clutch slipped for "
								   << (slipping_substeps * SUBSTEP) << " s at an average of "
								   << average_launch_rpm << " rpm");

	CHECK(bench.kmh() > 45.0);
	CHECK(bench.drive.clutch_locked());
	// The assist finds the same equilibrium a driver would: the clutch's capacity
	// meets the engine's torque curve inside the powerband rather than at idle.
	CHECK(average_launch_rpm > 7000.0);
	CHECK(average_launch_rpm < 13000.0);
}

TEST_CASE("an over-rev on a downshift is possible, and it costs the engine") {
	Bench bench;
	bench.drive.assists.auto_clutch = false;
	bench.drive.assists.auto_shift = false;
	bench.place_at(135.0, 6);

	for (int substep = 0; substep < 24; ++substep) {
		bench.step(1.0, 0.0);
	}
	const double before = bench.last.engine_rpm;

	bench.step(0.0, 0.0, false, true);
	double highest = 0.0;
	for (int substep = 0; substep < 60; ++substep) {
		const DrivetrainOutput out = bench.step(0.0, 0.0);
		if (out.engine_rpm > highest) {
			highest = out.engine_rpm;
		}
	}

	MESSAGE("sixth to fifth at 135 km/h: " << before << " rpm becomes " << highest
										   << " rpm; damage " << bench.drive.engine.damage());

	// The gearbox does not refuse the shift and the rev limiter cannot prevent
	// what follows, because the road is turning the engine through a locked
	// driveline and the limiter only removes drive.
	CHECK(highest > bench.drive.engine.hard_cut_rpm);
	CHECK(bench.drive.engine.damage() > 0.0);
	CHECK(bench.drive.engine.peak_torque() < 26.2);

	// The same request with the auto-shift on is simply not made. That is issue
	// #40's "demonstrably harder with the assists off" in one measurement: the
	// mistake exists, and the assist is what stands between a new player and it.
	Bench assisted;
	assisted.drive.assists.auto_shift = true;
	assisted.place_at(135.0, 6);
	for (int substep = 0; substep < 120; ++substep) {
		assisted.step(0.0, 0.0, false, true);
	}
	MESSAGE("the same request with auto-shift on: gear " << assisted.last.gear << ", "
														 << assisted.last.engine_rpm << " rpm, damage "
														 << assisted.drive.engine.damage());
	CHECK(assisted.drive.engine.damage() == doctest::Approx(0.0));
}

TEST_CASE("reflected inertia is the ratio squared, and it is zero when the clutch is open") {
	Bench bench;
	bench.drive.assists.auto_clutch = false;
	bench.drive.assists.auto_shift = false;

	double reflected[7] = { 0, 0, 0, 0, 0, 0, 0 };
	for (int gear = 1; gear <= kart::core::kz::GEAR_COUNT; ++gear) {
		Bench geared;
		geared.drive.assists.auto_clutch = false;
		geared.drive.assists.auto_shift = false;
		geared.place_at(gear * 20.0, gear);
		geared.step(0.5, 0.0);
		reflected[gear] = geared.last.reflected_inertia;
		MESSAGE("gear " << gear << ": total ratio " << geared.drive.gearbox.total_ratio(gear)
						<< ", reflected inertia " << reflected[gear] << " kg m^2 ("
						<< (reflected[gear] / (ROLLING_RADIUS * ROLLING_RADIUS))
						<< " kg of apparent mass at the tire)");
	}

	const double ratio_1 = bench.drive.gearbox.total_ratio(1);
	const double ratio_6 = bench.drive.gearbox.total_ratio(6);
	CHECK(reflected[1] == doctest::Approx(bench.drive.engine.inertia * ratio_1 * ratio_1));
	// The square is the whole point: a factor of 2.74 in the ratios is a factor
	// of 7.5 in the inertia the axle feels.
	CHECK(reflected[1] / reflected[6] == doctest::Approx((ratio_1 * ratio_1) / (ratio_6 * ratio_6)));

	// Clutch fully pulled: nothing rigid connects the crank to the road, so
	// nothing of its inertia reaches the axle.
	bench.place_at(80.0, 4);
	bench.step(1.0, 1.0);
	CHECK(bench.last.reflected_inertia == doctest::Approx(0.0));
	CHECK(bench.last.axle_torque == doctest::Approx(0.0));
}

TEST_CASE("the same inputs produce the same numbers twice") {
	// ARCHITECTURE.md §8. The drivetrain reads no clock and holds no unordered
	// state, so two runs of an identical input stream must agree bit for bit —
	// not approximately, exactly. This is the property a ghost replay is built
	// on, and it is cheap to keep and expensive to recover.
	double first_speed = 0.0;
	double first_rpm = 0.0;
	double second_speed = 0.0;
	double second_rpm = 0.0;

	for (int run = 0; run < 2; ++run) {
		Bench bench;
		for (int substep = 0; substep < 240 * 8; ++substep) {
			const double time = substep * SUBSTEP;
			const double throttle = (substep / 120) % 3 == 0 ? 0.0 : 1.0;
			const bool down = std::fabs(time - 5.0) < SUBSTEP * 0.5;
			bench.step(throttle, 0.0, false, down);
		}
		if (run == 0) {
			first_speed = bench.speed;
			first_rpm = bench.last.engine_rpm;
		} else {
			second_speed = bench.speed;
			second_rpm = bench.last.engine_rpm;
		}
	}

	MESSAGE("two identical 8 s runs: " << first_speed << " m/s / " << first_rpm
									   << " rpm against " << second_speed << " m/s / " << second_rpm << " rpm");
	CHECK(first_speed == second_speed);
	CHECK(first_rpm == second_rpm);
}

TEST_CASE("engine speed stays finite and inside its limits through everything") {
	// A deliberately abusive input stream: throttle chopping, shift requests in
	// both directions, the clutch flicked in and out. Nothing here should produce
	// a NaN, an engine speed the limiter cannot explain, or a torque the clutch
	// cannot hold.
	Bench bench;
	bench.drive.assists.auto_clutch = false;
	bench.drive.assists.auto_shift = false;
	bench.place_at(70.0, 3);

	double highest_rpm = 0.0;
	for (int substep = 0; substep < 240 * 30; ++substep) {
		const double throttle = (substep % 97) < 60 ? 1.0 : 0.0;
		const double lever = (substep % 313) < 20 ? 1.0 : 0.0;
		const bool up = (substep % 421) == 0;
		const bool down = (substep % 977) == 0;

		const DrivetrainOutput out = bench.step(throttle, lever, up, down);
		if (bench.drive.stalled()) {
			bench.drive.push_start();
		}

		REQUIRE(std::isfinite(out.engine_rpm));
		REQUIRE(std::isfinite(out.axle_torque));
		REQUIRE(std::isfinite(out.reflected_inertia));
		REQUIRE(std::isfinite(bench.speed));
		CHECK(out.reflected_inertia >= 0.0);
		CHECK(std::fabs(out.clutch_torque) <= bench.drive.clutch.capacity + 1e-9);
		if (out.engine_rpm > highest_rpm) {
			highest_rpm = out.engine_rpm;
		}
	}

	MESSAGE("30 s of abuse: highest engine speed " << highest_rpm << " rpm, damage "
												   << bench.drive.engine.damage());
	// It may over-rev — the inputs are asking it to — but it must not run away.
	// An engine speed past 20,000 rpm here would mean the integration, not the
	// driver, put it there.
	CHECK(highest_rpm < 20000.0);
}
