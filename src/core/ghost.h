#ifndef KART_CORE_GHOST_H
#define KART_CORE_GHOST_H

#include "core/session.h"
#include "core/tuning.h"
#include "core/units.h"
#include "core/vec3.h"

#include <cmath>
#include <cstdint>

// The ghost format: a sampled transform stream with a lap time and its sector
// splits. ADR-0041's second format, and ADR-0042's largest object.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017. Same
// house rules as `replay.h`: allocation-free, locale-free, caller-supplied buffers,
// and **no function here opens a file**.
//
// ## Why this is not `replay.h` with different fields
//
// A replay re-simulates. A ghost is drawn beside a live session that is diverging
// from it by design — that is the entire point of racing one — so re-simulating it
// buys nothing and costs a second vehicle solve every tick. ADR-0041 says this in
// one line and it has three consequences that make the two files genuinely
// different, not two dialects of one:
//
//   1. **A ghost stores results, not causes.** Position and orientation, already
//      solved. There is no input stream, no seed, and no tuning set to apply,
//      because nothing will be integrated from it.
//   2. **A ghost is deliberately compared across configurations.** You race your
//      own best lap from a different session, a different field, a different day.
//      So a ghost has **no `config_hash` and never refuses on one** — a
//      configuration match is exactly the thing it is not asserting. Only the
//      handful of fields that make one lap comparable to another are checked, and
//      the rest are carried as warnings.
//   3. **A ghost may be quantized on write, and a replay may not.** `replay.h`'s
//      whole quantization argument is that a rounded input fed to a solver diverges
//      from the full-precision input the live run consumed. Nothing consumes a
//      ghost but the renderer, so there is no second run to disagree with, and
//      rounding on write is free. `ghost_encode_sample` therefore *does* round,
//      where `replay_encode_input` refuses to. The asymmetry is deliberate and it
//      is the clearest statement of what ADR-0041's trap actually was.
//
// ## Versioning follows ADR-0042, not ADR-0041, and that is a correction
//
// ADR-0041 specifies the ghost and takes a refuse-on-mismatch line, because a
// replay is a diagnostic artifact. But ADR-0042 puts the ghost in `user://ghosts/`,
// referenced by id from `profile.save`, as one half of "best lap and ghost per
// track per layout per class" — which makes a ghost **user data**, and ADR-0042 is
// explicit that refusing to load user data is deleting it with extra steps.
//
// A refused ghost is a deleted best lap. So this file takes ADR-0042's policy:
//
//   * An **older** format version migrates forward and always loads. v1 is the
//     only version, so the chain is currently the identity function, and ADR-0042
//     is explicit that an identity migration is the correct thing to write rather
//     than a version bump to argue about.
//   * A **newer** version than this build knows cannot be migrated — that is not
//     migration, it is guessing — and is reported as unreadable rather than
//     misread. A ghost drawn from a stream this build does not understand is a kart
//     driving through the scenery.
//
// The two ADRs disagree here and the disagreement is worth a line in the log; see
// the report that accompanied this work.
//
// ## The sampling rate, and what a sample costs
//
// **30 Hz, every fourth tick at `project.godot`'s 120 Hz.** Sampling on a tick
// boundary rather than on a wall-clock interval means no resampling: sample `i` is
// exactly tick `4i`, its time is exactly `i / 30`, and the stream inherits the
// simulation's own clock instead of a renderer's.
//
// The rate is chosen against the interpolation error, not picked round. Linear
// interpolation of a curved path is short by its sagitta, `a h^2 / 8`. The worst
// sustained lateral acceleration a ghost will be drawn through is the test track's
// 30 m sweeper — `test_track.tscn`'s T3, and ARCHITECTURE's §6.4 figures put the
// kart around 20 m/s there, so 13.3 m/s^2. At 30 Hz that is
//
//     13.3 * (1/30)^2 / 8 = 1.8 mm
//
// of cut corner, which is a fifth of a tire's contact patch width and invisible at
// any camera distance. At 15 Hz it would be 7.4 mm and at 10 Hz 16.6 mm, which is
// where a ghost starts visibly clipping the inside of a corner. Catmull-Rom, which
// is what `ghost_sample_at` actually uses, is an order better again — the error term
// is fourth order in `h` rather than second — so 30 Hz has margin rather than
// sitting on its limit.
//
// **A sample is 18 bytes**: three int32 at 1 mm, three int16 over -pi..pi. At 30 Hz
// that is 540 bytes per second, and a 48 s lap on a 1,200 m circuit — the pace
// `GAMEDESIGN.md` §4 assumes — is 1,440 samples and **25,920 bytes**. The whole of
// GAMEDESIGN §10's content target, two circuits times two layouts times two classes,
// is eight ghosts and 207 kB, against a `profile.save` of a few kB of text. ADR-0042
// calls a ghost the largest thing in the profile by an order of magnitude; it is
// nearer three, which is exactly why the ADR is right that it must be referenced
// rather than inlined.
//
// ## Why 1 mm and not a float
//
// A float32 loses precision with distance from the origin: at 1,500 m from the
// start line its spacing is already 122 um, which is fine — so this is not a
// precision argument. It is a determinism-of-storage argument of the same kind as
// `replay.h`'s: a fixed grid means a ghost written on one machine and read on
// another is the same ghost, byte for byte, and `int32` at 1 mm covers +/- 2,147 km
// with no exponent to lose. A millimeter is a tenth of what the eye can resolve on
// a kart 5 m away and a hundredth of the 1.8 mm interpolation error above, so it is
// not the term that limits anything.

