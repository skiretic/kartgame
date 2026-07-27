#include "session/kart_ghost.h"

#include "core/profile.h"
#include "core/state_hash.h"
#include "session/kart_session.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

// `godot::String` decodes a bare `const char *` as Latin-1, so everything crossing
// this boundary goes through `String::utf8`. CLAUDE.md's mojibake trap.
String from_utf8(const char *text) {
	return String::utf8(text);
}

// Parse "0x..." or bare hex into a uint64. Strict, for `kart_session.cpp`'s reason:
// a partial parse is a hash nobody wrote, and a hash that is quietly wrong is worse
// than one that is refused.
bool parse_hex64(const String &p_text, uint64_t &r_out) {
	const CharString utf8 = p_text.strip_edges().utf8();
	const char *c = utf8.get_data();
	if (c == nullptr) {
		return false;
	}
	if (c[0] == '0' && (c[1] == 'x' || c[1] == 'X')) {
		c += 2;
	}
	if (*c == '\0') {
		return false;
	}
	uint64_t value = 0;
	int digits = 0;
	for (; *c != '\0'; ++c) {
		int nibble = -1;
		if (*c >= '0' && *c <= '9') {
			nibble = *c - '0';
		} else if (*c >= 'a' && *c <= 'f') {
			nibble = *c - 'a' + 10;
		} else if (*c >= 'A' && *c <= 'F') {
			nibble = *c - 'A' + 10;
		} else {
			return false;
		}
		if (digits >= 16) {
			return false;
		}
		value = (value << 4) | static_cast<uint64_t>(nibble);
		++digits;
	}
	r_out = value;
	return true;
}

String hex64(uint64_t p_value) {
	char buffer[32];
	if (format_hex64(p_value, buffer, static_cast<int>(sizeof(buffer))) < 0) {
		return String("0x0000000000000000");
	}
	return from_utf8(buffer);
}

// The line between the text header and the binary body.
//
// A sentinel rather than a byte count, and the reason is that the body may contain
// any bytes at all, including newlines: a reader that scanned for "the last header
// line" would be guessing, and one that trusted `samples` to locate the split could
// not detect the disagreement it exists to detect. `ghost_format_header` emits no
// line that begins with `body`, so the **first** occurrence of this sequence is
// always the real one.
constexpr const char *GHOST_BODY_MARKER = "body\n";
constexpr int GHOST_BODY_MARKER_LEN = 5;

// The number of characters of the track id that survive into an id. See
// `KartGhost::mint_id`: 15 + 1 + 3 + 1 + 4 + 1 + 6 is 31, which is
// `PROFILE_SLUG_CHARS - 1`.
constexpr int ID_TRACK_CHARS = 15;
constexpr int ID_LAYOUT_CHARS = 3;
constexpr int ID_CLASS_CHARS = 4;
constexpr int ID_HASH_DIGITS = 6;

// Bumped if the shape of a minted id ever changes, so that the old and new schemes
// cannot collide on a hash and hand two different laps one filename.
constexpr uint64_t ID_SCHEME_TAG = 1;

// A slug character, or `_`. Applied to the track id on its way into a filename.
//
// This is not paranoia about a hostile file: `SessionConfig::set_track_id` and
// `GhostHeader::set_track_id` both validate **length and nothing else**, so a track
// id carrying a `/` or a `..` is accepted everywhere upstream of here and this is
// the first place it becomes a path. `profile.h` says the same thing about the same
// two characters and only enforces it inside `profile.save`.
char slug_char(char c) {
	if (c >= 'a' && c <= 'z') {
		return c;
	}
	if (c >= '0' && c <= '9') {
		return c;
	}
	if (c == '_' || c == '-') {
		return c;
	}
	if (c >= 'A' && c <= 'Z') {
		return static_cast<char>(c - 'A' + 'a');
	}
	return '_';
}

char hex_digit(int nibble) {
	return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('a' + nibble - 10);
}

// Yaw, pitch and roll out of a basis, in the order a vehicle wants them: yaw
// outermost, so gimbal lock sits at a pitch of +/- 90 degrees, which a kart on its
// wheels never reaches. Orthonormalized first, because `get_euler` on a basis
// carrying scale returns angles that do not reconstruct.
//
// `Basis::from_euler(b.get_euler(order), order)` reproduces `b` for a rotation, and
// `tools/verify/ghost_probe.gd` measures that rather than assuming it.
GhostSample sample_from_transform(const Transform3D &p_transform) {
	const Vector3 euler = p_transform.basis.orthonormalized().get_euler(EULER_ORDER_YXZ);
	GhostSample sample;
	sample.position = Vec3(p_transform.origin.x, p_transform.origin.y, p_transform.origin.z);
	sample.yaw = euler.y;
	sample.pitch = euler.x;
	sample.roll = euler.z;
	return sample;
}

Transform3D transform_from_sample(const GhostSample &p_sample) {
	const Basis basis = Basis::from_euler(
			Vector3(p_sample.pitch, p_sample.yaw, p_sample.roll), EULER_ORDER_YXZ);
	return Transform3D(basis,
			Vector3(p_sample.position.x, p_sample.position.y, p_sample.position.z));
}

