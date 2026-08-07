#include "doctest.h"

#include "core/settings.h"

#include <cstring>

// Issue #178: the settings model lived in `src/session/kart_profile.cpp`, where
// this suite could not reach it, with a comment saying it belonged in core. The
// behaviors below are the ones the engine side promised in prose — best-effort
// load, one spelling per bool, clamp rather than refuse, neither file loadable
// as the other — asserted here as the contract of the moved code.

using namespace kart::core;

namespace {

bool has_note(const SettingsParse &report, SettingsProblem problem, const char *key) {
	for (int i = 0; i < report.note_count; ++i) {
		if (report.notes[i].problem != problem) {
			continue;
		}
		if (key == nullptr || std::strcmp(report.notes[i].key, key) == 0) {
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("the defaults are the documented defaults") {
	const Settings fresh;
	CHECK(fresh.auto_clutch); // issue #40: both assists on, or a KZ2 stalls on the grid
	CHECK(fresh.auto_shift);
	CHECK(fresh.camera == SettingsCamera::Chase);
	CHECK(fresh.master_volume_db == 0.0);
	CHECK(fresh.steer_deadzone == 0.15); // project.godot's steer-action deadzone

	// ARCHITECTURE.md §18's comfort block. **Every one of these is the value its
	// consumer already shipped**, which is the property that matters more than the
	// individual numbers: a player who never opens the settings screen must get
	// exactly the picture they got before the screen existed. A trim of zero and a
	// multiplier of one are that written down.
	CHECK(fresh.field_of_view_deg == 0.0);
	CHECK(fresh.head_motion == 1.0);
	CHECK(fresh.shake == 1.0);
	CHECK(fresh.horizon_lock == false); // cockpit_camera.gd follows the body today
	CHECK(fresh.motion_blur == 1.0); // motion_blur.gd's 180-degree shutter, unscaled

	CHECK(fresh.pause_invalidates_lap); // ADR-0052 §4
}

TEST_CASE("the widest possible settings file fits SETTINGS_TEXT_CHARS") {
	// The header carries the arithmetic; this checks it rather than trusting it.
	// 512 was sized for five keys and the M5f comfort block took the document past
	// it, which is the kind of overrun that shows up as a settings file silently
	// refusing to save.
	Settings widest;
	widest.auto_clutch = false;
	widest.auto_shift = false;
	widest.camera = SettingsCamera::Cockpit; // the longest camera name
	widest.set_master_volume_db(SETTINGS_MIN_VOLUME_DB); // -60.000000, ten characters
	widest.set_steer_deadzone(SETTINGS_MAX_DEADZONE);
	widest.set_field_of_view_deg(SETTINGS_MIN_FOV_TRIM_DEG); // -20.000000
	widest.set_head_motion(SETTINGS_MAX_INTENSITY);
	widest.set_shake(SETTINGS_MAX_INTENSITY);
	widest.horizon_lock = false; // `false` is a character longer than `true`
	widest.set_motion_blur(SETTINGS_MAX_INTENSITY);
	widest.pause_invalidates_lap = false;

	static char text[SETTINGS_TEXT_CHARS];
	const int length = settings_format(widest, text, SETTINGS_TEXT_CHARS);
	REQUIRE(length > 0);
	MESSAGE("widest settings document: " << length << " of " << SETTINGS_TEXT_CHARS
										 << " characters");
	// Room for roughly ten more keys. A buffer that only just clears is a buffer the
	// next key overruns.
	CHECK(length < SETTINGS_TEXT_CHARS - 200);

	// And it refuses rather than truncating when the buffer genuinely is too small,
	// which is the property that makes the margin above a comfort rather than a
	// requirement.
	static char cramped[64];
	CHECK(settings_format(widest, cramped, static_cast<int>(sizeof(cramped))) < 0);
}

TEST_CASE("the text round-trips to byte-identical text") {
	Settings settings;
	settings.auto_clutch = false;
	settings.camera = SettingsCamera::Cockpit;
	settings.set_master_volume_db(-12.5);
	settings.set_steer_deadzone(0.22);
	// Every comfort key moved off its default too, so a key the formatter or the
	// parser dropped shows up as a value that came back at the default rather than
	// as an equality that was true before the round trip started.
	settings.set_field_of_view_deg(-7.5);
	settings.set_head_motion(0.4);
	settings.set_shake(1.75);
	settings.horizon_lock = true;
	settings.set_motion_blur(0.0);
	settings.pause_invalidates_lap = false;

	static char first[SETTINGS_TEXT_CHARS];
	const int first_length = settings_format(settings, first, SETTINGS_TEXT_CHARS);
	REQUIRE(first_length > 0);

	Settings parsed;
	SettingsParse report;
	settings_parse(first, first_length, parsed, report);
	// Eleven keys, and `settings_version` is not one of them — it is read by the
	// version branch, which returns before `applied` is touched.
	CHECK(report.applied == 11);
	CHECK(report.seen_version);
	CHECK(report.declared_version == SETTINGS_FORMAT_VERSION);
	CHECK(report.note_count == 0);

	CHECK(parsed.auto_clutch == settings.auto_clutch);
	CHECK(parsed.auto_shift == settings.auto_shift);
	CHECK(parsed.camera == settings.camera);
	CHECK(parsed.master_volume_db == settings.master_volume_db);
	CHECK(parsed.steer_deadzone == settings.steer_deadzone);
	CHECK(parsed.field_of_view_deg == settings.field_of_view_deg);
	CHECK(parsed.head_motion == settings.head_motion);
	CHECK(parsed.shake == settings.shake);
	CHECK(parsed.horizon_lock == settings.horizon_lock);
	CHECK(parsed.motion_blur == settings.motion_blur);
	CHECK(parsed.pause_invalidates_lap == settings.pause_invalidates_lap);

	static char second[SETTINGS_TEXT_CHARS];
	const int second_length = settings_format(parsed, second, SETTINGS_TEXT_CHARS);
	REQUIRE(second_length == first_length);
	CHECK(std::memcmp(first, second, static_cast<size_t>(first_length)) == 0);
}

TEST_CASE("an unknown key is skipped with a note and costs nothing else") {
	// The realistic case: a newer build wrote a key this one does not have.
	// Refusing the file would leave a downgrading player without a readable menu.
	const char *text =
			"settings_version 1\n"
			"hud_opacity 0.8\n"
			"assist_auto_clutch false\n";
	Settings parsed;
	SettingsParse report;
	settings_parse(text, -1, parsed, report);
	CHECK(report.applied == 1);
	CHECK_FALSE(parsed.auto_clutch);
	CHECK(has_note(report, SettingsProblem::UnknownKey, "hud_opacity"));
	CHECK(report.notes[0].line == 2);
}

TEST_CASE("a bad value falls back to its default and names itself") {
	SUBCASE("a bool that is not exactly true or false") {
		// Not 1/0 and not yes/no: one spelling per value is what keeps a round
		// trip byte-identical.
		const char *text = "settings_version 1\nassist_auto_shift yes\n";
		Settings parsed;
		SettingsParse report;
		settings_parse(text, -1, parsed, report);
		CHECK(parsed.auto_shift); // still the default
		CHECK(report.applied == 0);
		CHECK(has_note(report, SettingsProblem::NotABool, "assist_auto_shift"));
	}
	SUBCASE("a camera naming no rig") {
		const char *text = "settings_version 1\ncamera drone\n";
		Settings parsed;
		SettingsParse report;
		settings_parse(text, -1, parsed, report);
		CHECK(parsed.camera == SettingsCamera::Chase);
		CHECK(has_note(report, SettingsProblem::NoSuchCamera, "camera"));
	}
	SUBCASE("a number that is not a number") {
		const char *text = "settings_version 1\nmaster_volume_db loud\n";
		Settings parsed;
		SettingsParse report;
		settings_parse(text, -1, parsed, report);
		CHECK(parsed.master_volume_db == 0.0);
		CHECK(has_note(report, SettingsProblem::NotANumber, "master_volume_db"));
	}
	SUBCASE("a key with no value") {
		const char *text = "settings_version 1\nsteer_deadzone\n";
		Settings parsed;
		SettingsParse report;
		settings_parse(text, -1, parsed, report);
		CHECK(parsed.steer_deadzone == 0.15);
		CHECK(has_note(report, SettingsProblem::KeyWithNoValue, "steer_deadzone"));
	}
}

TEST_CASE("out-of-range numbers clamp rather than refuse") {
	// A slider cannot produce these; a hand-edited file can, and one bad number
	// must not cost the rest of the file its keys.
	const char *text =
			"settings_version 1\n"
			"master_volume_db 100.0\n"
			"steer_deadzone 0.9\n";
	Settings parsed;
	SettingsParse report;
	settings_parse(text, -1, parsed, report);
	CHECK(report.applied == 2);
	CHECK(report.note_count == 0); // clamped is applied, not skipped
	CHECK(parsed.master_volume_db == SETTINGS_MAX_VOLUME_DB);
	CHECK(parsed.steer_deadzone == SETTINGS_MAX_DEADZONE);
}

TEST_CASE("the version line is required in name only") {
	SUBCASE("a missing version is a note, not a refusal") {
		const char *text = "assist_auto_clutch false\n";
		Settings parsed;
		SettingsParse report;
		settings_parse(text, -1, parsed, report);
		CHECK_FALSE(report.seen_version);
		CHECK(report.applied == 1); // the file still loaded
		CHECK(has_note(report, SettingsProblem::NoVersionLine, nullptr));
	}
	SUBCASE("a version that is not a version is noted and zeroed") {
		const char *text = "settings_version soon\n";
		Settings parsed;
		SettingsParse report;
		settings_parse(text, -1, parsed, report);
		CHECK(report.seen_version);
		CHECK(report.declared_version == 0);
		CHECK(has_note(report, SettingsProblem::VersionNotAVersion, "settings_version"));
	}
}

TEST_CASE("a profile pointed at the settings parser yields defaults and noise, not a mixture") {
	// The other half of the two-file distinctness property. `profile.h`'s lexer
	// refuses `settings.cfg` with `version_not_first`; this direction cannot
	// refuse, so the guarantee is weaker and still enough: nothing a profile
	// says is a settings key, so nothing is applied.
	const char *text =
			"version 1\n"
			"driver_name Anthony\n"
			"career_class 1\n";
	Settings parsed;
	SettingsParse report;
	settings_parse(text, -1, parsed, report);
	CHECK(report.applied == 0);
	CHECK_FALSE(report.seen_version); // `version` is not `settings_version`
	CHECK(has_note(report, SettingsProblem::UnknownKey, "version"));
	const Settings fresh;
	CHECK(parsed.auto_clutch == fresh.auto_clutch);
	CHECK(parsed.camera == fresh.camera);
}

TEST_CASE("comments, blank lines and inline comments cost nothing") {
	const char *text =
			"# a comment\n"
			"\n"
			"settings_version 1\n"
			"   camera free   # the debug rig\n";
	Settings parsed;
	SettingsParse report;
	settings_parse(text, -1, parsed, report);
	CHECK(report.applied == 1);
	CHECK(report.note_count == 0);
	CHECK(parsed.camera == SettingsCamera::Free);
}

TEST_CASE("a file of garbage overflows the notes by counting, not by growing") {
	static char text[2048];
	int written = 0;
	const char *line = "nonsense_key nonsense\n";
	for (int i = 0; i < 24; ++i) {
		for (const char *c = line; *c != '\0'; ++c) {
			text[written++] = *c;
		}
	}
	text[written] = '\0';
	Settings parsed;
	SettingsParse report;
	settings_parse(text, written, parsed, report);
	CHECK(report.note_count == SETTINGS_MAX_NOTES);
	// 24 unknown keys plus the missing version line, minus the 16 kept.
	CHECK(report.notes_dropped == 24 + 1 - SETTINGS_MAX_NOTES);
	CHECK(report.applied == 0);
}
