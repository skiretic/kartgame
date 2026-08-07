#include "core/ai_driver.h"

#include "core/racing_line.h"
#include "core/replay.h"
#include "core/units.h"

#include <doctest.h>

#include <cmath>
#include <memory>

// Tests for `src/core/ai_driver.h`. Engine-free; `tests/run.sh` runs them.
//
// ## What is asserted, and what deliberately is not
//
// The same rule `test_racing_line.cpp` states and for the same reason: issue
// #137 is open, the tire is expected to move, so **no test here names a lateral
// acceleration, a corner speed or a lap time.** What is asserted is behavior
// that holds whatever the ceilings turn out to be:
//
//   * a kart already at the line's speed on a straight neither drives nor
//     brakes;
//   * a kart faster than the corner it is approaching brakes, and brakes
//     *earlier* when the ceiling is lower — the relation, not the distance;
//   * the aim point decides the sign of the steer, both ways, and the sign
//     convention is checked against `DriverInput`'s own comment rather than
//     against a number somebody remembered;
//   * the shift hysteresis needs `shift_confirm_ticks` of agreement and then
//     stays quiet for `shift_cooldown_ticks`;
//   * the traction budget cuts throttle in a corner and does not in a straight
//     line, with the budget read off the limits handed in.
//
// ## The analytic case, and why it is a stopping distance
//
// A controller planning at deceleration `a` may hold `v` at distance `d` from a
// corner of speed `w` exactly while `v^2 <= w^2 + 2 a d`. That is one line of
// kinematics, it is exact, and it gives every braking assertion below a number
// to be checked against that no amount of tuning can move. The tests solve it
// for the distance at which the controller must first ask for brake and check
// the transition lands within one preview sample of it.

using namespace kart::core;
using namespace kart::core::ai;

namespace {

// Ceilings a test can reason about. **Not this kart's** — deliberately round
// numbers, so a reader can do the kinematics in their head and so nothing here
// goes red when `tire.h` moves. What the real node hands over comes from
// `KartRacingLine::model()`; what these tests check is that the controller uses
// whatever it is handed.
AiLimits test_limits(double brake_ms2 = 20.0, double lateral_ms2 = 20.0) {
	AiLimits limits;
	limits.brake_limit_ms2 = brake_ms2;
	limits.lateral_limit_ms2 = lateral_ms2;
	limits.wheelbase_m = 1.05;
	limits.max_lock_rad = 0.4363; // 25 degrees
	limits.soft_cut_rpm = 13000.0;
	limits.hard_cut_rpm = 14000.0;
	limits.gear_count = 6;
	return limits;
}

// A flat preview: the line is `speed` everywhere in the window.
void level_preview(AiObservation &observation, double speed, double span_m) {
	observation.preview_count = PREVIEW_SAMPLES;
	for (int index = 0; index < PREVIEW_SAMPLES; ++index) {
		observation.preview[index].distance_m =
				span_m * double(index) / double(PREVIEW_SAMPLES - 1);
		observation.preview[index].speed_ms = speed;
	}
}

// A corner of `corner_speed` starting `corner_at` meters ahead; the line is
// `straight_speed` before it.
void corner_preview(AiObservation &observation, double straight_speed, double corner_speed,
		double corner_at, double span_m) {
	observation.preview_count = PREVIEW_SAMPLES;
	for (int index = 0; index < PREVIEW_SAMPLES; ++index) {
		const double distance = span_m * double(index) / double(PREVIEW_SAMPLES - 1);
		observation.preview[index].distance_m = distance;
		observation.preview[index].speed_ms =
				distance >= corner_at ? corner_speed : straight_speed;
	}
}

// The kart at the origin pointing down -Z, which is `track.h` heading zero.
AiObservation straight_ahead(double speed_ms) {
	AiObservation observation;
	observation.speed_ms = speed_ms;
	observation.x = 0.0;
	observation.z = 0.0;
	observation.heading_rad = 0.0;
	observation.gear = 4;
	observation.line_gear = 4;
	observation.rpm = 10000.0;
	observation.aim_x = 0.0;
	observation.aim_z = -10.0; // straight ahead
	return observation;
}

} // namespace