Dictionary sample_to_dictionary(const GhostSample &p_sample) {
	Dictionary out;
	out["position"] = Vector3(p_sample.position.x, p_sample.position.y, p_sample.position.z);
	out["yaw"] = p_sample.yaw;
	out["pitch"] = p_sample.pitch;
	out["roll"] = p_sample.roll;
	return out;
}

} // namespace

// --- bindings -------------------------------------------------------------------

void KartGhost::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_track", "slug"), &KartGhost::set_track);
	ClassDB::bind_method(D_METHOD("get_track"), &KartGhost::get_track);
	ClassDB::bind_method(D_METHOD("set_track_hash_hex", "hex"), &KartGhost::set_track_hash_hex);
	ClassDB::bind_method(D_METHOD("get_track_hash_hex"), &KartGhost::get_track_hash_hex);
	ClassDB::bind_method(D_METHOD("set_layout", "layout"), &KartGhost::set_layout);
	ClassDB::bind_method(D_METHOD("get_layout"), &KartGhost::get_layout);
	ClassDB::bind_method(D_METHOD("set_kart_class", "kart_class"), &KartGhost::set_kart_class);
	ClassDB::bind_method(D_METHOD("get_kart_class"), &KartGhost::get_kart_class);
	ClassDB::bind_method(D_METHOD("set_tuning_hash_hex", "hex"), &KartGhost::set_tuning_hash_hex);
	ClassDB::bind_method(D_METHOD("get_tuning_hash_hex"), &KartGhost::get_tuning_hash_hex);
	ClassDB::bind_method(D_METHOD("adopt_session", "session"), &KartGhost::adopt_session);

	ClassDB::bind_method(D_METHOD("begin_record"), &KartGhost::begin_record);
	ClassDB::bind_method(D_METHOD("is_recording"), &KartGhost::is_recording);
	ClassDB::bind_method(D_METHOD("record_tick", "transform"), &KartGhost::record_tick);
	ClassDB::bind_method(D_METHOD("recorded_ticks"), &KartGhost::recorded_ticks);
	ClassDB::bind_method(D_METHOD("finish_record", "lap_time_s", "sector_durations_s"),
			&KartGhost::finish_record);
	ClassDB::bind_method(D_METHOD("adopt_lap", "lap_timer"), &KartGhost::adopt_lap);
	ClassDB::bind_method(D_METHOD("discard"), &KartGhost::discard);

	ClassDB::bind_method(D_METHOD("is_complete"), &KartGhost::is_complete);
	ClassDB::bind_method(D_METHOD("sample_count"), &KartGhost::sample_count);
	ClassDB::bind_method(D_METHOD("sample_hz"), &KartGhost::sample_hz);
	ClassDB::bind_method(D_METHOD("format_version"), &KartGhost::format_version);
	ClassDB::bind_method(D_METHOD("lap_time"), &KartGhost::lap_time);
	ClassDB::bind_method(D_METHOD("playback_duration"), &KartGhost::playback_duration);
	ClassDB::bind_method(D_METHOD("sector_count"), &KartGhost::sector_count);
	ClassDB::bind_method(D_METHOD("sector_splits"), &KartGhost::sector_splits);
	ClassDB::bind_method(D_METHOD("sector_durations"), &KartGhost::sector_durations);
	ClassDB::bind_method(D_METHOD("header"), &KartGhost::header);
	ClassDB::bind_method(D_METHOD("byte_size"), &KartGhost::byte_size);

	ClassDB::bind_method(D_METHOD("transform_at_time", "time_s"), &KartGhost::transform_at_time);
	ClassDB::bind_method(D_METHOD("transform_at_index", "index"), &KartGhost::transform_at_index);
	ClassDB::bind_method(D_METHOD("sample_at_index", "index"), &KartGhost::sample_at_index);
	ClassDB::bind_method(D_METHOD("sample_at_time", "time_s"), &KartGhost::sample_at_time);

	ClassDB::bind_method(D_METHOD("compare_to_session", "session"), &KartGhost::compare_to_session);

	ClassDB::bind_method(D_METHOD("id"), &KartGhost::id);
	ClassDB::bind_method(D_METHOD("save", "path"), &KartGhost::save);
	ClassDB::bind_method(D_METHOD("save_as_id"), &KartGhost::save_as_id);
	ClassDB::bind_method(D_METHOD("load", "path"), &KartGhost::load);
	ClassDB::bind_method(D_METHOD("load_id", "id"), &KartGhost::load_id);

	ClassDB::bind_static_method("KartGhost", D_METHOD("mint_id", "track", "layout", "kart_class"),
			&KartGhost::mint_id);
	ClassDB::bind_static_method("KartGhost", D_METHOD("path_for_id", "id"),
			&KartGhost::path_for_id);
	ClassDB::bind_static_method("KartGhost", D_METHOD("is_valid_id", "id"), &KartGhost::is_valid_id);
	ClassDB::bind_static_method("KartGhost", D_METHOD("ghost_directory"),
			&KartGhost::ghost_directory);
	ClassDB::bind_static_method("KartGhost", D_METHOD("stored_ids"), &KartGhost::stored_ids);
	ClassDB::bind_static_method("KartGhost", D_METHOD("orphan_ids", "kept"),
			&KartGhost::orphan_ids);
	ClassDB::bind_static_method("KartGhost", D_METHOD("samples_for_lap", "lap_time_s"),
			&KartGhost::samples_for_lap);
	ClassDB::bind_static_method("KartGhost", D_METHOD("bytes_for_lap", "lap_time_s"),
			&KartGhost::bytes_for_lap);

	BIND_CONSTANT(FORMAT_VERSION);
	BIND_CONSTANT(SAMPLE_HZ);
	BIND_CONSTANT(TICKS_PER_SAMPLE);
	BIND_CONSTANT(SAMPLE_BYTES);
	BIND_CONSTANT(MAX_SECTORS);
	BIND_CONSTANT(VERDICT_COMPARABLE);
	BIND_CONSTANT(VERDICT_WARNED);
	BIND_CONSTANT(VERDICT_INCOMPARABLE);
}

