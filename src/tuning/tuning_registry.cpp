#include "tuning/tuning_registry.h"

#include "session/fsync_shim.h"
#include "vehicle/kart_body.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using kart::core::is_defended;
using kart::core::ParsedLine;
using kart::core::Provenance;
using kart::core::TUNABLE_COUNT;
using kart::core::TUNABLES;

namespace kartgame {

namespace {

// The group a scene puts this node in so an overlay can find it without being
// wired. `scripts/game/telemetry_panel.gd` finds `KartBody` the same way.
StringName tuning_source_group() {
	static const StringName group("tuning_source");
	return group;
}

StringName tuning_changed_signal() {
	static const StringName signal("tuning_changed");
	return signal;
}

bool valid_id(int p_id) {
	return p_id >= 0 && p_id < TUNABLE_COUNT;
}

// A `godot::String` from a C++ line buffer. One place, so the encoding question
// is answered once: every string in `core/tuning.h` is ASCII by construction —
// keys, labels and citations are all source literals — so UTF-8 is exact.
String from_utf8(const char *p_text) {
	return String::utf8(p_text);
}

} // namespace

double KartTuning::quantum() {
	return kart::core::TUNING_QUANTUM;
}

String KartTuning::entry_text(int p_id) const {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(String(), vformat("no tunable with id %d", p_id));
	}
	char line[kart::core::TUNING_LINE_CHARS];
	if (kart::core::format_entry(set_, p_id, line, sizeof(line)) <= 0) {
		return String();
	}
	return from_utf8(line);
}

String KartTuning::source_group() {
	return String(tuning_source_group());
}

KartTuning::KartTuning() {}

void KartTuning::_bind_methods() {
	ClassDB::bind_method(D_METHOD("tunable_count"), &KartTuning::tunable_count);
	ClassDB::bind_method(D_METHOD("descriptor", "id"), &KartTuning::descriptor);
	ClassDB::bind_method(D_METHOD("descriptors"), &KartTuning::descriptors);
	ClassDB::bind_method(D_METHOD("id_of", "key"), &KartTuning::id_of);

	ClassDB::bind_method(D_METHOD("get_value", "id"), &KartTuning::get_value);
	ClassDB::bind_method(D_METHOD("set_value", "id", "value"), &KartTuning::set_value);
	ClassDB::bind_method(D_METHOD("nudge", "id", "steps"), &KartTuning::nudge);
	ClassDB::bind_method(D_METHOD("reset_value", "id"), &KartTuning::reset_value);
	ClassDB::bind_method(D_METHOD("reset_all"), &KartTuning::reset_all);
	ClassDB::bind_method(D_METHOD("is_default", "id"), &KartTuning::is_default);
	ClassDB::bind_method(D_METHOD("delta", "id"), &KartTuning::delta);

	ClassDB::bind_method(D_METHOD("is_defended", "id"), &KartTuning::is_defended);
	ClassDB::bind_method(D_METHOD("is_acknowledged", "id"), &KartTuning::is_acknowledged);
	ClassDB::bind_method(D_METHOD("last_change_refused"), &KartTuning::last_change_refused);
	ClassDB::bind_static_method("KartTuning", D_METHOD("source_group"),
			&KartTuning::source_group);
	ClassDB::bind_method(D_METHOD("acknowledge", "id"), &KartTuning::acknowledge);
	ClassDB::bind_method(D_METHOD("withdraw_acknowledgement", "id"),
			&KartTuning::withdraw_acknowledgement);

	ClassDB::bind_method(D_METHOD("changed_count"), &KartTuning::changed_count);
	ClassDB::bind_method(D_METHOD("defended_override_count"),
			&KartTuning::defended_override_count);
	ClassDB::bind_method(D_METHOD("tuning_hash_hex"), &KartTuning::tuning_hash_hex);
	ClassDB::bind_method(D_METHOD("default_hash_hex"), &KartTuning::default_hash_hex);
	ClassDB::bind_method(D_METHOD("is_at_defaults"), &KartTuning::is_at_defaults);
	ClassDB::bind_method(D_METHOD("audit_text"), &KartTuning::audit_text);

	ClassDB::bind_method(D_METHOD("save_preset", "path", "name"), &KartTuning::save_preset);
	ClassDB::bind_method(D_METHOD("load_preset", "path"), &KartTuning::load_preset);
	ClassDB::bind_method(D_METHOD("to_text", "name"), &KartTuning::to_text);
	ClassDB::bind_method(D_METHOD("entry_text", "id"), &KartTuning::entry_text);
	ClassDB::bind_static_method("KartTuning", D_METHOD("quantum"), &KartTuning::quantum);
	ClassDB::bind_method(D_METHOD("from_text", "text"), &KartTuning::from_text);

	ClassDB::bind_method(D_METHOD("set_vehicle_path", "path"), &KartTuning::set_vehicle_path);
	ClassDB::bind_method(D_METHOD("get_vehicle_path"), &KartTuning::get_vehicle_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "vehicle_path",
						PROPERTY_HINT_NODE_PATH_VALID_TYPES, "KartBody"),
			"set_vehicle_path", "get_vehicle_path");

	ClassDB::bind_method(D_METHOD("apply_all"), &KartTuning::apply_all);

	// The three arguments the overlay and the audio rig both read. `owner` is an
	// int rather than a string for the same reason `descriptor` serves the
	// provenance as an int: a GDScript `match` on a string turns a new enumerator
	// into a silent "unknown" branch instead of an obvious one.
	ADD_SIGNAL(MethodInfo("tuning_changed", PropertyInfo(Variant::STRING, "key"),
			PropertyInfo(Variant::FLOAT, "value"), PropertyInfo(Variant::INT, "owner")));
}

