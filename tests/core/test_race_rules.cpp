#include "doctest.h"

#include "core/race_rules.h"

#include <cstring>

// The race format, as arithmetic. ADR-0043.
//
// The first test in this file is the reason the file exists. Everything after it is
// ordinary, and says so.
//
// No `doctest::Approx` on any points value anywhere below. Points are integers in
// half units by design, so they are compared exactly; and `Approx(x).epsilon(e)` is
// not a relative tolerance — it compares against `e * (1.0 + max(|a|,|b|))`, which
// on a value of 0.03 makes `epsilon(0.06)` a tolerance of 0.062. Where a double is
// unavoidable below, the comparison is written out.

using namespace kart::core;

namespace {

// A race classification in a given finishing order, everybody at full distance.
// `order[0]` finished first.
Classification full_distance_race(SessionType type, const int *order, int count) {
	Classification classification = Classification::of(type);
	const int laps = scheduled_laps(classification.scheduled_distance_m, 1200.0);
	for (int i = 0; i < count; ++i) {
		DriverResult result;
		result.driver_id = order[i];
		result.position = i + 1;
		result.laps_completed = laps;
		result.distance_m = classification.scheduled_distance_m;
		result.best_lap_s = 48.0 + 0.1 * static_cast<double>(i);
		classification.add(result);
	}
	return classification;
}

// A qualifying classification from lap times, positions assigned by the module.
Classification qualifying_from_laps(const int *ids, const double *laps, int count) {
	Classification classification = Classification::of(SessionType::Qualifying);
	for (int i = 0; i < count; ++i) {
		DriverResult result;
		result.driver_id = ids[i];
		result.laps_completed = 3;
		result.best_lap_s = laps[i];
		classification.add(result);
	}
	assign_positions_by_best_lap(classification);
	return classification;
}

} // namespace

// --- the point of the module ---------------------------------------------------

TEST_CASE("the four points scales are pairwise distinct") {
	// **This is the highest-value test in the module and it is meant to look
	// strange.** ADR-0043: the defect it catches is a copy-paste that produces
	// working code. Three of the four scales begin with 50 or 25, so an
	// implementation that used one array where it meant another compiles, runs,
	// orders a grid, fills a standings table, and is wrong forever — because unlike
	// a wrong tire curve there is no render to disagree with it. Nobody plays a
	// championship and notices that 8th place paid 30 instead of 46.
	//
	// So the scales are asserted to be four different objects, place by place.
	for (int a = 0; a < POINTS_SCALE_COUNT; ++a) {
		for (int b = a + 1; b < POINTS_SCALE_COUNT; ++b) {
			const int *left = points_scale_values(static_cast<PointsScale>(a));
			const int *right = points_scale_values(static_cast<PointsScale>(b));
			REQUIRE(left != nullptr);
			REQUIRE(right != nullptr);
			// Not the same array, and not an element-wise copy of it.
			CHECK(left != right);
			bool identical = true;
			for (int place = 0; place < POINTS_SCALE_PLACES; ++place) {
				identical = identical && left[place] == right[place];
			}
			CHECK_FALSE(identical);
		}
	}

	// And the values themselves, restated from GAMEDESIGN.md §4's table rather than
	// read out of the header, so that editing the header cannot quietly edit the
	// test that guards it.
	const int heat[POINTS_SCALE_PLACES] = { 50, 44, 41, 38, 36, 34, 32, 30 };
	const int super_heat[POINTS_SCALE_PLACES] = { 90, 80, 72, 66, 60, 54, 50, 46 };
	const int championship_heat[POINTS_SCALE_PLACES] = { 25, 22, 19, 17, 15, 13, 11, 9 };
	const int championship_final[POINTS_SCALE_PLACES] = { 50, 44, 38, 34, 30, 26, 22, 18 };
	for (int place = 1; place <= POINTS_SCALE_PLACES; ++place) {
		CHECK(points_for(PointsScale::HeatPosition, place) == heat[place - 1]);
		CHECK(points_for(PointsScale::SuperHeatPosition, place) == super_heat[place - 1]);
		CHECK(points_for(PointsScale::ChampionshipHeat, place) ==
				championship_heat[place - 1]);
		CHECK(points_for(PointsScale::ChampionshipFinal, place) ==
				championship_final[place - 1]);
	}

	// Every scale is strictly decreasing. A scale that plateaus would make two
	// finishing positions worth the same, which none of the four real ones does over
	// its first eight places.
	for (int s = 0; s < POINTS_SCALE_COUNT; ++s) {
		for (int place = 2; place <= POINTS_SCALE_PLACES; ++place) {
			CHECK(points_for(static_cast<PointsScale>(s), place) <
					points_for(static_cast<PointsScale>(s), place - 1));
		}
	}
}

