#include "doctest.h"

#include "core/replay.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// ADR-0041's format, and the two properties the ADR is actually about: that an
// input survives the body **bit for bit**, and that the three playback outcomes are
// three things rather than two and a shade.
//
// Note on tolerances: nothing below uses `doctest::Approx` on a value whose
// magnitude is not order 1. CLAUDE.md records why — `Approx(x).epsilon(e)` compares
// against `e * (1.0 + max(|a|,|b|))`, so on a value of 0.03 an `epsilon(0.06)` is a
// tolerance of 0.062 and it once passed a test that was wrong by 46%. Every
// comparison here is either exact (which is the point, for a storage grid) or an
// explicit relative difference.

using namespace kart::core;

namespace {

bool contains(const char *haystack, const char *needle) {
	return std::strstr(haystack, needle) != nullptr;
}

// A complete recorded header. Every test starts here and moves one thing.
ReplayHeader recorded_header() {
	ReplayHeader header;
	header.set_build("8f1c4a2b6d0e");
	header.set_api_version("4.7.1");
	header.tick_hz = 120;
	header.tick_count = 3600;
	header.hash_interval = REPLAY_HASH_INTERVAL;
	header.config.set_track_id("autumn_ridge");
	header.config.track_hash = 0x0123456789abcdefULL;
	header.config.layout = TrackLayout::Forward;
	header.config.condition = TrackCondition::Dry;
	header.config.type = SessionType::Heat;
	header.config.kart_class = KartClass::KZ2;
	header.config.limit = SessionLimit::distance_m(3750.0);
	header.config.entry_count = 8;
	header.config.roster_hash = 0x99ULL;
	header.config.assists.auto_clutch = true;
	header.config.assists.auto_shift = false;
	header.config.seed = 42;
	header.stamp();
	return header;
}

// A deterministic spread of inputs that is not a straight line and not symmetric:
// a ramping throttle, a brake that overlaps it, a steering trace that changes sign,
// and shift edges on a couple of ticks. Snapped, because a producer snaps.
DriverInput sample_input(int tick) {
	DriverInput raw;
	raw.throttle = 0.5 + 0.5 * std::sin(static_cast<double>(tick) * 0.031);
	raw.brake = tick % 40 < 6 ? 0.83 : 0.0;
	raw.steer = 0.62 * std::sin(static_cast<double>(tick) * 0.017) -
			0.11 * std::cos(static_cast<double>(tick) * 0.004);
	raw.clutch = tick % 97 < 3 ? 1.0 : 0.0;
	raw.shift_up = tick % 53 == 0;
	raw.shift_down = tick % 71 == 0;
	return replay_snap(raw);
}

} // namespace

// --- the grids -----------------------------------------------------------------

TEST_CASE("every code round-trips exactly, which is what a storage grid has to do") {
	// The whole quantization argument rests on `encode(decode(code)) == code` for
	// every code, so it is checked for every code rather than for a sample of them.
	// A `code / N` grid gives this by construction; a decimal grid like
	// `StateHash`'s 1e-4 does not, because 1e-4 is not a binary fraction.
	int unit_failures = 0;
	int first_unit_failure = -1;
	for (int code = 0; code <= REPLAY_UNIT_CODES; ++code) {
		const double value = replay_decode_unit(static_cast<uint16_t>(code));
		if (replay_encode_unit(value) != static_cast<uint16_t>(code)) {
			if (first_unit_failure < 0) {
				first_unit_failure = code;
			}
			++unit_failures;
		}
		// And snapping an already-snapped value is a no-op, bitwise. This is the
		// property `replay_is_on_grid` tests with `==`.
		if (replay_snap_unit(value) != value) {
			++unit_failures;
		}
	}
	CHECK(first_unit_failure == -1);
	CHECK(unit_failures == 0);

	int steer_failures = 0;
	for (int code = -REPLAY_STEER_CODES; code <= REPLAY_STEER_CODES; ++code) {
		const double value = replay_decode_steer(static_cast<int16_t>(code));
		if (replay_encode_steer(value) != static_cast<int16_t>(code)) {
			++steer_failures;
		}
		if (replay_snap_steer(value) != value) {
			++steer_failures;
		}
	}
	CHECK(steer_failures == 0);
}

