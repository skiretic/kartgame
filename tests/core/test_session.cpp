#include "doctest.h"

#include "core/session.h"

#include <cstring>

// `SessionConfig` is the argument the session runner takes, the replay header's
// content and what the race rules classify against — ADR-0041, ADR-0043 and
// `ARCHITECTURE.md` §17 are three views of this one type. So the tests below are
// mostly about the fingerprint, because the fingerprint is the thing three
// separate systems will trust without looking.

using namespace kart::core;

namespace {

// A complete, valid configuration. Every test starts from this and moves one
// field, so a test that fails names the field it moved.
SessionConfig practice_config() {
	SessionConfig config;
	config.set_track_id("autumn_ridge");
	config.track_hash = 0x0123456789abcdefULL;
	config.layout = TrackLayout::Forward;
	config.condition = TrackCondition::Dry;
	config.type = SessionType::Practice;
	config.kart_class = KartClass::KZ2;
	config.limit = SessionLimit::open();
	config.entry_count = 1;
	config.roster_hash = 0;
	config.seed = 42;
	return config;
}

} // namespace

TEST_CASE("a default SessionConfig is not valid, because it has no track") {
	SessionConfig config;
	CHECK_FALSE(config.is_valid());
	config.set_track_id("autumn_ridge");
	CHECK(config.is_valid());
}

TEST_CASE("a track id is a slug, because it reaches a filename") {
	SessionConfig config;
	CHECK(config.set_track_id("autumn_ridge"));
	CHECK(std::strcmp(config.track_id, "autumn_ridge") == 0);
	CHECK(config.set_track_id("circuit-1"));
	CHECK(config.set_track_id("t2"));

	// **The case this exists for.** ADR-0046 loads a track from a file named for the
	// id and `profile.h` pastes it into a `user://` path, so a traversal in a track
	// id is a traversal in a filename. Length was the only thing checked before.
	SUBCASE("a traversal is refused and leaves nothing behind") {
		REQUIRE(config.set_track_id("autumn_ridge"));
		CHECK_FALSE(config.set_track_id("../../../etc/passwd"));
		// Empty, not the truncated prefix: `circuit/..` is worse than what it started
		// as, and an empty id fails validation at once.
		CHECK(config.track_id[0] == '\0');
		CHECK_FALSE(config.is_valid());
	}
	SUBCASE("every other way in is refused too") {
		const char *hostile[] = { "..", ".", "a/b", "a\\b", "a.b", "A_B", "a b", "a\tb",
			"a\nb", "ghost;rm", "a*", "~root", "" };
		for (const char *id : hostile) {
			CHECK_FALSE(config.set_track_id(id));
			CHECK(config.track_id[0] == '\0');
		}
	}
	SUBCASE("too long is refused, and also leaves nothing behind") {
		char oversized[SESSION_ID_CHARS + 8];
		for (int i = 0; i < SESSION_ID_CHARS + 7; ++i) {
			oversized[i] = 'a';
		}
		oversized[SESSION_ID_CHARS + 7] = '\0';
		CHECK_FALSE(config.set_track_id(oversized));
		CHECK(config.track_id[0] == '\0');
		CHECK_FALSE(config.is_valid());

		// One character shorter is the longest id that fits.
		char exact[SESSION_ID_CHARS];
		for (int i = 0; i < SESSION_ID_CHARS - 1; ++i) {
			exact[i] = 'a';
		}
		exact[SESSION_ID_CHARS - 1] = '\0';
		CHECK(config.set_track_id(exact));
		CHECK(static_cast<int>(std::strlen(config.track_id)) == SESSION_ID_CHARS - 1);
	}
	SUBCASE("null is refused") {
		CHECK_FALSE(config.set_track_id(nullptr));
		CHECK(config.track_id[0] == '\0');
		CHECK_FALSE(config.is_valid());
	}
}

TEST_CASE("is_valid rejects a limit with no number and an impossible field") {
	SessionConfig config = practice_config();

	config.limit = SessionLimit::laps(0.0);
	CHECK_FALSE(config.is_valid());
	config.limit = SessionLimit::laps(6.0);
	CHECK(config.is_valid());

	// Open ignores its value, so an open session with a stale number in it is
	// still valid — the runner cannot act on a number the kind says is absent.
	config.limit = SessionLimit{ SessionLimitKind::Open, 999.0 };
	CHECK(config.is_valid());

	config.entry_count = 0;
	CHECK_FALSE(config.is_valid());
	config.entry_count = SESSION_MAX_ENTRIES + 1;
	CHECK_FALSE(config.is_valid());
	config.entry_count = 1;
	CHECK(config.is_valid());

	// A parse that produced garbage in an enum is rejected rather than switched on.
	config.type = static_cast<SessionType>(SESSION_TYPE_COUNT);
	CHECK_FALSE(config.is_valid());
}

