#ifndef KART_CORE_REPLAY_H
#define KART_CORE_REPLAY_H

#include "core/session.h"
#include "core/state_hash.h"
#include "core/tuning.h"
#include "core/vehicle_state.h"

#include <cmath>
#include <cstdint>

// The replay format: text header, binary body, checkpointed footer. ADR-0041, and
// ROADMAP M3c's engine-free half of it.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// This header is also allocation-free and locale-free for the reasons `tuning.h`
// gives, and every function below writes into a caller-supplied buffer and
// returns the length written or -1. **No function here opens a file.** File I/O
// is the Godot side's, per ADR-0041's "where it lives"; what is here is the
// arithmetic, and the arithmetic is what has the traps in it.
//
// ## The shape, from ADR-0041
//
//     header    format_version, build and extension API version
//               track + layout + session type
//               class, assists, field, surface
//               tuning preset - the full diff, inline, not a path
//               config_hash - over all of the above
//               seed, tick_count
//     body      DriverInput per tick, per kart
//     footer    state hash every N ticks
//
// Everything from "track" down to "seed" is `SessionConfig`, which is why this
// struct carries one rather than restating its fields. The ADR's "surface" is
// `SessionConfig::condition` (track-wide) plus `track_hash` (per-patch surface is
// authored in `track.json`, ADR-0046, and the hash is what pins it); the ADR's
// "field" is `entry_count` plus `roster_hash`.
//
// **There is no `kart_count` field.** The body is one `DriverInput` per kart per
// tick and the number of karts is `config.entry_count` — the value that is already
// hashed. A second copy could disagree with the first, and the failure mode is a
// body read at the wrong stride, which decodes into plausible garbage rather than
// into an error.
//
// ## Text header, binary body, and the two decisions are separate
//
// The header is what a person reads, diffs and files a bug with, so it is text in
// the family `tuning.h` established: the same `format_value` and `format_hex64`,
// the same `parse_line`, the same six-decimal grid. The tuning diff inside it is
// literally the preset format's own entry lines, produced by `format_entry`, so a
// replay header can be pasted into a `.tune` file and vice versa.
//
// The body is not text. A 15-minute round is 108,000 ticks at 120 Hz, and eight
// karts of it is 864,000 input records. As six 32-bit floats that is 24 bytes a
// record and 20.7 MB, which is ADR-0041's "about 20 MB". Quantized to the fixed
// point below it is 9 bytes and 7.78 MB, which is its "under 8 MB" — with 220 kB
// of headroom, so the claim is true and it is not true by much.
//
// ## The quantization trap, which is the reason ADR-0041 exists at all
//
// If input were quantized **on write**, the live run would have consumed
// full-precision values and the replay would consume rounded ones. The two runs
// then diverge, at a rate that grows exponentially the way any chaotic system's
// does, and the divergence looks exactly like a solver bug. It is the same class
// of defect as ADR-0033's lever arm: correct arithmetic applied at the wrong point
// in the chain.
//
// So the producer emits already-quantized input. `PlayerDriver` calls
// `replay_snap()` before `KartBody` ever sees the struct, and the live run and the
// replay consume bit-identical values by construction rather than by hope.
//
// **That is made structural here and not left to a comment.**
// `replay_encode_input` **refuses** — returns -1 — when it is handed input that is
// not already on the grid. It does not round it. A recorder cannot silently
// introduce the defect this note is about, because the only function that can
// write a body byte will not do it. `replay_snap` is a separate function with a
// separate name, and the only correct place to call it is upstream of the solver.
//
// ## The grids, and why each one is the number it is
//
// This is a **storage** grid and `StateHash`'s 1e-4 is a **comparison** grid. They
// are answering different questions and neither number is usable as the other:
//
//   * `StateHash` compares two values that are *allowed* to differ, by float noise,
//     and needs a tolerance. 1e-4 is a tenth of a millimeter of position.
//   * An input grid has to round-trip a value **bit-identically**, because the same
//     double has to come back out of the file that went into the solver. A decimal
//     grid cannot promise that: 1e-4 is not a binary fraction, so
//     `llround(x / 1e-4) * 1e-4` is not guaranteed idempotent, and a value that
//     moved by one ULP on the way through is a divergence with no cause.
//
// The form used below is `code / N` for integer `code` and integer `N`. Decoding
// is one division, and the identical division reproduces the identical double
// every time on the same binary, so `encode(decode(code)) == code` for every code
// in range — asserted exhaustively over all 65,536 of them in
// `tests/core/test_replay.cpp` rather than argued for here.
//
// **Steer: 32,767 codes over -1..1, a quantum of 3.05e-5.** The number is chosen
// against a measurement, not rounded to something tidy. `src/vehicle/kart_body.h`
// records that at 100 km/h the tightest radius this kart can hold is 37.5 m, which
// is 0.065 of lock — so the whole followable steering band is 6.5% of the range,
// and a grid coarse relative to *that* is a grid that cannot express a corner. It
// gets 2,130 codes. And `KartBody`'s x^3 curve compresses the bottom of the range
// hardest, which is where a grid fails first: at the 0.15 deadzone edge one step of
// a stick's 8-bit axis (1/127) is a post-curve step of
// (0.1579)^3 - (0.15)^3 = 5.6e-4 of lock, which is 18 codes. Every stick position
// the hardware can report is a distinct code, at the worst point in the mapping,
// with 18 to spare.
//
// **Throttle, brake and clutch: 65,535 codes over 0..1, a quantum of 1.53e-5.**
// They are unsigned so they get the full 16 bits. A trigger is an 8-bit axis, so
// this is 257 codes per hardware step; the surplus is free, because the record is
// nine bytes either way and the alternative was eight bytes and a clutch on a
// coarser grid than the throttle for no reason anyone could state later.
//
// **Steer is signed and scales by 32,767 rather than 32,768.** The asymmetric
// version makes -1.0 exact and +1.0 unreachable, so full left lock and full right
// lock are different magnitudes and a kart that is symmetric in every other respect
// steers 1/32768 harder one way. `chassis.h` already records that a validation
// scenario which only turns one way measures half a kart; this is the same mistake
// available for free in an encoder.
//
// **The shift requests are bits, because `DriverInput` says they are edges** — true
// for exactly the tick the button went down. There is nothing to quantize. They
// share a flags byte with six reserved bits, and `replay_decode_input` **rejects a
// record with any reserved bit set** rather than masking it off, so a v2 body that
// added a flag cannot be misread by a v1 reader as a v1 body.

