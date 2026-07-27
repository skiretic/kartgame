#include "session/kart_session.h"

#include "core/race_rules.h"

#include "tuning/tuning_registry.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

// `godot::String` decodes a bare `const char *` as Latin-1, so everything crossing
// this boundary goes through `String::utf8`. CLAUDE.md: an em dash in a literal
// arrives as mojibake, and it showed up in the tuning audit before anyone looked.
String from_utf8(const char *text) {
	return String::utf8(text);
}

// Parse "0x…" or bare hex into a uint64. Strict: a partial parse is a hash nobody
// wrote, and a hash that is quietly wrong is worse than one that is refused.
bool parse_hex64(const String &text, uint64_t &out) {
	const CharString utf8 = text.strip_edges().utf8();
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
	out = value;
	return true;
}

String hex64(uint64_t value) {
	char buffer[32];
	if (format_hex64(value, buffer, static_cast<int>(sizeof(buffer))) < 0) {
		return String("0x0000000000000000");
	}
	return from_utf8(buffer);
}

// Every `SessionField` in hashing order. One list, here, because `session.h` owns
// the enum and this is the only place that has to enumerate it for a UI.
constexpr SessionField SESSION_FIELDS[] = {
	SessionField::TrackId, SessionField::TrackHash, SessionField::Layout,
	SessionField::Condition, SessionField::Type, SessionField::KartClass,
	SessionField::LimitKind, SessionField::LimitValue, SessionField::EntryCount,
	SessionField::RosterHash, SessionField::AutoClutch, SessionField::AutoShift,
	SessionField::Tuning, SessionField::Seed
};

constexpr int SESSION_FIELD_COUNT =
		static_cast<int>(sizeof(SESSION_FIELDS) / sizeof(SESSION_FIELDS[0]));

} // namespace

// --- KartSession --------------------------------------------------------------

