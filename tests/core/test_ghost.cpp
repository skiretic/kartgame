#include "doctest.h"

#include "core/ghost.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// ADR-0041's second format. The two things worth testing are the two things a ghost
// is for: that it interpolates to where the kart actually was, and that it is small
// enough to keep one per track per layout per class without the profile getting fat.
//
// Same tolerance discipline as `test_replay.cpp`: no `doctest::Approx` on anything
// whose magnitude is not order 1, because `epsilon` is not a relative tolerance.
// Interpolation errors below are compared as explicit absolute distances in meters,
// which is the unit the claim is made in.

using namespace kart::core;

namespace {

bool contains(const char *haystack, const char *needle) {
	return std::strstr(haystack, needle) != nullptr;
}

GhostHeader lap_header() {
	GhostHeader header;
	header.set_track_id("autumn_ridge");
	header.track_hash = 0x0123456789abcdefULL;
	header.layout = TrackLayout::Forward;
	header.kart_class = KartClass::KZ2;
	header.sample_hz = GHOST_SAMPLE_HZ;
	header.sample_count = 1440;
	header.lap_time_s = 47.912;
	header.sector_count = 3;
	header.sector_split_s[0] = 15.204;
	header.sector_split_s[1] = 31.880;
	header.sector_split_s[2] = 47.912; // cumulative, so the last is the lap time
	header.tuning_hash = default_tuning_hash();
	return header;
}

} // namespace

// --- quantization --------------------------------------------------------------

TEST_CASE("a sample round-trips to within its own grid") {
	GhostSample sample;
	sample.position = Vec3(123.4567891, -4.0009999, 1088.0000004);
	sample.yaw = 2.9;
	sample.pitch = -0.031;
	sample.roll = 0.0177;

	unsigned char bytes[GHOST_SAMPLE_BYTES];
	REQUIRE(ghost_encode_sample(sample, bytes, sizeof(bytes)) == GHOST_SAMPLE_BYTES);
	GhostSample decoded;
	REQUIRE(ghost_decode_sample(bytes, sizeof(bytes), decoded));

	// A millimeter, exactly — the grid, not a tolerance chosen to make this pass.
	CHECK(std::fabs(decoded.position.x - sample.position.x) <= 0.5 * GHOST_POSITION_QUANTUM);
	CHECK(std::fabs(decoded.position.y - sample.position.y) <= 0.5 * GHOST_POSITION_QUANTUM);
	CHECK(std::fabs(decoded.position.z - sample.position.z) <= 0.5 * GHOST_POSITION_QUANTUM);

	const double angle_quantum = PI / static_cast<double>(GHOST_ANGLE_CODES);
	CHECK(std::fabs(decoded.yaw - sample.yaw) <= 0.5 * angle_quantum);
	CHECK(std::fabs(decoded.pitch - sample.pitch) <= 0.5 * angle_quantum);
	CHECK(std::fabs(decoded.roll - sample.roll) <= 0.5 * angle_quantum);

	// And **encoding an already-snapped sample is exact**, which is what makes the
	// stream the same stream on a second machine. A ghost may be quantized on write
	// — nothing re-simulates it — but it still has to be stable once written.
	const GhostSample snapped = ghost_snap(sample);
	unsigned char again[GHOST_SAMPLE_BYTES];
	REQUIRE(ghost_encode_sample(snapped, again, sizeof(again)) == GHOST_SAMPLE_BYTES);
	CHECK(std::memcmp(bytes, again, GHOST_SAMPLE_BYTES) == 0);
	CHECK(ghost_snap(snapped).position.x == snapped.position.x);
	CHECK(ghost_snap(snapped).yaw == snapped.yaw);

	std::printf("\n    ghost sample grid\n");
	std::printf("      %-46s %10.4f mm\n", "position quantum", GHOST_POSITION_QUANTUM * 1000.0);
	std::printf("      %-46s %10.4f deg\n", "angle quantum", angle_quantum * 180.0 / PI);
}