namespace kart::core {

// --- version and shape ---------------------------------------------------------

inline constexpr int GHOST_FORMAT_VERSION = 1;

// Samples per second, and the tick stride that produces it at 120 Hz.
inline constexpr int GHOST_SAMPLE_HZ = 30;
inline constexpr int GHOST_TICKS_PER_SAMPLE = 4;

// Position: signed millimeters. Angles: -pi..pi over 32,767 codes, symmetric for
// the same reason `replay.h`'s steer axis is — a ghost that leaned 1/32768 further
// one way than the other would be a kart that is not symmetric about its own
// centerline.
inline constexpr double GHOST_POSITION_SCALE = 1000.0; // codes per meter
inline constexpr double GHOST_POSITION_QUANTUM = 1.0 / GHOST_POSITION_SCALE;
inline constexpr int GHOST_ANGLE_CODES = 32767;

inline constexpr int GHOST_SAMPLE_BYTES = 18;

// A ceiling on the sector count, not a claim about how many there are. ADR-0046
// puts sector marks in `track.json` and is explicit that their number is data; this
// exists only so a parse that produced garbage can be rejected. Three is the
// ordinary number.
inline constexpr int GHOST_MAX_SECTORS = 8;

// One sample, decoded. Orientation as three angles rather than a quaternion or a
// basis: a ghost is drawn, not simulated, and `vec3.h`'s position is that a
// transform type invites code which has stopped saying what frame it is in. Yaw is
// the one that matters and the one that wraps; pitch and roll are there because a
// kart hopping a curb that stayed dead level would read as a bug in the ghost.
struct GhostSample {
	Vec3 position; // world, meters
	double yaw = 0.0; // radians, -pi..pi, wraps
	double pitch = 0.0; // radians
	double roll = 0.0; // radians
};

// --- quantization --------------------------------------------------------------

inline int32_t ghost_encode_position(double meters) {
	if (std::isnan(meters)) {
		return 0;
	}
	double scaled = meters * GHOST_POSITION_SCALE;
	if (scaled > 2147483000.0) {
		scaled = 2147483000.0;
	}
	if (scaled < -2147483000.0) {
		scaled = -2147483000.0;
	}
	return static_cast<int32_t>(std::llround(scaled));
}

inline double ghost_decode_position(int32_t code) {
	return static_cast<double>(code) / GHOST_POSITION_SCALE;
}

// Wrap to -pi..pi before encoding, so a yaw that has accumulated turns across a lap
// stores the same code as the heading it actually is.
//
// **A closed interval, with both -pi and +pi reachable**, and that is not a detail.
// The obvious form — `fmod(x + pi, 2pi) - pi` — is half-open and maps +pi onto -pi,
// so `ghost_decode_angle(32767)` returns +pi and re-encoding it returns -32767. The
// stream would then fail to round-trip at exactly one code, and the symptom is a
// ghost pointing the wrong way for one sample when a lap happens to face due south.
inline double ghost_wrap_angle(double radians) {
	if (std::isnan(radians)) {
		return 0.0;
	}
	const double two_pi = 2.0 * PI;
	double wrapped = std::fmod(radians, two_pi);
	if (wrapped > PI) {
		wrapped -= two_pi;
	} else if (wrapped < -PI) {
		wrapped += two_pi;
	}
	return wrapped;
}

inline int16_t ghost_encode_angle(double radians) {
	const double wrapped = ghost_wrap_angle(radians);
	long long code = std::llround(wrapped / PI * static_cast<double>(GHOST_ANGLE_CODES));
	if (code > GHOST_ANGLE_CODES) {
		code = GHOST_ANGLE_CODES;
	}
	if (code < -GHOST_ANGLE_CODES) {
		code = -GHOST_ANGLE_CODES;
	}
	return static_cast<int16_t>(code);
}

inline double ghost_decode_angle(int16_t code) {
	return static_cast<double>(code) / static_cast<double>(GHOST_ANGLE_CODES) * PI;
}

// **Rounds, deliberately.** See the header note: a ghost is never integrated from,
// so quantizing on write cannot produce the divergence `replay.h` exists to
// prevent. The recorder hands over whatever the solver produced.
inline int ghost_encode_sample(const GhostSample &sample, unsigned char *out, int cap) {
	if (out == nullptr || cap < GHOST_SAMPLE_BYTES) {
		return -1;
	}
	const int32_t position[3] = {
		ghost_encode_position(sample.position.x),
		ghost_encode_position(sample.position.y),
		ghost_encode_position(sample.position.z),
	};
	for (int axis = 0; axis < 3; ++axis) {
		const uint32_t bits = static_cast<uint32_t>(position[axis]);
		for (int byte = 0; byte < 4; ++byte) {
			out[axis * 4 + byte] = static_cast<unsigned char>((bits >> (byte * 8)) & 0xFFU);
		}
	}
	const int16_t angles[3] = {
		ghost_encode_angle(sample.yaw),
		ghost_encode_angle(sample.pitch),
		ghost_encode_angle(sample.roll),
	};
	for (int i = 0; i < 3; ++i) {
		const uint16_t bits = static_cast<uint16_t>(angles[i]);
		out[12 + i * 2] = static_cast<unsigned char>(bits & 0xFFU);
		out[13 + i * 2] = static_cast<unsigned char>((bits >> 8) & 0xFFU);
	}
	return GHOST_SAMPLE_BYTES;
}

inline bool ghost_decode_sample(const unsigned char *in, int len, GhostSample &out) {
	if (in == nullptr || len < GHOST_SAMPLE_BYTES) {
		return false;
	}
	int32_t position[3] = {};
	for (int axis = 0; axis < 3; ++axis) {
		uint32_t bits = 0;
		for (int byte = 0; byte < 4; ++byte) {
			bits |= static_cast<uint32_t>(in[axis * 4 + byte]) << (byte * 8);
		}
		position[axis] = static_cast<int32_t>(bits);
	}
	int16_t angles[3] = {};
	for (int i = 0; i < 3; ++i) {
		const uint16_t bits =
				static_cast<uint16_t>(in[12 + i * 2] | (in[13 + i * 2] << 8));
		angles[i] = static_cast<int16_t>(bits);
		if (angles[i] < -GHOST_ANGLE_CODES) {
			// -32768 is the one code the encoder cannot produce.
			return false;
		}
	}
	out.position = Vec3(ghost_decode_position(position[0]), ghost_decode_position(position[1]),
			ghost_decode_position(position[2]));
	out.yaw = ghost_decode_angle(angles[0]);
	out.pitch = ghost_decode_angle(angles[1]);
	out.roll = ghost_decode_angle(angles[2]);
	return true;
}

inline GhostSample ghost_snap(const GhostSample &sample) {
	GhostSample snapped;
	snapped.position = Vec3(ghost_decode_position(ghost_encode_position(sample.position.x)),
			ghost_decode_position(ghost_encode_position(sample.position.y)),
			ghost_decode_position(ghost_encode_position(sample.position.z)));
	snapped.yaw = ghost_decode_angle(ghost_encode_angle(sample.yaw));
	snapped.pitch = ghost_decode_angle(ghost_encode_angle(sample.pitch));
	snapped.roll = ghost_decode_angle(ghost_encode_angle(sample.roll));
	return snapped;
}

// --- interpolation -------------------------------------------------------------

// Shortest arc between two angles. The reason yaw cannot be lerped naively: a kart
// crossing the -pi/+pi seam would spin a whole turn in one sample interval.
inline double ghost_angle_delta(double from, double to) {
	return ghost_wrap_angle(to - from);
}

inline double ghost_lerp_angle(double from, double to, double t) {
	return ghost_wrap_angle(from + ghost_angle_delta(from, to) * t);
}

// Uniform Catmull-Rom through `b` and `c`, with `a` and `d` as the neighbors that
// set the tangents. Passes through every sample — which matters, because a sample is
// a place the kart actually was and a curve that missed it would draw a ghost
// through the scenery on a hairpin.
inline Vec3 ghost_spline(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, double t) {
	const double t2 = t * t;
	const double t3 = t2 * t;
	// The standard basis, tension 1/2.
	const double w0 = -0.5 * t3 + t2 - 0.5 * t;
	const double w1 = 1.5 * t3 - 2.5 * t2 + 1.0;
	const double w2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
	const double w3 = 0.5 * t3 - 0.5 * t2;
	return a * w0 + b * w1 + c * w2 + d * w3;
}

// Where the ghost was at `time_s` into the lap.
//
// Positions go through Catmull-Rom, angles through the shortest arc linearly. The
// split is not laziness: a Catmull-Rom over a wrapping quantity needs the sequence
// unwrapped first, and the error it would save is not worth the unwrapping. Yaw
// acceleration in a corner entry is around 7 rad/s^2, so linear interpolation is
// off by `7 * (1/30)^2 / 8 = 9.7e-4` rad — 0.056 degrees, an order below the 0.0055
// degree storage quantum times ten and far below anything visible on a kart 5 m
// away.
//
// Out of range clamps to the ends rather than extrapolating. A ghost has a first and
// a last sample and there is nothing before or after them; extrapolating would draw
// a kart accelerating away from the finish line forever.
inline GhostSample ghost_sample_at(const GhostSample *samples, int count, double time_s,
		int sample_hz = GHOST_SAMPLE_HZ) {
	GhostSample result;
	if (samples == nullptr || count < 1 || sample_hz < 1) {
		return result;
	}
	if (count == 1) {
		return samples[0];
	}
	double position = time_s * static_cast<double>(sample_hz);
	if (position <= 0.0) {
		return samples[0];
	}
	const double last = static_cast<double>(count - 1);
	if (position >= last) {
		return samples[count - 1];
	}
	const int index = static_cast<int>(std::floor(position));
	const double t = position - static_cast<double>(index);

	const int i1 = index;
	const int i2 = index + 1 < count ? index + 1 : count - 1;

	// **The phantom points at the ends are reflected, not duplicated**, and the
	// difference is visible on every lap. Duplicating the endpoint — `p0 = p1` — does
	// not give a zero tangent, it gives a tangent of half the correct magnitude, so
	// the first and last segments bow away from the path: measured at 0.52 m on a
	// straight line in `tests/core/test_ghost.cpp`, which is most of a kart's width.
	// Reflecting — `p0 = 2*p1 - p2` — makes a straight run **exactly** straight, and
	// that half is measured at precisely zero error.
	//
	// It does **not** give the right tangent at the ends, and this comment used to
	// claim it did. On a curve the reflected phantom sits on the extension of the
	// chord, missing the true previous point by `2R(1 - cos(w*h))` — 14.8 mm on the
	// 30 m corner at 20 m/s this file sizes against — and `w0(t)` carries a fraction
	// of that into the first segment. Measured **1.1530 mm** against a closed-form
	// prediction of 1.0850 mm: third order in the step where the interior is fourth.
	//
	// It stays as it is, and the reason is a number rather than a shrug: 1.15 mm is
	// inside the 1 mm storage quantum's own rounding and 480 times better than the
	// 0.52 m the duplicated endpoint produced. Fixing it properly wants the sample
	// before the line, which is a cyclic stream this format does not carry. Recorded
	// here because a flying lap starts mid-corner as often as not, so this is the
	// error's real operating point rather than a corner case.
	//
	// And the ends are not a standing start. A best lap is a **flying** lap: the first
	// sample is already at racing speed across the line, so a tangent that was
	// deliberately wrong there would stutter the ghost at the one place a driver is
	// looking at it, every single lap.
	const Vec3 p1 = samples[i1].position;
	const Vec3 p2 = samples[i2].position;
	const Vec3 p0 = index > 0 ? samples[index - 1].position : p1 + (p1 - p2);
	const Vec3 p3 = index + 2 < count ? samples[index + 2].position : p2 + (p2 - p1);

	result.position = ghost_spline(p0, p1, p2, p3, t);
	result.yaw = ghost_lerp_angle(samples[i1].yaw, samples[i2].yaw, t);
	result.pitch = ghost_lerp_angle(samples[i1].pitch, samples[i2].pitch, t);
	result.roll = ghost_lerp_angle(samples[i1].roll, samples[i2].roll, t);
	return result;
}

// --- the header ----------------------------------------------------------------

// What makes one lap comparable to another, plus the times.
//
// **No `SessionConfig` and no `config_hash`**, which is the structural difference
// from `ReplayHeader` — see the file note. What is here is the smallest set of
// fields that decide whether two lap times mean the same thing: the same circuit,
// the same direction round it, and the same class of kart. The tuning hash rides
// along so that a lap set on a preset can be labeled, not rejected.
struct GhostHeader {
	int format_version = GHOST_FORMAT_VERSION;

