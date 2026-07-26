#include "doctest.h"

#include "core/tuning.h"

#include <cstring>

// The tuning vocabulary is the thing that keeps ARCHITECTURE.md §19's unbounded
// tuning bounded, so what is tested here is not "does a slider move". It is the
// three properties an audit rests on:
//
//   1. the table is internally consistent — every default is inside its own
//      range, every key is unique, every citation exists;
//   2. a value survives the round trip through the text format **exactly**, so
//      a saved preset and a running kart are the same kart; and
//   3. the state hash and the tuning hash stay separate, and a defended
//      override is loud in the file rather than merely recorded.

using kart::core::format_entry;
using kart::core::format_hex64;
using kart::core::format_value;
using kart::core::is_defended;
using kart::core::ParsedLine;
using kart::core::parse_line;
using kart::core::parse_value;
using kart::core::Provenance;
using kart::core::TUNABLE_COUNT;
using kart::core::TUNABLES;
using kart::core::tunable_by_key;
using kart::core::TuningSet;

TEST_CASE("every default is inside its own declared range") {
	// A default outside its range would be clamped the first time anything set
	// it, which is a constant that silently changes value the moment the tuning
	// UI is opened — and it would be blamed on the UI.
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		const auto &t = TUNABLES[id];
		CAPTURE(t.key);
		CHECK(t.min_value < t.max_value);
		CHECK(t.default_value >= t.min_value);
		CHECK(t.default_value <= t.max_value);
		CHECK(t.step > 0.0);
		// Between roughly 10 and 500 presses to cross the range. Wider than that
		// and the knob is useless in a corner; narrower and it cannot be placed.
		const double presses = (t.max_value - t.min_value) / t.step;
		CHECK(presses >= 10.0);
		CHECK(presses <= 500.0);
	}
}

TEST_CASE("every tunable is fully declared and uniquely keyed") {
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		const auto &t = TUNABLES[id];
		CAPTURE(t.key);
		CHECK(t.key != nullptr);
		CHECK(std::strlen(t.key) > 0);
		CHECK(std::strlen(t.label) > 0);
		CHECK(t.unit != nullptr); // may be empty, may not be null
		CHECK(std::strlen(t.home) > 0);
		// A citation is the whole point of the provenance field. An empty one
		// makes `sourced` a claim rather than a reference.
		CHECK(std::strlen(t.citation) > 20);
		// It has to fit the line buffer alongside the rest of the entry.
		CHECK(std::strlen(t.citation) < 380);
		CHECK(tunable_by_key(t.key) == id);
		for (int other = id + 1; other < TUNABLE_COUNT; ++other) {
			CHECK(std::strcmp(t.key, TUNABLES[other].key) != 0);
		}
	}
	CHECK(tunable_by_key("no_such_tunable") == -1);
	CHECK(tunable_by_key(nullptr) == -1);
	// A prefix must not match: "steer_rate" and "steer_ratexyz" are different
	// keys and a loader that confused them would tune the wrong constant.
	CHECK(tunable_by_key("steer_rat") == -1);
	CHECK(tunable_by_key("steer_ratee") == -1);
	CHECK(tunable_by_key("steer_rate_extra", 10) == tunable_by_key("steer_rate"));
}

TEST_CASE("the defended set is exactly the sourced and measured ones") {
	// The rule stated in the header, held as a test so it cannot drift into "and
	// also this one I found annoying".
	CHECK(is_defended(Provenance::Sourced));
	CHECK(is_defended(Provenance::Measured));
	CHECK_FALSE(is_defended(Provenance::Derived));
	CHECK_FALSE(is_defended(Provenance::Unsourced));

	// And at least one of each of the two defended classes exists, because a
	// mechanism with no members is a mechanism nobody has exercised.
	int sourced = 0;
	int measured = 0;
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		sourced += TUNABLES[id].provenance == Provenance::Sourced ? 1 : 0;
		measured += TUNABLES[id].provenance == Provenance::Measured ? 1 : 0;
	}
	CHECK(sourced >= 1);
	CHECK(measured >= 1);
}

TEST_CASE("a fresh set is all defaults and hashes as such") {
	TuningSet set;
	CHECK(set.changed_count() == 0);
	CHECK(set.defended_override_count() == 0);
	CHECK(set.hash() == kart::core::default_tuning_hash());
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		CHECK(set.is_default(id));
		CHECK(set.delta(id) == doctest::Approx(0.0));
	}
}