void KartTuning::_ready() {
	add_to_group(tuning_source_group());

	// Push once, so a scene that constructed this node already tuned — a probe
	// that called `set_value` before `add_child`, say — does not have to remember
	// to. At defaults this writes each constant its own current value, which is
	// free and keeps the "the registry is the truth" invariant true from the
	// first tick rather than from the first change.
	//
	// **Deferred, and that is not tidiness.** `KartBody::_ready` calls
	// `KartVehicle::configure()`, which rebuilds the steering geometry wholesale
	// from `kz_front_geometry()` — so a `max_lock` pushed before the kart is
	// ready is silently discarded. Node ready order depends on which child was
	// added first, which is a scene-authoring detail no scene should have to get
	// right. Deferring runs this after every `_ready` in the tree, so the answer
	// no longer depends on the order.
	//
	// At defaults the failure would have been invisible, because the value being
	// discarded and the value replacing it are the same number. It would have
	// appeared the first time somebody loaded a preset before driving.
	callable_mp(this, &KartTuning::apply_all).call_deferred();
}

// --- the table --------------------------------------------------------------

int KartTuning::tunable_count() const {
	return TUNABLE_COUNT;
}

Dictionary KartTuning::descriptor(int p_id) const {
	Dictionary d;
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(d, vformat("no tunable with id %d; there are %d", p_id, TUNABLE_COUNT));
	}
	const kart::core::Tunable &t = TUNABLES[p_id];
	d["id"] = p_id;
	d["key"] = from_utf8(t.key);
	d["label"] = from_utf8(t.label);
	d["unit"] = from_utf8(t.unit);
	d["home"] = from_utf8(t.home);
	d["citation"] = from_utf8(t.citation);
	d["default_value"] = t.default_value;
	d["min_value"] = t.min_value;
	d["max_value"] = t.max_value;
	d["step"] = t.step;
	d["provenance"] = static_cast<int>(t.provenance);
	d["provenance_name"] = from_utf8(kart::core::provenance_name(t.provenance));
	d["defended"] = kart::core::is_defended(t.provenance);
	d["owner"] = static_cast<int>(t.owner);
	d["owner_name"] = from_utf8(kart::core::owner_name(t.owner));
	return d;
}

