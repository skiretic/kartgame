#include "doctest.h"

#include "core/lap_timing.h"

#include <cmath>
#include <cstring>

// The timer is driven the way the boundary will drive it: a position on the track
// and a bool, once per tick. Every test below is a lap someone could drive.

using namespace kart::core;

namespace {

constexpr double STEP = 1.0 / 120.0;

// The test track: 1,030 m, three even sectors. `scripts/track/track_layout.gd`
// owns the real number; this is a round one so the arithmetic in the assertions
// stays readable.
LapMarks three_sectors(double length = 1200.0) {
	return LapMarks::even(length, 3);
}

// Drive from one arc length to another at a fixed speed, one tick at a time,
// returning how many laps completed on the way. `off_track` applies to the whole
// run, which is how a test says "this bit was off the road".
int drive(LapTimer &timer, double from, double to, double speed_ms, double length,
		bool off_track = false) {
	int laps = 0;
	const double per_tick = speed_ms * STEP;
	// **The tick count is computed, not accumulated, and that is not fussiness.**
	// Adding `per_tick` in a loop until the position passes the destination drops the
	// last step whenever the accumulated error lands a hair short: 480 additions of
	// 25/120 reach 1199.9999999 rather than 1200, the `to - 1e-9` guard is satisfied,
	// and the step that crosses the start line is never fed to the timer at all. That
	// looked exactly like the timer failing to count a lap, and it cost four false
	// failures before the harness was suspected. Indexing from `from` keeps the error
	// at one multiplication and makes the final position exactly `to`.
	const int ticks = static_cast<int>(std::ceil((to - from) / per_tick));
	for (int i = 1; i <= ticks; ++i) {
		double position = from + per_tick * static_cast<double>(i);
		if (position > to || i == ticks) {
			position = to;
		}
		double wrapped = position;
		while (wrapped >= length) {
			wrapped -= length;
		}
		if (timer.advance(wrapped, off_track)) {
			++laps;
		}
	}
	return laps;
}

// One whole lap from the line at a constant speed, which is the case every other
// test is a variation on.
int drive_lap(LapTimer &timer, double speed_ms, double length, bool off_track = false) {
	return drive(timer, 0.0, length, speed_ms, length, off_track);
}

} // namespace

TEST_CASE("LapMarks rejects a layout that cannot be timed") {
	LapMarks marks = three_sectors();
	CHECK(marks.is_valid());
	CHECK(marks.sector_count() == 3);
	CHECK(marks.sector_length_m(0) == doctest::Approx(400.0));
	CHECK(marks.sector_length_m(2) == doctest::Approx(400.0));

	SUBCASE("the first mark must be the start line") {
		marks.mark_m[0] = 10.0;
		CHECK_FALSE(marks.is_valid());
	}
	SUBCASE("two marks at the same place are a zero-length sector") {
		// Which would report a 0.000 split, and a 0.000 split reads as a very fast
		// sector rather than as a broken track.
		marks.mark_m[2] = marks.mark_m[1];
		CHECK_FALSE(marks.is_valid());
	}
	SUBCASE("marks must ascend") {
		const double swap = marks.mark_m[1];
		marks.mark_m[1] = marks.mark_m[2];
		marks.mark_m[2] = swap;
		CHECK_FALSE(marks.is_valid());
	}
	SUBCASE("a mark at or past the length is the start line again") {
		marks.mark_m[2] = marks.length_m;
		CHECK_FALSE(marks.is_valid());
	}
	SUBCASE("a zero-length lap is not a lap") {
		LapMarks empty = LapMarks::even(0.0, 3);
		CHECK_FALSE(empty.is_valid());
	}
}

TEST_CASE("the first lap is an out lap and cannot become a best lap") {
	// The failure this prevents: a driver leaves the pits, drives half a circuit,
	// crosses the line, and a timer trusting its own zero records a 24-second lap
	// record that nothing can ever beat.
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	CHECK(timer.progress().reason == LapInvalidReason::OutLap);

	// Joining the track halfway round and crossing the line.
	drive(timer, 600.0, length, 25.0, length);
	CHECK(timer.laps_completed() == 1);
	CHECK(timer.last().reason == LapInvalidReason::OutLap);
	CHECK_FALSE(timer.last().is_valid());
	CHECK_FALSE(timer.best().is_valid());
	CHECK(timer.valid_lap_count() == 0);

	// And the lap after it is a real one.
	drive_lap(timer, 25.0, length);
	CHECK(timer.last().is_valid());
	CHECK(timer.valid_lap_count() == 1);
	CHECK(timer.best().time_s == timer.last().time_s);
}