TEST_CASE("every angle code round-trips, and the encoding is symmetric") {
	int failures = 0;
	for (int code = -GHOST_ANGLE_CODES; code <= GHOST_ANGLE_CODES; ++code) {
		const double angle = ghost_decode_angle(static_cast<int16_t>(code));
		if (ghost_encode_angle(angle) != static_cast<int16_t>(code)) {
			++failures;
		}
	}
	CHECK(failures == 0);
	CHECK(ghost_encode_angle(0.0) == 0);
	// A kart that leaned 1/32768 further one way than the other would not be
	// symmetric about its own centerline. Same argument as `replay.h`'s steer axis.
	for (int code = 1; code <= GHOST_ANGLE_CODES; ++code) {
		REQUIRE(ghost_decode_angle(static_cast<int16_t>(code)) ==
				-ghost_decode_angle(static_cast<int16_t>(-code)));
	}
	// The one code the encoder cannot emit is rejected on the way back in.
	unsigned char forged[GHOST_SAMPLE_BYTES] = {};
	forged[12] = 0x00;
	forged[13] = 0x80;
	GhostSample decoded;
	CHECK_FALSE(ghost_decode_sample(forged, GHOST_SAMPLE_BYTES, decoded));
}

TEST_CASE("yaw wraps, so a ghost crossing the seam does not spin a whole turn") {
	// The failure this prevents: a kart heading at +3.14 rad and the next sample at
	// -3.14 rad is 0.003 rad of yaw, and a naive lerp draws 6.28 rad of it in a
	// thirtieth of a second.
	const double before = PI - 0.01;
	const double after = -PI + 0.01;
	CHECK(std::fabs(ghost_angle_delta(before, after) - 0.02) < 1e-9);
	const double half = ghost_lerp_angle(before, after, 0.5);
	// Halfway across the seam is at the seam, either sign.
	CHECK(std::fabs(std::fabs(half) - PI) < 1e-9);
	// And a lerp that ignored the wrap would land near zero, which is the bug.
	CHECK(std::fabs(half) > 3.0);

	CHECK(std::fabs(ghost_wrap_angle(3.0 * PI) - PI) < 1e-9);
	CHECK(std::fabs(ghost_wrap_angle(-3.0 * PI) + PI) < 1e-9);
	CHECK(ghost_wrap_angle(0.0) == 0.0);
}

// --- interpolation -------------------------------------------------------------

TEST_CASE("interpolation passes through the samples it was built from") {
	// Catmull-Rom's whole reason for being here over a plain cubic: a sample is a place
	// the kart actually was, and a curve that only came near it would draw the ghost
	// through the inside of a hairpin.
	GhostSample samples[8];
	for (int i = 0; i < 8; ++i) {
		const double t = static_cast<double>(i);
		samples[i].position = Vec3(t * 3.0, 0.4 * std::sin(t), t * t * 0.2);
		samples[i].yaw = 0.3 * t - 1.0;
	}
	for (int i = 0; i < 8; ++i) {
		const double time_s = static_cast<double>(i) / GHOST_SAMPLE_HZ;
		const GhostSample at = ghost_sample_at(samples, 8, time_s);
		CHECK(std::fabs(at.position.x - samples[i].position.x) < 1e-9);
		CHECK(std::fabs(at.position.y - samples[i].position.y) < 1e-9);
		CHECK(std::fabs(at.position.z - samples[i].position.z) < 1e-9);
		CHECK(std::fabs(ghost_angle_delta(at.yaw, samples[i].yaw)) < 1e-9);
	}
}

