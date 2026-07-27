#include "session/kart_profile.h"

#include "session/fsync_shim.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

// `godot::String` decodes a bare `const char *` as Latin-1, so everything crossing
// this boundary goes through `String::utf8`. CLAUDE.md records the symptom: an em
// dash in a literal arrived in the tuning audit as `Ã¢` and nobody looked for a
// milestone. Every literal in this file is ASCII as well, belt and braces.
String from_utf8(const char *text) {
	return String::utf8(text);
}

// A `godot::String` as bytes, for handing to `profile.h`, which works in
// `const char *` throughout. Held in a `CharString` by the caller because the
// pointer dies with it — a two-line trap that produces a garbage slug rather than
// a crash.
const char *bytes_of(const CharString &utf8) {
	const char *data = utf8.get_data();
	return data == nullptr ? "" : data;
}

// The mapping this class's LOAD_* constants promise. Held as a static assert
// rather than as a comment, because the two enums are in different files and the
// promise is what lets a caller compare them.
static_assert(KartProfile::LOAD_OK == static_cast<int>(ProfileLoadStatus::Ok), "");
static_assert(KartProfile::LOAD_CORRUPT == static_cast<int>(ProfileLoadStatus::Corrupt), "");
static_assert(KartProfile::LOAD_FUTURE_VERSION ==
				static_cast<int>(ProfileLoadStatus::FutureVersion),
		"");
static_assert(KartProfile::LOAD_NO_FILE != KartProfile::LOAD_OK, "");
static_assert(KartProfile::LOAD_NO_FILE != KartProfile::LOAD_CORRUPT, "");
static_assert(KartProfile::LOAD_NO_FILE != KartProfile::LOAD_FUTURE_VERSION, "");
static_assert(KartProfile::LOAD_UNREADABLE != KartProfile::LOAD_NO_FILE, "");

// Strip a trailing slash and make sure the directory exists. Returns the
// normalized directory, or an empty String when it could not be made.
//
// The creation is here rather than at the write for the reason
// `KartTuning::save_preset` gives about `user://tuning/`: a save that failed
// because a directory did not exist is a lost session, and the directory is one
// call away.
String normalize_dir(const String &raw) {
	String dir = raw.strip_edges();
	if (dir.is_empty()) {
		return String();
	}
	while (dir.length() > 1 && dir.ends_with("/")) {
		dir = dir.substr(0, dir.length() - 1);
	}
	// `user://` is the one form where the trailing slash is part of the scheme.
	if (dir == "user:/" || dir == "user:") {
		dir = String("user://");
	}
	if (dir == "res:/" || dir == "res:") {
		return String();
	}
	if (dir != "user://" && !DirAccess::dir_exists_absolute(dir)) {
		if (DirAccess::make_dir_recursive_absolute(dir) != OK) {
			return String();
		}
	}
	return dir;
}

String path_in(const String &dir, const char *name) {
	if (dir.ends_with("/")) {
		return dir + from_utf8(name);
	}
	return dir + String("/") + from_utf8(name);
}

// Write `text` to `target` through `target + ".tmp"`, then rename over it.
//
// The whole of the atomicity is these lines, and the three things that make it
// work are that the target is never opened, that the bytes are synced to the
// medium before the rename (issue #173's shim — `fsync_shim.h` names the
// platform call per platform), and that the rename is the last name-changing
// operation. `kart_profile.h` states exactly what that does and does not
// guarantee; the short version is that a power cut leaves either the old
// complete file or the new complete one.
//
// **Not modeled on `KartTuning::save_preset`, on purpose.** That function opens
// its target directly, so a write interrupted halfway leaves a truncated preset
// where a good one was. It is the wrong shape for anything that holds a record,
// and copying it here would have been the same defect one file over.
Error atomic_store(const String &target, const String &temp, const PackedByteArray &bytes,
		PackedStringArray &warnings) {
	Ref<FileAccess> file = FileAccess::open(temp, FileAccess::WRITE);
	if (file.is_null()) {
		const Error why = FileAccess::get_open_error();
		warnings.push_back(vformat("cannot write %s (error %d)", temp, why));
		return why == OK ? ERR_FILE_CANT_WRITE : why;
	}
	const bool stored = file->store_buffer(bytes);
	// Flush before close rather than trusting close to do it. `close()` does flush,
	// but the ordering that matters is "the bytes are with the kernel before the
	// rename is issued" and an explicit flush is the only place in this sequence
	// where that is stated rather than inherited.
	file->flush();
	file->close();
	if (!stored) {
		warnings.push_back(vformat("the write to %s did not complete", temp));
		DirAccess::remove_absolute(temp);
		return ERR_FILE_CANT_WRITE;
	}

	// Read the length back off disk. `store_buffer` returning true says the calls
	// were accepted, not that the bytes landed, and a full disk is the case where
	// those differ. Cheap: a stat, not a read.
	{
		Ref<FileAccess> check = FileAccess::open(temp, FileAccess::READ);
		if (check.is_null()) {
			warnings.push_back(vformat("wrote %s and could not reopen it", temp));
			return ERR_FILE_CANT_READ;
		}
		const int64_t on_disk = static_cast<int64_t>(check->get_length());
		check->close();
		if (on_disk != bytes.size()) {
			warnings.push_back(vformat("%s holds %d bytes of %d -- out of space?", temp, on_disk,
					bytes.size()));
			DirAccess::remove_absolute(temp);
			return ERR_FILE_CANT_WRITE;
		}
	}

	// Issue #173: the bytes reach the medium before any name points at them. A
	// filesystem that cannot sync downgrades this save to the process-death
	// guarantee it had before the shim, with a warning; a sync that failed is a
	// failed save, because returning OK from here claims durability.
	const Error synced = fsync_file(temp);
	if (synced == ERR_UNAVAILABLE) {
		warnings.push_back(vformat(
				"%s cannot sync; this save is safe against a crash, not a power cut", temp));
	} else if (synced != OK) {
		warnings.push_back(vformat("the sync of %s failed (error %d)", temp, synced));
		DirAccess::remove_absolute(temp);
		return ERR_FILE_CANT_WRITE;
	}

	const Error renamed = DirAccess::rename_absolute(temp, target);
	if (renamed != OK) {
		// The target is untouched, which is the entire point. Take the temporary with
		// us so a retry does not find a stale one and so nothing is left for somebody
		// to mistake for a save.
		warnings.push_back(vformat("cannot rename %s over %s (error %d)", temp, target, renamed));
		DirAccess::remove_absolute(temp);
		return renamed;
	}

	// And the rename itself, so a save that reported OK exists after the cut.
	// Best effort: a lost rename reverts to the previous complete save, which is
	// a lost lap and not a corrupt career, so this one warns rather than fails.
	if (fsync_dir(target.get_base_dir()) != OK) {
		warnings.push_back(vformat(
				"the directory of %s did not sync; a power cut may revert to the previous save",
				target));
	}
	return OK;
}