TEST_CASE("ai driver refuses to pretend it was configured") {
	AiDriver driver;
	CHECK_FALSE(driver.configured());

	AiLimits limits;
	limits.brake_limit_ms2 = 20.0;
	// Everything else left at zero: no lateral ceiling, no wheelbase, no lock.
	driver.configure(limits, AiTune());
	CHECK_FALSE(driver.configured());

	driver.configure(test_limits(), AiTune());
	CHECK(driver.configured());
}

TEST_CASE("on the line's own speed on a straight it neither drives nor brakes") {
	AiDriver driver;
	AiTune tune;
	driver.configure(test_limits(), tune);

	AiObservation observation = straight_ahead(30.0);
	level_preview(observation, 30.0, tune.preview_m);

	const DriverInput input = driver.step(observation);
	CHECK(input.throttle == doctest::Approx(0.0));
	CHECK(input.brake == doctest::Approx(0.0));
	// The steer is not exactly zero only if the aim point is off to a side, and
	// it is not.
	CHECK(std::fabs(input.steer) < 1e-9);
	CHECK_FALSE(driver.decision().braking);
}

TEST_CASE("below the line's speed it drives, above it it brakes") {
	AiDriver driver;
	AiTune tune;
	driver.configure(test_limits(), tune);

	AiObservation slow = straight_ahead(20.0);
	level_preview(slow, 30.0, tune.preview_m);
	const DriverInput driving = driver.step(slow);
	CHECK(driving.throttle > 0.0);
	CHECK(driving.brake == doctest::Approx(0.0));

	// A level preview at 20 with the kart at 30 is a straight the kart is
	// already too fast for, everywhere in the window.
	AiObservation fast = straight_ahead(30.0);
	level_preview(fast, 20.0, tune.preview_m);
	const DriverInput braking = driver.step(fast);
	CHECK(braking.brake > 0.0);
	CHECK(braking.throttle == doctest::Approx(0.0));
	CHECK(driver.decision().braking);
}

TEST_CASE("the brake point is the kinematic one and moves with the ceiling") {
	// Approaching a 20 m/s corner at 40 m/s. The controller must hold throttle
	// while `40^2 <= 20^2 + 2 a d` and ask for brake once it does not, with
	// `a = brake_limit * brake_plan_fraction`.
	AiTune tune;
	tune.traction_budget = false; // isolate the speed controller

	struct Case {
		double ceiling_ms2;
	};
	const Case cases[2] = { { 20.0 }, { 10.0 } };
	double first_brake_at[2] = { 0.0, 0.0 };

	for (int which = 0; which < 2; ++which) {
		AiDriver driver;
		driver.configure(test_limits(cases[which].ceiling_ms2), tune);
		const double plan = cases[which].ceiling_ms2 * tune.brake_plan_fraction;
		// **The coast band is part of the analytic answer, not slop around it.**
		// The controller asks for brake when the allowed speed is `coast_band_ms`
		// *below* the actual one, so the distance to solve for is the one at
		// which the reachable speed has fallen to `40 - coast_band`, not to 40.
		// The first cut of this test left it out, put the expectation 0.8 m and
		// 1.5 m early in the two cases, and would have been "fixed" by widening
		// the tolerance until it passed — which is how a gate stops measuring
		// anything.
		const double entry = 40.0 - tune.coast_band_ms;
		const double analytic = (entry * entry - 20.0 * 20.0) / (2.0 * plan);

		// Walk the corner toward the kart a meter at a time and find where the
		// controller first asks for brake.
		double found = -1.0;
		for (double corner_at = tune.preview_m; corner_at > 0.5; corner_at -= 0.25) {
			AiObservation observation = straight_ahead(40.0);
			corner_preview(observation, 40.0, 20.0, corner_at, tune.preview_m);
			const DriverInput input = driver.step(observation);
			if (input.brake > 0.0) {
				found = corner_at;
				break;
			}
		}
		REQUIRE(found > 0.0);
		first_brake_at[which] = found;

		// Within one preview sample of it, and the tolerance is the grid rather
		// than a number chosen to make this pass: the controller sees the corner
		// only at a grid point, so the last station that can still bind is the
		// largest multiple of the grid at or under the analytic distance, and
		// that is at most one grid step early. Never *late* — the check below is
		// one-sided for that reason, and a controller that braked after the
		// kinematics said it must would fail it.
		const double grid = tune.preview_m / double(PREVIEW_SAMPLES - 1);
		CHECK(found <= analytic + 0.3);
		CHECK(found >= analytic - grid - 0.3);
	}

	// The relation that survives issue #137: a lower ceiling brakes earlier.
	CHECK(first_brake_at[1] > first_brake_at[0]);
}