TEST_CASE("the hash is stable across a copy and changes for every field") {
	const SessionConfig base = practice_config();
	const SessionConfig copy = base;
	CHECK(copy.hash() == base.hash());
	CHECK(first_difference(base, copy) == SessionField::None);

	// One case per hashed field. This is the test that catches a field added to
	// the struct and forgotten in `hash()` — the failure mode being a replay that
	// claims to match a configuration it was not recorded under.
	SUBCASE("track_id") {
		SessionConfig moved = base;
		moved.set_track_id("autumn_ridge_2");
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::TrackId);
	}
	SUBCASE("track_hash") {
		SessionConfig moved = base;
		moved.track_hash ^= 1ULL;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::TrackHash);
	}
	SUBCASE("layout") {
		SessionConfig moved = base;
		moved.layout = TrackLayout::Reverse;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::Layout);
	}
	SUBCASE("type") {
		SessionConfig moved = base;
		moved.type = SessionType::Final;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::Type);
	}
	SUBCASE("kart_class") {
		SessionConfig moved = base;
		moved.kart_class = KartClass::OK;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::KartClass);
	}
	SUBCASE("limit kind and value are separate fields") {
		SessionConfig kind = base;
		kind.limit = SessionLimit::laps(1.0);
		CHECK(kind.hash() != base.hash());
		CHECK(first_difference(base, kind) == SessionField::LimitKind);

		SessionConfig value = kind;
		value.limit.value = 2.0;
		CHECK(value.hash() != kind.hash());
		CHECK(first_difference(kind, value) == SessionField::LimitValue);
	}
	SUBCASE("entry_count") {
		SessionConfig moved = base;
		moved.entry_count = 8;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::EntryCount);
	}
	SUBCASE("roster_hash") {
		SessionConfig moved = base;
		moved.roster_hash = 7;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::RosterHash);
	}
	SUBCASE("assists, both of them, separately") {
		SessionConfig clutch = base;
		clutch.assists.auto_clutch = false;
		CHECK(clutch.hash() != base.hash());
		CHECK(first_difference(base, clutch) == SessionField::AutoClutch);

		SessionConfig shift = base;
		shift.assists.auto_shift = false;
		CHECK(shift.hash() != base.hash());
		CHECK(first_difference(base, shift) == SessionField::AutoShift);
		// And the two are not interchangeable, which a single `assists` byte
		// hashed as one integer would have made them.
		CHECK(clutch.hash() != shift.hash());
	}
	SUBCASE("tuning, which is the hole ADR-0041 exists to close") {
		SessionConfig moved = base;
		const int id = tunable_by_key("steer_gamma");
		REQUIRE(id >= 0);
		moved.tuning.nudge(id, -1);
		REQUIRE_FALSE(moved.tuning.is_default(id));
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::Tuning);
	}
	SUBCASE("seed") {
		SessionConfig moved = base;
		moved.seed = 43;
		CHECK(moved.hash() != base.hash());
		CHECK(first_difference(base, moved) == SessionField::Seed);
	}
}

TEST_CASE("the field names are distinct, so an error message cannot name two fields alike") {
	// The pairwise check exists for the same reason ADR-0043's points-scale check
	// does: a copy-paste in the switch above produces working code, and the defect
	// only shows up as a diagnostic that blames the wrong field.
	const SessionField fields[] = {
		SessionField::None, SessionField::TrackId, SessionField::TrackHash,
		SessionField::Layout, SessionField::Condition, SessionField::Type,
		SessionField::KartClass, SessionField::LimitKind, SessionField::LimitValue,
		SessionField::EntryCount, SessionField::RosterHash, SessionField::AutoClutch,
		SessionField::AutoShift, SessionField::Tuning, SessionField::Seed
	};
	const int count = static_cast<int>(sizeof(fields) / sizeof(fields[0]));
	for (int i = 0; i < count; ++i) {
		const char *name = session_field_name(fields[i]);
		CHECK(std::strcmp(name, "invalid") != 0);
		for (int j = i + 1; j < count; ++j) {
			CHECK(std::strcmp(name, session_field_name(fields[j])) != 0);
		}
	}
}

TEST_CASE("every enum name is distinct within its own enum") {
	for (int i = 0; i < SESSION_TYPE_COUNT; ++i) {
		const char *name = session_type_name(static_cast<SessionType>(i));
		CHECK(std::strcmp(name, "invalid") != 0);
		for (int j = i + 1; j < SESSION_TYPE_COUNT; ++j) {
			CHECK(std::strcmp(name, session_type_name(static_cast<SessionType>(j))) != 0);
		}
	}
	for (int i = 0; i < KART_CLASS_COUNT; ++i) {
		CHECK(std::strcmp(kart_class_name(static_cast<KartClass>(i)), "invalid") != 0);
	}
	for (int i = 0; i < TRACK_LAYOUT_COUNT; ++i) {
		CHECK(std::strcmp(track_layout_name(static_cast<TrackLayout>(i)), "invalid") != 0);
	}
	for (int i = 0; i < SESSION_LIMIT_KIND_COUNT; ++i) {
		CHECK(std::strcmp(session_limit_kind_name(static_cast<SessionLimitKind>(i)),
					  "invalid") != 0);
	}
	// Out of range is named, not undefined behavior, because a corrupt save
	// reaches these functions before `is_valid` has a chance to reject it.
	CHECK(std::strcmp(session_type_name(static_cast<SessionType>(99)), "invalid") == 0);
	CHECK(std::strcmp(track_condition_name(static_cast<TrackCondition>(99)), "invalid") == 0);
}