// --- the settings text format --------------------------------------------------
//
// `src/core/settings.h` now, per issue #178. The model first shipped here with a
// comment saying it belonged in core and why it had not moved — the milestone's
// file split — and `tests/core/test_settings.cpp` is what the move bought. What
// stays on this side is `user://`, the temp-then-rename write, and turning each
// typed parse note into the warning sentence a boot log prints.

// One warning sentence per parse note, the same sentences this file printed when
// it owned the parser. The note is typed so a unit test asserts on the enum; the
// sentence is here so the wording can improve without touching core.
String settings_note_text(const kart::core::SettingsNote &note) {
	using kart::core::SettingsProblem;
	const String key = from_utf8(note.key);
	switch (note.problem) {
		case SettingsProblem::VersionNotAVersion:
			return vformat("line %d: settings_version is not a version", note.line);
		case SettingsProblem::KeyWithNoValue:
			return vformat("line %d: a key with no value, skipped", note.line);
		case SettingsProblem::NotABool:
			return vformat("line %d: %s is not true or false", note.line, key);
		case SettingsProblem::NoSuchCamera:
			return vformat("line %d: camera names no rig", note.line);
		case SettingsProblem::NotANumber:
			return vformat("line %d: %s is not a number", note.line, key);
		case SettingsProblem::UnknownKey:
			return vformat("line %d: unknown setting, skipped", note.line);
		case SettingsProblem::NoVersionLine:
			return from_utf8("no settings_version line; the file may not be a settings file");
		case SettingsProblem::None:
			break;
	}
	return String();
}

} // namespace

// --- KartProfile ---------------------------------------------------------------

