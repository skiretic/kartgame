#ifndef KART_CORE_PROFILE_H
#define KART_CORE_PROFILE_H

#include "core/session.h"
#include "core/tuning.h"

#include <cmath>
#include <cstdint>

// The career save: its text format, its version, its migration chain, and the
// corruption and atomic-write policies as pure logic. ROADMAP M3c,
// `docs/DECISIONS.md` ADR-0042, `docs/GAMEDESIGN.md` §8.
//
// Nothing in src/core/ may include godot-cpp. See ADR-0017. This header is also
// allocation-free and locale-free for the reasons `tuning.h` gives: no
// `std::string`, no `std::vector`, no `snprintf` — `%f` respects the C locale and
// a decimal-comma machine would write a file this parser rejects. Every function
// writes into a caller-supplied buffer and returns the length written, or -1.
//
// **No file I/O and no `user://` resolution live here.** Nothing below opens,
// reads, renames or deletes anything. The Godot side does that, and the contract
// it has to honor is written out under "The write is atomic" and "Corruption is
// not a version problem" so that it cannot be got wrong by accident.
//
// ## Why a save is not a replay
//
// ADR-0041 refuses a replay whose `format_version` does not match, and that is
// right: a replay is a diagnostic artifact, and migrating one silently produces a
// plausible-looking run of something that never happened. ADR-0042 takes the
// **opposite** policy here, because a career save is the record of an evening
// somebody spent. Refusing to load it is deleting it with extra steps.
//
// So: **a save never fails to load because of its age.** Loading chains
// migrations, v1 -> v2 -> v3, and each bump ships with a test that eats a real
// captured file of the older version.
//
// The mirror case is not age and gets the opposite answer. A file from a *newer*
// build is reported as `FutureVersion`, the profile is left untouched, and the
// file is **not** moved aside and **not** overwritten — see the corruption rules
// below for why that distinction is load-bearing rather than pedantic.
//
// ## The format
//
//     # kartgame driver profile.
//     version 1
//     driver_name Skirving, Anthony
//     driver_number 101
//     driver_nationality GBR
//     driver_livery works_blue
//     career_class ok
//     career_season 0
//     career_round 2
//
//     standing skirving_anthony 44.000000
//     standing turney_joe 50.000000
//
//     best autumn_ridge forward ok 48.123456 ar_fwd_ok_0001
//     best autumn_ridge reverse ok 49.874000 ar_rev_ok_0001
//
// Line oriented, one record per line, `key` then the rest of the line — which is
// exactly `ParsedLine::Header`'s shape in `tuning.h`, and deliberately so. Numbers
// are written by `format_value` and read by `parse_value` from that same header,
// so a lap time and a tunable are quantized to the same 1e-6 grid and rendered by
// the same integer arithmetic. There is one number renderer in this project and
// this format does not add a second.
//
// **Where it departs from `tuning.h`'s line machinery, and why.** `parse_line`
// there resolves a line to *one* key and *one* number, and caps a header's free
// text at `TUNING_TEXT_CHARS` (96). A `best` record is five tokens and up to 106
// characters, so `parse_line` cannot lex one. The lexer below is that function's
// shape generalized from "key plus one value" to "key plus the rest of the line",
// with `profile_tokenize` splitting the rest when a record wants tokens. The
// value-level primitives are reused unchanged; only the line shape is new.
//
// ## Integers are integers
//
// `format_value` writes six decimals always, which is right for a lap time and
// wrong for a race number: `driver_number 101.000000` is a line nobody wants to
// read in a diff. So counts, indices and the race number go through
// `profile_format_int`, and only genuinely continuous quantities — a lap time, a
// points total that part-distance scaling can halve — go through `format_value`.
//
// ## Every field is required, and that is the whole point
//
// ADR-0042 rejects best-effort loading: parse what is recognized, default what is
// missing. It costs nothing to write and it fails silently in the one case that
// matters. Rename `standings` to `championship` and every career resets to empty,
// with no error raised and nothing for a player to point at.
//
// So the binder is strict in both directions. A key the current schema does not
// know is an error naming the key. A required key that is absent is an error
// naming the key. A record with the wrong number of tokens is an error. The only
// thing a migration's `apply` ever has to do is make an old document satisfy the
// new schema, and the corpus test is what proves it did.
//
// ## Ghosts are referenced, not inlined
//
// A `best` carries a ghost **id**, not a transform stream. Ghosts are the largest
// thing in the profile by an order of magnitude, and a profile that has to be
// fully parsed to read a driver's name gets slow exactly as somebody plays more.
// The id resolves to `user://ghosts/<id>.ghost` on the Godot side, which is why
// `profile_is_slug` rejects `.` and `/`: a save file is user-writable and a ghost
// id is pasted into a path, so the character set is a boundary and not a style
// rule.
//
// ## Settings are NOT in this struct
//
// ADR-0042 is explicit that the separation is not tidiness. If `profile.save` is
// unreadable a player still has to reach the menu, read the text and use the pad,
// so the comfort options, the control bindings and the accessibility settings in
// `ARCHITECTURE.md` §18 must load when the career does not. Coupling them means a
// corrupt career save presents an unreadable menu for fixing it.
//
// `PROFILE_SETTINGS_FILE_NAME` is named here and modeled in `core/settings.h` —
// a separate header on purpose, so the separation cannot erode by somebody
// adding a `bool invert_look` to `Profile`. It was modeled nowhere for a
// milestone, then engine-side where `tests/run.sh` could not reach it; issue
// #178 is the move to its stated home.
//
// ## Multiple profiles
//
// Neither designed in nor designed out. One profile per file, so a second is a
// path change and not a schema change. Nothing below knows the file's name except
// the two helpers that build the temporary and the moved-aside names from it.
//
// ## The standings projection, and the join that is not done here
//
// `src/core/standings.h` holds the **authoritative runtime type** for a season
// table: the classification, the tie-breaks, promotion. What is stored here is
// its **serialized projection** — a fixed array of `{ driver_id, points }` plus
// where the career is — and this header deliberately does not include that one.
//
// A persistence format that depends on a runtime type is a format that changes
// every time the runtime type does, which is a version bump and a migration for a
// refactor that changed no data. So the separation is a feature. **Wiring the two
// together is a main-thread join**, not something either file does alone.
//
// The projection stores a driver id as a **roster slug and not an index**. An
// index into `data/drivers.json` is only meaningful against the roster that was
// loaded when it was written, and ADR-0047's roster is content: reorder it and
// every name in a saved standings table silently changes driver. The slug also
// means the file is readable with no roster loaded at all, which is what makes it
// diffable in the sense ADR-0042 wanted.
//
// ## Type names are all prefixed `Profile`
//
// Not a style preference. `standings.h`, `race_rules.h`, `replay.h` and `ghost.h`
// are landing in this same `kart::core` namespace, and `StandingsRow` or
// `CareerState` are names two of those would plausibly pick. Two definitions of
// one name in one namespace is a link error at best and an ODR violation at
// worst, discovered by whoever includes both headers first.