void KartSession::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_track", "slug"), &KartSession::set_track);
	ClassDB::bind_method(D_METHOD("get_track"), &KartSession::get_track);
	ClassDB::bind_method(D_METHOD("set_track_hash_hex", "hex"), &KartSession::set_track_hash_hex);
	ClassDB::bind_method(D_METHOD("get_track_hash_hex"), &KartSession::get_track_hash_hex);
	ClassDB::bind_method(D_METHOD("set_layout", "layout"), &KartSession::set_layout);
	ClassDB::bind_method(D_METHOD("get_layout"), &KartSession::get_layout);
	ClassDB::bind_method(D_METHOD("set_condition", "condition"), &KartSession::set_condition);
	ClassDB::bind_method(D_METHOD("get_condition"), &KartSession::get_condition);

	ClassDB::bind_method(D_METHOD("set_type", "type"), &KartSession::set_type);
	ClassDB::bind_method(D_METHOD("get_type"), &KartSession::get_type);
	ClassDB::bind_method(D_METHOD("set_kart_class", "kart_class"), &KartSession::set_kart_class);
	ClassDB::bind_method(D_METHOD("get_kart_class"), &KartSession::get_kart_class);
	ClassDB::bind_method(D_METHOD("set_limit", "kind", "value"), &KartSession::set_limit);
	ClassDB::bind_method(D_METHOD("get_limit_kind"), &KartSession::get_limit_kind);
	ClassDB::bind_method(D_METHOD("get_limit_value"), &KartSession::get_limit_value);
	ClassDB::bind_method(D_METHOD("use_scheduled_limit"), &KartSession::use_scheduled_limit);

	ClassDB::bind_method(D_METHOD("set_entry_count", "count"), &KartSession::set_entry_count);
	ClassDB::bind_method(D_METHOD("get_entry_count"), &KartSession::get_entry_count);
	ClassDB::bind_method(D_METHOD("set_roster_hash_hex", "hex"), &KartSession::set_roster_hash_hex);
	ClassDB::bind_method(D_METHOD("get_roster_hash_hex"), &KartSession::get_roster_hash_hex);

	ClassDB::bind_method(D_METHOD("set_auto_clutch", "enabled"), &KartSession::set_auto_clutch);
	ClassDB::bind_method(D_METHOD("is_auto_clutch"), &KartSession::is_auto_clutch);
	ClassDB::bind_method(D_METHOD("set_auto_shift", "enabled"), &KartSession::set_auto_shift);
	ClassDB::bind_method(D_METHOD("is_auto_shift"), &KartSession::is_auto_shift);
	ClassDB::bind_method(D_METHOD("set_seed_hex", "hex"), &KartSession::set_seed_hex);
	ClassDB::bind_method(D_METHOD("get_seed_hex"), &KartSession::get_seed_hex);
	ClassDB::bind_method(D_METHOD("adopt_tuning", "tuning"), &KartSession::adopt_tuning);

	ClassDB::bind_method(D_METHOD("config_hash_hex"), &KartSession::config_hash_hex);
	ClassDB::bind_method(D_METHOD("is_valid"), &KartSession::is_valid);
	ClassDB::bind_method(D_METHOD("first_difference_with", "other"),
			&KartSession::first_difference_with);
	ClassDB::bind_method(D_METHOD("field_value", "field"), &KartSession::field_value);
	ClassDB::bind_method(D_METHOD("to_dictionary"), &KartSession::to_dictionary);
	ClassDB::bind_static_method("KartSession", D_METHOD("field_names"), &KartSession::field_names);
	ClassDB::bind_static_method("KartSession", D_METHOD("type_name", "type"),
			&KartSession::type_name);
	ClassDB::bind_static_method("KartSession", D_METHOD("kart_class_name", "kart_class"),
			&KartSession::kart_class_name);
	ClassDB::bind_static_method("KartSession", D_METHOD("layout_name", "layout"),
			&KartSession::layout_name);
	ClassDB::bind_static_method("KartSession", D_METHOD("limit_kind_name", "kind"),
			&KartSession::limit_kind_name);

	// The enums, as constants, so GDScript never writes a bare integer for a session
	// type. `set_type(KartSession.TYPE_HEAT)` fails loudly on a typo; `set_type(2)`
	// is a silently different session.
	BIND_CONSTANT(TYPE_PRACTICE);
	BIND_CONSTANT(TYPE_QUALIFYING);
	BIND_CONSTANT(TYPE_HEAT);
	BIND_CONSTANT(TYPE_SUPER_HEAT);
	BIND_CONSTANT(TYPE_FINAL);
	BIND_CONSTANT(CLASS_OK);
	BIND_CONSTANT(CLASS_KZ2);
	BIND_CONSTANT(LAYOUT_FORWARD);
	BIND_CONSTANT(LAYOUT_REVERSE);
	BIND_CONSTANT(CONDITION_DRY);
	BIND_CONSTANT(LIMIT_OPEN);
	BIND_CONSTANT(LIMIT_LAPS);
	BIND_CONSTANT(LIMIT_DISTANCE);
	BIND_CONSTANT(LIMIT_DURATION);
	BIND_CONSTANT(MAX_ENTRIES);
}

bool KartSession::set_track(const String &p_slug) {
	const CharString utf8 = p_slug.utf8();
	if (!config_.set_track_id(utf8.get_data())) {
		// Refused rather than truncated. A truncated slug fails to resolve at load,
		// which is a long way from the menu that typed it.
		ERR_PRINT(vformat("KartSession: track slug '%s' does not fit in %d characters",
				p_slug, SESSION_ID_CHARS - 1));
		return false;
	}
	return true;
}

String KartSession::get_track() const {
	return from_utf8(config_.track_id);
}

bool KartSession::set_track_hash_hex(const String &p_hex) {
	uint64_t value = 0;
	if (!parse_hex64(p_hex, value)) {
		ERR_PRINT(vformat("KartSession: '%s' is not a 64-bit hex hash", p_hex));
		return false;
	}
	config_.track_hash = value;
	return true;
}

String KartSession::get_track_hash_hex() const {
	return hex64(config_.track_hash);
}