TEST_CASE("the aim point decides the sign of the steer, and left is positive") {
	AiDriver driver;
	driver.configure(test_limits(), AiTune());

	// Heading zero is -Z. Godot's +X is the kart's right, so an aim point at
	// +X is to the right and `DriverInput::steer` — "positive to the left" — must
	// come out negative. This is the assertion that catches a flipped basis, and
	// it is written against `vehicle_state.h`'s own sentence rather than against
	// a remembered convention.
	AiObservation right = straight_ahead(20.0);
	level_preview(right, 20.0, AiTune().preview_m);
	right.aim_x = 4.0;
	right.aim_z = -10.0;
	const DriverInput to_right = driver.step(right);
	CHECK(to_right.steer < 0.0);
	CHECK(driver.decision().demanded_curvature > 0.0); // right-hander, track.h sign

	AiObservation left = straight_ahead(20.0);
	level_preview(left, 20.0, AiTune().preview_m);
	left.aim_x = -4.0;
	left.aim_z = -10.0;
	const DriverInput to_left = driver.step(left);
	CHECK(to_left.steer > 0.0);
	CHECK(driver.decision().demanded_curvature < 0.0);

	// Symmetric, because nothing in the chain has a handed term.
	CHECK(to_left.steer == doctest::Approx(-to_right.steer));
}

TEST_CASE("the steer is a lock fraction and it saturates rather than wrapping") {
	AiDriver driver;
	driver.configure(test_limits(), AiTune());

	// An aim point directly abeam. Pure pursuit wants an enormous curvature; the
	// answer has to be a saturated lock and not a wrapped one.
	AiObservation abeam = straight_ahead(10.0);
	level_preview(abeam, 10.0, AiTune().preview_m);
	abeam.aim_x = 2.0;
	abeam.aim_z = 0.0;
	const DriverInput input = driver.step(abeam);
	CHECK(input.steer <= 0.0);
	CHECK(input.steer >= -1.0);

	// And behind it. The pure-pursuit circle through a point behind the kart is
	// the shortest arc back, which at any speed is a spin; the controller
	// saturates on the side the point is on and is then held by the grip cap.
	AiObservation behind = straight_ahead(10.0);
	level_preview(behind, 10.0, AiTune().preview_m);
	behind.aim_x = 1.0;
	behind.aim_z = 8.0; // +Z is behind a kart heading -Z
	const DriverInput back = driver.step(behind);
	CHECK(back.steer < 0.0);
	CHECK(back.steer >= -1.0);
	// The cap is what stops it, not the lock clamp: at 10 m/s against a 20 m/s^2
	// ceiling the most the geometry may ask for is `curve_margin * 20 / 100`.
	CHECK(driver.decision().demanded_curvature ==
			doctest::Approx(AiTune().curve_margin * 20.0 / 100.0));
}