void KartProfile::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_base_dir", "dir"), &KartProfile::set_base_dir);
	ClassDB::bind_method(D_METHOD("get_base_dir"), &KartProfile::get_base_dir);
	ClassDB::bind_method(D_METHOD("profile_path"), &KartProfile::profile_path);
	ClassDB::bind_method(D_METHOD("temp_path"), &KartProfile::temp_path);
	ClassDB::bind_method(D_METHOD("settings_path"), &KartProfile::settings_path);
	ClassDB::bind_method(D_METHOD("ghost_dir"), &KartProfile::ghost_dir);
	ClassDB::bind_method(D_METHOD("ghost_path", "ghost_id"), &KartProfile::ghost_path);
	ClassDB::bind_method(D_METHOD("corrupt_path", "index"), &KartProfile::corrupt_path);
	ClassDB::bind_method(D_METHOD("next_corrupt_index"), &KartProfile::next_corrupt_index);
	ClassDB::bind_method(D_METHOD("corrupt_paths"), &KartProfile::corrupt_paths);
	ClassDB::bind_static_method("KartProfile", D_METHOD("system_path", "path"),
			&KartProfile::system_path);

	ClassDB::bind_method(D_METHOD("set_driver_name", "name"), &KartProfile::set_driver_name);
	ClassDB::bind_method(D_METHOD("get_driver_name"), &KartProfile::get_driver_name);
	ClassDB::bind_method(D_METHOD("set_driver_number", "number"), &KartProfile::set_driver_number);
	ClassDB::bind_method(D_METHOD("get_driver_number"), &KartProfile::get_driver_number);
	ClassDB::bind_method(D_METHOD("set_nationality", "code"), &KartProfile::set_nationality);
	ClassDB::bind_method(D_METHOD("get_nationality"), &KartProfile::get_nationality);
	ClassDB::bind_method(D_METHOD("set_livery", "slug"), &KartProfile::set_livery);
	ClassDB::bind_method(D_METHOD("get_livery"), &KartProfile::get_livery);

	ClassDB::bind_method(D_METHOD("set_career_class", "kart_class"), &KartProfile::set_career_class);
	ClassDB::bind_method(D_METHOD("get_career_class"), &KartProfile::get_career_class);
	ClassDB::bind_method(D_METHOD("set_career_season", "season"), &KartProfile::set_career_season);
	ClassDB::bind_method(D_METHOD("get_career_season"), &KartProfile::get_career_season);
	ClassDB::bind_method(D_METHOD("set_career_round", "round"), &KartProfile::set_career_round);
	ClassDB::bind_method(D_METHOD("get_career_round"), &KartProfile::get_career_round);

	ClassDB::bind_method(D_METHOD("clear_standings"), &KartProfile::clear_standings);
	ClassDB::bind_method(D_METHOD("add_standing", "driver_id", "points"),
			&KartProfile::add_standing);
	ClassDB::bind_method(D_METHOD("standing_count"), &KartProfile::standing_count);
	ClassDB::bind_method(D_METHOD("standing_driver", "index"), &KartProfile::standing_driver);
	ClassDB::bind_method(D_METHOD("standing_points", "index"), &KartProfile::standing_points);
	ClassDB::bind_method(D_METHOD("has_standing", "driver_id"), &KartProfile::has_standing);
	ClassDB::bind_method(D_METHOD("points_for", "driver_id"), &KartProfile::points_for);
	ClassDB::bind_method(D_METHOD("standings_table"), &KartProfile::standings_table);

	ClassDB::bind_method(
			D_METHOD("set_best", "track_id", "layout", "kart_class", "lap_time_s", "ghost_id"),
			&KartProfile::set_best);
	ClassDB::bind_method(D_METHOD("has_best", "track_id", "layout", "kart_class"),
			&KartProfile::has_best);
	ClassDB::bind_method(D_METHOD("best_time", "track_id", "layout", "kart_class"),
			&KartProfile::best_time);
	ClassDB::bind_method(D_METHOD("best_ghost_id", "track_id", "layout", "kart_class"),
			&KartProfile::best_ghost_id);
	ClassDB::bind_method(D_METHOD("best_ghost_path", "track_id", "layout", "kart_class"),
			&KartProfile::best_ghost_path);
	ClassDB::bind_method(D_METHOD("best_count"), &KartProfile::best_count);
	ClassDB::bind_method(D_METHOD("best_at", "index"), &KartProfile::best_at);
	ClassDB::bind_method(D_METHOD("bests_table"), &KartProfile::bests_table);

	ClassDB::bind_method(D_METHOD("reset_to_fresh"), &KartProfile::reset_to_fresh);
	ClassDB::bind_method(D_METHOD("is_valid"), &KartProfile::is_valid);
	ClassDB::bind_method(D_METHOD("to_text"), &KartProfile::to_text);

	ClassDB::bind_method(D_METHOD("load"), &KartProfile::load);
	ClassDB::bind_method(D_METHOD("save"), &KartProfile::save);
	ClassDB::bind_method(D_METHOD("is_save_blocked"), &KartProfile::is_save_blocked);
	ClassDB::bind_method(D_METHOD("save_block_reason"), &KartProfile::save_block_reason);
	ClassDB::bind_method(D_METHOD("clear_save_block"), &KartProfile::clear_save_block);
	ClassDB::bind_static_method("KartProfile", D_METHOD("load_status_name", "status"),
			&KartProfile::load_status_name);

	BIND_CONSTANT(CLASS_OK);
	BIND_CONSTANT(CLASS_KZ2);
	BIND_CONSTANT(LAYOUT_FORWARD);
	BIND_CONSTANT(LAYOUT_REVERSE);
	BIND_CONSTANT(LOAD_OK);
	BIND_CONSTANT(LOAD_CORRUPT);
	BIND_CONSTANT(LOAD_FUTURE_VERSION);
	BIND_CONSTANT(LOAD_NO_FILE);
	BIND_CONSTANT(LOAD_UNREADABLE);
	BIND_CONSTANT(MAX_STANDINGS);
	BIND_CONSTANT(MAX_BESTS);
	BIND_CONSTANT(MIN_NUMBER);
	BIND_CONSTANT(MAX_NUMBER);
	BIND_CONSTANT(FORMAT_VERSION);
}

String KartProfile::join(const char *p_name) const {
	return path_in(base_dir_, p_name);
}

bool KartProfile::set_base_dir(const String &p_dir) {
	const String normalized = normalize_dir(p_dir);
	if (normalized.is_empty()) {
		return false;
	}
	base_dir_ = normalized;
	return true;
}

String KartProfile::get_base_dir() const {
	return base_dir_;
}

String KartProfile::profile_path() const {
	return join(PROFILE_FILE_NAME);
}

String KartProfile::temp_path() const {
	char buffer[64];
	if (profile_temp_name(PROFILE_FILE_NAME, buffer, static_cast<int>(sizeof(buffer))) < 0) {
		return String();
	}
	return path_in(base_dir_, buffer);
}

String KartProfile::settings_path() const {
	return join(PROFILE_SETTINGS_FILE_NAME);
}

String KartProfile::ghost_dir() const {
	return join("ghosts");
}

String KartProfile::ghost_path(const String &p_ghost_id) const {
	const CharString utf8 = p_ghost_id.utf8();
	const char *id = bytes_of(utf8);
	if (!profile_is_slug(id, profile_length(id))) {
		// Empty is the refusal. `kart_profile.h` says why this is a boundary and not
		// a style rule: the id comes out of a user-writable file and goes into a path.
		return String();
	}
	return ghost_dir() + String("/") + p_ghost_id + String(".ghost");
}

