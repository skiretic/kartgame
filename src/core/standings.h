#ifndef KART_CORE_STANDINGS_H
#define KART_CORE_STANDINGS_H

#include "core/race_rules.h"

// The season table, and promotion. ROADMAP M3c, ADR-0043.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// Allocation-free and locale-free, same as `race_rules.h` and `tuning.h`.
//
// **This header does not include `profile.h`, and must not.** The save file
// persists a projection of this table; the dependency runs one way, so the season
// arithmetic can be tested without a save format existing and the save format can
// change without this file moving. A cycle between them would make both untestable
// in isolation, which is the property ADR-0017 bought in the first place.
//
// ## What a season is
//
// Four rounds, nothing dropped, promotion at top 3. Two of those three are
// sourced — `GAMEDESIGN.md` §5 and `docs/REFERENCES.md` G4 Art. 4 — and the third
// is ours and is marked as such below.

namespace kart::core {

inline constexpr int STANDINGS_MAX_DRIVERS = RULES_MAX_ENTRIES;

// A season is four Competitions. G4 Art. 4: the 2026 FIA Karting European
// Championship for OK is run over four. `GAMEDESIGN.md` §5 keeps it because a
// round is about 15 minutes and four rounds is one evening, which was the
// design's only hard constraint — so the number is sourced *and* it happens to
// be the number the constraint wanted.
inline constexpr int SEASON_ROUNDS = 4;

// Storage ceiling, not the season length. Sized so the discard rule below can be
// exercised past its threshold in a test without the table being the thing that
// refuses. A season longer than this is a parse error, in the same spirit as
// `SESSION_MAX_ENTRIES`.
inline constexpr int SEASON_MAX_ROUNDS = 8;

// The number of Competitions at which G2 Art. 19 b starts discarding results.
// Below it every result counts; at it and above, some do not.
//
// **This is the rule, encoded, and the rule is a threshold plus a table this
// project has not read.** `docs/REFERENCES.md` records that dropped scores exist
// (G2 Art. 19 b) and do not bite below five Competitions, and it records that the
// article's own "80% of the results rounded up or down" sentence does not
// reproduce its own case table — 80% of 7 is 5.6, not 6 — so the case table
// governs and the percentage is descriptive. The case table itself is not quoted
// anywhere in this repository.
//
// So `counted_results()` below returns the honest thing above the threshold, which
// is "unknown", rather than 80% of the round count. Inventing the discard count
// would be exactly the failure ADR-0043 is about: nobody would ever see it.
inline constexpr int DISCARD_FIRST_COMPETITION_COUNT = 5;

// Whether every result in a season of this length counts.
//
// At `SEASON_ROUNDS` this is true, and `GAMEDESIGN.md` §5 calls that "a rule we get
// to keep by accident of season length". The accident is worth encoding as a
// threshold rather than as the sentence "at four rounds nothing is dropped",
// because the four is a design number and the five is a regulation.
inline constexpr bool season_counts_every_result(int round_count) {
	return round_count < DISCARD_FIRST_COMPETITION_COUNT;
}

// How many of a season's results count toward the championship, or **-1 when the
// answer is not sourced**. See `DISCARD_FIRST_COMPETITION_COUNT`: past the
// threshold the number comes from a case table nobody here has read, and a
// plausible guess is worse than a refusal.
inline constexpr int counted_results(int round_count) {
	if (round_count < 0) {
		return 0;
	}
	return season_counts_every_result(round_count) ? round_count : -1;
}

// --- promotion ----------------------------------------------------------------

// **OURS, not the FIA's.** No regulation promotes anybody between classes; this is
// `GAMEDESIGN.md` §5's career rule and it is a game design decision.
//
// Top 3 rather than the title, and §5 gives the reason: a 4-round season is short
// enough that one bad Final on a 50-point scale can end a title run on variance
// rather than on driving, and the point of the career is to reach the shifter, not
// to gate it behind a perfect season. Losing is still real — finish 4th and the
// season does not promote, and it has to be re-run.
inline constexpr int PROMOTION_POSITIONS = 3;

// Whether a final standings position promotes. Total, so position 0 — a driver
// with no entry in the table — does not.
inline constexpr bool position_promotes(int standings_position) {
	return standings_position >= 1 && standings_position <= PROMOTION_POSITIONS;
}

// --- the table ----------------------------------------------------------------

// Championship standings over a season.
//
// Fixed storage, no allocation, drivers registered on first appearance. Points are
// `HalfPoints` throughout for the reason `race_rules.h` gives: the part-distance
// rule produces halves, and the standings order is decided by comparing these
// numbers for equality.
class SeasonTable {
public:
	int driver_count() const { return driver_count_; }
	int round_count() const { return round_count_; }