TEST_CASE("a straight line interpolates exactly, at any point between samples") {
	// The degenerate case that catches a wrong basis weight: Catmull-Rom through
	// collinear, evenly spaced points is the line itself.
	GhostSample samples[6];
	for (int i = 0; i < 6; ++i) {
		const double t = static_cast<double>(i);
		samples[i].position = Vec3(2.5 * t, 1.0, -7.0 * t);
	}
	double worst = 0.0;
	for (int step = 0; step <= 100; ++step) {
		const double u = static_cast<double>(step) / 100.0 * 5.0; // in samples
		const GhostSample at = ghost_sample_at(samples, 6, u / GHOST_SAMPLE_HZ);
		worst = std::fmax(worst, std::fabs(at.position.x - 2.5 * u));
		worst = std::fmax(worst, std::fabs(at.position.z + 7.0 * u));
	}
	CHECK(worst < 1e-9);
}

TEST_CASE("30 Hz is chosen against the interpolation error, and the error is measured") {
	// The claim in `ghost.h`: at the worst sustained lateral acceleration a ghost will
	// be drawn through — the test track's 30 m sweeper at about 20 m/s, so 13.3 m/s^2 —
	// linear interpolation cuts the corner by `a h^2 / 8` and Catmull-Rom is better
	// again. Both are measured here against the exact arc, because a sagitta formula in
	// a comment is a model and this project does not take a model's word for it.
	const double radius = 30.0;
	const double speed = 20.0;
	const double omega = speed / radius; // rad/s
	const double lateral_a = speed * speed / radius;

	auto arc_at = [&](double time_s) {
		return Vec3(radius * std::sin(omega * time_s), 0.0,
				radius * (1.0 - std::cos(omega * time_s)));
	};

	struct Row {
		int hz;
		double linear_mm;
		double spline_mm;
	};
	Row rows[4] = { { 10, 0.0, 0.0 }, { 15, 0.0, 0.0 }, { 30, 0.0, 0.0 }, { 60, 0.0, 0.0 } };

	for (int r = 0; r < 4; ++r) {
		const int hz = rows[r].hz;
		const int count = hz * 2; // two seconds of corner
		static GhostSample samples[256];
		for (int i = 0; i < count; ++i) {
			samples[i].position = arc_at(static_cast<double>(i) / hz);
		}
		double worst_spline = 0.0;
		double worst_linear = 0.0;
		// Probe between samples, away from the clamped ends where the tangents are
		// deliberately zero.
		for (int i = 2; i < count - 3; ++i) {
			for (int sub = 1; sub < 8; ++sub) {
				const double t = static_cast<double>(sub) / 8.0;
				const double time_s = (static_cast<double>(i) + t) / hz;
				const Vec3 truth = arc_at(time_s);

				const GhostSample interpolated = ghost_sample_at(samples, count, time_s, hz);
				worst_spline = std::fmax(worst_spline,
						(interpolated.position - truth).length());

				const Vec3 linear = samples[i].position * (1.0 - t) +
						samples[i + 1].position * t;
				worst_linear = std::fmax(worst_linear, (linear - truth).length());
			}
		}
		rows[r].linear_mm = worst_linear * 1000.0;
		rows[r].spline_mm = worst_spline * 1000.0;
	}

	std::printf("\n    interpolation error on a %.0f m corner at %.0f m/s (%.1f m/s^2)\n", radius,
			speed, lateral_a);
	std::printf("      %6s %14s %14s %14s\n", "Hz", "linear mm", "sagitta mm", "Catmull-Rom mm");
	for (int r = 0; r < 4; ++r) {
		const double h = 1.0 / rows[r].hz;
		const double sagitta_mm = lateral_a * h * h / 8.0 * 1000.0;
		std::printf("      %6d %14.3f %14.3f %14.4f\n", rows[r].hz, rows[r].linear_mm,
				sagitta_mm, rows[r].spline_mm);
	}

	// The sagitta model in the header comment predicts the measured linear error, to
	// within 5%. An explicit relative comparison, not `Approx`.
	for (int r = 0; r < 4; ++r) {
		const double h = 1.0 / rows[r].hz;
		const double sagitta_mm = lateral_a * h * h / 8.0 * 1000.0;
		const double relative = std::fabs(rows[r].linear_mm - sagitta_mm) / sagitta_mm;
		CHECK(relative < 0.05);
	}

	// 30 Hz linear is under 2 mm, which is the number the header quotes.
	CHECK(rows[2].linear_mm < 2.0);
	CHECK(rows[2].linear_mm > 1.5);
	// And the spline is an order better, which is the margin that makes 30 Hz a
	// choice rather than a limit.
	CHECK(rows[2].spline_mm < rows[2].linear_mm / 10.0);
	// 10 Hz is where a ghost starts visibly clipping a corner: over a centimeter.
	CHECK(rows[0].linear_mm > 10.0);
}