bool KartSession::set_layout(int p_layout) {
	if (p_layout < 0 || p_layout >= TRACK_LAYOUT_COUNT) {
		ERR_PRINT(vformat("KartSession: layout %d is not one of the %d layouts", p_layout,
				TRACK_LAYOUT_COUNT));
		return false;
	}
	config_.layout = static_cast<TrackLayout>(p_layout);
	return true;
}

int KartSession::get_layout() const {
	return static_cast<int>(config_.layout);
}

bool KartSession::set_condition(int p_condition) {
	if (p_condition < 0 || p_condition >= TRACK_CONDITION_COUNT) {
		ERR_PRINT(vformat("KartSession: condition %d is not one of the %d conditions",
				p_condition, TRACK_CONDITION_COUNT));
		return false;
	}
	config_.condition = static_cast<TrackCondition>(p_condition);
	return true;
}

int KartSession::get_condition() const {
	return static_cast<int>(config_.condition);
}

bool KartSession::set_type(int p_type) {
	if (p_type < 0 || p_type >= SESSION_TYPE_COUNT) {
		ERR_PRINT(vformat("KartSession: session type %d is not one of the %d types", p_type,
				SESSION_TYPE_COUNT));
		return false;
	}
	config_.type = static_cast<SessionType>(p_type);
	return true;
}

int KartSession::get_type() const {
	return static_cast<int>(config_.type);
}

bool KartSession::set_kart_class(int p_kart_class) {
	if (p_kart_class < 0 || p_kart_class >= KART_CLASS_COUNT) {
		ERR_PRINT(vformat("KartSession: class %d is not one of the %d classes", p_kart_class,
				KART_CLASS_COUNT));
		return false;
	}
	config_.kart_class = static_cast<KartClass>(p_kart_class);
	return true;
}

int KartSession::get_kart_class() const {
	return static_cast<int>(config_.kart_class);
}

bool KartSession::set_limit(int p_kind, double p_value) {
	if (p_kind < 0 || p_kind >= SESSION_LIMIT_KIND_COUNT) {
		ERR_PRINT(vformat("KartSession: limit kind %d is not one of the %d kinds", p_kind,
				SESSION_LIMIT_KIND_COUNT));
		return false;
	}
	config_.limit.kind = static_cast<SessionLimitKind>(p_kind);
	config_.limit.value = p_value;
	return true;
}

int KartSession::get_limit_kind() const {
	return static_cast<int>(config_.limit.kind);
}

double KartSession::get_limit_value() const {
	return config_.limit.value;
}

void KartSession::use_scheduled_limit() {
	config_.limit = scheduled_limit(config_.type);
}

bool KartSession::set_entry_count(int p_count) {
	if (p_count < 1 || p_count > SESSION_MAX_ENTRIES) {
		ERR_PRINT(vformat("KartSession: an entry list of %d is outside 1..%d", p_count,
				SESSION_MAX_ENTRIES));
		return false;
	}
	config_.entry_count = p_count;
	return true;
}

int KartSession::get_entry_count() const {
	return config_.entry_count;
}

bool KartSession::set_roster_hash_hex(const String &p_hex) {
	uint64_t value = 0;
	if (!parse_hex64(p_hex, value)) {
		ERR_PRINT(vformat("KartSession: '%s' is not a 64-bit hex hash", p_hex));
		return false;
	}
	config_.roster_hash = value;
	return true;
}

String KartSession::get_roster_hash_hex() const {
	return hex64(config_.roster_hash);
}

void KartSession::set_auto_clutch(bool p_enabled) {
	config_.assists.auto_clutch = p_enabled;
}

bool KartSession::is_auto_clutch() const {
	return config_.assists.auto_clutch;
}

void KartSession::set_auto_shift(bool p_enabled) {
	config_.assists.auto_shift = p_enabled;
}

bool KartSession::is_auto_shift() const {
	return config_.assists.auto_shift;
}

bool KartSession::set_seed_hex(const String &p_hex) {
	uint64_t value = 0;
	if (!parse_hex64(p_hex, value)) {
		ERR_PRINT(vformat("KartSession: '%s' is not a 64-bit hex seed", p_hex));
		return false;
	}
	config_.seed = value;
	return true;
}