TEST_CASE("the Final scale is exactly twice the heat scale, which is a trap not a bug") {
	// Pinned because it is true of G2 Art. 19 and it defeats two obvious tests.
	//
	// An implementation that wrote `2 * CHAMPIONSHIP_HEAT_POINTS` for the Final
	// would pass every assertion an 8-kart field can make, so the distinctness test
	// above proves less than it looks like it proves. And a **half-credit Final**
	// award is numerically identical to a **full-credit heat** award, which is why
	// `RoundAward` carries the Final position instead of `standings.h` recovering it
	// from the points.
	for (int place = 1; place <= POINTS_SCALE_PLACES; ++place) {
		CHECK(points_for(PointsScale::ChampionshipFinal, place) ==
				2 * points_for(PointsScale::ChampionshipHeat, place));
	}

	// Half credit on the Final, full credit on the heats, same number.
	for (int place = 1; place <= POINTS_SCALE_PLACES; ++place) {
		const HalfPoints final_half = credited_half_points(
				points_for(PointsScale::ChampionshipFinal, place), DistanceCredit::Half);
		const HalfPoints heat_full = credited_half_points(
				points_for(PointsScale::ChampionshipHeat, place), DistanceCredit::Full);
		CHECK(final_half == heat_full);
	}

	// The relation breaks at 10th place in the real scales — 10 against 6 — which is
	// why it must not be turned into a formula. Recorded as a comment because both
	// scales are truncated at 8 here; `docs/REFERENCES.md` holds the full ones.
	CHECK(POINTS_SCALE_PLACES == 8);
}

TEST_CASE("points_for is total, and the truncation to 8 places is visible") {
	// Not classified, and out past the end of the truncated scale. Both score
	// nothing rather than reading off the array.
	for (int s = 0; s < POINTS_SCALE_COUNT; ++s) {
		const PointsScale scale = static_cast<PointsScale>(s);
		CHECK(points_for(scale, 0) == 0);
		CHECK(points_for(scale, -1) == 0);
		CHECK(points_for(scale, POINTS_SCALE_PLACES + 1) == 0);
		CHECK(points_for(scale, 36) == 0);
	}

	// The truncation is OURS, and this is the guard on it: the design's eight karts
	// are covered, a field of nine is not, and a caller can find that out before a
	// standings table does. GAMEDESIGN.md §6 sets the field from an audio
	// measurement that is expected to move.
	CHECK(points_scale_covers(8));
	CHECK(points_scale_covers(2));
	CHECK_FALSE(points_scale_covers(9));
	CHECK_FALSE(points_scale_covers(SESSION_MAX_ENTRIES));

	CHECK(std::strcmp(points_scale_name(static_cast<PointsScale>(99)), "invalid") == 0);
	CHECK(points_scale_values(static_cast<PointsScale>(99)) == nullptr);
	for (int s = 0; s < POINTS_SCALE_COUNT; ++s) {
		CHECK(std::strcmp(points_scale_name(static_cast<PointsScale>(s)), "invalid") != 0);
	}
}

// --- part distance ------------------------------------------------------------