TEST_CASE("a clean lap times to the tick, and its sectors add up to it") {
	const double length = 1200.0;
	const double speed = 25.0; // 1,200 m at 25 m/s is 48 s, GAMEDESIGN 4's pace
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, speed, length); // the out lap
	drive_lap(timer, speed, length);

	const LapRecord lap = timer.last();
	REQUIRE(lap.is_valid());
	CHECK(lap.lap_number == 2);
	CHECK(lap.sector_count == 3);
	// 48 s, to within the one tick the loop can land on either side of the line.
	CHECK(lap.time_s == doctest::Approx(48.0).epsilon(0.001));

	// **The sectors sum to the lap exactly**, not approximately: both sides are an
	// integer number of ticks times one step, so a discrepancy here is a lost or
	// double-counted tick and not a rounding artifact.
	double sum = 0.0;
	for (int i = 0; i < lap.sector_count; ++i) {
		CHECK(lap.sector_s[i] > 0.0);
		sum += lap.sector_s[i];
	}
	CHECK(sum == doctest::Approx(lap.time_s).epsilon(1e-9));
	MESSAGE("48 s lap: sectors " << lap.sector_s[0] << " / " << lap.sector_s[1] << " / "
								 << lap.sector_s[2] << ", total " << lap.time_s);
}

TEST_CASE("a kart parked on the start line does not count laps") {
	// The other half of why a lap is a sequence of crossings: arc length near zero
	// jitters either side of the line, and a wrap test that only looked for a fall
	// would count a lap every time it did.
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	for (int i = 0; i < 600; ++i) {
		const double jitter = (i % 2 == 0) ? 0.05 : length - 0.05;
		CHECK_FALSE(timer.advance(jitter, false));
	}
	CHECK(timer.laps_completed() == 0);
}

TEST_CASE("cutting the track past a checkpoint costs the lap it happened on") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap

	// Round to the second sector, then jump the third mark: the kart appears past
	// 800 m without ever crossing it in order.
	drive(timer, 0.0, 700.0, 25.0, length);
	// A teleport of 400 m in one tick is not a lap — it is under half the circuit —
	// so no lap completes here, and the mark at 800 is simply never consumed.
	CHECK_FALSE(timer.advance(1100.0, false));
	CHECK(timer.progress().reason == LapInvalidReason::MissedMark);
	drive(timer, 1100.0, length, 25.0, length);

	CHECK(timer.laps_completed() == 2);
	CHECK(timer.last().reason == LapInvalidReason::MissedMark);
	CHECK_FALSE(timer.best().is_valid());

	// And the next lap is clean, because a cut costs one lap and not the session.
	drive_lap(timer, 25.0, length);
	CHECK(timer.last().is_valid());
}

TEST_CASE("driving backwards toward the line does not award a lap") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap
	drive(timer, 0.0, 300.0, 25.0, length);

	// Spin and drive back the way you came. Arc length falls the whole way, and each
	// step is small, so nothing here is a discontinuity — it is just a kart going the
	// wrong way, which is a thing that happens after every spin.
	double position = 300.0;
	while (position > 5.0) {
		position -= 15.0 * STEP;
		CHECK_FALSE(timer.advance(position, false));
	}
	CHECK(timer.laps_completed() == 1);
	// **Nothing is wrong with the lap yet, and that is the point.** Going backwards
	// consumes no mark, so the marks ahead are still owed — but the driver has not
	// broken a rule, and a timer that struck the lap out here would be punishing a
	// spin rather than a cut.
	CHECK(timer.progress().reason == LapInvalidReason::Valid);

	// Turning round again and driving the rest of the way crosses every mark in
	// order, so it is a legal lap. A slow one: the spin is paid for in lap time,
	// which is the correct punishment and the only one the sport applies.
	drive(timer, 5.0, length, 25.0, length);
	CHECK(timer.laps_completed() == 2);
	CHECK(timer.last().is_valid());
	CHECK(timer.last().time_s > 48.0);
	MESSAGE("lap with a spin and a reverse: " << timer.last().time_s << " s");
}

TEST_CASE("going off invalidates the lap in progress and nothing else") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap

	// One sector with all four wheels over the line, then a clean rejoin.
	drive(timer, 0.0, 400.0, 25.0, length);
	drive(timer, 400.0, 500.0, 25.0, length, /*off_track=*/true);
	CHECK(timer.progress().reason == LapInvalidReason::OffTrack);
	drive(timer, 500.0, length, 25.0, length);

	CHECK(timer.last().reason == LapInvalidReason::OffTrack);
	CHECK_FALSE(timer.last().is_valid());
	// The time is still measured. A timing screen that loses the kart because it put
	// a wheel on the grass is worse than one that shows a struck-through lap.
	CHECK(lap_time_exists(timer.last().time_s));
	CHECK(timer.invalid_lap_count() == 2); // the out lap and this one

	drive_lap(timer, 25.0, length);
	CHECK(timer.last().is_valid());
}

