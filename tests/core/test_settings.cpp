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
}

TEST_CASE("the text round-trips to byte-identical text") {
	Settings settings;
	settings.auto_clutch = false;
	settings.camera = SettingsCamera::Cockpit;
	settings.set_master_volume_db(-12.5);
	settings.set_steer_deadzone(0.22);

	static char first[SETTINGS_TEXT_CHARS];
	const int first_length = settings_format(settings, first, SETTINGS_TEXT_CHARS);
	REQUIRE(first_length > 0);

	Settings parsed;
	SettingsParse report;
	settings_parse(first, first_length, parsed, report);
	CHECK(report.applied == 5);
	CHECK(report.seen_version);
	CHECK(report.declared_version == SETTINGS_FORMAT_VERSION);
	CHECK(report.note_count == 0);

	CHECK(parsed.auto_clutch == settings.auto_clutch);
	CHECK(parsed.auto_shift == settings.auto_shift);
	CHECK(parsed.camera == settings.camera);
	CHECK(parsed.master_volume_db == settings.master_volume_db);
	CHECK(parsed.steer_deadzone == settings.steer_deadzone);

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
