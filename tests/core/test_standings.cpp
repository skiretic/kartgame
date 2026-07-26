#include "doctest.h"

#include "core/pcg32.h"
#include "core/standings.h"

// The season table and promotion. ADR-0043.
//
// The last test in this file is the one ADR-0043 asked for: a whole season,
// simulated end to end — eight drivers, four rounds, points from all three
// classifications per round, a promotion decision — because that is the cheapest
// possible way to find out that the structure in `GAMEDESIGN.md` §4 does not
// actually work.
//
// Points are integers in half units, so every comparison below is exact and no
// `doctest::Approx` appears on one. `Approx(x).epsilon(e)` is not a relative
// tolerance and the two places a double is compared are written out.

using namespace kart::core;

namespace {

// --- a season simulator -------------------------------------------------------
//
// Enough of a race to exercise the rules and no more: every driver has a pace, a
// session adds noise to it, and the classification is the ordering that produces.
// Deterministic from one seed, so the numbers in this file are reproducible.

constexpr int FIELD = 8;

// FIA OK racing numbers start at 101 — `docs/REFERENCES.md` reads the block off a
// real entry list (G8). Using them rather than 0..7 catches any place that confuses
// a driver id with an index.
constexpr int FIRST_NUMBER = 101;

// Whole championship points available in one round at full credit, over an 8-kart
// field: the 131-point championship heat scale twice — the intermediate
// classification and the Super Heat — plus the Final's 262, plus the fastest lap.
constexpr int ROUND_POINTS_AT_FULL_CREDIT = 131 + 131 + 262 + 1;

struct Season {
	Pcg32 rng;

	// Base lap time per driver, seconds. 0.12 s apart on a 48 s lap, against
	// +/-0.25 s of session noise, so a single session shuffles freely. Whether four
	// rounds of it shuffle is the question the season test measures rather than
	// assumes.
	double base_lap_s[FIELD] = {};

	explicit Season(uint64_t seed) :
			rng(seed, Pcg32::DEFAULT_STREAM) {
		for (int i = 0; i < FIELD; ++i) {
			base_lap_s[i] = 47.80 + 0.12 * static_cast<double>(i);
		}
	}

	int driver_id(int index) const { return FIRST_NUMBER + index; }

	// One session's pace per driver.
	void draw_pace(double *out) {
		for (int i = 0; i < FIELD; ++i) {
			out[i] = base_lap_s[i] + rng.next_range(-0.25, 0.25);
		}
	}

	// Indices sorted by pace, quickest first. Insertion sort: no allocation, and the
	// suite has no business pulling in <algorithm> for eight elements.
	void order_by_pace(const double *pace, int *order) {
		for (int i = 0; i < FIELD; ++i) {
			order[i] = i;
		}
		for (int i = 1; i < FIELD; ++i) {
			const int current = order[i];
			int j = i - 1;
			while (j >= 0 && pace[current] < pace[order[j]]) {
				order[j + 1] = order[j];
				--j;
			}
			order[j + 1] = current;
		}
	}

	Classification qualifying() {
		double pace[FIELD];
		draw_pace(pace);
		Classification classification = Classification::of(SessionType::Qualifying);
		for (int i = 0; i < FIELD; ++i) {
			DriverResult result;
			result.driver_id = driver_id(i);
			result.laps_completed = 3;
			result.best_lap_s = pace[i];
			classification.add(result);
		}
		assign_positions_by_best_lap(classification);
		return classification;
	}