TEST_CASE("the first reason to spoil a lap is the one reported") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap

	// Off the road first, then a cut while out there — one that skips **every**
	// intermediate mark, from 250 m straight to 950 m.
	drive(timer, 0.0, 250.0, 25.0, length, /*off_track=*/true);
	timer.advance(950.0, false); // jumps the marks at 400 and 800
	CHECK(timer.progress().reason == LapInvalidReason::OffTrack);

	// **Crossing the line now awards nothing at all, and that is the rule rather
	// than an edge case.** A lap needs one witness that the kart went round: a mark
	// consumed in order, or `LAP_TRAVEL_FRACTION` of the circuit actually driven. This
	// kart has neither — it drove 250 m, vanished, reappeared 700 m later and drove
	// another 250 m, which is 500 m of travel on a 1,200 m lap. It did not do a lap,
	// so it does not get one, for the same reason a kart parked on the line collects
	// nothing however many times it wobbles over.
	//
	// The numbers were 300 and 900 here until the arming condition was fixed for a
	// cut over the *first* mark of the lap, which put this case at exactly 600 m of
	// travel — precisely on the half-circuit arm, so it flipped to completing. Moved
	// off the boundary rather than left sitting on it: a test that a rounding change
	// could turn over is testing the boundary, and the boundary has its own case.
	drive(timer, 950.0, length, 25.0, length);
	CHECK(timer.laps_completed() == 1); // still just the out lap

	// It self-heals on the next real lap, which crosses both marks and therefore
	// closes — reporting the *first* thing that spoiled it, which is the off-track
	// excursion and not the cut that came after.
	drive_lap(timer, 25.0, length);
	CHECK(timer.laps_completed() == 2);
	CHECK(timer.last().reason == LapInvalidReason::OffTrack);
	CHECK_FALSE(timer.last().is_valid());
}

TEST_CASE("a pause strikes the lap in flight without swallowing it") {
	// ADR-0052 §4 and mockup plate 8. **The kart is not frozen** — an ADR-0052 pause
	// keeps the world running and gates input at the driver — so the lap keeps timing
	// and simply stops counting.
	//
	// The failure this guards against is the one this class has already had: a lap
	// that can never close. A cut over the first mark once left `next_mark_` at 1
	// while the arming condition only accepted a consumed mark, and the driver got no
	// lap on the screen and no reason. So the assertions here are not just "the reason
	// came out right" — they are that the lap *closes*, that the timer's own count
	// went up, and that the next lap is clean.
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap
	REQUIRE(timer.laps_completed() == 1);

	SUBCASE("struck mid-lap, the lap still runs to the line and closes") {
		drive(timer, 0.0, 500.0, 25.0, length);
		CHECK(timer.progress().reason == LapInvalidReason::Valid);

		timer.strike_paused();
		CHECK(timer.progress().reason == LapInvalidReason::Paused);
		// Nothing else moved. The kart is where it was and the timer is still running.
		CHECK(timer.progress().distance_m == doctest::Approx(500.0).epsilon(0.001));

		const int ticks = drive(timer, 500.0, length, 25.0, length);
		CHECK(ticks > 0);
		CHECK(timer.laps_completed() == 2);
		CHECK(timer.last().reason == LapInvalidReason::Paused);
		CHECK_FALSE(timer.last().is_valid());
		CHECK(timer.last().time_s > 0.0);
		CHECK(timer.invalid_lap_count() == 2); // the out lap and this one
		CHECK(timer.valid_lap_count() == 0);

		// And the next lap is ordinary. A strike that left the marks or the travel
		// counter disturbed would show up here as a lap that never closes.
		drive_lap(timer, 25.0, length);
		CHECK(timer.laps_completed() == 3);
		CHECK(timer.last().is_valid());
		CHECK(timer.valid_lap_count() == 1);
	}
	SUBCASE("struck before the first mark, the lap still closes") {
		// The exact shape of the bug the header argues about, driven at the one place
		// it was reachable: struck at 50 m, before the mark at 400. `strike_paused`
		// touches no mark state at all, so `next_mark_` is still 1 and the *travel*
		// arm is what closes the lap — which is the `||` that fixed the original.
		drive(timer, 0.0, 50.0, 25.0, length);
		timer.strike_paused();
		drive(timer, 50.0, length, 25.0, length);
		CHECK(timer.laps_completed() == 2);
		CHECK(timer.last().reason == LapInvalidReason::Paused);
	}
	SUBCASE("the first reason wins, so going off then pausing reports the excursion") {
		// A driver who put a wheel on the grass and then paused is told they went off,
		// which is the thing they did and the thing they can fix.
		drive(timer, 0.0, 300.0, 25.0, length, true);
		timer.strike_paused();
		CHECK(timer.progress().reason == LapInvalidReason::OffTrack);
		drive(timer, 300.0, length, 25.0, length);
		CHECK(timer.last().reason == LapInvalidReason::OffTrack);
	}
	SUBCASE("striking an already-struck lap twice changes nothing") {
		drive(timer, 0.0, 300.0, 25.0, length);
		timer.strike_paused();
		timer.strike_paused();
		timer.strike_paused();
		drive(timer, 300.0, length, 25.0, length);
		CHECK(timer.laps_completed() == 2);
		CHECK(timer.last().reason == LapInvalidReason::Paused);
	}
	SUBCASE("a struck lap sets no best and no best sector") {
		// The point of striking it. A paused lap that seeded a sector best would feed
		// `optimal_lap_s` a split nobody drove.
		drive(timer, 0.0, 400.0, 25.0, length);
		timer.strike_paused();
		drive(timer, 400.0, length, 25.0, length);
		CHECK_FALSE(timer.best().is_valid());
		CHECK_FALSE(lap_time_exists(timer.best_sector_s(0)));
		CHECK(timer.optimal_lap_s() == LAP_TIME_NONE);
	}
}