TEST_CASE("part distance is zero under 2 laps, half under 75%, full at or above") {
	// G2 Art. 19. The scheduled distance is the compressed Heat: 15 km at a quarter.
	const double scheduled = Classification::of(SessionType::Heat).scheduled_distance_m;
	REQUIRE(scheduled == 3750.0);
	const double boundary = 0.75 * scheduled; // 2,812.5 m

	// The lap floor comes first and it is not implied by the fraction. One lap of a
	// four-lap race is 25% of the distance, which the fraction alone would score at
	// half; Art. 19 scores it at nothing.
	CHECK(distance_credit(0, 0.0, scheduled) == DistanceCredit::None);
	CHECK(distance_credit(1, scheduled, scheduled) == DistanceCredit::None);
	CHECK(distance_credit(MIN_CLASSIFIED_LAPS - 1, boundary, scheduled) ==
			DistanceCredit::None);

	// Past the lap floor, the distance fraction decides. The boundary itself is
	// full: "75% or more".
	CHECK(distance_credit(2, 0.0, scheduled) == DistanceCredit::Half);
	CHECK(distance_credit(2, boundary - 0.001, scheduled) == DistanceCredit::Half);
	CHECK(distance_credit(2, boundary, scheduled) == DistanceCredit::Full);
	CHECK(distance_credit(4, scheduled, scheduled) == DistanceCredit::Full);
	// Over the scheduled distance, which happens because a race ends on a full lap.
	CHECK(distance_credit(4, scheduled + 400.0, scheduled) == DistanceCredit::Full);

	// A session with no scheduled distance cannot be part-scored, and the lap floor
	// still applies. Practice and Qualifying Practice, per `scheduled_limit()`.
	CHECK(distance_credit(3, 100.0, 0.0) == DistanceCredit::Full);
	CHECK(distance_credit(1, 100.0, 0.0) == DistanceCredit::None);

	for (int c = 0; c < DISTANCE_CREDIT_COUNT; ++c) {
		CHECK(std::strcmp(distance_credit_name(static_cast<DistanceCredit>(c)),
					  "invalid") != 0);
	}
	CHECK(std::strcmp(distance_credit_name(static_cast<DistanceCredit>(9)), "invalid") == 0);
}

TEST_CASE("half of an odd score keeps its half, which is why points are integers") {
	// 25 is the first place on the championship heat scale and it is odd, as are 19,
	// 17, 15, 13, 11 and 9. **Seven of the eight places on that scale halve to a
	// fraction** — 22 is the only even one — so a whole-integer points type would
	// have to round seven times per round per part-scored driver, and every rounding
	// rule for that is invented. The Final scale is the opposite case: every value on
	// it is even, because it is exactly twice this one.
	CHECK(credited_half_points(25, DistanceCredit::Full) == 50);
	CHECK(credited_half_points(25, DistanceCredit::Half) == 25);
	CHECK(credited_half_points(25, DistanceCredit::None) == 0);
	CHECK(half_points_to_points(credited_half_points(25, DistanceCredit::Half)) == 12.5);
	CHECK(half_points_to_points(half_points_of(50)) == 50.0);

	int odd_places = 0;
	int odd_final_places = 0;
	for (int place = 1; place <= POINTS_SCALE_PLACES; ++place) {
		odd_places += points_for(PointsScale::ChampionshipHeat, place) % 2 != 0 ? 1 : 0;
		odd_final_places +=
				points_for(PointsScale::ChampionshipFinal, place) % 2 != 0 ? 1 : 0;
	}
	CHECK(odd_places == 7);
	CHECK(odd_final_places == 0);
}

// --- the grid chain -----------------------------------------------------------

TEST_CASE("qualifying orders itself by best lap, and no lap is not a slow lap") {
	const int ids[5] = { 101, 102, 103, 104, 105 };
	// 104 is fastest, 101 second; 103 never set a time.
	const double laps[5] = { 48.20, 48.55, 0.0, 48.11, 48.90 };
	const Classification qualifying = qualifying_from_laps(ids, laps, 5);

	CHECK(qualifying.position_of(104) == 1);
	CHECK(qualifying.position_of(101) == 2);
	CHECK(qualifying.position_of(102) == 3);
	CHECK(qualifying.position_of(105) == 4);
	// Unclassified, not 5th. A kart that never crossed the line has no lap.
	CHECK(qualifying.position_of(103) == 0);
	CHECK(fastest_lap_driver(qualifying) == 104);

	// The Heat grid is that classification, and it still seats the driver with no
	// time — at the back, because a grid has to seat everybody entered.
	const Order grid = heat_grid(qualifying);
	CHECK(grid.count == 5);
	CHECK(grid.at(1) == 104);
	CHECK(grid.at(5) == 103);
	CHECK(grid.position_of(101) == 2);
	CHECK(grid.at(0) == DRIVER_NONE);
	CHECK(grid.at(6) == DRIVER_NONE);
	CHECK(grid.position_of(999) == 0);
}