Array KartTuning::descriptors() const {
	Array all;
	all.resize(TUNABLE_COUNT);
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		all[id] = descriptor(id);
	}
	return all;
}

int KartTuning::id_of(const String &p_key) const {
	const CharString utf8 = p_key.utf8();
	return kart::core::tunable_by_key(utf8.get_data());
}

// --- values -----------------------------------------------------------------

double KartTuning::get_value(int p_id) const {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(0.0, vformat("no tunable with id %d", p_id));
	}
	return set_.get(p_id);
}

double KartTuning::set_value(int p_id, double p_value) {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(0.0, vformat("no tunable with id %d", p_id));
	}
	const kart::core::Tunable &t = TUNABLES[p_id];

	// The refusal. Not an assert and not a crash: a UI asking to move a locked
	// row is an ordinary event, and the answer is "no, and here is what you would
	// be overriding" rather than a stack trace.
	if (kart::core::is_defended(t.provenance) && !acknowledged_[p_id]) {
		last_refused_ = true;
		WARN_PRINT(vformat(
				"tuning: %s is %s and has not been acknowledged, so it was not changed.\n"
				"  %s\n"
				"  acknowledge(%d) unlocks it for this session.",
				from_utf8(t.key), from_utf8(kart::core::provenance_name(t.provenance)),
				from_utf8(t.citation), p_id));
		return set_.get(p_id);
	}

	last_refused_ = false;
	const double stored = set_.set(p_id, p_value);
	apply(p_id);
	return stored;
}

double KartTuning::nudge(int p_id, int p_steps) {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(0.0, vformat("no tunable with id %d", p_id));
	}
	return set_value(p_id, set_.get(p_id) + static_cast<double>(p_steps) * TUNABLES[p_id].step);
}

double KartTuning::reset_value(int p_id) {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(0.0, vformat("no tunable with id %d", p_id));
	}
	// Never refused, and never needs an acknowledgement. Putting a sourced number
	// back where its source put it is the opposite of an override, and a system
	// that made undoing a mistake as ceremonial as making one would teach people
	// to leave the mistake.
	last_refused_ = false;
	const double stored = set_.reset(p_id);
	apply(p_id);
	return stored;
}

void KartTuning::reset_all() {
	set_.reset();
	last_refused_ = false;
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		// Acknowledgements go too. They are permission to move a specific number
		// away from its source, and once it is back there the permission has been
		// spent — leaving it standing would mean a later nudge moved a sourced
		// constant with no gesture at all.
		acknowledged_[id] = false;
	}
	apply_all();
}

bool KartTuning::is_default(int p_id) const {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(true, vformat("no tunable with id %d", p_id));
	}
	return set_.is_default(p_id);
}

double KartTuning::delta(int p_id) const {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(0.0, vformat("no tunable with id %d", p_id));
	}
	return set_.delta(p_id);
}

// --- the acknowledgement ----------------------------------------------------

bool KartTuning::is_defended(int p_id) const {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(false, vformat("no tunable with id %d", p_id));
	}
	return kart::core::is_defended(TUNABLES[p_id].provenance);
}

bool KartTuning::last_change_refused() const {
	return last_refused_;
}

bool KartTuning::is_acknowledged(int p_id) const {
	if (!valid_id(p_id)) {
		ERR_FAIL_V_MSG(false, vformat("no tunable with id %d", p_id));
	}
	return acknowledged_[p_id];
}

