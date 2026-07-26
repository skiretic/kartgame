#include "doctest.h"

#include "core/profile.h"

#include <cstdio>
#include <cstring>
#include <string>

// The save format, ADR-0042. What is tested here is not "does a field round trip"
// — that is one of the cheap ones — but the four properties the ADR actually buys:
//
//   1. **a real captured v1 file loads and means what it says**, field by field,
//      and it is the checked-in file rather than one this test wrote;
//   2. a write-read-write is byte-identical, which is the bar
//      `tools/verify/tuning.sh --check` already holds the preset format to;
//   3. a file that does not parse is **rejected**, not half-loaded — truncated,
//      garbage, no version line, a renamed field — because best-effort loading is
//      the alternative ADR-0042 explicitly rejected and it fails silently in the
//      one case that matters;
//   4. the migration machinery chains, in order, and refuses a gap. Exercised
//      against a synthetic table because the real one is empty at version 1, and
//      inventing a fictional v2 *file* to have something to migrate would put a
//      fake capture in a corpus whose whole value is that its files are real.

using namespace kart::core;

namespace {

// Where the corpus lives, without depending on the working directory.
//
// No existing test in this suite reads anything off disk, so there was no
// mechanism to copy. `tests/run.sh` happens to run the binary from the project
// root and compiles with `-I src/ -I third_party/doctest` only, so there is no
// `-D` to carry a path in and nothing may lean on the cwd. `__FILE__` is the
// cheapest thing that cannot break: `run.sh` builds its source list with
// `find "$PROJECT_ROOT/tests"`, so the compiler is handed an absolute path and
// `__FILE__` is `<project>/tests/core/test_profile.cpp`. Trimming the tail off
// that gives the project root at compile time, whoever runs the binary and from
// wherever.
std::string project_root() {
	const std::string self = __FILE__;
	const std::string tail = "tests/core/test_profile.cpp";
	if (self.size() >= tail.size() && self.compare(self.size() - tail.size(), tail.size(), tail) == 0) {
		return self.substr(0, self.size() - tail.size());
	}
	// A relative or otherwise unexpected `__FILE__`. Fall back to the cwd rather
	// than to a wrong absolute path, and let the read fail loudly.
	return std::string();
}

std::string corpus_path(const char *name) {
	return project_root() + "tests/data/saves/" + name;
}

// Read a whole file. Returns empty on any failure, and every caller REQUIREs a
// non-empty result so a missing corpus file fails as "the corpus is missing"
// rather than as a parse error.
std::string read_file(const std::string &path) {
	std::FILE *handle = std::fopen(path.c_str(), "rb");
	if (handle == nullptr) {
		return std::string();
	}
	std::string contents;
	char buffer[4096];
	size_t read = 0;
	while ((read = std::fread(buffer, 1, sizeof(buffer), handle)) > 0) {
		contents.append(buffer, read);
	}
	std::fclose(handle);
	return contents;
}

std::string corpus_v1() {
	const std::string text = read_file(corpus_path("v1.save"));
	REQUIRE_MESSAGE(!text.empty(), "tests/data/saves/v1.save is missing or unreadable");
	return text;
}

// Serialize into a std::string so tests can compare with `==` and print a diff.
std::string write_profile(const Profile &profile) {
	std::string out(PROFILE_TEXT_CAP, '\0');
	const int length = format_profile(profile, &out[0], PROFILE_TEXT_CAP);
	if (length < 0) {
		return std::string();
	}
	out.resize(static_cast<size_t>(length));
	return out;
}

ProfileLoadResult load(const std::string &text, Profile &out) {
	return load_profile(text.data(), static_cast<int>(text.size()), out);
}

// Replace the first occurrence of `from` with `to`. Used to mutate the real v1
// file into the specific broken files the failure tests need, so those tests are
// about a real save with one thing wrong rather than about a string somebody typed.
std::string replace_first(const std::string &text, const std::string &from,
		const std::string &to) {
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	std::string copy = text;
	copy.replace(at, from.size(), to);
	return copy;
}

std::string replace_all(const std::string &text, const std::string &from,
		const std::string &to) {
	std::string copy;
	size_t index = 0;
	bool found = false;
	while (index < text.size()) {
		const size_t at = text.find(from, index);
		if (at == std::string::npos) {
			copy.append(text, index, std::string::npos);
			break;
		}
		found = true;
		copy.append(text, index, at - index);
		copy.append(to);
		index = at + from.size();
	}
	REQUIRE(found);
	return copy;
}

// --- the synthetic migration chain ---------------------------------------------
//
// Versions 2 and 3 do not exist and no file in `tests/data/saves/` pretends they
// do. These four functions exist so the *machinery* — chaining, ordering, gap
// detection, the failure path — runs through the real `profile_run_migrations`
// today, against a table passed in as an argument exactly so that it can be.

int migration_1_calls = 0;
int migration_2_calls = 0;

bool migrate_1_to_2(ProfileDocument &document) {
	++migration_1_calls;
	// Something observable, so "did it run" is not answered by a counter alone.
	return document.set_text("driver_livery", "migrated_one");
}

bool migrate_2_to_3(ProfileDocument &document) {
	++migration_2_calls;
	return document.set_text("driver_number", "222");
}

bool migrate_fails(ProfileDocument &) {
	return false;
}

// A migration that renames a key the current schema does not know. This is
// ADR-0042's own worked failure, injected: the file said `standing`, the migration
// renamed it to `championship`, and nothing in the current schema accepts that.
// The strict binder has to say so rather than produce an empty career.
bool migrate_renames_standings_away(ProfileDocument &document) {
	return document.rename_key("standing", "championship") > 0;
}

const ProfileMigration TWO_STEP_CHAIN[] = {
	{ 1, 2, migrate_1_to_2, "test: v1 to v2" },
	{ 2, 3, migrate_2_to_3, "test: v2 to v3" },
};
const int TWO_STEP_COUNT = 2;

} // namespace

// --- the corpus ----------------------------------------------------------------