// --- what makes a lap comparable ------------------------------------------------

bool KartGhost::set_track(const String &p_slug) {
	const CharString utf8 = p_slug.utf8();
	if (!header_.set_track_id(utf8.get_data())) {
		ERR_PRINT(vformat("KartGhost: track slug '%s' does not fit in %d characters", p_slug,
				SESSION_ID_CHARS - 1));
		return false;
	}
	return true;
}

String KartGhost::get_track() const {
	return from_utf8(header_.track_id);
}

bool KartGhost::set_track_hash_hex(const String &p_hex) {
	uint64_t value = 0;
	if (!parse_hex64(p_hex, value)) {
		ERR_PRINT(vformat("KartGhost: '%s' is not a 64-bit hex hash", p_hex));
		return false;
	}
	header_.track_hash = value;
	return true;
}

String KartGhost::get_track_hash_hex() const {
	return hex64(header_.track_hash);
}

void KartGhost::set_layout(int p_layout) {
	if (p_layout < 0 || p_layout >= TRACK_LAYOUT_COUNT) {
		ERR_PRINT(vformat("KartGhost: layout %d is not one of the %d layouts", p_layout,
				TRACK_LAYOUT_COUNT));
		return;
	}
	header_.layout = static_cast<TrackLayout>(p_layout);
}

int KartGhost::get_layout() const {
	return static_cast<int>(header_.layout);
}

void KartGhost::set_kart_class(int p_kart_class) {
	if (p_kart_class < 0 || p_kart_class >= KART_CLASS_COUNT) {
		ERR_PRINT(vformat("KartGhost: class %d is not one of the %d classes", p_kart_class,
				KART_CLASS_COUNT));
		return;
	}
	header_.kart_class = static_cast<KartClass>(p_kart_class);
}

int KartGhost::get_kart_class() const {
	return static_cast<int>(header_.kart_class);
}

bool KartGhost::set_tuning_hash_hex(const String &p_hex) {
	uint64_t value = 0;
	if (!parse_hex64(p_hex, value)) {
		ERR_PRINT(vformat("KartGhost: '%s' is not a 64-bit hex hash", p_hex));
		return false;
	}
	header_.tuning_hash = value;
	return true;
}

String KartGhost::get_tuning_hash_hex() const {
	return hex64(header_.tuning_hash);
}

bool KartGhost::adopt_session(Object *p_session) {
	KartSession *session = Object::cast_to<KartSession>(p_session);
	if (session == nullptr) {
		ERR_PRINT("KartGhost: adopt_session wants a KartSession");
		return false;
	}
	const SessionConfig &config = session->config();
	// Copied field by field rather than by assigning a struct: the five fields a
	// ghost has are the five that decide whether two lap times mean the same thing,
	// and a memberwise copy of a wider type is how a `config_hash` ends up in a
	// format that `ghost.h` is explicit must not carry one.
	if (!header_.set_track_id(config.track_id)) {
		ERR_PRINT("KartGhost: the session's track id does not fit a ghost header");
		return false;
	}
	header_.track_hash = config.track_hash;
	header_.layout = config.layout;
	header_.kart_class = config.kart_class;
	header_.tuning_hash = config.tuning.hash();
	return true;
}

// --- recording ------------------------------------------------------------------

void KartGhost::begin_record() {
	samples_.clear();
	samples_.reserve(static_cast<size_t>(RESERVE_SAMPLES));
	tick_ = 0;
	recording_ = true;
	closed_ = false;
	migrated_ = false;
	// The times go with the body. A recorder that kept the previous lap's lap time
	// would produce a header describing one lap and a body describing another, which
	// is exactly the disagreement `load` refuses -- from the other side, where
	// nothing would catch it.
	header_.sample_count = 0;
	header_.lap_time_s = 0.0;
	header_.sector_count = 0;
	for (int i = 0; i < GHOST_MAX_SECTORS; ++i) {
		header_.sector_split_s[i] = 0.0;
	}
	header_.sample_hz = GHOST_SAMPLE_HZ;
	header_.format_version = GHOST_FORMAT_VERSION;
}

bool KartGhost::is_recording() const {
	return recording_;
}