TEST_CASE("a respawn kills the lap and re-arms the marks from where the kart landed") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap
	drive(timer, 0.0, 500.0, 25.0, length);

	// Put back on the road at 900 m — past the mark at 800 that was never crossed.
	timer.respawn(900.0);
	CHECK(timer.progress().reason == LapInvalidReason::Respawned);
	drive(timer, 900.0, length, 25.0, length);
	CHECK(timer.last().reason == LapInvalidReason::Respawned);

	// The following lap is clean: the respawn re-armed from 900, so the marks ahead
	// of the line are owed and get crossed normally.
	drive_lap(timer, 25.0, length);
	CHECK(timer.last().is_valid());
}

TEST_CASE("a respawn behind an uncrossed mark still owes it") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap

	// Off at 700, dropped back at 100 — the marks at 400 and 800 are both ahead.
	drive(timer, 0.0, 700.0, 25.0, length);
	timer.respawn(100.0);
	drive(timer, 100.0, length, 25.0, length);
	// Respawned, not MissedMark: the first reason wins, and both are true.
	CHECK(timer.last().reason == LapInvalidReason::Respawned);
	// Crucially it did not award a lap on the way back to 100 — that would be a
	// 600 m fall, half the circuit, which is why the wrap threshold is where it is.
	CHECK(timer.laps_completed() == 2);
}

TEST_CASE("best sectors come only from valid laps, and the optimal lap is not a lap time") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap
	drive_lap(timer, 25.0, length); // 48 s
	const double even_lap = timer.last().time_s;

	// A faster first sector, slower elsewhere, so no lap record but a sector one.
	drive(timer, 0.0, 400.0, 30.0, length);
	drive(timer, 400.0, length, 22.0, length);
	CHECK(timer.last().is_valid());
	CHECK(timer.last().time_s > even_lap);
	CHECK(timer.best().time_s == doctest::Approx(even_lap));
	CHECK(timer.best_sector_s(0) < 400.0 / 25.0);

	const double optimal = timer.optimal_lap_s();
	REQUIRE(lap_time_exists(optimal));
	// The optimal lap is faster than the best lap and is not a lap anybody drove.
	CHECK(optimal < timer.best().time_s);
	MESSAGE("best " << timer.best().time_s << " s, optimal " << optimal << " s");

	// A sector driven quickly on an invalid lap does not count.
	const double before = timer.best_sector_s(1);
	drive(timer, 0.0, 400.0, 25.0, length);
	drive(timer, 400.0, 800.0, 40.0, length, /*off_track=*/true);
	drive(timer, 800.0, length, 25.0, length);
	CHECK_FALSE(timer.last().is_valid());
	CHECK(timer.best_sector_s(1) == doctest::Approx(before));
}

TEST_CASE("a qualifying penalty deletes the fastest lap, not the current one") {
	// The regulation everybody implements wrong, because "invalidate the current
	// lap" looks correct in any test that drives one lap.
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap
	drive_lap(timer, 24.0, length); // slower
	const double slow = timer.last().time_s;
	drive_lap(timer, 26.0, length); // the fastest
	const double fast = timer.last().time_s;
	REQUIRE(fast < slow);
	CHECK(timer.best().time_s == doctest::Approx(fast));
	CHECK(timer.valid_lap_count() == 2);

	const double deleted = timer.invalidate_fastest();
	CHECK(deleted == doctest::Approx(fast));
	CHECK_FALSE(timer.best().is_valid());
	CHECK(timer.best().reason == LapInvalidReason::Deleted);
	CHECK(timer.valid_lap_count() == 1);

	// Nothing to delete twice.
	CHECK(timer.invalidate_fastest() == LAP_TIME_NONE);
}

TEST_CASE("sector deltas are signed against a reference, and absent is not zero") {
	const double length = 1200.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 25.0, length); // out lap
	drive_lap(timer, 25.0, length);
	const LapRecord reference = timer.last();
	REQUIRE(reference.is_valid());

	// Half of sector one, faster than the reference was.
	drive(timer, 0.0, 200.0, 30.0, length);
	const double delta = timer.sector_delta_s(reference);
	CHECK(lap_time_exists(-delta)); // negative, so ahead
	CHECK(delta < 0.0);

	// Against a reference that has no such sector there is no delta, and it is not
	// reported as dead level — a driver reads 0.000 as good news.
	LapRecord empty;
	CHECK(timer.sector_delta_s(empty) == LAP_TIME_NONE);
	CHECK_FALSE(timer.has_sector_delta(empty));
	CHECK(timer.has_sector_delta(reference));

	// **And the predicate is not the value in disguise.** A driver exactly one
	// second up on the reference sector has a delta of -1.0, which is bit-identical
	// to `LAP_TIME_NONE` — so a caller testing the number instead of asking the
	// question hides a real split behind "no comparison". Construct that case.
	LapRecord one_second;
	one_second.sector_count = reference.sector_count;
	for (int i = 0; i < one_second.sector_count; ++i) {
		one_second.sector_s[i] = reference.sector_s[i];
	}
	one_second.time_s = reference.time_s;
	const double elapsed = timer.progress().sector_time_s;
	one_second.sector_s[timer.progress().sector] = elapsed + 1.0;
	CHECK(timer.sector_delta_s(one_second) == doctest::Approx(-1.0));
	CHECK(timer.has_sector_delta(one_second));
}