TEST_CASE("the captured v1 file loads and yields exactly the values written in it") {
	const std::string text = corpus_v1();
	Profile profile;
	const ProfileLoadResult result = load(text, profile);

	CAPTURE(profile_load_status_name(result.status));
	CAPTURE(profile_problem_name(result.problem));
	CAPTURE(result.line);
	CAPTURE(result.detail);
	REQUIRE(result.ok());
	CHECK(result.declared_version == 1);
	// One version exists, so the chain from it to the current version is empty.
	CHECK(result.migrations_applied == 0);

	CHECK(std::strcmp(profile.driver.name, "Skirving, Anthony") == 0);
	CHECK(profile.driver.number == 101);
	CHECK(std::strcmp(profile.driver.nationality, "GBR") == 0);
	CHECK(std::strcmp(profile.driver.livery, "works_blue") == 0);

	CHECK(profile.career.kart_class == KartClass::OK);
	CHECK(profile.career.season == 0);
	CHECK(profile.career.round == 2);

	// Standings keep the file's order, because the order *is* the classification.
	REQUIRE(profile.career.standings_count == 8);
	CHECK(std::strcmp(profile.career.standings[0].driver_id, "turney_joe") == 0);
	CHECK(tuning_micro(profile.career.standings[0].points) == tuning_micro(88.0));
	CHECK(std::strcmp(profile.career.standings[1].driver_id, "toviggino_maximo") == 0);
	CHECK(tuning_micro(profile.career.standings[1].points) == tuning_micro(76.0));
	CHECK(std::strcmp(profile.career.standings[2].driver_id, "cuman_nicolo") == 0);
	CHECK(tuning_micro(profile.career.standings[2].points) == tuning_micro(71.0));
	CHECK(std::strcmp(profile.career.standings[3].driver_id, "skirving_anthony") == 0);
	CHECK(tuning_micro(profile.career.standings[3].points) == tuning_micro(63.0));
	CHECK(std::strcmp(profile.career.standings[4].driver_id, "bertuca_alfio") == 0);
	CHECK(tuning_micro(profile.career.standings[4].points) == tuning_micro(57.0));
	CHECK(std::strcmp(profile.career.standings[5].driver_id, "lammers_kean") == 0);
	CHECK(tuning_micro(profile.career.standings[5].points) == tuning_micro(44.0));
	CHECK(std::strcmp(profile.career.standings[6].driver_id, "vidales_david") == 0);
	CHECK(tuning_micro(profile.career.standings[6].points) == tuning_micro(38.0));
	CHECK(std::strcmp(profile.career.standings[7].driver_id, "powell_zachary") == 0);
	CHECK(tuning_micro(profile.career.standings[7].points) == tuning_micro(29.0));

	REQUIRE(profile.best_count == 4);
	// Sorted by (track_id, layout, class) and the file is authored in that order,
	// so the array indices below are also the file's line order.
	CHECK(std::strcmp(profile.bests[0].track_id, "autumn_ridge") == 0);
	CHECK(profile.bests[0].layout == TrackLayout::Forward);
	CHECK(profile.bests[0].kart_class == KartClass::OK);
	CHECK(tuning_micro(profile.bests[0].lap_time_s) == tuning_micro(48.412355));
	CHECK(std::strcmp(profile.bests[0].ghost_id, "ar_fwd_ok_0001") == 0);

	CHECK(std::strcmp(profile.bests[1].track_id, "autumn_ridge") == 0);
	CHECK(profile.bests[1].layout == TrackLayout::Forward);
	CHECK(profile.bests[1].kart_class == KartClass::KZ2);
	CHECK(tuning_micro(profile.bests[1].lap_time_s) == tuning_micro(44.108900));
	CHECK(std::strcmp(profile.bests[1].ghost_id, "ar_fwd_kz2_0003") == 0);

	CHECK(std::strcmp(profile.bests[2].track_id, "autumn_ridge") == 0);
	CHECK(profile.bests[2].layout == TrackLayout::Reverse);
	CHECK(profile.bests[2].kart_class == KartClass::OK);
	CHECK(tuning_micro(profile.bests[2].lap_time_s) == tuning_micro(49.877010));
	CHECK(std::strcmp(profile.bests[2].ghost_id, "ar_rev_ok_0001") == 0);

	CHECK(std::strcmp(profile.bests[3].track_id, "brackwater") == 0);
	CHECK(profile.bests[3].layout == TrackLayout::Forward);
	CHECK(profile.bests[3].kart_class == KartClass::OK);
	CHECK(tuning_micro(profile.bests[3].lap_time_s) == tuning_micro(62.330500));
	CHECK(std::strcmp(profile.bests[3].ghost_id, "bw_fwd_ok_0002") == 0);

	// The lookup a session actually makes, and the one it must not find.
	const ProfileBest *kz2 = profile.find_best("autumn_ridge", TrackLayout::Forward,
			KartClass::KZ2);
	REQUIRE(kz2 != nullptr);
	CHECK(tuning_micro(kz2->lap_time_s) == tuning_micro(44.108900));
	CHECK(profile.find_best("brackwater", TrackLayout::Reverse, KartClass::KZ2) == nullptr);
}

TEST_CASE("the captured v1 file is byte-identical to what the v1 writer produces") {
	// The strictest statement available: the checked-in capture is exactly what
	// `format_profile` emits at this version, comment preamble included. Change
	// anything the writer writes and this fails until the version is bumped and a
	// new file is captured, which is ADR-0042's "bump on every format change" made
	// mechanical instead of remembered.
	const std::string text = corpus_v1();
	Profile profile;
	REQUIRE(load(text, profile).ok());
	const std::string written = write_profile(profile);
	REQUIRE(!written.empty());

	if (written != text) {
		// Print the first divergence, because a 1,000-character mismatch reported as
		// "not equal" is a diagnostic nobody can act on.
		size_t at = 0;
		while (at < written.size() && at < text.size() && written[at] == text[at]) {
			++at;
		}
		MESSAGE("first difference at byte " << at);
		MESSAGE("corpus: " << text.substr(at, 60));
		MESSAGE("writer: " << written.substr(at, 60));
	}
	CHECK(written == text);
	CHECK(written.size() == text.size());
}

TEST_CASE("write, read, write is byte-identical") {
	// The property `tools/verify/tuning.sh --check` holds the preset format to, and
	// the right bar here: a save that reordered or re-rounded itself on every write
	// would show a whole-file diff for a career that gained one point.
	const std::string text = corpus_v1();
	Profile first;
	REQUIRE(load(text, first).ok());

	const std::string a = write_profile(first);
	REQUIRE(!a.empty());
	Profile second;
	REQUIRE(load(a, second).ok());
	const std::string b = write_profile(second);
	REQUIRE(!b.empty());
	CHECK(a == b);

	// And a third pass, because an idempotence bug can be a two-cycle.
	Profile third;
	REQUIRE(load(b, third).ok());
	CHECK(write_profile(third) == b);
}

TEST_CASE("a hand-edited file is canonicalized rather than refused") {
	// The format is meant to be diffable and hand-editable — ADR-0042 wants a bug
	// report to carry a save inline — so a non-canonical integer, extra blank lines
	// and an inline comment all load, and the next write puts the file back in
	// canonical form. Refusing them would spend a career to teach somebody about
	// leading zeros.
	std::string text = corpus_v1();
	text = replace_first(text, "driver_number 101", "driver_number 0101   # hand-edited");
	text = replace_first(text, "career_round 2", "\n\ncareer_round +2\n\n");

	Profile profile;
	const ProfileLoadResult result = load(text, profile);
	CAPTURE(profile_problem_name(result.problem));
	CAPTURE(result.line);
	CAPTURE(result.detail);
	REQUIRE(result.ok());
	CHECK(profile.driver.number == 101);
	CHECK(profile.career.round == 2);
	// Back to the canonical file, which is the corpus file.
	CHECK(write_profile(profile) == corpus_v1());
}

// --- rejection rather than half-loading ---------------------------------------