TEST_CASE("the steer grid is symmetric, so full lock is the same magnitude both ways") {
	// The asymmetric encoding — scaling by 32768 to use the whole int16 range — makes
	// -1.0 exact and +1.0 unreachable. `chassis.h` already records that a scenario
	// which only turns one way measures half a kart; this is the same defect
	// available for free in an encoder.
	CHECK(replay_encode_steer(1.0) == REPLAY_STEER_CODES);
	CHECK(replay_encode_steer(-1.0) == -REPLAY_STEER_CODES);
	CHECK(replay_snap_steer(1.0) == 1.0);
	CHECK(replay_snap_steer(-1.0) == -1.0);
	CHECK(replay_snap_steer(0.0) == 0.0);
	for (int code = 1; code <= REPLAY_STEER_CODES; ++code) {
		const double right = replay_decode_steer(static_cast<int16_t>(code));
		const double left = replay_decode_steer(static_cast<int16_t>(-code));
		REQUIRE(right == -left);
	}
	// Out of range clamps rather than wrapping, which is the difference between a
	// producer bug and a kart that suddenly steers the other way.
	CHECK(replay_encode_steer(4.0) == REPLAY_STEER_CODES);
	CHECK(replay_encode_steer(-4.0) == -REPLAY_STEER_CODES);
	CHECK(replay_encode_unit(-3.0) == 0);
	CHECK(replay_encode_unit(3.0) == REPLAY_UNIT_CODES);
}

TEST_CASE("the steer grid is finer than the smallest input the hardware can produce") {
	// The grid is chosen against `src/vehicle/kart_body.h`'s measurement rather than
	// rounded to something tidy, so the measurement is re-derived here and the grid is
	// asserted against it. If the curve or the deadzone move, this test says so.
	//
	// STEER_GAMMA and DEADZONE are duplicated from `kart_body.h`, which needs
	// godot-cpp and so cannot be included here (ADR-0017). `test_vehicle.cpp` carries
	// the same duplication and says so in the same words.
	const double STEER_GAMMA = 3.0;
	const double DEADZONE = 0.15;
	const double STICK_STEP = 1.0 / 127.0; // an 8-bit pad axis, one side of center
	const double FOLLOWABLE_LOCK = 0.065; // 37.5 m at 100 km/h, kart_body.h

	// The worst case is the bottom of the range, because x^3 compresses hardest
	// there: one step of the stick just above the deadzone is the smallest non-zero
	// change in lock the hardware can ask for.
	const double edge = std::pow(DEADZONE, STEER_GAMMA);
	const double next = std::pow(DEADZONE + STICK_STEP, STEER_GAMMA);
	const double smallest_step = next - edge;
	const double codes_per_step = smallest_step / REPLAY_STEER_QUANTUM;
	const double codes_in_band = FOLLOWABLE_LOCK / REPLAY_STEER_QUANTUM;

	std::printf("\n    the steer grid against the measurement\n");
	std::printf("      %-46s %10.3e\n", "grid quantum, of lock", REPLAY_STEER_QUANTUM);
	std::printf("      %-46s %10.3e\n", "smallest post-curve step a stick can ask",
			smallest_step);
	std::printf("      %-46s %10.1f\n", "which is this many codes", codes_per_step);
	std::printf("      %-46s %10.1f\n", "codes across the whole followable band",
			codes_in_band);

	// Every stick position the hardware can report has to be a distinct code, at the
	// worst point in the mapping. Eight is an arbitrary floor; the measured figure is
	// 18, so this has margin rather than sitting on the limit.
	CHECK(codes_per_step > 8.0);
	// And the band that produces cornering rather than scrub needs real resolution
	// inside it, not two or three steps.
	CHECK(codes_in_band > 1000.0);
	// A 1e-4 grid — `StateHash`'s — would give only 5.6 codes per stick step, which is
	// the check that would have caught a round number chosen by eye.
	CHECK(smallest_step / 1e-4 < codes_per_step);
}

// --- the body ------------------------------------------------------------------

TEST_CASE("a body of ticks decodes bit-identically when the input was on the grid") {
	const int ticks = 500;
	const int karts = 4;
	static unsigned char body[500 * 4 * REPLAY_INPUT_BYTES];

	int written = 0;
	for (int tick = 0; tick < ticks; ++tick) {
		DriverInput field[4];
		for (int kart = 0; kart < karts; ++kart) {
			field[kart] = sample_input(tick * karts + kart);
			REQUIRE(replay_is_on_grid(field[kart]));
		}
		const int count = replay_encode_tick(field, karts, body + written,
				static_cast<int>(sizeof(body)) - written);
		REQUIRE(count == karts * REPLAY_INPUT_BYTES);
		written += count;
	}
	CHECK(written == ticks * karts * REPLAY_INPUT_BYTES);

	// **Exact equality, not a tolerance.** The guarantee is that the same double
	// comes back out; "the same to within something" is what the trap looks like.
	int mismatches = 0;
	for (int tick = 0; tick < ticks; ++tick) {
		DriverInput decoded[4];
		REQUIRE(replay_decode_tick(body + tick * karts * REPLAY_INPUT_BYTES,
				karts * REPLAY_INPUT_BYTES, karts, decoded));
		for (int kart = 0; kart < karts; ++kart) {
			const DriverInput expected = sample_input(tick * karts + kart);
			if (decoded[kart].throttle != expected.throttle ||
					decoded[kart].brake != expected.brake ||
					decoded[kart].steer != expected.steer ||
					decoded[kart].clutch != expected.clutch ||
					decoded[kart].shift_up != expected.shift_up ||
					decoded[kart].shift_down != expected.shift_down) {
				++mismatches;
			}
		}
	}
	CHECK(mismatches == 0);

	// The stride is fixed, so kart 2 of tick 300 is reachable without an index.
	DriverInput one;
	const int offset = (300 * karts + 2) * REPLAY_INPUT_BYTES;
	REQUIRE(replay_decode_input(body + offset, REPLAY_INPUT_BYTES, one));
	CHECK(one.steer == sample_input(300 * karts + 2).steer);
}