TEST_CASE("set clamps to the declared range and returns what it stored") {
	// The caller is a thumb on a d-pad, so the end of the range has to be a wall
	// rather than an error, and the return value has to be the truth so the
	// overlay shows what is running.
	TuningSet set;
	const auto &t = TUNABLES[kart::core::TUNE_STEER_GAMMA];
	CHECK(set.set(kart::core::TUNE_STEER_GAMMA, 99.0) == doctest::Approx(t.max_value));
	CHECK(set.set(kart::core::TUNE_STEER_GAMMA, -99.0) == doctest::Approx(t.min_value));
	CHECK(set.get(kart::core::TUNE_STEER_GAMMA) == doctest::Approx(t.min_value));
}

TEST_CASE("a nudge up and back down is default again, exactly") {
	// The reason `is_default` compares micro-units rather than doubles. With a
	// float comparison this is the classic almost-default: the file grows a line
	// saying a constant was tuned to the value it already had.
	TuningSet set;
	const int id = kart::core::TUNE_VOICE_GAIN;
	for (int i = 0; i < 7; ++i) {
		set.nudge(id, 1);
	}
	CHECK_FALSE(set.is_default(id));
	for (int i = 0; i < 7; ++i) {
		set.nudge(id, -1);
	}
	CHECK(set.is_default(id));
	CHECK(set.hash() == kart::core::default_tuning_hash());
}

TEST_CASE("the tuning hash separates configurations and vocabularies") {
	TuningSet a;
	TuningSet b;
	CHECK(a.hash() == b.hash());
	b.set(kart::core::TUNE_NOISE_GAIN, 0.20);
	CHECK(a.hash() != b.hash());
	b.reset(kart::core::TUNE_NOISE_GAIN);
	CHECK(a.hash() == b.hash());

	// Two tunables moved to each other's values must not cancel. The key is
	// hashed alongside the value precisely so this cannot happen.
	TuningSet c;
	TuningSet d;
	c.set(kart::core::TUNE_COMB_DEPTH, 0.42);
	d.set(kart::core::TUNE_NOISE_GAIN, 0.42);
	CHECK(c.hash() != d.hash());
}

TEST_CASE("changed and defended counts answer different questions") {
	TuningSet set;
	set.set(kart::core::TUNE_STEER_GAMMA, 2.4); // derived, free to move
	set.set(kart::core::TUNE_NOISE_GAIN, 0.20); // a guess, free to move
	CHECK(set.changed_count() == 2);
	CHECK(set.defended_override_count() == 0);

	set.set(kart::core::TUNE_FRAME_TORSION, 400.0); // sourced
	CHECK(set.changed_count() == 3);
	CHECK(set.defended_override_count() == 1);
}

TEST_CASE("format_value always writes six decimals and one leading digit") {
	char buffer[kart::core::TUNING_VALUE_CHARS];
	CHECK(format_value(2.4, buffer, sizeof(buffer)) == 8);
	CHECK(std::strcmp(buffer, "2.400000") == 0);
	format_value(-0.6, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "-0.600000") == 0);
	format_value(0.0, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "0.000000") == 0);
	format_value(150.0, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "150.000000") == 0);
	format_value(0.4363323129985824, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "0.436332") == 0);

	// Negative zero prints as zero. Two values that hash alike have to print
	// alike, or a diff shows a change that the running kart does not have.
	format_value(-0.0000004, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "0.000000") == 0);

	// It refuses rather than truncating.
	char tiny[4];
	CHECK(format_value(1.0, tiny, sizeof(tiny)) == -1);
}

TEST_CASE("format_hex64 pads to sixteen digits whatever the leading nibble") {
	// CLAUDE.md records that GDScript's `pad_zeros` counts only digit characters
	// and mangles exactly this, so the C++ side does it by hand and the test
	// pins the all-letters case that broke it.
	char buffer[32];
	CHECK(format_hex64(0xFFFFFFFFFFFFFFFFULL, buffer, sizeof(buffer)) == 18);
	CHECK(std::strcmp(buffer, "0xffffffffffffffff") == 0);
	format_hex64(0, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "0x0000000000000000") == 0);
	format_hex64(0x8f1c4a2b6d0e7391ULL, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "0x8f1c4a2b6d0e7391") == 0);
}