TEST_CASE("a truncated file never half-loads a record") {
	// Every prefix of the real file. A prefix that ends on a line boundary is a
	// legitimately shorter save and must load; a prefix that stops inside a record
	// must be refused rather than producing a partial one.
	//
	// **The residual limitation, stated rather than hidden:** a cut can land
	// somewhere that is still structurally valid — `driver_number 10` is a real
	// number and `ar_fwd_ok_000` is a real slug — and no format detects that
	// without a checksum over the content. A checksum was rejected because it would
	// make hand-editing, which this format exists to permit, indistinguishable from
	// corruption. What actually prevents truncation is the atomic write: temp file,
	// fsync, rename. That is why rule 5 and rule 4 of ADR-0042 are the same rule
	// seen twice.
	const std::string full = corpus_v1();
	int accepted = 0;
	int rejected = 0;
	for (size_t cut = 0; cut <= full.size(); ++cut) {
		Profile profile;
		const ProfileLoadResult result =
				load_profile(full.data(), static_cast<int>(cut), profile);
		if (result.ok()) {
			++accepted;
			// Whatever was accepted is a coherent, shorter save: it re-serializes and
			// re-parses to itself. This is the property that rules out a half-loaded
			// record, because a partial record could not survive a round trip.
			const std::string again = write_profile(profile);
			REQUIRE(!again.empty());
			Profile reparsed;
			REQUIRE(load(again, reparsed).ok());
			CHECK(write_profile(reparsed) == again);
		} else {
			++rejected;
			// Never `FutureVersion`: a truncated file of this version is corruption.
			CHECK(result.status == ProfileLoadStatus::Corrupt);
		}
	}
	MESSAGE("truncation sweep over " << full.size() + 1 << " prefixes: " << accepted
			<< " accepted as shorter valid saves, " << rejected << " rejected as corrupt");
	// The empty prefix and the full file are the two ends, and both are decided.
	CHECK(accepted >= 1);
	CHECK(rejected >= 1);

	// The specific mid-record cuts, named so a regression says which shape broke.
	SUBCASE("cut inside a best record is the wrong token count") {
		const size_t at = full.find("best brackwater forward ok 62");
		REQUIRE(at != std::string::npos);
		Profile profile;
		const ProfileLoadResult result =
				load_profile(full.data(), static_cast<int>(at + 30), profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		CHECK(result.problem == ProfileProblem::WrongTokenCount);
		CHECK(std::strcmp(result.detail, "best") == 0);
	}
	SUBCASE("cut inside a standing record is the wrong token count") {
		const size_t at = full.find("standing turney_joe");
		REQUIRE(at != std::string::npos);
		Profile profile;
		const ProfileLoadResult result =
				load_profile(full.data(), static_cast<int>(at + 12), profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		CHECK(result.problem == ProfileProblem::WrongTokenCount);
	}
	SUBCASE("cut before the version line has no version at all") {
		Profile profile;
		const ProfileLoadResult result = load_profile(full.data(), 20, profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		CHECK(result.problem == ProfileProblem::NoVersionLine);
	}
}

TEST_CASE("garbage is rejected, whatever shape the garbage is") {
	Profile profile;

	SUBCASE("binary junk") {
		const char junk[] = { '\x00', '\x7f', '\xff', '\x01', 'Z', '\xc3', '\x28', '\n', 'q' };
		const ProfileLoadResult result = load_profile(junk, sizeof(junk), profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		// Whatever the reason, it is named, and the detail carries no raw control
		// characters into a log line.
		CHECK(result.problem != ProfileProblem::None);
		for (int i = 0; result.detail[i] != '\0'; ++i) {
			const unsigned char c = static_cast<unsigned char>(result.detail[i]);
			CHECK(c >= 0x20);
			CHECK(c != 0x7F);
		}
	}
	SUBCASE("empty") {
		const ProfileLoadResult result = load_profile("", 0, profile);
		CHECK(result.problem == ProfileProblem::NoVersionLine);
	}
	SUBCASE("nothing but comments") {
		const char text[] = "# a save\n# with nothing in it\n";
		CHECK(load_profile(text, -1, profile).problem == ProfileProblem::NoVersionLine);
	}
	SUBCASE("a different file entirely, that happens to be text") {
		// The preset format, handed to the profile loader. It has no version line.
		const char text[] = "format 1\nname hairpin-v3\nsteer_gamma = 2.400000\n";
		CHECK(load_profile(text, -1, profile).problem == ProfileProblem::VersionNotFirst);
	}
	SUBCASE("prose") {
		const char text[] = "Dear diary, today I went karting and it was quite good.\n";
		CHECK(load_profile(text, -1, profile).status == ProfileLoadStatus::Corrupt);
	}
}

TEST_CASE("a missing or malformed version line is corruption, named") {
	Profile profile;

	SUBCASE("no version line, everything else intact") {
		const std::string text = replace_first(corpus_v1(), "version 1\n", "");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		// The first record is now `driver_name`, so the failure is that the version
		// is not where a version has to be rather than that it is absent. Both are
		// refusals and both name the offending key.
		CHECK(result.problem == ProfileProblem::VersionNotFirst);
		CHECK(std::strcmp(result.detail, "driver_name") == 0);
	}
	SUBCASE("version below the records it describes") {
		std::string text = replace_first(corpus_v1(), "version 1\n", "");
		text += "version 1\n";
		CHECK(load(text, profile).problem == ProfileProblem::VersionNotFirst);
	}
	SUBCASE("not a number") {
		const std::string text = replace_first(corpus_v1(), "version 1", "version one");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.problem == ProfileProblem::BadVersionNumber);
		CHECK(std::strcmp(result.detail, "one") == 0);
	}
	SUBCASE("zero and negative are not versions") {
		CHECK(load(replace_first(corpus_v1(), "version 1", "version 0"), profile).problem ==
				ProfileProblem::BadVersionNumber);
		CHECK(load(replace_first(corpus_v1(), "version 1", "version -1"), profile).problem ==
				ProfileProblem::BadVersionNumber);
	}
	SUBCASE("two version lines") {
		const std::string text = replace_first(corpus_v1(), "version 1\n", "version 1\nversion 1\n");
		CHECK(load(text, profile).problem == ProfileProblem::DuplicateField);
	}
}

TEST_CASE("a save from a newer build is refused without being touched") {
	// ADR-0042's "never fails to load because of its age" is about *old* files. A
	// file from the future is not old, and the two need opposite handling: a v9
	// save reported as `Corrupt` would be renamed to `profile.save.corrupt.1` by a
	// caller following the corruption rule, which is a player downgrading once and
	// finding their intact season filed under corruption.
	const std::string text = replace_first(corpus_v1(), "version 1", "version 9");
	Profile profile = fresh_profile();
	const Profile before = profile;
	const ProfileLoadResult result = load(text, profile);

	CHECK(result.status == ProfileLoadStatus::FutureVersion);
	CHECK(result.problem == ProfileProblem::FutureVersion);
	CHECK(result.declared_version == 9);
	CHECK(std::strcmp(profile_load_status_name(result.status), "future_version") == 0);
	// The caller's profile is untouched, so "refused" cannot be confused with
	// "loaded something".
	CHECK(std::strcmp(profile.driver.name, before.driver.name) == 0);
	CHECK(profile.best_count == before.best_count);
	// And it is emphatically not corruption, which is what a caller branches on.
	CHECK(result.status != ProfileLoadStatus::Corrupt);
}

TEST_CASE("a renamed field does not silently default: ADR-0042's own example") {
	Profile profile;

	SUBCASE("standings renamed away is an unknown key, not an empty career") {
		// The exact failure the ADR describes. Best-effort loading would report
		// success with `standings_count == 0` and nothing for a player to point at.
		const std::string text = replace_all(corpus_v1(), "standing ", "championship ");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		CHECK(result.problem == ProfileProblem::UnknownKey);
		CHECK(std::strcmp(result.detail, "championship") == 0);
		// The line is named, so the message can point at the file.
		CHECK(result.line > 0);
	}
	SUBCASE("a required scalar renamed away is a missing field, not a zero") {
		const std::string text = replace_first(corpus_v1(), "career_round", "career_rounds");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		CHECK(result.problem == ProfileProblem::UnknownKey);
		CHECK(std::strcmp(result.detail, "career_rounds") == 0);
	}
	SUBCASE("a required scalar simply deleted is a missing field") {
		const std::string text = replace_first(corpus_v1(), "career_round 2\n", "");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.status == ProfileLoadStatus::Corrupt);
		CHECK(result.problem == ProfileProblem::MissingField);
		CHECK(std::strcmp(result.detail, "career_round") == 0);
	}
	SUBCASE("an added field nobody declared is an unknown key") {
		const std::string text =
				replace_first(corpus_v1(), "career_round 2\n", "career_round 2\ndriver_helmet red\n");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.problem == ProfileProblem::UnknownKey);
		CHECK(std::strcmp(result.detail, "driver_helmet") == 0);
	}
	SUBCASE("an empty repeated block is legal, because absent is not missing") {
		// A career that has run no rounds has no standings and a driver who has set
		// no lap has no bests. Neither is a missing field.
		std::string text = corpus_v1();
		const size_t at = text.find("\nstanding turney_joe");
		REQUIRE(at != std::string::npos);
		text = text.substr(0, at) + "\n";
		Profile trimmed;
		REQUIRE(load(text, trimmed).ok());
		CHECK(trimmed.career.standings_count == 0);
		CHECK(trimmed.best_count == 0);
	}
}

TEST_CASE("a malformed record is refused with the field named") {
	Profile profile;

	SUBCASE("a best with four fields") {
		const std::string text = replace_first(corpus_v1(),
				"best brackwater forward ok 62.330500 bw_fwd_ok_0002",
				"best brackwater forward ok 62.330500");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.problem == ProfileProblem::WrongTokenCount);
		CHECK(std::strcmp(result.detail, "best") == 0);
	}
	SUBCASE("a best with six fields") {
		const std::string text = replace_first(corpus_v1(),
				"best brackwater forward ok 62.330500 bw_fwd_ok_0002",
				"best brackwater forward ok 62.330500 bw_fwd_ok_0002 extra");
		CHECK(load(text, profile).problem == ProfileProblem::WrongTokenCount);
	}
	SUBCASE("a class that names nothing") {
		const std::string text = replace_first(corpus_v1(), "career_class ok", "career_class kz");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.problem == ProfileProblem::BadEnum);
		CHECK(std::strcmp(result.detail, "kz") == 0);
	}
	SUBCASE("a layout that names nothing") {
		const std::string text = replace_first(corpus_v1(), "autumn_ridge reverse ok",
				"autumn_ridge backwards ok");
		CHECK(load(text, profile).problem == ProfileProblem::BadEnum);
	}
	SUBCASE("a lap time that is not a number") {
		const std::string text = replace_first(corpus_v1(), "48.412355", "48.41s");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.problem == ProfileProblem::BadValue);
		CHECK(std::strcmp(result.detail, "48.41s") == 0);
	}
	SUBCASE("a decimal comma, which is what a locale-dependent writer produces") {
		// The failure that would appear on somebody else's machine and nowhere near
		// here. It is refused, loudly, rather than read as 48.
		const std::string text = replace_first(corpus_v1(), "48.412355", "48,412355");
		CHECK(load(text, profile).problem == ProfileProblem::BadValue);
	}
	SUBCASE("a lowercase nationality") {
		const std::string text = replace_first(corpus_v1(), "driver_nationality GBR",
				"driver_nationality gbr");
		CHECK(load(text, profile).problem == ProfileProblem::BadIdentifier);
	}
	SUBCASE("an empty driver name") {
		const std::string text = replace_first(corpus_v1(), "driver_name Skirving, Anthony",
				"driver_name");
		CHECK(load(text, profile).problem == ProfileProblem::WrongTokenCount);
	}
	SUBCASE("two standings for one driver") {
		const std::string text = replace_first(corpus_v1(), "standing turney_joe 88.000000\n",
				"standing turney_joe 88.000000\nstanding turney_joe 12.000000\n");
		CHECK(load(text, profile).problem == ProfileProblem::BadValue);
	}
	SUBCASE("two bests for one track, layout and class") {
		const std::string text = replace_first(corpus_v1(),
				"best autumn_ridge forward ok 48.412355 ar_fwd_ok_0001\n",
				"best autumn_ridge forward ok 48.412355 ar_fwd_ok_0001\n"
				"best autumn_ridge forward ok 51.000000 ar_fwd_ok_0009\n");
		const ProfileLoadResult result = load(text, profile);
		// Not "the slower one is ignored". A file with two rows for one key is a file
		// whose author disagreed with itself and it says so.
		CHECK(result.problem == ProfileProblem::DuplicateField);
	}
	SUBCASE("a duplicated scalar") {
		const std::string text = replace_first(corpus_v1(), "driver_number 101\n",
				"driver_number 101\ndriver_number 102\n");
		const ProfileLoadResult result = load(text, profile);
		CHECK(result.problem == ProfileProblem::DuplicateField);
		CHECK(std::strcmp(result.detail, "driver_number") == 0);
	}
	SUBCASE("a race number outside the real blocks") {
		const std::string text = replace_first(corpus_v1(), "driver_number 101", "driver_number 4000");
		CHECK(load(text, profile).problem == ProfileProblem::Invalid);
	}
}