TEST_CASE("input that is not already on the grid is refused, not rounded") {
	// This is the whole of ADR-0041's trap, made structural. If the recorder rounded
	// here, the live run would have consumed the unrounded value and the replay the
	// rounded one, and the two would diverge for a reason that looks like a solver
	// bug. So the only function that can write a body byte refuses.
	unsigned char record[REPLAY_INPUT_BYTES];

	DriverInput off_grid;
	off_grid.throttle = 0.42; // 0.42 * 65535 = 27524.7, so 0.42 is not a code
	REQUIRE_FALSE(replay_is_on_grid(off_grid));
	CHECK(replay_encode_input(off_grid, record, REPLAY_INPUT_BYTES) == -1);

	// And the snapped version of the same struct is accepted.
	const DriverInput snapped = replay_snap(off_grid);
	CHECK(replay_is_on_grid(snapped));
	CHECK(replay_encode_input(snapped, record, REPLAY_INPUT_BYTES) == REPLAY_INPUT_BYTES);
	// Snapping is idempotent, so a producer that snaps twice has not moved anything.
	CHECK(replay_snap(snapped).throttle == snapped.throttle);

	SUBCASE("each axis independently") {
		for (int axis = 0; axis < 4; ++axis) {
			DriverInput input;
			double *field[4] = { &input.throttle, &input.brake, &input.clutch, &input.steer };
			*field[axis] = 0.42;
			CHECK_FALSE(replay_is_on_grid(input));
			CHECK(replay_encode_input(input, record, REPLAY_INPUT_BYTES) == -1);
		}
	}

	SUBCASE("a NaN is a producer bug and cannot reach the file") {
		DriverInput input;
		input.throttle = std::nan("");
		// NaN never equals its own snapped value, so this falls out of the same check
		// rather than needing its own.
		CHECK_FALSE(replay_is_on_grid(input));
		CHECK(replay_encode_input(input, record, REPLAY_INPUT_BYTES) == -1);
	}

	SUBCASE("out of range is refused rather than silently clamped into the file") {
		DriverInput input;
		input.throttle = 1.4;
		CHECK_FALSE(replay_is_on_grid(input));
		CHECK(replay_encode_input(input, record, REPLAY_INPUT_BYTES) == -1);
		input.throttle = 0.0;
		input.steer = -1.2;
		CHECK_FALSE(replay_is_on_grid(input));
		CHECK(replay_encode_input(input, record, REPLAY_INPUT_BYTES) == -1);
	}

	SUBCASE("a short buffer is refused") {
		const DriverInput neutral;
		CHECK(replay_encode_input(neutral, record, REPLAY_INPUT_BYTES - 1) == -1);
	}
}

TEST_CASE("the shift requests are bits, and a reserved bit is an error") {
	unsigned char record[REPLAY_INPUT_BYTES];
	DriverInput input;
	input.shift_up = true;
	input.shift_down = false;
	REQUIRE(replay_encode_input(input, record, REPLAY_INPUT_BYTES) == REPLAY_INPUT_BYTES);
	CHECK(record[8] == REPLAY_FLAG_SHIFT_UP);

	DriverInput decoded;
	REQUIRE(replay_decode_input(record, REPLAY_INPUT_BYTES, decoded));
	CHECK(decoded.shift_up);
	CHECK_FALSE(decoded.shift_down);

	// A v2 body that added a flag must not be misread by a v1 reader as a v1 body.
	record[8] |= 0x40;
	CHECK_FALSE(replay_decode_input(record, REPLAY_INPUT_BYTES, decoded));

	// The one steer code the encoder cannot emit is rejected on the way back in.
	unsigned char forged[REPLAY_INPUT_BYTES] = {};
	forged[6] = 0x00;
	forged[7] = 0x80; // -32768
	CHECK_FALSE(replay_decode_input(forged, REPLAY_INPUT_BYTES, decoded));
}