TEST_CASE("the steering demand is capped by the grip ceiling at this speed") {
	// The single most load-bearing line in the controller, and it is a relation
	// rather than a number: whatever the ceiling turns out to be, the curvature
	// asked for never implies more lateral acceleration than `curve_margin` of
	// it. Written against an absolute this test would go red the day issue #137
	// closed.
	AiTune tune;
	for (double ceiling : { 8.0, 20.0, 40.0 }) {
		AiDriver driver;
		driver.configure(test_limits(20.0, ceiling), tune);
		for (double speed : { 5.0, 15.0, 30.0, 45.0 }) {
			AiObservation observation = straight_ahead(speed);
			level_preview(observation, speed, tune.preview_m);
			// An aim point hard to one side: the geometry wants far more than
			// the grip allows.
			observation.aim_x = 20.0;
			observation.aim_z = -4.0;
			driver.step(observation);
			const double curvature = driver.decision().demanded_curvature;
			const double implied = curvature * speed * speed;
			CHECK(implied <= tune.curve_margin * ceiling + 1e-9);
		}
	}
}

TEST_CASE("it can select a gear from neutral") {
	// With `auto_shift` off — which is how an AI kart runs, because two shifters
	// on one gearbox is 15 shifts in five seconds — the gearbox starts in
	// neutral. A shift rule that required a gear already engaged had no way out
	// of it: measured, 1,201 ticks at 0.30 km/h with the throttle wide open.
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation observation = straight_ahead(0.0);
	level_preview(observation, 30.0, tune.preview_m);
	observation.gear = 0; // neutral
	observation.line_gear = 1;

	for (int tick = 0; tick < tune.shift_confirm_ticks - 1; ++tick) {
		CHECK_FALSE(driver.step(observation).shift_up);
	}
	CHECK(driver.step(observation).shift_up);
}

TEST_CASE("the rev limiter override does not ratchet through the gearbox") {
	// It fires only when the table agrees with the engaged gear. Written `<=`
	// instead of `==` it fires again in the gear it just selected, and again,
	// and again: measured, sixth gear at 72 km/h against a table saying second.
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation observation = straight_ahead(20.0);
	level_preview(observation, 20.0, tune.preview_m);
	observation.gear = 4;
	observation.line_gear = 2; // the table wants a LOWER gear
	observation.rpm = 14000.0; // and the engine is over the soft cut

	// The override must not fire, because the table and the gear disagree. What
	// should happen is a downshift toward the table's answer.
	for (int tick = 0; tick < tune.shift_confirm_ticks - 1; ++tick) {
		driver.step(observation);
	}
	const DriverInput fired = driver.step(observation);
	CHECK_FALSE(fired.shift_up);
	CHECK(fired.shift_down);
}

TEST_CASE("the traction budget spends the ellipse and can be turned off") {
	AiTune budgeted;
	AiTune bare;
	bare.traction_budget = false;

	// A corner using half the lateral ceiling. `sqrt(1 - 0.5^2)` is 0.866, so
	// the throttle must come out at that fraction of the unbudgeted answer.
	const double ceiling = 20.0;
	const double speed = 20.0;
	const double curvature = 0.5 * ceiling / (speed * speed);

	AiDriver with_budget;
	with_budget.configure(test_limits(20.0, ceiling), budgeted);
	AiDriver without;
	without.configure(test_limits(20.0, ceiling), bare);

	AiObservation observation = straight_ahead(speed);
	level_preview(observation, speed + 10.0, budgeted.preview_m);
	observation.line_curvature = curvature;

	const DriverInput budgeted_input = with_budget.step(observation);
	const DriverInput bare_input = without.step(observation);

	REQUIRE(bare_input.throttle > 0.0);
	CHECK(with_budget.decision().longitudinal_budget == doctest::Approx(std::sqrt(0.75)));
	CHECK(without.decision().longitudinal_budget == doctest::Approx(1.0));
	CHECK(budgeted_input.throttle ==
			doctest::Approx(bare_input.throttle * std::sqrt(0.75)).epsilon(1e-9));

	// Straight line, same everything: the budget must cost nothing.
	AiObservation straight = straight_ahead(speed);
	level_preview(straight, speed + 10.0, budgeted.preview_m);
	straight.line_curvature = 0.0;
	CHECK(with_budget.step(straight).throttle == doctest::Approx(without.step(straight).throttle));
}

