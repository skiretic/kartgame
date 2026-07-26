#ifndef KARTGAME_TUNING_TUNING_REGISTRY_H
#define KARTGAME_TUNING_TUNING_REGISTRY_H

#include "core/tuning.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace kartgame {

// The tuning boundary. ROADMAP M3b's last bullet, issue #159.
//
// `src/core/tuning.h` is the vocabulary, the file format and the audit
// arithmetic, and it cannot see Godot — ADR-0017. This node is the only thing
// between it and the engine, and like `KartBody` it does a short list of things
// and no more:
//
//   1. holds one `kart::core::TuningSet` — the live configuration,
//   2. serves the descriptor table to GDScript as Dictionaries, so the overlay
//      renders provenance without a second copy of the table,
//   3. gates a change to a **defended** tunable behind an explicit
//      acknowledgement,
//   4. pushes vehicle and controller values into `KartBody`'s existing setters
//      and emits `tuning_changed` for everything it does not own itself, and
//   5. reads and writes presets, which are diffs against the defaults.
//
// There is no tuning policy in this file. What a number means, what it may
// range over and where it came from all live in `core/tuning.h`, where a test
// reaches them with no engine running.
//
// ## Why this is a Node and not a singleton
//
// Nothing in this project is an autoload. `scripts/game/test_track.gd` and
// `proving_ground.gd` build their scenes by hand, `telemetry_panel.gd` finds its
// source through a group rather than a global, and the reason is that a headless
// probe constructs exactly the nodes it wants. A tuning singleton would exist
// during `drive.sh`, would be reachable from anything, and would be one edit away
// from a §6.4 figure measured under a preset that nothing recorded.
//
// A scene that wants tuning adds one of these and gives it a vehicle path. A
// scene that does not, has none, and `KartBody` never learns this class exists.
//
// ## The acknowledgement, which is the point of the whole system
//
// `ARCHITECTURE.md` §19 names unbounded vehicle tuning as the live risk, and
// ADR-0033 refused to retune M3a's constants rather than restore a figure by
// moving one. So a default with evidence behind it — `Sourced` or `Measured`,
// `kart::core::is_defended` — cannot be moved by turning a knob:
//
//   * `set_value` and `nudge` **refuse** and return the unchanged value, and
//     push a warning naming the citation being overridden;
//   * `acknowledge(id)` unlocks that one tunable, for this session only, and is
//     not persisted anywhere;
//   * the overlay binds it to a deliberate two-key gesture rather than to the
//     same control that moves everything else; and
//   * once moved, the tunable is written to the preset with a leading `!` and
//     the citation it overrides, by `core/tuning.h`'s formatter.
//
// The acknowledgement is per-tunable and per-session on purpose. A global
// "expert mode" would be switched on once, in the first ten minutes, and would
// then defend nothing for the rest of the project.
//
// **Loading a preset grants the acknowledgement rather than demanding it, and
// that is a decision rather than a hole.** The gesture exists to stop a knob
// from being turned absent-mindedly. A file is not absent-minded: a `!` line
// naming the citation it overrides was written down, saved, and is visible in
// every diff and every review of that file from then on. Demanding a second
// ceremony at load time would only mean a preset that does not do what it says,
// which is a worse failure than the one it would prevent.
//
// The two things that do follow from it are both handled: `load_preset` reports
// how many defended overrides the file carried, and a defended entry whose line
// is missing its `!` — a hand edit, or a file from an older build — is applied
// but warned about and is written back with the marker.
//
// ## Determinism: this node never touches the state hash
//
// `core/tuning.h`'s header has the full argument. In one line: `StateHash` asks
// whether two runs of the same configuration diverged, `TuningSet::hash()` asks
// whether this is the configuration you think it is, and mixing them makes a
// mismatch un-diagnosable. `tools/verify/tuning.sh` and `drive_probe.gd` assert
// `is_at_defaults()` before recording any §6.4 figure, so a tuned run is loud
// rather than merely different.
class KartTuning : public godot::Node {
	GDCLASS(KartTuning, godot::Node)

protected:
	static void _bind_methods();

public:
	KartTuning();

	// The group `_ready` joins, so an overlay finds this node without the scene
	// wiring a path — the mechanism `scripts/game/telemetry_panel.gd` already
	// uses for `KartBody`.
	//
	// **Named here rather than only in the `.cpp`, because it is a two-file
	// contract and this header is the one a GDScript author reads.** It was in
	// the implementation alone for exactly one afternoon and the overlay guessed
	// the wrong string from this header's prose.
	static godot::String source_group();