namespace kart::core {

// --- version -------------------------------------------------------------------

// Bumped on any change to the header's field list or the body's layout. **Refused
// across a mismatch, never migrated**, per ADR-0041: a replay is a diagnostic
// artifact rather than user data, and a silently migrated one is a plausible-looking
// run of something that never happened. ADR-0042 takes the opposite policy for a
// career save, deliberately, and `ghost.h` follows ADR-0042 rather than this file
// because a best-lap ghost *is* user data.
inline constexpr int REPLAY_FORMAT_VERSION = 1;

// The build the replay was recorded by, and the godot-cpp extension API it was
// built against. **Recorded and warned about, not refused**, because cross-build
// bit determinism is not claimed anywhere in this project — `state_hash.h` says so
// in as many words and ROADMAP defers it explicitly. A warning that says "this was
// recorded on a different build" is the honest form of that; a refusal would claim
// a guarantee that does not exist, and passing it silently would attribute a
// compiler's FMA contraction to the solver.
inline constexpr int REPLAY_BUILD_CHARS = 48;
inline constexpr int REPLAY_API_CHARS = 24;

// --- the grids -----------------------------------------------------------------

inline constexpr int REPLAY_UNIT_CODES = 65535; // throttle, brake, clutch: 0..1
inline constexpr int REPLAY_STEER_CODES = 32767; // steer: -1..1, symmetric

inline constexpr double REPLAY_UNIT_QUANTUM = 1.0 / static_cast<double>(REPLAY_UNIT_CODES);
inline constexpr double REPLAY_STEER_QUANTUM = 1.0 / static_cast<double>(REPLAY_STEER_CODES);

// One kart's input for one tick: three unsigned axes, one signed axis, one flags
// byte. Little-endian, written a byte at a time — no struct is ever memcpy'd into
// a file, so there is no padding to leak and no endianness to get wrong on a
// machine nobody has tested on.
inline constexpr int REPLAY_INPUT_BYTES = 9;

// One footer entry: the tick, then the hash. Sixteen bytes, same byte order.
inline constexpr int REPLAY_CHECKPOINT_BYTES = 16;

// How often the footer records a state hash.
//
// 120 is one simulated second at `project.godot`'s 120 Hz, which makes the footer
// readable without arithmetic: entry 37 is second 37. A 15-minute round is 900
// entries and 14.4 kB, which is 0.19% of the body — cheap enough that a finer
// interval would be buying resolution nobody needs, and coarse enough that
// "diverged at second 41" is the first thing the report says.
inline constexpr int REPLAY_HASH_INTERVAL = 120;

// `project.godot`'s `physics/common/physics_ticks_per_second`. The default a
// recorder stamps unless it was told otherwise.
//
// **This is in the header and not in `SessionConfig`, and that is a hole in
// ADR-0041 rather than a decision of this file.** A replay recorded at 120 Hz and
// re-simulated at 240 Hz has an identical `config_hash` and a completely different
// lap: same solver, same inputs, different integration. ADR-0041's two outcomes
// would then classify it as "config matches, state hash diverges", which the ADR
// says is a *real determinism bug* — and it is nothing of the sort. So the rate is
// carried here and checked explicitly, and the check refuses with its own reason.
// The tidy fix is for the tick rate to become a `SessionConfig` field, since it is
// configuration by every test the ADR applies; that is the main thread's call and
// the note in this project's issue list, not a change made here.
inline constexpr int REPLAY_DEFAULT_TICK_HZ = 120;

// --- input quantization --------------------------------------------------------

// A 0..1 axis to its code. Clamps, so a producer that hands over 1.0000001 gets
// full throttle rather than an out-of-range code; a NaN becomes 0, which is
// neutral, and is caught anyway because `replay_is_on_grid` compares NaN against
// its own snapped value and NaN never equals anything.
inline uint16_t replay_encode_unit(double value) {
	if (std::isnan(value)) {
		return 0;
	}
	double clamped = value;
	if (clamped < 0.0) {
		clamped = 0.0;
	}
	if (clamped > 1.0) {
		clamped = 1.0;
	}
	long long code = std::llround(clamped * static_cast<double>(REPLAY_UNIT_CODES));
	if (code < 0) {
		code = 0;
	}
	if (code > REPLAY_UNIT_CODES) {
		code = REPLAY_UNIT_CODES;
	}
	return static_cast<uint16_t>(code);
}

inline double replay_decode_unit(uint16_t code) {
	return static_cast<double>(code) / static_cast<double>(REPLAY_UNIT_CODES);
}

inline double replay_snap_unit(double value) {
	return replay_decode_unit(replay_encode_unit(value));
}

// A -1..1 axis to its code. See the header note on why the scale is 32,767.
inline int16_t replay_encode_steer(double value) {
	if (std::isnan(value)) {
		return 0;
	}
	double clamped = value;
	if (clamped < -1.0) {
		clamped = -1.0;
	}
	if (clamped > 1.0) {
		clamped = 1.0;
	}
	long long code = std::llround(clamped * static_cast<double>(REPLAY_STEER_CODES));
	if (code < -REPLAY_STEER_CODES) {
		code = -REPLAY_STEER_CODES;
	}
	if (code > REPLAY_STEER_CODES) {
		code = REPLAY_STEER_CODES;
	}
	return static_cast<int16_t>(code);
}

inline double replay_decode_steer(int16_t code) {
	return static_cast<double>(code) / static_cast<double>(REPLAY_STEER_CODES);
}

inline double replay_snap_steer(double value) {
	return replay_decode_steer(replay_encode_steer(value));
}

// **What a producer calls, upstream of the solver.** `PlayerDriver`, `AIDriver`
// and anything else filling a `DriverInput` snaps here first, so the value the
// vehicle integrates is the value a replay can store. Nothing downstream of this
// call is allowed to round anything.
inline DriverInput replay_snap(const DriverInput &input) {
	DriverInput snapped;
	snapped.throttle = replay_snap_unit(input.throttle);
	snapped.brake = replay_snap_unit(input.brake);
	snapped.clutch = replay_snap_unit(input.clutch);
	snapped.steer = replay_snap_steer(input.steer);
	snapped.shift_up = input.shift_up;
	snapped.shift_down = input.shift_down;
	return snapped;
}

// Whether this struct is already exactly on the grid — bitwise equality against
// its own snapped form, not a tolerance. A tolerance here would defeat the whole
// point: the guarantee being checked is that the file returns the same double, and
// "the same to within something" is what the trap looks like.
//
// Also false for a NaN and for anything out of range, both of which are producer
// bugs that would otherwise be silently clamped into the file.
inline bool replay_is_on_grid(const DriverInput &input) {
	return input.throttle == replay_snap_unit(input.throttle) &&
			input.brake == replay_snap_unit(input.brake) &&
			input.clutch == replay_snap_unit(input.clutch) &&
			input.steer == replay_snap_steer(input.steer);
}

// --- the body ------------------------------------------------------------------

inline constexpr unsigned char REPLAY_FLAG_SHIFT_UP = 0x01;
inline constexpr unsigned char REPLAY_FLAG_SHIFT_DOWN = 0x02;
inline constexpr unsigned char REPLAY_FLAGS_KNOWN =
		static_cast<unsigned char>(REPLAY_FLAG_SHIFT_UP | REPLAY_FLAG_SHIFT_DOWN);

// One kart, one tick, nine bytes. Returns the count written, or **-1 if the input
// was not already on the grid** — see the header note. This function does not
// round, and that refusal is the structural half of ADR-0041's decision.
inline int replay_encode_input(const DriverInput &input, unsigned char *out, int cap) {
	if (out == nullptr || cap < REPLAY_INPUT_BYTES) {
		return -1;
	}
	if (!replay_is_on_grid(input)) {
		return -1;
	}
	const uint16_t throttle = replay_encode_unit(input.throttle);
	const uint16_t brake = replay_encode_unit(input.brake);
	const uint16_t clutch = replay_encode_unit(input.clutch);
	const uint16_t steer = static_cast<uint16_t>(replay_encode_steer(input.steer));

	out[0] = static_cast<unsigned char>(throttle & 0xFFU);
	out[1] = static_cast<unsigned char>((throttle >> 8) & 0xFFU);
	out[2] = static_cast<unsigned char>(brake & 0xFFU);
	out[3] = static_cast<unsigned char>((brake >> 8) & 0xFFU);
	out[4] = static_cast<unsigned char>(clutch & 0xFFU);
	out[5] = static_cast<unsigned char>((clutch >> 8) & 0xFFU);
	out[6] = static_cast<unsigned char>(steer & 0xFFU);
	out[7] = static_cast<unsigned char>((steer >> 8) & 0xFFU);
	out[8] = static_cast<unsigned char>((input.shift_up ? REPLAY_FLAG_SHIFT_UP : 0) |
			(input.shift_down ? REPLAY_FLAG_SHIFT_DOWN : 0));
	return REPLAY_INPUT_BYTES;
}

// The inverse. False on a short buffer or on a reserved flag bit — a record this
// reader does not fully understand is an error rather than a masked-off surprise.
inline bool replay_decode_input(const unsigned char *in, int len, DriverInput &out) {
	if (in == nullptr || len < REPLAY_INPUT_BYTES) {
		return false;
	}
	if ((in[8] & static_cast<unsigned char>(~REPLAY_FLAGS_KNOWN)) != 0) {
		return false;
	}
	const uint16_t throttle = static_cast<uint16_t>(in[0] | (in[1] << 8));
	const uint16_t brake = static_cast<uint16_t>(in[2] | (in[3] << 8));
	const uint16_t clutch = static_cast<uint16_t>(in[4] | (in[5] << 8));
	const uint16_t steer_bits = static_cast<uint16_t>(in[6] | (in[7] << 8));
	if (throttle > REPLAY_UNIT_CODES || brake > REPLAY_UNIT_CODES ||
			clutch > REPLAY_UNIT_CODES) {
		return false;
	}
	const int16_t steer = static_cast<int16_t>(steer_bits);
	if (steer < -REPLAY_STEER_CODES) {
		// -32768 is the one code the encoder cannot emit; a file containing it was
		// not written by this encoder.
		return false;
	}
	out.throttle = replay_decode_unit(throttle);
	out.brake = replay_decode_unit(brake);
	out.clutch = replay_decode_unit(clutch);
	out.steer = replay_decode_steer(steer);
	out.shift_up = (in[8] & REPLAY_FLAG_SHIFT_UP) != 0;
	out.shift_down = (in[8] & REPLAY_FLAG_SHIFT_DOWN) != 0;
	return true;
}

// One tick across the whole field, in kart order. The stride is fixed, so tick `t`
// of kart `k` is at `(t * kart_count + k) * REPLAY_INPUT_BYTES` and a body is
// seekable without an index.
inline int replay_encode_tick(const DriverInput *inputs, int kart_count, unsigned char *out,
		int cap) {
	if (inputs == nullptr || kart_count < 1 || out == nullptr) {
		return -1;
	}
	const int needed = kart_count * REPLAY_INPUT_BYTES;
	if (cap < needed) {
		return -1;
	}
	for (int kart = 0; kart < kart_count; ++kart) {
		if (replay_encode_input(inputs[kart], out + kart * REPLAY_INPUT_BYTES,
					REPLAY_INPUT_BYTES) < 0) {
			return -1;
		}
	}
	return needed;
}

inline bool replay_decode_tick(const unsigned char *in, int len, int kart_count,
		DriverInput *out) {
	if (in == nullptr || out == nullptr || kart_count < 1) {
		return false;
	}
	if (len < kart_count * REPLAY_INPUT_BYTES) {
		return false;
	}
	for (int kart = 0; kart < kart_count; ++kart) {
		if (!replay_decode_input(in + kart * REPLAY_INPUT_BYTES, REPLAY_INPUT_BYTES,
					out[kart])) {
			return false;
		}
	}
	return true;
}

// --- the footer ----------------------------------------------------------------

// One state hash and the tick it was taken on. The tick is stored rather than
// implied by the entry's position, because a footer that has been truncated by a
// crash must still say what it does contain.
struct ReplayCheckpoint {
	uint64_t tick = 0;
	uint64_t hash = 0;
};

inline int replay_encode_checkpoint(const ReplayCheckpoint &point, unsigned char *out, int cap) {
	if (out == nullptr || cap < REPLAY_CHECKPOINT_BYTES) {
		return -1;
	}
	for (int byte = 0; byte < 8; ++byte) {
		out[byte] = static_cast<unsigned char>((point.tick >> (byte * 8)) & 0xFFU);
		out[8 + byte] = static_cast<unsigned char>((point.hash >> (byte * 8)) & 0xFFU);
	}
	return REPLAY_CHECKPOINT_BYTES;
}

inline bool replay_decode_checkpoint(const unsigned char *in, int len, ReplayCheckpoint &out) {
	if (in == nullptr || len < REPLAY_CHECKPOINT_BYTES) {
		return false;
	}
	uint64_t tick = 0;
	uint64_t hash = 0;
	for (int byte = 0; byte < 8; ++byte) {
		tick |= static_cast<uint64_t>(in[byte]) << (byte * 8);
		hash |= static_cast<uint64_t>(in[8 + byte]) << (byte * 8);
	}
	out.tick = tick;
	out.hash = hash;
	return true;
}

// --- the header ----------------------------------------------------------------

// Everything above the body. An ordinary aggregate filled field by field, then
// `stamp()`ed and `is_valid()`ated — the same contract `SessionConfig` states, and
// for the same reason: a parser has to be able to build a partial object.
struct ReplayHeader {
	int format_version = REPLAY_FORMAT_VERSION;