TEST_CASE("a ghost id cannot escape the ghosts directory") {
	// A ghost id is pasted into `user://ghosts/<id>.ghost` and a save file is
	// user-writable, so the character set is a boundary and not a style rule. `.`
	// and `/` are the two exclusions that matter.
	Profile profile;
	const char *attacks[] = {
		"..", "../..", "a/b", "a.ghost", "a\\b", "a b", "A", "-a", "",
	};
	for (const char *attack : attacks) {
		CAPTURE(attack);
		CHECK_FALSE(profile_is_slug(attack, static_cast<int>(std::strlen(attack))));
	}
	const std::string text = replace_first(corpus_v1(), "ar_fwd_ok_0001", "../../../etc/passwd");
	const ProfileLoadResult result = load(text, profile);
	CHECK(result.status == ProfileLoadStatus::Corrupt);
	// Reported as a wrong token count rather than a bad identifier because the path
	// contains no space but the substitution made the record's own shape wrong; what
	// matters is that it is refused. Check the identifier rule directly too.
	CHECK(result.problem != ProfileProblem::None);
	CHECK_FALSE(profile_is_slug("../../../etc/passwd", 19));

	// And a legal one, so the rule is not simply "reject everything".
	CHECK(profile_is_slug("ar_fwd_kz2_0003", 15));
	CHECK(profile_is_slug("a", 1));
	CHECK(profile_is_slug("0", 1));
}

// --- the migration machinery ---------------------------------------------------

TEST_CASE("there are no migrations at version 1, and the chain says so honestly") {
	CHECK(PROFILE_FORMAT_VERSION == 1);
	CHECK(PROFILE_MIGRATION_COUNT == 0);
	CHECK(PROFILE_MIGRATIONS == nullptr);
	// An empty table is a complete chain from 1 to 1: there is no intermediate
	// version to step out of. This is the assertion that would start failing the
	// moment `PROFILE_FORMAT_VERSION` is bumped without an entry being added, which
	// is exactly the mistake worth catching.
	CHECK(profile_migration_chain_is_complete(PROFILE_MIGRATIONS, PROFILE_MIGRATION_COUNT,
			PROFILE_FORMAT_VERSION));
}

TEST_CASE("the chain completeness check catches every way a table can be wrong") {
	bool (*ok)(ProfileDocument &) = profile_migrate_identity;

	SUBCASE("a well-formed three-version chain") {
		const ProfileMigration table[] = { { 1, 2, ok, "a" }, { 2, 3, ok, "b" } };
		CHECK(profile_migration_chain_is_complete(table, 2, 3));
	}
	SUBCASE("declared out of order is still complete") {
		// The runner searches rather than indexing, so order in the table is not
		// part of the contract, and pinning that means a reordering is not a bug.
		const ProfileMigration table[] = { { 2, 3, ok, "b" }, { 1, 2, ok, "a" } };
		CHECK(profile_migration_chain_is_complete(table, 2, 3));
	}
	SUBCASE("a gap") {
		const ProfileMigration table[] = { { 1, 2, ok, "a" } };
		CHECK_FALSE(profile_migration_chain_is_complete(table, 1, 3));
	}
	SUBCASE("two steps out of one version") {
		// Compiles perfectly and picks one of them by position. The reason this
		// function exists.
		const ProfileMigration table[] = { { 1, 2, ok, "a" }, { 1, 2, ok, "also a" },
			{ 2, 3, ok, "b" } };
		CHECK_FALSE(profile_migration_chain_is_complete(table, 3, 3));
	}
	SUBCASE("a step that jumps two versions") {
		const ProfileMigration table[] = { { 1, 3, ok, "a" } };
		CHECK_FALSE(profile_migration_chain_is_complete(table, 1, 3));
	}
	SUBCASE("a step with no function") {
		const ProfileMigration table[] = { { 1, 2, nullptr, "a" } };
		CHECK_FALSE(profile_migration_chain_is_complete(table, 1, 2));
	}
	SUBCASE("a step out of version zero") {
		const ProfileMigration table[] = { { 0, 1, ok, "a" }, { 1, 2, ok, "b" } };
		CHECK_FALSE(profile_migration_chain_is_complete(table, 2, 2));
	}
}