String KartSession::get_seed_hex() const {
	return hex64(config_.seed);
}

bool KartSession::adopt_tuning(Object *p_tuning) {
	KartTuning *tuning = Object::cast_to<KartTuning>(p_tuning);
	if (tuning == nullptr) {
		ERR_PRINT("KartSession: adopt_tuning wants a KartTuning");
		return false;
	}
	config_.tuning = tuning->tuning_set();
	return true;
}

String KartSession::config_hash_hex() const {
	return hex64(config_.hash());
}

bool KartSession::is_valid() const {
	return config_.is_valid();
}

String KartSession::first_difference_with(const Ref<KartSession> &p_other) const {
	if (p_other.is_null()) {
		ERR_PRINT("KartSession: first_difference_with wants a KartSession");
		return String();
	}
	const SessionField field = first_difference(config_, p_other->config_);
	if (field == SessionField::None) {
		return String();
	}
	return from_utf8(session_field_name(field));
}

String KartSession::field_value(const String &p_field) const {
	for (int i = 0; i < SESSION_FIELD_COUNT; ++i) {
		const SessionField field = SESSION_FIELDS[i];
		if (p_field != from_utf8(session_field_name(field))) {
			continue;
		}
		char buffer[128];
		if (session_field_value(config_, field, buffer, static_cast<int>(sizeof(buffer))) < 0) {
			return String();
		}
		return from_utf8(buffer);
	}
	ERR_PRINT(vformat("KartSession: '%s' is not a session field", p_field));
	return String();
}

Dictionary KartSession::to_dictionary() const {
	Dictionary out;
	for (int i = 0; i < SESSION_FIELD_COUNT; ++i) {
		const SessionField field = SESSION_FIELDS[i];
		char buffer[128];
		if (session_field_value(config_, field, buffer, static_cast<int>(sizeof(buffer))) < 0) {
			continue;
		}
		out[from_utf8(session_field_name(field))] = from_utf8(buffer);
	}
	return out;
}

PackedStringArray KartSession::field_names() {
	PackedStringArray out;
	for (int i = 0; i < SESSION_FIELD_COUNT; ++i) {
		out.push_back(from_utf8(session_field_name(SESSION_FIELDS[i])));
	}
	return out;
}

String KartSession::type_name(int p_type) {
	return from_utf8(session_type_name(static_cast<SessionType>(p_type)));
}

String KartSession::kart_class_name(int p_kart_class) {
	return from_utf8(kart::core::kart_class_name(static_cast<KartClass>(p_kart_class)));
}

String KartSession::layout_name(int p_layout) {
	return from_utf8(track_layout_name(static_cast<TrackLayout>(p_layout)));
}

String KartSession::limit_kind_name(int p_kind) {
	return from_utf8(session_limit_kind_name(static_cast<SessionLimitKind>(p_kind)));
}

// --- KartLapTimer -------------------------------------------------------------