TEST_CASE("the grid chain is four different links, each off the right classification") {
	const int ids[4] = { 101, 102, 103, 104 };
	const double laps[4] = { 48.1, 48.2, 48.3, 48.4 };
	const Classification qualifying = qualifying_from_laps(ids, laps, 4);

	// Heat finish reverses qualifying entirely.
	const int heat_order[4] = { 104, 103, 102, 101 };
	const Classification heat = full_distance_race(SessionType::Heat, heat_order, 4);

	// Link 2: the Super Heat grid is the heats' position points, so it follows the
	// heat result and not qualifying.
	const Order super_heat_grid = intermediate_classification(&heat, 1, qualifying);
	CHECK(super_heat_grid.at(1) == 104);
	CHECK(super_heat_grid.at(4) == 101);

	// Link 3: the Final grid is the Super Heat's position points.
	const int super_order[4] = { 102, 101, 104, 103 };
	const Classification super_heat =
			full_distance_race(SessionType::SuperHeat, super_order, 4);
	const Order grid = final_grid(super_heat, qualifying);
	CHECK(grid.at(1) == 102);
	CHECK(grid.at(2) == 101);
	CHECK(grid.at(3) == 104);
	CHECK(grid.at(4) == 103);

	// The two position scales are genuinely different numbers on the same result.
	CHECK(heat_position_tally(&heat, 1).points_of(104) == 50);
	CHECK(super_heat_position_tally(super_heat).points_of(102) == 90);
	CHECK(heat_position_tally(&heat, 1).points_of(101) == 38);
	CHECK(super_heat_position_tally(super_heat).points_of(103) == 66);

	// And the chain as a predicate agrees with the chain as functions.
	CHECK_FALSE(session_sets_next_grid(SessionType::Practice));
	CHECK(session_sets_next_grid(SessionType::Qualifying));
	CHECK(session_sets_next_grid(SessionType::Heat));
	CHECK(session_sets_next_grid(SessionType::SuperHeat));
	CHECK_FALSE(session_sets_next_grid(SessionType::Final));
}

TEST_CASE("the heats aggregate, because the rule is a sum and the heat count is data") {
	// GAMEDESIGN.md §4 runs one heat. The FIA runs three for Seniors and adds their
	// position points. This checks the sum, so that restoring the real structure is
	// content and not a code change.
	const int ids[3] = { 101, 102, 103 };
	const double laps[3] = { 48.1, 48.2, 48.3 };
	const Classification qualifying = qualifying_from_laps(ids, laps, 3);

	Classification heats[3];
	const int a[3] = { 101, 102, 103 };
	const int b[3] = { 103, 101, 102 };
	const int c[3] = { 102, 103, 101 };
	heats[0] = full_distance_race(SessionType::Heat, a, 3);
	heats[1] = full_distance_race(SessionType::Heat, b, 3);
	heats[2] = full_distance_race(SessionType::Heat, c, 3);

	// One win, one second, one third each: 50 + 44 + 41 = 135 for everybody.
	const PointsTally tally = heat_position_tally(heats, 3);
	CHECK(tally.count == 3);
	CHECK(tally.points_of(101) == 135);
	CHECK(tally.points_of(102) == 135);
	CHECK(tally.points_of(103) == 135);

	// Dead level, so the whole intermediate classification is the tie-break, which
	// is the Qualifying Practice classification. G2, at every stage.
	const Order intermediate = intermediate_classification(heats, 3, qualifying);
	CHECK(intermediate.at(1) == 101);
	CHECK(intermediate.at(2) == 102);
	CHECK(intermediate.at(3) == 103);

	// A null heat list is a tally of nobody rather than a crash.
	CHECK(heat_position_tally(nullptr, 3).count == 0);
	CHECK(heat_position_tally(heats, 0).count == 0);
}