bool KartGhost::record_tick(const Transform3D &p_transform) {
	if (!recording_) {
		return false;
	}
	const bool on_a_sample = (tick_ % GHOST_TICKS_PER_SAMPLE) == 0;
	++tick_;
	if (!on_a_sample) {
		return false;
	}
	if (static_cast<int>(samples_.size()) >= MAX_SAMPLES) {
		// Stop sampling rather than stop recording: the lap is still being driven and
		// the timer still has to close it. What is lost is the tail of a lap longer
		// than twenty minutes, and saying so once is the whole of the handling.
		ERR_PRINT_ONCE(vformat("KartGhost: a recording hit %d samples and stopped sampling",
				MAX_SAMPLES));
		return false;
	}
	// Snapped here, not at `save`. See the class note: a recorder that held full
	// precision would play back one line before the file was written and a different
	// one after.
	samples_.push_back(ghost_snap(sample_from_transform(p_transform)));
	return true;
}

int KartGhost::recorded_ticks() const {
	return static_cast<int>(tick_);
}

bool KartGhost::finish_record(double p_lap_time_s,
		const PackedFloat64Array &p_sector_durations_s) {
	if (samples_.empty()) {
		ERR_PRINT("KartGhost: finish_record with no samples; there is nothing to draw");
		return false;
	}
	if (!(p_lap_time_s > 0.0) || !std::isfinite(p_lap_time_s)) {
		ERR_PRINT(vformat("KartGhost: %f is not a lap time", p_lap_time_s));
		return false;
	}

	header_.sample_hz = GHOST_SAMPLE_HZ;
	// What the recorder counted. See the class note on why this is not derived from
	// the lap time.
	header_.sample_count = static_cast<int>(samples_.size());
	header_.lap_time_s = p_lap_time_s;

	const int given = p_sector_durations_s.size();
	if (given > GHOST_MAX_SECTORS) {
		ERR_PRINT(vformat("KartGhost: %d sectors is more than the %d a ghost carries; the "
						  "splits are dropped and the lap is kept",
				given, GHOST_MAX_SECTORS));
		header_.sector_count = 0;
	} else {
		// Durations in, cumulative out. `ghost.h` is explicit that the file carries
		// cumulative splits and that accumulating in the other direction walks a
		// rounding error down the lap.
		double running = 0.0;
		bool sane = true;
		for (int i = 0; i < given; ++i) {
			const double duration = p_sector_durations_s[i];
			if (!(duration > 0.0) || !std::isfinite(duration)) {
				sane = false;
				break;
			}
			running += duration;
			header_.sector_split_s[i] = running;
		}
		if (sane && given > 0) {
			// The last cumulative split **is** the lap time, by definition, so it is
			// assigned rather than accumulated: summing three products of a tick count
			// and 1/120 does not have to land on the same double as one product of the
			// total, and `GhostHeader::is_valid` compares them on a 1e-6 grid.
			//
			// A disagreement bigger than a millisecond is not float noise -- it means
			// the marks and the lap do not describe the same lap, which happens when a
			// lap is armed from part-way round. The line is still worth keeping, so the
			// splits are dropped and the ghost is not.
			if (std::fabs(running - p_lap_time_s) > 1e-3) {
				ERR_PRINT(vformat("KartGhost: the sectors sum to %f against a %f lap; the "
								  "splits are dropped and the lap is kept",
						running, p_lap_time_s));
				sane = false;
			} else {
				header_.sector_split_s[given - 1] = p_lap_time_s;
			}
		}
		header_.sector_count = sane ? given : 0;
	}
	if (header_.sector_count == 0) {
		for (int i = 0; i < GHOST_MAX_SECTORS; ++i) {
			header_.sector_split_s[i] = 0.0;
		}
	}

	recording_ = false;
	closed_ = header_.is_valid();
	if (!closed_) {
		ERR_PRINT("KartGhost: the header this lap produced is not valid; nothing will be saved");
	}
	return closed_;
}

Dictionary KartGhost::adopt_lap(Object *p_lap_timer) {
	Dictionary report;
	report["ok"] = false;
	report["reason"] = String();

	KartLapTimer *timer = Object::cast_to<KartLapTimer>(p_lap_timer);
	if (timer == nullptr) {
		ERR_PRINT("KartGhost: adopt_lap wants a KartLapTimer");
		report["reason"] = String("not a KartLapTimer");
		return report;
	}
	const LapRecord &lap = timer->timer().last();
	report["lap_time"] = lap.time_s;
	report["reason"] = from_utf8(lap_invalid_reason_name(lap.reason));
	if (!lap.is_valid()) {
		// Refused here rather than at `save`, and the caller is told which lap it was.
		// A ghost recorded on an invalid lap is not saved -- `GAMEDESIGN.md` §3 puts
		// lap validation next to the ghost in Practice for this reason.
		discard();
		return report;
	}
	PackedFloat64Array durations;
	for (int i = 0; i < lap.sector_count; ++i) {
		durations.push_back(lap.sector_s[i]);
	}
	report["ok"] = finish_record(lap.time_s, durations);
	report["samples"] = static_cast<int>(samples_.size());
	return report;
}