TEST_CASE("a real v1 file walks a synthetic two-step chain in order") {
	// The mechanism, exercised on the real captured file. Versions 2 and 3 are
	// fictional and live only here; no fake file goes in the corpus.
	migration_1_calls = 0;
	migration_2_calls = 0;

	const std::string text = corpus_v1();
	Profile profile;
	const ProfileLoadResult result = load_profile_with(text.data(),
			static_cast<int>(text.size()), profile, TWO_STEP_CHAIN, TWO_STEP_COUNT, 3);

	CAPTURE(profile_problem_name(result.problem));
	REQUIRE(result.ok());
	CHECK(result.declared_version == 1);
	CHECK(result.migrations_applied == 2);
	// Each step ran exactly once, and in order: step 2 overwrote a field step 1
	// did not touch, and step 1's change survived.
	CHECK(migration_1_calls == 1);
	CHECK(migration_2_calls == 1);
	CHECK(std::strcmp(profile.driver.livery, "migrated_one") == 0);
	CHECK(profile.driver.number == 222);
	// Everything the chain did not touch came through unchanged.
	CHECK(std::strcmp(profile.driver.name, "Skirving, Anthony") == 0);
	CHECK(profile.career.standings_count == 8);
	CHECK(profile.best_count == 4);
}

TEST_CASE("a broken migration is caught by the binder rather than shipping a blank career") {
	// A migration that renames `standing` to something the current schema does not
	// know. Under best-effort loading this is ADR-0042's silent data loss; here it
	// is a refusal that names the key.
	const ProfileMigration table[] = { { 1, 2, migrate_renames_standings_away, "bad rename" } };
	const std::string text = corpus_v1();
	Profile profile;
	const ProfileLoadResult result =
			load_profile_with(text.data(), static_cast<int>(text.size()), profile, table, 1, 2);

	CHECK(result.status == ProfileLoadStatus::Corrupt);
	CHECK(result.problem == ProfileProblem::UnknownKey);
	CHECK(std::strcmp(result.detail, "championship") == 0);
	// The migration ran and reported success; the binder is what caught it. That
	// division is the point: a migration cannot be trusted to know what the current
	// schema requires, and the schema table is the one place that does.
	CHECK(result.migrations_applied == 1);
}

TEST_CASE("a gap in the chain is refused rather than skipped") {
	const ProfileMigration table[] = { { 2, 3, profile_migrate_identity, "b" } };
	const std::string text = corpus_v1();
	Profile profile;
	const ProfileLoadResult result =
			load_profile_with(text.data(), static_cast<int>(text.size()), profile, table, 1, 3);
	CHECK(result.status == ProfileLoadStatus::Corrupt);
	CHECK(result.problem == ProfileProblem::MigrationGap);
	CHECK(result.migrations_applied == 0);
}

TEST_CASE("a migration that fails is reported with its summary") {
	const ProfileMigration table[] = { { 1, 2, migrate_fails, "test: the step that fails" } };
	const std::string text = corpus_v1();
	Profile profile;
	const ProfileLoadResult result =
			load_profile_with(text.data(), static_cast<int>(text.size()), profile, table, 1, 2);
	CHECK(result.problem == ProfileProblem::MigrationFailed);
	CHECK(std::strcmp(result.detail, "test: the step that fails") == 0);
}

TEST_CASE("the document helpers are what make a migration one line") {
	const std::string text = corpus_v1();
	ProfileDocument document;
	ProfileLoadResult result;
	REQUIRE(profile_lex(text.data(), static_cast<int>(text.size()), document, result));

	CHECK(document.declared_version == 1);
	CHECK(document.count_of("version") == 1);
	CHECK(document.count_of("standing") == 8);
	CHECK(document.count_of("best") == 4);
	CHECK(document.count_of("nothing_like_this") == 0);
	// Comments and blank lines are not records, so line numbers survive them.
	const ProfileRecord *version = document.find("version");
	REQUIRE(version != nullptr);
	CHECK(version->line == 4);
	CHECK(std::strcmp(version->text, "1") == 0);

	const ProfileRecord *third = document.find("standing", 2);
	REQUIRE(third != nullptr);
	CHECK(std::strcmp(third->text, "cuman_nicolo 71.000000") == 0);
	CHECK(document.find("standing", 8) == nullptr);

	SUBCASE("rename reports how many it moved") {
		CHECK(document.rename_key("standing", "championship") == 8);
		CHECK(document.count_of("standing") == 0);
		CHECK(document.count_of("championship") == 8);
		CHECK(document.rename_key("no_such_key", "whatever") == 0);
	}
	SUBCASE("remove preserves the order of the rest") {
		const int before = document.record_count;
		CHECK(document.remove_key("standing") == 8);
		CHECK(document.record_count == before - 8);
		CHECK(std::strcmp(document.record[0].key, "version") == 0);
		CHECK(std::strcmp(document.record[1].key, "driver_name") == 0);
		CHECK(std::strcmp(document.record[8].key, "best") == 0);
	}
	SUBCASE("append and set_text") {
		CHECK(document.append("driver_helmet", "red"));
		CHECK(document.count_of("driver_helmet") == 1);
		CHECK(document.set_text("driver_helmet", "blue"));
		CHECK(std::strcmp(document.find("driver_helmet")->text, "blue") == 0);
		// set_text on an absent key appends, which is what "add a field with a
		// default" needs to be a one-liner.
		CHECK(document.set_text("driver_gloves", "black"));
		CHECK(std::strcmp(document.find("driver_gloves")->text, "black") == 0);
	}
}

// --- the corruption and atomic-write policies as logic ------------------------

TEST_CASE("the moved-aside name finds the next free index") {
	char name[64];

	REQUIRE(profile_corrupt_name(PROFILE_FILE_NAME, 1, name, sizeof(name)) > 0);
	CHECK(std::strcmp(name, "profile.save.corrupt.1") == 0);
	REQUIRE(profile_corrupt_name(PROFILE_FILE_NAME, 42, name, sizeof(name)) > 0);
	CHECK(std::strcmp(name, "profile.save.corrupt.42") == 0);
	// Index 0 is not used: `.corrupt.0` reads as "no corruptions" to everybody who
	// sees it.
	CHECK(profile_corrupt_name(PROFILE_FILE_NAME, 0, name, sizeof(name)) == -1);
	CHECK(profile_corrupt_name(PROFILE_FILE_NAME, -1, name, sizeof(name)) == -1);
	// It refuses rather than truncating, like every other writer in this project.
	char tiny[8];
	CHECK(profile_corrupt_name(PROFILE_FILE_NAME, 1, tiny, sizeof(tiny)) == -1);

	SUBCASE("reading an index back out of a directory listing") {
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.1") == 1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.42") == 42);
		// Not ours, so not counted. A listing that reported a highest index no file
		// actually holds would leave a gap, and the next corruption would overwrite
		// a real file.
		CHECK(profile_corrupt_index_of("profile.save", "profile.save") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.tmp") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.007") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.0") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.+3") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "profile.save.corrupt.3x") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "other.save.corrupt.3") == -1);
		CHECK(profile_corrupt_index_of("profile.save", "") == -1);
		// A second profile is a path change and not a schema change, so the helpers
		// take the base name rather than assuming it.
		CHECK(profile_corrupt_index_of("profile2.save", "profile2.save.corrupt.5") == 5);
	}
	SUBCASE("the round trip, which is how the Godot side uses both") {
		int highest = 0;
		for (int index = 1; index <= 12; ++index) {
			REQUIRE(profile_corrupt_name(PROFILE_FILE_NAME, index, name, sizeof(name)) > 0);
			const int read_back = profile_corrupt_index_of(PROFILE_FILE_NAME, name);
			CHECK(read_back == index);
			if (read_back > highest) {
				highest = read_back;
			}
		}
		CHECK(profile_next_corrupt_index(highest) == 13);
		CHECK(profile_next_corrupt_index(0) == 1);
		CHECK(profile_next_corrupt_index(-5) == 1);
	}
}

TEST_CASE("the temporary name is the target plus a suffix") {
	char name[64];
	REQUIRE(profile_temp_name(PROFILE_FILE_NAME, name, sizeof(name)) > 0);
	CHECK(std::strcmp(name, "profile.save.tmp") == 0);
	// It must not itself look like a moved-aside file, or a directory scan would
	// count an in-progress write as a corruption.
	CHECK(profile_corrupt_index_of(PROFILE_FILE_NAME, name) == -1);
	char tiny[4];
	CHECK(profile_temp_name(PROFILE_FILE_NAME, tiny, sizeof(tiny)) == -1);
	CHECK(profile_temp_name(nullptr, name, sizeof(name)) == -1);
}