TEST_CASE("a tie on points resolves to qualifying at every stage") {
	// Two drivers level on heat points, opposite ways round in qualifying, and the
	// intermediate classification follows qualifying both times. The same comparator
	// serves the Super Heat grid and the Final grid, so proving it once on a
	// constructed tie proves the rule.
	const int ids[2] = { 201, 202 };

	{
		const double laps[2] = { 47.9, 48.4 }; // 201 quicker
		const Classification qualifying = qualifying_from_laps(ids, laps, 2);
		PointsTally tally;
		tally.add(202, 36);
		tally.add(201, 36);
		const Order order = order_tally(tally, qualifying);
		CHECK(order.at(1) == 201);
		CHECK(order.at(2) == 202);
	}
	{
		const double laps[2] = { 48.4, 47.9 }; // 202 quicker
		const Classification qualifying = qualifying_from_laps(ids, laps, 2);
		PointsTally tally;
		tally.add(202, 36);
		tally.add(201, 36);
		const Order order = order_tally(tally, qualifying);
		CHECK(order.at(1) == 202);
		CHECK(order.at(2) == 201);
	}

	// Neither driver set a qualifying time: the regulation has run out of answers
	// and the OURS fall-back keeps the ordering total, in order of first appearance
	// in the tally.
	{
		const double laps[2] = { 0.0, 0.0 };
		const Classification qualifying = qualifying_from_laps(ids, laps, 2);
		PointsTally tally;
		tally.add(202, 36);
		tally.add(201, 36);
		const Order order = order_tally(tally, qualifying);
		CHECK(order.at(1) == 202);
		CHECK(order.at(2) == 201);
	}

	// A driver absent from qualifying sorts behind one who qualified, on a points
	// tie. `tie_break_key` is what makes position 0 sort last rather than at pole.
	{
		const double laps[1] = { 48.0 };
		const int only[1] = { 201 };
		const Classification qualifying = qualifying_from_laps(only, laps, 1);
		CHECK(tie_break_key(qualifying, 201) == 1);
		CHECK(tie_break_key(qualifying, 202) > RULES_MAX_ENTRIES);
		PointsTally tally;
		tally.add(202, 36);
		tally.add(201, 36);
		const Order order = order_tally(tally, qualifying);
		CHECK(order.at(1) == 201);
	}
}

// --- championship points for a round ------------------------------------------

TEST_CASE("championship points come from three classifications, not the Final alone") {
	const int ids[8] = { 101, 102, 103, 104, 105, 106, 107, 108 };
	const double laps[8] = { 48.0, 48.1, 48.2, 48.3, 48.4, 48.5, 48.6, 48.7 };
	const Classification qualifying = qualifying_from_laps(ids, laps, 8);

	const int order[8] = { 101, 102, 103, 104, 105, 106, 107, 108 };
	const Classification heat = full_distance_race(SessionType::Heat, order, 8);
	const Classification super_heat = full_distance_race(SessionType::SuperHeat, order, 8);
	Classification final_race = full_distance_race(SessionType::Final, order, 8);

	const RoundScore score = score_round(qualifying, &heat, 1, super_heat, final_race);
	REQUIRE(score.count == 8);

	// 101 won all three: 25 from the intermediate classification, 25 from the Super
	// Heat, 50 from the Final. 100 whole points, and the fastest lap of the Final on
	// top of it — `full_distance_race` gives the winner the quickest lap.
	const RoundAward *winner = score.find(101);
	REQUIRE(winner != nullptr);
	CHECK(winner->intermediate == half_points_of(25));
	CHECK(winner->super_heat == half_points_of(25));
	CHECK(winner->final_race == half_points_of(50));
	CHECK(winner->fastest_lap == half_points_of(1));
	CHECK(winner->total() == half_points_of(101));
	CHECK(winner->final_position == 1);
	CHECK(score.fastest_lap_driver_id == 101);

	// 8th place: 9 + 9 + 18, and no fastest lap.
	CHECK(score.total_of(108) == half_points_of(36));

	// **The Final alone would be 50, and the round pays 101.** That difference is
	// the whole reason GAMEDESIGN.md §4 scores three classifications: a bad Final
	// does not erase a good day.
	CHECK(score.total_of(101) > half_points_of(
			points_for(PointsScale::ChampionshipFinal, 1) + FINAL_FASTEST_LAP_POINT));

	// The Super Heat is scored twice on two different scales, and that is correct:
	// 90 position points ordering the Final grid, 25 championship points in the
	// table. An implementation that used one array for both is exactly the collapse
	// ADR-0043 is about.
	CHECK(super_heat_position_tally(super_heat).points_of(101) == 90);
	CHECK(points_for(PointsScale::ChampionshipHeat, super_heat.position_of(101)) == 25);

	// Nobody is scored for qualifying, at all.
	CHECK_FALSE(session_awards_championship_points(SessionType::Qualifying));
	CHECK_FALSE(session_awards_championship_points(SessionType::Practice));
	CHECK(session_awards_championship_points(SessionType::Heat));
	CHECK(session_awards_championship_points(SessionType::SuperHeat));
	CHECK(session_awards_championship_points(SessionType::Final));

	// One heat or three, the championship total is unchanged, because the heats
	// produce one classification between them. This is the reading of §4 that the
	// module took and it is worth a check rather than only a comment.
	Classification heats[3] = { heat, heat, heat };
	const RoundScore three = score_round(qualifying, heats, 3, super_heat, final_race);
	CHECK(three.total_of(101) == score.total_of(101));
	CHECK(three.total_of(108) == score.total_of(108));
}