TEST_CASE("lap timing is a function of ticks, so two runs of the same drive agree exactly") {
	// The property the whole design exists for: no wall clock anywhere, so a lap
	// time is bit-identical across two runs and inside a replay.
	const double length = 1200.0;
	LapTimer a;
	LapTimer b;
	a.begin(three_sectors(length), STEP);
	b.begin(three_sectors(length), STEP);
	for (int lap = 0; lap < 3; ++lap) {
		drive_lap(a, 25.0, length);
		drive_lap(b, 25.0, length);
	}
	CHECK(a.best().time_s == b.best().time_s);
	CHECK(std::memcmp(a.best().sector_s, b.best().sector_s, sizeof(a.best().sector_s)) == 0);

	// And the same drive at a different step gives the same *time*, not the same
	// tick count — 240 Hz is twice the ticks and one lap.
	LapTimer fast;
	fast.begin(three_sectors(length), STEP);
	drive_lap(fast, 25.0, length);
	CHECK(fast.laps_completed() == 1);
}

TEST_CASE("which sessions invalidate a lap, and which delete the fastest one") {
	// Ours in the timed sessions; nothing in a race, because the regulations attach
	// no penalty to going off alone and a race is not scored per lap anyway.
	CHECK(session_invalidates_laps(SessionType::Practice));
	CHECK(session_invalidates_laps(SessionType::Qualifying));
	CHECK_FALSE(session_invalidates_laps(SessionType::Heat));
	CHECK_FALSE(session_invalidates_laps(SessionType::SuperHeat));
	CHECK_FALSE(session_invalidates_laps(SessionType::Final));

	// The fastest-lap deletion is qualifying's alone, and it is the FIA's rule.
	CHECK(session_deletes_fastest_lap(SessionType::Qualifying));
	CHECK_FALSE(session_deletes_fastest_lap(SessionType::Practice));
	CHECK_FALSE(session_deletes_fastest_lap(SessionType::Final));

	for (int i = 0; i < LAP_INVALID_REASON_COUNT; ++i) {
		const char *name = lap_invalid_reason_name(static_cast<LapInvalidReason>(i));
		CHECK(std::strcmp(name, "invalid") != 0);
		for (int j = i + 1; j < LAP_INVALID_REASON_COUNT; ++j) {
			CHECK(std::strcmp(name, lap_invalid_reason_name(
											static_cast<LapInvalidReason>(j))) != 0);
		}
	}
}

TEST_CASE("the test track's own length times out to something a driver would recognize") {
	// `scripts/track/track_layout.gd` builds a 1,030 m loop. At the pace the
	// drive_probe sweep settles on through Turn 4 — call it 22 m/s average, since
	// Turn 2's 11 m hairpin is #137 and costs most of a straight — that is a lap in
	// the mid-40s, which is what M3c's Practice will be showing on screen.
	const double length = 1030.0;
	LapTimer timer;
	timer.begin(three_sectors(length), STEP);
	drive_lap(timer, 22.0, length); // out lap
	drive_lap(timer, 22.0, length);
	REQUIRE(timer.best().is_valid());
	CHECK(timer.best().time_s == doctest::Approx(46.8).epsilon(0.01));
	MESSAGE("1,030 m at 22 m/s: " << timer.best().time_s << " s");
}

// --- authored marks: two lists, one ordered set ---------------------------------
//
// ADR-0046 puts sector marks and checkpoints in `track.json` as separate lists.
// `from_stations` merges them, and everything below is a way that merge can be
// wrong while still looking plausible on a timing screen.

namespace {

// Valdirone Nuova's forward layout, from `data/tracks/valdirone_nuova.track.json`.
// Two authored sector marks and fourteen evenly spaced checkpoints, the first of
// which is the start line.
constexpr double VALDIRONE_LENGTH = 1375.119417;
constexpr double VALDIRONE_SECTORS[] = { 524.0, 902.0 };
constexpr double VALDIRONE_CHECKPOINTS[] = {
	0.0, 98.222816, 196.445631, 294.668447, 392.891262, 491.114078, 589.336893,
	687.559709, 785.782524, 884.00534, 982.228155, 1080.450971, 1178.673786,
	1276.896602,
};

LapMarks valdirone_forward() {
	return LapMarks::from_stations(VALDIRONE_LENGTH, VALDIRONE_SECTORS, 2,
			VALDIRONE_CHECKPOINTS, 14);
}

} // namespace