void KartGhost::discard() {
	samples_.clear();
	tick_ = 0;
	recording_ = false;
	closed_ = false;
	migrated_ = false;
	header_.sample_count = 0;
	header_.lap_time_s = 0.0;
	header_.sector_count = 0;
	for (int i = 0; i < GHOST_MAX_SECTORS; ++i) {
		header_.sector_split_s[i] = 0.0;
	}
}

// --- reading it back ------------------------------------------------------------

bool KartGhost::is_complete() const {
	return closed_ && header_.is_valid() &&
			header_.sample_count == static_cast<int>(samples_.size()) && !samples_.empty();
}

int KartGhost::sample_count() const {
	return static_cast<int>(samples_.size());
}

int KartGhost::sample_hz() const {
	return header_.sample_hz;
}

int KartGhost::format_version() const {
	return header_.format_version;
}

double KartGhost::lap_time() const {
	return header_.lap_time_s;
}

double KartGhost::playback_duration() const {
	if (samples_.size() < 2 || header_.sample_hz < 1) {
		return 0.0;
	}
	return static_cast<double>(samples_.size() - 1) / static_cast<double>(header_.sample_hz);
}

int KartGhost::sector_count() const {
	return header_.sector_count;
}

PackedFloat64Array KartGhost::sector_splits() const {
	PackedFloat64Array out;
	for (int i = 0; i < header_.sector_count; ++i) {
		out.push_back(header_.sector_split_s[i]);
	}
	return out;
}

PackedFloat64Array KartGhost::sector_durations() const {
	PackedFloat64Array out;
	double previous = 0.0;
	for (int i = 0; i < header_.sector_count; ++i) {
		out.push_back(header_.sector_split_s[i] - previous);
		previous = header_.sector_split_s[i];
	}
	return out;
}

Dictionary KartGhost::header() const {
	Dictionary out;
	out["format_version"] = header_.format_version;
	out["track"] = get_track();
	out["track_hash"] = get_track_hash_hex();
	out["layout"] = static_cast<int>(header_.layout);
	out["layout_name"] = from_utf8(track_layout_name(header_.layout));
	out["kart_class"] = static_cast<int>(header_.kart_class);
	out["kart_class_name"] = from_utf8(kart::core::kart_class_name(header_.kart_class));
	out["sample_hz"] = header_.sample_hz;
	out["sample_count"] = header_.sample_count;
	out["lap_time"] = header_.lap_time_s;
	out["playback_duration"] = playback_duration();
	out["sector_count"] = header_.sector_count;
	out["sector_splits"] = sector_splits();
	out["tuning_hash"] = get_tuning_hash_hex();
	out["complete"] = is_complete();
	out["migrated"] = migrated_;
	out["id"] = id();
	out["bytes"] = byte_size();
	return out;
}

int64_t KartGhost::byte_size() const {
	char text[GHOST_HEADER_CHARS];
	const int written = ghost_format_header(header_, text, static_cast<int>(sizeof(text)));
	if (written < 0) {
		return -1;
	}
	return static_cast<int64_t>(written) + GHOST_BODY_MARKER_LEN +
			static_cast<int64_t>(samples_.size()) * GHOST_SAMPLE_BYTES;
}

// --- playback -------------------------------------------------------------------

Transform3D KartGhost::transform_at_time(double p_time_s) const {
	if (samples_.empty()) {
		return Transform3D();
	}
	return transform_from_sample(ghost_sample_at(samples_.data(),
			static_cast<int>(samples_.size()), p_time_s, header_.sample_hz));
}

Transform3D KartGhost::transform_at_index(int p_index) const {
	if (p_index < 0 || p_index >= static_cast<int>(samples_.size())) {
		return Transform3D();
	}
	return transform_from_sample(samples_[static_cast<size_t>(p_index)]);
}

Dictionary KartGhost::sample_at_index(int p_index) const {
	if (p_index < 0 || p_index >= static_cast<int>(samples_.size())) {
		return Dictionary();
	}
	return sample_to_dictionary(samples_[static_cast<size_t>(p_index)]);
}

Dictionary KartGhost::sample_at_time(double p_time_s) const {
	if (samples_.empty()) {
		return Dictionary();
	}
	return sample_to_dictionary(ghost_sample_at(samples_.data(),
			static_cast<int>(samples_.size()), p_time_s, header_.sample_hz));
}

// --- comparability --------------------------------------------------------------