namespace kart::core {

// --- the version, and the chain -----------------------------------------------
//
// **Bumped on every format change, and a migration may be the identity
// function.** ADR-0042 rejected the alternative — bump only on incompatible
// changes — because it requires somebody to correctly classify a change as
// compatible at the moment they are thinking about something else.
//
// The test suite enforces this rather than trusting it: `tests/data/saves/v1.save`
// is asserted to be **byte-identical** to what `format_profile` produces from the
// profile it parses to. Change anything the writer emits, including the comment
// preamble, and that assertion fails until the version is bumped and a new corpus
// file is captured. Which is the rule, made mechanical.
inline constexpr int PROFILE_FORMAT_VERSION = 1;

// Names, not paths. `user://` resolution is Godot's job.
inline constexpr const char *PROFILE_FILE_NAME = "profile.save";
inline constexpr const char *PROFILE_SETTINGS_FILE_NAME = "settings.cfg";

// --- sizes ---------------------------------------------------------------------

// A display name, in **bytes and not characters**. The name is the one field in
// this format that is free text rather than an identifier, and it is passed
// through as opaque UTF-8: a driver called Nicolo Cuman spells it with an accent
// and the format has no business refusing that. Nothing here decodes it, so the
// ASCII-only rule that CLAUDE.md states for string literals in `src/` applies to
// this header's own literals and not to the data flowing through it.
//
// `GAMEDESIGN.md` §7 read the real entry list rather than imagining it: the name
// is "Surname, Forename", one field, comma and space included.
inline constexpr int PROFILE_NAME_CHARS = 48;

// An FIA three-letter nationality code plus its terminator. GBR, ITA, ARG.
inline constexpr int PROFILE_NATION_CHARS = 4;

// A slug: a livery, a ghost id, a driver id. Filename-safe by construction
// because a ghost id becomes part of a path.
inline constexpr int PROFILE_SLUG_CHARS = 32;

// The standings projection cannot hold more drivers than a session can hold
// karts, so it borrows `session.h`'s ceiling rather than declaring a second one.
inline constexpr int PROFILE_MAX_STANDINGS = SESSION_MAX_ENTRIES;

// `GAMEDESIGN.md` §10's content target is **two** circuits, and a best is per
// track per layout per class: 2 x 2 x 2 = 8 rows against the design as written.
// This is eight circuits' worth of headroom, and like `SESSION_MAX_ENTRIES` it is
// a sanity ceiling rather than a plan — it exists so a parse that produced
// garbage can be rejected.
inline constexpr int PROFILE_MAX_BESTS = 32;

// Sanity ceilings on the career position, same argument again. The design is 2
// seasons of 4 rounds (`GAMEDESIGN.md` §5), and §10 is explicit that a season
// reads its rounds from data, so neither number is hardcoded as the design's.
inline constexpr int PROFILE_MAX_SEASONS = 8;
inline constexpr int PROFILE_MAX_ROUNDS = 16;

// A race number. `GAMEDESIGN.md` §7: per-category blocks, 101-197 for OK and a
// separate 3xx block for wild cards, fixed for the season. Three digits is the
// widest the real documents use.
inline constexpr int PROFILE_MIN_NUMBER = 1;
inline constexpr int PROFILE_MAX_NUMBER = 999;

// --- the profile ---------------------------------------------------------------

struct ProfileIdentity {
	char name[PROFILE_NAME_CHARS] = {};
	int number = 0;
	char nationality[PROFILE_NATION_CHARS] = {};
	char livery[PROFILE_SLUG_CHARS] = {};
};

// One row of the serialized season table. See the header comment on why this is a
// projection of `standings.h`'s type and not that type.
struct ProfileStandingRow {
	char driver_id[PROFILE_SLUG_CHARS] = {};
	// A double rather than an int because ADR-0043's part-distance rule scales a
	// classification to **half** points under 75% of the scheduled distance, and
	// half of 25 is 12.5. Quantized to `tuning_micro`'s grid like every other
	// number in this project.
	double points = 0.0;
};

// Where the career is, and the standings it has accumulated.
//
// `round` is the 0-based index of the round the career is **at**, so a fresh
// season is 0 and a season whose last round has been run sits at the round count
// until it promotes or is re-run. `PROFILE_MAX_ROUNDS` is therefore an inclusive
// bound.
struct ProfileCareer {
	KartClass kart_class = KartClass::OK;
	int season = 0;
	int round = 0;
	int standings_count = 0;
	ProfileStandingRow standings[PROFILE_MAX_STANDINGS];
};

// A best lap and the ghost that set it.
//
// `GAMEDESIGN.md` §5: best laps and ghosts persist across a re-run of a season,
// the standings do not. That is a rule about who clears what and it is not
// enforced here — nothing in this header erases anything — but it is the reason
// bests are a separate array from the career rather than a field inside it.
struct ProfileBest {
	char track_id[SESSION_ID_CHARS] = {};
	TrackLayout layout = TrackLayout::Forward;
	// The class the lap was set in. A best lap with no class attached silently
	// compares a shifter against a single-speed, which is the reason `session.h`
	// carries `KartClass` at all.
	KartClass kart_class = KartClass::KZ2;
	double lap_time_s = 0.0;
	// Resolves to `user://ghosts/<id>.ghost`. A stream is never inlined here.
	char ghost_id[PROFILE_SLUG_CHARS] = {};
};

// --- text primitives -----------------------------------------------------------

inline constexpr int PROFILE_KEY_CHARS = 32;
// The longest record is a `best`: a 47-character track id, `reverse`, `kz2`, a
// lap time and a 31-character ghost id, with four separators. 106 characters.
inline constexpr int PROFILE_TEXT_CHARS = 128;
inline constexpr int PROFILE_MAX_TOKENS = 5;
inline constexpr int PROFILE_DETAIL_CHARS = 64;

inline bool profile_is_space(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Compare a counted slice against a NUL-terminated literal. Hand-rolled rather
// than `strncmp` so the length check is not the caller's problem: `strncmp` would
// call "forward" and "forwards" equal at n = 7.
inline bool profile_text_equals(const char *text, int len, const char *literal) {
	if (text == nullptr || literal == nullptr || len < 0) {
		return false;
	}
	int i = 0;
	for (; i < len; ++i) {
		if (literal[i] == '\0' || text[i] != literal[i]) {
			return false;
		}
	}
	return literal[i] == '\0';
}

inline int profile_length(const char *text) {
	int len = 0;
	if (text == nullptr) {
		return 0;
	}
	while (text[len] != '\0') {
		++len;
	}
	return len;
}

// Filename-safe, canonical, lower case. `[a-z0-9_-]`, non-empty, and not starting
// with `-` so a slug can never be read as a command-line flag by anything
// downstream.
//
// `.` and `/` are the two exclusions that matter: a ghost id is pasted into
// `user://ghosts/<id>.ghost` and a save file is user-writable, so `../../` in a
// slug is the difference between a save format and a file-deletion primitive.
inline bool profile_is_slug_within(const char *text, int len, int cap) {
	if (text == nullptr || len <= 0 || len >= cap) {
		return false;
	}
	if (text[0] == '-') {
		return false;
	}
	for (int i = 0; i < len; ++i) {
		const char c = text[i];
		const bool lower = c >= 'a' && c <= 'z';
		const bool digit = c >= '0' && c <= '9';
		if (!lower && !digit && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

inline bool profile_is_slug(const char *text, int len) {
	return profile_is_slug_within(text, len, PROFILE_SLUG_CHARS);
}

// A track id is a slug too, but with `session.h`'s wider buffer: `SESSION_ID_CHARS`
// is 48 and `PROFILE_SLUG_CHARS` is 32, and the id in a `best` record has to fit
// whatever `SessionConfig::set_track_id` accepted.
inline bool profile_is_track_slug(const char *text, int len) {
	return profile_is_slug_within(text, len, SESSION_ID_CHARS);
}

// Three upper-case letters. The FIA codes are upper case in the real entry list
// and a format that accepted both cases would have two spellings of one country,
// which breaks a byte-identical round trip the moment somebody hand-edits one.
inline bool profile_is_nationality(const char *text, int len) {
	if (text == nullptr || len != 3) {
		return false;
	}
	for (int i = 0; i < 3; ++i) {
		if (text[i] < 'A' || text[i] > 'Z') {
			return false;
		}
	}
	return true;
}

// A display name: non-empty, no control characters, no `#` (which starts a
// comment), and no leading or trailing space. `set_driver_name` guarantees all of
// this on the way in, so the writer never has to sanitize and the round trip is
// exact.
inline bool profile_is_display_name(const char *text, int len) {
	if (text == nullptr || len <= 0 || len >= PROFILE_NAME_CHARS) {
		return false;
	}
	if (profile_is_space(text[0]) || profile_is_space(text[len - 1])) {
		return false;
	}
	for (int i = 0; i < len; ++i) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c < 0x20 || c == 0x7F || c == '#') {
			return false;
		}
	}
	return true;
}


// Parse an integer, rejecting anything that is not entirely one. A prefix is not
// a number, for the reason `parse_value` gives: a silently truncated "3abc" is a
// value nobody wrote down.
//
// A leading `+` and leading zeros are **accepted**, and the writer emits neither.
// A hand-edited `driver_number 007` loads as 7 and is written back canonical,
// which keeps the round trip byte-identical without spending a career to teach
// somebody about canonical integers.
inline bool profile_parse_int(const char *text, int len, int &out) {
	if (text == nullptr || len <= 0) {
		return false;
	}
	int index = 0;
	bool negative = false;
	if (text[0] == '+' || text[0] == '-') {
		negative = text[0] == '-';
		index = 1;
	}
	long long value = 0;
	int digits = 0;
	for (; index < len; ++index) {
		if (text[index] < '0' || text[index] > '9') {
			return false;
		}
		value = value * 10 + (text[index] - '0');
		++digits;
		if (value > 2147483647LL) {
			return false;
		}
	}
	if (digits == 0) {
		return false;
	}
	out = static_cast<int>(negative ? -value : value);
	return true;
}

// The tokens of a record's text, as slices into it. Nothing is copied.
struct ProfileTokens {
	int count = 0;
	const char *begin[PROFILE_MAX_TOKENS] = {};
	int length[PROFILE_MAX_TOKENS] = {};
};

// Split on runs of whitespace. Returns false if there are more than
// `PROFILE_MAX_TOKENS` of them, so a record with a stray sixth field is an error
// rather than a silently ignored tail.
inline bool profile_tokenize(const char *text, int len, ProfileTokens &out) {
	out.count = 0;
	if (text == nullptr) {
		return true;
	}
	if (len < 0) {
		len = profile_length(text);
	}
	int index = 0;
	while (index < len) {
		while (index < len && profile_is_space(text[index])) {
			++index;
		}
		if (index >= len) {
			break;
		}
		const int start = index;
		while (index < len && !profile_is_space(text[index])) {
			++index;
		}
		if (out.count >= PROFILE_MAX_TOKENS) {
			return false;
		}
		out.begin[out.count] = text + start;
		out.length[out.count] = index - start;
		++out.count;
	}
	return true;
}

inline bool profile_parse_kart_class(const char *text, int len, KartClass &out) {
	for (int i = 0; i < KART_CLASS_COUNT; ++i) {
		if (profile_text_equals(text, len, kart_class_name(static_cast<KartClass>(i)))) {
			out = static_cast<KartClass>(i);
			return true;
		}
	}
	return false;
}

inline bool profile_parse_track_layout(const char *text, int len, TrackLayout &out) {
	for (int i = 0; i < TRACK_LAYOUT_COUNT; ++i) {
		if (profile_text_equals(text, len, track_layout_name(static_cast<TrackLayout>(i)))) {
			out = static_cast<TrackLayout>(i);
			return true;
		}
	}
	return false;
}

// --- the profile struct --------------------------------------------------------

struct Profile {
	ProfileIdentity driver;
	ProfileCareer career;
	int best_count = 0;
	ProfileBest bests[PROFILE_MAX_BESTS] = {};

	// Copy a display name in, sanitizing rather than rejecting. A caller passing a
	// name with a newline or a `#` in it has made a typo, not an attack, and losing
	// the save would be the larger harm — the same call `tuning.h`'s preset writer
	// makes about a multi-line preset name. Returns false if anything had to be
	// changed or dropped, so a UI can say so.
	bool set_driver_name(const char *text) {
		for (int i = 0; i < PROFILE_NAME_CHARS; ++i) {
			driver.name[i] = '\0';
		}
		if (text == nullptr) {
			return false;
		}
		bool clean = true;
		int written = 0;
		// Runs of whitespace collapse to one space, and leading whitespace is
		// dropped. Replacing a `#` beside a space would otherwise leave a double
		// space in the name — "Turney #1" becoming "Turney  1" — which round-trips
		// fine and looks like a bug on a timing screen forever.
		bool pending_space = false;
		for (int i = 0; text[i] != '\0'; ++i) {
			const unsigned char c = static_cast<unsigned char>(text[i]);
			const bool illegal = c < 0x20 || c == 0x7F || c == '#';
			if (illegal) {
				clean = false;
			}
			if (illegal || profile_is_space(text[i])) {
				if (written > 0) {
					// Deferred, so a trailing run never reaches the buffer and the
					// trim is not a second pass.
					if (pending_space) {
						clean = false;
					}
					pending_space = true;
				} else {
					clean = false; // Leading whitespace, dropped.
				}
				continue;
			}
			if (pending_space) {
				if (written >= PROFILE_NAME_CHARS - 1) {
					clean = false;
					break;
				}
				driver.name[written++] = ' ';
				pending_space = false;
			}
			if (written >= PROFILE_NAME_CHARS - 1) {
				clean = false;
				break;
			}
			driver.name[written++] = text[i];
		}
		if (pending_space) {
			clean = false; // Trailing whitespace, dropped.
		}
		for (int i = written; i < PROFILE_NAME_CHARS; ++i) {
			driver.name[i] = '\0';
		}
		return clean && driver.name[0] != '\0';
	}

	// Nationality and livery are identifiers rather than free text, so they are
	// rejected rather than sanitized: there is no defensible repair for `gbr` or
	// for `../evil`, and a UI picks these from a list.
	bool set_nationality(const char *text) {
		for (int i = 0; i < PROFILE_NATION_CHARS; ++i) {
			driver.nationality[i] = '\0';
		}
		if (!profile_is_nationality(text, profile_length(text))) {
			return false;
		}
		for (int i = 0; i < 3; ++i) {
			driver.nationality[i] = text[i];
		}
		return true;
	}

	bool set_livery(const char *text) {
		return copy_slug(text, driver.livery, PROFILE_SLUG_CHARS);
	}

	// Append a standings row. Rejects a duplicate driver, because two rows for one
	// driver is a classification that cannot be displayed and a projection that
	// cannot be joined back to `standings.h`.
	bool add_standing(const char *driver_id, double points) {
		if (career.standings_count >= PROFILE_MAX_STANDINGS) {
			return false;
		}
		const int len = profile_length(driver_id);
		if (!profile_is_slug(driver_id, len)) {
			return false;
		}
		if (!(points >= 0.0) || !std::isfinite(points)) {
			return false;
		}
		for (int i = 0; i < career.standings_count; ++i) {
			if (profile_text_equals(driver_id, len, career.standings[i].driver_id)) {
				return false;
			}
		}
		ProfileStandingRow &row = career.standings[career.standings_count];
		if (!copy_slug(driver_id, row.driver_id, PROFILE_SLUG_CHARS)) {
			return false;
		}
		row.points = points;
		++career.standings_count;
		return true;
	}

	// Record or replace a best. Kept sorted by (track_id, layout, class) on the way
	// in, so the writer emits array order and two profiles with the same bests are
	// byte-identical whatever order they were set in. Sorting **on write** would
	// have worked too and would have shown every line as changed on a save that
	// changed one, which is the argument `tuning.h`'s preset writer makes for
	// declaration order.
	//
	// A slower lap on an existing key is ignored and reported as `true`: the caller
	// finished a lap, nothing failed, and a best that got worse is not a best.
	bool set_best(const char *track_id, TrackLayout layout, KartClass kart_class,
			double lap_time_s, const char *ghost_id) {
		const int track_len = profile_length(track_id);
		if (!profile_is_track_slug(track_id, track_len)) {
			return false;
		}
		if (!profile_is_slug(ghost_id, profile_length(ghost_id))) {
			return false;
		}
		if (!(lap_time_s > 0.0) || !std::isfinite(lap_time_s)) {
			return false;
		}
		if (static_cast<int>(layout) < 0 || static_cast<int>(layout) >= TRACK_LAYOUT_COUNT) {
			return false;
		}
		if (static_cast<int>(kart_class) < 0 ||
				static_cast<int>(kart_class) >= KART_CLASS_COUNT) {
			return false;
		}

		int slot = 0;
		while (slot < best_count) {
			const int order = compare_best_key(bests[slot], track_id, track_len, layout, kart_class);
			if (order == 0) {
				if (tuning_micro(lap_time_s) >= tuning_micro(bests[slot].lap_time_s)) {
					return true; // Not an improvement. Nothing failed.
				}
				bests[slot].lap_time_s = lap_time_s;
				return copy_slug(ghost_id, bests[slot].ghost_id, PROFILE_SLUG_CHARS);
			}
			if (order > 0) {
				break;
			}
			++slot;
		}
		if (best_count >= PROFILE_MAX_BESTS) {
			return false;
		}
		for (int i = best_count; i > slot; --i) {
			bests[i] = bests[i - 1];
		}
		ProfileBest &best = bests[slot];
		best = ProfileBest();
		for (int i = 0; i < track_len; ++i) {
			best.track_id[i] = track_id[i];
		}
		best.layout = layout;
		best.kart_class = kart_class;
		best.lap_time_s = lap_time_s;
		++best_count;
		return copy_slug(ghost_id, best.ghost_id, PROFILE_SLUG_CHARS);
	}

	const ProfileBest *find_best(const char *track_id, TrackLayout layout,
			KartClass kart_class) const {
		const int len = profile_length(track_id);
		for (int i = 0; i < best_count; ++i) {
			if (compare_best_key(bests[i], track_id, len, layout, kart_class) == 0) {
				return &bests[i];
			}
		}
		return nullptr;
	}

	const ProfileStandingRow *find_standing(const char *driver_id) const {
		const int len = profile_length(driver_id);
		for (int i = 0; i < career.standings_count; ++i) {
			if (profile_text_equals(driver_id, len, career.standings[i].driver_id)) {
				return &career.standings[i];
			}
		}
		return nullptr;
	}

	// Whether this describes a profile that can be written and read back. Cheap,
	// total, and called by the loader rather than trusted — same contract as
	// `SessionConfig::is_valid`.
	bool is_valid() const {
		if (!profile_is_display_name(driver.name, profile_length(driver.name))) {
			return false;
		}
		if (driver.number < PROFILE_MIN_NUMBER || driver.number > PROFILE_MAX_NUMBER) {
			return false;
		}
		if (!profile_is_nationality(driver.nationality, profile_length(driver.nationality))) {
			return false;
		}
		if (!profile_is_slug(driver.livery, profile_length(driver.livery))) {
			return false;
		}
		if (static_cast<int>(career.kart_class) < 0 ||
				static_cast<int>(career.kart_class) >= KART_CLASS_COUNT) {
			return false;
		}
		if (career.season < 0 || career.season >= PROFILE_MAX_SEASONS) {
			return false;
		}
		if (career.round < 0 || career.round > PROFILE_MAX_ROUNDS) {
			return false;
		}
		if (career.standings_count < 0 || career.standings_count > PROFILE_MAX_STANDINGS) {
			return false;
		}
		for (int i = 0; i < career.standings_count; ++i) {
			const ProfileStandingRow &row = career.standings[i];
			const int len = profile_length(row.driver_id);
			if (!profile_is_slug(row.driver_id, len)) {
				return false;
			}
			if (!std::isfinite(row.points) || row.points < 0.0) {
				return false;
			}
			for (int j = i + 1; j < career.standings_count; ++j) {
				if (profile_text_equals(row.driver_id, len, career.standings[j].driver_id)) {
					return false;
				}
			}
		}
		if (best_count < 0 || best_count > PROFILE_MAX_BESTS) {
			return false;
		}
		for (int i = 0; i < best_count; ++i) {
			const ProfileBest &best = bests[i];
			if (!profile_is_track_slug(best.track_id, profile_length(best.track_id))) {
				return false;
			}
			if (!profile_is_slug(best.ghost_id, profile_length(best.ghost_id))) {
				return false;
			}
			if (!std::isfinite(best.lap_time_s) || !(best.lap_time_s > 0.0)) {
				return false;
			}
			if (static_cast<int>(best.layout) < 0 ||
					static_cast<int>(best.layout) >= TRACK_LAYOUT_COUNT) {
				return false;
			}
			if (static_cast<int>(best.kart_class) < 0 ||
					static_cast<int>(best.kart_class) >= KART_CLASS_COUNT) {
				return false;
			}
			// Strictly ascending, which rejects a duplicate key and an unsorted array
			// in one comparison. The writer depends on the order.
			if (i > 0) {
				const ProfileBest &previous = bests[i - 1];
				if (compare_best_key(previous, best.track_id, profile_length(best.track_id),
							best.layout, best.kart_class) >= 0) {
					return false;
				}
			}
		}
		return true;
	}

	// The ordering the bests array is kept in. Negative if `best` sorts before the
	// given key, zero if they are the same key, positive if after.
	static int compare_best_key(const ProfileBest &best, const char *track_id, int track_len,
			TrackLayout layout, KartClass kart_class) {
		const int stored_len = profile_length(best.track_id);
		const int shared = stored_len < track_len ? stored_len : track_len;
		for (int i = 0; i < shared; ++i) {
			const unsigned char a = static_cast<unsigned char>(best.track_id[i]);
			const unsigned char b = static_cast<unsigned char>(track_id[i]);
			if (a != b) {
				return a < b ? -1 : 1;
			}
		}
		if (stored_len != track_len) {
			return stored_len < track_len ? -1 : 1;
		}
		if (best.layout != layout) {
			return static_cast<int>(best.layout) < static_cast<int>(layout) ? -1 : 1;
		}
		if (best.kart_class != kart_class) {
			return static_cast<int>(best.kart_class) < static_cast<int>(kart_class) ? -1 : 1;
		}
		return 0;
	}

private:
	static bool copy_slug(const char *text, char *out, int cap) {
		for (int i = 0; i < cap; ++i) {
			out[i] = '\0';
		}
		const int len = profile_length(text);
		if (!profile_is_slug(text, len) || len >= cap) {
			return false;
		}
		for (int i = 0; i < len; ++i) {
			out[i] = text[i];
		}
		return true;
	}
};

// --- writing -------------------------------------------------------------------
//
// **The write is atomic, and that contract is the Godot side's to honor.** ADR-0042
// spells out the sequence and it is repeated here because this is the file a
// reader is in when they wonder about it:
//
//   1. serialize into memory with `format_profile` — it either fits or returns -1,
//      so a truncated buffer never reaches a file;
//   2. write the whole thing to `profile.save.tmp` (`profile_temp_name`);
//   3. flush the temporary file, then close it, then confirm its length off disk
//      before going near the target;
//   4. rename the temporary **over** the target. Rename is atomic on every
//      filesystem this ships on and it is the only operation that gives "the
//      previous save survives a crash" for free.
//
// Never open `profile.save` for writing. A save interrupted halfway through a
// direct write is exactly the truncated file that `load_profile` cannot always
// distinguish from a shorter valid one, and step 4 is what makes that
// undetectable case unreachable in practice.
//
// **Step 3 said "fsync" and that instruction cannot be followed.** ADR-0042 says it
// too. Godot's `FileAccess` exposes 68 methods and not one of them syncs: there is
// no `fsync`, no `F_FULLFSYNC`, no `O_SYNC`, and nothing in `DirAccess` either —
// checked against `extension_api.json` rather than remembered. So the guarantee has
// to be stated at the size it really is:
//
//   * **Process death cannot produce a half-written save.** The target is never
//     opened for writing at all, so whatever kills the process kills it during the
//     temporary, and the previous save is untouched. That is measured from outside
//     the implementation: `profile_probe.gd` chmods the target to 0444 and saves
//     successfully, because POSIX `rename` needs write permission on the directory
//     and not on the file. A direct open fails there with error 12, which is what
//     `KartTuning::save_preset` used to do.
//   * **A power cut is not covered and cannot be from here.** A close is not a
//     sync; without one, POSIX does not order the data against the directory entry.
//     Claiming APFS makes it safe would be memory dressed up as a measurement.
//
// The honest fix is an fsync shim on the GDExtension side, which is a ticket rather
// than a line. Confirming the length off disk before the rename is the part of the
// guarantee that *is* reachable, and it is what catches a full disk — `store_buffer`
// returning true says the calls were accepted, not that the bytes landed.

// The preamble, emitted verbatim. Deliberately short: the byte-identity assertion
// against the corpus means every character here is part of the format, so a
// paragraph added to it is a version bump.
inline constexpr const char *PROFILE_PREAMBLE =
		"# kartgame driver profile. One profile per file, and settings are not here:\n"
		"# they live alongside in settings.cfg so that a profile which will not parse\n"
		"# still leaves a menu a player can read. See src/core/profile.h, ADR-0042.\n";

// Serialize a profile. Returns the length written, or -1 if it did not fit or the
// profile is not valid — a writer that emitted an invalid profile would produce a
// file its own loader rejects, and the failure would surface as corruption on the
// next launch rather than as a bug here.
//
// Section order is fixed and a blank line precedes each non-empty repeated block.
// Two saves of the same profile are byte-identical; that is what makes `git diff`
// on a copied save mean something.
inline int format_profile(const Profile &profile, char *out, int cap) {
	if (out == nullptr || cap <= 0 || !profile.is_valid()) {
		return -1;
	}
	int written = 0;
	bool overflow = false;
	auto append = [&](const char *text) {
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
	char number[TUNING_VALUE_CHARS];

	append(PROFILE_PREAMBLE);

	append("version ");
	if (format_int(PROFILE_FORMAT_VERSION, number, sizeof(number)) < 0) {
		return -1;
	}
	append(number);
	append("\n");

	append("driver_name ");
	append(profile.driver.name);
	append("\n");

	append("driver_number ");
	if (format_int(profile.driver.number, number, sizeof(number)) < 0) {
		return -1;
	}
	append(number);
	append("\n");

	append("driver_nationality ");
	append(profile.driver.nationality);
	append("\n");

	append("driver_livery ");
	append(profile.driver.livery);
	append("\n");

	append("career_class ");
	append(kart_class_name(profile.career.kart_class));
	append("\n");

	append("career_season ");
	if (format_int(profile.career.season, number, sizeof(number)) < 0) {
		return -1;
	}
	append(number);
	append("\n");

	append("career_round ");
	if (format_int(profile.career.round, number, sizeof(number)) < 0) {
		return -1;
	}
	append(number);
	append("\n");

	// Standings keep their stored order. It is a **classification** — position is
	// the meaning — and `standings.h` is what decides it, so sorting here would be
	// this file second-guessing the authority it is a projection of.
	if (profile.career.standings_count > 0) {
		append("\n");
	}
	for (int i = 0; i < profile.career.standings_count; ++i) {
		const ProfileStandingRow &row = profile.career.standings[i];
		append("standing ");
		append(row.driver_id);
		append(" ");
		if (format_value(row.points, number, sizeof(number)) < 0) {
			return -1;
		}
		append(number);
		append("\n");
	}

	if (profile.best_count > 0) {
		append("\n");
	}
	for (int i = 0; i < profile.best_count; ++i) {
		const ProfileBest &best = profile.bests[i];
		append("best ");
		append(best.track_id);
		append(" ");
		append(track_layout_name(best.layout));
		append(" ");
		append(kart_class_name(best.kart_class));
		append(" ");
		if (format_value(best.lap_time_s, number, sizeof(number)) < 0) {
			return -1;
		}
		append(number);
		append(" ");
		append(best.ghost_id);
		append("\n");
	}

	if (overflow || written >= cap) {
		return -1;
	}
	out[written] = '\0';
	return written;
}

// A comfortable ceiling for a serialized profile, so a caller can size a buffer
// without arithmetic: the preamble, eight scalar lines, and every repeated record
// at its widest.
inline constexpr int PROFILE_TEXT_CAP =
		320 + 8 * (PROFILE_KEY_CHARS + PROFILE_NAME_CHARS) +
		PROFILE_MAX_STANDINGS * (PROFILE_SLUG_CHARS + TUNING_VALUE_CHARS + 16) +
		PROFILE_MAX_BESTS * (SESSION_ID_CHARS + 2 * PROFILE_SLUG_CHARS + TUNING_VALUE_CHARS + 32);

// --- the lexed document -------------------------------------------------------
//
// The intermediate a migration operates on, and the reason the lexer is
// version-independent: a save is `key` plus the rest of the line at **every**
// version, so only the schema is versioned and the tokenizer never is. A
// migration is therefore a record rewrite — add a key with a default, rename one,
// drop one — and the strict binder afterwards is what proves it did enough.
//
// Sized to a little over 13 KB. Fine on a stack in a test and fine as a member on
// the Godot side; a caller putting one in a deeply recursive function should not.

inline constexpr int PROFILE_MAX_RECORDS =
		8 + PROFILE_MAX_STANDINGS + PROFILE_MAX_BESTS;

struct ProfileRecord {
	char key[PROFILE_KEY_CHARS] = {};
	char text[PROFILE_TEXT_CHARS] = {};
	int line = 0; // 1-based, for a diagnostic a person can act on
};

struct ProfileDocument {
	int declared_version = 0;
	int record_count = 0;
	ProfileRecord record[PROFILE_MAX_RECORDS] = {};

	int count_of(const char *key) const {
		int found = 0;
		const int len = profile_length(key);
		for (int i = 0; i < record_count; ++i) {
			if (profile_text_equals(key, len, record[i].key)) {
				++found;
			}
		}
		return found;
	}

	// The nth record with this key, or null. `occurrence` is 0-based.
	ProfileRecord *find(const char *key, int occurrence = 0) {
		const int len = profile_length(key);
		int seen = 0;
		for (int i = 0; i < record_count; ++i) {
			if (profile_text_equals(key, len, record[i].key)) {
				if (seen == occurrence) {
					return &record[i];
				}
				++seen;
			}
		}
		return nullptr;
	}

	const ProfileRecord *find(const char *key, int occurrence = 0) const {
		return const_cast<ProfileDocument *>(this)->find(key, occurrence);
	}

	// Append a record. The three helpers below are what make ADR-0042's "an
	// identity migration costs one line" true of a non-identity one as well: a
	// bump that adds a field is `append("new_key", "default")`, and a bump that
	// renames one is `rename_key("standings", "championship")`.
	bool append(const char *key, const char *text) {
		if (record_count >= PROFILE_MAX_RECORDS) {
			return false;
		}
		const int key_len = profile_length(key);
		const int text_len = profile_length(text);
		if (key_len <= 0 || key_len >= PROFILE_KEY_CHARS || text_len >= PROFILE_TEXT_CHARS) {
			return false;
		}
		ProfileRecord &slot = record[record_count];
		slot = ProfileRecord();
		for (int i = 0; i < key_len; ++i) {
			slot.key[i] = key[i];
		}
		for (int i = 0; i < text_len; ++i) {
			slot.text[i] = text[i];
		}
		slot.line = 0; // Synthesized by a migration, not read from a file.
		++record_count;
		return true;
	}

	// Rename every occurrence. Returns how many were renamed, so a migration can
	// assert it found what it expected rather than assuming.
	int rename_key(const char *from, const char *to) {
		const int from_len = profile_length(from);
		const int to_len = profile_length(to);
		if (to_len <= 0 || to_len >= PROFILE_KEY_CHARS) {
			return 0;
		}
		int renamed = 0;
		for (int i = 0; i < record_count; ++i) {
			if (!profile_text_equals(from, from_len, record[i].key)) {
				continue;
			}
			for (int c = 0; c < PROFILE_KEY_CHARS; ++c) {
				record[i].key[c] = '\0';
			}
			for (int c = 0; c < to_len; ++c) {
				record[i].key[c] = to[c];
			}
			++renamed;
		}
		return renamed;
	}

	// Remove every occurrence, preserving the order of the rest.
	int remove_key(const char *key) {
		const int len = profile_length(key);
		int removed = 0;
		int write = 0;
		for (int read = 0; read < record_count; ++read) {
			if (profile_text_equals(key, len, record[read].key)) {
				++removed;
				continue;
			}
			if (write != read) {
				record[write] = record[read];
			}
			++write;
		}
		record_count = write;
		return removed;
	}

	// Replace a singleton's text, or append it if absent.
	bool set_text(const char *key, const char *text) {
		ProfileRecord *slot = find(key);
		if (slot == nullptr) {
			return append(key, text);
		}
		const int len = profile_length(text);
		if (len >= PROFILE_TEXT_CHARS) {
			return false;
		}
		for (int i = 0; i < PROFILE_TEXT_CHARS; ++i) {
			slot->text[i] = '\0';
		}
		for (int i = 0; i < len; ++i) {
			slot->text[i] = text[i];
		}
		return true;
	}
};

// --- what went wrong ----------------------------------------------------------

enum class ProfileLoadStatus : int {
	// Loaded, after zero or more migrations.
	Ok = 0,

	// Did not parse. **The caller moves the file aside, it does not overwrite it.**
	// See `profile_corrupt_name`.
	Corrupt = 1,

	// Written by a newer build. **The caller leaves the file completely alone**:
	// not loaded, not renamed, not overwritten. A save from a newer build is not an
	// old save and ADR-0042's "never fails because of its age" does not reach it;
	// what would reach it is the player downgrading, losing a career that is
	// perfectly intact, and finding `profile.save.corrupt.1` where their season
	// was.
	FutureVersion = 2,
};

inline const char *profile_load_status_name(ProfileLoadStatus status) {
	switch (status) {
		case ProfileLoadStatus::Ok: return "ok";
		case ProfileLoadStatus::Corrupt: return "corrupt";
		case ProfileLoadStatus::FutureVersion: return "future_version";
	}
	return "invalid";
}

// Why it did not load. One value per distinguishable failure, because "corrupt"
// on its own is the diagnostic ADR-0041 complained about in the replay case:
// a hash mismatch says two things differ and nothing about where.
enum class ProfileProblem : int {
	None = 0,
	NoVersionLine,     // no `version` record at all
	VersionNotFirst,   // a record before it; the version has to be readable alone
	BadVersionNumber,  // not an integer, or below 1
	FutureVersion,     // above PROFILE_FORMAT_VERSION
	MigrationGap,      // no registered step out of this version
	MigrationFailed,   // a step ran and reported failure
	UnknownKey,        // not in the current schema after migration
	MissingField,      // a required key absent
	DuplicateField,    // a singleton key more than once
	WrongTokenCount,   // a record with the wrong number of fields
	BadValue,          // a number or an integer that does not parse
	BadEnum,           // a class or a layout that names nothing
	BadIdentifier,     // a slug, a nationality or a name outside its character set
	TooLong,           // a key or a line past its buffer
	TooManyRecords,    // more records than PROFILE_MAX_RECORDS
	Invalid,           // parsed, but `Profile::is_valid` rejects the result
};

inline const char *profile_problem_name(ProfileProblem problem) {
	switch (problem) {
		case ProfileProblem::None: return "none";
		case ProfileProblem::NoVersionLine: return "no_version_line";
		case ProfileProblem::VersionNotFirst: return "version_not_first";
		case ProfileProblem::BadVersionNumber: return "bad_version_number";
		case ProfileProblem::FutureVersion: return "future_version";
		case ProfileProblem::MigrationGap: return "migration_gap";
		case ProfileProblem::MigrationFailed: return "migration_failed";
		case ProfileProblem::UnknownKey: return "unknown_key";
		case ProfileProblem::MissingField: return "missing_field";
		case ProfileProblem::DuplicateField: return "duplicate_field";
		case ProfileProblem::WrongTokenCount: return "wrong_token_count";
		case ProfileProblem::BadValue: return "bad_value";
		case ProfileProblem::BadEnum: return "bad_enum";
		case ProfileProblem::BadIdentifier: return "bad_identifier";
		case ProfileProblem::TooLong: return "too_long";
		case ProfileProblem::TooManyRecords: return "too_many_records";
		case ProfileProblem::Invalid: return "invalid";
	}
	return "invalid_problem";
}

struct ProfileLoadResult {
	ProfileLoadStatus status = ProfileLoadStatus::Ok;
	ProfileProblem problem = ProfileProblem::None;

	// 1-based source line, or 0 when the failure is not about one line.
	int line = 0;

	// What the file said its version was, whatever happened next. Populated even on
	// a failure so the message can say "written as version 4".
	int declared_version = 0;

	int migrations_applied = 0;

	// The key or token at fault, for a message a person can act on. Empty when
	// there is nothing specific to name.
	char detail[PROFILE_DETAIL_CHARS] = {};

	bool ok() const { return status == ProfileLoadStatus::Ok; }

	void set_detail(const char *text, int len) {
		for (int i = 0; i < PROFILE_DETAIL_CHARS; ++i) {
			detail[i] = '\0';
		}
		if (text == nullptr) {
			return;
		}
		if (len < 0) {
			len = profile_length(text);
		}
		if (len > PROFILE_DETAIL_CHARS - 1) {
			len = PROFILE_DETAIL_CHARS - 1;
		}
		for (int i = 0; i < len; ++i) {
			const unsigned char c = static_cast<unsigned char>(text[i]);
			// A diagnostic goes on a screen and into a log, and a raw control
			// character from a garbage file has no business in either.
			detail[i] = (c < 0x20 || c == 0x7F) ? '?' : text[i];
		}
	}

	void fail(ProfileProblem why, int at_line, const char *what, int what_len) {
		status = ProfileLoadStatus::Corrupt;
		problem = why;
		line = at_line;
		set_detail(what, what_len);
	}
};

// --- the migration chain ------------------------------------------------------
//
// A step from one version to the next, operating on the lexed document. Rewrite
// records; the strict binder afterwards decides whether it was enough.
//
// **There are no migrations today, and inventing one would be a lie in a test
// fixture.** `PROFILE_FORMAT_VERSION` is 1, so the chain from any loadable file
// to the current version is empty: there is no v0 and there is no v2. That is the
// honest state and it is stated rather than papered over with an identity entry
// from 1 to 1, which is not a step and would make `migration_chain_is_complete`
// assert something false.
//
// What *is* built and tested is the machinery, and it is parameterized on the
// table precisely so that it can be: `run_migrations` and `load_profile_with`
// take the table as an argument, so `test_profile.cpp` drives a synthetic
// 1 -> 2 -> 3 chain through the real code and proves the ordering, the gap
// detection and the failure path today. The fictional versions live in a test of
// the mechanism; they never enter `tests/data/saves/`, which holds only real
// captured files.
//
// When v2 actually arrives it is: one entry in `PROFILE_MIGRATIONS`, one bump to
// `PROFILE_FORMAT_VERSION`, and `tests/data/saves/v1.save` stays exactly where it
// is and starts earning its keep. No new mechanism.

using ProfileMigrationFn = bool (*)(ProfileDocument &document);

struct ProfileMigration {
	int from_version = 0;
	int to_version = 0;
	ProfileMigrationFn apply = nullptr;
	// What the bump changed, in a few words, for a log line that says which step
	// ran. Not read by any logic.
	const char *summary = "";
};

// Null with a count of zero rather than a one-element array with a dead entry: a
// zero-length array is not legal C++ and a placeholder entry is a migration that
// does not exist sitting in a table of migrations that do.
inline const ProfileMigration *const PROFILE_MIGRATIONS = nullptr;
inline constexpr int PROFILE_MIGRATION_COUNT = 0;

// An identity migration, provided because ADR-0042 says a bump may need one and a
// caller should not have to write `return true;` in a lambda to get it. Unused
// today; the first format change that moves nothing structural uses it.
inline bool profile_migrate_identity(ProfileDocument &) {
	return true;
}

// Whether a table can carry a file from version 1 to `current`: exactly one step
// out of every intermediate version, each advancing by exactly one, and none
// pointing anywhere else. Held as a test rather than trusted, because a table
// with two steps out of v2 compiles perfectly and picks one of them by position.
inline bool profile_migration_chain_is_complete(const ProfileMigration *table, int count,
		int current_version) {
	if (current_version < 1) {
		return false;
	}
	if (count < 0 || (count > 0 && table == nullptr)) {
		return false;
	}
	for (int i = 0; i < count; ++i) {
		if (table[i].to_version != table[i].from_version + 1) {
			return false;
		}
		if (table[i].apply == nullptr) {
			return false;
		}
		if (table[i].from_version < 1 || table[i].to_version > current_version) {
			return false;
		}
		for (int j = i + 1; j < count; ++j) {
			if (table[j].from_version == table[i].from_version) {
				return false;
			}
		}
	}
	for (int version = 1; version < current_version; ++version) {
		int steps = 0;
		for (int i = 0; i < count; ++i) {
			if (table[i].from_version == version) {
				++steps;
			}
		}
		if (steps != 1) {
			return false;
		}
	}
	return true;
}

// Walk a document from its declared version to `to_version`. Writes into
// `result`; returns true if it arrived.
inline bool profile_run_migrations(ProfileDocument &document, int to_version,
		const ProfileMigration *table, int count, ProfileLoadResult &result) {
	int version = document.declared_version;
	if (version < 1) {
		result.fail(ProfileProblem::BadVersionNumber, 0, nullptr, 0);
		return false;
	}
	if (version > to_version) {
		// Not corruption, and the caller must not treat it as such.
		result.status = ProfileLoadStatus::FutureVersion;
		result.problem = ProfileProblem::FutureVersion;
		result.line = 0;
		return false;
	}
	int guard = 0;
	while (version < to_version) {
		const ProfileMigration *step = nullptr;
		for (int i = 0; i < count; ++i) {
			if (table[i].from_version == version) {
				step = &table[i];
				break;
			}
		}
		if (step == nullptr || step->apply == nullptr) {
			result.fail(ProfileProblem::MigrationGap, 0, nullptr, 0);
			return false;
		}
		if (!step->apply(document)) {
			result.fail(ProfileProblem::MigrationFailed, 0, step->summary, -1);
			return false;
		}
		version = step->to_version;
		document.declared_version = version;
		++result.migrations_applied;
		// A table that passed `profile_migration_chain_is_complete` cannot loop, but
		// `run_migrations` is public and takes any table a caller hands it.
		if (++guard > 256) {
			result.fail(ProfileProblem::MigrationGap, 0, nullptr, 0);
			return false;
		}
	}
	return true;
}

// --- the current schema -------------------------------------------------------

struct ProfileKeySpec {
	const char *key;
	int token_count; // -1 for a key whose value is the whole rest of the line
	bool repeated;
	bool required;
};

// Every key the **current** version knows, and nothing else is accepted. A file
// that reaches the binder holding a key not in this table has not been migrated
// far enough, and saying so is the difference between ADR-0042's format and the
// best-effort loader it rejected.
inline constexpr ProfileKeySpec PROFILE_KEYS[] = {
	{ "version", 1, false, true },
	// The one free-text field: "Surname, Forename" contains a space, so its value
	// is the rest of the line rather than a token.
	{ "driver_name", -1, false, true },
	{ "driver_number", 1, false, true },
	{ "driver_nationality", 1, false, true },
	{ "driver_livery", 1, false, true },
	{ "career_class", 1, false, true },
	{ "career_season", 1, false, true },
	{ "career_round", 1, false, true },
	// Both repeated blocks are optional and empty is meaningful: a career that has
	// run no rounds has no standings, and a profile that has set no lap has no
	// bests. Absent is not the same as missing here, which is why neither is
	// `required`.
	{ "standing", 2, true, false },
	{ "best", 5, true, false },
};

inline constexpr int PROFILE_KEY_COUNT =
		static_cast<int>(sizeof(PROFILE_KEYS) / sizeof(PROFILE_KEYS[0]));

inline const ProfileKeySpec *profile_key_spec(const char *key, int len) {
	for (int i = 0; i < PROFILE_KEY_COUNT; ++i) {
		if (profile_text_equals(key, len, PROFILE_KEYS[i].key)) {
			return &PROFILE_KEYS[i];
		}
	}
	return nullptr;
}

// --- lexing -------------------------------------------------------------------

// Split text into records. Blank lines and `#` comments are dropped; a trailing
// `#` comment is stripped from a record, exactly as `tuning.h`'s `parse_line`
// does, so a save can be annotated by hand.
//
// The `version` record must be the **first** non-comment line. A reader has to be
// able to learn a file's version without understanding the rest of it, and a
// version buried below a block of records it does not describe is a file that
// cannot be migrated safely.
inline bool profile_lex(const char *text, int len, ProfileDocument &document,
		ProfileLoadResult &result) {
	document = ProfileDocument();
	if (text == nullptr) {
		result.fail(ProfileProblem::NoVersionLine, 0, nullptr, 0);
		return false;
	}
	if (len < 0) {
		len = profile_length(text);
	}

	int line_number = 0;
	int index = 0;
	bool seen_version = false;

	while (index <= len) {
		// One line, up to '\n' or the end.
		const int line_begin = index;
		while (index < len && text[index] != '\n') {
			++index;
		}
		const int line_end = index;
		++index; // Past the newline, or past the end.
		++line_number;
		if (line_begin > len) {
			break;
		}

		int begin = line_begin;
		int end = line_end;
		while (begin < end && profile_is_space(text[begin])) {
			++begin;
		}
		while (end > begin && profile_is_space(text[end - 1])) {
			--end;
		}
		if (begin == end) {
			if (line_end >= len) {
				break;
			}
			continue;
		}
		if (text[begin] == '#') {
			if (line_end >= len) {
				break;
			}
			continue;
		}
		// Strip a trailing comment.
		for (int i = begin; i < end; ++i) {
			if (text[i] == '#') {
				end = i;
				break;
			}
		}
		while (end > begin && profile_is_space(text[end - 1])) {
			--end;
		}
		if (begin == end) {
			if (line_end >= len) {
				break;
			}
			continue;
		}

		int key_end = begin;
		while (key_end < end && !profile_is_space(text[key_end])) {
			++key_end;
		}
		const int key_len = key_end - begin;
		if (key_len >= PROFILE_KEY_CHARS) {
			result.fail(ProfileProblem::TooLong, line_number, text + begin, key_len);
			return false;
		}
		int value_begin = key_end;
		while (value_begin < end && profile_is_space(text[value_begin])) {
			++value_begin;
		}
		const int value_len = end - value_begin;
		if (value_len >= PROFILE_TEXT_CHARS) {
			result.fail(ProfileProblem::TooLong, line_number, text + begin, key_len);
			return false;
		}

		if (!seen_version) {
			if (!profile_text_equals(text + begin, key_len, "version")) {
				result.fail(ProfileProblem::VersionNotFirst, line_number, text + begin, key_len);
				return false;
			}
			int declared = 0;
			if (!profile_parse_int(text + value_begin, value_len, declared)) {
				result.fail(ProfileProblem::BadVersionNumber, line_number,
						text + value_begin, value_len);
				return false;
			}
			if (declared < 1) {
				result.declared_version = declared;
				result.fail(ProfileProblem::BadVersionNumber, line_number,
						text + value_begin, value_len);
				return false;
			}
			document.declared_version = declared;
			result.declared_version = declared;
			seen_version = true;
		}

		if (document.record_count >= PROFILE_MAX_RECORDS) {
			result.fail(ProfileProblem::TooManyRecords, line_number, text + begin, key_len);
			return false;
		}
		ProfileRecord &slot = document.record[document.record_count++];
		slot = ProfileRecord();
		for (int i = 0; i < key_len; ++i) {
			slot.key[i] = text[begin + i];
		}
		for (int i = 0; i < value_len; ++i) {
			slot.text[i] = text[value_begin + i];
		}
		slot.line = line_number;

		if (line_end >= len) {
			break;
		}
	}

	if (!seen_version) {
		result.fail(ProfileProblem::NoVersionLine, 0, nullptr, 0);
		return false;
	}
	return true;
}

// --- binding ------------------------------------------------------------------

// Turn a migrated document into a profile, strictly. Every key must be known,
// every required key present, every singleton unique, every record the right
// width, every value in range.
inline bool profile_bind(const ProfileDocument &document, Profile &out,
		ProfileLoadResult &result) {
	out = Profile();

	bool seen[PROFILE_KEY_COUNT] = {};

	for (int i = 0; i < document.record_count; ++i) {
		const ProfileRecord &record = document.record[i];
		const int key_len = profile_length(record.key);
		const ProfileKeySpec *spec = profile_key_spec(record.key, key_len);
		if (spec == nullptr) {
			// The ADR's own example lands here. Rename `standing` to `championship`
			// without a migration and this fires, naming the key, rather than the
			// career quietly resetting to empty.
			result.fail(ProfileProblem::UnknownKey, record.line, record.key, key_len);
			return false;
		}
		const int spec_index = static_cast<int>(spec - PROFILE_KEYS);
		if (seen[spec_index] && !spec->repeated) {
			result.fail(ProfileProblem::DuplicateField, record.line, record.key, key_len);
			return false;
		}
		seen[spec_index] = true;

		const int text_len = profile_length(record.text);
		ProfileTokens tokens;
		if (spec->token_count < 0) {
			// A free-text value is never tokenized. Doing it anyway would make a
			// six-word driver name a `WrongTokenCount` failure, which is a save
			// refused for having a long name in it.
			if (text_len == 0) {
				result.fail(ProfileProblem::WrongTokenCount, record.line, record.key, key_len);
				return false;
			}
		} else {
			if (!profile_tokenize(record.text, text_len, tokens)) {
				result.fail(ProfileProblem::WrongTokenCount, record.line, record.key, key_len);
				return false;
			}
			if (tokens.count != spec->token_count) {
				result.fail(ProfileProblem::WrongTokenCount, record.line, record.key, key_len);
				return false;
			}
		}

		if (profile_text_equals(record.key, key_len, "version")) {
			// Already read by the lexer and already migrated. Re-reading it here would
			// let a document carry two versions.
			continue;
		}
		if (profile_text_equals(record.key, key_len, "driver_name")) {
			if (!profile_is_display_name(record.text, text_len)) {
				result.fail(ProfileProblem::BadIdentifier, record.line, record.text, text_len);
				return false;
			}
			for (int c = 0; c < text_len; ++c) {
				out.driver.name[c] = record.text[c];
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "driver_number")) {
			if (!profile_parse_int(tokens.begin[0], tokens.length[0], out.driver.number)) {
				result.fail(ProfileProblem::BadValue, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "driver_nationality")) {
			if (!profile_is_nationality(tokens.begin[0], tokens.length[0])) {
				result.fail(ProfileProblem::BadIdentifier, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			for (int c = 0; c < 3; ++c) {
				out.driver.nationality[c] = tokens.begin[0][c];
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "driver_livery")) {
			if (!profile_is_slug(tokens.begin[0], tokens.length[0])) {
				result.fail(ProfileProblem::BadIdentifier, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			for (int c = 0; c < tokens.length[0]; ++c) {
				out.driver.livery[c] = tokens.begin[0][c];
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "career_class")) {
			if (!profile_parse_kart_class(tokens.begin[0], tokens.length[0],
						out.career.kart_class)) {
				result.fail(ProfileProblem::BadEnum, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "career_season")) {
			if (!profile_parse_int(tokens.begin[0], tokens.length[0], out.career.season)) {
				result.fail(ProfileProblem::BadValue, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "career_round")) {
			if (!profile_parse_int(tokens.begin[0], tokens.length[0], out.career.round)) {
				result.fail(ProfileProblem::BadValue, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "standing")) {
			char driver_id[PROFILE_SLUG_CHARS] = {};
			if (!profile_is_slug(tokens.begin[0], tokens.length[0])) {
				result.fail(ProfileProblem::BadIdentifier, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			for (int c = 0; c < tokens.length[0]; ++c) {
				driver_id[c] = tokens.begin[0][c];
			}
			double points = 0.0;
			if (!parse_value(tokens.begin[1], tokens.length[1], points)) {
				result.fail(ProfileProblem::BadValue, record.line, tokens.begin[1],
						tokens.length[1]);
				return false;
			}
			if (!out.add_standing(driver_id, points)) {
				// A duplicate driver, a negative points total, or a table larger than
				// a grid. `add_standing` is the one writer and it refuses all three.
				result.fail(ProfileProblem::BadValue, record.line, driver_id, -1);
				return false;
			}
			continue;
		}
		if (profile_text_equals(record.key, key_len, "best")) {
			char track_id[SESSION_ID_CHARS] = {};
			if (!profile_is_track_slug(tokens.begin[0], tokens.length[0])) {
				result.fail(ProfileProblem::BadIdentifier, record.line, tokens.begin[0],
						tokens.length[0]);
				return false;
			}
			for (int c = 0; c < tokens.length[0]; ++c) {
				track_id[c] = tokens.begin[0][c];
			}
			TrackLayout layout = TrackLayout::Forward;
			if (!profile_parse_track_layout(tokens.begin[1], tokens.length[1], layout)) {
				result.fail(ProfileProblem::BadEnum, record.line, tokens.begin[1],
						tokens.length[1]);
				return false;
			}
			KartClass kart_class = KartClass::KZ2;
			if (!profile_parse_kart_class(tokens.begin[2], tokens.length[2], kart_class)) {
				result.fail(ProfileProblem::BadEnum, record.line, tokens.begin[2],
						tokens.length[2]);
				return false;
			}
			double lap_time_s = 0.0;
			if (!parse_value(tokens.begin[3], tokens.length[3], lap_time_s)) {
				result.fail(ProfileProblem::BadValue, record.line, tokens.begin[3],
						tokens.length[3]);
				return false;
			}
			char ghost_id[PROFILE_SLUG_CHARS] = {};
			if (!profile_is_slug(tokens.begin[4], tokens.length[4])) {
				result.fail(ProfileProblem::BadIdentifier, record.line, tokens.begin[4],
						tokens.length[4]);
				return false;
			}
			for (int c = 0; c < tokens.length[4]; ++c) {
				ghost_id[c] = tokens.begin[4][c];
			}
			// A duplicate key would be silently absorbed as "not an improvement", so
			// it is caught here rather than by `set_best`.
			if (out.find_best(track_id, layout, kart_class) != nullptr) {
				result.fail(ProfileProblem::DuplicateField, record.line, track_id, -1);
				return false;
			}
			if (!out.set_best(track_id, layout, kart_class, lap_time_s, ghost_id)) {
				result.fail(ProfileProblem::BadValue, record.line, track_id, -1);
				return false;
			}
			continue;
		}

		// Unreachable: `profile_key_spec` accepted the key, so one of the branches
		// above handles it. Present so that adding a key to `PROFILE_KEYS` and
		// forgetting the branch is a load failure naming the key rather than a field
		// that silently stays at its default, which is the whole thing ADR-0042 is
		// buying.
		result.fail(ProfileProblem::UnknownKey, record.line, record.key, key_len);
		return false;
	}

	for (int i = 0; i < PROFILE_KEY_COUNT; ++i) {
		if (PROFILE_KEYS[i].required && !seen[i]) {
			result.fail(ProfileProblem::MissingField, 0, PROFILE_KEYS[i].key, -1);
			return false;
		}
	}

	if (!out.is_valid()) {
		result.fail(ProfileProblem::Invalid, 0, nullptr, 0);
		return false;
	}
	return true;
}

// --- loading ------------------------------------------------------------------

// Lex, migrate, bind. `out` is only written on success — a caller that got a
// `Corrupt` back has its previous profile untouched, which matters because the
// answer to a corrupt file is to start a fresh profile deliberately rather than
// to inherit half of one.
//
// The table is an argument so the machinery is testable against a synthetic chain
// while the production chain is genuinely empty. `load_profile` below is the call
// everything real makes.
inline ProfileLoadResult load_profile_with(const char *text, int len, Profile &out,
		const ProfileMigration *table, int count, int to_version) {
	ProfileLoadResult result;
	ProfileDocument document;
	if (!profile_lex(text, len, document, result)) {
		return result;
	}
	if (!profile_run_migrations(document, to_version, table, count, result)) {
		return result;
	}
	Profile parsed;
	if (!profile_bind(document, parsed, result)) {
		return result;
	}
	out = parsed;
	result.status = ProfileLoadStatus::Ok;
	result.problem = ProfileProblem::None;
	return result;
}

inline ProfileLoadResult load_profile(const char *text, int len, Profile &out) {
	return load_profile_with(text, len, out, PROFILE_MIGRATIONS, PROFILE_MIGRATION_COUNT,
			PROFILE_FORMAT_VERSION);
}

// --- corruption, and where the old file goes ----------------------------------
//
// **A file that does not parse is moved aside, not overwritten.** The failure this
// prevents is the ordinary one, not an exotic one: something goes wrong at load,
// the game starts fresh, the player plays for an hour, and the first successful
// save destroys the only copy of the evidence. So `profile.save` becomes
// `profile.save.corrupt.1`, a fresh profile is started, and **the game says where
// the old one went** — a moved file nobody was told about is a deleted file with
// extra steps.
//
// The naming is logic and lives here. The rename does not. The Godot side lists
// the directory, runs each name through `profile_corrupt_index_of`, takes the
// highest, and asks `profile_next_corrupt_index` for the next one, so a second
// corruption does not overwrite the first — which would be the same bug one level
// up.

inline constexpr const char *PROFILE_CORRUPT_INFIX = ".corrupt.";
inline constexpr const char *PROFILE_TEMP_SUFFIX = ".tmp";

// `profile.save` -> `profile.save.tmp`. Step 2 of the atomic write.
inline int profile_temp_name(const char *base, char *out, int cap) {
	const int base_len = profile_length(base);
	const int suffix_len = profile_length(PROFILE_TEMP_SUFFIX);
	if (base == nullptr || base_len == 0 || base_len + suffix_len + 1 > cap) {
		return -1;
	}
	for (int i = 0; i < base_len; ++i) {
		out[i] = base[i];
	}
	for (int i = 0; i < suffix_len; ++i) {
		out[base_len + i] = PROFILE_TEMP_SUFFIX[i];
	}
	out[base_len + suffix_len] = '\0';
	return base_len + suffix_len;
}

// `profile.save`, 1 -> `profile.save.corrupt.1`. Indices start at 1 because
// `.corrupt.0` reads as "no corruptions" to everybody who sees it.
inline int profile_corrupt_name(const char *base, int index, char *out, int cap) {
	if (base == nullptr || index < 1) {
		return -1;
	}
	const int base_len = profile_length(base);
	const int infix_len = profile_length(PROFILE_CORRUPT_INFIX);
	if (base_len == 0) {
		return -1;
	}
	char digits[16];
	const int digit_len = format_int(index, digits, sizeof(digits));
	if (digit_len < 0) {
		return -1;
	}
	const int total = base_len + infix_len + digit_len;
	if (total + 1 > cap) {
		return -1;
	}
	int written = 0;
	for (int i = 0; i < base_len; ++i) {
		out[written++] = base[i];
	}
	for (int i = 0; i < infix_len; ++i) {
		out[written++] = PROFILE_CORRUPT_INFIX[i];
	}
	for (int i = 0; i < digit_len; ++i) {
		out[written++] = digits[i];
	}
	out[written] = '\0';
	return written;
}

// The index in a name this function's counterpart produced, or -1 if the name is
// not one of ours. Strict about the digits: `.corrupt.007` and `.corrupt.+3` are
// names we never wrote, and treating them as 7 and 3 would mean a directory
// listing reported a highest index that no file actually holds.
inline int profile_corrupt_index_of(const char *base, const char *name) {
	if (base == nullptr || name == nullptr) {
		return -1;
	}
	const int base_len = profile_length(base);
	const int infix_len = profile_length(PROFILE_CORRUPT_INFIX);
	const int name_len = profile_length(name);
	if (base_len == 0 || name_len <= base_len + infix_len) {
		return -1;
	}
	for (int i = 0; i < base_len; ++i) {
		if (name[i] != base[i]) {
			return -1;
		}
	}
	for (int i = 0; i < infix_len; ++i) {
		if (name[base_len + i] != PROFILE_CORRUPT_INFIX[i]) {
			return -1;
		}
	}
	const char *digits = name + base_len + infix_len;
	const int digit_len = name_len - base_len - infix_len;
	if (digit_len <= 0 || digits[0] == '0') {
		return -1;
	}
	int index = 0;
	for (int i = 0; i < digit_len; ++i) {
		if (digits[i] < '0' || digits[i] > '9') {
			return -1;
		}
		index = index * 10 + (digits[i] - '0');
		if (index > 1000000) {
			return -1;
		}
	}
	return index >= 1 ? index : -1;
}

// The next free index given the highest one already on disk. Zero or negative
// means there are none.
inline int profile_next_corrupt_index(int highest_seen) {
	return highest_seen < 1 ? 1 : highest_seen + 1;
}

// --- what a fresh profile is --------------------------------------------------
//
// A save that would not load is worse than no save, so the fallback after a
// corruption is a profile that satisfies `is_valid()` by construction. The name
// and number are placeholders a first-run screen is expected to replace; they are
// here so that "start fresh" is one call rather than eight assignments that a
// caller can get one of wrong.
//
// `OK` and season 0, round 0, because `GAMEDESIGN.md` §5's ladder starts in the
// direct-drive class. Not KZ2, which is the class this repository can currently
// simulate — a fresh career starting in the class the career exists to promote
// *into* would be the ladder skipped by default.
inline Profile fresh_profile() {
	Profile profile;
	profile.set_driver_name("New Driver");
	profile.driver.number = 101;
	profile.set_nationality("GBR");
	profile.set_livery("works_blue");
	profile.career.kart_class = KartClass::OK;
	profile.career.season = 0;
	profile.career.round = 0;
	return profile;
}

} // namespace kart::core

#endif // KART_CORE_PROFILE_H