	// Free-form build identifier — a short commit hash is what the recorder will
	// pass. Compared for equality only, and a mismatch warns.
	char build[REPLAY_BUILD_CHARS] = {};

	// The godot-cpp extension API version string, "4.7.1". Also compared for
	// equality only. Not parsed into components: this file has no opinion about
	// which API versions are compatible, and inventing one here would be a claim
	// nobody has measured.
	char api_version[REPLAY_API_CHARS] = {};

	// Physics ticks per second. See `REPLAY_DEFAULT_TICK_HZ` for why this is here
	// and not in `SessionConfig`.
	int tick_hz = REPLAY_DEFAULT_TICK_HZ;

	// How long the body is, in ticks. The kart count is `config.entry_count`.
	uint64_t tick_count = 0;

	int hash_interval = REPLAY_HASH_INTERVAL;

	SessionConfig config;

	// `config.hash()` as it stood when the replay was recorded. Written into the
	// text so a reader can see it and a `diff` can show it moving, and checked
	// against a re-hash of the parsed config on load: if they disagree, the header
	// is corrupt or was written by a schema this build does not have, and that is a
	// different failure from "the configuration changed".
	uint64_t config_hash = 0;

	bool set_build(const char *id) { return copy_id(id, build, REPLAY_BUILD_CHARS); }
	bool set_api_version(const char *id) {
		return copy_id(id, api_version, REPLAY_API_CHARS);
	}