TEST_CASE("settings are not in the profile, and the separation is stated") {
	// ADR-0042: if `profile.save` is unreadable a player still has to reach the
	// menu, read the text and use the pad. This is the mechanical half of that —
	// the file is named here and modeled nowhere, so a `bool invert_look` cannot be
	// added to `Profile` without somebody deleting this test.
	CHECK(std::strcmp(PROFILE_SETTINGS_FILE_NAME, "settings.cfg") == 0);
	// Two files, and the loader that fails on one has not been handed the other.
	CHECK(std::strcmp(PROFILE_SETTINGS_FILE_NAME, PROFILE_FILE_NAME) != 0);
	// `Profile` has four parts and none of them is settings. A size check is a
	// crude proxy; the schema table is the real statement, so assert against that.
	for (int i = 0; i < PROFILE_KEY_COUNT; ++i) {
		const char *key = PROFILE_KEYS[i].key;
		CHECK(std::strstr(key, "setting") == nullptr);
		CHECK(std::strstr(key, "control") == nullptr);
		CHECK(std::strstr(key, "volume") == nullptr);
		CHECK(std::strstr(key, "assist") == nullptr);
	}
}

// --- the struct and its invariants --------------------------------------------

TEST_CASE("a fresh profile is valid, writes, and reads back") {
	// The fallback after a corruption. A fresh profile that did not satisfy
	// `is_valid` would mean the recovery path writes a file its own loader rejects,
	// and the second launch would report corruption for a file the game wrote.
	const Profile fresh = fresh_profile();
	REQUIRE(fresh.is_valid());
	// `GAMEDESIGN.md` §5's ladder starts in the direct-drive class, not in the one
	// this repository can already simulate.
	CHECK(fresh.career.kart_class == KartClass::OK);
	CHECK(fresh.career.season == 0);
	CHECK(fresh.career.round == 0);
	CHECK(fresh.career.standings_count == 0);
	CHECK(fresh.best_count == 0);

	const std::string text = write_profile(fresh);
	REQUIRE(!text.empty());
	Profile loaded;
	const ProfileLoadResult result = load(text, loaded);
	CAPTURE(profile_problem_name(result.problem));
	REQUIRE(result.ok());
	CHECK(write_profile(loaded) == text);
}

TEST_CASE("the writer refuses an invalid profile rather than emitting one") {
	// A writer that emitted an invalid profile would produce a file its own loader
	// rejects, and the bug would surface as corruption on the next launch rather
	// than here.
	char buffer[PROFILE_TEXT_CAP];
	Profile empty;
	CHECK(format_profile(empty, buffer, sizeof(buffer)) == -1);

	Profile profile = fresh_profile();
	REQUIRE(format_profile(profile, buffer, sizeof(buffer)) > 0);
	profile.driver.number = 0;
	CHECK(format_profile(profile, buffer, sizeof(buffer)) == -1);
	profile.driver.number = 101;
	profile.career.season = PROFILE_MAX_SEASONS;
	CHECK(format_profile(profile, buffer, sizeof(buffer)) == -1);

	// And it refuses a buffer it does not fit rather than truncating.
	profile = fresh_profile();
	char tiny[16];
	CHECK(format_profile(profile, tiny, sizeof(tiny)) == -1);
}

TEST_CASE("set_driver_name sanitizes rather than losing the save") {
	// The same call `tuning.h`'s preset writer makes about a multi-line name: a
	// caller passing something odd has made a typo, not an attack, and refusing
	// would cost more than repairing.
	Profile profile;
	CHECK_FALSE(profile.set_driver_name("Skirving\nAnthony"));
	CHECK(std::strcmp(profile.driver.name, "Skirving Anthony") == 0);
	CHECK_FALSE(profile.set_driver_name("  Turney, Joe  "));
	CHECK(std::strcmp(profile.driver.name, "Turney, Joe") == 0);
	// A `#` would turn the rest of the line into a comment and the name would come
	// back short. Replaced on the way in, so the round trip is exact.
	CHECK_FALSE(profile.set_driver_name("Turney #1"));
	CHECK(std::strcmp(profile.driver.name, "Turney 1") == 0);
	CHECK(profile.set_driver_name("Turney, Joe"));
	CHECK_FALSE(profile.set_driver_name(nullptr));
	CHECK(profile.driver.name[0] == '\0');
	CHECK_FALSE(profile.set_driver_name(""));

	// Non-ASCII passes through as opaque bytes. A driver called Nicolo Cuman
	// spells it with an accent and the format has no business refusing that; the
	// ASCII-only rule applies to this project's string literals, not to its data.
	const char accented[] = "Cuman, Nicol\xc3\xb2";
	CHECK(profile.set_driver_name(accented));
	CHECK(std::strcmp(profile.driver.name, accented) == 0);
	profile.driver.number = 303;
	profile.set_nationality("ITA");
	profile.set_livery("forza_red");
	REQUIRE(profile.is_valid());
	const std::string text = write_profile(profile);
	REQUIRE(!text.empty());
	Profile loaded;
	REQUIRE(load(text, loaded).ok());
	CHECK(std::strcmp(loaded.driver.name, accented) == 0);

	// Too long truncates and says so.
	char oversized[PROFILE_NAME_CHARS + 16];
	for (int i = 0; i < PROFILE_NAME_CHARS + 15; ++i) {
		oversized[i] = 'a';
	}
	oversized[PROFILE_NAME_CHARS + 15] = '\0';
	CHECK_FALSE(profile.set_driver_name(oversized));
	CHECK(static_cast<int>(std::strlen(profile.driver.name)) == PROFILE_NAME_CHARS - 1);
	CHECK(profile.is_valid());
}

TEST_CASE("nationality and livery are rejected rather than repaired") {
	// There is no defensible repair for `gbr` or for `../evil`, and a UI picks
	// these from a list rather than accepting free text.
	Profile profile;
	CHECK(profile.set_nationality("GBR"));
	CHECK_FALSE(profile.set_nationality("gbr"));
	CHECK(profile.driver.nationality[0] == '\0');
	CHECK_FALSE(profile.set_nationality("GB"));
	CHECK_FALSE(profile.set_nationality("GBRA"));
	CHECK_FALSE(profile.set_nationality(nullptr));

	CHECK(profile.set_livery("works_blue"));
	CHECK_FALSE(profile.set_livery("Works Blue"));
	CHECK_FALSE(profile.set_livery("../../evil"));
	CHECK(profile.driver.livery[0] == '\0');
}

