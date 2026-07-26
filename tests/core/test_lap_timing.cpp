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
	// intermediate mark, from 300 m straight to 900 m.
	drive(timer, 0.0, 300.0, 25.0, length, /*off_track=*/true);
	timer.advance(900.0, false); // jumps the marks at 400 and 800
	CHECK(timer.progress().reason == LapInvalidReason::OffTrack);

	// **Crossing the line now awards nothing at all, and that is the rule rather
	// than an edge case.** A lap needs one witness that the kart went round, and the
	// only witness available is a mark consumed in order. This kart has none: it drove
	// 300 m, vanished, and reappeared 600 m later. It did not do a lap, so it does not
	// get one — the same reasoning that stops a kart parked on the line collecting a
	// lap per wobble, since neither has a mark to show.
	drive(timer, 900.0, length, 25.0, length);
	CHECK(timer.laps_completed() == 1); // still just the out lap

	// It self-heals on the next real lap, which crosses both marks and therefore
	// closes — reporting the *first* thing that spoiled it, which is the off-track
	// excursion and not the cut that came after.
	drive_lap(timer, 25.0, length);
	CHECK(timer.laps_completed() == 2);
	CHECK(timer.last().reason == LapInvalidReason::OffTrack);
	CHECK_FALSE(timer.last().is_valid());
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