	// Fill `config_hash` from `config`. Called once, after the config is complete
	// and before anything is written.
	void stamp() { config_hash = config.hash(); }

	// Whether the header describes a replay that can be read at all. Deliberately
	// **not** a version check: a header from the future has to be representable so
	// that `replay_admit` can refuse it by name rather than failing to parse.
	bool is_valid() const {
		if (!config.is_valid()) {
			return false;
		}
		if (tick_hz < 1 || hash_interval < 1) {
			return false;
		}
		if (config.entry_count < 1 || config.entry_count > SESSION_MAX_ENTRIES) {
			return false;
		}
		if (!terminated(build, REPLAY_BUILD_CHARS) ||
				!terminated(api_version, REPLAY_API_CHARS)) {
			return false;
		}
		return true;
	}

	int kart_count() const { return config.entry_count; }

	uint64_t body_bytes() const {
		return tick_count * static_cast<uint64_t>(config.entry_count) *
				static_cast<uint64_t>(REPLAY_INPUT_BYTES);
	}

	// One checkpoint on every tick where `tick % hash_interval == 0`, tick 0
	// included, over `[0, tick_count)`.
	uint64_t checkpoint_count() const {
		if (hash_interval < 1) {
			return 0;
		}
		const uint64_t interval = static_cast<uint64_t>(hash_interval);
		return (tick_count + interval - 1) / interval;
	}

	uint64_t footer_bytes() const {
		return checkpoint_count() * static_cast<uint64_t>(REPLAY_CHECKPOINT_BYTES);
	}

	bool is_checkpoint_tick(uint64_t tick) const {
		return hash_interval > 0 && (tick % static_cast<uint64_t>(hash_interval)) == 0;
	}

private:
	static bool copy_id(const char *id, char *out, int cap) {
		for (int i = 0; i < cap; ++i) {
			out[i] = '\0';
		}
		if (id == nullptr) {
			return false;
		}
		int i = 0;
		while (i < cap - 1 && id[i] != '\0') {
			out[i] = id[i];
			++i;
		}
		return id[i] == '\0';
	}