	char track_id[SESSION_ID_CHARS] = {};

	// Content hash of `track.json`. A **warning** rather than a refusal, unlike the
	// replay's — see `ghost_admit`.
	uint64_t track_hash = 0;

	TrackLayout layout = TrackLayout::Forward;
	KartClass kart_class = KartClass::KZ2;

	int sample_hz = GHOST_SAMPLE_HZ;
	int sample_count = 0;

	double lap_time_s = 0.0;

	int sector_count = 0;
	// **Cumulative** split times, not per-sector durations: a split is what the
	// timing screen shows and the last one equals the lap time. Per-sector durations
	// are a subtraction away, and the other direction accumulates a rounding error
	// down the lap.
	double sector_split_s[GHOST_MAX_SECTORS] = {};

	// `TuningSet::hash()` of the session the lap was set in. Recorded so a best lap
	// can be labeled as having been set on a preset. There is no `TuningSet` here:
	// nothing re-simulates a ghost, so the values themselves have no consumer, and
	// carrying them would put a second copy of the preset in every ghost file.
	uint64_t tuning_hash = 0;

	// No driver name and no display label. ADR-0044 puts display names on the script
	// side, and ADR-0042 references a ghost by id from the profile — so the profile
	// entry that names the lap is the one thing that owns the name.