Dictionary KartGhost::compare_to_session(Object *p_session) const {
	Dictionary out;
	out["verdict"] = VERDICT_INCOMPARABLE;
	out["verdict_name"] = from_utf8(ghost_verdict_name(GhostVerdict::Incomparable));
	out["problem"] = static_cast<int>(GhostProblem::None);
	out["problem_name"] = from_utf8(ghost_problem_name(GhostProblem::None));
	out["drawable"] = false;
	out["track_changed"] = false;
	out["tuning_differs"] = false;
	out["migrated"] = migrated_;

	KartSession *session = Object::cast_to<KartSession>(p_session);
	if (session == nullptr) {
		ERR_PRINT("KartGhost: compare_to_session wants a KartSession");
		return out;
	}

	// The live side of `ghost_admit` is a `GhostHeader` carrying only the fields a
	// ghost has, filled from the session about to be driven. Its times are this
	// ghost's, because `ghost_admit` calls `is_valid()` on the recorded side only and
	// a live header with no lap time would otherwise have to carry a fake one.
	GhostHeader live;
	const SessionConfig &config = session->config();
	live.set_track_id(config.track_id);
	live.track_hash = config.track_hash;
	live.layout = config.layout;
	live.kart_class = config.kart_class;
	live.tuning_hash = config.tuning.hash();

	const GhostReport report = ghost_admit(header_, live);
	out["verdict"] = static_cast<int>(report.verdict);
	out["verdict_name"] = from_utf8(ghost_verdict_name(report.verdict));
	out["problem"] = static_cast<int>(report.problem);
	out["problem_name"] = from_utf8(ghost_problem_name(report.problem));
	out["drawable"] = report.drawable();
	out["track_changed"] = report.track_changed;
	out["tuning_differs"] = report.tuning_differs;
	out["migrated"] = report.migrated || migrated_;
	return out;
}

// --- ids, and the files they name ------------------------------------------------

String KartGhost::mint_id(const String &p_track, int p_layout, int p_kart_class) {
	const CharString utf8 = p_track.utf8();
	const char *track = utf8.get_data();
	if (track == nullptr || track[0] == '\0') {
		return String();
	}
	if (p_layout < 0 || p_layout >= TRACK_LAYOUT_COUNT || p_kart_class < 0 ||
			p_kart_class >= KART_CLASS_COUNT) {
		return String();
	}

	// FNV-1a over the **untruncated** slot, plus a scheme tag. `state_hash.h` is the
	// only hash in this project and there is no reason for a second one.
	StateHash digest;
	digest.add_uint64(ID_SCHEME_TAG);
	for (int i = 0; track[i] != '\0'; ++i) {
		digest.add_int(static_cast<int64_t>(static_cast<unsigned char>(track[i])));
	}
	digest.add_int(p_layout);
	digest.add_int(p_kart_class);
	const uint64_t hash = digest.digest();

	char out[PROFILE_SLUG_CHARS];
	int written = 0;
	for (int i = 0; i < ID_TRACK_CHARS && track[i] != '\0'; ++i) {
		out[written++] = slug_char(track[i]);
	}
	// A leading `-` would make the id readable as a command-line flag, which
	// `profile.h` refuses for exactly that reason.
	if (out[0] == '-') {
		out[0] = '_';
	}
	out[written++] = '-';
	const char *layout = track_layout_name(static_cast<TrackLayout>(p_layout));
	for (int i = 0; i < ID_LAYOUT_CHARS && layout[i] != '\0'; ++i) {
		out[written++] = slug_char(layout[i]);
	}
	out[written++] = '-';
	const char *cls = kart::core::kart_class_name(static_cast<KartClass>(p_kart_class));
	for (int i = 0; i < ID_CLASS_CHARS && cls[i] != '\0'; ++i) {
		out[written++] = slug_char(cls[i]);
	}
	out[written++] = '-';
	for (int i = ID_HASH_DIGITS - 1; i >= 0; --i) {
		out[written++] = hex_digit(static_cast<int>((hash >> (i * 4)) & 0xFULL));
	}
	out[written] = '\0';

	if (!profile_is_slug(out, written)) {
		// Unreachable by construction, and asserted rather than assumed: this string
		// becomes a filename.
		ERR_PRINT(vformat("KartGhost: minted '%s', which is not a slug", from_utf8(out)));
		return String();
	}
	return from_utf8(out);
}

String KartGhost::id() const {
	return mint_id(get_track(), static_cast<int>(header_.layout),
			static_cast<int>(header_.kart_class));
}

bool KartGhost::is_valid_id(const String &p_id) {
	const CharString utf8 = p_id.utf8();
	const char *text = utf8.get_data();
	if (text == nullptr) {
		return false;
	}
	return profile_is_slug(text, profile_length(text));
}

String KartGhost::ghost_directory() {
	return String("user://ghosts");
}

String KartGhost::path_for_id(const String &p_id) {
	if (!is_valid_id(p_id)) {
		ERR_PRINT(vformat("KartGhost: '%s' is not a ghost id", p_id));
		return String();
	}
	return ghost_directory() + String("/") + p_id + String(".ghost");
}