TEST_CASE("parse_value accepts what format_value writes and rejects the rest") {
	double value = 0.0;
	CHECK(parse_value("2.400000", 8, value));
	CHECK(value == doctest::Approx(2.4));
	CHECK(parse_value("-0.600000", 9, value));
	CHECK(value == doctest::Approx(-0.6));
	CHECK(parse_value("3", 1, value));
	CHECK(value == doctest::Approx(3.0));
	CHECK(parse_value(".5", 2, value));
	CHECK(value == doctest::Approx(0.5));

	// A seventh decimal rounds into the sixth rather than being dropped, so a
	// hand-edited file means what it says.
	CHECK(parse_value("0.1234565", 9, value));
	CHECK(kart::core::tuning_micro(value) == 123457);

	// A prefix is not a number. "3.0abc" parsing as 3.0 would be a kart tuned to
	// something nobody wrote down.
	CHECK_FALSE(parse_value("3.0abc", 6, value));
	CHECK_FALSE(parse_value("abc", 3, value));
	CHECK_FALSE(parse_value("", 0, value));
	CHECK_FALSE(parse_value("-", 1, value));
	CHECK_FALSE(parse_value("1.", 2, value));
	CHECK_FALSE(parse_value("1 2", 3, value));
	// A decimal comma, which is what a locale-dependent writer would have
	// produced on somebody else's machine.
	CHECK_FALSE(parse_value("2,400000", 8, value));
}

TEST_CASE("a value round-trips through text exactly, for every tunable") {
	// The property the whole format rests on: what a preset saves is what it
	// loads. Exercised at both ends of every declared range and at a few steps
	// in from each, because the ends are where a clamp or a rounding shows.
	char buffer[kart::core::TUNING_VALUE_CHARS];
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		const auto &t = TUNABLES[id];
		CAPTURE(t.key);
		const double probes[] = {
			t.min_value, t.max_value, t.default_value,
			t.min_value + t.step, t.max_value - t.step,
			t.default_value + 3.0 * t.step, t.default_value - 3.0 * t.step,
		};
		for (double probe : probes) {
			TuningSet set;
			const double stored = set.set(id, probe);
			REQUIRE(format_value(stored, buffer, sizeof(buffer)) > 0);
			double parsed = 0.0;
			int length = 0;
			while (buffer[length] != '\0') {
				++length;
			}
			REQUIRE(parse_value(buffer, length, parsed));
			CHECK(kart::core::tuning_micro(parsed) == kart::core::tuning_micro(stored));
		}
	}
}

TEST_CASE("a default writes no entry, because a preset is a diff") {
	TuningSet set;
	char line[kart::core::TUNING_LINE_CHARS];
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		CHECK(format_entry(set, id, line, sizeof(line)) == -1);
	}
}

TEST_CASE("an undefended entry is a plain line with its delta") {
	TuningSet set;
	set.set(kart::core::TUNE_STEER_GAMMA, 2.4);
	char line[kart::core::TUNING_LINE_CHARS];
	REQUIRE(format_entry(set, kart::core::TUNE_STEER_GAMMA, line, sizeof(line)) > 0);
	CHECK(std::strstr(line, "steer_gamma = 2.400000") == line);
	CHECK(std::strstr(line, "default 3.000000") != nullptr);
	CHECK(std::strstr(line, "-0.600000") != nullptr);
	CHECK(std::strstr(line, "(derived)") != nullptr);
	CHECK(std::strstr(line, "OVERRIDE") == nullptr);
}

TEST_CASE("a defended entry is loud, and the marker is first on the line") {
	// First on the line on purpose: it has to survive a diff hunk, a grep, and a
	// terminal that wrapped the comment off the right-hand edge.
	TuningSet set;
	set.set(kart::core::TUNE_FRAME_TORSION, 400.0);
	char line[kart::core::TUNING_LINE_CHARS];
	REQUIRE(format_entry(set, kart::core::TUNE_FRAME_TORSION, line, sizeof(line)) > 0);
	CHECK(line[0] == '!');
	CHECK(std::strstr(line, "! frame_torsion = 400.000000") == line);
	CHECK(std::strstr(line, "(sourced)") != nullptr);
	CHECK(std::strstr(line, "OVERRIDE: ") != nullptr);
	CHECK(std::strstr(line, "Fu and Wang") != nullptr);
	CHECK(std::strstr(line, "+206.380000") != nullptr);
}