void KartTuning::acknowledge(int p_id) {
	if (!valid_id(p_id)) {
		ERR_FAIL_MSG(vformat("no tunable with id %d", p_id));
	}
	const kart::core::Tunable &t = TUNABLES[p_id];
	if (!kart::core::is_defended(t.provenance)) {
		// Not an error — a UI that acknowledged whatever row was selected is
		// reasonable code. It just does nothing, and saying so beats leaving a
		// flag set on a tunable that has no lock to unlock.
		return;
	}
	acknowledged_[p_id] = true;

	// Printed at warning level even though nothing is wrong yet, because this is
	// the moment worth having in a terminal: it is the record that somebody
	// decided to override a number with evidence behind it, and it survives a
	// session that never saves a preset.
	WARN_PRINT(vformat("tuning: %s (%s) unlocked for this session. It overrides:\n  %s",
			from_utf8(t.key), from_utf8(kart::core::provenance_name(t.provenance)),
			from_utf8(t.citation)));
}

void KartTuning::withdraw_acknowledgement(int p_id) {
	if (!valid_id(p_id)) {
		ERR_FAIL_MSG(vformat("no tunable with id %d", p_id));
	}
	acknowledged_[p_id] = false;
}

// --- the audit --------------------------------------------------------------

int KartTuning::changed_count() const {
	return set_.changed_count();
}

int KartTuning::defended_override_count() const {
	return set_.defended_override_count();
}

String KartTuning::tuning_hash_hex() const {
	char buffer[32];
	kart::core::format_hex64(set_.hash(), buffer, sizeof(buffer));
	return from_utf8(buffer);
}

String KartTuning::default_hash_hex() const {
	char buffer[32];
	kart::core::format_hex64(kart::core::default_tuning_hash(), buffer, sizeof(buffer));
	return from_utf8(buffer);
}

bool KartTuning::is_at_defaults() const {
	return set_.hash() == kart::core::default_tuning_hash();
}

String KartTuning::audit_text() const {
	String out;
	const int changed = set_.changed_count();
	const int defended = set_.defended_override_count();

	// The defended count goes first when it is not zero. That is the number
	// ARCHITECTURE.md §19 is about, and burying it under a list is how a sourced
	// override becomes one line among fourteen.
	if (defended > 0) {
		out += vformat("%d DEFENDED OVERRIDE%s -- a sourced or measured default has been "
					   "moved\n",
				defended, defended == 1 ? String("") : String("S"));
	}
	if (changed == 0) {
		out += "at defaults -- nothing has been tuned\n";
	} else {
		out += vformat("%d of %d tunables differ from their default\n", changed, TUNABLE_COUNT);
	}
	out += vformat("tuning  %s\ndefault %s\n", tuning_hash_hex(), default_hash_hex());

	if (changed > 0) {
		out += "\n";
		char line[kart::core::TUNING_LINE_CHARS];
		for (int id = 0; id < TUNABLE_COUNT; ++id) {
			if (kart::core::format_entry(set_, id, line, sizeof(line)) > 0) {
				out += from_utf8(line);
				out += "\n";
			}
		}
	}
	return out;
}

// --- presets ----------------------------------------------------------------

String KartTuning::to_text(const String &p_name) const {
	char buffer[kart::core::TUNING_LINE_CHARS];

	String out =
			"# kartgame tuning preset.\n"
			"#\n"
			"# A diff against the built-in defaults: a tunable with no line here is at its\n"
			"# default, so an empty body means nothing has been tuned. A line beginning `!`\n"
			"# overrides a default that has a source or a measurement behind it, and the\n"
			"# comment names what it is overriding. See src/core/tuning.h.\n"
			"format 1\n";

	// The name is written on one line and read back whole, so a newline in it
	// would produce a file that parses as something else. Replaced rather than
	// rejected: a caller passing a multi-line name has made a typo, not an
	// attack, and losing the save would be the larger harm.
	//
	// **`#` is in this list and was missing from it**, which broke the one property
	// `tools/verify/tuning.sh --check` gates on. `parse_line` strips a comment before
	// it classifies the line, so a preset saved as `hairpin #3` loaded back named
	// `hairpin` and one saved as `#private` loaded back with an empty name and
	// re-saved as `unnamed` — a round trip that is not byte-identical, in the format
	// whose whole selling point is that it is. Two of ADR-0040's readers found the
	// wrong doc comment in `tuning.h` independently; neither the comment nor this
	// line had been checked against the other.
	String clean = p_name.replace("\n", " ").replace("\r", " ").replace("#", " ").strip_edges();
	if (clean.is_empty()) {
		clean = "unnamed";
	}
	out += vformat("name %s\n", clean);

	kart::core::format_hex64(kart::core::default_tuning_hash(), buffer, sizeof(buffer));
	out += vformat("defaults %s\n", from_utf8(buffer));
	kart::core::format_hex64(set_.hash(), buffer, sizeof(buffer));
	out += vformat("tuned %s\n", from_utf8(buffer));
	out += "\n";

	// Declaration order, always. Two saves of the same configuration have to be
	// byte-identical or "diffable" means nothing — a file that reordered itself
	// would show every line as changed on a run that changed one.
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		if (kart::core::format_entry(set_, id, buffer, sizeof(buffer)) > 0) {
			out += from_utf8(buffer);
			out += "\n";
		}
	}
	return out;
}