	bool set_track_id(const char *id) {
		for (int i = 0; i < SESSION_ID_CHARS; ++i) {
			track_id[i] = '\0';
		}
		if (id == nullptr) {
			return false;
		}
		int i = 0;
		while (i < SESSION_ID_CHARS - 1 && id[i] != '\0') {
			track_id[i] = id[i];
			++i;
		}
		return id[i] == '\0';
	}

	bool is_valid() const {
		if (track_id[0] == '\0') {
			return false;
		}
		bool terminated = false;
		for (int i = 0; i < SESSION_ID_CHARS; ++i) {
			if (track_id[i] == '\0') {
				terminated = true;
				break;
			}
		}
		if (!terminated) {
			return false;
		}
		if (static_cast<int>(layout) < 0 || static_cast<int>(layout) >= TRACK_LAYOUT_COUNT) {
			return false;
		}
		if (static_cast<int>(kart_class) < 0 ||
				static_cast<int>(kart_class) >= KART_CLASS_COUNT) {
			return false;
		}
		if (sample_hz < 1 || sample_count < 1) {
			return false;
		}
		if (!(lap_time_s > 0.0)) {
			return false;
		}
		if (sector_count < 0 || sector_count > GHOST_MAX_SECTORS) {
			return false;
		}
		// Splits are cumulative, so they must increase and end at the lap time.
		double previous = 0.0;
		for (int i = 0; i < sector_count; ++i) {
			if (!(sector_split_s[i] > previous)) {
				return false;
			}
			previous = sector_split_s[i];
		}
		if (sector_count > 0 &&
				tuning_micro(sector_split_s[sector_count - 1]) != tuning_micro(lap_time_s)) {
			return false;
		}
		return true;
	}