TEST_CASE("a shift needs its confirmation ticks and then a cooldown") {
	AiTune tune;
	tune.traction_budget = false;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation observation = straight_ahead(30.0);
	level_preview(observation, 30.0, tune.preview_m);
	observation.gear = 3;
	observation.line_gear = 4;

	int ups = 0;
	for (int tick = 0; tick < tune.shift_confirm_ticks - 1; ++tick) {
		const DriverInput input = driver.step(observation);
		if (input.shift_up) {
			++ups;
		}
	}
	// Nothing yet: the disagreement has not persisted long enough.
	CHECK(ups == 0);

	const DriverInput fired = driver.step(observation);
	CHECK(fired.shift_up);
	CHECK_FALSE(fired.shift_down);

	// And then quiet, for the whole cooldown, even though the gearbox has not
	// answered yet and the disagreement is still there. Without this the
	// controller sweeps the whole box in `shift_cooldown_ticks` ticks.
	for (int tick = 0; tick < tune.shift_cooldown_ticks; ++tick) {
		const DriverInput input = driver.step(observation);
		CHECK_FALSE(input.shift_up);
		CHECK_FALSE(input.shift_down);
	}
}

TEST_CASE("a disagreement that does not persist never fires") {
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation agree = straight_ahead(30.0);
	level_preview(agree, 30.0, tune.preview_m);
	agree.gear = 3;
	agree.line_gear = 3;

	AiObservation disagree = agree;
	disagree.line_gear = 4;

	// Alternating: `shift_confirm_ticks` disagreements, but never consecutive.
	for (int tick = 0; tick < 8 * tune.shift_confirm_ticks; ++tick) {
		const DriverInput input = driver.step(tick % 2 == 0 ? disagree : agree);
		CHECK_FALSE(input.shift_up);
		CHECK_FALSE(input.shift_down);
	}
}

TEST_CASE("the rev limiter overrides the line's gear upward") {
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation observation = straight_ahead(30.0);
	level_preview(observation, 30.0, tune.preview_m);
	observation.gear = 3;
	observation.line_gear = 3; // the line is happy
	observation.rpm = 13500.0; // past the soft cut

	for (int tick = 0; tick < tune.shift_confirm_ticks - 1; ++tick) {
		driver.step(observation);
	}
	CHECK(driver.step(observation).shift_up);
}

TEST_CASE("a standing start feathers the clutch home") {
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	// Stopped: as disengaged as the launch gets, and on the throttle.
	AiObservation stopped = straight_ahead(0.0);
	level_preview(stopped, 30.0, tune.preview_m);
	stopped.gear = 1;
	stopped.line_gear = 1;
	const DriverInput launch = driver.step(stopped);
	// `launch_clutch_max`, not 1.0. See its own note: `clutch = 1` is the lever
	// fully pulled and nothing reaches the axle, so a controller that asked for
	// it sat on the grid at 0.19 km/h with the throttle wide open.
	CHECK(launch.clutch == doctest::Approx(tune.launch_clutch_max));
	CHECK(launch.throttle >= tune.launch_throttle);
	CHECK(driver.decision().launching);

	// Halfway to `launch_speed_ms`: halfway home.
	AiObservation rolling = stopped;
	rolling.speed_ms = 0.5 * tune.launch_speed_ms;
	level_preview(rolling, 30.0, tune.preview_m);
	CHECK(driver.step(rolling).clutch == doctest::Approx(0.5 * tune.launch_clutch_max));

	// Past it: the clutch is out and the launch is over.
	AiObservation away = stopped;
	away.speed_ms = tune.launch_speed_ms + 1.0;
	level_preview(away, 30.0, tune.preview_m);
	const DriverInput driving = driver.step(away);
	CHECK(driving.clutch == doctest::Approx(0.0));
	CHECK_FALSE(driver.decision().launching);
}