TEST_CASE("every tunable's defended entry fits the line buffer") {
	// The citation is the longest part of the line and it is written by hand, so
	// a long one would silently drop an override out of the saved file.
	char line[kart::core::TUNING_LINE_CHARS];
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		TuningSet set;
		CAPTURE(TUNABLES[id].key);
		set.set(id, TUNABLES[id].max_value);
		if (set.is_default(id)) {
			set.set(id, TUNABLES[id].min_value);
		}
		CHECK(format_entry(set, id, line, sizeof(line)) > 0);
	}
}

TEST_CASE("parse_line classifies the file's five line shapes") {
	CHECK(parse_line("", -1).kind == ParsedLine::Blank);
	CHECK(parse_line("   \t ", -1).kind == ParsedLine::Blank);
	CHECK(parse_line("# a comment", -1).kind == ParsedLine::Comment);
	CHECK(parse_line("   # indented", -1).kind == ParsedLine::Comment);

	const ParsedLine header = parse_line("defaults 0x8f1c4a2b6d0e7391", -1);
	CHECK(header.kind == ParsedLine::Header);
	CHECK(std::strcmp(header.key, "defaults") == 0);
	CHECK(std::strcmp(header.text, "0x8f1c4a2b6d0e7391") == 0);

	const ParsedLine named = parse_line("name hairpin v3", -1);
	CHECK(named.kind == ParsedLine::Header);
	CHECK(std::strcmp(named.text, "hairpin v3") == 0);

	const ParsedLine entry = parse_line("steer_gamma = 2.400000  # default 3.000000", -1);
	CHECK(entry.kind == ParsedLine::Entry);
	CHECK(entry.id == kart::core::TUNE_STEER_GAMMA);
	CHECK(entry.value == doctest::Approx(2.4));
	CHECK_FALSE(entry.defended_marker);

	const ParsedLine defended = parse_line("! frame_torsion = 400.000000  # OVERRIDE: x", -1);
	CHECK(defended.kind == ParsedLine::Entry);
	CHECK(defended.defended_marker);
	CHECK(defended.id == kart::core::TUNE_FRAME_TORSION);
	CHECK(defended.value == doctest::Approx(400.0));
}

TEST_CASE("a carriage return does not become part of a value") {
	// A preset edited on Windows and committed. The line ends "2.400000\r", and
	// a parser that fed that to the number reader would reject the whole file.
	const ParsedLine entry = parse_line("steer_gamma = 2.400000\r\n", -1);
	CHECK(entry.kind == ParsedLine::Entry);
	CHECK(entry.value == doctest::Approx(2.4));
}

TEST_CASE("a malformed entry is Invalid rather than skipped") {
	// A preset that half-loaded would be a kart tuned to something nobody wrote
	// down, which is worse than one that refuses to load.
	CHECK(parse_line("steer_gamma = ", -1).kind == ParsedLine::Invalid);
	CHECK(parse_line("steer_gamma = wobbly", -1).kind == ParsedLine::Invalid);
	CHECK(parse_line("steer_gamma = 2.4 3.4", -1).kind == ParsedLine::Invalid);
	CHECK(parse_line("! ", -1).kind == ParsedLine::Invalid);
	CHECK(parse_line("! defaults 0x00", -1).kind == ParsedLine::Invalid);

	// An unknown key parses as a well-formed entry with no id. The loader has to
	// report it — a renamed constant and a typo look identical here, and both
	// deserve a line on stderr rather than silence.
	const ParsedLine unknown = parse_line("steer_gamme = 2.400000", -1);
	CHECK(unknown.kind == ParsedLine::Entry);
	CHECK(unknown.id == -1);
}

TEST_CASE("a written entry parses back to the value that wrote it") {
	// The end-to-end property, over every tunable: format then parse is the
	// identity on the stored micro-units, comment and all.
	char line[kart::core::TUNING_LINE_CHARS];
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		TuningSet set;
		CAPTURE(TUNABLES[id].key);
		const double target = TUNABLES[id].default_value + 5.0 * TUNABLES[id].step;
		const double stored = set.set(id, target);
		REQUIRE(format_entry(set, id, line, sizeof(line)) > 0);

		const ParsedLine parsed = parse_line(line, -1);
		REQUIRE(parsed.kind == ParsedLine::Entry);
		CHECK(parsed.id == id);
		CHECK(parsed.defended_marker == is_defended(TUNABLES[id].provenance));

		TuningSet loaded;
		loaded.set(parsed.id, parsed.value);
		CHECK(kart::core::tuning_micro(loaded.get(id)) == kart::core::tuning_micro(stored));
		CHECK(loaded.hash() == set.hash());
	}
}