TEST_CASE("out of range clamps rather than extrapolating") {
	GhostSample samples[3];
	samples[0].position = Vec3(0.0, 0.0, 0.0);
	samples[1].position = Vec3(1.0, 0.0, 0.0);
	samples[2].position = Vec3(2.0, 0.0, 0.0);

	// Before the start and after the finish there is nothing; extrapolating would
	// draw a kart accelerating away from the finish line forever.
	CHECK(ghost_sample_at(samples, 3, -5.0).position.x == 0.0);
	CHECK(ghost_sample_at(samples, 3, 0.0).position.x == 0.0);
	CHECK(ghost_sample_at(samples, 3, 999.0).position.x == 2.0);
	// A one-sample ghost is a standing start and nothing else.
	CHECK(ghost_sample_at(samples, 1, 4.0).position.x == 0.0);
	// And a caller with no samples gets a defined answer rather than a read off the
	// end of an array.
	CHECK(ghost_sample_at(nullptr, 0, 1.0).position.x == 0.0);
	CHECK(ghost_sample_at(samples, 0, 1.0).position.x == 0.0);
}

// --- the header ----------------------------------------------------------------

TEST_CASE("the ghost header round-trips to byte-identical text") {
	const GhostHeader header = lap_header();
	REQUIRE(header.is_valid());

	char first[GHOST_HEADER_CHARS];
	char second[GHOST_HEADER_CHARS];
	const int first_length = ghost_format_header(header, first, GHOST_HEADER_CHARS);
	REQUIRE(first_length > 0);

	GhostHeader parsed;
	const GhostParse parse = ghost_parse_header(first, first_length, parsed);
	if (!parse.ok) {
		std::printf("    parse failed on line %d: %s (%s)\n", parse.line,
				ghost_parse_problem_name(parse.problem), parse.key);
	}
	REQUIRE(parse.ok);

	const int second_length = ghost_format_header(parsed, second, GHOST_HEADER_CHARS);
	REQUIRE(second_length == first_length);
	CHECK(std::memcmp(first, second, static_cast<size_t>(first_length)) == 0);

	CHECK(std::strcmp(parsed.track_id, "autumn_ridge") == 0);
	CHECK(parsed.track_hash == 0x0123456789abcdefULL);
	CHECK(parsed.layout == TrackLayout::Forward);
	CHECK(parsed.kart_class == KartClass::KZ2);
	CHECK(parsed.sample_hz == GHOST_SAMPLE_HZ);
	CHECK(parsed.sample_count == 1440);
	CHECK(parsed.sector_count == 3);
	CHECK(tuning_micro(parsed.lap_time_s) == tuning_micro(47.912));
	CHECK(tuning_micro(parsed.sector_split_s[1]) == tuning_micro(31.880));
	CHECK(parsed.tuning_hash == default_tuning_hash());

	// A ghost header has no session configuration and no config_hash in it, which is
	// the structural difference from a replay header rather than an omission.
	//
	// Matched at the start of a line, because the comment block *mentions*
	// `config_hash` in order to say the format does not have one — a bare substring
	// search found that and failed, which is the right failure for the wrong reason.
	CHECK_FALSE(contains(first, "\nconfig_hash "));
	CHECK_FALSE(contains(first, "\nseed "));
	CHECK_FALSE(contains(first, "\nentries "));

	// And the positive form of the same claim: the parser has no such field, so a
	// file offering one is an unknown key rather than a silently ignored line.
	GhostHeader unused;
	const char *with_config_hash =
			"format 1\ntrack autumn_ridge\ntrack_hash 0x1\nlayout forward\nclass kz2\n"
			"sample_hz 30\nsamples 100\nlap 47.912000\nsectors 0\ntuning_hash 0x2\n"
			"config_hash 0x3\n";
	const GhostParse rejected = ghost_parse_header(with_config_hash, -1, unused);
	CHECK_FALSE(rejected.ok);
	CHECK(rejected.problem == GhostParseProblem::UnknownKey);
	CHECK(std::strcmp(rejected.key, "config_hash") == 0);

	std::printf("\n    a ghost header is %d bytes of text\n", first_length);
	std::printf("%s", first);
}

