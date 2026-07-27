#ifndef KARTGAME_SESSION_KART_SESSION_H
#define KARTGAME_SESSION_KART_SESSION_H

#include "core/lap_timing.h"
#include "core/session.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace kartgame {

// The shell's view of a session. ROADMAP M3c, and the seam `docs/GAMEDESIGN.md`
// §9 means by "the shell owns no simulation state".
//
// `src/core/session.h` is the type; this is the only way GDScript can hold one.
// Two classes, and the split is the same one `KartTuning` already makes: a value
// object a menu can fill in and hand over, and a timer a scene feeds once per
// tick. Neither is a Node, because neither belongs in a tree — a session
// configuration is an argument and a lap timer is one per kart.
//
// ## Why the config is a RefCounted and not a Dictionary
//
// A Dictionary would be less code here and it would move the schema into every
// caller. `SessionConfig::hash()` is the whole reason this type exists — a replay
// compares it, the rules classify against it, and ADR-0041's refusal names the
// field that moved — and a Dictionary cannot be hashed that way without somebody
// agreeing, in GDScript, on key names and iteration order. `session.h`'s own
// comment says a hash over values alone reports two different schemas as the same
// configuration; a Dictionary is exactly that hazard with a friendlier face.
//
// So the setters are typed, the enums are bound as constants, and an invalid
// value is refused at the boundary rather than travelling as an integer nobody
// checked. `to_dictionary` exists for a UI that wants to lay the fields out in a
// table, and it is one-way on purpose.
//
// **Every setter returns whether it took the value.** Six of them used to return
// `void` and only print, which made the sentence above half true: the value was
// refused and the caller was not told, on exactly the setters where a wrong integer
// is otherwise silent. A setup screen validating by return value got false
// confidence from the six that could not give it.
class KartSession : public godot::RefCounted {
	GDCLASS(KartSession, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// The `session.h` enums, as class constants, so a menu writes
	// `KartSession.TYPE_HEAT` and not a bare `2`. A typo on the name is an error at
	// parse time; a wrong integer is a silently different session.
	enum {
		TYPE_PRACTICE = static_cast<int>(kart::core::SessionType::Practice),
		TYPE_QUALIFYING = static_cast<int>(kart::core::SessionType::Qualifying),
		TYPE_HEAT = static_cast<int>(kart::core::SessionType::Heat),
		TYPE_SUPER_HEAT = static_cast<int>(kart::core::SessionType::SuperHeat),
		TYPE_FINAL = static_cast<int>(kart::core::SessionType::Final),

		CLASS_OK = static_cast<int>(kart::core::KartClass::OK),
		CLASS_KZ2 = static_cast<int>(kart::core::KartClass::KZ2),

		LAYOUT_FORWARD = static_cast<int>(kart::core::TrackLayout::Forward),
		LAYOUT_REVERSE = static_cast<int>(kart::core::TrackLayout::Reverse),

		// One value, and `session.h` says why: nothing models water, and a `Wet`
		// constant here would advertise an intention the code does not honor.
		CONDITION_DRY = static_cast<int>(kart::core::TrackCondition::Dry),

		LIMIT_OPEN = static_cast<int>(kart::core::SessionLimitKind::Open),
		LIMIT_LAPS = static_cast<int>(kart::core::SessionLimitKind::Laps),
		LIMIT_DISTANCE = static_cast<int>(kart::core::SessionLimitKind::Distance),
		LIMIT_DURATION = static_cast<int>(kart::core::SessionLimitKind::Duration),

		// The sanity ceiling, not the design's field size — ADR-0046 makes the grid
		// count data, and `GAMEDESIGN.md` §6's eight karts came from an audio
		// measurement that is expected to move it.
		MAX_ENTRIES = kart::core::SESSION_MAX_ENTRIES,
	};

	// --- content ---------------------------------------------------------------

	// Refuses a slug that does not fit `SESSION_ID_CHARS`, rather than truncating
	// silently: a truncated track id fails to resolve at load, a long way from here.
	bool set_track(const godot::String &p_slug);
	godot::String get_track() const;

	// The content hash of `track.json`, per ADR-0046. Taken as a String because
	// GDScript has no unsigned 64-bit integer and a hash that arrives as a negative
	// number is a hash somebody will "fix" by taking its absolute value. Hex, with
	// or without the `0x`.
	bool set_track_hash_hex(const godot::String &p_hex);
	godot::String get_track_hash_hex() const;

	bool set_layout(int p_layout);
	int get_layout() const;

	bool set_condition(int p_condition);
	int get_condition() const;

	// --- what kind of session ---------------------------------------------------

	bool set_type(int p_type);
	int get_type() const;

	bool set_kart_class(int p_kart_class);
	int get_kart_class() const;

	// The limit, as the pair it is. Setting the kind alone leaves the value, which
	// is why `use_scheduled_limit` exists: a Heat's distance is arithmetic on a
	// sourced FIA figure and typing 3750 into a menu is how that citation gets lost.
	bool set_limit(int p_kind, double p_value);
	int get_limit_kind() const;
	double get_limit_value() const;
	void use_scheduled_limit();

	// --- the field --------------------------------------------------------------

	bool set_entry_count(int p_count);
	int get_entry_count() const;

	bool set_roster_hash_hex(const godot::String &p_hex);
	godot::String get_roster_hash_hex() const;

	// --- the run ----------------------------------------------------------------

	void set_auto_clutch(bool p_enabled);
	bool is_auto_clutch() const;
	void set_auto_shift(bool p_enabled);
	bool is_auto_shift() const;

	bool set_seed_hex(const godot::String &p_hex);
	godot::String get_seed_hex() const;

	// Take the tuning from a `KartTuning` node, so a session records the preset it
	// was driven under. Passed as an `Object *` rather than a typed pointer to keep
	// this header from including the tuning bridge; the cast is checked.
	bool adopt_tuning(godot::Object *p_tuning);

	// --- the whole thing --------------------------------------------------------

	// ADR-0041's `config_hash`, as hex. The one number that says two sessions are
	// the same session.
	godot::String config_hash_hex() const;

	// Whether this describes a session that can be run. A menu calls it before
	// enabling its start button, and the runner calls it again — `session.h` puts
	// validation in a function precisely so both can.
	bool is_valid() const;

	// The field that differs from another configuration, as a name, or an empty
	// String when they agree. ADR-0041's refusal message in one call.
	godot::String first_difference_with(const godot::Ref<KartSession> &p_other) const;

	// One field's value as text, by the name `session_field_name` uses. For a setup
	// screen that lists the configuration back to the player.
	godot::String field_value(const godot::String &p_field) const;

	// Every field, as a Dictionary of name to text. Read-only view for a table.
	godot::Dictionary to_dictionary() const;

	// The names, in the order they are hashed, so a UI can lay them out without
	// hardcoding a list that will drift from the schema.
	static godot::PackedStringArray field_names();

	// Human-readable names for the enums, for a menu that has to label a row.
	static godot::String type_name(int p_type);
	static godot::String kart_class_name(int p_kart_class);
	static godot::String layout_name(int p_layout);
	static godot::String limit_kind_name(int p_kind);

	// Not bound. The runner and the recorder want the real struct.
	const kart::core::SessionConfig &config() const { return config_; }
	kart::core::SessionConfig &config() { return config_; }

private:
	kart::core::SessionConfig config_;
};

// One kart's laps and sectors. `src/core/lap_timing.h` is the timer; this hands it
// a position once per tick and reads the results back out.
//
// **The scene computes arc length, not this class.** Projecting a kart onto the
// centerline needs the track, and the track is a GDScript object
// (`scripts/track/track_layout.gd`) that this header must not know about. So the
// contract is a number in meters, which is also exactly what a replay would feed
// it and what a unit test already does.
//
// Times come out as seconds and as `LAP_TIME_NONE` — a negative number — when
// there is no lap yet. Not zero: `lap_timing.h` explains that a zero best lap
// sorts first and can never be beaten. GDScript callers check `has_best()` rather
// than comparing against a magic number.
class KartLapTimer : public godot::RefCounted {
	GDCLASS(KartLapTimer, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// Even sectors on a closed lap of `length_m`. What a circuit gets before
	// anybody has authored splits for it, and `lap_timing.h` says so.
	bool begin_even(double p_length_m, int p_sectors, double p_step_s);

	// Authored marks, in meters from the start line, ascending, first at zero.
	// Refuses a layout that cannot be timed rather than reporting nonsense splits.
	bool begin_marks(const godot::PackedFloat64Array &p_marks, double p_length_m, double p_step_s);

	void reset();

	// One physics tick. `off_track` is the FIA's definition and nothing else: all
	// four wheels outside the line. Returns true on the tick a lap completes.
	bool advance(double p_distance_m, bool p_off_track);

	// The kart was put back on the track by something other than the driver, which
	// is a real disqualification and not a courtesy.
	void respawn(double p_distance_m);

	// A qualifying penalty deletes the driver's *fastest* lap, not the current one.
	// Returns the deleted time, or a negative number if there was nothing to delete.
	double invalidate_fastest();

	// --- reading it back --------------------------------------------------------

	double lap_time() const;
	int sector() const;
	double sector_time() const;
	int laps_completed() const;
	double distance() const;
	godot::String current_reason() const;

	bool has_last() const;
	double last_time() const;
	godot::PackedFloat64Array last_sectors() const;
	godot::String last_reason() const;
	bool last_was_valid() const;

	bool has_best() const;
	double best_time() const;
	godot::PackedFloat64Array best_sectors() const;

	double best_sector(int p_index) const;
	double optimal_lap() const;
	int valid_lap_count() const;
	int invalid_lap_count() const;

	// Split against the best lap so far, negative when ahead.
	//
	// **Ask `has_sector_delta_to_best()` first.** Negative means ahead and
	// `LAP_TIME_NONE` is -1.0, so "one second up on your best" and "there is nothing
	// to compare against" are the same number. That is a live trap rather than a
	// theoretical one: the first HUD written against this had to guard it, and a HUD
	// that forgot would show a plausible -1.000 for the whole session. The predicate
	// is here so the guard is one call rather than a convention.
	double sector_delta_to_best() const;
	bool has_sector_delta_to_best() const;

	// Whether this session type invalidates a lap for running wide — ours, and only
	// in the timed sessions — and whether a penalty deletes the fastest lap instead.
	static bool type_invalidates_laps(int p_type);
	static bool type_deletes_fastest_lap(int p_type);

	// `race_rules.h`'s sequencing predicates, forwarded.
	//
	// **They are here because the session runner was otherwise re-deciding them.**
	// ADR-0043 put the progression in `src/core/` as a pure state machine precisely
	// so that GDScript is left with loading a scene, drawing a table and moving a
	// focus ring — and then nothing bound them, so the first runner written against
	// this bridge compared against `TYPE_PRACTICE` by hand. That is a second reader
	// of a fact the ADR gave one owner, which is the shape of defect this project
	// keeps finding in its documents.
	static bool type_has_field(int p_type);
	static bool type_awards_championship_points(int p_type);
	static bool type_sets_next_grid(int p_type);

	// Minimum full laps that reach a distance, which is a ceiling and not a
	// division. `GAMEDESIGN.md` §4's lap column was written as a floor and was one
	// lap short on every race because of it.
	static int laps_for_distance(double p_distance_m, double p_lap_length_m);

	kart::core::LapTimer &timer() { return timer_; }

private:
	kart::core::LapTimer timer_;
	kart::core::LapMarks marks_;
	bool started_ = false;
};

} // namespace kartgame

#endif // KARTGAME_SESSION_KART_SESSION_H