	// A race. `retire_index` is a driver who stops after `retire_laps` laps, or -1.
	Classification race(SessionType type, int retire_index, int retire_laps) {
		double pace[FIELD];
		draw_pace(pace);
		int order[FIELD];
		order_by_pace(pace, order);

		Classification classification = Classification::of(type);
		const double scheduled = classification.scheduled_distance_m;
		const int laps = scheduled_laps(scheduled, 1200.0);

		int position = 0;
		for (int slot = 0; slot < FIELD; ++slot) {
			const int index = order[slot];
			DriverResult result;
			result.driver_id = driver_id(index);
			result.best_lap_s = pace[index];
			if (index == retire_index) {
				// Classified behind everybody who went the distance, which is what a
				// results sheet does: a retirement is still classified if it covered
				// enough of the race.
				result.laps_completed = retire_laps;
				result.distance_m = 1200.0 * static_cast<double>(retire_laps);
				result.position = FIELD;
				classification.add(result);
				continue;
			}
			++position;
			result.laps_completed = laps;
			result.distance_m = scheduled;
			result.position = position;
			classification.add(result);
		}
		// Positions come out 1..FIELD with no gap: the retirement does not take a
		// number from the drivers who finished ahead of it, it takes the last one.
		return classification;
	}

	// One round of GAMEDESIGN.md §4's weekend: qualifying, one heat, the Super Heat,
	// the Final.
	RoundScore round(int retire_index, int retire_laps) {
		const Classification qualifying_classification = qualifying();
		const Classification heat = race(SessionType::Heat, -1, 0);
		const Classification super_heat = race(SessionType::SuperHeat, -1, 0);
		const Classification final_race =
				race(SessionType::Final, retire_index, retire_laps);
		return score_round(qualifying_classification, &heat, 1, super_heat, final_race);
	}
};

} // namespace

// --- the rules the table encodes ----------------------------------------------

TEST_CASE("a four-round season drops nothing, and the rule is the threshold not the four") {
	// G2 Art. 19 b discards results at five Competitions or more. G4 Art. 4 makes the
	// season four. GAMEDESIGN.md §5 calls that "a rule we get to keep by accident of
	// season length" — so the four is a design number and the five is a regulation,
	// and the threshold is what gets encoded.
	CHECK(SEASON_ROUNDS == 4);
	CHECK(DISCARD_FIRST_COMPETITION_COUNT == 5);
	CHECK(season_counts_every_result(SEASON_ROUNDS));
	for (int rounds = 0; rounds < DISCARD_FIRST_COMPETITION_COUNT; ++rounds) {
		CHECK(season_counts_every_result(rounds));
		CHECK(counted_results(rounds) == rounds);
	}
	// At the threshold the discard engages, and the number of results that count
	// comes from a case table this repository has not read. `counted_results` says
	// "unknown" rather than guessing 80%, which would be a plausible wrong number
	// nobody would ever see. `docs/REFERENCES.md` records why 80% is not it: 80% of
	// 7 is 5.6, and the article's own case table does not say 5.6.
	CHECK_FALSE(season_counts_every_result(DISCARD_FIRST_COMPETITION_COUNT));
	CHECK(counted_results(DISCARD_FIRST_COMPETITION_COUNT) == -1);
	CHECK(counted_results(6) == -1);
	CHECK(counted_results(-1) == 0);
}

TEST_CASE("promotion is top 3 in the final standings, and it is ours") {
	// GAMEDESIGN.md §5. No regulation promotes anybody between classes; this is a
	// game design decision and `standings.h` marks it as one. Top 3 rather than the
	// title because a 4-round season on a 50-point Final scale can lose a title to
	// one bad race.
	CHECK(PROMOTION_POSITIONS == 3);
	CHECK(position_promotes(1));
	CHECK(position_promotes(3));
	CHECK_FALSE(position_promotes(4));
	// Position 0 is "not in the table", which does not promote.
	CHECK_FALSE(position_promotes(0));
	CHECK_FALSE(position_promotes(-1));
}