	// Register a driver with no results. Only needed to fix the last-resort
	// tie-break order — see `classification()` — since `add_round` registers
	// anybody it has not seen. Returns false when full.
	bool add_driver(int driver_id) {
		return index_of_or_add(driver_id) >= 0;
	}

	int index_of(int driver_id) const {
		for (int i = 0; i < driver_count_; ++i) {
			if (driver_id_[i] == driver_id) {
				return i;
			}
		}
		return -1;
	}

	// Record one round. Returns false when the season is already
	// `SEASON_MAX_ROUNDS` long, rather than overwriting the last round.
	bool add_round(const RoundScore &score) {
		if (round_count_ >= SEASON_MAX_ROUNDS) {
			return false;
		}
		const int round = round_count_;
		++round_count_;
		for (int i = 0; i < score.count; ++i) {
			const RoundAward &award = score.awards[i];
			const int index = index_of_or_add(award.driver_id);
			if (index < 0) {
				continue;
			}
			half_points_[index][round] = award.total();
			final_position_[index][round] = award.final_position;
		}
		return true;
	}

	HalfPoints round_half_points(int driver_id, int round) const {
		const int index = index_of(driver_id);
		if (index < 0 || round < 0 || round >= round_count_) {
			return 0;
		}
		return half_points_[index][round];
	}

	int round_final_position(int driver_id, int round) const {
		const int index = index_of(driver_id);
		if (index < 0 || round < 0 || round >= round_count_) {
			return 0;
		}
		return final_position_[index][round];
	}

	// Season total, in half points.
	//
	// **Every round counts, and that is checked rather than assumed.** If the
	// season has grown past the discard threshold this still sums everything,
	// because dropping the right number of results needs a case table this project
	// has not read. `every_result_counts()` is how a caller finds out that the total
	// it is holding is a sum and not a championship.
	HalfPoints total_half_points(int driver_id) const {
		const int index = index_of(driver_id);
		if (index < 0) {
			return 0;
		}
		HalfPoints total = 0;
		for (int round = 0; round < round_count_; ++round) {
			total += half_points_[index][round];
		}
		return total;
	}

	double total_points(int driver_id) const {
		return half_points_to_points(total_half_points(driver_id));
	}

	// Whether the season is short enough that the sum above is the championship.
	bool every_result_counts() const {
		return season_counts_every_result(round_count_);
	}

	// How many Finals a driver won. The first countback key, and worth exposing on
	// its own because a standings table shows it.
	int final_wins(int driver_id) const {
		return final_finishes_at(driver_id, 1);
	}

	// How many times a driver finished a Final in exactly this position.
	int final_finishes_at(int driver_id, int position) const {
		const int index = index_of(driver_id);
		if (index < 0 || position < 1) {
			return 0;
		}
		int count = 0;
		for (int round = 0; round < round_count_; ++round) {
			if (final_position_[index][round] == position) {
				++count;
			}
		}
		return count;
	}