String KartProfile::corrupt_path(int p_index) const {
	char buffer[80];
	if (profile_corrupt_name(PROFILE_FILE_NAME, p_index, buffer,
				static_cast<int>(sizeof(buffer))) < 0) {
		return String();
	}
	return path_in(base_dir_, buffer);
}

int KartProfile::next_corrupt_index() const {
	// List once, run every name through the format's own recognizer, take the
	// highest. `profile.h` writes the naming and this reads it back, which is what
	// makes a second corruption land at `.2`: nothing here counts files, because a
	// count is wrong the moment somebody deletes `.1`.
	int highest = 0;
	if (!DirAccess::dir_exists_absolute(base_dir_)) {
		// Checked rather than attempted: `get_files_at` on an absent directory is not
		// an error worth handling, but it prints one, and an engine ERROR line during
		// an ordinary first run reads as a fault.
		return profile_next_corrupt_index(0);
	}
	const PackedStringArray files = DirAccess::get_files_at(base_dir_);
	for (int i = 0; i < files.size(); ++i) {
		const CharString utf8 = String(files[i]).utf8();
		const int index = profile_corrupt_index_of(PROFILE_FILE_NAME, bytes_of(utf8));
		if (index > highest) {
			highest = index;
		}
	}
	return profile_next_corrupt_index(highest);
}

PackedStringArray KartProfile::corrupt_paths() const {
	// Ascending by index rather than by directory order, because a recovery screen
	// listing `.10` before `.2` is a screen nobody can read. Selection sort over at
	// most a handful of names.
	PackedStringArray found;
	if (!DirAccess::dir_exists_absolute(base_dir_)) {
		return found;
	}
	const PackedStringArray files = DirAccess::get_files_at(base_dir_);
	int indices[64];
	int count = 0;
	for (int i = 0; i < files.size() && count < 64; ++i) {
		const CharString utf8 = String(files[i]).utf8();
		const int index = profile_corrupt_index_of(PROFILE_FILE_NAME, bytes_of(utf8));
		if (index >= 1) {
			indices[count++] = index;
		}
	}
	for (int i = 0; i < count; ++i) {
		int lowest = i;
		for (int j = i + 1; j < count; ++j) {
			if (indices[j] < indices[lowest]) {
				lowest = j;
			}
		}
		const int swap = indices[i];
		indices[i] = indices[lowest];
		indices[lowest] = swap;
		found.push_back(corrupt_path(indices[i]));
	}
	return found;
}

String KartProfile::system_path(const String &p_path) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return p_path;
	}
	return settings->globalize_path(p_path);
}

bool KartProfile::set_driver_name(const String &p_name) {
	const CharString utf8 = p_name.utf8();
	return profile_.set_driver_name(bytes_of(utf8));
}

String KartProfile::get_driver_name() const {
	return from_utf8(profile_.driver.name);
}

bool KartProfile::set_driver_number(int p_number) {
	if (p_number < PROFILE_MIN_NUMBER || p_number > PROFILE_MAX_NUMBER) {
		return false;
	}
	profile_.driver.number = p_number;
	return true;
}

int KartProfile::get_driver_number() const {
	return profile_.driver.number;
}

bool KartProfile::set_nationality(const String &p_code) {
	const CharString utf8 = p_code.utf8();
	return profile_.set_nationality(bytes_of(utf8));
}

String KartProfile::get_nationality() const {
	return from_utf8(profile_.driver.nationality);
}

bool KartProfile::set_livery(const String &p_slug) {
	const CharString utf8 = p_slug.utf8();
	return profile_.set_livery(bytes_of(utf8));
}

String KartProfile::get_livery() const {
	return from_utf8(profile_.driver.livery);
}

bool KartProfile::to_layout(int p_value, TrackLayout &out) {
	if (p_value < 0 || p_value >= TRACK_LAYOUT_COUNT) {
		return false;
	}
	out = static_cast<TrackLayout>(p_value);
	return true;
}

bool KartProfile::to_kart_class(int p_value, KartClass &out) {
	if (p_value < 0 || p_value >= KART_CLASS_COUNT) {
		return false;
	}
	out = static_cast<KartClass>(p_value);
	return true;
}

bool KartProfile::set_career_class(int p_kart_class) {
	KartClass resolved = KartClass::OK;
	if (!to_kart_class(p_kart_class, resolved)) {
		return false;
	}
	profile_.career.kart_class = resolved;
	return true;
}

int KartProfile::get_career_class() const {
	return static_cast<int>(profile_.career.kart_class);
}

bool KartProfile::set_career_season(int p_season) {
	if (p_season < 0 || p_season >= PROFILE_MAX_SEASONS) {
		return false;
	}
	profile_.career.season = p_season;
	return true;
}

int KartProfile::get_career_season() const {
	return profile_.career.season;
}

bool KartProfile::set_career_round(int p_round) {
	// Inclusive at the top, and `profile.h` says why: a season whose last round has
	// been run sits at the round count until it promotes or is re-run.
	if (p_round < 0 || p_round > PROFILE_MAX_ROUNDS) {
		return false;
	}
	profile_.career.round = p_round;
	return true;
}

int KartProfile::get_career_round() const {
	return profile_.career.round;
}