	void _ready() override;

	// --- the table ------------------------------------------------------------

	// Number of tunables. `kart::core::TUNABLE_COUNT`, served so GDScript does
	// not carry its own copy.
	int tunable_count() const;

	// One descriptor, as the overlay wants it. Keys, all present always:
	//
	//     id (int), key, label, unit, home, citation (String),
	//     default_value, min_value, max_value, step (float),
	//     provenance (int, 0 sourced .. 3 unsourced), provenance_name (String),
	//     defended (bool), owner (int), owner_name (String)
	//
	// `provenance` is served as **both** the int and the name because the overlay
	// colors a row by the first and prints the second, and a GDScript-side
	// `match` on a string is how a fifth provenance class would silently become
	// "unknown" instead of a compile error.
	godot::Dictionary descriptor(int p_id) const;

	// Every descriptor in declaration order, which is also file order.
	godot::Array descriptors() const;

	// -1 for an unknown key, which callers report rather than swallow.
	int id_of(const godot::String &p_key) const;

	// --- values ---------------------------------------------------------------

	double get_value(int p_id) const;

	// Clamped, quantized to 1e-6, stored, applied, and returned — so a caller
	// that displays what it set displays the truth.
	//
	// **Refuses and returns the unchanged value** if the tunable is defended and
	// has not been acknowledged this session. See the header note.
	double set_value(int p_id, double p_value);

	// `set_value(get_value(id) + steps * step)`. One press of the overlay's
	// adjust control per step; negative steps go the other way.
	double nudge(int p_id, int p_steps);

	// Back to the declared default. Never refused — putting a sourced number
	// back where the source put it is not an override and must not need a
	// ceremony to do.
	double reset_value(int p_id);

	// Every tunable back to its default, and every acknowledgement dropped.
	void reset_all();

	bool is_default(int p_id) const;
	double delta(int p_id) const;

	// --- the acknowledgement --------------------------------------------------

	bool is_defended(int p_id) const;
	bool is_acknowledged(int p_id) const;

	// Whether the last `set_value` or `nudge` was refused for want of an
	// acknowledgement.
	//
	// **This exists because a refusal and a clamp are indistinguishable without
	// it.** Both return the value unchanged, so an overlay that wanted to say
	// "refused" rather than "already at the end of its range" had to re-check
	// `defended && !acknowledged` itself — which is the UI re-deriving the one
	// rule this class is supposed to own, and the exact drift `core/tuning.h`
	// keeps `is_defended` in one place to prevent.
	//
	// Cleared by the next accepted change, by `reset_value` and by `reset_all`.
	bool last_change_refused() const;

	// Unlock one defended tunable for this session. Prints the citation being
	// overridden, at warning level, so the terminal carries a record of it even
	// if the session never saves a preset.
	void acknowledge(int p_id);

	// Re-lock it. The overlay offers this so a slip can be undone without
	// restarting.
	void withdraw_acknowledgement(int p_id);

	// --- the audit ------------------------------------------------------------

	int changed_count() const;

	// How many defended defaults have been moved. The number §19 is about, and
	// the first line `tools/verify/tuning.sh` prints.
	int defended_override_count() const;

	// "0x8f1c4a2b6d0e7391", sixteen digits, formatted C++-side because
	// GDScript's `pad_zeros` counts only digit characters and mangles a hex
	// string that starts with a letter — CLAUDE.md records the burn.
	godot::String tuning_hash_hex() const;
	godot::String default_hash_hex() const;

	// Nothing has been tuned. **The gate**: `drive_probe.gd` and every §6.4
	// measurement assert this before recording a figure, so a tuned run cannot
	// be quietly written down as a reference.
	bool is_at_defaults() const;

	// The one-command answer to "what has been tuned away from its source, and
	// by how much" — the same body the saved file carries, plus a summary line,
	// with no file needed. `tools/verify/tuning.sh` prints exactly this.
	godot::String audit_text() const;

	// --- presets --------------------------------------------------------------