// --- the header ----------------------------------------------------------------

TEST_CASE("the header round-trips to byte-identical text") {
	ReplayHeader header = recorded_header();
	// With a tuning diff in it, because the diff is `format_entry`'s output and the
	// round trip has to survive the preset format too.
	const int gamma = tunable_by_key("steer_gamma");
	const int torsion = tunable_by_key("frame_torsion");
	REQUIRE(gamma >= 0);
	REQUIRE(torsion >= 0);
	header.config.tuning.set(gamma, 2.4);
	header.config.tuning.set(torsion, 210.0);
	header.stamp();

	static char first[REPLAY_HEADER_CHARS];
	static char second[REPLAY_HEADER_CHARS];
	const int first_length = replay_format_header(header, first, REPLAY_HEADER_CHARS);
	REQUIRE(first_length > 0);

	ReplayHeader parsed;
	const ReplayParse parse = replay_parse_header(first, first_length, parsed);
	if (!parse.ok) {
		std::printf("    parse failed on line %d: %s (%s)\n", parse.line,
				replay_parse_problem_name(parse.problem), parse.key);
	}
	REQUIRE(parse.ok);

	const int second_length = replay_format_header(parsed, second, REPLAY_HEADER_CHARS);
	REQUIRE(second_length == first_length);
	CHECK(std::memcmp(first, second, static_cast<size_t>(first_length)) == 0);

	// And every field came back, which byte-identical text implies but does not say.
	CHECK(std::strcmp(parsed.config.track_id, "autumn_ridge") == 0);
	CHECK(parsed.config.track_hash == 0x0123456789abcdefULL);
	CHECK(parsed.config.type == SessionType::Heat);
	CHECK(parsed.config.entry_count == 8);
	CHECK(parsed.config.assists.auto_clutch);
	CHECK_FALSE(parsed.config.assists.auto_shift);
	CHECK(parsed.config.seed == 42);
	CHECK(parsed.tick_count == 3600);
	CHECK(parsed.tick_hz == 120);
	CHECK(std::strcmp(parsed.build, "8f1c4a2b6d0e") == 0);
	CHECK(std::strcmp(parsed.api_version, "4.7.1") == 0);
	CHECK(parsed.config.limit.kind == SessionLimitKind::Distance);
	CHECK(tuning_micro(parsed.config.limit.value) == tuning_micro(3750.0));
	CHECK(tuning_micro(parsed.config.tuning.get(gamma)) == tuning_micro(2.4));
	CHECK(tuning_micro(parsed.config.tuning.get(torsion)) == tuning_micro(210.0));

	// **The hash survives the text.** Both sides quantize on the same 1e-6 grid that
	// `format_value` prints, so this is by construction — but it is the single
	// property everything downstream trusts, so it is asserted rather than argued.
	CHECK(parsed.config.hash() == header.config.hash());
	CHECK(parsed.config_hash == header.config_hash);

	std::printf("\n    a replay header is %d bytes of text (%d tunables moved)\n", first_length,
			header.config.tuning.changed_count());
	std::printf("%s", first);
}

TEST_CASE("an untouched tuning set writes no diff lines at all") {
	const ReplayHeader header = recorded_header();
	static char text[REPLAY_HEADER_CHARS];
	const int length = replay_format_header(header, text, REPLAY_HEADER_CHARS);
	REQUIRE(length > 0);
	// A preset is a diff, so nothing tuned means no entry lines — no `=` outside the
	// comment block that explains what an `=` would mean.
	const char *body = std::strstr(text, "format 1");
	REQUIRE(body != nullptr);
	CHECK(std::strchr(body, '=') == nullptr);

	ReplayHeader parsed;
	REQUIRE(replay_parse_header(text, length, parsed).ok);
	CHECK(parsed.config.tuning.changed_count() == 0);
	CHECK(parsed.config.hash() == header.config.hash());
}

