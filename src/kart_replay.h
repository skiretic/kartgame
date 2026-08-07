#ifndef KARTGAME_KART_REPLAY_H
#define KARTGAME_KART_REPLAY_H

#include "core/replay.h"
#include "session/kart_session.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace kartgame {

// GDScript's handle on `kart::core::replay.h`. ROADMAP M6, ADR-0041.
//
// The format header was 1,528 lines of tested arithmetic that nothing could
// reach: no class, no registration, no consumer anywhere in `src/`, `scripts/`
// or `tools/`. That is the failure `CLAUDE.md` calls "a capability built at both
// ends and not joined in the middle", except this one was only built at one end,
// which is worse — the tests were green and the feature did not exist.
//
// This class is the join, and it is deliberately thin. Every arithmetic decision
// stays in `src/core/replay.h`; what lives here is the three things ADR-0017 and
// ADR-0041 put on the Godot side and nowhere else:
//
//   1. **A container.** `replay.h` writes into caller-supplied buffers and never
//      allocates. Somebody has to own the bytes, and a `PackedByteArray` is the
//      shape Godot's file API already speaks.
//   2. **File I/O.** ADR-0041's "where it lives": `replay.h` opens no files, on
//      purpose. The atomic write below is `kart_profile.cpp`'s, for the reasons
//      issue #173 gives, and it is a copy rather than a shared helper because
//      that helper is file-local to a class this one must not depend on.
//   3. **Variant marshalling.** A `DriverInput` is six numbers; GDScript sees the
//      same Dictionary shape `KartBody::input_driver` already returns, so a
//      recorder can hand the identical object to the body and to the recorder.
//
// ## The one thing a caller must get right
//
// `replay.h`'s central argument is that input is quantized **before** the solver
// sees it, never on the way into the file. `record_input` therefore **refuses**
// off-grid input rather than rounding it — `replay_encode_input` returns -1 and
// so does this. A producer calls `snap_input` first, upstream of the vehicle, and
// hands the *snapped* Dictionary to both the body and the recorder. Doing it the
// other way round produces a live run and a replay that consumed different
// doubles, and the divergence that follows looks exactly like a solver bug.
//
// `snap_input` is bound as a static, so a producer does not need a recorder in
// hand to do the right thing.
//
// ## What this class does not do
//
// It does not drive anything. There is no `ReplayDriver` node here, because the
// producer and consumer of an input stream is whatever is holding the vehicle —
// `tools/verify/replay_probe.gd` is the first one and it feeds the stream through
// the same `input_driver` Callable every other scripted run uses. A driver node
// would be a fourth way for input to reach `KartBody` and ADR-0040 says there are
// two.
class KartReplay : public godot::RefCounted {
	GDCLASS(KartReplay, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// `PlaybackVerdict` and `RefusalReason`, as class constants, for the same
	// reason `KartSession` binds its enums: a bare integer compared against the
	// wrong constant is silent, and this one decides whether a red is a
	// determinism bug or a stale file.
	enum {
		VERDICT_PASSED = static_cast<int>(kart::core::PlaybackVerdict::Passed),
		VERDICT_DIVERGED = static_cast<int>(kart::core::PlaybackVerdict::Diverged),
		VERDICT_REFUSED = static_cast<int>(kart::core::PlaybackVerdict::Refused),

		REFUSAL_NONE = static_cast<int>(kart::core::RefusalReason::None),
		REFUSAL_FORMAT_VERSION = static_cast<int>(kart::core::RefusalReason::FormatVersion),
		REFUSAL_HEADER_CORRUPT = static_cast<int>(kart::core::RefusalReason::HeaderCorrupt),
		REFUSAL_CONFIG_MISMATCH = static_cast<int>(kart::core::RefusalReason::ConfigMismatch),
		REFUSAL_TRUNCATED_BODY = static_cast<int>(kart::core::RefusalReason::TruncatedBody),

		// `replay.h`'s own defaults, so a probe can print them rather than
		// restating them. A second copy of `120` in GDScript is a second owner of
		// a number the format defines.
		FORMAT_VERSION = kart::core::REPLAY_FORMAT_VERSION,
		HASH_INTERVAL = kart::core::REPLAY_HASH_INTERVAL,
		INPUT_BYTES = kart::core::REPLAY_INPUT_BYTES,
		CHECKPOINT_BYTES = kart::core::REPLAY_CHECKPOINT_BYTES,
	};

	// --- the producer's half ------------------------------------------------

	// One tick of intent, snapped onto the storage grid. **Call this upstream of
	// the vehicle and hand the result to both.** Static, because the correct
	// caller is a driver that has no reason to hold a recorder.
	//
	// Reads and writes the `input_driver` Dictionary shape — `throttle`, `brake`,
	// `steer`, `clutch`, `shift_up`, `shift_down` — and always returns all six
	// keys, so a caller cannot lose one by round-tripping through here.
	static godot::Dictionary snap_input(const godot::Dictionary &p_input);

	// Whether a Dictionary is already exactly on the grid. Bitwise, not a
	// tolerance; `replay.h` explains why a tolerance here defeats the point.
	static bool is_on_grid(const godot::Dictionary &p_input);

	// --- recording ------------------------------------------------------------

	// Start a recording of `p_session`. Copies the configuration, stamps
	// `config_hash`, fills `build` and `api` from this binary, and throws away
	// anything previously recorded or loaded.
	//
	// Refuses a null or invalid session rather than recording a run that can never
	// be admitted: `replay_admit` would refuse it later, a long way from here and
	// after a full re-simulation had already been paid for.
	bool begin_record(const godot::Ref<KartSession> &p_session);

	// How often a checkpoint is expected. Settable before `begin_record` or right
	// after it; the format allows any interval and the probe tightens it to 1 so
	// that "the first divergent tick" is a tick and not an interval.
	bool set_hash_interval(int p_interval);
	int get_hash_interval() const;

	// Append one tick for a single-kart recording. Returns false — and appends
	// nothing — when the input is not already on the grid.
	bool record_input(const godot::Dictionary &p_input);

	// Append one tick for the whole field, in kart order. The array length must be
	// the session's `entry_count`: `replay.h` has no `kart_count` field precisely
	// so that a second copy cannot disagree with the first, and a short array here
	// would be that disagreement arriving one layer up.
	bool record_tick(const godot::Array &p_inputs);

	// Record a state hash for a tick. Takes the hex string `KartStateHash.hex()`
	// returns, because GDScript's int is signed and a digest past 2^63 arrives
	// negative — `KartSession` takes its hashes as hex for the same reason.
	//
	// Refuses a tick the header's interval does not call for, so a footer cannot
	// quietly acquire entries at ticks a re-simulation will never checkpoint.
	bool record_checkpoint(int64_t p_tick, const godot::String &p_hash_hex);

	// Whether `p_tick` is one the current interval checkpoints on.
	bool is_checkpoint_tick(int64_t p_tick) const;

	// Close the recording: stamp `tick_count` and re-validate. Nothing may be
	// appended afterwards, and `save` refuses an unfinalized recording — a header
	// declaring zero ticks over a full body is a file that reads as empty.
	bool finalize();
	bool is_finalized() const;

	// --- the file ---------------------------------------------------------------

	// Write to `p_path` through `p_path + ".tmp"` in the same directory, then
	// rename. `kart_profile.cpp`'s sequence exactly: never open the target, flush,
	// close, confirm the length off disk, `F_FULLFSYNC` the temp, rename, fsync the
	// directory. Returns a Godot `Error`; `warnings()` carries the sentences.
	godot::Error save(const godot::String &p_path);

	// Read a replay back. Returns `OK`, or the error that stopped it. A parse
	// failure fills `parse_problem`, `parse_line` and `parse_key` — "the header did
	// not parse" is not a sentence anybody can act on, which is why `replay.h`
	// reports a line and a key.
	godot::Error load(const godot::String &p_path);

	// The sentences from the last `save` or `load`. Cleared at the start of each.
	godot::PackedStringArray warnings() const;

	godot::String parse_problem() const;
	int parse_line() const;
	godot::String parse_key() const;

	// --- reading the header ------------------------------------------------------

	int get_format_version() const;
	godot::String get_build() const;
	godot::String get_api_version() const;
	int64_t get_tick_count() const;
	int get_kart_count() const;
	godot::String config_hash_hex() const;

	// The recorded configuration, as a `KartSession` a caller can inspect, compare
	// or push onto a tuning registry. A fresh object each call — this is the
	// recorded run's configuration and handing out an alias would let a caller
	// edit the thing it is about to be judged against.
	godot::Ref<KartSession> to_session() const;

	// The header exactly as it is written to disk. What a person reads, diffs and
	// files a bug with, and what `--report` prints.
	godot::String header_text() const;

	// --- reading the body and footer ---------------------------------------------

	// Tick `p_tick`, kart `p_kart`, as the `input_driver` Dictionary shape. Returns
	// an empty Dictionary out of range, which a caller must check: an empty one
	// read with `.get(key, 0.0)` is neutral input, and neutral input is a plausible
	// tick rather than an error.
	godot::Dictionary input_at(int64_t p_tick, int p_kart) const;

	int64_t checkpoint_count() const;
	int64_t checkpoint_tick(int64_t p_index) const;
	godot::String checkpoint_hash_hex(int64_t p_index) const;

	// The hash recorded at a tick, or an empty String if there is none. What a
	// player-back compares against without having to know the interval.
	godot::String checkpoint_hash_at(int64_t p_tick) const;

	// --- playback ------------------------------------------------------------------

	// Decide whether this replay may be played against `p_live` at all, and reset
	// the report. `replay_admit`'s order, which is ADR-0041's: format, then the
	// header's self-consistency, then the configuration, then the warnings that
	// never change a verdict.
	//
	// **Refused is not a flavor of red.** A refusal says the replay cannot answer
	// the question; a divergence says it answered and the answer is that the
	// simulation is not deterministic. A harness that folds them together turns
	// every stale replay into a determinism alarm and the alarms get ignored.
	bool begin_playback(const godot::Ref<KartSession> &p_live);

	// Feed one live checkpoint. Ignored entirely on a refused report, which is what
	// makes the sentence above structural rather than advisory.
	void compare_checkpoint(int64_t p_tick, const godot::String &p_live_hash_hex);

	// Compare every recorded checkpoint against a list of live ones, in order.
	// The array is hex strings at the recording's own checkpoint ticks.
	void compare_footer(const godot::PackedStringArray &p_live_hashes);

	int verdict() const;
	godot::String verdict_name() const;
	int refusal_reason() const;
	godot::String refusal_name() const;

	// ADR-0041's sentence: "recorded with frame_torsion at 210.0; the current
	// default is 193.62" rather than "config_hash mismatch".
	godot::String describe() const;

	int64_t diverged_tick() const;
	godot::String recorded_hash_hex() const;
	godot::String live_hash_hex() const;
	int checkpoints_compared() const;
	bool build_warning() const;
	bool api_warning() const;

	// --- deliberate sabotage, for the negative controls ------------------------------

	// Flip the low bit of one recorded axis, in place. **This exists so that
	// `replay.sh --break` can prove the harness catches a corrupted input stream**,
	// and it is the smallest possible corruption: one code of one axis, which is
	// 1.53e-5 of throttle or 3.05e-5 of lock.
	//
	// It is in the shipped class rather than in the probe because the alternative
	// is a probe that reaches into the byte array itself, and then the negative
	// control is testing the probe's understanding of the layout rather than the
	// format's. `axis` is 0 throttle, 1 brake, 2 clutch, 3 steer.
	//
	// A gate that cannot fail is not a gate; this is the lever that makes this one
	// fail on demand.
	bool perturb_input(int64_t p_tick, int p_kart, int p_axis);

	// Drop the last `p_bytes` of the body, so `replay_body_is_complete` refuses.
	// The other half of the same argument.
	bool truncate_body(int64_t p_bytes);

private:
	// The build identifier this binary stamps into a recording. Version, target,
	// platform and arch, which is everything `KartCore::build_info` knows and all
	// that distinguishes two builds of the same source here. Compared for equality
	// only, and a mismatch warns rather than refusing — cross-build determinism is
	// not claimed anywhere in this project.
	static godot::String this_build();

	// The **running** engine's version, not the compiled-against extension API.
	// Stated plainly because it is a deviation: godot-cpp exposes no version macro
	// this build could read at compile time, and inventing one from the submodule's
	// tag would be a number nobody measured. `replay.h` only ever compares this for
	// equality and only ever warns on it, so the running version is the honest
	// thing to record — it is what actually decided the arithmetic.
	static godot::String this_api_version();

	// Fill a header describing *now* from a live session, for `replay_admit`.
	bool live_header(const godot::Ref<KartSession> &p_live, kart::core::ReplayHeader &out) const;

	static godot::String hex64(uint64_t value);
	static bool parse_hex64(const godot::String &text, uint64_t &out);

	kart::core::ReplayHeader header_;
	godot::PackedByteArray body_;
	godot::PackedByteArray footer_;

	// The recording's own tick counter. `header_.tick_count` is only filled at
	// `finalize`, so that a half-written recording cannot be saved as a whole one.
	int64_t recorded_ticks_ = 0;
	bool recording_ = false;
	bool finalized_ = false;
	bool loaded_ = false;

	kart::core::PlaybackReport report_;
	kart::core::ReplayHeader live_header_;
	bool playing_ = false;

	godot::PackedStringArray warnings_;
	kart::core::ReplayParse parse_;
};

} // namespace kartgame

#endif // KARTGAME_KART_REPLAY_H