Dictionary KartTuning::from_text(const String &p_text) {
	Dictionary result;
	PackedStringArray warnings;
	PackedInt32Array applied_ids;
	String name;
	String defaults_declared;
	bool defaults_match = true;
	int applied = 0;
	int defended = 0;

	// Parsed into a staging set first, and only committed if every line was good.
	// A preset that half-applied would leave the kart in a configuration that
	// exists in no file, which is worse than one that refuses to load.
	kart::core::TuningSet staged;
	bool staged_acknowledgement[TUNABLE_COUNT] = {};

	const PackedStringArray lines = p_text.split("\n");
	for (int64_t index = 0; index < lines.size(); ++index) {
		const String &line = lines[index];
		const CharString utf8 = line.utf8();
		const ParsedLine parsed = kart::core::parse_line(utf8.get_data(), utf8.length());

		switch (parsed.kind) {
			case ParsedLine::Blank:
			case ParsedLine::Comment:
				break;

			case ParsedLine::Header: {
				const String key = from_utf8(parsed.key);
				const String text = from_utf8(parsed.text);
				if (key == "name") {
					name = text;
				} else if (key == "format") {
					if (text != "1") {
						warnings.push_back(vformat(
								"line %d: format %s is not format 1; reading it as format 1 anyway",
								index + 1, text));
					}
				} else if (key == "defaults") {
					defaults_declared = text;
					defaults_match = text == default_hash_hex();
					// **A warning, not a refusal.** Every entry carries its own key
					// and value, so the preset still says what it meant. What it no
					// longer says is what it meant *relative to the defaults* — a
					// line that reads "+0.5 from a default of 3.0" was written when
					// the default was 3.0 — and the reader has to be told which.
					if (text != default_hash_hex()) {
						warnings.push_back(vformat(
								"line %d: written against defaults %s, this build has %s. The "
								"values still load; the deltas in the comments do not describe "
								"this build.",
								index + 1, text, default_hash_hex()));
					}
				} else if (key == "tuned") {
					// Checked after the body, below.
					result["declared_hash"] = text;
				} else {
					warnings.push_back(vformat("line %d: unknown header '%s', ignored",
							index + 1, key));
				}
				break;
			}

			case ParsedLine::Entry: {
				if (parsed.id < 0) {
					// A renamed constant and a typo look identical here, and both
					// deserve to be said out loud rather than dropped.
					warnings.push_back(vformat("line %d: no tunable named '%s', skipped",
							index + 1, from_utf8(parsed.key)));
					break;
				}
				const kart::core::Tunable &t = TUNABLES[parsed.id];
				const double stored = staged.set(parsed.id, parsed.value);
				if (kart::core::tuning_micro(stored) != kart::core::tuning_micro(parsed.value)) {
					warnings.push_back(vformat(
							"line %d: %s asked for %f, clamped to %f by its declared range",
							index + 1, from_utf8(t.key), parsed.value, stored));
				}
				++applied;
				applied_ids.push_back(parsed.id);
				if (kart::core::is_defended(t.provenance)) {
					++defended;
					// The file said `!`, in writing, in a file somebody can read in
					// a diff. That is a stronger acknowledgement than the two-press
					// gesture the overlay asks for, so loading grants it — and the
					// preset is reported as carrying a defended override either way.
					staged_acknowledgement[parsed.id] = true;
					if (!parsed.defended_marker) {
						warnings.push_back(vformat(
								"line %d: %s is %s and its line has no '!' marker. Hand-edited? "
								"It is applied, and it will be written back with the marker.",
								index + 1, from_utf8(t.key),
								from_utf8(kart::core::provenance_name(t.provenance))));
					}
				}
				break;
			}

			case ParsedLine::Invalid:
			default:
				result["ok"] = false;
				result["error"] = static_cast<int>(ERR_PARSE_ERROR);
				result["name"] = name;
				result["applied"] = 0;
				result["defended"] = 0;
				warnings.push_back(vformat("line %d: cannot parse '%s' -- nothing was applied",
						index + 1, line));
				result["warnings"] = warnings;
				return result;
		}
	}

	set_ = staged;
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		if (staged_acknowledgement[id]) {
			acknowledged_[id] = true;
		}
	}
	apply_all();

	// The `tuned` header is a checksum on the body, checked last because it
	// describes the whole set. It is a warning rather than a refusal for the same
	// reason `defaults` is: a hand-edited preset with a stale checksum is a
	// person editing a text file, which is exactly what a diffable format is for.
	if (result.has("declared_hash")) {
		const String declared = result["declared_hash"];
		if (declared != tuning_hash_hex()) {
			warnings.push_back(vformat(
					"the file declares tuned %s and the loaded set hashes to %s -- the file was "
					"hand-edited, or it names tunables this build does not have",
					declared, tuning_hash_hex()));
		}
	}

	result["ok"] = true;
	result["error"] = static_cast<int>(OK);
	result["name"] = name;
	result["applied"] = applied;
	result["applied_ids"] = applied_ids;
	result["defended"] = defended;
	result["defaults_hash"] = defaults_declared;
	result["defaults_match"] = defaults_match;
	result["warnings"] = warnings;
	return result;
}

