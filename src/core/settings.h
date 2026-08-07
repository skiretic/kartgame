#ifndef KART_CORE_SETTINGS_H
#define KART_CORE_SETTINGS_H

#include "core/profile.h"
#include "core/tuning.h"

// The settings file, as a value and a text format. ROADMAP M3c, issue #178.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// Allocation-free and locale-free for the reasons `tuning.h` gives.
//
// This model first shipped inside `src/session/kart_profile.cpp`, where
// `tests/run.sh` could not reach it, with a comment saying it belonged here and
// why it had not moved: `src/core/` was owned by another agent in that
// milestone's file split. ADR-0017 and ADR-0042 both name this home. The
// engine-side `KartSettings` still owns the two things that genuinely need an
// engine — `user://` and the temp-then-rename write — and delegates every
// decision about what the bytes mean to this header.
//
// ## The failure policy is the opposite of the career's, deliberately
//
// ADR-0042 rejected best-effort loading for the profile. Settings get
// best-effort loading anyway, and `kart_profile.h` carries the full argument:
// an unknown key is realistic (a *newer* build wrote it), a bad value falls
// back to its default, and the file's whole job is that a player reaches a
// readable menu whatever happened. Nothing here is a record of anything —
// losing a season is unrecoverable; losing a volume setting is a slider.
//
// So `settings_parse` cannot fail. It always leaves a usable `Settings` behind
// and reports what it skipped as typed notes, because "the file did not parse"
// is not a sentence anyone can act on and neither is a silently half-applied
// file. The engine side turns each note into the warning sentence it always
// printed; a unit test asserts on the note itself.
//
// ## The first key is not `version`
//
// It is `settings_version`, and the distinctness is load-bearing: `profile.h`'s
// lexer requires `version` as the first non-comment record, so neither file can
// be loaded as the other. Pointing this parser at `profile.save` yields a pile
// of unknown-key notes and the defaults; pointing the profile at `settings.cfg`
// fails with `version_not_first`. Both failures name themselves.