void KartProfile::clear_standings() {
	profile_.career.standings_count = 0;
	for (int i = 0; i < PROFILE_MAX_STANDINGS; ++i) {
		profile_.career.standings[i] = ProfileStandingRow();
	}
}

bool KartProfile::add_standing(const String &p_driver_id, double p_points) {
	const CharString utf8 = p_driver_id.utf8();
	return profile_.add_standing(bytes_of(utf8), p_points);
}

int KartProfile::standing_count() const {
	return profile_.career.standings_count;
}

String KartProfile::standing_driver(int p_index) const {
	if (p_index < 0 || p_index >= profile_.career.standings_count) {
		return String();
	}
	return from_utf8(profile_.career.standings[p_index].driver_id);
}

double KartProfile::standing_points(int p_index) const {
	if (p_index < 0 || p_index >= profile_.career.standings_count) {
		return 0.0;
	}
	return profile_.career.standings[p_index].points;
}

bool KartProfile::has_standing(const String &p_driver_id) const {
	const CharString utf8 = p_driver_id.utf8();
	return profile_.find_standing(bytes_of(utf8)) != nullptr;
}

double KartProfile::points_for(const String &p_driver_id) const {
	const CharString utf8 = p_driver_id.utf8();
	const ProfileStandingRow *row = profile_.find_standing(bytes_of(utf8));
	return row == nullptr ? 0.0 : row->points;
}

Array KartProfile::standings_table() const {
	Array table;
	for (int i = 0; i < profile_.career.standings_count; ++i) {
		Dictionary row;
		row["driver_id"] = from_utf8(profile_.career.standings[i].driver_id);
		row["points"] = profile_.career.standings[i].points;
		table.push_back(row);
	}
	return table;
}

bool KartProfile::set_best(const String &p_track_id, int p_layout, int p_kart_class,
		double p_lap_time_s, const String &p_ghost_id) {
	TrackLayout layout = TrackLayout::Forward;
	KartClass kart_class = KartClass::KZ2;
	if (!to_layout(p_layout, layout) || !to_kart_class(p_kart_class, kart_class)) {
		return false;
	}
	const CharString track = p_track_id.utf8();
	const CharString ghost = p_ghost_id.utf8();
	return profile_.set_best(bytes_of(track), layout, kart_class, p_lap_time_s, bytes_of(ghost));
}

const ProfileBest *KartProfile::lookup_best(const String &p_track_id, int p_layout,
		int p_kart_class) const {
	TrackLayout layout = TrackLayout::Forward;
	KartClass kart_class = KartClass::KZ2;
	if (!to_layout(p_layout, layout) || !to_kart_class(p_kart_class, kart_class)) {
		return nullptr;
	}
	const CharString track = p_track_id.utf8();
	return profile_.find_best(bytes_of(track), layout, kart_class);
}

bool KartProfile::has_best(const String &p_track_id, int p_layout, int p_kart_class) const {
	return lookup_best(p_track_id, p_layout, p_kart_class) != nullptr;
}

double KartProfile::best_time(const String &p_track_id, int p_layout, int p_kart_class) const {
	const ProfileBest *best = lookup_best(p_track_id, p_layout, p_kart_class);
	return best == nullptr ? -1.0 : best->lap_time_s;
}

String KartProfile::best_ghost_id(const String &p_track_id, int p_layout,
		int p_kart_class) const {
	const ProfileBest *best = lookup_best(p_track_id, p_layout, p_kart_class);
	return best == nullptr ? String() : from_utf8(best->ghost_id);
}

String KartProfile::best_ghost_path(const String &p_track_id, int p_layout,
		int p_kart_class) const {
	const String id = best_ghost_id(p_track_id, p_layout, p_kart_class);
	return id.is_empty() ? String() : ghost_path(id);
}

int KartProfile::best_count() const {
	return profile_.best_count;
}

Dictionary KartProfile::best_at(int p_index) const {
	Dictionary row;
	if (p_index < 0 || p_index >= profile_.best_count) {
		return row;
	}
	const ProfileBest &best = profile_.bests[p_index];
	const String ghost_id = from_utf8(best.ghost_id);
	row["track"] = from_utf8(best.track_id);
	row["layout"] = static_cast<int>(best.layout);
	row["kart_class"] = static_cast<int>(best.kart_class);
	row["lap_time_s"] = best.lap_time_s;
	row["ghost_id"] = ghost_id;
	row["ghost_path"] = ghost_path(ghost_id);
	return row;
}

Array KartProfile::bests_table() const {
	Array table;
	for (int i = 0; i < profile_.best_count; ++i) {
		table.push_back(best_at(i));
	}
	return table;
}

void KartProfile::reset_to_fresh() {
	profile_ = fresh_profile();
}

bool KartProfile::is_valid() const {
	return profile_.is_valid();
}

String KartProfile::to_text() const {
	char buffer[PROFILE_TEXT_CAP];
	const int written = format_profile(profile_, buffer, static_cast<int>(sizeof(buffer)));
	if (written < 0) {
		return String();
	}
	return String::utf8(buffer, written);
}

bool KartProfile::is_save_blocked() const {
	return !save_block_.is_empty();
}

String KartProfile::save_block_reason() const {
	return save_block_;
}

void KartProfile::clear_save_block() {
	save_block_ = String();
}