	uint64_t sample_bytes() const {
		return static_cast<uint64_t>(sample_count) * static_cast<uint64_t>(GHOST_SAMPLE_BYTES);
	}
};

// How many samples a lap of this length takes, and what it costs. Both are here
// rather than in a test so that a caller sizing a buffer and the report that quotes
// the number are reading the same arithmetic.
inline int ghost_samples_for_lap(double lap_time_s, int sample_hz = GHOST_SAMPLE_HZ) {
	if (!(lap_time_s > 0.0) || sample_hz < 1) {
		return 0;
	}
	// One sample at t = 0 and one at every interval after it.
	//
	// **The nudge is not cosmetic and this function was wrong without it.** A lap
	// time does not arrive as an arbitrary real: `lap_timing.h` builds it as
	// `lap_ticks * (1.0 / 120.0)`, and that reciprocal is not exact in binary. So a
	// lap that is exactly 30 sample intervals long arrives a fraction of an ULP
	// under, `floor` drops to 29, and the count is one short — measured on **234 of
	// the first 20,000 tick counts**, first failure at 124 ticks, one second into a
	// lap. Writing the division the other way round does not save it either: that
	// form fails at 98.
	//
	// The consequence is worth stating because it is silent. A recorder that sized
	// its buffer from this function and a header that quoted it would disagree with
	// the body by one sample, `load` would refuse the ghost, and a driver would lose
	// a best lap on roughly one lap in a hundred with nothing on screen to explain
	// it. The ghost agent found it by measuring the helper rather than trusting it,
	// which is the only reason it is not in the tree.
	//
	// The epsilon is relative to the sample count and is far below one interval, so
	// it cannot invent a sample on a lap that genuinely falls between two.
	const double intervals = lap_time_s * static_cast<double>(sample_hz);
	const double snapped = std::floor(intervals + 1e-9 * (intervals + 1.0));
	return 1 + static_cast<int>(snapped);
}

inline uint64_t ghost_bytes_for_lap(double lap_time_s, int sample_hz = GHOST_SAMPLE_HZ) {
	return static_cast<uint64_t>(ghost_samples_for_lap(lap_time_s, sample_hz)) *
			static_cast<uint64_t>(GHOST_SAMPLE_BYTES);
}

// --- comparability -------------------------------------------------------------

// A ghost is not admitted or refused; it is comparable, comparable-with-a-caveat,
// or meaningless. The third case is not a version policy failure — it is a ghost
// from somewhere else.
enum class GhostVerdict : int {
	Comparable = 0,
	// Drawable and worth drawing, with something the UI should say out loud.
	Warned = 1,
	// Nothing useful can be done with it against this session.
	Incomparable = 2,
};

inline const char *ghost_verdict_name(GhostVerdict verdict) {
	switch (verdict) {
		case GhostVerdict::Comparable: return "comparable";
		case GhostVerdict::Warned: return "comparable with a caveat";
		case GhostVerdict::Incomparable: return "incomparable";
	}
	return "invalid";
}

enum class GhostProblem : int {
	None = 0,
	// Written by a newer build. Cannot be migrated forward, only guessed at.
	FutureFormat,
	Track,
	Layout,
	Class,
	Malformed,
};

inline const char *ghost_problem_name(GhostProblem problem) {
	switch (problem) {
		case GhostProblem::None: return "none";
		case GhostProblem::FutureFormat: return "newer format";
		case GhostProblem::Track: return "different track";
		case GhostProblem::Layout: return "different layout";
		case GhostProblem::Class: return "different class";
		case GhostProblem::Malformed: return "malformed header";
	}
	return "invalid";
}

struct GhostReport {
	GhostVerdict verdict = GhostVerdict::Incomparable;
	GhostProblem problem = GhostProblem::None;