int KartTuning::adopt_set(const kart::core::TuningSet &p_set) {
	set_ = p_set;
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		if (kart::core::is_defended(TUNABLES[id].provenance) && !set_.is_default(id)) {
			// In writing, in a configuration somebody recorded — `from_text` grants
			// the acknowledgement for a file's `!` marker on the same reasoning.
			acknowledged_[id] = true;
		}
	}
	apply_all();
	return set_.defended_override_count();
}

Error KartTuning::save_preset(const String &p_path, const String &p_name) const {
	// Create the directory rather than fail on it. The natural home for these is
	// `user://tuning/`, which does not exist until something makes it, and a save
	// that failed for that reason is a lost session that found a good number.
	const String directory = p_path.get_base_dir();
	if (!directory.is_empty() && !DirAccess::dir_exists_absolute(directory)) {
		const Error made = DirAccess::make_dir_recursive_absolute(directory);
		if (made != OK) {
			ERR_FAIL_V_MSG(made, vformat("tuning: cannot create %s (error %d)", directory, made));
		}
	}

	// **Written to a temporary and renamed over the target, and it did not used to
	// be.** The previous version opened `p_path` for writing directly, which is three
	// defects in five lines and all of them measured:
	//
	//   * `FileAccess::open(target, WRITE)` **truncates on open**, so between that
	//     call and the first `store_string` the preset on disk is zero bytes. Kill the
	//     process there and the file is not half-written, it is empty.
	//   * it opens the target at all, so a read-only file fails with error 12 where a
	//     rename would have succeeded.
	//   * it ignored `store_string`'s return and `close`'s, so a full disk reported
	//     `OK` and the caller believed the preset was saved.
	//
	// `GAMEDESIGN.md` §8 lists presets in what persists and §5 has them surviving a
	// season re-run, so they are user data by exactly the argument ADR-0042 makes
	// about the career save. A preset is a session that found a good number.
	//
	// The guarantee: a power cut leaves either the old complete preset or the new
	// complete one. `FileAccess` has no fsync of any kind, so for a milestone this
	// bought process death only; issue #173's shim (`fsync_shim.h`, which names
	// the platform call per platform) closed the gap between the close and the
	// rename below. `src/core/profile.h` states the whole argument at length.
	const String temp = p_path + String(".tmp");
	{
		Ref<FileAccess> file = FileAccess::open(temp, FileAccess::WRITE);
		if (file.is_null()) {
			const Error why = FileAccess::get_open_error();
			ERR_FAIL_V_MSG(why, vformat("tuning: cannot write %s (error %d)", temp, why));
		}
		const String text = to_text(p_name);
		const bool stored = file->store_string(text);
		// Flush before close rather than trusting close to do it. Close does flush;
		// the ordering that matters is that the bytes are with the kernel before the
		// rename is issued, and this is the only place that says so.
		file->flush();
		file->close();
		if (!stored) {
			DirAccess::remove_absolute(temp);
			ERR_FAIL_V_MSG(ERR_FILE_CANT_WRITE,
					vformat("tuning: the write to %s did not complete", temp));
		}
	}

	// Read the length back. `store_string` returning true says the calls were
	// accepted, not that the bytes landed, and a full disk is the case where those
	// two differ. A stat, not a read.
	{
		Ref<FileAccess> check = FileAccess::open(temp, FileAccess::READ);
		if (check.is_null()) {
			ERR_FAIL_V_MSG(ERR_FILE_CANT_READ,
					vformat("tuning: wrote %s and could not reopen it", temp));
		}
		const int64_t on_disk = static_cast<int64_t>(check->get_length());
		check->close();
		const int64_t expected = static_cast<int64_t>(to_text(p_name).utf8().length());
		if (on_disk != expected) {
			DirAccess::remove_absolute(temp);
			ERR_FAIL_V_MSG(ERR_FILE_CANT_WRITE,
					vformat("tuning: %s holds %d bytes of %d -- out of space?", temp, on_disk,
							expected));
		}
	}

	// Issue #173: the bytes reach the medium before any name points at them.
	// A filesystem that cannot sync downgrades this save to the process-death
	// guarantee it had before the shim, out loud; a sync that failed is a failed
	// save, because returning OK here claims durability.
	const Error synced = fsync_file(temp);
	if (synced == ERR_UNAVAILABLE) {
		WARN_PRINT(vformat("tuning: %s cannot sync; this preset is safe against a crash, "
						   "not a power cut",
				temp));
	} else if (synced != OK) {
		DirAccess::remove_absolute(temp);
		ERR_FAIL_V_MSG(ERR_FILE_CANT_WRITE,
				vformat("tuning: the sync of %s failed (error %d)", temp, synced));
	}

	const Error renamed = DirAccess::rename_absolute(temp, p_path);
	if (renamed != OK) {
		// The target is untouched, which is the whole point. Take the temporary with
		// us so a retry does not find a stale one and nobody mistakes it for a preset.
		DirAccess::remove_absolute(temp);
		ERR_FAIL_V_MSG(renamed,
				vformat("tuning: cannot rename %s over %s (error %d)", temp, p_path, renamed));
	}

	// And the rename itself, so a preset that reported OK exists after the cut.
	// Best effort: a lost rename reverts to the old complete preset, which loses
	// a number and not a career, so this one warns rather than fails.
	if (fsync_dir(p_path.get_base_dir()) != OK) {
		WARN_PRINT(vformat("tuning: the directory of %s did not sync; a power cut may "
						   "revert to the previous preset",
				p_path));
	}
	return OK;
}