Error KartGhost::save(const String &p_path) const {
	if (!is_complete()) {
		// The single place "an invalid lap is not saved" is enforced. Every path into
		// a complete ghost -- `finish_record`, `adopt_lap`, `load` -- sets `closed_`
		// only on a valid header with a body that matches it.
		ERR_FAIL_V_MSG(ERR_INVALID_DATA,
				vformat("KartGhost: refusing to write %s; this is not a complete ghost", p_path));
	}
	if (p_path.is_empty()) {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "KartGhost: save wants a path");
	}

	char text[GHOST_HEADER_CHARS];
	const int written = ghost_format_header(header_, text, static_cast<int>(sizeof(text)));
	if (written < 0) {
		ERR_FAIL_V_MSG(ERR_INVALID_DATA, "KartGhost: the header does not fit its own buffer");
	}

	const String directory = p_path.get_base_dir();
	if (!directory.is_empty() && !DirAccess::dir_exists_absolute(directory)) {
		const Error made = DirAccess::make_dir_recursive_absolute(directory);
		if (made != OK) {
			ERR_FAIL_V_MSG(made, vformat("KartGhost: cannot create %s (error %d)", directory, made));
		}
	}

	// Atomic, per ADR-0042: write a temporary, then rename over the target. A save
	// interrupted by a crash leaves the previous best lap intact rather than a
	// half-written ghost that `load` would then refuse -- which for user data is the
	// same as losing it.
	const String temporary = p_path + String(".tmp");
	{
		Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
		if (file.is_null()) {
			const Error why = FileAccess::get_open_error();
			ERR_FAIL_V_MSG(why, vformat("KartGhost: cannot write %s (error %d)", temporary, why));
		}
		PackedByteArray bytes;
		bytes.resize(static_cast<int64_t>(samples_.size()) * GHOST_SAMPLE_BYTES);
		unsigned char *raw = reinterpret_cast<unsigned char *>(bytes.ptrw());
		for (size_t i = 0; i < samples_.size(); ++i) {
			if (ghost_encode_sample(samples_[i], raw + i * GHOST_SAMPLE_BYTES,
						GHOST_SAMPLE_BYTES) != GHOST_SAMPLE_BYTES) {
				ERR_FAIL_V_MSG(ERR_INVALID_DATA, "KartGhost: a sample refused to encode");
			}
		}
		// The header bytes as they came out of `ghost_format_header`, not through a
		// `godot::String`. It is already ASCII -- `ghost.h` writes nothing else -- and a
		// round trip through String and back would be one more place for the Latin-1
		// decode CLAUDE.md warns about to change a byte in a file that has to be
		// byte-identical for identical input.
		for (int i = 0; i < written; ++i) {
			file->store_8(static_cast<uint8_t>(text[i]));
		}
		for (int i = 0; i < GHOST_BODY_MARKER_LEN; ++i) {
			file->store_8(static_cast<uint8_t>(GHOST_BODY_MARKER[i]));
		}
		file->store_buffer(bytes);
		file->flush();
		file->close();
	}

	if (FileAccess::file_exists(p_path)) {
		// `rename_absolute` will not replace an existing file on every platform, so the
		// target is removed first. The window this opens is the reason the temporary
		// exists at all: the data is already on disk and complete before anything
		// touches the old file.
		const Error removed = DirAccess::remove_absolute(p_path);
		if (removed != OK) {
			ERR_FAIL_V_MSG(removed,
					vformat("KartGhost: cannot replace %s (error %d)", p_path, removed));
		}
	}
	const Error renamed = DirAccess::rename_absolute(temporary, p_path);
	if (renamed != OK) {
		ERR_FAIL_V_MSG(renamed, vformat("KartGhost: cannot rename %s to %s (error %d)", temporary,
										p_path, renamed));
	}
	return OK;
}

Error KartGhost::save_as_id() const {
	const String path = path_for_id(id());
	if (path.is_empty()) {
		ERR_FAIL_V_MSG(ERR_INVALID_DATA,
				"KartGhost: this ghost has no id, so it has no file; set a track first");
	}
	return save(path);
}