TEST_CASE("a parse names the line and the key rather than saying it failed") {
	ReplayHeader parsed;

	SUBCASE("an unknown header key") {
		const char *text = "format 1\nnonsense 3\n";
		const ReplayParse parse = replay_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == ReplayParseProblem::UnknownKey);
		CHECK(parse.line == 2);
		CHECK(std::strcmp(parse.key, "nonsense") == 0);
	}
	SUBCASE("a bad hex value") {
		const char *text = "format 1\ntrack_hash beef\n";
		const ReplayParse parse = replay_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == ReplayParseProblem::BadValue);
		CHECK(std::strcmp(parse.key, "track_hash") == 0);
	}
	SUBCASE("a tuning entry this build does not have") {
		const char *text = "format 1\nno_such_tunable = 1.000000\n";
		const ReplayParse parse = replay_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == ReplayParseProblem::UnknownTunable);
		CHECK(std::strcmp(parse.key, "no_such_tunable") == 0);
	}
	SUBCASE("a required field that never appeared") {
		// A defaulted `tick_hz` is a silently different simulation, so a missing
		// field is refused rather than filled in. ADR-0042 rejects a best-effort
		// load for a save; a replay has even less claim to guess.
		const char *text = "format 1\ntrack autumn_ridge\n";
		const ReplayParse parse = replay_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == ReplayParseProblem::MissingField);
		CHECK(std::strcmp(parse.key, "tick_hz") == 0);
	}
	SUBCASE("a config_hash that disagrees with the configuration above it") {
		ReplayHeader header = recorded_header();
		static char text[REPLAY_HEADER_CHARS];
		const int length = replay_format_header(header, text, REPLAY_HEADER_CHARS);
		REQUIRE(length > 0);
		// Move one hex digit of the recorded hash.
		char *hash_line = std::strstr(text, "config_hash 0x");
		REQUIRE(hash_line != nullptr);
		hash_line[14] = hash_line[14] == 'f' ? 'e' : 'f';
		const ReplayParse parse = replay_parse_header(text, length, parsed);
		CHECK_FALSE(parse.ok);
		// A corrupt header is not a changed configuration, and the two need
		// different sentences.
		CHECK(parse.problem == ReplayParseProblem::ConfigHashMismatch);
	}
}

// --- playback ------------------------------------------------------------------

TEST_CASE("the three outcomes are three things") {
	const ReplayHeader recorded = recorded_header();
	char message[REPLAY_MESSAGE_CHARS];

	SUBCASE("passed") {
		const ReplayHeader live = recorded_header();
		PlaybackReport report = replay_admit(recorded, live);
		REQUIRE(report.verdict == PlaybackVerdict::Passed);
		CHECK(report.reason == RefusalReason::None);
		CHECK_FALSE(report.warned());
		for (uint64_t tick = 0; tick < 3600; tick += 120) {
			replay_compare_checkpoint(report, tick, 0xabc0000ULL + tick, 0xabc0000ULL + tick);
		}
		CHECK(report.verdict == PlaybackVerdict::Passed);
		CHECK(report.checkpoints_compared == 30);
		REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
		CHECK(contains(message, "passed"));
	}

	SUBCASE("diverged, which is the only outcome that means a determinism bug") {
		const ReplayHeader live = recorded_header();
		PlaybackReport report = replay_admit(recorded, live);
		REQUIRE(report.verdict == PlaybackVerdict::Passed);
		replay_compare_checkpoint(report, 0, 0x1111ULL, 0x1111ULL);
		replay_compare_checkpoint(report, 120, 0x2222ULL, 0x2222ULL);
		replay_compare_checkpoint(report, 240, 0x3333ULL, 0x4444ULL);
		// A later disagreement does not overwrite the first: everything after the
		// first divergence is downstream of the same cause.
		replay_compare_checkpoint(report, 360, 0x5555ULL, 0x6666ULL);
		CHECK(report.verdict == PlaybackVerdict::Diverged);
		CHECK(report.diverged_tick == 240);
		CHECK(report.recorded_hash == 0x3333ULL);
		CHECK(report.live_hash == 0x4444ULL);
		REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
		CHECK(contains(message, "diverged"));
		CHECK(contains(message, "determinism bug"));
	}

	SUBCASE("refused, and it is not a flavor of diverged") {
		ReplayHeader live = recorded_header();
		live.config.seed = 43;
		live.stamp();
		PlaybackReport report = replay_admit(recorded, live);
		REQUIRE(report.verdict == PlaybackVerdict::Refused);
		CHECK(report.reason == RefusalReason::ConfigMismatch);

		// **The structural claim.** A caller that keeps feeding checkpoints into a
		// refused report cannot turn it into a determinism failure, so a stale
		// replay can never be reported as one. ADR-0041's consequence section says
		// CI must not fold refused into red; this is what makes that possible.
		replay_compare_checkpoint(report, 0, 0xdeadULL, 0xbeefULL);
		CHECK(report.verdict == PlaybackVerdict::Refused);
		CHECK(report.checkpoints_compared == 0);
		CHECK(report.diverged_tick == 0);
	}
}