Dictionary KartTuning::load_preset(const String &p_path) {
	Dictionary result;
	if (!FileAccess::file_exists(p_path)) {
		PackedStringArray warnings;
		warnings.push_back(vformat("no preset at %s", p_path));
		result["ok"] = false;
		result["error"] = static_cast<int>(ERR_FILE_NOT_FOUND);
		result["name"] = String();
		result["applied"] = 0;
		result["defended"] = 0;
		result["warnings"] = warnings;
		return result;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		const Error why = FileAccess::get_open_error();
		PackedStringArray warnings;
		warnings.push_back(vformat("cannot read %s (error %d)", p_path, why));
		result["ok"] = false;
		result["error"] = static_cast<int>(why);
		result["name"] = String();
		result["applied"] = 0;
		result["defended"] = 0;
		result["warnings"] = warnings;
		return result;
	}
	const String text = file->get_as_text();
	file->close();
	return from_text(text);
}

// --- wiring -----------------------------------------------------------------

void KartTuning::set_vehicle_path(const NodePath &p_path) {
	vehicle_path_ = p_path;
	if (is_inside_tree()) {
		apply_all();
	}
}

NodePath KartTuning::get_vehicle_path() const {
	return vehicle_path_;
}

void KartTuning::apply_all() {
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		apply(id);
	}
}