String KartProfile::load_status_name(int p_status) {
	switch (p_status) {
		case LOAD_NO_FILE: return from_utf8("no_file");
		case LOAD_UNREADABLE: return from_utf8("unreadable");
		default: break;
	}
	if (p_status < 0 || p_status > LOAD_FUTURE_VERSION) {
		return from_utf8("invalid");
	}
	return from_utf8(profile_load_status_name(static_cast<ProfileLoadStatus>(p_status)));
}

Dictionary KartProfile::load() {
	Dictionary result;
	PackedStringArray warnings;
	const String path = profile_path();

	result["path"] = path;
	result["moved_to"] = String();
	result["problem"] = static_cast<int>(ProfileProblem::None);
	result["problem_name"] = from_utf8(profile_problem_name(ProfileProblem::None));
	result["line"] = 0;
	result["declared_version"] = 0;
	result["migrations"] = 0;
	result["detail"] = String();
	result["bytes"] = 0;

	auto finish = [&](int status) {
		result["status"] = status;
		result["status_name"] = load_status_name(status);
		result["warnings"] = warnings;
		return result;
	};

	if (!FileAccess::file_exists(path)) {
		// A first run. Nothing to move aside, nothing wrong.
		profile_ = fresh_profile();
		return finish(LOAD_NO_FILE);
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		// The one case where the file must be left exactly where it is: its bytes
		// were never read, so calling it corrupt is a guess, and moving it aside on a
		// guess is how an intact save gets filed under `.corrupt.1`.
		//
		// **And the save is blocked, which is the same rule as the failed move.** A
		// rename over this file would succeed -- POSIX rename needs permission on the
		// directory, not on the file -- so without the block the next save destroys a
		// file that may well have been a perfectly good season, on the strength of a
		// permission problem that lasted one launch. This class does not overwrite
		// bytes it has never seen. `clear_save_block()` is the escape hatch for a
		// player who has been told and wants to start again anyway.
		const Error why = FileAccess::get_open_error();
		save_block_ = vformat("%s exists and could not be read (error %d), so its contents "
							  "are unknown",
				path, why);
		warnings.push_back(save_block_);
		return finish(LOAD_UNREADABLE);
	}
	const PackedByteArray raw = file->get_buffer(static_cast<int64_t>(file->get_length()));
	file->close();
	result["bytes"] = raw.size();

	// Through the bytes rather than through `get_as_text`. `get_as_text` normalizes
	// line endings and would make a CRLF file round-trip to LF, which is a save the
	// byte-identity assertion would then disagree with while everything looked fine.
	Profile parsed;
	ProfileLoadResult outcome;
	{
		// A NUL past the end, and it is belt and braces rather than a requirement:
		// `profile_lex` is bounded by its length argument and was read to confirm it
		// never touches `text[len]`. It is added anyway for one case that is not
		// belt and braces -- an **empty** file, where `PackedByteArray::ptr()` returns
		// null and the loader would be handed a null pointer for a file that exists.
		// It handles that (null lexes to `NoVersionLine`, which is `Corrupt`, which is
		// the right answer for a zero-byte save) but "the right answer via the null
		// path" is a coincidence, and a one-byte copy is cheaper than depending on it.
		PackedByteArray terminated = raw;
		terminated.push_back(0);
		const char *text = reinterpret_cast<const char *>(terminated.ptr());
		outcome = load_profile(text, raw.size(), parsed);
	}

	result["problem"] = static_cast<int>(outcome.problem);
	result["problem_name"] = from_utf8(profile_problem_name(outcome.problem));
	result["line"] = outcome.line;
	result["declared_version"] = outcome.declared_version;
	result["migrations"] = outcome.migrations_applied;
	result["detail"] = from_utf8(outcome.detail);

	if (outcome.status == ProfileLoadStatus::Ok) {
		profile_ = parsed;
		save_block_ = String();
		return finish(LOAD_OK);
	}

	if (outcome.status == ProfileLoadStatus::FutureVersion) {
		// Left completely alone: not loaded, not renamed, not overwritten. The
		// in-memory profile is untouched too, so a caller that ignores the status is
		// looking at whatever it had rather than at half of a newer file.
		//
		// The save is blocked for the same reason the corruption case blocks it: this
		// file holds a season, and writing an older format over it is the data loss the
		// separate status exists to prevent.
		save_block_ = vformat("%s was written by a newer build (version %d, this build reads %d)",
				path, outcome.declared_version, PROFILE_FORMAT_VERSION);
		warnings.push_back(save_block_);
		return finish(LOAD_FUTURE_VERSION);
	}

	// Corrupt. Move it aside before anything else happens to it.
	const int index = next_corrupt_index();
	const String moved = corrupt_path(index);
	if (moved.is_empty()) {
		save_block_ = vformat("%s did not parse and no free .corrupt name could be built", path);
		warnings.push_back(save_block_);
		profile_ = fresh_profile();
		return finish(LOAD_CORRUPT);
	}
	const Error renamed = DirAccess::rename_absolute(path, moved);
	profile_ = fresh_profile();
	if (renamed != OK) {
		// Saving now would overwrite the evidence an hour from now, with nobody
		// watching. So it is refused until a caller says otherwise.
		save_block_ = vformat("%s did not parse and could not be moved to %s (error %d)", path,
				moved, renamed);
		warnings.push_back(save_block_);
		return finish(LOAD_CORRUPT);
	}
	save_block_ = String();
	result["moved_to"] = moved;
	warnings.push_back(vformat("%s did not parse and was moved to %s", path, moved));
	return finish(LOAD_CORRUPT);
}