TEST_CASE("a config mismatch names the field that moved") {
	const ReplayHeader recorded = recorded_header();
	char message[REPLAY_MESSAGE_CHARS];

	SUBCASE("track_id") {
		ReplayHeader live = recorded_header();
		live.config.set_track_id("summer_loop");
		live.stamp();
		const PlaybackReport report = replay_admit(recorded, live);
		CHECK(report.verdict == PlaybackVerdict::Refused);
		CHECK(report.reason == RefusalReason::ConfigMismatch);
		CHECK(report.field == SessionField::TrackId);
		CHECK(report.field == first_difference(recorded.config, live.config));
		REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
		CHECK(contains(message, "refused"));
		CHECK(contains(message, "track_id"));
		CHECK(contains(message, "autumn_ridge"));
		CHECK(contains(message, "summer_loop"));
	}

	SUBCASE("track_hash, which is a corner that was smoothed") {
		ReplayHeader live = recorded_header();
		live.config.track_hash ^= 1ULL;
		live.stamp();
		const PlaybackReport report = replay_admit(recorded, live);
		CHECK(report.verdict == PlaybackVerdict::Refused);
		CHECK(report.field == SessionField::TrackHash);
	}

	SUBCASE("assists, because they change the lap") {
		ReplayHeader live = recorded_header();
		live.config.assists.auto_shift = true;
		live.stamp();
		const PlaybackReport report = replay_admit(recorded, live);
		CHECK(report.verdict == PlaybackVerdict::Refused);
		CHECK(report.field == SessionField::AutoShift);
		REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
		CHECK(contains(message, "auto_shift"));
		CHECK(contains(message, "off"));
		CHECK(contains(message, "on"));
	}

	SUBCASE("tuning, and the message names the tunable rather than the word tuning") {
		// This is ADR-0041's own example sentence, and the reason the ADR is not
		// satisfied by naming `tuning`: "recorded with frame_torsion at 210.0" is
		// actionable and "the tuning differs" is not. `session.h` deliberately
		// reports tuning as one field and leaves the per-key answer to `tuning.h`;
		// `replay_admit` is where the two are joined.
		ReplayHeader recorded_tuned = recorded_header();
		const int torsion = tunable_by_key("frame_torsion");
		REQUIRE(torsion >= 0);
		recorded_tuned.config.tuning.set(torsion, 210.0);
		recorded_tuned.stamp();

		const ReplayHeader live = recorded_header(); // at its default, 193.62
		const PlaybackReport report = replay_admit(recorded_tuned, live);
		CHECK(report.verdict == PlaybackVerdict::Refused);
		CHECK(report.reason == RefusalReason::ConfigMismatch);
		CHECK(report.field == SessionField::Tuning);
		CHECK(report.tunable_id == torsion);
		REQUIRE(replay_describe(report, recorded_tuned, live, message, sizeof(message)) > 0);
		CHECK(contains(message, "frame_torsion"));
		CHECK(contains(message, "210.000000"));
		CHECK(contains(message, "193.620000"));
		std::printf("\n    ADR-0041's sentence, as the code actually prints it:\n      %s\n",
				message);
	}
}

TEST_CASE("a format version mismatch is refused, never migrated") {
	ReplayHeader recorded = recorded_header();
	recorded.format_version = REPLAY_FORMAT_VERSION + 1;
	const ReplayHeader live = recorded_header();
	const PlaybackReport report = replay_admit(recorded, live);
	CHECK(report.verdict == PlaybackVerdict::Refused);
	CHECK(report.reason == RefusalReason::FormatVersion);
	// Checked before anything else: a header this build cannot read cannot be
	// trusted to say what configuration it was recorded under.
	CHECK(report.field == SessionField::None);

	char message[REPLAY_MESSAGE_CHARS];
	REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
	CHECK(contains(message, "refused"));
	CHECK(contains(message, "not migrated"));

	// And an older one too. A replay is a diagnostic artifact; `ghost.h` takes the
	// opposite line for user data, per ADR-0042.
	recorded.format_version = REPLAY_FORMAT_VERSION - 1;
	CHECK(replay_admit(recorded, live).reason == RefusalReason::FormatVersion);
}