	static bool terminated(const char *text, int cap) {
		for (int i = 0; i < cap; ++i) {
			if (text[i] == '\0') {
				return true;
			}
		}
		return false;
	}
};

// --- text ----------------------------------------------------------------------
//
// Same family as `tuning.h`'s text section: hand-rolled, locale-free, into a
// caller-supplied buffer, six decimals on every number that has any. `format_value`
// and `format_hex64` are reused rather than reimplemented, and the tuning diff is
// `format_entry`'s output verbatim, so the preset format and the replay header
// cannot drift apart.

// The fixed lines come to about 300 characters; the rest is the tuning diff, at up
// to `TUNING_LINE_CHARS` per moved tunable. Sized for every tunable moved at once,
// which is not a realistic preset but is a realistic fuzz case.
inline constexpr int REPLAY_HEADER_CHARS = 512 + TUNING_LINE_CHARS * TUNABLE_COUNT;


// Parse "0x" followed by one to sixteen hex digits. Strict: anything else fails
// rather than parsing a prefix, because a half-read hash is a hash that compares
// unequal for a reason nobody will find.
inline bool replay_parse_hex64(const char *text, int len, uint64_t &out) {
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

// Parse a decimal integer with no sign allowed. Used for counts, which cannot be
// negative and where a "-1" in a file is corruption rather than a value.
inline bool replay_parse_uint(const char *text, int len, uint64_t &out) {
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

// The enum name lookups, driven off `session.h`'s own name functions so that there
// is exactly one list of these strings in the project. A second list is how the
// error message ends up naming a layout the file does not contain.
inline bool replay_parse_layout(const char *text, int len, TrackLayout &out) {
	for (int i = 0; i < TRACK_LAYOUT_COUNT; ++i) {
		const char *name = track_layout_name(static_cast<TrackLayout>(i));
		int n = 0;
		while (name[n] != '\0') {
			++n;
		}
		if (n != len) {
			continue;
		}
		bool same = true;
		for (int c = 0; c < n; ++c) {
			if (name[c] != text[c]) {
				same = false;
				break;
			}
		}
		if (same) {
			out = static_cast<TrackLayout>(i);
			return true;
		}
	}
	return false;
}

inline bool replay_parse_condition(const char *text, int len, TrackCondition &out) {
	for (int i = 0; i < TRACK_CONDITION_COUNT; ++i) {
		const char *name = track_condition_name(static_cast<TrackCondition>(i));
		int n = 0;
		while (name[n] != '\0') {
			++n;
		}
		if (n != len) {
			continue;
		}
		bool same = true;
		for (int c = 0; c < n; ++c) {
			if (name[c] != text[c]) {
				same = false;
				break;
			}
		}
		if (same) {
			out = static_cast<TrackCondition>(i);
			return true;
		}
	}
	return false;
}

inline bool replay_parse_session_type(const char *text, int len, SessionType &out) {
	for (int i = 0; i < SESSION_TYPE_COUNT; ++i) {
		const char *name = session_type_name(static_cast<SessionType>(i));
		int n = 0;
		while (name[n] != '\0') {
			++n;
		}
		if (n != len) {
			continue;
		}
		bool same = true;
		for (int c = 0; c < n; ++c) {
			if (name[c] != text[c]) {
				same = false;
				break;
			}
		}
		if (same) {
			out = static_cast<SessionType>(i);
			return true;
		}
	}
	return false;
}

inline bool replay_parse_kart_class(const char *text, int len, KartClass &out) {
	for (int i = 0; i < KART_CLASS_COUNT; ++i) {
		const char *name = kart_class_name(static_cast<KartClass>(i));
		int n = 0;
		while (name[n] != '\0') {
			++n;
		}
		if (n != len) {
			continue;
		}
		bool same = true;
		for (int c = 0; c < n; ++c) {
			if (name[c] != text[c]) {
				same = false;
				break;
			}
		}
		if (same) {
			out = static_cast<KartClass>(i);
			return true;
		}
	}
	return false;
}

inline bool replay_parse_limit_kind(const char *text, int len, SessionLimitKind &out) {
	for (int i = 0; i < SESSION_LIMIT_KIND_COUNT; ++i) {
		const char *name = session_limit_kind_name(static_cast<SessionLimitKind>(i));
		int n = 0;
		while (name[n] != '\0') {
			++n;
		}
		if (n != len) {
			continue;
		}
		bool same = true;
		for (int c = 0; c < n; ++c) {
			if (name[c] != text[c]) {
				same = false;
				break;
			}
		}
		if (same) {
			out = static_cast<SessionLimitKind>(i);
			return true;
		}
	}
	return false;
}

// Write the header. Returns the length written, or -1 if it did not fit.
//
// **Byte-identical for identical input, always.** Fixed field order, fixed
// formatting, tuning entries in declaration order — the same three properties
// `KartTuning::to_text` needs and for the same reason: a header that reordered
// itself would show every line as changed in a diff of a run that changed one.
inline int replay_format_header(const ReplayHeader &header, char *out, int cap) {
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
	auto put_hex = [&](uint64_t value) {
		if (format_hex64(value, scratch, static_cast<int>(sizeof(scratch))) < 0) {
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

	put("# kartgame replay. Text header, binary body, checkpointed footer.\n"
		"#\n"
		"# Everything below `format` is the session this was recorded from, and\n"
		"# `config_hash` fingerprints all of it. Playback compares that hash first:\n"
		"# a mismatch is refused and names the field, and only a matching config can\n"
		"# report a determinism bug. See src/core/replay.h and DECISIONS ADR-0041.\n"
		"#\n"
		"# Lines with an `=` are the tuning diff, in the preset format: a tunable\n"
		"# with no line here is at its default.\n");
	put("format ");
	put_int(header.format_version);
	put("\n");
	put("build ");
	put(header.build[0] == '\0' ? "unknown" : header.build);
	put("\n");
	put("api ");
	put(header.api_version[0] == '\0' ? "unknown" : header.api_version);
	put("\n");
	put("tick_hz ");
	put_int(header.tick_hz);
	put("\n");
	put("ticks ");
	put_int(static_cast<int64_t>(header.tick_count));
	put("\n");
	put("hash_interval ");
	put_int(header.hash_interval);
	put("\n");
	put("\n");

	put("track ");
	put(header.config.track_id);
	put("\n");
	put("track_hash ");
	put_hex(header.config.track_hash);
	put("\n");
	put("layout ");
	put(track_layout_name(header.config.layout));
	put("\n");
	put("condition ");
	put(track_condition_name(header.config.condition));
	put("\n");
	put("session ");
	put(session_type_name(header.config.type));
	put("\n");
	put("class ");
	put(kart_class_name(header.config.kart_class));
	put("\n");
	put("limit ");
	put(session_limit_kind_name(header.config.limit.kind));
	put(" ");
	put_value(header.config.limit.value);
	put("\n");
	put("entries ");
	put_int(header.config.entry_count);
	put("\n");
	put("roster_hash ");
	put_hex(header.config.roster_hash);
	put("\n");
	put("auto_clutch ");
	put_int(header.config.assists.auto_clutch ? 1 : 0);
	put("\n");
	put("auto_shift ");
	put_int(header.config.assists.auto_shift ? 1 : 0);
	put("\n");
	put("seed ");
	put_hex(header.config.seed);
	put("\n");
	put("config_hash ");
	put_hex(header.config_hash);
	put("\n");

	// The tuning diff. Declaration order, `format_entry`'s own lines, and nothing
	// at all when nothing has been tuned.
	if (header.config.tuning.changed_count() > 0) {
		put("\n");
		for (int id = 0; id < TUNABLE_COUNT; ++id) {
			if (format_entry(header.config.tuning, id, scratch,
						static_cast<int>(sizeof(scratch))) > 0) {
				put(scratch);
				put("\n");
			}
		}
	}

	if (overflow || written >= cap) {
		return -1;
	}
	out[written] = '\0';
	return written;
}

// What went wrong in a parse. A line number and a key, because "the header did not
// parse" is not a sentence anyone can act on.
enum class ReplayParseProblem : int {
	None = 0,
	MalformedLine, // `parse_line` rejected it outright
	UnknownKey, // a header line this format does not have
	BadValue, // the key is known, the value is not readable
	UnknownTunable, // a tuning entry naming a key this build does not have
	MissingField, // a required header line never appeared
	ConfigHashMismatch, // the recorded hash disagrees with a re-hash of the config
	Invalid, // parsed, but `is_valid()` rejects the result
};

inline const char *replay_parse_problem_name(ReplayParseProblem problem) {
	switch (problem) {
		case ReplayParseProblem::None: return "none";
		case ReplayParseProblem::MalformedLine: return "malformed line";
		case ReplayParseProblem::UnknownKey: return "unknown key";
		case ReplayParseProblem::BadValue: return "bad value";
		case ReplayParseProblem::UnknownTunable: return "unknown tunable";
		case ReplayParseProblem::MissingField: return "missing field";
		case ReplayParseProblem::ConfigHashMismatch: return "config hash mismatch";
		case ReplayParseProblem::Invalid: return "invalid header";
	}
	return "invalid";
}

struct ReplayParse {
	bool ok = false;
	int line = 0; // 1-based
	ReplayParseProblem problem = ReplayParseProblem::None;
	char key[TUNING_KEY_CHARS] = {};
};

// The required header lines, in the order they are written. A file missing one of
// these is rejected rather than defaulted: ADR-0042's best-effort load is rejected
// for a *save*, and a replay has even less claim to guess — a defaulted `tick_hz`
// is a silently different simulation.
//
// `build` and `api` are deliberately absent from this list. They are diagnostic,
// they warn rather than refuse, and a replay recorded by a tool that did not know
// its own commit hash is still a replay.
inline constexpr const char *REPLAY_REQUIRED_KEYS[] = {
	"format", "tick_hz", "ticks", "hash_interval", "track", "track_hash", "layout",
	"condition", "session", "class", "limit", "entries", "roster_hash", "auto_clutch",
	"auto_shift", "seed", "config_hash"
};
inline constexpr int REPLAY_REQUIRED_KEY_COUNT =
		static_cast<int>(sizeof(REPLAY_REQUIRED_KEYS) / sizeof(REPLAY_REQUIRED_KEYS[0]));

// Parse a header out of `text`. Reads to `len`, or to the first NUL if `len` is
// negative. Fills `out` and returns a report; on failure `out` is whatever was
// parsed up to the failure and must not be used.
//
// **The result is re-hashed and checked against the recorded `config_hash`.** That
// is not belt-and-braces: it is what distinguishes "the configuration changed"
// from "this reader and that writer do not agree about the schema", and those two
// need different sentences.
inline ReplayParse replay_parse_header(const char *text, int len, ReplayHeader &out) {
	ReplayParse result;
	if (text == nullptr) {
		result.problem = ReplayParseProblem::MalformedLine;
		return result;
	}
	if (len < 0) {
		len = 0;
		while (text[len] != '\0') {
			++len;
		}
	}

	out = ReplayHeader();
	// Nothing is defaulted into the config: a field that never appears has to be
	// reported, and a defaulted TuningSet is the one exception because "no entry
	// line" *means* default in this format.
	bool seen[REPLAY_REQUIRED_KEY_COUNT] = {};
	uint64_t recorded_config_hash = 0;

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
		for (int i = 0; i < REPLAY_REQUIRED_KEY_COUNT; ++i) {
			if (key_matches(key, REPLAY_REQUIRED_KEYS[i])) {
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
		if (parsed.kind == ParsedLine::Invalid) {
			result.problem = ReplayParseProblem::MalformedLine;
			return result;
		}
		if (parsed.kind == ParsedLine::Entry) {
			// A tuning diff line. `parse_line` has already read the value on the
			// 1e-6 grid, so this round-trips exactly.
			if (parsed.id < 0) {
				result.problem = ReplayParseProblem::UnknownTunable;
				copy_key(parsed.key);
				return result;
			}
			out.config.tuning.set(parsed.id, parsed.value);
			continue;
		}

		// A header line: `key text`.
		const char *key = parsed.key;
		const char *value = parsed.text;
		int value_length = 0;
		while (value[value_length] != '\0') {
			++value_length;
		}
		copy_key(key);

		if (key_matches(key, "format")) {
			uint64_t number = 0;
			if (!replay_parse_uint(value, value_length, number) || number > 0x7FFFFFFFULL) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
			out.format_version = static_cast<int>(number);
		} else if (key_matches(key, "build")) {
			if (!out.set_build(value)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "api")) {
			if (!out.set_api_version(value)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "tick_hz")) {
			uint64_t number = 0;
			if (!replay_parse_uint(value, value_length, number) || number < 1 ||
					number > 0x7FFFFFFFULL) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
			out.tick_hz = static_cast<int>(number);
		} else if (key_matches(key, "ticks")) {
			if (!replay_parse_uint(value, value_length, out.tick_count)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "hash_interval")) {
			uint64_t number = 0;
			if (!replay_parse_uint(value, value_length, number) || number < 1 ||
					number > 0x7FFFFFFFULL) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
			out.hash_interval = static_cast<int>(number);
		} else if (key_matches(key, "track")) {
			if (!out.config.set_track_id(value)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "track_hash")) {
			if (!replay_parse_hex64(value, value_length, out.config.track_hash)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "layout")) {
			if (!replay_parse_layout(value, value_length, out.config.layout)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "condition")) {
			if (!replay_parse_condition(value, value_length, out.config.condition)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "session")) {
			if (!replay_parse_session_type(value, value_length, out.config.type)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "class")) {
			if (!replay_parse_kart_class(value, value_length, out.config.kart_class)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "limit")) {
			// "kind value" — one line, because `SessionLimit` is one field with a
			// number attached and two lines would let a file say it is both 6 laps
			// and open.
			int split = 0;
			while (split < value_length && value[split] != ' ' && value[split] != '\t') {
				++split;
			}
			if (!replay_parse_limit_kind(value, split, out.config.limit.kind)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
			int number_start = split;
			while (number_start < value_length &&
					(value[number_start] == ' ' || value[number_start] == '\t')) {
				++number_start;
			}
			if (!parse_value(value + number_start, value_length - number_start,
						out.config.limit.value)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "entries")) {
			uint64_t number = 0;
			if (!replay_parse_uint(value, value_length, number) || number < 1 ||
					number > static_cast<uint64_t>(SESSION_MAX_ENTRIES)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
			out.config.entry_count = static_cast<int>(number);
		} else if (key_matches(key, "roster_hash")) {
			if (!replay_parse_hex64(value, value_length, out.config.roster_hash)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "auto_clutch") || key_matches(key, "auto_shift")) {
			uint64_t number = 0;
			if (!replay_parse_uint(value, value_length, number) || number > 1) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
			if (key_matches(key, "auto_clutch")) {
				out.config.assists.auto_clutch = number != 0;
			} else {
				out.config.assists.auto_shift = number != 0;
			}
		} else if (key_matches(key, "seed")) {
			if (!replay_parse_hex64(value, value_length, out.config.seed)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else if (key_matches(key, "config_hash")) {
			if (!replay_parse_hex64(value, value_length, recorded_config_hash)) {
				result.problem = ReplayParseProblem::BadValue;
				return result;
			}
		} else {
			result.problem = ReplayParseProblem::UnknownKey;
			return result;
		}
		mark_seen(key);
	}

	result.line = 0;
	result.key[0] = '\0';
	for (int i = 0; i < REPLAY_REQUIRED_KEY_COUNT; ++i) {
		if (!seen[i]) {
			result.problem = ReplayParseProblem::MissingField;
			copy_key(REPLAY_REQUIRED_KEYS[i]);
			return result;
		}
	}

	out.config_hash = recorded_config_hash;
	if (!out.is_valid()) {
		result.problem = ReplayParseProblem::Invalid;
		return result;
	}
	// The self-check. Both sides of this comparison are quantized on the same 1e-6
	// grid that the text is written on — `SessionConfig::hash` uses
	// `SESSION_HASH_QUANTUM`, which is `TUNING_QUANTUM`, and `format_value` prints
	// exactly six decimals — so the round trip preserves the hash by construction
	// rather than by luck.
	if (out.config.hash() != recorded_config_hash) {
		result.problem = ReplayParseProblem::ConfigHashMismatch;
		copy_key("config_hash");
		return result;
	}

	result.ok = true;
	result.problem = ReplayParseProblem::None;
	return result;
}

// --- playback ------------------------------------------------------------------
//
// **Three outcomes, and *refused* is not a flavor of red.** ADR-0041's consequence
// section is explicit that the determinism harness gains a failure mode it did not
// have, and that CI must treat it as its own thing. A refusal says "this replay
// cannot answer the question"; a divergence says "it answered, and the answer is
// that the simulation is not deterministic". Folding them together turns every
// stale replay into a determinism alarm, and the alarms then get ignored.

enum class PlaybackVerdict : int {
	// Admitted, and every checkpoint compared so far agrees.
	Passed = 0,
	// Admitted, and a checkpoint disagreed. A real determinism bug.
	Diverged = 1,
	// Not admitted. The question was never asked.
	Refused = 2,
};

inline const char *playback_verdict_name(PlaybackVerdict verdict) {
	switch (verdict) {
		case PlaybackVerdict::Passed: return "passed";
		case PlaybackVerdict::Diverged: return "diverged";
		case PlaybackVerdict::Refused: return "refused";
	}
	return "invalid";
}

enum class RefusalReason : int {
	None = 0,
	// `format_version` differs. Refused rather than migrated, per ADR-0041.
	FormatVersion,
	// The header's own `config_hash` disagrees with a re-hash of its config.
	HeaderCorrupt,
	// `config_hash` differs from the live configuration's.
	ConfigMismatch,
	// Same configuration, different integration rate. See `REPLAY_DEFAULT_TICK_HZ`.
	TickRate,
	// The body is shorter than `tick_count` times the field says it should be.
	TruncatedBody,
};

inline const char *refusal_reason_name(RefusalReason reason) {
	switch (reason) {
		case RefusalReason::None: return "none";
		case RefusalReason::FormatVersion: return "format version";
		case RefusalReason::HeaderCorrupt: return "corrupt header";
		case RefusalReason::ConfigMismatch: return "configuration mismatch";
		case RefusalReason::TickRate: return "tick rate";
		case RefusalReason::TruncatedBody: return "truncated body";
	}
	return "invalid";
}

struct PlaybackReport {
	PlaybackVerdict verdict = PlaybackVerdict::Refused;
	RefusalReason reason = RefusalReason::None;

	// Which configuration field moved, when `reason` is `ConfigMismatch`. Comes
	// from `session.h`'s `first_difference`, which is the single owner of the field
	// order and of these names.
	SessionField field = SessionField::None;

	// Which tunable moved, when `field` is `Tuning`. -1 otherwise. `session.h`
	// deliberately reports tuning as one field and leaves the per-key answer to
	// `tuning.h`; this is where the two are joined, once.
	int tunable_id = -1;

	// Recorded on a different build, or against a different extension API. Both
	// warn and play, because cross-build determinism is not claimed.
	bool build_warning = false;
	bool api_warning = false;

	// The first checkpoint that disagreed.
	uint64_t diverged_tick = 0;
	uint64_t recorded_hash = 0;
	uint64_t live_hash = 0;
	int checkpoints_compared = 0;

	bool admitted() const { return verdict != PlaybackVerdict::Refused; }
	bool warned() const { return build_warning || api_warning; }
};

// Compare the recorded header against a header describing the live environment,
// and decide whether the replay may be played at all.
//
// The `live` argument is a full `ReplayHeader` rather than a bare `SessionConfig`
// so that the build, the API version and the tick rate are compared by the same
// call that compares the configuration. A caller builds one describing *now* —
// current build, current API, current tick rate, the configuration it is about to
// run — and hands both in.
//
// **Order matters and is ADR-0041's.** Format first, because a header this build
// cannot read cannot be trusted to say anything. Then the header's self-consistency,
// because a corrupt header is not a changed configuration. Then `config_hash`,
// which is the ADR's "first" among the things that describe the run. Then the tick
// rate, which the ADR does not mention and which would otherwise be reported as a
// determinism bug. Warnings last, because they never change the verdict.
inline PlaybackReport replay_admit(const ReplayHeader &recorded, const ReplayHeader &live) {
	PlaybackReport report;

	if (recorded.format_version != live.format_version) {
		report.verdict = PlaybackVerdict::Refused;
		report.reason = RefusalReason::FormatVersion;
		return report;
	}
	if (recorded.config_hash != recorded.config.hash()) {
		report.verdict = PlaybackVerdict::Refused;
		report.reason = RefusalReason::HeaderCorrupt;
		return report;
	}
	if (recorded.config_hash != live.config.hash()) {
		report.verdict = PlaybackVerdict::Refused;
		report.reason = RefusalReason::ConfigMismatch;
		report.field = first_difference(recorded.config, live.config);
		if (report.field == SessionField::Tuning) {
			for (int id = 0; id < TUNABLE_COUNT; ++id) {
				if (tuning_micro(recorded.config.tuning.get(id)) !=
						tuning_micro(live.config.tuning.get(id))) {
					report.tunable_id = id;
					break;
				}
			}
		}
		return report;
	}
	if (recorded.tick_hz != live.tick_hz) {
		report.verdict = PlaybackVerdict::Refused;
		report.reason = RefusalReason::TickRate;
		return report;
	}

	// Admitted. The two version strings are diagnostic only.
	report.verdict = PlaybackVerdict::Passed;
	report.reason = RefusalReason::None;
	for (int i = 0; i < REPLAY_BUILD_CHARS; ++i) {
		if (recorded.build[i] != live.build[i]) {
			report.build_warning = true;
			break;
		}
		if (recorded.build[i] == '\0') {
			break;
		}
	}
	for (int i = 0; i < REPLAY_API_CHARS; ++i) {
		if (recorded.api_version[i] != live.api_version[i]) {
			report.api_warning = true;
			break;
		}
		if (recorded.api_version[i] == '\0') {
			break;
		}
	}
	return report;
}

// Whether the bytes on hand are enough for the body the header describes. Separate
// from `replay_admit` because the header and the body arrive from the Godot side
// separately, and a truncated body is worth its own sentence.
inline bool replay_body_is_complete(const ReplayHeader &header, uint64_t body_bytes) {
	return body_bytes >= header.body_bytes();
}

inline void replay_refuse_truncated(PlaybackReport &report) {
	report.verdict = PlaybackVerdict::Refused;
	report.reason = RefusalReason::TruncatedBody;
}

// Feed one checkpoint pair. The first disagreement sets `Diverged` and records the
// tick; later ones do not overwrite it, because the first divergence is the one
// with diagnostic value — everything after it is downstream of the same cause.
//
// **A refused report is never touched.** That is the structural half of "refused is
// not a flavor of red": a caller that keeps comparing after a refusal cannot turn
// the verdict into a divergence, so a stale replay can never be reported as a
// determinism bug.
inline void replay_compare_checkpoint(PlaybackReport &report, uint64_t tick,
		uint64_t recorded_hash, uint64_t live_hash) {
	if (report.verdict == PlaybackVerdict::Refused) {
		return;
	}
	++report.checkpoints_compared;
	if (recorded_hash == live_hash) {
		return;
	}
	if (report.verdict == PlaybackVerdict::Diverged) {
		return;
	}
	report.verdict = PlaybackVerdict::Diverged;
	report.diverged_tick = tick;
	report.recorded_hash = recorded_hash;
	report.live_hash = live_hash;
}

// The whole footer at once. Ticks must line up: a live checkpoint at a tick the
// recording does not have is a bug in the harness rather than in the solver, and it
// is reported as a divergence on that tick rather than silently skipped.
inline void replay_compare_footer(PlaybackReport &report, const ReplayCheckpoint *recorded,
		int recorded_count, const ReplayCheckpoint *live, int live_count) {
	if (report.verdict == PlaybackVerdict::Refused || recorded == nullptr || live == nullptr) {
		return;
	}
	const int count = recorded_count < live_count ? recorded_count : live_count;
	for (int i = 0; i < count; ++i) {
		if (recorded[i].tick != live[i].tick) {
			replay_compare_checkpoint(report, live[i].tick, recorded[i].hash, ~live[i].hash);
			return;
		}
		replay_compare_checkpoint(report, recorded[i].tick, recorded[i].hash, live[i].hash);
	}
}

// --- the sentence a person reads -----------------------------------------------


inline constexpr int REPLAY_MESSAGE_CHARS = 320;

// ADR-0041's sentence. "This replay was recorded with `frame_torsion` at 210.0; the
// current default is 193.62" is a sentence a person can act on, and
// "config_hash mismatch" is not — so the mismatch case names the field, prints both
// values, and for a tuning difference names the tunable rather than the word
// "tuning".
//
// Returns the length written, or -1 if it did not fit.
inline int replay_describe(const PlaybackReport &report, const ReplayHeader &recorded,
		const ReplayHeader &live, char *out, int cap) {
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
	auto put_field_value = [&](const SessionConfig &config, SessionField field) {
		if (session_field_value(config, field, scratch, static_cast<int>(sizeof(scratch))) < 0) {
			overflow = true;
			return;
		}
		put(scratch);
	};

	switch (report.verdict) {
		case PlaybackVerdict::Refused:
			put("refused: ");
			switch (report.reason) {
				case RefusalReason::FormatVersion:
					put("this replay is format ");
					put_int(recorded.format_version);
					put(" and this build reads format ");
					put_int(live.format_version);
					put(". A replay is not migrated across a format change.");
					break;
				case RefusalReason::HeaderCorrupt:
					put("the header's config_hash is ");
					put_hex(recorded.config_hash);
					put(" but its own configuration hashes to ");
					put_hex(recorded.config.hash());
					put(".");
					break;
				case RefusalReason::ConfigMismatch:
					if (report.field == SessionField::Tuning && report.tunable_id >= 0) {
						put("recorded with ");
						put(TUNABLES[report.tunable_id].key);
						put(" at ");
						put_value(recorded.config.tuning.get(report.tunable_id));
						put("; it is now ");
						put_value(live.config.tuning.get(report.tunable_id));
						put(".");
					} else {
						put("recorded with ");
						put(session_field_name(report.field));
						put(" at ");
						put_field_value(recorded.config, report.field);
						put("; it is now ");
						put_field_value(live.config, report.field);
						put(".");
					}
					break;
				case RefusalReason::TickRate:
					put("recorded at ");
					put_int(recorded.tick_hz);
					put(" Hz and this session runs at ");
					put_int(live.tick_hz);
					put(" Hz. Same inputs, different integration.");
					break;
				case RefusalReason::TruncatedBody:
					put("the body is shorter than the ");
					put_int(static_cast<int64_t>(recorded.tick_count));
					put(" ticks the header declares.");
					break;
				case RefusalReason::None:
					put("no reason recorded, which is itself a bug.");
					break;
			}
			break;
		case PlaybackVerdict::Diverged:
			put("diverged: the state hash disagrees at tick ");
			put_int(static_cast<int64_t>(report.diverged_tick));
			put(" (recorded ");
			put_hex(report.recorded_hash);
			put(", live ");
			put_hex(report.live_hash);
			put("). The configuration matched, so this is a determinism bug.");
			break;
		case PlaybackVerdict::Passed:
			put("passed: ");
			put_int(report.checkpoints_compared);
			put(" checkpoints agree over ");
			put_int(static_cast<int64_t>(recorded.tick_count));
			put(" ticks.");
			break;
	}

	if (report.build_warning) {
		put(" Warning: recorded by build ");
		put(recorded.build[0] == '\0' ? "unknown" : recorded.build);
		put(", running ");
		put(live.build[0] == '\0' ? "unknown" : live.build);
		put("; cross-build determinism is not claimed.");
	}
	if (report.api_warning) {
		put(" Warning: recorded against extension API ");
		put(recorded.api_version[0] == '\0' ? "unknown" : recorded.api_version);
		put(", running ");
		put(live.api_version[0] == '\0' ? "unknown" : live.api_version);
		put(".");
	}

	if (overflow || written >= cap) {
		return -1;
	}
	out[written] = '\0';
	return written;
}

} // namespace kart::core

#endif // KART_CORE_REPLAY_H