TEST_CASE("a circuit's two authored lists merge into one ordered set of marks") {
	const LapMarks marks = valdirone_forward();
	REQUIRE(marks.is_valid());

	// 1 start line + 13 checkpoints past it + 2 sector marks. The fourteenth
	// checkpoint is at 0.0 and merges into the line rather than becoming a mark on
	// top of it — which would be a sector of zero length, and `is_valid` would have
	// refused the whole layout.
	CHECK(marks.mark_count == 16);
	CHECK(marks.mark_m[0] == 0.0);
	for (int i = 1; i < marks.mark_count; ++i) {
		CHECK(marks.mark_m[i] > marks.mark_m[i - 1]);
	}

	// **Three sectors and not sixteen.** This is the whole point of the flag: every
	// mark has to be crossed in order for the anti-cut check, and only the two
	// authored splits plus the line may produce a time on the screen.
	CHECK(marks.sector_count() == 3);
	CHECK(marks.sector_length_m(0) == doctest::Approx(524.0));
	CHECK(marks.sector_length_m(1) == doctest::Approx(902.0 - 524.0));
	CHECK(marks.sector_length_m(2) == doctest::Approx(VALDIRONE_LENGTH - 902.0));
	CHECK(marks.sector_length_m(0) + marks.sector_length_m(1) + marks.sector_length_m(2)
			== doctest::Approx(VALDIRONE_LENGTH));

	// The sector marks kept their authored stations exactly, rather than being
	// rounded into a neighboring checkpoint.
	CHECK(marks.mark_m[marks.mark_of_sector(1)] == 524.0);
	CHECK(marks.mark_m[marks.mark_of_sector(2)] == 902.0);

	// And the checkpoints between them are marks that start no sector.
	const int before_first_split = marks.mark_of_sector(1) - 1;
	CHECK(marks.checkpoint_only[before_first_split]);
	CHECK(marks.sector_of_mark(before_first_split) == 0);
	CHECK(marks.sector_of_mark(marks.mark_of_sector(1)) == 1);
	CHECK(marks.sector_of_mark(marks.mark_count - 1) == 2);

	MESSAGE("Valdirone forward: " << marks.mark_count << " marks, " << marks.sector_count()
								  << " sectors of " << marks.sector_length_m(0) << " / "
								  << marks.sector_length_m(1) << " / "
								  << marks.sector_length_m(2) << " m");
}

TEST_CASE("the reverse layout is its own partition of the same road") {
	// 473 and 862 rather than 524 and 902. A layout that reused the forward marks
	// would put a split in a different corner of the same circuit and quietly report
	// sector times nobody can compare against anything.
	static constexpr double reverse_sectors[] = { 473.0, 862.0 };
	const LapMarks marks = LapMarks::from_stations(VALDIRONE_LENGTH, reverse_sectors, 2,
			VALDIRONE_CHECKPOINTS, 14);
	REQUIRE(marks.is_valid());
	CHECK(marks.mark_count == 16);
	CHECK(marks.sector_count() == 3);
	CHECK(marks.sector_length_m(0) == doctest::Approx(473.0));
	CHECK(marks.sector_length_m(1) == doctest::Approx(862.0 - 473.0));
}

TEST_CASE("a checkpoint that lands on a sector mark is absorbed by it") {
	static constexpr double sectors[] = { 400.0, 800.0 };

	SUBCASE("exactly on it") {
		static constexpr double checkpoints[] = { 0.0, 400.0, 800.0 };
		const LapMarks marks = LapMarks::from_stations(1200.0, sectors, 2, checkpoints, 3);
		REQUIRE(marks.is_valid());
		CHECK(marks.mark_count == 3);
		CHECK(marks.sector_count() == 3);
	}
	SUBCASE("a tenth of a millimeter short of it, which is what float rounding does") {
		// The trap `LAP_MARK_MERGE_M` exists for, and the same one the schema's
		// coincident-control-point rule exists for: two authored stations that mean the
		// same place arrive separated by 0.0001 m, and the sector between them divides
		// by nearly zero everywhere downstream.
		static constexpr double checkpoints[] = { 0.0, 399.9999, 800.0001 };
		const LapMarks marks = LapMarks::from_stations(1200.0, sectors, 2, checkpoints, 3);
		REQUIRE(marks.is_valid());
		CHECK(marks.mark_count == 3);
		CHECK(marks.sector_count() == 3);
		// **The sector mark's station wins, not the checkpoint's.** A split a hair off
		// the design's own number is a split nobody authored.
		CHECK(marks.mark_m[1] == 400.0);
		CHECK(marks.mark_m[2] == 800.0);
	}
}