TEST_CASE("a different build warns and still plays") {
	ReplayHeader recorded = recorded_header();
	ReplayHeader live = recorded_header();
	live.set_build("0000ffff1234");

	PlaybackReport report = replay_admit(recorded, live);
	// Cross-build bit determinism is not claimed anywhere in this project, so
	// refusing here would assert a guarantee that does not exist.
	CHECK(report.verdict == PlaybackVerdict::Passed);
	CHECK(report.reason == RefusalReason::None);
	CHECK(report.build_warning);
	CHECK_FALSE(report.api_warning);
	CHECK(report.admitted());
	CHECK(report.warned());

	// It plays, and it can still reach either of the other two outcomes.
	replay_compare_checkpoint(report, 0, 0x1ULL, 0x1ULL);
	CHECK(report.verdict == PlaybackVerdict::Passed);

	char message[REPLAY_MESSAGE_CHARS];
	REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
	CHECK(contains(message, "passed"));
	CHECK(contains(message, "Warning"));
	CHECK(contains(message, "cross-build determinism is not claimed"));

	SUBCASE("and so does a different extension API version") {
		live.set_build(recorded.build);
		live.set_api_version("4.8.0");
		const PlaybackReport api_report = replay_admit(recorded, live);
		CHECK(api_report.verdict == PlaybackVerdict::Passed);
		CHECK(api_report.api_warning);
		CHECK_FALSE(api_report.build_warning);
	}

	SUBCASE("a recorder that did not know its own commit is still a replay") {
		ReplayHeader anonymous = recorded_header();
		anonymous.set_build("");
		live.set_api_version(recorded.api_version);
		const PlaybackReport blank = replay_admit(anonymous, live);
		CHECK(blank.verdict == PlaybackVerdict::Passed);
		CHECK(blank.build_warning);
	}
}

TEST_CASE("the tick rate is checked separately, because config_hash cannot see it") {
	// The finding this test exists for: `SessionConfig` does not carry the physics
	// tick rate, so a replay recorded at 120 Hz and re-simulated at 240 Hz has an
	// **identical** config_hash and a completely different lap. ADR-0041's two
	// outcomes would classify that as "config matches, state hash diverges", which
	// the ADR calls a real determinism bug — and it is nothing of the sort.
	const ReplayHeader recorded = recorded_header();
	ReplayHeader live = recorded_header();
	live.tick_hz = 240;

	CHECK(recorded.config.hash() == live.config.hash()); // the hole, demonstrated

	const PlaybackReport report = replay_admit(recorded, live);
	CHECK(report.verdict == PlaybackVerdict::Refused);
	CHECK(report.reason == RefusalReason::TickRate);

	char message[REPLAY_MESSAGE_CHARS];
	REQUIRE(replay_describe(report, recorded, live, message, sizeof(message)) > 0);
	CHECK(contains(message, "120"));
	CHECK(contains(message, "240"));
}

TEST_CASE("a header whose own hash disagrees with its config is corrupt, not changed") {
	ReplayHeader recorded = recorded_header();
	recorded.config_hash ^= 0x40ULL;
	const ReplayHeader live = recorded_header();
	const PlaybackReport report = replay_admit(recorded, live);
	CHECK(report.verdict == PlaybackVerdict::Refused);
	CHECK(report.reason == RefusalReason::HeaderCorrupt);
}

TEST_CASE("a truncated body is its own refusal") {
	const ReplayHeader header = recorded_header();
	CHECK(replay_body_is_complete(header, header.body_bytes()));
	CHECK_FALSE(replay_body_is_complete(header, header.body_bytes() - 1));

	PlaybackReport report = replay_admit(header, header);
	REQUIRE(report.verdict == PlaybackVerdict::Passed);
	replay_refuse_truncated(report);
	CHECK(report.verdict == PlaybackVerdict::Refused);
	CHECK(report.reason == RefusalReason::TruncatedBody);
	// Refused stays refused.
	replay_compare_checkpoint(report, 0, 1, 2);
	CHECK(report.verdict == PlaybackVerdict::Refused);
}

// --- the footer ----------------------------------------------------------------

TEST_CASE("checkpoints round-trip, and the footer comparison finds the first divergence") {
	unsigned char bytes[REPLAY_CHECKPOINT_BYTES];
	const ReplayCheckpoint point{ 108000ULL, 0xfedcba9876543210ULL };
	REQUIRE(replay_encode_checkpoint(point, bytes, sizeof(bytes)) == REPLAY_CHECKPOINT_BYTES);
	ReplayCheckpoint decoded;
	REQUIRE(replay_decode_checkpoint(bytes, sizeof(bytes), decoded));
	CHECK(decoded.tick == point.tick);
	CHECK(decoded.hash == point.hash);

	const ReplayHeader header = recorded_header();
	ReplayCheckpoint recorded[30];
	ReplayCheckpoint live[30];
	for (int i = 0; i < 30; ++i) {
		recorded[i].tick = static_cast<uint64_t>(i) * 120ULL;
		recorded[i].hash = 0x9000ULL + static_cast<uint64_t>(i);
		live[i] = recorded[i];
	}
	live[17].hash ^= 1ULL;
	live[23].hash ^= 1ULL;

	PlaybackReport report = replay_admit(header, header);
	replay_compare_footer(report, recorded, 30, live, 30);
	CHECK(report.verdict == PlaybackVerdict::Diverged);
	CHECK(report.diverged_tick == 17 * 120);
	CHECK(report.checkpoints_compared == 30);

	SUBCASE("a checkpoint at a tick the recording does not have is a harness bug, reported") {
		PlaybackReport shifted_report = replay_admit(header, header);
		ReplayCheckpoint shifted[30];
		for (int i = 0; i < 30; ++i) {
			shifted[i] = recorded[i];
		}
		shifted[4].tick += 1;
		replay_compare_footer(shifted_report, recorded, 30, shifted, 30);
		CHECK(shifted_report.verdict == PlaybackVerdict::Diverged);
		CHECK(shifted_report.diverged_tick == recorded[4].tick + 1);
	}
}