namespace kart::core {

// **Still 1 after the M5f comfort keys landed, and that is a decision.**
//
// `profile.h` bumps on every format change because an older build that cannot read
// a `best` record files the whole career under `.corrupt.1`. Nothing in this file
// can do that: an unknown key is skipped with a note, a missing key keeps its
// default, and a file that will not parse at all is never moved aside. So adding
// keys is safe in both directions by construction — a v1 build reading a file with
// `field_of_view_deg` in it skips one line and draws a menu, which is the exact
// outcome the best-effort policy was chosen for.
//
// There is also no settings migration chain for a number to drive. Bumping would
// be a version nobody reads, changing on a schedule nobody can act on, and
// `parse` would still have to tolerate every combination of keys regardless.
inline constexpr int SETTINGS_FORMAT_VERSION = 1;

// The camera rigs a settings file can name. `scripts/game/` owns what each one
// does; this enum owns only the spelling on disk.
enum class SettingsCamera : int {
	Chase = 0,
	Cockpit = 1,
	Free = 2,
};

inline constexpr int SETTINGS_CAMERA_COUNT = 3;

inline const char *settings_camera_name(SettingsCamera camera) {
	switch (camera) {
		case SettingsCamera::Chase: return "chase";
		case SettingsCamera::Cockpit: return "cockpit";
		case SettingsCamera::Free: return "free";
	}
	return "invalid";
}

inline bool settings_parse_camera(const char *text, int len, SettingsCamera &out) {
	for (int i = 0; i < SETTINGS_CAMERA_COUNT; ++i) {
		const SettingsCamera camera = static_cast<SettingsCamera>(i);
		if (profile_text_equals(text, len, settings_camera_name(camera))) {
			out = camera;
			return true;
		}
	}
	return false;
}

inline constexpr double SETTINGS_MIN_VOLUME_DB = -60.0;
inline constexpr double SETTINGS_MAX_VOLUME_DB = 6.0;

// `player_driver.h`'s table: below 0.15 the whole followable steering range at
// 100 km/h is inside the deadzone. Above 0.5 a stick has no usable travel left.
inline constexpr double SETTINGS_MIN_DEADZONE = 0.0;
inline constexpr double SETTINGS_MAX_DEADZONE = 0.5;

// --- ARCHITECTURE.md §18's comfort options ------------------------------------
//
// §18 asks for six things and five of them are here; full input remapping is the
// sixth and is a screen rather than a value, so it is not in this struct.
//
// **Field of view is a trim in degrees, not an absolute.** §18 says "FOV slider
// per camera rig" and the two rigs do not share a number: `chase_camera.gd` runs
// 62 to 78 degrees and `cockpit_camera.gd` runs 74 to 92, because a chase view
// already shows the kart moving through the world and a cockpit view has only the
// road going past. An absolute setting would have to pick one of those and derive
// the other, which is a second owner of a relationship both files already state.
// A trim added to both keeps each rig's own tuning and its own kick and still gives
// a player the one control they actually want.
//
// +-20 degrees, which is generous rather than measured: it lands the chase rig at
// 42-98 and the cockpit at 54-112, both still projections a person can drive from.
// Estimated, per CLAUDE.md's three-way rule — there is no source for a comfortable
// FOV range and this is a judgement about what stays usable.
inline constexpr double SETTINGS_MIN_FOV_TRIM_DEG = -20.0;
inline constexpr double SETTINGS_MAX_FOV_TRIM_DEG = 20.0;

// Head motion, camera shake and motion blur are all **multipliers on the shipped
// intensity**, so 1.0 is what the rigs were tuned at and 0.0 is fully off — §18
// asks for "including full off" in as many words, and for motion blur it asks for
// a toggle, which 0.0 is.
//
// The ceiling is 2.0 rather than 1.0 for one reason per setting. Head motion and
// shake: a driver who wants more motion is as legitimate as one who wants less,
// and a slider that only subtracts reads as an apology. Motion blur: the
// multiplier scales `motion_blur.gd`'s 180-degree shutter, and 2.0 is exactly the
// 360-degree shutter that file already documents as the maximum meaningful value —
// a shutter that never closes. So the ceiling is derived there and merely shared
// by the other two.
inline constexpr double SETTINGS_MIN_INTENSITY = 0.0;
inline constexpr double SETTINGS_MAX_INTENSITY = 2.0;

inline double settings_clamp(double value, double low, double high) {
	if (!(value == value)) { // NaN
		return low > 0.0 || high < 0.0 ? low : 0.0;
	}
	if (value < low) {
		return low;
	}
	if (value > high) {
		return high;
	}
	return value;
}

// The eleven settings, at the defaults a fresh install runs.
//
// Assists both default **on** — issue #40, and `ARCHITECTURE.md` §19's
// "unplayable for newcomers" risk with the mitigation removed. The deadzone
// default is `project.godot`'s 0.15 for the steer actions. The volume trim is
// ADR-0039's measured Master trim, made a preference at 0.0 here because the
// trim itself lives on the bus and this is the player's offset from it.
//
// **Every comfort default is the value its consumer already shipped**, which is
// the property that matters more than any of the individual numbers: a player who
// never opens the settings screen must get the same picture they got before it
// existed. A trim of 0.0 and a multiplier of 1.0 are that statement written down,
// and `horizon_lock` is false because `cockpit_camera.gd` follows the body's roll
// today and turning that off by default would be a silent art change shipped under
// the heading of an accessibility option.
//
// The clamping setters exist because a slider cannot produce an out-of-range
// value but a hand-edited file can, and one bad number must not cost the rest
// of the file its keys.
struct Settings {
	bool auto_clutch = true;
	bool auto_shift = true;
	SettingsCamera camera = SettingsCamera::Chase;
	double master_volume_db = 0.0;
	double steer_deadzone = 0.15;

	// ARCHITECTURE.md §18's comfort block. See the constants above for what each
	// range is and where it came from.
	double field_of_view_deg = 0.0;
	double head_motion = 1.0;
	double shake = 1.0;
	bool horizon_lock = false;
	double motion_blur = 1.0;

	// ADR-0052 §4, and it defaults **on**: pausing strikes the lap in flight.
	//
	// On is the honest default because the alternative is a lap time set with the
	// game stopped in the middle of it, and a records screen has no way to show
	// that unless the record says so — which is what issue #186's flag on the saved
	// best is for. A player may turn it off; the best they then set is marked.
	bool pause_invalidates_lap = true;

	void set_master_volume_db(double db) {
		master_volume_db = settings_clamp(db, SETTINGS_MIN_VOLUME_DB, SETTINGS_MAX_VOLUME_DB);
	}

	void set_steer_deadzone(double deadzone) {
		steer_deadzone = settings_clamp(deadzone, SETTINGS_MIN_DEADZONE, SETTINGS_MAX_DEADZONE);
	}