TEST_CASE("from_stations refuses rather than clamping") {
	// A clamped set is a cut detector with holes in it: the checkpoints that were
	// dropped are exactly the ones a driver could then skip for free.
	SUBCASE("more marks than there is room for") {
		double many[LAP_MAX_MARKS + 4] = {};
		for (int i = 0; i < LAP_MAX_MARKS + 4; ++i) {
			many[i] = 10.0 * static_cast<double>(i + 1);
		}
		CHECK_FALSE(LapMarks::from_stations(1200.0, nullptr, 0, many, LAP_MAX_MARKS + 4)
							.is_valid());
	}
	SUBCASE("a station at or past the length is the start line again") {
		static constexpr double past[] = { 1200.0 };
		CHECK_FALSE(LapMarks::from_stations(1200.0, past, 1, nullptr, 0).is_valid());
	}
	SUBCASE("a negative station is not on the lap") {
		static constexpr double behind[] = { -1.0 };
		CHECK_FALSE(LapMarks::from_stations(1200.0, behind, 1, nullptr, 0).is_valid());
	}
	SUBCASE("more sectors than a lap record can hold") {
		// `LAP_MAX_SECTORS` splits plus the start line is one sector too many. Refused
		// here rather than dropped by the bounds guard in `complete_lap`, which is what
		// used to happen: the splits past the eighth vanished and the ones that remained
		// did not add up to the lap time.
		double splits[LAP_MAX_SECTORS] = {};
		for (int i = 0; i < LAP_MAX_SECTORS; ++i) {
			splits[i] = 100.0 * static_cast<double>(i + 1);
		}
		CHECK_FALSE(LapMarks::from_stations(1200.0, splits, LAP_MAX_SECTORS, nullptr, 0)
							.is_valid());
		CHECK(LapMarks::from_stations(1200.0, splits, LAP_MAX_SECTORS - 1, nullptr, 0)
						.is_valid());
	}
	SUBCASE("a layout whose first mark is not a sector boundary") {
		LapMarks marks = LapMarks::even(1200.0, 3);
		marks.checkpoint_only[0] = true;
		CHECK_FALSE(marks.is_valid());
	}
}

TEST_CASE("a checkpoint is crossed for the ordering and produces no split") {
	// The failure being ruled out: the driver sees three sector times that do not add
	// up to the lap, because the checkpoints in between reset the sector clock.
	const LapMarks marks = valdirone_forward();
	REQUIRE(marks.is_valid());
	LapTimer timer;
	timer.begin(marks, STEP);

	drive_lap(timer, 22.0, VALDIRONE_LENGTH); // out lap
	drive_lap(timer, 22.0, VALDIRONE_LENGTH);

	const LapRecord &lap = timer.last();
	REQUIRE(lap.is_valid());
	CHECK(lap.sector_count == 3);
	const double sum = lap.sector_s[0] + lap.sector_s[1] + lap.sector_s[2];
	// To the tick, because both sides are tick counts times the same step.
	CHECK(sum == doctest::Approx(lap.time_s).epsilon(1e-12));
	// And the splits are in the ratio of the sector lengths, which is what a constant
	// speed means. Checked against the authored stations rather than against thirds.
	CHECK(lap.sector_s[0] / lap.time_s == doctest::Approx(524.0 / VALDIRONE_LENGTH).epsilon(0.01));
	CHECK(lap.sector_s[1] / lap.time_s
			== doctest::Approx((902.0 - 524.0) / VALDIRONE_LENGTH).epsilon(0.01));
	MESSAGE("Valdirone at 22 m/s: " << lap.time_s << " s, splits " << lap.sector_s[0] << " / "
									<< lap.sector_s[1] << " / " << lap.sector_s[2]);
}

TEST_CASE("skipping a checkpoint-only mark costs the lap just as a sector mark would") {
	// A cut that misses no *split* is still a cut. Checkpoints are the anti-cut
	// resolution and a timer that only owed sector marks would let a driver skip 380 m
	// of circuit between two of them for free.
	const LapMarks marks = valdirone_forward();
	LapTimer timer;
	timer.begin(marks, STEP);
	drive_lap(timer, 22.0, VALDIRONE_LENGTH); // out lap

	// Round to 600 m, which is past the first split at 524 and past the checkpoint at
	// 589.3, then appear at 900 m — under half a lap, so it is a discontinuity rather
	// than a completed lap, and the checkpoints at 687.6 and 785.8 stay owed. Both are
	// checkpoint-only marks; the split at 902 is still ahead and is crossed normally.
	drive(timer, 0.0, 600.0, 22.0, VALDIRONE_LENGTH);
	CHECK(timer.progress().reason == LapInvalidReason::Valid);
	CHECK_FALSE(timer.advance(900.0, false));
	CHECK(timer.progress().reason == LapInvalidReason::MissedMark);

	drive(timer, 900.0, VALDIRONE_LENGTH, 22.0, VALDIRONE_LENGTH);
	CHECK(timer.laps_completed() == 2);
	CHECK(timer.last().reason == LapInvalidReason::MissedMark);
	CHECK_FALSE(timer.best().is_valid());

	// One lap and not the session.
	drive_lap(timer, 22.0, VALDIRONE_LENGTH);
	CHECK(timer.last().is_valid());
}