TEST_CASE("the table sums rounds, and refuses to overrun the season") {
	SeasonTable table;
	CHECK(table.driver_count() == 0);
	CHECK(table.round_count() == 0);
	CHECK(table.every_result_counts());

	RoundScore score;
	score.awards[0].driver_id = 101;
	score.awards[0].final_race = half_points_of(50);
	score.awards[0].final_position = 1;
	score.awards[1].driver_id = 102;
	score.awards[1].final_race = half_points_of(44);
	score.awards[1].final_position = 2;
	score.count = 2;

	for (int round = 0; round < SEASON_MAX_ROUNDS; ++round) {
		CHECK(table.add_round(score));
	}
	CHECK(table.round_count() == SEASON_MAX_ROUNDS);
	// Full: refuses rather than overwriting the last round, which would silently
	// lose a result.
	CHECK_FALSE(table.add_round(score));
	CHECK(table.round_count() == SEASON_MAX_ROUNDS);

	CHECK(table.total_half_points(101) == half_points_of(50 * SEASON_MAX_ROUNDS));
	CHECK(table.total_points(101) == 400.0);
	CHECK(table.final_wins(101) == SEASON_MAX_ROUNDS);
	CHECK(table.final_wins(102) == 0);
	CHECK(table.final_finishes_at(102, 2) == SEASON_MAX_ROUNDS);
	CHECK(table.final_finishes_at(102, 0) == 0);

	// Past the discard threshold, so the sum above is a sum and not a championship,
	// and the table says which it is.
	CHECK_FALSE(table.every_result_counts());

	// A driver nobody entered.
	CHECK(table.total_half_points(999) == 0);
	CHECK(table.index_of(999) == -1);
	CHECK(table.position_of(999) == 0);
	CHECK_FALSE(table.promotes(999));
	CHECK(table.round_half_points(101, SEASON_MAX_ROUNDS) == 0);
	CHECK(table.round_final_position(101, -1) == 0);
}

TEST_CASE("a season tie falls to a countback on Final results, which is ours") {
	// The FIA's championship tie-break could not be obtained: `docs/REFERENCES.md`
	// sources the *weekend* tie-break — the Qualifying Practice classification, at
	// every stage — and that rule cannot be reused here, because a season has four
	// qualifying classifications and no way to choose between them. So the countback
	// is this project's rule and `standings.h` labels it rather than citing it.
	SeasonTable table;
	// Two drivers, two rounds, identical totals: one takes a win and a third, the
	// other two seconds. 50 + 38 == 44 + 44.
	RoundScore first;
	first.count = 2;
	first.awards[0].driver_id = 201;
	first.awards[0].final_race = half_points_of(50);
	first.awards[0].final_position = 1;
	first.awards[1].driver_id = 202;
	first.awards[1].final_race = half_points_of(44);
	first.awards[1].final_position = 2;
	RoundScore second;
	second.count = 2;
	second.awards[0].driver_id = 201;
	second.awards[0].final_race = half_points_of(38);
	second.awards[0].final_position = 3;
	second.awards[1].driver_id = 202;
	second.awards[1].final_race = half_points_of(44);
	second.awards[1].final_position = 2;
	CHECK(table.add_round(first));
	CHECK(table.add_round(second));

	CHECK(table.total_half_points(201) == table.total_half_points(202));
	CHECK(table.total_points(201) == 88.0);
	// The win decides it.
	CHECK(table.position_of(201) == 1);
	CHECK(table.position_of(202) == 2);
	CHECK(table.final_wins(201) == 1);
	CHECK(table.final_wins(202) == 0);

	// And with the countback exhausted as well — identical results in every round —
	// order of registration is the last resort. It exists so the ordering is total:
	// a comparator that calls two drivers equal makes a promotion decision depend on
	// the sort.
	SeasonTable identical;
	RoundScore level;
	level.count = 2;
	level.awards[0].driver_id = 301;
	level.awards[0].final_race = half_points_of(30);
	level.awards[0].final_position = 5;
	level.awards[1].driver_id = 302;
	level.awards[1].final_race = half_points_of(30);
	level.awards[1].final_position = 5;
	CHECK(identical.add_round(level));
	CHECK(identical.position_of(301) == 1);
	CHECK(identical.position_of(302) == 2);
}