	// Write a preset: the header, then one line per tunable that differs from
	// its default, in declaration order. Byte-identical for the same set, so a
	// diff between two saves is a diff between two configurations.
	//
	// Returns `godot::OK`, or the `FileAccess` error. Creates the containing
	// directory if it does not exist, because the natural place to keep these is
	// `user://tuning/` and a save that failed because a directory was missing is
	// a lost session.
	godot::Error save_preset(const godot::String &p_path, const godot::String &p_name) const;

	// Read a preset. Applies every entry it understood, then pushes everything.
	//
	// Returns a Dictionary, always with these keys:
	//
	//     ok (bool)                    the file parsed and was applied
	//     error (int)                  godot::Error, OK when ok
	//     name (String)                the preset's own name, or ""
	//     applied (int)                entries applied
	//     applied_ids (PackedInt32Array)  which ones, so a caller can say what the
	//                                  file changed rather than only how much
	//     defended (int)               of those, how many overrode a defended default
	//     defaults_hash (String)       the `defaults` header the file carried, or ""
	//     defaults_match (bool)        whether it matches this build
	//     warnings (PackedStringArray) unknown keys, clamped values, a defaults
	//                                  hash that does not match this build
	//
	// **A `defaults` hash that does not match is a warning, not a refusal.** A
	// preset written before somebody moved a default still says what it meant —
	// every line carries its own key and value — but it no longer means the same
	// thing relative to the defaults, and the reader has to be told which.
	//
	// A malformed line **fails the whole load** and applies nothing. A preset
	// that half-applied would be a kart tuned to a configuration that exists in
	// no file.
	godot::Dictionary load_preset(const godot::String &p_path);

	// The preset text, without a file. What `save_preset` would write. Ends with a
	// newline whenever it has a body, so appending a line to it is safe.
	godot::String to_text(const godot::String &p_name) const;

	// One tunable's preset line, or "" when it is at its default. The same
	// formatter `to_text` uses, per entry, so a caller checking that a defended
	// override carries its marker can assert on a line rather than searching the
	// whole file for one containing a key — a search that is exact only for as
	// long as no key is a prefix of another.
	godot::String entry_text(int p_id) const;

	// The grid every value snaps to, 1e-6. Served rather than restated, so a
	// probe comparing two values does not become a second owner of the number.
	static double quantum();

	// Parse from text. Same contract as `load_preset`.
	godot::Dictionary from_text(const godot::String &p_text);

	// --- wiring ---------------------------------------------------------------

	// The `KartBody` that `Vehicle`- and `Controller`-owned values are pushed
	// into. Empty is legal: the audit and the file format work with no kart at
	// all, which is what makes `tools/verify/tuning.sh` cheap.
	void set_vehicle_path(const godot::NodePath &p_path);
	godot::NodePath get_vehicle_path() const;

	// Push every value to its owner. Called after a load, and once from
	// `_ready`, so a scene that constructs this node already tuned does not have
	// to remember to.
	void apply_all();

	// Emitted for **every** applied value, including the ones this node pushed
	// into `KartBody` itself, so a HUD can mirror the whole configuration from
	// one connection:
	//
	//     tuning_changed(key: String, value: float, owner: int)
	//
	// `scripts/game/engine_voice_rig.gd` is the consumer that needs it — the
	// three voice constants and the two synth ones live on the GDScript side and
	// in `EngineVoiceStream`, and this node deliberately does not know how to
	// reach either.

private:
	// Push one value to whoever owns it, and emit `tuning_changed`.
	void apply(int p_id);

	kart::core::TuningSet set_;

public:
	// The live configuration, for C++ that has to record it. Not bound: GDScript
	// reaches values through `get_value` and the whole set through `to_text`, and a
	// bound copy of a 23-field struct would be a second representation of the one
	// thing this class exists to be the single owner of.
	//
	// `KartSession::adopt_tuning` is the caller: a session records the preset it was
	// driven under, per ADR-0041, because a replay that re-sims under the defaults
	// reports a determinism failure that is nothing of the sort.
	const kart::core::TuningSet &tuning_set() const { return set_; }

private:

	// Per-tunable, per-session, never persisted. See the header note on why this
	// is not a global expert mode.
	bool acknowledged_[kart::core::TUNABLE_COUNT] = {};

	// See `last_change_refused`.
	bool last_refused_ = false;

	godot::NodePath vehicle_path_;
};

} // namespace kartgame

#endif // KARTGAME_TUNING_TUNING_REGISTRY_H