TEST_CASE("a driver who scores in the heats and misses the Final keeps what he scored") {
	const int ids[3] = { 101, 102, 103 };
	const double laps[3] = { 48.0, 48.1, 48.2 };
	const Classification qualifying = qualifying_from_laps(ids, laps, 3);

	const int all[3] = { 101, 102, 103 };
	const Classification heat = full_distance_race(SessionType::Heat, all, 3);
	const Classification super_heat = full_distance_race(SessionType::SuperHeat, all, 3);

	// 101 does not start the Final at all.
	const int final_order[2] = { 102, 103 };
	const Classification final_race = full_distance_race(SessionType::Final, final_order, 2);

	const RoundScore score = score_round(qualifying, &heat, 1, super_heat, final_race);
	CHECK(score.count == 3);
	const RoundAward *absent = score.find(101);
	REQUIRE(absent != nullptr);
	CHECK(absent->intermediate == half_points_of(25));
	CHECK(absent->super_heat == half_points_of(25));
	CHECK(absent->final_race == 0);
	CHECK(absent->final_position == 0);
	CHECK(absent->total() == half_points_of(50));

	CHECK(score.find(999) == nullptr);
	CHECK(score.total_of(999) == 0);
}

TEST_CASE("part distance scales a round's championship points, and the fastest lap point does not") {
	const int ids[3] = { 101, 102, 103 };
	const double laps[3] = { 48.0, 48.1, 48.2 };
	const Classification qualifying = qualifying_from_laps(ids, laps, 3);

	const int order[3] = { 101, 102, 103 };
	const Classification heat = full_distance_race(SessionType::Heat, order, 3);
	const Classification super_heat = full_distance_race(SessionType::SuperHeat, order, 3);

	// 101 leads the Final, sets the fastest lap, and stops on lap 3 of 7 — past the
	// two-lap floor, short of 75%.
	Classification final_race = Classification::of(SessionType::Final);
	{
		DriverResult leader;
		leader.driver_id = 101;
		leader.position = 3; // classified last, having covered least distance
		leader.laps_completed = 3;
		leader.distance_m = 3600.0; // of 7,500 m: 48%
		leader.best_lap_s = 47.4; // fastest lap of the session
		final_race.add(leader);
		for (int i = 0; i < 2; ++i) {
			DriverResult result;
			result.driver_id = 102 + i;
			result.position = i + 1;
			result.laps_completed = 7;
			result.distance_m = 7500.0;
			result.best_lap_s = 48.2 + 0.1 * static_cast<double>(i);
			final_race.add(result);
		}
	}
	CHECK(final_race.credit_of(101) == DistanceCredit::Half);
	CHECK(final_race.credit_of(102) == DistanceCredit::Full);

	const RoundScore score = score_round(qualifying, &heat, 1, super_heat, final_race);
	const RoundAward *retired = score.find(101);
	REQUIRE(retired != nullptr);

	// Half of the 3rd-place Final score, 38/2 = 19, carried exactly as 19 halves.
	CHECK(retired->final_race == points_for(PointsScale::ChampionshipFinal, 3));
	CHECK(retired->final_race == 38);
	CHECK(half_points_to_points(retired->final_race) == 19.0);

	// The fastest lap point is not scaled. Art. 19 attaches it to a lap, and the
	// part-distance rule scales the points for a classification. Stated in the
	// source as a reading rather than a quotation, and pinned here so that changing
	// the reading is a deliberate edit.
	CHECK(score.fastest_lap_driver_id == 101);
	CHECK(retired->fastest_lap == half_points_of(FINAL_FASTEST_LAP_POINT));

	// Full credit in the heats, because those he finished.
	CHECK(retired->intermediate == half_points_of(25));
	CHECK(heats_credit(&heat, 1, 101) == DistanceCredit::Full);
	CHECK(heats_credit(nullptr, 1, 101) == DistanceCredit::None);
	CHECK(heats_credit(&heat, 1, 999) == DistanceCredit::None);
}