TEST_CASE("bests are kept sorted, deduplicated, and only improve") {
	Profile profile = fresh_profile();

	// Inserted out of order on purpose. Sorting on the way in is what makes two
	// profiles with the same bests byte-identical whatever order they were set in.
	REQUIRE(profile.set_best("brackwater", TrackLayout::Forward, KartClass::OK, 62.5, "g1"));
	REQUIRE(profile.set_best("autumn_ridge", TrackLayout::Reverse, KartClass::OK, 49.9, "g2"));
	REQUIRE(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::KZ2, 44.1, "g3"));
	REQUIRE(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, 48.4, "g4"));
	REQUIRE(profile.best_count == 4);
	CHECK(std::strcmp(profile.bests[0].track_id, "autumn_ridge") == 0);
	CHECK(profile.bests[0].layout == TrackLayout::Forward);
	CHECK(profile.bests[0].kart_class == KartClass::OK);
	CHECK(profile.bests[1].kart_class == KartClass::KZ2);
	CHECK(profile.bests[2].layout == TrackLayout::Reverse);
	CHECK(std::strcmp(profile.bests[3].track_id, "brackwater") == 0);
	CHECK(profile.is_valid());

	SUBCASE("a slower lap is not a best, and nothing failed") {
		CHECK(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, 51.0, "g9"));
		CHECK(profile.best_count == 4);
		CHECK(tuning_micro(profile.bests[0].lap_time_s) == tuning_micro(48.4));
		CHECK(std::strcmp(profile.bests[0].ghost_id, "g4") == 0);
	}
	SUBCASE("a faster lap replaces the time and the ghost together") {
		CHECK(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, 47.9, "g9"));
		CHECK(profile.best_count == 4);
		CHECK(tuning_micro(profile.bests[0].lap_time_s) == tuning_micro(47.9));
		// The ghost has to move with the time. A best lap pointing at the ghost of a
		// slower one is a ghost that gets beaten by the number beside it.
		CHECK(std::strcmp(profile.bests[0].ghost_id, "g9") == 0);
	}
	SUBCASE("the four keys are four separate bests") {
		// The reason `KartClass` and `TrackLayout` are both in the key: without
		// either, a shifter lap and a single-speed lap on the same circuit compete.
		CHECK(profile.find_best("autumn_ridge", TrackLayout::Forward, KartClass::OK) !=
				profile.find_best("autumn_ridge", TrackLayout::Forward, KartClass::KZ2));
		CHECK(profile.find_best("autumn_ridge", TrackLayout::Forward, KartClass::OK) !=
				profile.find_best("autumn_ridge", TrackLayout::Reverse, KartClass::OK));
	}
	SUBCASE("a bad key or a bad time is refused") {
		CHECK_FALSE(profile.set_best("Autumn Ridge", TrackLayout::Forward, KartClass::OK, 48.0, "g"));
		CHECK_FALSE(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, 0.0, "g"));
		CHECK_FALSE(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, -1.0, "g"));
		CHECK_FALSE(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, 48.0, ""));
		CHECK_FALSE(profile.set_best("", TrackLayout::Forward, KartClass::OK, 48.0, "g"));
		CHECK(profile.best_count == 4);
	}
	SUBCASE("the array fills up and refuses rather than overflowing") {
		Profile full = fresh_profile();
		char track[8];
		for (int i = 0; i < PROFILE_MAX_BESTS; ++i) {
			// `t00`..`t31`, which sorts the same way as the numeric order.
			track[0] = 't';
			track[1] = static_cast<char>('0' + i / 10);
			track[2] = static_cast<char>('0' + i % 10);
			track[3] = '\0';
			REQUIRE(full.set_best(track, TrackLayout::Forward, KartClass::OK, 48.0 + i, "g"));
		}
		CHECK(full.best_count == PROFILE_MAX_BESTS);
		CHECK(full.is_valid());
		CHECK_FALSE(full.set_best("zzz", TrackLayout::Forward, KartClass::OK, 48.0, "g"));
		CHECK(full.best_count == PROFILE_MAX_BESTS);
		// A full profile still fits its own buffer, which is what `PROFILE_TEXT_CAP`
		// promises a caller.
		for (int i = 0; i < PROFILE_MAX_STANDINGS; ++i) {
			char id[8] = { 'd', static_cast<char>('0' + i / 10),
				static_cast<char>('0' + i % 10), '\0', 0, 0, 0, 0 };
			REQUIRE(full.add_standing(id, 25.0));
		}
		const std::string text = write_profile(full);
		REQUIRE(!text.empty());
		CHECK(static_cast<int>(text.size()) < PROFILE_TEXT_CAP);
		Profile loaded;
		REQUIRE(load(text, loaded).ok());
		CHECK(write_profile(loaded) == text);
	}
}

TEST_CASE("standings reject a duplicate driver and keep their order") {
	Profile profile = fresh_profile();
	REQUIRE(profile.add_standing("turney_joe", 88.0));
	REQUIRE(profile.add_standing("cuman_nicolo", 71.0));
	CHECK_FALSE(profile.add_standing("turney_joe", 12.0));
	CHECK(profile.career.standings_count == 2);
	// Order is the classification, so it is preserved rather than sorted.
	CHECK(std::strcmp(profile.career.standings[0].driver_id, "turney_joe") == 0);
	CHECK(std::strcmp(profile.career.standings[1].driver_id, "cuman_nicolo") == 0);

	// Half points are real: ADR-0043 scales a classification to half under 75% of
	// the scheduled distance, and half of 25 is 12.5.
	REQUIRE(profile.add_standing("vidales_david", 12.5));
	CHECK(tuning_micro(profile.find_standing("vidales_david")->points) == tuning_micro(12.5));
	CHECK(profile.find_standing("nobody") == nullptr);

	CHECK_FALSE(profile.add_standing("turney joe", 1.0));
	CHECK_FALSE(profile.add_standing("", 1.0));
	CHECK_FALSE(profile.add_standing("ok_driver", -1.0));

	const std::string text = write_profile(profile);
	REQUIRE(!text.empty());
	Profile loaded;
	REQUIRE(load(text, loaded).ok());
	CHECK(loaded.career.standings_count == 3);
	CHECK(tuning_micro(loaded.find_standing("vidales_david")->points) == tuning_micro(12.5));
	CHECK(write_profile(loaded) == text);
}

// --- the vocabulary ------------------------------------------------------------

TEST_CASE("the schema table is internally consistent") {
	for (int i = 0; i < PROFILE_KEY_COUNT; ++i) {
		const ProfileKeySpec &spec = PROFILE_KEYS[i];
		CAPTURE(spec.key);
		CHECK(spec.key != nullptr);
		CHECK(std::strlen(spec.key) > 0);
		CHECK(std::strlen(spec.key) < static_cast<size_t>(PROFILE_KEY_CHARS));
		CHECK(spec.token_count >= -1);
		CHECK(spec.token_count <= PROFILE_MAX_TOKENS);
		CHECK(spec.token_count != 0);
		// A repeated key that is also required would mean "at least one", which no
		// block in this format wants: an empty career has no standings.
		const bool repeated_and_required = spec.repeated && spec.required;
		CHECK_FALSE(repeated_and_required);
		CHECK(profile_key_spec(spec.key, static_cast<int>(std::strlen(spec.key))) == &spec);
		for (int j = i + 1; j < PROFILE_KEY_COUNT; ++j) {
			CHECK(std::strcmp(spec.key, PROFILE_KEYS[j].key) != 0);
		}
	}
	CHECK(profile_key_spec("no_such_key", 11) == nullptr);
	// A prefix must not match, for the same reason `tunable_by_key` pins it: a
	// loader that confused `best` and `bests` would write the wrong field.
	CHECK(profile_key_spec("bes", 3) == nullptr);
	CHECK(profile_key_spec("bests", 5) == nullptr);
	CHECK(profile_key_spec("best", 4) != nullptr);

	// The writer emits every declared key, so a key added to the table and
	// forgotten in `format_profile` is caught here rather than as a load failure
	// after the next save.
	Profile profile = fresh_profile();
	REQUIRE(profile.add_standing("turney_joe", 25.0));
	REQUIRE(profile.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, 48.0, "g1"));
	const std::string text = write_profile(profile);
	REQUIRE(!text.empty());
	for (int i = 0; i < PROFILE_KEY_COUNT; ++i) {
		CAPTURE(PROFILE_KEYS[i].key);
		const std::string needle = std::string("\n") + PROFILE_KEYS[i].key + " ";
		CHECK(text.find(needle) != std::string::npos);
	}
}