void KartLapTimer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin_even", "length_m", "sectors", "step_s"),
			&KartLapTimer::begin_even);
	ClassDB::bind_method(D_METHOD("begin_marks", "marks_m", "length_m", "step_s"),
			&KartLapTimer::begin_marks);
	ClassDB::bind_method(D_METHOD("reset"), &KartLapTimer::reset);
	ClassDB::bind_method(D_METHOD("advance", "distance_m", "off_track"), &KartLapTimer::advance);
	ClassDB::bind_method(D_METHOD("respawn", "distance_m"), &KartLapTimer::respawn);
	ClassDB::bind_method(D_METHOD("invalidate_fastest"), &KartLapTimer::invalidate_fastest);

	ClassDB::bind_method(D_METHOD("lap_time"), &KartLapTimer::lap_time);
	ClassDB::bind_method(D_METHOD("sector"), &KartLapTimer::sector);
	ClassDB::bind_method(D_METHOD("sector_time"), &KartLapTimer::sector_time);
	ClassDB::bind_method(D_METHOD("laps_completed"), &KartLapTimer::laps_completed);
	ClassDB::bind_method(D_METHOD("distance"), &KartLapTimer::distance);
	ClassDB::bind_method(D_METHOD("current_reason"), &KartLapTimer::current_reason);

	ClassDB::bind_method(D_METHOD("has_last"), &KartLapTimer::has_last);
	ClassDB::bind_method(D_METHOD("last_time"), &KartLapTimer::last_time);
	ClassDB::bind_method(D_METHOD("last_sectors"), &KartLapTimer::last_sectors);
	ClassDB::bind_method(D_METHOD("last_reason"), &KartLapTimer::last_reason);
	ClassDB::bind_method(D_METHOD("last_was_valid"), &KartLapTimer::last_was_valid);

	ClassDB::bind_method(D_METHOD("has_best"), &KartLapTimer::has_best);
	ClassDB::bind_method(D_METHOD("best_time"), &KartLapTimer::best_time);
	ClassDB::bind_method(D_METHOD("best_sectors"), &KartLapTimer::best_sectors);
	ClassDB::bind_method(D_METHOD("best_sector", "index"), &KartLapTimer::best_sector);
	ClassDB::bind_method(D_METHOD("optimal_lap"), &KartLapTimer::optimal_lap);
	ClassDB::bind_method(D_METHOD("valid_lap_count"), &KartLapTimer::valid_lap_count);
	ClassDB::bind_method(D_METHOD("invalid_lap_count"), &KartLapTimer::invalid_lap_count);
	ClassDB::bind_method(D_METHOD("sector_delta_to_best"), &KartLapTimer::sector_delta_to_best);
	ClassDB::bind_method(D_METHOD("has_sector_delta_to_best"),
			&KartLapTimer::has_sector_delta_to_best);

	ClassDB::bind_static_method("KartLapTimer", D_METHOD("type_invalidates_laps", "type"),
			&KartLapTimer::type_invalidates_laps);
	ClassDB::bind_static_method("KartLapTimer", D_METHOD("type_deletes_fastest_lap", "type"),
			&KartLapTimer::type_deletes_fastest_lap);
	ClassDB::bind_static_method("KartLapTimer", D_METHOD("type_has_field", "type"),
			&KartLapTimer::type_has_field);
	ClassDB::bind_static_method("KartLapTimer",
			D_METHOD("type_awards_championship_points", "type"),
			&KartLapTimer::type_awards_championship_points);
	ClassDB::bind_static_method("KartLapTimer", D_METHOD("type_sets_next_grid", "type"),
			&KartLapTimer::type_sets_next_grid);
	ClassDB::bind_static_method("KartLapTimer",
			D_METHOD("laps_for_distance", "distance_m", "lap_length_m"),
			&KartLapTimer::laps_for_distance);
}

bool KartLapTimer::begin_even(double p_length_m, int p_sectors, double p_step_s) {
	const LapMarks marks = LapMarks::even(p_length_m, p_sectors);
	if (!marks.is_valid() || !(p_step_s > 0.0)) {
		ERR_PRINT(vformat("KartLapTimer: %f m in %d sectors at a %f s step is not timeable",
				p_length_m, p_sectors, p_step_s));
		return false;
	}
	marks_ = marks;
	timer_.begin(marks_, p_step_s);
	started_ = true;
	return true;
}

bool KartLapTimer::begin_marks(const PackedFloat64Array &p_marks, double p_length_m,
		double p_step_s) {
	LapMarks marks;
	marks.length_m = p_length_m;
	marks.mark_count = p_marks.size();
	if (marks.mark_count < 1 || marks.mark_count > LAP_MAX_MARKS) {
		ERR_PRINT(vformat("KartLapTimer: %d marks is outside 1..%d", marks.mark_count,
				LAP_MAX_MARKS));
		return false;
	}
	for (int i = 0; i < marks.mark_count; ++i) {
		marks.mark_m[i] = p_marks[i];
	}
	if (!marks.is_valid() || !(p_step_s > 0.0)) {
		// Named, because ADR-0046 makes validation part of the schema and "a track
		// that loads and cannot be raced is worse than one that refuses to load".
		ERR_PRINT("KartLapTimer: the marks must start at 0.0, ascend, and stay under "
				  "the lap length");
		return false;
	}
	marks_ = marks;
	timer_.begin(marks_, p_step_s);
	started_ = true;
	return true;
}