// --- sequencing ---------------------------------------------------------------

TEST_CASE("the round is a state machine, and it ends at the Final") {
	CHECK(first_session_of_round() == SessionType::Practice);
	CHECK(last_session_of_round() == SessionType::Final);
	CHECK(ROUND_ORDER_COUNT == SESSION_TYPE_COUNT);

	SessionType current = first_session_of_round();
	SessionType next = current;
	int steps = 0;
	while (next_session_of_round(current, next)) {
		current = next;
		++steps;
		REQUIRE(steps <= ROUND_ORDER_COUNT);
	}
	CHECK(steps == ROUND_ORDER_COUNT - 1);
	CHECK(current == SessionType::Final);
	CHECK(round_complete_after(current));
	CHECK_FALSE(round_complete_after(SessionType::SuperHeat));

	// G2 Art. 18's order, verbatim, and every session type is in it exactly once.
	CHECK(round_order_index(SessionType::Practice) == 0);
	CHECK(round_order_index(SessionType::Qualifying) == 1);
	CHECK(round_order_index(SessionType::Heat) == 2);
	CHECK(round_order_index(SessionType::SuperHeat) == 3);
	CHECK(round_order_index(SessionType::Final) == 4);
	CHECK(round_order_index(static_cast<SessionType>(99)) == -1);

	// The Final has no successor, which is how the runner knows to score the round.
	SessionType after_final = SessionType::Practice;
	CHECK_FALSE(next_session_of_round(SessionType::Final, after_final));
	CHECK(after_final == SessionType::Practice); // untouched on failure
	SessionType after_bogus = SessionType::Final;
	CHECK_FALSE(next_session_of_round(static_cast<SessionType>(99), after_bogus));
}

TEST_CASE("scheduled_laps is a ceiling, because the flag falls on a full lap") {
	// G2 Art. 18: "the minimum number of full laps necessary for reaching the
	// distance". On the 1,200 m circuit GAMEDESIGN.md §4 assumes, the compressed
	// weekend is 4, 5 and 7 laps — not 3.1, 4.2 and 6.3.
	CHECK(scheduled_laps(3750.0, 1200.0) == 4);
	CHECK(scheduled_laps(5000.0, 1200.0) == 5);
	CHECK(scheduled_laps(7500.0, 1200.0) == 7);
	// Exactly on a lap boundary does not add a lap.
	CHECK(scheduled_laps(6000.0, 1200.0) == 5);
	// Degenerate inputs are zero, not a division.
	CHECK(scheduled_laps(0.0, 1200.0) == 0);
	CHECK(scheduled_laps(3750.0, 0.0) == 0);

	// Practice has no scheduled distance, so it has no lap count.
	CHECK(Classification::of(SessionType::Practice).scheduled_distance_m == 0.0);
	CHECK(Classification::of(SessionType::Qualifying).scheduled_distance_m == 0.0);
	// And the race distances come from `session.h` rather than being restated here.
	CHECK(Classification::of(SessionType::Heat).scheduled_distance_m ==
			scheduled_limit(SessionType::Heat).value);
	CHECK(Classification::of(SessionType::Final).scheduled_distance_m ==
			scheduled_limit(SessionType::Final).value);
}

TEST_CASE("a classification is a bounded list, and it says so rather than overrunning") {
	Classification classification = Classification::of(SessionType::Heat);
	for (int i = 0; i < RULES_MAX_ENTRIES; ++i) {
		DriverResult result;
		result.driver_id = 100 + i;
		result.position = i + 1;
		CHECK(classification.add(result));
	}
	CHECK(classification.count == RULES_MAX_ENTRIES);
	DriverResult overflow;
	overflow.driver_id = 999;
	CHECK_FALSE(classification.add(overflow));
	CHECK(classification.count == RULES_MAX_ENTRIES);

	// The ceiling is session.h's, not a second opinion about grid size.
	CHECK(RULES_MAX_ENTRIES == SESSION_MAX_ENTRIES);

	CHECK(classification.find(999) == nullptr);
	CHECK(classification.position_of(999) == 0);
	CHECK(classification.credit_of(999) == DistanceCredit::None);
	CHECK(classification.index_of(100) == 0);

	Order order;
	for (int i = 0; i < RULES_MAX_ENTRIES; ++i) {
		CHECK(order.add(100 + i));
	}
	CHECK_FALSE(order.add(999));
}