TEST_CASE("every status and problem name is distinct, so a message names one thing") {
	// The pairwise check ADR-0043 asks for on the points scales, applied to the
	// diagnostic vocabulary: a copy-paste in one of these switches produces working
	// code and a message that blames the wrong thing.
	const ProfileProblem problems[] = {
		ProfileProblem::None, ProfileProblem::NoVersionLine, ProfileProblem::VersionNotFirst,
		ProfileProblem::BadVersionNumber, ProfileProblem::FutureVersion,
		ProfileProblem::MigrationGap, ProfileProblem::MigrationFailed,
		ProfileProblem::UnknownKey, ProfileProblem::MissingField,
		ProfileProblem::DuplicateField, ProfileProblem::WrongTokenCount,
		ProfileProblem::BadValue, ProfileProblem::BadEnum, ProfileProblem::BadIdentifier,
		ProfileProblem::TooLong, ProfileProblem::TooManyRecords, ProfileProblem::Invalid,
	};
	const int count = static_cast<int>(sizeof(problems) / sizeof(problems[0]));
	for (int i = 0; i < count; ++i) {
		const char *name = profile_problem_name(problems[i]);
		CAPTURE(name);
		CHECK(std::strcmp(name, "invalid_problem") != 0);
		for (int j = i + 1; j < count; ++j) {
			CHECK(std::strcmp(name, profile_problem_name(problems[j])) != 0);
		}
	}
	CHECK(std::strcmp(profile_problem_name(static_cast<ProfileProblem>(99)),
				  "invalid_problem") == 0);

	const ProfileLoadStatus statuses[] = { ProfileLoadStatus::Ok, ProfileLoadStatus::Corrupt,
		ProfileLoadStatus::FutureVersion };
	for (int i = 0; i < 3; ++i) {
		CHECK(std::strcmp(profile_load_status_name(statuses[i]), "invalid") != 0);
		for (int j = i + 1; j < 3; ++j) {
			CHECK(std::strcmp(profile_load_status_name(statuses[i]),
						  profile_load_status_name(statuses[j])) != 0);
		}
	}
	CHECK(std::strcmp(profile_load_status_name(static_cast<ProfileLoadStatus>(9)),
				  "invalid") == 0);
}

// --- the text primitives -------------------------------------------------------

TEST_CASE("profile_format_int writes canonical integers and refuses to truncate") {
	char buffer[24];
	CHECK(format_int(0, buffer, sizeof(buffer)) == 1);
	CHECK(std::strcmp(buffer, "0") == 0);
	format_int(101, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "101") == 0);
	format_int(-7, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "-7") == 0);
	format_int(2147483647LL, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "2147483647") == 0);
	// The most negative value, which a naive `-value` would overflow.
	format_int(-9223372036854775807LL - 1, buffer, sizeof(buffer));
	CHECK(std::strcmp(buffer, "-9223372036854775808") == 0);
	char tiny[3];
	CHECK(format_int(1000, tiny, sizeof(tiny)) == -1);
}

TEST_CASE("profile_parse_int accepts what a person might type and rejects a prefix") {
	int value = -1;
	CHECK(profile_parse_int("101", 3, value));
	CHECK(value == 101);
	CHECK(profile_parse_int("-7", 2, value));
	CHECK(value == -7);
	CHECK(profile_parse_int("+2", 2, value));
	CHECK(value == 2);
	CHECK(profile_parse_int("0007", 4, value));
	CHECK(value == 7);
	CHECK(profile_parse_int("0", 1, value));
	CHECK(value == 0);

	// A prefix is not a number. `101a` parsing as 101 would be a race number nobody
	// wrote down.
	CHECK_FALSE(profile_parse_int("101a", 4, value));
	CHECK_FALSE(profile_parse_int("1 2", 3, value));
	CHECK_FALSE(profile_parse_int("1.0", 3, value));
	CHECK_FALSE(profile_parse_int("", 0, value));
	CHECK_FALSE(profile_parse_int("-", 1, value));
	CHECK_FALSE(profile_parse_int("+", 1, value));
	CHECK_FALSE(profile_parse_int(nullptr, 3, value));
	// Overflow is refused rather than wrapped.
	CHECK_FALSE(profile_parse_int("99999999999", 11, value));
}

TEST_CASE("profile_tokenize splits on runs of whitespace and caps the count") {
	ProfileTokens tokens;
	CHECK(profile_tokenize("a  b\tc", -1, tokens));
	REQUIRE(tokens.count == 3);
	CHECK(profile_text_equals(tokens.begin[0], tokens.length[0], "a"));
	CHECK(profile_text_equals(tokens.begin[2], tokens.length[2], "c"));

	CHECK(profile_tokenize("", -1, tokens));
	CHECK(tokens.count == 0);
	CHECK(profile_tokenize("   ", -1, tokens));
	CHECK(tokens.count == 0);

	CHECK(profile_tokenize("a b c d e", -1, tokens));
	CHECK(tokens.count == 5);
	// A sixth field is an error rather than a silently ignored tail.
	CHECK_FALSE(profile_tokenize("a b c d e f", -1, tokens));
}

TEST_CASE("profile_text_equals does not call a prefix a match") {
	// `strncmp` would call these equal at n = 7, which is how "forward" and
	// "forwards" become one layout.
	CHECK(profile_text_equals("forward", 7, "forward"));
	CHECK_FALSE(profile_text_equals("forward", 7, "forwards"));
	CHECK_FALSE(profile_text_equals("forwards", 8, "forward"));
	CHECK_FALSE(profile_text_equals("", 0, "forward"));
	CHECK(profile_text_equals("", 0, ""));
	CHECK_FALSE(profile_text_equals(nullptr, 0, "forward"));
}

TEST_CASE("a carriage return does not become part of a value") {
	// A save copied out of a bug report through a Windows machine. Every line ends
	// `\r\n`, and a parser that fed that to the number reader would refuse the whole
	// career.
	std::string text = corpus_v1();
	std::string crlf;
	for (char c : text) {
		if (c == '\n') {
			crlf.push_back('\r');
		}
		crlf.push_back(c);
	}
	Profile profile;
	const ProfileLoadResult result = load(crlf, profile);
	CAPTURE(profile_problem_name(result.problem));
	CAPTURE(result.detail);
	REQUIRE(result.ok());
	CHECK(profile.driver.number == 101);
	CHECK(tuning_micro(profile.bests[0].lap_time_s) == tuning_micro(48.412355));
	// And it is written back with Unix line endings, which is the canonical form.
	CHECK(write_profile(profile) == text);
}

TEST_CASE("a lap time keeps microsecond resolution through the file") {
	// The format shares `tuning.h`'s 1e-6 grid, so a lap time is exact to the
	// microsecond and there is no float that round-trips to a neighbor. At a 48
	// second lap that is eight significant figures, which is four more than a
	// timing screen shows.
	Profile profile = fresh_profile();
	const double probes[] = { 0.000001, 1.0, 48.412355, 62.999999, 3599.999999 };
	for (double probe : probes) {
		CAPTURE(probe);
		Profile one = profile;
		REQUIRE(one.set_best("autumn_ridge", TrackLayout::Forward, KartClass::OK, probe, "g1"));
		const std::string text = write_profile(one);
		REQUIRE(!text.empty());
		Profile loaded;
		REQUIRE(load(text, loaded).ok());
		const ProfileBest *best =
				loaded.find_best("autumn_ridge", TrackLayout::Forward, KartClass::OK);
		REQUIRE(best != nullptr);
		CHECK(tuning_micro(best->lap_time_s) == tuning_micro(probe));
	}
}

TEST_CASE("the sizes in this header are consistent with each other") {
	// The widest record has to fit the record buffer, or a legal save would fail to
	// lex with `TooLong` — a failure that only appears once somebody names a track
	// with a long slug.
	const int widest_best = 4 + 1 + (SESSION_ID_CHARS - 1) + 1 + 7 + 1 + 3 + 1 +
			(TUNING_VALUE_CHARS - 1) + 1 + (PROFILE_SLUG_CHARS - 1);
	CHECK(widest_best < PROFILE_TEXT_CHARS + PROFILE_KEY_CHARS);
	// The document has to hold every record a valid profile can produce.
	CHECK(PROFILE_MAX_RECORDS >= 8 + PROFILE_MAX_STANDINGS + PROFILE_MAX_BESTS);
	// The projection cannot hold more drivers than a session can hold karts.
	CHECK(PROFILE_MAX_STANDINGS == SESSION_MAX_ENTRIES);
	// A profile can never need more than the advertised buffer, checked against a
	// genuinely full one in the bests test above rather than by arithmetic here.
	CHECK(PROFILE_TEXT_CAP > 4096);
}