TEST_CASE("splits are cumulative and have to agree with the lap time") {
	GhostHeader header = lap_header();
	REQUIRE(header.is_valid());

	// A split that goes backwards is corruption, not a fast sector.
	header.sector_split_s[1] = 9.0;
	CHECK_FALSE(header.is_valid());

	header = lap_header();
	// The last cumulative split *is* the lap time. Storing durations instead would
	// accumulate a rounding error down the lap; storing splits that disagree with the
	// lap time is a timing screen that contradicts itself.
	header.sector_split_s[2] = 47.0;
	CHECK_FALSE(header.is_valid());

	header = lap_header();
	header.sector_count = 0; // no sector marks authored on this track yet
	CHECK(header.is_valid());

	header = lap_header();
	header.sector_count = GHOST_MAX_SECTORS + 1;
	CHECK_FALSE(header.is_valid());

	header = lap_header();
	header.lap_time_s = 0.0;
	CHECK_FALSE(header.is_valid());
}

TEST_CASE("a ghost parse names the line and the key") {
	GhostHeader parsed;

	SUBCASE("a declared sector count with no split lines") {
		const char *text = "format 1\ntrack autumn_ridge\ntrack_hash 0x1\nlayout forward\n"
						   "class kz2\nsample_hz 30\nsamples 100\nlap 47.912000\nsectors 3\n"
						   "tuning_hash 0x2\n";
		const GhostParse parse = ghost_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == GhostParseProblem::MissingField);
		CHECK(std::strcmp(parse.key, "split") == 0);
	}
	SUBCASE("an unknown key") {
		const char *text = "format 1\ndriver anthony\n";
		const GhostParse parse = ghost_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == GhostParseProblem::UnknownKey);
		CHECK(std::strcmp(parse.key, "driver") == 0);
	}
	SUBCASE("a key = value line, which this format does not have at all") {
		const char *text = "format 1\nlap = 47.912000\n";
		const GhostParse parse = ghost_parse_header(text, -1, parsed);
		CHECK_FALSE(parse.ok);
		CHECK(parse.problem == GhostParseProblem::MalformedLine);
	}
	SUBCASE("a future version parses, so that it can be reported rather than misread") {
		// ADR-0042's rule turned into a parser property: a save always loads far
		// enough to say what it is. A parser that refused here would make
		// "a save always loads" into "unless it fails to parse".
		GhostHeader future = lap_header();
		future.format_version = GHOST_FORMAT_VERSION + 1;
		char text[GHOST_HEADER_CHARS];
		const int length = ghost_format_header(future, text, GHOST_HEADER_CHARS);
		REQUIRE(length > 0);
		const GhostParse parse = ghost_parse_header(text, length, parsed);
		CHECK(parse.ok);
		CHECK(parsed.format_version == GHOST_FORMAT_VERSION + 1);
		// And the judgement happens in `ghost_admit`, not in the parser.
		CHECK_FALSE(ghost_can_migrate(parsed.format_version));
	}
}

// --- comparability -------------------------------------------------------------