TEST_CASE("a spun kart is not given power") {
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation spun = straight_ahead(15.0);
	level_preview(spun, 30.0, tune.preview_m); // the line wants more speed
	spun.body_slip_rad = tune.slip_abort_rad + 0.1;

	const DriverInput input = driver.step(spun);
	CHECK(input.throttle == doctest::Approx(0.0));
	CHECK(driver.decision().aborting);

	// Just inside the threshold it drives, so the test above is measuring the
	// threshold and not the fact that the throttle happens to be zero.
	AiObservation held = spun;
	held.body_slip_rad = tune.slip_abort_rad - 0.1;
	CHECK(driver.step(held).throttle > 0.0);
}

TEST_CASE("everything it emits survives the replay grid") {
	// `replay_encode_input` refuses off-grid input rather than rounding it, so a
	// producer whose output cannot be snapped is a producer whose laps cannot be
	// recorded. The node snaps; this checks the snap is lossless in the sense
	// that matters — snapping twice is snapping once — over the whole range the
	// controller can produce.
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	for (int tick = 0; tick < 400; ++tick) {
		AiObservation observation = straight_ahead(2.0 + 0.1 * double(tick));
		corner_preview(observation, 40.0, 12.0, 20.0, tune.preview_m);
		observation.aim_x = std::sin(0.05 * double(tick)) * 6.0;
		observation.aim_z = -12.0;
		observation.line_curvature = 0.01 * std::sin(0.03 * double(tick));
		observation.gear = 1 + (tick / 60) % 6;
		observation.line_gear = observation.gear;

		const DriverInput raw = driver.step(observation);
		const DriverInput snapped = replay_snap(raw);
		CHECK(replay_is_on_grid(snapped));
		// And the snap moved it by less than a code, on every axis.
		CHECK(std::fabs(snapped.throttle - raw.throttle) <= REPLAY_UNIT_QUANTUM);
		CHECK(std::fabs(snapped.brake - raw.brake) <= REPLAY_UNIT_QUANTUM);
		CHECK(std::fabs(snapped.clutch - raw.clutch) <= REPLAY_UNIT_QUANTUM);
		CHECK(std::fabs(snapped.steer - raw.steer) <= REPLAY_STEER_QUANTUM);
	}
}

TEST_CASE("the lookahead grows with speed and never collapses") {
	AiDriver driver;
	AiTune tune;
	driver.configure(test_limits(), tune);

	CHECK(driver.lookahead_for(0.0) == doctest::Approx(tune.lookahead_base_m));
	CHECK(driver.lookahead_for(40.0) ==
			doctest::Approx(tune.lookahead_base_m + 40.0 * tune.lookahead_gain_s));
	// A negative speed is not reachable from a `KartBody`, but a controller that
	// divides by a lookahead of zero is one bad reading away from a NaN steer.
	CHECK(driver.lookahead_for(-100.0) >= tune.lookahead_base_m);
}

TEST_CASE("reset forgets the hysteresis rather than carrying it across a respawn") {
	AiTune tune;
	AiDriver driver;
	driver.configure(test_limits(), tune);

	AiObservation observation = straight_ahead(30.0);
	level_preview(observation, 30.0, tune.preview_m);
	observation.gear = 3;
	observation.line_gear = 4;

	for (int tick = 0; tick < tune.shift_confirm_ticks - 1; ++tick) {
		driver.step(observation);
	}
	driver.reset();
	// The count is gone, so the very next tick must not fire.
	CHECK_FALSE(driver.step(observation).shift_up);
}