	// The track file changed since the lap was set, so the lap time is not
	// comparable — but the line still is, and hiding it would be worse than saying
	// so. The replay refuses on the same field, because a replay re-simulates
	// against the collision geometry and a ghost does not touch it.
	bool track_changed = false;

	// The lap was set under a tuning preset that is not the one running now.
	bool tuning_differs = false;

	// Recorded by an older format and migrated forward on the way in. ADR-0042: a
	// save always loads, and a ghost is a save.
	bool migrated = false;

	bool drawable() const { return verdict != GhostVerdict::Incomparable; }
};

// Whether this build can read a ghost of that version at all. Older migrates,
// newer does not — see the file note on the ADR-0041/ADR-0042 split.
inline bool ghost_can_migrate(int format_version) {
	return format_version >= 1 && format_version <= GHOST_FORMAT_VERSION;
}

// v1 is the only version, so the chain is the identity function. ADR-0042 is
// explicit that this is the right thing to write rather than an argument to have:
// "a migration may be the identity function", and the cost is one line.
inline bool ghost_migrate(GhostHeader &header) {
	if (!ghost_can_migrate(header.format_version)) {
		return false;
	}
	// while (header.format_version < GHOST_FORMAT_VERSION) { ... }
	header.format_version = GHOST_FORMAT_VERSION;
	return true;
}

// Compare a stored ghost against the session about to be driven.
//
// `live` carries only the fields a ghost has: the current track, its hash, the
// layout, the class, and the tuning hash now in force. There is no `config_hash`
// comparison anywhere in this function and that is the design, not an omission — a
// ghost is raced against a session that differs from the one that produced it.
inline GhostReport ghost_admit(const GhostHeader &recorded, const GhostHeader &live) {
	GhostReport report;

	if (!recorded.is_valid()) {
		report.verdict = GhostVerdict::Incomparable;
		report.problem = GhostProblem::Malformed;
		return report;
	}
	if (!ghost_can_migrate(recorded.format_version)) {
		report.verdict = GhostVerdict::Incomparable;
		report.problem = GhostProblem::FutureFormat;
		return report;
	}
	report.migrated = recorded.format_version < GHOST_FORMAT_VERSION;

	for (int i = 0; i < SESSION_ID_CHARS; ++i) {
		if (recorded.track_id[i] != live.track_id[i]) {
			report.verdict = GhostVerdict::Incomparable;
			report.problem = GhostProblem::Track;
			return report;
		}
		if (recorded.track_id[i] == '\0') {
			break;
		}
	}
	if (recorded.layout != live.layout) {
		report.verdict = GhostVerdict::Incomparable;
		report.problem = GhostProblem::Layout;
		return report;
	}
	if (recorded.kart_class != live.kart_class) {
		report.verdict = GhostVerdict::Incomparable;
		report.problem = GhostProblem::Class;
		return report;
	}

	report.track_changed = recorded.track_hash != live.track_hash;
	report.tuning_differs = recorded.tuning_hash != live.tuning_hash;
	report.verdict = (report.track_changed || report.tuning_differs || report.migrated)
			? GhostVerdict::Warned
			: GhostVerdict::Comparable;
	report.problem = GhostProblem::None;
	return report;
}

// --- text ----------------------------------------------------------------------
//
// Same family as `replay.h` and `tuning.h`: a text header a person can read and
// diff, a binary sample stream that is not. The header is small — a ghost has no
// tuning diff and no session configuration in it — so this is a fixed handful of
// lines.

// The comment block is most of it; the fields come to a little over 200 characters
// with three sectors, and eight sectors adds 80 more. Sized with room rather than to
// fit, because `ghost_format_header` returns -1 on overflow and a caller that sized
// its buffer by counting would break the day a line of the comment grew.
inline constexpr int GHOST_HEADER_CHARS = 1024;


// Write the header. Byte-identical for identical input, fixed field order.
inline int ghost_format_header(const GhostHeader &header, char *out, int cap) {
	if (out == nullptr || cap < 2) {
		return -1;
	}
	int written = 0;
	bool overflow = false;
	auto put = [&](const char *text) {
		if (overflow || text == nullptr) {
			return;
		}
		for (const char *c = text; *c != '\0'; ++c) {
			if (written + 1 >= cap) {
				overflow = true;
				return;
			}
			out[written++] = *c;
		}
	};
	char scratch[TUNING_LINE_CHARS];
	auto put_int = [&](int64_t value) {
		if (format_int(value, scratch, static_cast<int>(sizeof(scratch))) < 0) {
			overflow = true;
			return;
		}
		put(scratch);
	};
	auto put_value = [&](double value) {
		if (format_value(value, scratch, static_cast<int>(sizeof(scratch))) < 0) {
			overflow = true;
			return;
		}
		put(scratch);
	};
	auto put_hex = [&](uint64_t value) {
		if (format_hex64(value, scratch, static_cast<int>(sizeof(scratch))) < 0) {
			overflow = true;
			return;
		}
		put(scratch);
	};

	put("# kartgame ghost. Text header, binary transform stream.\n"
		"#\n"
		"# A lap, sampled, to be drawn beside a live session. Not a replay: nothing\n"
		"# here is re-simulated, so there is no input stream and no config_hash --\n"
		"# a ghost is raced against a session that differs from the one that set it.\n"
		"# Splits are cumulative and the last one is the lap time.\n"
		"# See src/core/ghost.h and DECISIONS ADR-0041, ADR-0042.\n");
	put("format ");
	put_int(header.format_version);
	put("\n");
	put("track ");
	put(header.track_id);
	put("\n");
	put("track_hash ");
	put_hex(header.track_hash);
	put("\n");
	put("layout ");
	put(track_layout_name(header.layout));
	put("\n");
	put("class ");
	put(kart_class_name(header.kart_class));
	put("\n");
	put("sample_hz ");
	put_int(header.sample_hz);
	put("\n");
	put("samples ");
	put_int(header.sample_count);
	put("\n");
	put("lap ");
	put_value(header.lap_time_s);
	put("\n");
	put("sectors ");
	put_int(header.sector_count);
	put("\n");
	for (int i = 0; i < header.sector_count; ++i) {
		put("split ");
		put_value(header.sector_split_s[i]);
		put("\n");
	}
	put("tuning_hash ");
	put_hex(header.tuning_hash);
	put("\n");

	if (overflow || written >= cap) {
		return -1;
	}
	out[written] = '\0';
	return written;
}

// Unsigned decimal and "0x..." hex. Both are the twins of `replay.h`'s, carried
// here for the same reason `ghost_format_int` is: the two formats do not include
// each other. Their real home is `tuning.h`, next to `parse_value`.
inline bool ghost_parse_uint(const char *text, int len, uint64_t &out) {
	if (text == nullptr || len <= 0 || len > 20) {
		return false;
	}
	uint64_t value = 0;
	for (int i = 0; i < len; ++i) {
		if (text[i] < '0' || text[i] > '9') {
			return false;
		}
		value = value * 10ULL + static_cast<uint64_t>(text[i] - '0');
	}
	out = value;
	return true;
}

inline bool ghost_parse_hex64(const char *text, int len, uint64_t &out) {
	if (text == nullptr || len < 3) {
		return false;
	}
	if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) {
		return false;
	}
	if (len - 2 > 16) {
		return false;
	}
	uint64_t value = 0;
	for (int i = 2; i < len; ++i) {
		const char c = text[i];
		int nibble = -1;
		if (c >= '0' && c <= '9') {
			nibble = c - '0';
		} else if (c >= 'a' && c <= 'f') {
			nibble = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			nibble = c - 'A' + 10;
		} else {
			return false;
		}
		value = (value << 4) | static_cast<uint64_t>(nibble);
	}
	out = value;
	return true;
}