TEST_CASE("the checkpoint arithmetic matches the interval") {
	ReplayHeader header = recorded_header();
	header.tick_count = 108000;
	header.hash_interval = 120;
	// One per simulated second over 15 minutes.
	CHECK(header.checkpoint_count() == 900);
	CHECK(header.is_checkpoint_tick(0));
	CHECK(header.is_checkpoint_tick(120));
	CHECK_FALSE(header.is_checkpoint_tick(119));

	header.tick_count = 121;
	CHECK(header.checkpoint_count() == 2); // ticks 0 and 120
	header.tick_count = 120;
	CHECK(header.checkpoint_count() == 1);
	header.tick_count = 0;
	CHECK(header.checkpoint_count() == 0);
}

// --- the size claim ------------------------------------------------------------

TEST_CASE("ADR-0041's under-8-MB claim is arithmetic, so it is checked") {
	// The ADR's own numbers: "a 15-minute round is 108,000 ticks, and eight karts of
	// uncompressed float input is about 20 MB. Quantized to fixed point it is under
	// 8 MB." Both halves are re-derived here rather than trusted.
	const uint64_t ticks = 108000;
	const int karts = 8;

	ReplayHeader header = recorded_header();
	header.tick_count = ticks;
	header.config.entry_count = karts;
	header.hash_interval = REPLAY_HASH_INTERVAL;
	header.stamp();

	const uint64_t body = header.body_bytes();
	const uint64_t footer = header.footer_bytes();
	static char text[REPLAY_HEADER_CHARS];
	const int header_bytes = replay_format_header(header, text, REPLAY_HEADER_CHARS);
	REQUIRE(header_bytes > 0);
	const uint64_t total = body + footer + static_cast<uint64_t>(header_bytes);

	// What the ADR is comparing against: six 32-bit floats a record.
	const uint64_t as_floats = ticks * static_cast<uint64_t>(karts) * 24ULL;

	const double lap_time_s = 48.0;
	const uint64_t lap_ticks = static_cast<uint64_t>(lap_time_s * 120.0);
	const uint64_t lap_bytes_one_kart = lap_ticks * REPLAY_INPUT_BYTES;

	std::printf("\n    replay size, measured\n");
	std::printf("      %-46s %10d\n", "bytes per tick per kart", REPLAY_INPUT_BYTES);
	std::printf("      %-46s %10llu\n", "bytes per tick, 8 karts",
			static_cast<unsigned long long>(karts * REPLAY_INPUT_BYTES));
	std::printf("      %-46s %10llu\n", "bytes per lap, one kart (48 s, 120 Hz)",
			static_cast<unsigned long long>(lap_bytes_one_kart));
	std::printf("      %-46s %10d\n", "header, text", header_bytes);
	std::printf("      %-46s %10llu\n", "footer, 900 checkpoints",
			static_cast<unsigned long long>(footer));
	std::printf("      %-46s %10llu\n", "body, 108,000 ticks x 8 karts",
			static_cast<unsigned long long>(body));
	std::printf("      %-46s %10llu  (%.2f MB)\n", "total for a 15-minute round",
			static_cast<unsigned long long>(total), static_cast<double>(total) / 1.0e6);
	std::printf("      %-46s %10llu  (%.2f MB)\n", "the same as six float32 a record",
			static_cast<unsigned long long>(as_floats), static_cast<double>(as_floats) / 1.0e6);
	std::printf("      %-46s %10.2f %%\n", "footer as a fraction of the body",
			100.0 * static_cast<double>(footer) / static_cast<double>(body));

	// The claim, both halves.
	CHECK(total < 8000000ULL);
	CHECK(as_floats > 20000000ULL);
	CHECK(as_floats < 21000000ULL);
	// And it is true without much room: an eight-byte record would have been
	// comfortable and a ten-byte one would have broken the claim, which is worth
	// knowing before anyone adds a field to `DriverInput`.
	CHECK(ticks * static_cast<uint64_t>(karts) * 10ULL > 8000000ULL);
	std::printf("      %-46s %10.2f %%\n", "headroom under the 8 MB claim",
			100.0 * (1.0 - static_cast<double>(total) / 8.0e6));
}