TEST_CASE("the controller drives a solved line around a circle without leaving it") {
	// A closed loop, driven open-loop against the controller's own steering
	// answer using a kinematic bicycle. Not a physics test — it cannot be, there
	// is no solver in `src/core/ai_driver.h`'s dependency set — but it is the one
	// thing a unit test can prove about pure pursuit that a probe cannot prove
	// cheaply: that the loop is *stable*, that a kart displaced from the line
	// returns to it and does not oscillate.
	AiLimits limits = test_limits();
	AiTune tune;
	AiDriver driver;
	driver.configure(limits, tune);

	const double radius = 40.0;
	const double speed = 15.0;
	const double step = 1.0 / 120.0;

	// Start 3 m outside the circle, pointing along it.
	double x = radius + 3.0;
	double z = 0.0;
	double heading = 0.0; // -Z, which is tangent to the circle at (r, 0)

	double worst_late = 0.0;
	for (int tick = 0; tick < 4000; ++tick) {
		AiObservation observation;
		observation.speed_ms = speed;
		observation.x = x;
		observation.z = z;
		observation.heading_rad = heading;
		observation.gear = 3;
		observation.line_gear = 3;
		observation.rpm = 10000.0;
		level_preview(observation, speed, tune.preview_m);

		// The aim point: the circle, one lookahead of arc ahead of where the
		// kart projects onto it.
		const double angle = std::atan2(z, x);
		const double ahead = driver.lookahead_for(speed) / radius;
		observation.aim_x = radius * std::cos(angle + ahead);
		observation.aim_z = radius * std::sin(angle + ahead);

		const DriverInput input = driver.step(observation);

		// Kinematic bicycle, integrated at the solver's own rate. `steer` is a
		// lock fraction positive to the left, so the curvature it asks for is
		// `tan(-steer * lock) / wheelbase`.
		const double angle_rad = -input.steer * limits.max_lock_rad;
		const double curvature = std::tan(angle_rad) / limits.wheelbase_m;
		heading += curvature * speed * step;
		x += std::sin(heading) * speed * step;
		z += -std::cos(heading) * speed * step;

		// The error, after the first half lap has settled it.
		if (tick > 1200) {
			const double error = std::fabs(std::sqrt(x * x + z * z) - radius);
			if (error > worst_late) {
				worst_late = error;
			}
		}
	}

	// It converged and stayed converged. The 3 m it started with is what makes
	// this a convergence test rather than a "it did not move" test.
	CHECK(worst_late < 0.35);
	// And it is still on the circle at the end rather than having wandered off
	// with a small residual every lap.
	CHECK(std::fabs(std::sqrt(x * x + z * z) - radius) < 0.35);
}

TEST_CASE("a slower line is followed as faithfully as a fast one") {
	// The difficulty tiers move `grip_usage` on the *line*; the controller does
	// not know what a tier is. This asserts exactly that: two identical
	// observations differing only in the preview speed produce the same steer and
	// a lower demanded speed, with no tier-shaped branch anywhere.
	AiTune tune;
	AiDriver fast;
	AiDriver slow;
	fast.configure(test_limits(), tune);
	slow.configure(test_limits(), tune);

	AiObservation quick = straight_ahead(20.0);
	level_preview(quick, 30.0, tune.preview_m);
	quick.aim_x = 3.0;
	quick.aim_z = -12.0;

	AiObservation gentle = quick;
	level_preview(gentle, 24.0, tune.preview_m);

	const DriverInput a = fast.step(quick);
	const DriverInput b = slow.step(gentle);
	CHECK(a.steer == doctest::Approx(b.steer));
	CHECK(fast.decision().allowed_speed_ms > slow.decision().allowed_speed_ms);
	CHECK(a.throttle >= b.throttle);
}