TEST_CASE("the compressed weekend is the FIA distances times the scale, not a lap table") {
	// GAMEDESIGN.md §4's table, re-derived rather than restated: 15 km, 20 km and
	// 30 km at a quarter are 3.75 km, 5 km and 7.5 km.
	CHECK(scheduled_limit(SessionType::Heat).kind == SessionLimitKind::Distance);
	CHECK(scheduled_limit(SessionType::Heat).value == doctest::Approx(3750.0));
	CHECK(scheduled_limit(SessionType::SuperHeat).value == doctest::Approx(5000.0));
	CHECK(scheduled_limit(SessionType::Final).value == doctest::Approx(7500.0));

	// Qualifying is a time, and compressed by half rather than a quarter — see the
	// constant's comment for why. 6 minutes becomes 3.
	CHECK(scheduled_limit(SessionType::Qualifying).kind == SessionLimitKind::Duration);
	CHECK(scheduled_limit(SessionType::Qualifying).value == doctest::Approx(180.0));

	// Practice ends when the driver leaves.
	CHECK(scheduled_limit(SessionType::Practice).kind == SessionLimitKind::Open);

	// Every scheduled limit is a runnable one.
	for (int i = 0; i < SESSION_TYPE_COUNT; ++i) {
		SessionConfig config = practice_config();
		config.type = static_cast<SessionType>(i);
		config.limit = scheduled_limit(config.type);
		config.entry_count = session_has_field(config.type) ? 8 : 1;
		config.roster_hash = session_has_field(config.type) ? 0x99ULL : 0;
		CHECK(config.is_valid());
	}
}

TEST_CASE("what the compression really costs, in whole laps") {
	// The compression exists to hit one number — a round of about 15 minutes,
	// `GAMEDESIGN.md` §4 — and nothing else in the codebase checks that it does.
	//
	// **A race is a whole number of laps, and rounding it down is what makes this
	// test agree with a table that is wrong.** FIA Karting's General Prescriptions
	// specify a distance and the race runs the minimum number of *full* laps that
	// reaches it, which is a ceiling: 3,750 m on a 1,200 m circuit is 4 laps, not
	// 3.125 and not 3. §4's lap column says ~3, ~4 and ~6 against the ceiling's
	// 4, 5 and 7, so the doc is one lap short on all three races and its "Round
	// ~= 15 minutes, Season ~= 60 minutes" follows from the short count.
	//
	// This case measures both and asserts the ceiling, because that is what the
	// game will actually run. The 60-minute season is a design constraint and
	// whether to move `SESSION_DISTANCE_SCALE` to meet it is not this test's call —
	// but the figure is written down here so the choice is made against a number.
	const double lap_time_s = 48.0;
	const double lap_length_m = 1200.0;

	double fractional_s = scheduled_limit(SessionType::Qualifying).value;
	double whole_lap_s = fractional_s;
	int laps = 0;
	for (const SessionType type :
			{ SessionType::Heat, SessionType::SuperHeat, SessionType::Final }) {
		const SessionLimit limit = scheduled_limit(type);
		REQUIRE(limit.kind == SessionLimitKind::Distance);
		fractional_s += limit.value / lap_length_m * lap_time_s;
		const int full_laps = static_cast<int>(std::ceil(limit.value / lap_length_m));
		laps += full_laps;
		whole_lap_s += static_cast<double>(full_laps) * lap_time_s;
	}

	// 4 + 5 + 7 on a 1,200 m circuit.
	CHECK(laps == 16);
	MESSAGE("a round is " << whole_lap_s / 60.0 << " min over " << laps
						  << " racing laps, against " << fractional_s / 60.0
						  << " min if part laps counted; a season of four rounds is "
						  << 4.0 * whole_lap_s / 60.0 << " min");
	// 180 s of qualifying plus 16 laps at 48 s.
	CHECK(whole_lap_s == doctest::Approx(948.0));
	// And the season overshoots the design's one evening by 3.2 minutes. Asserted
	// rather than lamented: if the scale moves, this line is what notices.
	CHECK(4.0 * whole_lap_s / 60.0 == doctest::Approx(63.2));
}