enum class GhostParseProblem : int {
	None = 0,
	MalformedLine,
	UnknownKey,
	BadValue,
	MissingField,
	TooManySectors,
	Invalid,
};

inline const char *ghost_parse_problem_name(GhostParseProblem problem) {
	switch (problem) {
		case GhostParseProblem::None: return "none";
		case GhostParseProblem::MalformedLine: return "malformed line";
		case GhostParseProblem::UnknownKey: return "unknown key";
		case GhostParseProblem::BadValue: return "bad value";
		case GhostParseProblem::MissingField: return "missing field";
		case GhostParseProblem::TooManySectors: return "too many sectors";
		case GhostParseProblem::Invalid: return "invalid header";
	}
	return "invalid";
}

struct GhostParse {
	bool ok = false;
	int line = 0;
	GhostParseProblem problem = GhostParseProblem::None;
	char key[TUNING_KEY_CHARS] = {};
};

inline constexpr const char *GHOST_REQUIRED_KEYS[] = {
	"format", "track", "track_hash", "layout", "class", "sample_hz", "samples", "lap",
	"sectors", "tuning_hash"
};
inline constexpr int GHOST_REQUIRED_KEY_COUNT =
		static_cast<int>(sizeof(GHOST_REQUIRED_KEYS) / sizeof(GHOST_REQUIRED_KEYS[0]));