Dictionary KartProfile::save() {
	Dictionary result;
	PackedStringArray warnings;
	const String path = profile_path();
	const String temp = temp_path();
	result["path"] = path;
	result["temp_path"] = temp;
	result["bytes"] = 0;

	auto finish = [&](bool ok, Error error) {
		result["ok"] = ok;
		result["error"] = static_cast<int>(error);
		result["warnings"] = warnings;
		return result;
	};

	if (is_save_blocked()) {
		warnings.push_back(save_block_);
		warnings.push_back(from_utf8("the save was refused; clear_save_block() overrides it"));
		return finish(false, ERR_FILE_CANT_WRITE);
	}
	if (temp.is_empty()) {
		warnings.push_back(from_utf8("no temporary name could be built"));
		return finish(false, ERR_FILE_BAD_PATH);
	}

	// Serialize whole, then decide. `format_profile` returns -1 on an invalid profile
	// or a buffer that did not fit, so a truncated document never reaches a file and
	// a profile whose own loader would reject it is never written.
	char buffer[PROFILE_TEXT_CAP];
	const int written = format_profile(profile_, buffer, static_cast<int>(sizeof(buffer)));
	if (written < 0) {
		warnings.push_back(from_utf8("the profile is not valid, so nothing was written"));
		return finish(false, ERR_INVALID_DATA);
	}
	PackedByteArray bytes;
	bytes.resize(written);
	uint8_t *out = bytes.ptrw();
	for (int i = 0; i < written; ++i) {
		out[i] = static_cast<uint8_t>(buffer[i]);
	}

	const Error stored = atomic_store(path, temp, bytes, warnings);
	if (stored != OK) {
		return finish(false, stored);
	}
	result["bytes"] = written;
	return finish(true, OK);
}

// --- KartSettings --------------------------------------------------------------

void KartSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_base_dir", "dir"), &KartSettings::set_base_dir);
	ClassDB::bind_method(D_METHOD("get_base_dir"), &KartSettings::get_base_dir);
	ClassDB::bind_method(D_METHOD("settings_path"), &KartSettings::settings_path);
	ClassDB::bind_method(D_METHOD("temp_path"), &KartSettings::temp_path);

	ClassDB::bind_method(D_METHOD("set_auto_clutch", "enabled"), &KartSettings::set_auto_clutch);
	ClassDB::bind_method(D_METHOD("is_auto_clutch"), &KartSettings::is_auto_clutch);
	ClassDB::bind_method(D_METHOD("set_auto_shift", "enabled"), &KartSettings::set_auto_shift);
	ClassDB::bind_method(D_METHOD("is_auto_shift"), &KartSettings::is_auto_shift);
	ClassDB::bind_method(D_METHOD("set_camera", "camera"), &KartSettings::set_camera);
	ClassDB::bind_method(D_METHOD("get_camera"), &KartSettings::get_camera);
	ClassDB::bind_method(D_METHOD("get_camera_name"), &KartSettings::get_camera_name);
	ClassDB::bind_method(D_METHOD("set_master_volume_db", "db"),
			&KartSettings::set_master_volume_db);
	ClassDB::bind_method(D_METHOD("get_master_volume_db"), &KartSettings::get_master_volume_db);
	ClassDB::bind_method(D_METHOD("set_steer_deadzone", "deadzone"),
			&KartSettings::set_steer_deadzone);
	ClassDB::bind_method(D_METHOD("get_steer_deadzone"), &KartSettings::get_steer_deadzone);

	ClassDB::bind_method(D_METHOD("reset_to_defaults"), &KartSettings::reset_to_defaults);
	ClassDB::bind_method(D_METHOD("to_text"), &KartSettings::to_text);
	ClassDB::bind_method(D_METHOD("save"), &KartSettings::save);
	ClassDB::bind_method(D_METHOD("load"), &KartSettings::load);
	ClassDB::bind_static_method("KartSettings", D_METHOD("camera_name", "camera"),
			&KartSettings::camera_name);

	BIND_CONSTANT(CAMERA_CHASE);
	BIND_CONSTANT(CAMERA_COCKPIT);
	BIND_CONSTANT(CAMERA_FREE);
	BIND_CONSTANT(CAMERA_COUNT);
	BIND_CONSTANT(SETTINGS_VERSION);
}

String KartSettings::join(const char *p_name) const {
	return path_in(base_dir_, p_name);
}

bool KartSettings::set_base_dir(const String &p_dir) {
	const String normalized = normalize_dir(p_dir);
	if (normalized.is_empty()) {
		return false;
	}
	base_dir_ = normalized;
	return true;
}

String KartSettings::get_base_dir() const {
	return base_dir_;
}

String KartSettings::settings_path() const {
	return join(PROFILE_SETTINGS_FILE_NAME);
}

String KartSettings::temp_path() const {
	char buffer[64];
	if (profile_temp_name(PROFILE_SETTINGS_FILE_NAME, buffer,
				static_cast<int>(sizeof(buffer))) < 0) {
		return String();
	}
	return path_in(base_dir_, buffer);
}

void KartSettings::set_auto_clutch(bool p_enabled) {
	settings_.auto_clutch = p_enabled;
}

bool KartSettings::is_auto_clutch() const {
	return settings_.auto_clutch;
}

void KartSettings::set_auto_shift(bool p_enabled) {
	settings_.auto_shift = p_enabled;
}

bool KartSettings::is_auto_shift() const {
	return settings_.auto_shift;
}

