#ifndef KARTGAME_SESSION_KART_GHOST_H
#define KARTGAME_SESSION_KART_GHOST_H

#include "core/ghost.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <vector>

namespace kartgame {

// The shell's view of a ghost lap. ROADMAP M3c, ADR-0041 and ADR-0042.
//
// `src/core/ghost.h` is the format: the quantization, the Catmull-Rom, the header
// and its parser. It contains no file I/O at all, by design. This class is the
// only place a ghost meets Godot, and it does exactly three things the core cannot:
//
//   1. **Records.** A scene hands it a `Transform3D` once per physics tick and it
//      samples every fourth one, which is `GHOST_TICKS_PER_SAMPLE` at
//      `project.godot`'s 120 Hz.
//   2. **Plays back.** A lap time in, a `Transform3D` out, through
//      `ghost_sample_at` and nothing else.
//   3. **Reads and writes `user://ghosts/<id>.ghost`.** The only file I/O in the
//      ghost feature.
//
// ## A ghost is not re-simulated, and this class has no driver in it
//
// ADR-0041 is explicit: a replay re-sims, a ghost is drawn beside a live session
// that is diverging from it by design, so re-simulating it buys nothing and costs a
// second vehicle solve every tick. ADR-0040's producer table disagrees with that in
// one line -- it lists `GhostDriver` as "a recorded input stream played back" --
// and ADR-0041 is the later and correct one. **There is no `GhostDriver` and there
// must not be one.** Nothing here fills a `DriverInput`, nothing here touches
// `KartBody`, and `scripts/game/ghost_kart.gd` is a `Node3D` with a mesh on it.
//
// ## Why the recorder counts its own samples instead of trusting the lap time
//
// `ghost_samples_for_lap` computes `1 + floor(lap_time_s * hz)`, and it is the
// right arithmetic for *sizing a buffer*. It is the wrong arithmetic for filling
// the header, because a lap time is `lap_ticks * step_s` with `step_s = 1/120` in
// double: `5760 * (1.0 / 120.0) * 30.0` is not guaranteed to land on 1440.0 from
// above, and one bit below turns `floor` into 1439 and the count into 1440 against
// a stream that really holds 1441 samples. A header that disagrees with its body by
// one sample is a ghost this class refuses to load, which would make a perfectly
// good best lap unreadable for a reason nobody could see. So `sample_count` is what
// the recorder counted, always, and `ghost_samples_for_lap` is used only to report
// what a lap of a given length is expected to cost.
//
// ## Playback ends at the last sample, not at the finish line
//
// The last sample sits at `(count - 1) / hz`, which is up to one sample interval
// short of the lap time -- 33 ms, which is 0.67 m at 20 m/s. `ghost_sample_at`
// clamps rather than extrapolating, so a caller that drove playback to `lap_time()`
// would hold the ghost still for that last fraction and then jump it back to the
// line. `playback_duration()` is the honest length of the stream and a caller that
// loops a ghost loops on that. Closing the gap needs a cyclic interpolation that
// wraps the last sample back onto the first, which `ghost.h` does not have and
// which this class will not grow a second interpolator to fake.
class KartGhost : public godot::RefCounted {
	GDCLASS(KartGhost, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// `ghost.h`'s constants and enums, as class constants, for the same reason
	// `KartSession` binds its own: a scene that writes `4` for the tick stride is a
	// scene that has to be found and edited if the rate ever moves.
	enum {
		FORMAT_VERSION = kart::core::GHOST_FORMAT_VERSION,
		SAMPLE_HZ = kart::core::GHOST_SAMPLE_HZ,
		TICKS_PER_SAMPLE = kart::core::GHOST_TICKS_PER_SAMPLE,
		SAMPLE_BYTES = kart::core::GHOST_SAMPLE_BYTES,
		MAX_SECTORS = kart::core::GHOST_MAX_SECTORS,

		VERDICT_COMPARABLE = static_cast<int>(kart::core::GhostVerdict::Comparable),
		VERDICT_WARNED = static_cast<int>(kart::core::GhostVerdict::Warned),
		VERDICT_INCOMPARABLE = static_cast<int>(kart::core::GhostVerdict::Incomparable),
	};

	// The most samples one ghost may hold. 36,000 at 30 Hz is 20 minutes, which is
	// longer than any session `GAMEDESIGN.md` §4 schedules, and 648 kB. It exists so
	// that a corrupt `samples` count in a hand-edited file cannot ask for a gigabyte
	// of allocation before anything has looked at the body.
	static constexpr int MAX_SAMPLES = 36000;

	// What `begin_record` reserves: a 180 s lap, which is three times the pace
	// `GAMEDESIGN.md` §4 assumes and 259 kB. Reserved rather than left to grow so
	// that no lap anybody drives reallocates mid-recording. A longer one still
	// works; it just pays for a copy once.
	static constexpr int RESERVE_SAMPLES = 5401;

	// --- what makes a lap comparable -------------------------------------------
	//
	// The header carries no `SessionConfig` and no `config_hash`; `ghost.h` explains
	// at length why not. These are the five fields it does have.

	bool set_track(const godot::String &p_slug);
	godot::String get_track() const;

	// Hex, with or without the `0x`. A String because GDScript has no unsigned
	// 64-bit integer and a hash that arrives negative is a hash somebody will "fix"
	// by taking its absolute value -- `KartSession` takes its hashes the same way.
	bool set_track_hash_hex(const godot::String &p_hex);
	godot::String get_track_hash_hex() const;

	void set_layout(int p_layout);
	int get_layout() const;

	void set_kart_class(int p_kart_class);
	int get_kart_class() const;

	bool set_tuning_hash_hex(const godot::String &p_hex);
	godot::String get_tuning_hash_hex() const;

	// Fill all five from a `KartSession`. `Object *` rather than a typed pointer so
	// this header does not include the session bridge; the cast is checked, the same
	// way `KartSession::adopt_tuning` takes a `KartTuning`.
	bool adopt_session(godot::Object *p_session);

	// --- recording ---------------------------------------------------------------

	// Start over. Drops any samples already held and any completed header times,
	// because a recorder that kept the previous lap's times would produce a ghost
	// whose header describes one lap and whose body describes another.
	void begin_record();

	bool is_recording() const;

	// One physics tick of the kart's transform. Returns true on the ticks that
	// produced a sample, which is every fourth one starting at the first.
	//
	// **Hand it the kart's own transform**, once per tick, from the same
	// `_physics_process` the solver ran in. `vehicle_state.h`'s note applies in
	// spirit: a recorder taps what happened, not what was asked for.
	bool record_tick(const godot::Transform3D &p_transform);

	int recorded_ticks() const;

	// Close the recording with a lap time and its sector times.
	//
	// **`p_sector_durations_s` is per-sector, not cumulative**, because that is what
	// `LapRecord::sector_s` holds and `KartLapTimer::last_sectors()` returns.
	// `GhostHeader::sector_split_s` is cumulative and its last entry must equal the
	// lap time; the accumulation happens here, once, so that no caller has to know
	// which of the two conventions it is holding.
	//
	// Refuses a lap time that is not positive and a recording with no samples. A
	// ghost that cannot be drawn is not a ghost.
	bool finish_record(double p_lap_time_s, const godot::PackedFloat64Array &p_sector_durations_s);

	// Close the recording from a `KartLapTimer`'s **last** lap.
	//
	// Refuses an invalid lap outright: `GAMEDESIGN.md` §3 puts lap validation in
	// Practice next to the ghost, and a best lap set with two wheels on the grass is
	// not a line worth chasing. The reason is returned in the report.
	godot::Dictionary adopt_lap(godot::Object *p_lap_timer);

	// Throw the recording away without touching the header fields.
	void discard();

	// --- reading it back ---------------------------------------------------------

	// Whether this holds a drawable ghost: a valid header and a body that agrees
	// with it.
	bool is_complete() const;

	int sample_count() const;
	int sample_hz() const;
	int format_version() const;

	double lap_time() const;

	// `(sample_count - 1) / sample_hz`. See the class note: this is the length of
	// the stream and it is shorter than the lap.
	double playback_duration() const;

	int sector_count() const;

	// Cumulative, as stored. The last one is the lap time.
	godot::PackedFloat64Array sector_splits() const;

	// Per-sector durations, which is what a timing screen shows next to a split.
	// Derived by subtraction here rather than stored, because `ghost.h` is explicit
	// that accumulating the other direction walks a rounding error down the lap.
	godot::PackedFloat64Array sector_durations() const;

	// Every header field, as a Dictionary. Read-only view for a HUD or a probe.
	godot::Dictionary header() const;

	// What this ghost occupies on disk: the text header plus `18 * sample_count`.
	int64_t byte_size() const;

	// --- playback ----------------------------------------------------------------

	// Where the ghost was `p_time_s` into the lap. Catmull-Rom on position, shortest
	// arc on the angles, clamped at both ends -- `ghost_sample_at` and nothing else.
	godot::Transform3D transform_at_time(double p_time_s) const;

	// Sample `p_index` exactly, with no interpolation in the path at all. The
	// reference `transform_at_time` has to agree with at `p_index / sample_hz`.
	godot::Transform3D transform_at_index(int p_index) const;

	// The stored numbers of one sample, un-reconstructed: position in meters on the
	// 1 mm grid, yaw, pitch and roll in radians on the angle grid. For a probe that
	// has to compare two streams for bit-identity without going through a `Basis`.
	godot::Dictionary sample_at_index(int p_index) const;

	// The same four quantities, interpolated. Exists so that exactness at the
	// samples can be asserted on the stored quantities rather than through the trig
	// of a `Basis` round trip.
	godot::Dictionary sample_at_time(double p_time_s) const;

	// --- comparability -----------------------------------------------------------

	// `ghost_admit` against the session about to be driven. Never a bool: a ghost is
	// comparable, comparable-with-a-caveat, or meaningless, and the middle case is
	// drawable and worth drawing with something said out loud.
	godot::Dictionary compare_to_session(godot::Object *p_session) const;

	// --- ids, and the files they name --------------------------------------------

	// The id for a (track, layout, class) slot.
	//
	// **A pure function of the slot**, and that is the whole design. ADR-0042 keys a
	// `ProfileBest` on exactly those three fields and holds one ghost id per key, so
	// a new best lap on the same slot has to land on the same filename or the old
	// file is orphaned every time somebody goes faster. Minting from the slot makes
	// replacement idempotent: one file per slot, forever, and no sweep needed for
	// the ordinary case of improving your own lap.
	//
	// The shape is `<track>-<lay>-<class>-<hash>`, at most 31 characters so it fits
	// `PROFILE_SLUG_CHARS`:
	//
	//     test_track-for-kz2-3f1a90
	//
	// The track id is truncated to 15 characters and sanitized to `[a-z0-9_-]`, and
	// the six hex digits are FNV-1a over the **untruncated** slot. The hash is not
	// decoration: `SessionConfig::set_track_id` validates length and nothing else, so
	// two circuits sharing a 15-character prefix would otherwise share a ghost file,
	// and the id is pasted into a path where a `.` or a `/` would be worse than that.
	static godot::String mint_id(const godot::String &p_track, int p_layout, int p_kart_class);

	// This ghost's own id, from its own header. Empty when the header has no track.
	godot::String id() const;

	// `user://ghosts/<id>.ghost`. Refuses an id that is not a slug and returns an
	// empty String, rather than building a path out of one.
	static godot::String path_for_id(const godot::String &p_id);

	static bool is_valid_id(const godot::String &p_id);

	// Where ghosts live. One place, so nothing else has to spell it.
	static godot::String ghost_directory();

	// Write it. Text header, a `body` line, then `18 * sample_count` bytes.
	//
	// **Refuses an incomplete ghost**, which is what makes "a lap that was not valid
	// is not saved" a property of this class rather than of every caller. Atomic:
	// written to `<path>.tmp` and renamed over the target, per ADR-0042 -- a save
	// interrupted halfway leaves the previous best lap intact rather than a
	// half-written one.
	godot::Error save(const godot::String &p_path) const;

	// `save(path_for_id(id()))`, which is the call a session runner actually makes.
	godot::Error save_as_id() const;

	// Read one. Returns `{ ok, error, detail, samples, migrated }`.
	//
	// ADR-0042: a ghost is user data and a refused ghost is a deleted best lap, so an
	// older format migrates forward and always loads. What is refused is a file this
	// build cannot understand -- a future version, an unparseable header, or a body
	// that disagrees with its own header, which would draw a kart through the
	// scenery. `detail` names which.
	godot::Dictionary load(const godot::String &p_path);

	godot::Dictionary load_id(const godot::String &p_id);

	// Every id with a file under `user://ghosts/`. Sorted, so two runs of a sweep
	// report the same order.
	static godot::PackedStringArray stored_ids();

	// The ids on disk that `p_kept` does not name -- what a profile no longer points
	// at.
	//
	// **Reported, not deleted.** Nothing in this class removes a ghost file. With a
	// minted id an orphan cannot arise from going faster; it arises from a deleted
	// profile entry, a renamed circuit or a hand-edited save, and in every one of
	// those cases the file is the only copy of a lap somebody drove. ADR-0042's rule
	// for a file it cannot use is "moved aside, not overwritten", and deleting one
	// behind a driver's back is the same mistake with a tidier name. Whoever owns
	// `profile.save` decides; this reports.
	static godot::PackedStringArray orphan_ids(const godot::PackedStringArray &p_kept);

	// What a lap of this length costs, from `ghost.h`'s own arithmetic. Bound so a
	// report and a buffer are reading the same function.
	static int samples_for_lap(double p_lap_time_s);
	static int64_t bytes_for_lap(double p_lap_time_s);

	// Not bound. The header, for anything on the C++ side.
	const kart::core::GhostHeader &ghost_header() const { return header_; }

private:
	// Decoded, already-quantized samples. Decoded rather than kept as bytes so that
	// playback does not run the decoder every frame, and **snapped on the way in** so
	// that what is held is exactly what a file would carry -- a recorder that kept
	// full precision and quantized at `save` would play back one thing before the
	// save and another after it, which is the whole class of bug ADR-0041's
	// quantization note is about, arriving through the door it did not name.
	std::vector<kart::core::GhostSample> samples_;

	kart::core::GhostHeader header_;

	int64_t tick_ = 0;
	bool recording_ = false;
	// Set by `finish_record` and cleared by `begin_record`. Distinct from
	// `header_.is_valid()`: a header can be valid while the body is still being
	// written.
	bool closed_ = false;
	// True when the samples came out of a file that declared an older format
	// version. Reported by `header()` so a UI can say the lap was migrated.
	bool migrated_ = false;
};

} // namespace kartgame

#endif // KARTGAME_SESSION_KART_GHOST_H