Dictionary KartGhost::load(const String &p_path) {
	Dictionary out;
	out["ok"] = false;
	out["error"] = static_cast<int>(FAILED);
	out["detail"] = String();
	out["samples"] = 0;
	out["migrated"] = false;

	if (!FileAccess::file_exists(p_path)) {
		out["error"] = static_cast<int>(ERR_FILE_NOT_FOUND);
		out["detail"] = vformat("no ghost at %s", p_path);
		return out;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		const Error why = FileAccess::get_open_error();
		out["error"] = static_cast<int>(why);
		out["detail"] = vformat("cannot read %s (error %d)", p_path, why);
		return out;
	}
	const PackedByteArray bytes = file->get_buffer(static_cast<int64_t>(file->get_length()));
	file->close();

	// Find the marker. Scanned from the start and the first hit taken, because the
	// body may contain any bytes and the header may not contain this line.
	const int64_t size = bytes.size();
	const uint8_t *raw = bytes.ptr();
	int64_t split = -1;
	for (int64_t i = 0; i + GHOST_BODY_MARKER_LEN <= size; ++i) {
		bool match = true;
		for (int j = 0; j < GHOST_BODY_MARKER_LEN && match; ++j) {
			match = raw[i + j] == static_cast<uint8_t>(GHOST_BODY_MARKER[j]);
		}
		if (match) {
			split = i;
			break;
		}
	}
	if (split < 0) {
		out["error"] = static_cast<int>(ERR_FILE_CORRUPT);
		out["detail"] = vformat("%s has no body marker; this is not a ghost", p_path);
		return out;
	}

	GhostHeader parsed;
	const GhostParse result = ghost_parse_header(reinterpret_cast<const char *>(raw),
			static_cast<int>(split), parsed);
	if (!result.ok) {
		out["error"] = static_cast<int>(ERR_FILE_CORRUPT);
		out["detail"] = vformat("%s line %d: %s at '%s'", p_path, result.line,
				from_utf8(ghost_parse_problem_name(result.problem)), from_utf8(result.key));
		return out;
	}
	if (!ghost_can_migrate(parsed.format_version)) {
		// Refused rather than guessed at. ADR-0042 migrates an older save forward and
		// says nothing about reading one from the future, and `ghost.h` is explicit:
		// a ghost drawn from a stream this build does not understand is a kart driving
		// through the scenery.
		out["error"] = static_cast<int>(ERR_FILE_UNRECOGNIZED);
		out["detail"] = vformat("%s is format %d and this build reads up to %d", p_path,
				parsed.format_version, GHOST_FORMAT_VERSION);
		return out;
	}
	const bool was_older = parsed.format_version < GHOST_FORMAT_VERSION;
	if (!ghost_migrate(parsed)) {
		out["error"] = static_cast<int>(ERR_FILE_UNRECOGNIZED);
		out["detail"] = vformat("%s could not be migrated from format %d", p_path,
				parsed.format_version);
		return out;
	}
	if (parsed.sample_count > MAX_SAMPLES) {
		out["error"] = static_cast<int>(ERR_FILE_CORRUPT);
		out["detail"] = vformat("%s declares %d samples, past the %d ceiling", p_path,
				parsed.sample_count, MAX_SAMPLES);
		return out;
	}

	// The body has to be exactly what the header says it is. A file whose header and
	// body disagree is refused rather than truncated to whichever is shorter: a
	// header claiming a 48 s lap over 40 s of samples plays a ghost that stops in the
	// middle of the circuit, and one claiming fewer samples than it carries silently
	// drops the end of the lap.
	const int64_t body = size - (split + GHOST_BODY_MARKER_LEN);
	const int64_t expected = static_cast<int64_t>(parsed.sample_count) * GHOST_SAMPLE_BYTES;
	if (body != expected) {
		out["error"] = static_cast<int>(ERR_FILE_CORRUPT);
		out["detail"] = vformat("%s declares %d samples (%d bytes) and carries %d", p_path,
				parsed.sample_count, expected, body);
		return out;
	}

	std::vector<GhostSample> decoded;
	decoded.reserve(static_cast<size_t>(parsed.sample_count));
	const uint8_t *cursor = raw + split + GHOST_BODY_MARKER_LEN;
	for (int i = 0; i < parsed.sample_count; ++i) {
		GhostSample sample;
		if (!ghost_decode_sample(reinterpret_cast<const unsigned char *>(cursor) +
						static_cast<int64_t>(i) * GHOST_SAMPLE_BYTES,
					GHOST_SAMPLE_BYTES, sample)) {
			out["error"] = static_cast<int>(ERR_FILE_CORRUPT);
			out["detail"] = vformat("%s sample %d does not decode", p_path, i);
			return out;
		}
		decoded.push_back(sample);
	}

	samples_ = decoded;
	header_ = parsed;
	tick_ = 0;
	recording_ = false;
	migrated_ = was_older;
	closed_ = header_.is_valid();

	out["ok"] = closed_;
	out["error"] = static_cast<int>(closed_ ? OK : ERR_FILE_CORRUPT);
	out["samples"] = static_cast<int>(samples_.size());
	out["migrated"] = migrated_;
	if (!closed_) {
		out["detail"] = vformat("%s parsed and is not a valid ghost", p_path);
	}
	return out;
}

Dictionary KartGhost::load_id(const String &p_id) {
	const String path = path_for_id(p_id);
	if (path.is_empty()) {
		Dictionary out;
		out["ok"] = false;
		out["error"] = static_cast<int>(ERR_INVALID_PARAMETER);
		out["detail"] = vformat("'%s' is not a ghost id", p_id);
		out["samples"] = 0;
		out["migrated"] = false;
		return out;
	}
	return load(path);
}

PackedStringArray KartGhost::stored_ids() {
	PackedStringArray out;
	const String directory = ghost_directory();
	if (!DirAccess::dir_exists_absolute(directory)) {
		return out;
	}
	Ref<DirAccess> dir = DirAccess::open(directory);
	if (dir.is_null()) {
		return out;
	}
	for (const String &file : dir->get_files()) {
		if (file.get_extension() != String("ghost")) {
			// `.tmp` from an interrupted save lands here, and so does anything a person
			// dropped in the directory. Neither is a ghost this build wrote.
			continue;
		}
		const String candidate = file.get_basename();
		if (!is_valid_id(candidate)) {
			continue;
		}
		out.push_back(candidate);
	}
	out.sort();
	return out;
}

PackedStringArray KartGhost::orphan_ids(const PackedStringArray &p_kept) {
	PackedStringArray out;
	for (const String &stored : stored_ids()) {
		if (p_kept.has(stored)) {
			continue;
		}
		out.push_back(stored);
	}
	return out;
}

int KartGhost::samples_for_lap(double p_lap_time_s) {
	return ghost_samples_for_lap(p_lap_time_s);
}

int64_t KartGhost::bytes_for_lap(double p_lap_time_s) {
	return static_cast<int64_t>(ghost_bytes_for_lap(p_lap_time_s));
}

} // namespace kartgame