	// The standings, best first.
	//
	// Most points, then a countback on Final results, then order of registration.
	//
	// **The countback is OURS and it is unsourced.** The FIA's championship
	// tie-break could not be obtained — `docs/REFERENCES.md` sources the *weekend*
	// tie-break (the Qualifying Practice classification, at every stage) and G2
	// Art. 19 b's discard threshold, and neither answers what happens when two
	// drivers finish a season level on points. A countback on best results is the
	// conventional motorsport answer and it is the one taken here, but it is a
	// convention this project has read in journalism and not in a rulebook, so it
	// is labelled rather than cited. It is also the reason `RoundAward` carries the
	// Final position at all.
	//
	// The weekend rule cannot be reused here: "the Qualifying Practice
	// classification" is a per-round object and a season has four of them.
	//
	// Order of registration is the last resort and it only fires for two drivers
	// with identical points and identical Final results in every round. It exists
	// because the ordering has to be total: a comparator that calls two distinct
	// drivers equal makes the output depend on the sort, and a promotion decision
	// would then hinge on it.
	//
	// Insertion sort, hand-rolled — `std::stable_sort` allocates.
	Order classification() const {
		int index[STANDINGS_MAX_DRIVERS];
		for (int i = 0; i < driver_count_; ++i) {
			index[i] = i;
		}
		for (int i = 1; i < driver_count_; ++i) {
			const int current = index[i];
			int j = i - 1;
			while (j >= 0 && ahead_of(current, index[j])) {
				index[j + 1] = index[j];
				--j;
			}
			index[j + 1] = current;
		}
		Order order;
		for (int i = 0; i < driver_count_; ++i) {
			order.add(driver_id_[index[i]]);
		}
		return order;
	}

	// 1-based standings position, or 0 for a driver who is not in the table.
	int position_of(int driver_id) const {
		return classification().position_of(driver_id);
	}

	// Whether this driver's season promotes him. `GAMEDESIGN.md` §5's rule, applied
	// to the final standings rather than to the title.
	bool promotes(int driver_id) const {
		return position_promotes(position_of(driver_id));
	}

	// The drivers who promote, best first. At most `PROMOTION_POSITIONS` of them,
	// and fewer when the field is smaller than that.
	Order promoted() const {
		const Order standings = classification();
		Order result;
		for (int position = 1; position <= PROMOTION_POSITIONS; ++position) {
			const int id = standings.at(position);
			if (id == DRIVER_NONE) {
				break;
			}
			result.add(id);
		}
		return result;
	}

private:
	int index_of_or_add(int driver_id) {
		const int existing = index_of(driver_id);
		if (existing >= 0) {
			return existing;
		}
		if (driver_count_ >= STANDINGS_MAX_DRIVERS) {
			return -1;
		}
		const int index = driver_count_;
		driver_id_[index] = driver_id;
		for (int round = 0; round < SEASON_MAX_ROUNDS; ++round) {
			half_points_[index][round] = 0;
			final_position_[index][round] = 0;
		}
		++driver_count_;
		return index;
	}

	// Strict weak ordering, and total: `ahead_of(a, b)` and `ahead_of(b, a)` are
	// never both false for distinct indices.
	bool ahead_of(int a, int b) const {
		const HalfPoints points_a = total_half_points(driver_id_[a]);
		const HalfPoints points_b = total_half_points(driver_id_[b]);
		if (points_a != points_b) {
			return points_a > points_b;
		}
		// Countback: more wins, then more seconds, and so on down the scale. Bounded
		// by the truncated scale's length because a position past it scored nothing
		// and cannot separate two drivers who are already level on points.
		for (int position = 1; position <= POINTS_SCALE_PLACES; ++position) {
			const int count_a = final_finishes_at(driver_id_[a], position);
			const int count_b = final_finishes_at(driver_id_[b], position);
			if (count_a != count_b) {
				return count_a > count_b;
			}
		}
		return a < b;
	}

	int driver_id_[STANDINGS_MAX_DRIVERS] = {};
	HalfPoints half_points_[STANDINGS_MAX_DRIVERS][SEASON_MAX_ROUNDS] = {};
	int final_position_[STANDINGS_MAX_DRIVERS][SEASON_MAX_ROUNDS] = {};
	int driver_count_ = 0;
	int round_count_ = 0;
};

} // namespace kart::core

#endif // KART_CORE_STANDINGS_H