// Parse a ghost header. Reuses `tuning.h`'s `parse_line` and `parse_value`, so the
// number grid is the same 1e-6 everywhere in the project and a lap time round-trips
// exactly.
//
// **`format` is parsed but not judged here.** A ghost from a future version has to
// be readable far enough to *say* it is from a future version; `ghost_admit` and
// `ghost_can_migrate` make that call. A parser that refused would turn ADR-0042's
// "a save always loads" into "a save loads unless it fails to parse", which is the
// same thing wearing a hat.
inline GhostParse ghost_parse_header(const char *text, int len, GhostHeader &out) {
	GhostParse result;
	if (text == nullptr) {
		result.problem = GhostParseProblem::MalformedLine;
		return result;
	}
	if (len < 0) {
		len = 0;
		while (text[len] != '\0') {
			++len;
		}
	}

	out = GhostHeader();
	bool seen[GHOST_REQUIRED_KEY_COUNT] = {};
	int splits_read = 0;
	int declared_sectors = 0;

	auto key_matches = [](const char *a, const char *b) {
		for (int i = 0;; ++i) {
			if (a[i] != b[i]) {
				return false;
			}
			if (a[i] == '\0') {
				return true;
			}
		}
	};
	auto mark_seen = [&](const char *key) {
		for (int i = 0; i < GHOST_REQUIRED_KEY_COUNT; ++i) {
			if (key_matches(key, GHOST_REQUIRED_KEYS[i])) {
				seen[i] = true;
				return;
			}
		}
	};
	auto copy_key = [&](const char *key) {
		int i = 0;
		while (i < TUNING_KEY_CHARS - 1 && key[i] != '\0') {
			result.key[i] = key[i];
			++i;
		}
		result.key[i] = '\0';
	};

	int line_number = 0;
	int cursor = 0;
	while (cursor <= len) {
		int end = cursor;
		while (end < len && text[end] != '\n') {
			++end;
		}
		if (cursor == len && line_number > 0) {
			break;
		}
		++line_number;
		const char *line = text + cursor;
		const int line_length = end - cursor;
		cursor = end + 1;

		const ParsedLine parsed = parse_line(line, line_length);
		if (parsed.kind == ParsedLine::Blank || parsed.kind == ParsedLine::Comment) {
			continue;
		}
		result.line = line_number;
		if (parsed.kind != ParsedLine::Header) {
			// A ghost header has no `key = value` entries at all, so an Entry line is
			// as malformed as an Invalid one.
			result.problem = GhostParseProblem::MalformedLine;
			return result;
		}

		const char *key = parsed.key;
		const char *value = parsed.text;
		int value_length = 0;
		while (value[value_length] != '\0') {
			++value_length;
		}
		copy_key(key);

		if (key_matches(key, "format")) {
			uint64_t number = 0;
			if (!ghost_parse_uint(value, value_length, number) || number > 0x7FFFFFFFULL) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
			out.format_version = static_cast<int>(number);
		} else if (key_matches(key, "track")) {
			if (!out.set_track_id(value)) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "track_hash")) {
			if (!ghost_parse_hex64(value, value_length, out.track_hash)) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "layout")) {
			bool found = false;
			for (int i = 0; i < TRACK_LAYOUT_COUNT && !found; ++i) {
				const char *name = track_layout_name(static_cast<TrackLayout>(i));
				if (key_matches(name, value)) {
					out.layout = static_cast<TrackLayout>(i);
					found = true;
				}
			}
			if (!found) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "class")) {
			bool found = false;
			for (int i = 0; i < KART_CLASS_COUNT && !found; ++i) {
				const char *name = kart_class_name(static_cast<KartClass>(i));
				if (key_matches(name, value)) {
					out.kart_class = static_cast<KartClass>(i);
					found = true;
				}
			}
			if (!found) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "sample_hz")) {
			uint64_t number = 0;
			if (!ghost_parse_uint(value, value_length, number) || number < 1 ||
					number > 0x7FFFFFFFULL) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
			out.sample_hz = static_cast<int>(number);
		} else if (key_matches(key, "samples")) {
			uint64_t number = 0;
			if (!ghost_parse_uint(value, value_length, number) || number > 0x7FFFFFFFULL) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
			out.sample_count = static_cast<int>(number);
		} else if (key_matches(key, "lap")) {
			if (!parse_value(value, value_length, out.lap_time_s)) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "sectors")) {
			uint64_t number = 0;
			if (!ghost_parse_uint(value, value_length, number) ||
					number > static_cast<uint64_t>(GHOST_MAX_SECTORS)) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
			declared_sectors = static_cast<int>(number);
			out.sector_count = declared_sectors;
		} else if (key_matches(key, "split")) {
			if (splits_read >= GHOST_MAX_SECTORS) {
				result.problem = GhostParseProblem::TooManySectors;
				return result;
			}
			if (!parse_value(value, value_length, out.sector_split_s[splits_read])) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
			++splits_read;
		} else if (key_matches(key, "tuning_hash")) {
			if (!ghost_parse_hex64(value, value_length, out.tuning_hash)) {
				result.problem = GhostParseProblem::BadValue;
				return result;
			}
		} else {
			result.problem = GhostParseProblem::UnknownKey;
			return result;
		}
		mark_seen(key);
	}

	result.line = 0;
	result.key[0] = '\0';
	for (int i = 0; i < GHOST_REQUIRED_KEY_COUNT; ++i) {
		if (!seen[i]) {
			result.problem = GhostParseProblem::MissingField;
			copy_key(GHOST_REQUIRED_KEYS[i]);
			return result;
		}
	}
	// The count and the lines have to agree. A file declaring three sectors and
	// carrying two would otherwise leave a zero split in the middle of a lap.
	if (splits_read != declared_sectors) {
		result.problem = GhostParseProblem::MissingField;
		copy_key("split");
		return result;
	}
	if (!out.is_valid()) {
		result.problem = GhostParseProblem::Invalid;
		return result;
	}

	result.ok = true;
	result.problem = GhostParseProblem::None;
	return result;
}

} // namespace kart::core

#endif // KART_CORE_GHOST_H