TEST_CASE("a cut over the first mark of the lap invalidates it rather than swallowing it") {
	// Found by `circuit_probe.gd --case=timing`, and the failure was silent: the
	// arming condition was "a mark has been consumed" whenever the layout had marks,
	// the marks a cut skips deliberately stay owed, so a cut over the *first* mark
	// left `next_mark_` at 1 for the rest of the lap and the lap never closed. The
	// driver got no lap and no reason. `lap_timing.h` says in its own words that a cut
	// must still complete its lap so the missed mark can strike it out.
	const LapMarks marks = valdirone_forward();
	LapTimer timer;
	timer.begin(marks, STEP);
	drive_lap(timer, 22.0, VALDIRONE_LENGTH); // out lap
	REQUIRE(timer.laps_completed() == 1);

	// The first mark past the line is the checkpoint at 98.2 m. Jump it.
	drive(timer, 0.0, 78.0, 22.0, VALDIRONE_LENGTH);
	CHECK_FALSE(timer.advance(118.0, false));
	CHECK(timer.progress().reason == LapInvalidReason::MissedMark);
	drive(timer, 118.0, VALDIRONE_LENGTH, 22.0, VALDIRONE_LENGTH);

	// The lap exists, is on the screen, and says why it does not count.
	CHECK(timer.laps_completed() == 2);
	CHECK(timer.last().reason == LapInvalidReason::MissedMark);
	CHECK(lap_time_exists(timer.last().time_s));
	CHECK_FALSE(timer.best().is_valid());

	// And the next lap is clean, because the mark is owed again from the line.
	drive_lap(timer, 22.0, VALDIRONE_LENGTH);
	CHECK(timer.last().is_valid());
}

TEST_CASE("the travel arm does not let a parked or cutting kart claim a lap") {
	// The other half of the change above: `went_round` now accepts *either* witness,
	// so the two cases that must still produce nothing are re-checked against the
	// looser condition rather than assumed to be unaffected.
	const double length = 1200.0;

	SUBCASE("a kart jittering across the line banks no travel") {
		LapTimer timer;
		timer.begin(three_sectors(length), STEP);
		double position = 1.0;
		for (int i = 0; i < 400; ++i) {
			position = (position > 0.5) ? length - 0.5 : 1.0;
			timer.advance(position, false);
		}
		CHECK(timer.laps_completed() == 0);
	}
	SUBCASE("a kart that cuts most of the circuit banks none of what it flew over") {
		LapTimer timer;
		timer.begin(three_sectors(length), STEP);
		drive_lap(timer, 25.0, length); // out lap
		// 100 m driven, then a jump of 1,000 m, then 100 m driven. 200 m of travel on
		// a 1,200 m lap is under the half-circuit arm, and no mark was consumed in
		// order, so the line crossing produces nothing.
		drive(timer, 0.0, 100.0, 25.0, length);
		CHECK_FALSE(timer.advance(1100.0, false));
		const int laps = drive(timer, 1100.0, length, 25.0, length);
		CHECK(laps == 0);
		CHECK(timer.laps_completed() == 1);
	}
	SUBCASE("and the arm is at half a circuit, measured from both sides of it") {
		// The boundary itself, since two other cases sit near it. Marks at 0, 400 and
		// 800; the cut starts before the first of them and lands past the last, so no
		// mark is ever consumed in order and travel is the only witness there is.
		for (const double driven : { 590.0, 610.0 }) {
			LapTimer timer;
			timer.begin(three_sectors(length), STEP);
			drive_lap(timer, 25.0, length); // out lap
			const double half = driven * 0.5;
			drive(timer, 0.0, half, 25.0, length);
			timer.advance(length - half, false);
			drive(timer, length - half, length, 25.0, length);
			// 590 m of travel is under 600 and produces nothing; 610 m is over it and
			// produces a lap that exists and says it was cut.
			CHECK(timer.laps_completed() == (driven > 600.0 ? 2 : 1));
			if (driven > 600.0) {
				CHECK(timer.last().reason == LapInvalidReason::MissedMark);
			}
		}
	}
}

TEST_CASE("armed mid-lap, the timer reports a sector and not a mark index") {
	// The bug this is here for reported "sector 11 of 3": `arm_marks_from` set the
	// sector to `next_mark_ - 1`, which is a mark index, and with fourteen checkpoints
	// in the list those two numbers stop being the same one.
	const LapMarks marks = valdirone_forward();
	LapTimer timer;
	timer.begin(marks, STEP);

	// A kart set down at 1,100 m: past the second split at 902, so sector 2 of 3.
	timer.advance(1100.0, false);
	CHECK(timer.progress().sector == 2);

	SUBCASE("and a respawn re-arms it the same way") {
		timer.respawn(600.0); // past 524, before 902
		CHECK(timer.progress().sector == 1);
		timer.respawn(10.0); // before the first split
		CHECK(timer.progress().sector == 0);
	}
}