void KartLapTimer::reset() {
	timer_.reset();
}

bool KartLapTimer::advance(double p_distance_m, bool p_off_track) {
	if (!started_) {
		// Silent would mean a Practice session that never times a lap and never says
		// why, which is the class of failure #169 exists for.
		ERR_PRINT_ONCE("KartLapTimer: advance before begin_even or begin_marks; no lap "
					   "will ever be timed");
		return false;
	}
	return timer_.advance(p_distance_m, p_off_track);
}

void KartLapTimer::respawn(double p_distance_m) {
	timer_.respawn(p_distance_m);
}

double KartLapTimer::invalidate_fastest() {
	return timer_.invalidate_fastest();
}

double KartLapTimer::lap_time() const {
	return timer_.progress().lap_time_s;
}

int KartLapTimer::sector() const {
	return timer_.progress().sector;
}

double KartLapTimer::sector_time() const {
	return timer_.progress().sector_time_s;
}

int KartLapTimer::laps_completed() const {
	return timer_.laps_completed();
}

double KartLapTimer::distance() const {
	return timer_.progress().distance_m;
}

String KartLapTimer::current_reason() const {
	return from_utf8(lap_invalid_reason_name(timer_.progress().reason));
}

bool KartLapTimer::has_last() const {
	return lap_time_exists(timer_.last().time_s);
}

double KartLapTimer::last_time() const {
	return timer_.last().time_s;
}

PackedFloat64Array KartLapTimer::last_sectors() const {
	PackedFloat64Array out;
	const LapRecord &lap = timer_.last();
	for (int i = 0; i < lap.sector_count; ++i) {
		out.push_back(lap.sector_s[i]);
	}
	return out;
}

String KartLapTimer::last_reason() const {
	return from_utf8(lap_invalid_reason_name(timer_.last().reason));
}

bool KartLapTimer::last_was_valid() const {
	return timer_.last().is_valid();
}

bool KartLapTimer::has_best() const {
	return timer_.best().is_valid();
}

double KartLapTimer::best_time() const {
	return timer_.best().time_s;
}

PackedFloat64Array KartLapTimer::best_sectors() const {
	PackedFloat64Array out;
	const LapRecord &lap = timer_.best();
	for (int i = 0; i < lap.sector_count; ++i) {
		out.push_back(lap.sector_s[i]);
	}
	return out;
}

double KartLapTimer::best_sector(int p_index) const {
	return timer_.best_sector_s(p_index);
}

double KartLapTimer::optimal_lap() const {
	return timer_.optimal_lap_s();
}

int KartLapTimer::valid_lap_count() const {
	return timer_.valid_lap_count();
}

int KartLapTimer::invalid_lap_count() const {
	return timer_.invalid_lap_count();
}

double KartLapTimer::sector_delta_to_best() const {
	return timer_.sector_delta_s(timer_.best());
}

bool KartLapTimer::has_sector_delta_to_best() const {
	return timer_.has_sector_delta(timer_.best());
}

bool KartLapTimer::type_invalidates_laps(int p_type) {
	return session_invalidates_laps(static_cast<SessionType>(p_type));
}

bool KartLapTimer::type_deletes_fastest_lap(int p_type) {
	return session_deletes_fastest_lap(static_cast<SessionType>(p_type));
}

bool KartLapTimer::type_has_field(int p_type) {
	return session_has_field(static_cast<SessionType>(p_type));
}

bool KartLapTimer::type_awards_championship_points(int p_type) {
	return session_awards_championship_points(static_cast<SessionType>(p_type));
}

bool KartLapTimer::type_sets_next_grid(int p_type) {
	return session_sets_next_grid(static_cast<SessionType>(p_type));
}

int KartLapTimer::laps_for_distance(double p_distance_m, double p_lap_length_m) {
	return scheduled_laps(p_distance_m, p_lap_length_m);
}

} // namespace kartgame