TEST_CASE("a ghost is compared on what makes a lap comparable, and nothing else") {
	const GhostHeader recorded = lap_header();

	SUBCASE("the same track, layout and class is comparable with no caveat") {
		const GhostHeader live = lap_header();
		const GhostReport report = ghost_admit(recorded, live);
		CHECK(report.verdict == GhostVerdict::Comparable);
		CHECK(report.problem == GhostProblem::None);
		CHECK(report.drawable());
		CHECK_FALSE(report.track_changed);
		CHECK_FALSE(report.tuning_differs);
	}

	SUBCASE("another track, layout or class is meaningless") {
		GhostHeader live = lap_header();
		live.set_track_id("summer_loop");
		CHECK(ghost_admit(recorded, live).problem == GhostProblem::Track);
		CHECK_FALSE(ghost_admit(recorded, live).drawable());

		live = lap_header();
		live.layout = TrackLayout::Reverse;
		CHECK(ghost_admit(recorded, live).problem == GhostProblem::Layout);

		live = lap_header();
		live.kart_class = KartClass::OK;
		CHECK(ghost_admit(recorded, live).problem == GhostProblem::Class);
	}

	SUBCASE("a changed track file warns, where a replay refuses") {
		// The asymmetry is the point. A replay re-simulates against the collision
		// geometry, so a smoothed corner invalidates it. A ghost never touches the
		// geometry: the lap time is no longer comparable, but the line still is, and
		// hiding a best lap because a curb moved is worse than labeling it.
		GhostHeader live = lap_header();
		live.track_hash ^= 1ULL;
		const GhostReport report = ghost_admit(recorded, live);
		CHECK(report.verdict == GhostVerdict::Warned);
		CHECK(report.problem == GhostProblem::None);
		CHECK(report.drawable());
		CHECK(report.track_changed);
	}

	SUBCASE("a lap set under a preset is labeled, not rejected") {
		GhostHeader live = lap_header();
		live.tuning_hash ^= 0x99ULL;
		const GhostReport report = ghost_admit(recorded, live);
		CHECK(report.verdict == GhostVerdict::Warned);
		CHECK(report.tuning_differs);
		CHECK(report.drawable());
	}

	SUBCASE("a newer format cannot be migrated forward, only guessed at") {
		GhostHeader future = lap_header();
		future.format_version = GHOST_FORMAT_VERSION + 1;
		const GhostReport report = ghost_admit(future, lap_header());
		CHECK(report.verdict == GhostVerdict::Incomparable);
		CHECK(report.problem == GhostProblem::FutureFormat);
	}

	SUBCASE("a malformed header is incomparable rather than drawn from garbage") {
		GhostHeader broken = lap_header();
		broken.sample_count = 0;
		const GhostReport report = ghost_admit(broken, lap_header());
		CHECK(report.verdict == GhostVerdict::Incomparable);
		CHECK(report.problem == GhostProblem::Malformed);
	}

	SUBCASE("an older format migrates and always loads, per ADR-0042") {
		// v1 is the only version, so this is the identity function today. It is here
		// because ADR-0042 is explicit that an identity migration is the right thing
		// to write rather than an argument to have, and because the day v2 arrives
		// this test is the one that says v1 still loads.
		GhostHeader old = lap_header();
		old.format_version = GHOST_FORMAT_VERSION;
		CHECK(ghost_can_migrate(old.format_version));
		CHECK(ghost_migrate(old));
		CHECK(old.format_version == GHOST_FORMAT_VERSION);

		GhostHeader impossible = lap_header();
		impossible.format_version = GHOST_FORMAT_VERSION + 1;
		CHECK_FALSE(ghost_migrate(impossible));
		impossible.format_version = 0;
		CHECK_FALSE(ghost_can_migrate(0));
	}
}

// --- the size claim ------------------------------------------------------------