	void set_field_of_view_deg(double degrees) {
		field_of_view_deg =
				settings_clamp(degrees, SETTINGS_MIN_FOV_TRIM_DEG, SETTINGS_MAX_FOV_TRIM_DEG);
	}

	void set_head_motion(double scale) {
		head_motion = settings_clamp(scale, SETTINGS_MIN_INTENSITY, SETTINGS_MAX_INTENSITY);
	}

	void set_shake(double scale) {
		shake = settings_clamp(scale, SETTINGS_MIN_INTENSITY, SETTINGS_MAX_INTENSITY);
	}

	void set_motion_blur(double scale) {
		motion_blur = settings_clamp(scale, SETTINGS_MIN_INTENSITY, SETTINGS_MAX_INTENSITY);
	}

	bool set_camera(int value) {
		if (value < 0 || value >= SETTINGS_CAMERA_COUNT) {
			return false;
		}
		camera = static_cast<SettingsCamera>(value);
		return true;
	}
};

// --- the text format ------------------------------------------------------------

inline constexpr const char *SETTINGS_PREAMBLE =
		"# kartgame settings: comfort, controls and assists. Deliberately NOT in\n"
		"# profile.save -- a career that will not parse must still leave a menu a\n"
		"# player can read. See src/core/settings.h, ADR-0042.\n";

// The whole document is ASCII by construction — twelve keys, five words and six
// numbers — so a byte count of the buffer is a character count of the file.
//
// **512 was sized for five keys and is no longer enough**, so here is the
// arithmetic rather than another round number. Every line is `key`, one space, the
// widest value, a newline; a `format_value` number is at most `-60.000000`, ten
// characters.
//
//     preamble                                                        200
//     settings_version 1                                    17 + 1 + 1 +  1 =  20
//     assist_auto_clutch false                              18 + 1 + 5 +  1 =  25
//     assist_auto_shift false                               17 + 1 + 5 +  1 =  24
//     camera cockpit                                         6 + 1 + 7 +  1 =  15
//     master_volume_db -60.000000                           16 + 1 + 10 + 1 =  28
//     steer_deadzone 0.500000                               14 + 1 + 10 + 1 =  26
//     field_of_view_deg -20.000000                          17 + 1 + 10 + 1 =  29
//     head_motion 2.000000                                  11 + 1 + 10 + 1 =  23
//     shake 2.000000                                         5 + 1 + 10 + 1 =  17
//     horizon_lock false                                    12 + 1 + 5 +  1 =  19
//     motion_blur 2.000000                                  11 + 1 + 10 + 1 =  23
//     pause_invalidates_lap false                           21 + 1 + 5 +  1 =  28
//                                                                        ------
//                                                                          477
//
// The preamble is 197 characters and is counted at 200. 477 plus the terminator is
// 478, which 512 would just clear — and "just clears it" is how a buffer gets
// overrun by the next key somebody adds. 768 is the next size with room for
// roughly ten more keys, and `settings_format` returns -1 rather than truncating
// if it is ever wrong. A unit test formats the widest possible `Settings` and
// asserts the measured length against this constant, so the arithmetic above is
// checked rather than trusted.
inline constexpr int SETTINGS_TEXT_CHARS = 768;

// Declaration order, fixed, one key per line, every number through `tuning.h`'s
// `format_value` so a deadzone and a tunable are quantized to the same 1e-6
// grid. There is one number renderer in this project. Returns the length
// written, or -1 on a buffer too small — which for a five-key format is a
// caller passing something other than `SETTINGS_TEXT_CHARS`.
inline int settings_format(const Settings &settings, char *out, int cap) {
	if (out == nullptr || cap < 2) {
		return -1;
	}
	int written = 0;
	bool overflow = false;
	auto put = [&](const char *text) {
		for (const char *c = text; *c != '\0'; ++c) {
			if (written + 1 >= cap) {
				overflow = true;
				return;
			}
			out[written] = *c;
			++written;
		}
	};
	char number[TUNING_VALUE_CHARS];

	put(SETTINGS_PREAMBLE);
	if (format_int(SETTINGS_FORMAT_VERSION, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("settings_version ");
	put(number);
	put("\n");
	put("assist_auto_clutch ");
	put(settings.auto_clutch ? "true" : "false");
	put("\n");
	put("assist_auto_shift ");
	put(settings.auto_shift ? "true" : "false");
	put("\n");
	put("camera ");
	put(settings_camera_name(settings.camera));
	put("\n");
	if (format_value(settings.master_volume_db, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("master_volume_db ");
	put(number);
	put("\n");
	if (format_value(settings.steer_deadzone, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("steer_deadzone ");
	put(number);
	put("\n");

	// ARCHITECTURE.md §18's comfort block, in declaration order like everything
	// above it.
	if (format_value(settings.field_of_view_deg, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("field_of_view_deg ");
	put(number);
	put("\n");
	if (format_value(settings.head_motion, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("head_motion ");
	put(number);
	put("\n");
	if (format_value(settings.shake, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("shake ");
	put(number);
	put("\n");
	put("horizon_lock ");
	put(settings.horizon_lock ? "true" : "false");
	put("\n");
	if (format_value(settings.motion_blur, number, static_cast<int>(sizeof(number))) < 0) {
		return -1;
	}
	put("motion_blur ");
	put(number);
	put("\n");
	put("pause_invalidates_lap ");
	put(settings.pause_invalidates_lap ? "true" : "false");
	put("\n");

	if (overflow) {
		return -1;
	}
	out[written] = '\0';
	return written;
}

// --- the parse ------------------------------------------------------------------

// What a parse skipped, per line, typed. Not strings: `src/core/` does not
// allocate, and a test that asserts on an enum cannot rot the way one matching
// a sentence can. The engine side owns turning each note into the warning it
// has always printed.
enum class SettingsProblem : int {
	None = 0,
	VersionNotAVersion, // `settings_version` present and not a positive integer
	KeyWithNoValue,
	NotABool, // an assist key whose value is not exactly `true` or `false`
	NoSuchCamera,
	NotANumber,
	UnknownKey, // realistic: a newer build's key. Skipped, never refused.
	NoVersionLine, // whole-file: the file may not be a settings file at all
};

inline const char *settings_problem_name(SettingsProblem problem) {
	switch (problem) {
		case SettingsProblem::None: return "none";
		case SettingsProblem::VersionNotAVersion: return "version_not_a_version";
		case SettingsProblem::KeyWithNoValue: return "key_with_no_value";
		case SettingsProblem::NotABool: return "not_a_bool";
		case SettingsProblem::NoSuchCamera: return "no_such_camera";
		case SettingsProblem::NotANumber: return "not_a_number";
		case SettingsProblem::UnknownKey: return "unknown_key";
		case SettingsProblem::NoVersionLine: return "no_version_line";
	}
	return "invalid";
}

inline constexpr int SETTINGS_KEY_CHARS = 32;

// Enough for every note a plausible file produces. A file of pure garbage
// produces one note per line; past the cap they are counted rather than kept,
// because a report that is itself unbounded would be the allocation this
// header exists not to make.
inline constexpr int SETTINGS_MAX_NOTES = 16;

struct SettingsNote {
	int line = 0; // 1-based
	SettingsProblem problem = SettingsProblem::None;
	char key[SETTINGS_KEY_CHARS] = {};
};

struct SettingsParse {
	int applied = 0;
	int declared_version = 0;
	bool seen_version = false;
	int note_count = 0;
	int notes_dropped = 0;
	SettingsNote notes[SETTINGS_MAX_NOTES] = {};

	void note(int line, SettingsProblem problem, const char *key, int key_len) {
		if (note_count >= SETTINGS_MAX_NOTES) {
			++notes_dropped;
			return;
		}
		SettingsNote &entry = notes[note_count];
		entry.line = line;
		entry.problem = problem;
		int copied = 0;
		while (copied < key_len && copied < SETTINGS_KEY_CHARS - 1 && key != nullptr) {
			entry.key[copied] = key[copied];
			++copied;
		}
		entry.key[copied] = '\0';
		++note_count;
	}
};

// Parse settings text over `out`. Cannot fail: every key the text does not
// mention, or mentions badly, is left at whatever `out` already holds — so a
// caller that wants "defaults plus the file" resets `out` first, which is what
// the engine side does and why loading twice from two files leaves no mixture.
//
// Line oriented, blank lines and `#` comments dropped, `key` then the rest —
// the same lexing the profile and the preset format use, written out here
// rather than reusing `profile_lex` because that one requires `version` as the
// first record and refuses everything else, which is exactly the property that
// keeps the two files from being loaded as each other.
//
// `len` may be -1 for NUL-terminated text.
inline void settings_parse(const char *text, int len, Settings &out, SettingsParse &report) {
	report = SettingsParse{};
	if (text == nullptr) {
		report.note(0, SettingsProblem::NoVersionLine, "", 0);
		return;
	}
	if (len < 0) {
		len = profile_length(text);
	}

	int index = 0;
	int line_number = 0;
	while (index < len) {
		const int line_begin = index;
		while (index < len && text[index] != '\n') {
			++index;
		}
		int begin = line_begin;
		int end = index;
		++index;
		++line_number;
		while (begin < end && profile_is_space(text[begin])) {
			++begin;
		}
		for (int i = begin; i < end; ++i) {
			if (text[i] == '#') {
				end = i;
				break;
			}
		}
		while (end > begin && profile_is_space(text[end - 1])) {
			--end;
		}
		if (begin >= end) {
			continue;
		}
		int key_end = begin;
		while (key_end < end && !profile_is_space(text[key_end])) {
			++key_end;
		}
		const char *key = text + begin;
		const int key_len = key_end - begin;
		int value_begin = key_end;
		while (value_begin < end && profile_is_space(text[value_begin])) {
			++value_begin;
		}
		const char *value = text + value_begin;
		const int value_len = end - value_begin;

		if (profile_text_equals(key, key_len, "settings_version")) {
			if (!profile_parse_int(value, value_len, report.declared_version) ||
					report.declared_version < 1) {
				report.note(line_number, SettingsProblem::VersionNotAVersion, key, key_len);
				report.declared_version = 0;
			}
			report.seen_version = true;
			continue;
		}
		if (value_len <= 0) {
			report.note(line_number, SettingsProblem::KeyWithNoValue, key, key_len);
			continue;
		}
		double number = 0.0;
		if (profile_text_equals(key, key_len, "assist_auto_clutch")) {
			if (profile_text_equals(value, value_len, "true")) {
				out.auto_clutch = true;
				++report.applied;
			} else if (profile_text_equals(value, value_len, "false")) {
				out.auto_clutch = false;
				++report.applied;
			} else {
				// Exactly `true` or `false`. Not 1/0 and not yes/no: a settings file
				// is diffable text and one spelling per value is what keeps a round
				// trip byte-identical.
				report.note(line_number, SettingsProblem::NotABool, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "assist_auto_shift")) {
			if (profile_text_equals(value, value_len, "true")) {
				out.auto_shift = true;
				++report.applied;
			} else if (profile_text_equals(value, value_len, "false")) {
				out.auto_shift = false;
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotABool, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "camera")) {
			SettingsCamera camera = SettingsCamera::Chase;
			if (settings_parse_camera(value, value_len, camera)) {
				out.camera = camera;
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NoSuchCamera, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "master_volume_db")) {
			if (parse_value(value, value_len, number)) {
				out.set_master_volume_db(number);
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotANumber, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "steer_deadzone")) {
			if (parse_value(value, value_len, number)) {
				out.set_steer_deadzone(number);
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotANumber, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "field_of_view_deg")) {
			if (parse_value(value, value_len, number)) {
				out.set_field_of_view_deg(number);
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotANumber, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "head_motion")) {
			if (parse_value(value, value_len, number)) {
				out.set_head_motion(number);
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotANumber, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "shake")) {
			if (parse_value(value, value_len, number)) {
				out.set_shake(number);
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotANumber, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "horizon_lock")) {
			if (profile_text_equals(value, value_len, "true")) {
				out.horizon_lock = true;
				++report.applied;
			} else if (profile_text_equals(value, value_len, "false")) {
				out.horizon_lock = false;
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotABool, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "motion_blur")) {
			if (parse_value(value, value_len, number)) {
				out.set_motion_blur(number);
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotANumber, key, key_len);
			}
		} else if (profile_text_equals(key, key_len, "pause_invalidates_lap")) {
			if (profile_text_equals(value, value_len, "true")) {
				out.pause_invalidates_lap = true;
				++report.applied;
			} else if (profile_text_equals(value, value_len, "false")) {
				out.pause_invalidates_lap = false;
				++report.applied;
			} else {
				report.note(line_number, SettingsProblem::NotABool, key, key_len);
			}
		} else {
			report.note(line_number, SettingsProblem::UnknownKey, key, key_len);
		}
	}

	if (!report.seen_version) {
		report.note(line_number + 1, SettingsProblem::NoVersionLine, "", 0);
	}
}

} // namespace kart::core

#endif // KART_CORE_SETTINGS_H