void KartTuning::apply(int p_id) {
	const kart::core::Tunable &t = TUNABLES[p_id];
	const double value = set_.get(p_id);

	// **Only vehicle values are pushed from here, and that changed with ADR-0040.**
	// Audio values never went anywhere from this node: the three voice constants
	// live in GDScript and the two synth ones live on `EngineVoiceStream`, and this
	// node deliberately does not know how to reach either. `tuning_changed` is how
	// they arrive, which also means a consumer that is not in the scene costs
	// nothing rather than needing a null check here.
	//
	// `steer_gamma` — the one `TuningOwner::Controller` value — joined them when the
	// steering curve moved out of `KartBody` and onto `PlayerDriver`. It is not a
	// property of the vehicle and this node has no business knowing which node is
	// holding the stick, so `PlayerDriver` subscribes to the signal like every other
	// non-vehicle consumer.
	if (t.owner == kart::core::TuningOwner::Vehicle && !vehicle_path_.is_empty() &&
			is_inside_tree()) {
		KartBody *kart = Object::cast_to<KartBody>(get_node_or_null(vehicle_path_));
		if (kart != nullptr) {
			switch (p_id) {
				case kart::core::TUNE_PEAK_FRICTION: kart->set_peak_friction(value); break;
				case kart::core::TUNE_MAX_LOCK: kart->set_max_lock(value); break;
				case kart::core::TUNE_FRAME_TORSION:
					kart->set_frame_torsion_nm_per_deg(value);
					break;
				case kart::core::TUNE_DRAG_AREA: kart->set_drag_area(value); break;
				case kart::core::TUNE_ROLLING_RESISTANCE:
					kart->set_rolling_resistance(value);
					break;
				case kart::core::TUNE_BRAKE_TORQUE_FRONT:
					kart->set_brake_torque_front(value);
					break;
				case kart::core::TUNE_BRAKE_TORQUE_REAR:
					kart->set_brake_torque_rear(value);
					break;
				case kart::core::TUNE_STEER_RATE: kart->set_steer_rate(value); break;
				default:
					// A vehicle-owned tunable with no case here would silently do
					// nothing, which is the failure this system is supposed to
					// prevent rather than commit. Say so once, loudly.
					ERR_PRINT_ONCE(vformat(
							"tuning: %s is owned by the %s but nothing applies it -- add a case "
							"in KartTuning::apply",
							from_utf8(t.key), from_utf8(kart::core::owner_name(t.owner))));
					break;
			}
		}
	}

	emit_signal(tuning_changed_signal(), from_utf8(t.key), value, static_cast<int>(t.owner));
}

} // namespace kartgame