TEST_CASE("promoted() is the top three, or the whole field if it is smaller") {
	SeasonTable table;
	RoundScore score;
	score.count = 2;
	score.awards[0].driver_id = 401;
	score.awards[0].final_race = half_points_of(50);
	score.awards[0].final_position = 1;
	score.awards[1].driver_id = 402;
	score.awards[1].final_race = half_points_of(44);
	score.awards[1].final_position = 2;
	CHECK(table.add_round(score));

	const Order promoted = table.promoted();
	CHECK(promoted.count == 2);
	CHECK(promoted.at(1) == 401);
	CHECK(promoted.at(2) == 402);
	CHECK(table.promotes(401));
	CHECK(table.promotes(402));

	// A driver registered with no results is last and does not promote once there are
	// four of them.
	SeasonTable wide;
	CHECK(wide.add_round(score));
	CHECK(wide.add_driver(403));
	CHECK(wide.add_driver(404));
	CHECK(wide.driver_count() == 4);
	CHECK(wide.promoted().count == 3);
	CHECK_FALSE(wide.promotes(404));
	CHECK(wide.position_of(404) == 4);
}

// --- the whole season ---------------------------------------------------------

TEST_CASE("a whole season runs end to end: eight drivers, four rounds, a promotion") {
	// ADR-0043's consequence, cashed in. Eight drivers, four rounds of the compressed
	// weekend, championship points from all three classifications per round, and a
	// promotion decision at the end — in about a millisecond, with no engine.
	//
	// The point of running it is not the numbers, it is that the structure closes:
	// every session produces a classification the next one can use as a grid, every
	// round produces an award for every driver, and the season produces a total order
	// with a top three in it. The specific figures below are pinned so that a change
	// to any scale, tie-break or credit rule shows up as a diff rather than as a
	// plausible different table.
	Season season(20260726ULL);
	SeasonTable table;

	for (int i = 0; i < FIELD; ++i) {
		CHECK(table.add_driver(season.driver_id(i)));
	}

	for (int round = 0; round < SEASON_ROUNDS; ++round) {
		// Round 3 loses a driver: 103 stops after 3 laps of a 7-lap Final, which is
		// past the two-lap floor and short of 75%, so his Final score is halved.
		const int retire_index = round == 2 ? 2 : -1;
		const RoundScore score = season.round(retire_index, 3);
		REQUIRE(score.count == FIELD);

		// Every round scores every driver, and the intermediate classification and the
		// Final grid both seat the whole field. This is the structural assertion.
		CHECK(score.intermediate.count == FIELD);
		CHECK(score.final_grid.count == FIELD);
		CHECK(score.fastest_lap_driver_id != DRIVER_NONE);
		for (int i = 0; i < FIELD; ++i) {
			const int id = season.driver_id(i);
			CHECK(score.intermediate.position_of(id) >= 1);
			CHECK(score.final_grid.position_of(id) >= 1);
			CHECK(score.total_of(id) > 0);
		}

		// The round's total award is the same every round when nobody is part-scored,
		// and it is a number worth stating: the championship heat scale sums to 131
		// over eight places, it is paid twice — once for the intermediate
		// classification and once for the Super Heat — the Final scale sums to 262, and
		// the fastest lap is 1. **525 championship points are on the table per round**,
		// of which the Final is half.
		HalfPoints round_total = 0;
		for (int i = 0; i < score.count; ++i) {
			round_total += score.awards[i].total();
		}
		CHECK(ROUND_POINTS_AT_FULL_CREDIT == 525);
		if (retire_index < 0) {
			CHECK(round_total == half_points_of(ROUND_POINTS_AT_FULL_CREDIT));
		} else {
			// One driver's Final score halved. He was classified 8th, worth 18, so the
			// round pays 9 whole points less than a clean one.
			CHECK(round_total == half_points_of(ROUND_POINTS_AT_FULL_CREDIT) - 18);
			CHECK(half_points_to_points(round_total) == 516.0);
		}

		CHECK(table.add_round(score));
	}

	CHECK(table.round_count() == SEASON_ROUNDS);
	CHECK(table.driver_count() == FIELD);
	CHECK(table.every_result_counts());

	// The standings are a total order over the whole field.
	const Order standings = table.classification();
	REQUIRE(standings.count == FIELD);
	for (int i = 0; i < FIELD; ++i) {
		CHECK(standings.position_of(season.driver_id(i)) >= 1);
	}
	// Monotonically decreasing points down the table, which is the property that
	// makes it a championship rather than a list.
	for (int position = 2; position <= FIELD; ++position) {
		CHECK(table.total_half_points(standings.at(position)) <=
				table.total_half_points(standings.at(position - 1)));
	}

	// Every point awarded in the season is in the table, and none was invented or
	// dropped. Three clean rounds at 525 and one at 516.
	HalfPoints season_total = 0;
	for (int i = 0; i < FIELD; ++i) {
		season_total += table.total_half_points(season.driver_id(i));
	}
	CHECK(season_total == 4 * half_points_of(ROUND_POINTS_AT_FULL_CREDIT) - 18);
	CHECK(half_points_to_points(season_total) == 2091.0);

	// The measured season, pinned from seed 20260726 and the pace spread above.
	const int expected_order[FIELD] = { 101, 102, 103, 104, 105, 106, 107, 108 };
	const double expected_points[FIELD] = {
		381.0, 352.0, 285.0, 271.0, 228.0, 224.0, 192.0, 158.0
	};
	for (int position = 1; position <= FIELD; ++position) {
		CHECK(standings.at(position) == expected_order[position - 1]);
		CHECK(table.total_points(standings.at(position)) ==
				expected_points[position - 1]);
	}
	CHECK(table.final_wins(101) == 2);
	CHECK(table.final_wins(102) == 1);
	CHECK(table.final_wins(103) == 1);
	CHECK(table.final_wins(104) == 0);

	// **The season sorted the field exactly on pace, and that is a finding about the
	// format rather than about the code.** Twelve races of 0.12 s pace steps under
	// +/-0.25 s of session noise came out 101 through 108 in order, even though the
	// individual sessions shuffled freely — 105 finished 6th in the first Final and
	// 106 finished 5th. Three races a round times four rounds is enough averaging to
	// bury two sigma of noise, so GAMEDESIGN.md §4's "three races per round may be one
	// too many" open question has a second edge to it: three races is also what makes
	// the championship a pace measurement rather than a coin toss.
	for (int position = 1; position <= FIELD; ++position) {
		CHECK(standings.at(position) == season.driver_id(position - 1));
	}

	// It is not a walkover, though, and the close pair is the interesting number: 5th
	// and 6th are 4 points apart on totals of 228 and 224, which is 1.8% — one Super
	// Heat classification. Written out rather than through Approx, whose epsilon is
	// not a relative tolerance.
	const double close_margin = table.total_points(standings.at(5)) -
			table.total_points(standings.at(6));
	CHECK(close_margin == 4.0);
	const double title_margin = table.total_points(standings.at(1)) -
			table.total_points(standings.at(2));
	CHECK(title_margin == 29.0);

	// The part-distance rule fired inside a real season and cost a real championship
	// position's worth of points: 103 stopped on lap 3 of round 3's Final, was
	// classified 8th, and took 9 whole points for it instead of 18. He finished the
	// season 3rd on 285, and would have been 3rd on 294 without it — so this test
	// covers the rule without the rule deciding the outcome, which is what makes the
	// rest of the season legible.
	CHECK(table.round_final_position(103, 2) == 8);
	CHECK(table.round_half_points(103, 2) == half_points_of(45));
	CHECK(table.round_half_points(103, 3) == half_points_of(74));

	// Promotion at top 3. 101, 102 and 103 go to KZ2; the other five re-run the
	// season, per GAMEDESIGN.md §5's "losing is real".
	const Order promoted = table.promoted();
	CHECK(promoted.count == PROMOTION_POSITIONS);
	CHECK(promoted.at(1) == 101);
	CHECK(promoted.at(2) == 102);
	CHECK(promoted.at(3) == 103);
	CHECK(table.promotes(103));
	// 4th by 14 points, and it does not promote. That is the rule doing the thing
	// GAMEDESIGN.md §5 wanted it to do.
	CHECK_FALSE(table.promotes(104));
	CHECK_FALSE(table.promotes(108));
}
