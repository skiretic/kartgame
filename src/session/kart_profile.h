#ifndef KARTGAME_SESSION_KART_PROFILE_H
#define KARTGAME_SESSION_KART_PROFILE_H

#include "core/profile.h"
#include "core/settings.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace kartgame {

// The career save and the settings file, on the Godot side. ROADMAP M3c,
// `docs/DECISIONS.md` ADR-0042, `docs/GAMEDESIGN.md` §8.
//
// `src/core/profile.h` is the format: the parser, the version, the migration
// chain, the corruption naming and the atomic-write *sequence*, all as pure logic
// with no file I/O anywhere in it. This is the other half — `user://` resolution,
// the write itself, and the two policies that only mean something once a
// filesystem is involved: a corrupt file gets moved aside before anything else
// touches it, and a file from a newer build gets left completely alone.
//
// Two classes, and the separation is the whole of ADR-0042's second decision
// rather than tidiness. See "Why settings are a second class" below.
//
// ## What `user://` resolves to
//
// `project.godot` sets `application/config/name` to `kartgame` and does not set
// `application/config/use_custom_user_dir`, so on macOS:
//
//     user://  ->  ~/Library/Application Support/Godot/app_userdata/kartgame/
//
// On Linux it is `~/.local/share/godot/app_userdata/kartgame/`. Nothing below
// hardcodes either: paths are built as `user://` strings and handed to
// `FileAccess` and `DirAccess`, which do the resolution. `globalize_path()` is
// bound as `system_path()` for exactly one purpose — printing the real directory
// in a diagnostic, because "your save was moved to profile.save.corrupt.1" is not
// actionable without saying which directory that is.
//
//     user://profile.save              the career
//     user://profile.save.tmp          the temporary, mid-write, never left behind
//     user://profile.save.corrupt.N    a file that would not parse
//     user://settings.cfg              comfort, controls, assists
//     user://ghosts/<id>.ghost         referenced by a best, never inlined
//
// The ghost directory's *contents* are somebody else's; this class owns only where
// a `best` points, and `ghost_path()` is the one function that turns a stored id
// into a path. It refuses an id that is not a slug, which is not fussiness:
// `profile.h` says a save file is user-writable and a ghost id gets pasted into a
// path, so `../../` in one is the difference between a save format and a
// file-deletion primitive.
//
// ## The atomic write, and what it actually guarantees
//
// The sequence is `profile.h`'s: serialize whole into memory, write it to
// `profile.save.tmp`, flush, close, then `DirAccess::rename` the temporary **over**
// the target. `profile.save` is never opened for writing. `tools/verify/profile_probe.gd`
// proves that last claim from outside rather than by reading this file: it sets the
// target to mode 0444 and saves, which succeeds — POSIX `rename` needs write
// permission on the *directory*, not on the file being replaced, so an
// implementation that opened the target would have failed there.
//
// **What is guaranteed, and it is worth being exact because ADR-0042 says
// "fsync" and Godot's `FileAccess` does not expose one.** That is checked against
// the API definition rather than remembered: `third_party/godot-cpp/gdextension/extension_api.json`
// lists 68 methods on `FileAccess` and the only two that touch durability are
// `flush` and `close`. There is no `fsync`, no `F_FULLFSYNC`, no `O_SYNC` mode flag,
// and no binding a GDExtension can reach one through. So:
//
//   * **A process death cannot produce a half-written save.** The temporary is
//     written, flushed and **closed**, and its length is read back off disk through
//     a second handle before the rename is issued — so the complete bytes are
//     visible to a separate reader at the moment the rename happens. The rename is
//     one operation that either has happened or has not. Kill the game at any
//     point and `profile.save` is either the previous save entire or the new one
//     entire. This is the failure that actually happens — a crash, a force quit, a
//     closed lid mid-session — and it is closed. `profile_probe.gd` check 11
//     builds exactly that state, a written temporary with no rename, and measures
//     the target unchanged.
//   * **A kernel panic or a power cut leaves either the old complete save or the
//     new complete one.** This was the open half for a milestone — a close is not
//     a sync, and without one POSIX does not order the file's data against the
//     directory entry that names it, so the rename could become durable while
//     the contents had not. Issue #173's shim closed it: `fsync_shim.h` syncs
//     the temporary to the medium (`F_FULLFSYNC` on macOS, `fsync` elsewhere —
//     the shim's header says why they differ) between the close and the rename,
//     and syncs the directory after it. A filesystem that refuses the sync
//     downgrades that save to the process-death guarantee, with a warning in the
//     save report, rather than pretending.
//
// What is still not claimed: that a save which never reported OK exists. A
// power cut before the rename loses the new save and keeps the old one, which
// is the correct reading of "atomic" and not a gap.
//
// **`FileAccess::create_temp` is the wrong tool here and this is the note that
// stops it being a simplification later.** It exists as of 4.7 and it puts the file
// in the OS temporary directory, which is routinely a different filesystem from
// `user://`; `rename` across filesystems fails with `EXDEV`, so the atomicity would
// be gone and the failure would be a save that works on one machine. The temporary
// has to be a sibling of the target, which is what `profile_temp_name` gives, and
// its deterministic name is a second small feature: a `profile.save.tmp` left in the
// directory is recognizable as the aftermath of a crash rather than as a mystery.
//
// ## Five load statuses, where `profile.h` has three
//
// `ProfileLoadStatus` distinguishes `Ok`, `Corrupt` and `FutureVersion`, which is
// the complete set of things that can go wrong *to a string*. A filesystem adds
// two more, and both of them are cases where doing the `Corrupt` thing would
// destroy an intact save:
//
//   * `LOAD_NO_FILE` — a first run. Nothing to move aside, nothing wrong, a fresh
//     profile is correct. Reporting this as corruption would put a
//     `profile.save.corrupt.1` in front of a player who had never saved.
//   * `LOAD_UNREADABLE` — the file is there and could not be opened. A permission
//     problem, a directory where a file should be, a disk that went away. The
//     profile is left untouched and **the file is not moved**, because the one
//     thing known about it is that its contents have not been read, and moving a
//     file whose bytes were never examined is guessing that it is broken. The save
//     is blocked too, and that is not symmetry for its own sake: a rename over the
//     file would *succeed*, so without the block the next save destroys what may
//     have been an intact season on the strength of a permission problem that
//     lasted one launch.
//
// `LOAD_OK`, `LOAD_CORRUPT` and `LOAD_FUTURE_VERSION` hold the same integer values
// as the `ProfileLoadStatus` members they mirror, so the mapping is one static
// assert rather than a switch somebody has to keep in step.
//
// ## Corruption is moved aside, and a failed move blocks the save
//
// A file that does not parse becomes `profile.save.corrupt.N` — the next free N,
// found by listing the directory and running every name through
// `profile_corrupt_index_of`, so a second corruption lands at `.2` rather than
// overwriting the first. Then a fresh profile is started and the caller is told
// the path it went to. ADR-0042's reason is the ordinary failure, not an exotic
// one: something goes wrong at load, the game starts fresh, the player plays for
// an hour, and the first successful *save* destroys the evidence.
//
// **Which is why a failed move blocks the save.** If the rename cannot be done —
// a read-only directory is the realistic case — then saving would be exactly the
// deletion the moving-aside exists to prevent, and it happens an hour later when
// nobody is looking. So `load()` sets a guard, `save()` refuses while it is set
// and says why, and `is_save_blocked()` / `save_block_reason()` let a menu tell
// the player before they spend the hour. `clear_save_block()` is the deliberate
// override for a player who has been told and does not care.
//
// A **future** version sets no guard and moves nothing. That distinction is the
// whole reason `FutureVersion` is separate from `Corrupt`: filing an intact season
// under `.corrupt.1` because somebody launched an older build is the failure the
// status exists to prevent.
//
// ## Why settings are a second class, and not a second method
//
// ADR-0042 is explicit that the separation is not tidiness: if `profile.save` is
// unreadable a player still has to reach the menu, read the text and use the pad,
// so the comfort options, the control bindings and the accessibility settings in
// `ARCHITECTURE.md` §18 must load when the career does not. Coupling them means a
// corrupt career save presents an unreadable menu for fixing it.
//
// Two `RefCounted`s make that structural rather than a promise. A boot script
// constructs a `KartSettings`, loads it, applies it, and has a usable menu before
// it has looked at a `KartProfile` at all. Nothing in `KartSettings` includes,
// reads or references the career.
//
// ## No UI text is built here
//
// ADR-0044 rule 2: UI text lives on the scene and script side, never in `src/`.
// So `load()` hands back the *facts* — a status name, a problem name, a line
// number, the offending token, the path, where a file was moved to — and GDScript
// composes the sentence. There is deliberately no `message` key, because a
// sentence assembled in C++ is a sentence no `tr()` can reach and a rule that gets
// broken once gets broken forty times.
//
// `warnings` is the exception and it is labeled: those are log lines, ASCII,
// aimed at a terminal and at `profile_probe.gd`. They are not shown on a screen.
class KartProfile : public godot::RefCounted {
	GDCLASS(KartProfile, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// The `profile.h` and `session.h` enums as class constants, for the same reason
	// `KartSession` binds its own: a menu writes `KartProfile.CLASS_KZ2` and a typo
	// on the name is an error at parse time, where a wrong integer is a silently
	// different career.
	enum {
		CLASS_OK = static_cast<int>(kart::core::KartClass::OK),
		CLASS_KZ2 = static_cast<int>(kart::core::KartClass::KZ2),

		LAYOUT_FORWARD = static_cast<int>(kart::core::TrackLayout::Forward),
		LAYOUT_REVERSE = static_cast<int>(kart::core::TrackLayout::Reverse),

		// The first three mirror `ProfileLoadStatus` and hold its values. The last
		// two are this class's, and the header says why a filesystem needs them.
		LOAD_OK = static_cast<int>(kart::core::ProfileLoadStatus::Ok),
		LOAD_CORRUPT = static_cast<int>(kart::core::ProfileLoadStatus::Corrupt),
		LOAD_FUTURE_VERSION = static_cast<int>(kart::core::ProfileLoadStatus::FutureVersion),
		LOAD_NO_FILE = 3,
		LOAD_UNREADABLE = 4,

		// Sanity ceilings, not the design's numbers. `profile.h` is explicit that
		// both exist so a parse which produced garbage can be rejected.
		MAX_STANDINGS = kart::core::PROFILE_MAX_STANDINGS,
		MAX_BESTS = kart::core::PROFILE_MAX_BESTS,

		MIN_NUMBER = kart::core::PROFILE_MIN_NUMBER,
		MAX_NUMBER = kart::core::PROFILE_MAX_NUMBER,

		FORMAT_VERSION = kart::core::PROFILE_FORMAT_VERSION,
	};

	// --- where the files are ----------------------------------------------------

	// The directory the three names hang off, `user://` by default. A trailing
	// slash is stripped so every path below is built the same way.
	//
	// It exists for two reasons and one of them is a test: `profile_probe.gd` has to
	// write, corrupt and re-read real files without going anywhere near the save of
	// whoever runs it. The other is that ADR-0042 says a second profile is "a path
	// change rather than a schema change", and this is that path.
	//
	// Refuses a directory it cannot create, rather than failing later at the write.
	bool set_base_dir(const godot::String &p_dir);
	godot::String get_base_dir() const;

	godot::String profile_path() const;
	godot::String temp_path() const;
	godot::String settings_path() const;
	godot::String ghost_dir() const;

	// `<base>/ghosts/<id>.ghost`, or an empty String if the id is not a slug. The
	// emptiness is the refusal: a caller that ignores it opens nothing rather than
	// opening something outside the directory.
	godot::String ghost_path(const godot::String &p_ghost_id) const;

	// The `.corrupt.N` name for an index, and the next free index on disk. Bound
	// because a menu that offers "recover my old save" needs to enumerate them, and
	// because the probe measures the sequence.
	godot::String corrupt_path(int p_index) const;
	int next_corrupt_index() const;
	godot::PackedStringArray corrupt_paths() const;

	// The absolute OS path for any of the above, for a diagnostic that has to tell a
	// person which directory to look in.
	static godot::String system_path(const godot::String &p_path);

	// --- the driver -------------------------------------------------------------

	// Sanitizes rather than rejects, and returns whether anything had to change, so
	// a name entry field can say "that got trimmed" without losing the save.
	// `profile.h` makes that call and gives the reason: a name with a `#` in it is a
	// typo, not an attack.
	bool set_driver_name(const godot::String &p_name);
	godot::String get_driver_name() const;

	// Everything below refuses out-of-range input and returns false. `profile.h` has
	// the validators; nothing here re-implements one.
	bool set_driver_number(int p_number);
	int get_driver_number() const;

	// Three upper-case letters, an FIA code. `gbr` is refused rather than upcased,
	// because two spellings of one country break a byte-identical round trip the
	// first time somebody hand-edits a save.
	bool set_nationality(const godot::String &p_code);
	godot::String get_nationality() const;

	bool set_livery(const godot::String &p_slug);
	godot::String get_livery() const;

	// --- where the career is ----------------------------------------------------

	bool set_career_class(int p_kart_class);
	int get_career_class() const;
	bool set_career_season(int p_season);
	int get_career_season() const;
	bool set_career_round(int p_round);
	int get_career_round() const;

	// --- the standings projection -----------------------------------------------
	//
	// A projection of `src/core/standings.h`'s runtime table, stored as
	// `{ driver_id, points }` in classification order. `profile.h` explains why it is
	// a projection and not that type, and why the id is a roster slug rather than an
	// index into `data/drivers.json`: reorder the roster and every name in a saved
	// table silently changes driver.
	//
	// **Order is meaning here.** Position in the array is position in the
	// championship, so `add_standing` appends and nothing sorts. A caller replacing
	// a table calls `clear_standings()` first.

	void clear_standings();
	bool add_standing(const godot::String &p_driver_id, double p_points);
	int standing_count() const;
	godot::String standing_driver(int p_index) const;
	double standing_points(int p_index) const;

	bool has_standing(const godot::String &p_driver_id) const;
	// Guard with `has_standing`. Returns 0.0 for an absent driver, which is also a
	// legitimate stored value — an entrant who has scored nothing is a row, not an
	// absence.
	double points_for(const godot::String &p_driver_id) const;

	// The whole table as an Array of Dictionaries, in order, for a standings screen.
	// One-way, like `KartSession::to_dictionary`.
	godot::Array standings_table() const;

	// --- best laps --------------------------------------------------------------
	//
	// Per track, per layout, per class, each with the id of the ghost that set it.
	// A slower time on an existing key is ignored and reported as success:
	// `profile.h`'s call, and the reason is that the caller finished a lap, nothing
	// failed, and a best that got worse is not a best. `has_best` before and after
	// is how a caller learns whether it improved.

	// `p_ghost_id` may be the empty String, which means the lap was set with ghost
	// recording turned off. **That is a menu row and not an edge case** — the setup
	// screen offers "Ghost: off / personal best" — and it was refused outright until
	// version 2 of the format: `profile_is_slug("")` is false, so `set_best` said no
	// and the lap time silently did not save. `profile.h`'s header has the sentinel
	// the record now uses.
	//
	// `p_pause_forgiven` is issue #186 and defaults false, so every existing caller
	// keeps its meaning and only a caller that knows about ADR-0052 §4's setting has
	// to say anything.
	bool set_best(const godot::String &p_track_id, int p_layout, int p_kart_class,
			double p_lap_time_s, const godot::String &p_ghost_id,
			bool p_pause_forgiven = false);
	bool has_best(const godot::String &p_track_id, int p_layout, int p_kart_class) const;

	// Seconds, or a negative number when there is no best. Negative rather than
	// zero, and `lap_timing.h` gives the reason in one sentence: a zero best lap
	// sorts first and can never be beaten. Callers check `has_best()`.
	double best_time(const godot::String &p_track_id, int p_layout, int p_kart_class) const;
	godot::String best_ghost_id(const godot::String &p_track_id, int p_layout,
			int p_kart_class) const;
	// The ghost id resolved through `ghost_path`, or empty when there is no best —
	// and also empty when there is a best that has no ghost, which is why a caller
	// deciding whether to offer a ghost checks this and not `has_best`.
	godot::String best_ghost_path(const godot::String &p_track_id, int p_layout,
			int p_kart_class) const;

	// Whether the stored best was set with ADR-0052 §4's pause forgiveness on.
	// False for a best that does not exist, which is the same answer a records
	// screen wants for a blank row.
	bool best_pause_forgiven(const godot::String &p_track_id, int p_layout,
			int p_kart_class) const;

	int best_count() const;
	// One row, by array index, for a records screen that lists every best without
	// knowing which tracks exist. Keys: `track`, `layout`, `kart_class`,
	// `lap_time_s`, `ghost_id`, `ghost_path`, `pause_forgiven`.
	godot::Dictionary best_at(int p_index) const;
	godot::Array bests_table() const;

	// --- the whole profile ------------------------------------------------------

	// A profile that satisfies `is_valid()` by construction, with placeholders a
	// first-run screen replaces. What a corrupt or absent save falls back to.
	void reset_to_fresh();

	bool is_valid() const;

	// Exactly what `save()` would write. For a bug report that carries a save inline,
	// and for the probe's byte-identity check.
	//
	// **`save()` is the authority and this is the diagnostic**, and there is one case
	// where they can disagree. `save()` writes the serializer's bytes straight
	// through, so it is byte-exact whatever the driver name holds; this has to decode
	// those bytes into a `godot::String`, and the name is the one field `profile.h`
	// passes through as opaque UTF-8. A file hand-edited to hold an invalid UTF-8
	// sequence in the name therefore round-trips exactly through `save()` and may not
	// through here. Never compare a file against this to decide whether to write one.
	godot::String to_text() const;

	// --- load and save ----------------------------------------------------------

	// Read `profile_path()`, apply the whole policy, and report what happened.
	// Never throws away a file it did not read, and never leaves a corrupt one where
	// the next save would land.
	//
	// Keys, all of them facts rather than sentences (ADR-0044 rule 2):
	//
	//     status            int, one of LOAD_*
	//     status_name       String, `profile_load_status_name` or ours
	//     problem           int, `ProfileProblem`
	//     problem_name      String
	//     line              int, 1-based, or 0 when the failure is not about a line
	//     declared_version  int, what the file said, even on a failure
	//     migrations        int, how many steps ran
	//     detail            String, the offending key or token, or empty
	//     path              String, the file that was read
	//     moved_to          String, empty unless a corrupt file was moved aside
	//     bytes             int, the file's length, 0 when there was no file
	//     warnings          PackedStringArray, log text
	godot::Dictionary load();

	// Serialize, write to the temporary, flush, close, rename over the target. Keys:
	//
	//     ok         bool
	//     error      int, a `godot::Error`
	//     path       String, the target
	//     temp_path  String, the temporary it went through
	//     bytes      int, how many were written
	//     warnings   PackedStringArray
	//
	// Refuses while `is_save_blocked()`, and the header says why that is not
	// paranoia.
	godot::Dictionary save();

	bool is_save_blocked() const;
	// A short ASCII log line naming what blocked it, or empty. Not UI text.
	godot::String save_block_reason() const;
	void clear_save_block();

	// Not bound. A runner or a results screen wants the real struct.
	const kart::core::Profile &profile() const { return profile_; }
	kart::core::Profile &profile() { return profile_; }

	static godot::String load_status_name(int p_status);

private:
	// Resolve a layout and a class from GDScript ints, refusing anything outside the
	// enum rather than casting it. A wrong integer here is a best lap filed under a
	// class that does not exist.
	static bool to_layout(int p_value, kart::core::TrackLayout &out);
	static bool to_kart_class(int p_value, kart::core::KartClass &out);

	const kart::core::ProfileBest *lookup_best(const godot::String &p_track_id, int p_layout,
			int p_kart_class) const;

	godot::String join(const char *p_name) const;

	kart::core::Profile profile_ = kart::core::fresh_profile();
	godot::String base_dir_ = godot::String("user://");
	godot::String save_block_;
};

// Comfort, controls and assists. The file a corrupt career must not be able to
// take down with it.
//
// ## What is in here is what exists
//
// Every key in this file has a reader. Putting keys here for settings nothing
// applies would be a save format advertising features the game does not have,
// which is the exact failure mode this project keeps having — an advertised
// control with no reader — and `GAMEDESIGN.md` §13 forbids a claim the code does
// not honor.
//
// The schema is eleven values and each one names what applies it:
//
//     assist_auto_clutch     KartBody.set_auto_clutch()      #40, defaults on
//     assist_auto_shift      KartBody.set_auto_shift()       #40, defaults on
//     camera                 which rig starts current        chase/cockpit/free
//     master_volume_db       AudioServer.set_bus_volume_db() ADR-0039 trims Master
//     steer_deadzone         InputMap.action_set_deadzone()  §18's deadzone slider
//     field_of_view_deg      chase_camera.gd, cockpit_camera.gd -- a trim, not an
//                            absolute; `settings.h` says why
//     head_motion            cockpit_camera.gd's aim lag
//     shake                  chase_camera.gd's lateral-G roll
//     horizon_lock           cockpit_camera.gd
//     motion_blur            motion_blur.gd's shutter angle
//     pause_invalidates_lap  KartLapTimer.strike_paused(), ADR-0052 sec 4
//
// The file grows a key when something grows a reader. That is still the whole
// rule; the M5f comfort block grew six readers first.
//
// ## The failure policy is the opposite of the career's, deliberately
//
// ADR-0042 rejected best-effort loading for the profile and gave a good reason:
// rename a key without a migration and every existing career silently resets to
// empty. **Settings get best-effort loading anyway**, and the asymmetry is the
// point rather than an oversight:
//
//   * An unknown key is skipped with a warning. A key from a *newer* build is the
//     realistic case, and refusing the file would leave a downgrading player
//     without a readable menu — which is the one thing the separate file exists to
//     guarantee.
//   * A bad value falls back to its default with a warning, and a missing key to
//     its default silently, because a key that has never been written is not an
//     error.
//   * A file that will not parse at all is **not** moved aside. There is no
//     evidence to preserve; five knobs are re-set in ten seconds, and a pile of
//     `settings.cfg.corrupt.N` would be noise in the directory that holds the one
//     file that matters.
//
// What makes that safe is that nothing here is a record of anything. Losing a
// season is unrecoverable; losing a volume setting is a slider.
//
// ## The first key is not `version`
//
// It is `settings_version`. `profile.h`'s lexer requires `version` as the first
// non-comment line, so a distinct first key means neither file can be loaded as the
// other: pointing this class at `profile.save` fails on the first line instead of
// half-succeeding, and pointing `KartProfile` at `settings.cfg` fails with
// `version_not_first`.
class KartSettings : public godot::RefCounted {
	GDCLASS(KartSettings, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// Bound projections of `src/core/settings.h`'s values, which owns the model
	// since issue #178. The static_casts keep this list from drifting into a
	// second spelling of the enum it mirrors.
	enum {
		CAMERA_CHASE = static_cast<int>(kart::core::SettingsCamera::Chase),
		CAMERA_COCKPIT = static_cast<int>(kart::core::SettingsCamera::Cockpit),
		CAMERA_FREE = static_cast<int>(kart::core::SettingsCamera::Free),
		CAMERA_COUNT = kart::core::SETTINGS_CAMERA_COUNT,

		SETTINGS_VERSION = kart::core::SETTINGS_FORMAT_VERSION,
	};

	bool set_base_dir(const godot::String &p_dir);
	godot::String get_base_dir() const;
	godot::String settings_path() const;
	godot::String temp_path() const;

	// #40: both default **on**, because an unassisted KZ gearbox is
	// `ARCHITECTURE.md` §19's "unplayable for newcomers" risk with the mitigation
	// removed.
	void set_auto_clutch(bool p_enabled);
	bool is_auto_clutch() const;
	void set_auto_shift(bool p_enabled);
	bool is_auto_shift() const;

	bool set_camera(int p_camera);
	int get_camera() const;
	godot::String get_camera_name() const;

	// dBFS trim on the Master bus. ADR-0039 measured the shipped rig and trimmed
	// Master to keep the room quiet; this is that number, made a preference.
	// Clamped rather than refused, because a slider cannot produce an out-of-range
	// value and a hand-edited file should not lose the rest of its keys over one.
	void set_master_volume_db(double p_db);
	double get_master_volume_db() const;

	// `project.godot` sets the steer actions to 0.15, and `player_driver.h` has the
	// table showing why: below that the entire followable range at 100 km/h is
	// inside the deadzone.
	void set_steer_deadzone(double p_deadzone);
	double get_steer_deadzone() const;

	// --- ARCHITECTURE.md §18's comfort block --------------------------------------
	//
	// All five clamp rather than refuse, for the reason the volume trim does: a
	// slider cannot produce an out-of-range value and a hand-edited file must not
	// lose the rest of its keys over one that did.
	//
	// **Every default is what the consumer already shipped**, so a player who never
	// opens the settings screen sees exactly the picture they saw before it existed.
	// `src/core/settings.h` carries the ranges and the reasoning for each.

	// A trim in degrees added to both camera rigs' static and at-speed field of
	// view. Not an absolute: the two rigs run 62-78 and 74-92 and that difference is
	// tuned, so one number cannot be both. +-20 degrees, 0.0 by default.
	void set_field_of_view_deg(double p_degrees);
	double get_field_of_view_deg() const;

	// Multipliers on the shipped intensity, 0.0 to 2.0, 1.0 by default. Zero is
	// fully off, which §18 asks for in as many words.
	void set_head_motion(double p_scale);
	double get_head_motion() const;
	void set_shake(double p_scale);
	double get_shake() const;
	// Scales `motion_blur.gd`'s 180-degree shutter, so 2.0 is the 360-degree
	// shutter that file names as the maximum and 0.0 is §18's toggle turned off.
	void set_motion_blur(double p_scale);
	double get_motion_blur() const;

	void set_horizon_lock(bool p_locked);
	bool is_horizon_lock() const;

	// ADR-0052 sec 4: pausing strikes the lap in flight. Defaults **on**, and a best
	// set with it off is marked on the saved record — issue #186, and
	// `KartProfile::set_best`'s sixth argument.
	void set_pause_invalidates_lap(bool p_enabled);
	bool is_pause_invalidates_lap() const;

	void reset_to_defaults();

	godot::String to_text() const;

	// Same shape as `KartProfile::save`, minus the corruption machinery — and it goes
	// through the same temporary-then-rename sequence, because a settings file
	// truncated by a crash is a menu that will not draw.
	godot::Dictionary save();

	// Keys: `ok`, `existed`, `declared_version`, `applied`, `path`, `bytes`,
	// `warnings`. `ok` is true whenever a usable set of settings is in hand, which
	// includes the first run and includes a file that was partly garbage — the
	// header says why that is the right answer here and the wrong one for a career.
	godot::Dictionary load();

	static godot::String camera_name(int p_camera);

private:
	godot::String join(const char *p_name) const;

	godot::String base_dir_ = godot::String("user://");

	// The model, whole. `src/core/settings.h` owns the defaults, the clamps and
	// the text format; this class owns `user://` and the write.
	kart::core::Settings settings_;
};

} // namespace kartgame

#endif // KARTGAME_SESSION_KART_PROFILE_H