TEST_CASE("what a ghost costs per lap, measured, against ADR-0042's order of magnitude") {
	// ADR-0042 calls a ghost the largest thing in the profile by an order of
	// magnitude, and uses that to justify referencing it rather than inlining it. The
	// figure is arithmetic, so it is checked.
	const double lap_time_s = 48.0; // GAMEDESIGN.md §4's pace on a 1,200 m circuit

	const int samples = ghost_samples_for_lap(lap_time_s);
	const uint64_t bytes = ghost_bytes_for_lap(lap_time_s);

	GhostHeader header = lap_header();
	header.lap_time_s = lap_time_s;
	header.sector_split_s[0] = 15.2;
	header.sector_split_s[1] = 31.9;
	header.sector_split_s[2] = lap_time_s;
	header.sample_count = samples;
	REQUIRE(header.is_valid());
	char text[GHOST_HEADER_CHARS];
	const int header_bytes = ghost_format_header(header, text, GHOST_HEADER_CHARS);
	REQUIRE(header_bytes > 0);

	// GAMEDESIGN §10's content target: two circuits, two layouts each, two classes.
	const int ghosts_in_a_full_profile = 2 * 2 * 2;
	const uint64_t profile_ghost_bytes =
			static_cast<uint64_t>(ghosts_in_a_full_profile) *
			(bytes + static_cast<uint64_t>(header_bytes));

	// What the same lap costs as a replay body, one kart at 120 Hz. The ratio is
	// exactly the tick stride times the byte ratio: 4 ticks a sample, 9 bytes a tick
	// against 18 a sample.
	const uint64_t replay_lap_bytes =
			static_cast<uint64_t>(lap_time_s * 120.0) * 9ULL;

	std::printf("\n    ghost size, measured\n");
	std::printf("      %-46s %10d\n", "bytes per sample", GHOST_SAMPLE_BYTES);
	std::printf("      %-46s %10d\n", "samples per second", GHOST_SAMPLE_HZ);
	std::printf("      %-46s %10d\n", "bytes per second",
			GHOST_SAMPLE_BYTES * GHOST_SAMPLE_HZ);
	std::printf("      %-46s %10d\n", "samples in a 48 s lap", samples);
	std::printf("      %-46s %10llu  (%.1f kB)\n", "bytes per lap",
			static_cast<unsigned long long>(bytes), static_cast<double>(bytes) / 1000.0);
	std::printf("      %-46s %10d\n", "plus a text header of", header_bytes);
	std::printf("      %-46s %10llu  (%.1f kB)\n", "eight ghosts, a full profile",
			static_cast<unsigned long long>(profile_ghost_bytes),
			static_cast<double>(profile_ghost_bytes) / 1000.0);
	std::printf("      %-46s %10llu  (%.1f kB)\n", "the same lap as a replay body, 1 kart",
			static_cast<unsigned long long>(replay_lap_bytes),
			static_cast<double>(replay_lap_bytes) / 1000.0);
	std::printf("      %-46s %10.2f x\n", "so a ghost is this fraction of a replay",
			static_cast<double>(bytes) / static_cast<double>(replay_lap_bytes));

	// 1,441 samples: one at t = 0 and one every thirtieth of a second after it.
	CHECK(samples == 1441);
	CHECK(bytes == 25938ULL);
	// Half a replay body per lap per kart, and no vehicle solve to run it — which is
	// the trade ADR-0041 is making, stated as a number.
	const double ratio = static_cast<double>(bytes) / static_cast<double>(replay_lap_bytes);
	CHECK(std::fabs(ratio - 0.5) < 0.01);
	// ADR-0042's order of magnitude against a profile of a few kB of text. It is
	// nearer three orders, which is why the ADR is right.
	CHECK(profile_ghost_bytes > 100000ULL);
	// And a whole profile's worth of ghosts still fits in a fraction of one replay,
	// so nothing here is the thing that needs compressing.
	CHECK(profile_ghost_bytes < 8000000ULL);

	CHECK(ghost_samples_for_lap(0.0) == 0);
	CHECK(ghost_samples_for_lap(-1.0) == 0);
	CHECK(ghost_samples_for_lap(48.0, 0) == 0);
	// The stride is exact at 120 Hz, which is what lets a sample sit on a tick
	// boundary instead of being resampled.
	CHECK(GHOST_SAMPLE_HZ * GHOST_TICKS_PER_SAMPLE == 120);
}