bool KartSettings::set_camera(int p_camera) {
	return settings_.set_camera(p_camera);
}

int KartSettings::get_camera() const {
	return static_cast<int>(settings_.camera);
}

String KartSettings::get_camera_name() const {
	return camera_name(get_camera());
}

String KartSettings::camera_name(int p_camera) {
	return from_utf8(settings_camera_name(static_cast<SettingsCamera>(p_camera)));
}

void KartSettings::set_master_volume_db(double p_db) {
	settings_.set_master_volume_db(p_db);
}

double KartSettings::get_master_volume_db() const {
	return settings_.master_volume_db;
}

void KartSettings::set_steer_deadzone(double p_deadzone) {
	settings_.set_steer_deadzone(p_deadzone);
}

double KartSettings::get_steer_deadzone() const {
	return settings_.steer_deadzone;
}

void KartSettings::reset_to_defaults() {
	settings_ = Settings{};
}

String KartSettings::to_text() const {
	// `settings_format` owns the layout, and its docstring carries what this
	// comment used to: declaration order, one key per line, one number renderer.
	char text[SETTINGS_TEXT_CHARS];
	if (settings_format(settings_, text, static_cast<int>(sizeof(text))) < 0) {
		return String();
	}
	return from_utf8(text);
}

Dictionary KartSettings::save() {
	Dictionary result;
	PackedStringArray warnings;
	const String path = settings_path();
	const String temp = temp_path();
	result["path"] = path;
	result["temp_path"] = temp;
	result["bytes"] = 0;

	auto finish = [&](bool ok, Error error) {
		result["ok"] = ok;
		result["error"] = static_cast<int>(error);
		result["warnings"] = warnings;
		return result;
	};

	if (temp.is_empty()) {
		warnings.push_back(from_utf8("no temporary name could be built"));
		return finish(false, ERR_FILE_BAD_PATH);
	}
	const String text = to_text();
	if (text.is_empty()) {
		warnings.push_back(from_utf8("the settings could not be serialized"));
		return finish(false, ERR_INVALID_DATA);
	}
	// The whole document is ASCII by construction -- five keys, two words and three
	// numbers -- so `to_utf8_buffer` is exact here and the byte count is the
	// character count. Through the same temporary-then-rename path as the career,
	// because a settings file truncated by a crash is a menu that will not draw.
	const PackedByteArray bytes = text.to_utf8_buffer();
	const Error stored = atomic_store(path, temp, bytes, warnings);
	if (stored != OK) {
		return finish(false, stored);
	}
	result["bytes"] = bytes.size();
	return finish(true, OK);
}

Dictionary KartSettings::load() {
	Dictionary result;
	PackedStringArray warnings;
	const String path = settings_path();
	int applied = 0;
	int declared = 0;

	// Every load starts from the defaults, so a key the file does not mention lands
	// at its default rather than at whatever the last file said. Loading twice from
	// two different files must not leave a mixture of both.
	reset_to_defaults();

	auto finish = [&](bool ok, bool existed) {
		result["ok"] = ok;
		result["existed"] = existed;
		result["declared_version"] = declared;
		result["applied"] = applied;
		result["path"] = path;
		result["warnings"] = warnings;
		return result;
	};

	if (!FileAccess::file_exists(path)) {
		// A first run, and the defaults are the answer. Not a failure: `ok` is true
		// because a usable set of settings is in hand, which is the only thing a boot
		// script can act on.
		result["bytes"] = 0;
		return finish(true, false);
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		const Error why = FileAccess::get_open_error();
		warnings.push_back(vformat("cannot read %s (error %d); using defaults", path, why));
		result["bytes"] = 0;
		// Still `ok`. **This is the promise the separate file exists to keep**: a
		// player reaches a readable menu whatever happened to either file, and the
		// defaults are readable by construction.
		return finish(true, true);
	}
	// The length off the file rather than `text.length()`. `String::length()` counts
	// **characters**, so a byte count taken from it is wrong by however much
	// non-ASCII the file holds -- and reporting a character count under the key
	// `bytes` is the kind of quietly wrong number a later reader builds on.
	const int64_t on_disk = static_cast<int64_t>(file->get_length());
	const String text = file->get_as_text();
	file->close();
	result["bytes"] = on_disk;

	const CharString utf8 = text.utf8();

	// `settings_parse` owns the lexing and every decision about what the bytes
	// mean — including why it does not reuse `profile_lex`, which is the property
	// that keeps the two files from being loaded as each other. What this side
	// adds is the sentences: one warning per typed note, the same wording this
	// file printed when it owned the parser, so a boot log reads unchanged.
	SettingsParse report;
	settings_parse(bytes_of(utf8), static_cast<int>(utf8.length()), settings_, report);
	applied = report.applied;
	declared = report.declared_version;
	for (int i = 0; i < report.note_count; ++i) {
		const String sentence = settings_note_text(report.notes[i]);
		if (!sentence.is_empty()) {
			warnings.push_back(sentence);
		}
	}
	if (report.notes_dropped > 0) {
		// A file of pure garbage produces one note per line; core caps what it
		// keeps rather than allocating, and the count is worth a sentence because
		// "16 warnings" reading as the whole story would understate the mess.
		warnings.push_back(vformat("and %d more warnings like these were dropped",
				report.notes_dropped));
	}
	return finish(true, true);
}

} // namespace kartgame
