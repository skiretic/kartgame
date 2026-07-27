#ifndef KART_CORE_SESSION_H
#define KART_CORE_SESSION_H

#include "core/state_hash.h"
#include "core/tuning.h"

#include <cstdint>

// What a session is, as one type. ROADMAP M3c, and the argument the session
// runner takes.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// This header is also allocation-free and locale-free for the reasons
// `tuning.h` gives: every string is a fixed buffer or a literal, and nothing
// here goes near `snprintf`.
//
// ## Why this type exists before anything that uses it
//
// Three separate pieces of work want it and they were specified in three
// different documents:
//
//   * `docs/GAMEDESIGN.md` §2 — "Session = track + layout + session type +
//     rule set + entry list", the atom every mode is an arrangement of;
//   * ADR-0041 — the replay header, which carries the whole configuration
//     inline and fingerprints it with a hash that is deliberately **not**
//     `StateHash`;
//   * ADR-0043 — `race_rules.h`, which classifies a result according to which
//     session produced it.
//
// Those are one object seen from three sides. Written three times it would be
// three incompatible objects, and the failure would surface as a replay that
// refuses to load for a reason nobody can name.
//
// ## The hash answers one question, and it is not StateHash's question
//
// `TuningSet::hash()` already made this distinction and this follows it exactly:
//
//   * `StateHash` asks **"did two runs of the same configuration diverge?"**
//   * `SessionConfig::hash()` asks **"is this the configuration I think it
//     is?"** — one number for the whole session, compared once per run.
//
// ADR-0037 keeps a tuning preset out of `StateHash` and that decision stands.
// The preset is in *this* hash, which is the hole ADR-0041 identified: without
// it, a replay recorded under a preset re-sims under the defaults and reports a
// determinism failure that is nothing of the sort.
//
// **The seed is hashed too, which ADR-0041's diagram does not show.** Its header
// sketch puts `seed` below `config_hash`, outside the hashed region. Including
// it cannot produce a false mismatch — playback reconstructs the config from the
// recorded header, seed included, so the recorded and reconstructed seeds are
// the same value by construction — and leaving it out means two runs that differ
// only in seed fingerprint identically, which is a fast yes/no that answers yes
// to a session that is not the same session. ADR-0047's field draw is seeded, so
// the seed selects the entry list: it is configuration, not a run detail.
//
// ## Content hashes rather than content
//
// The track and the driver roster are files (ADR-0046, ADR-0047) and they are
// read on the Godot side. This struct carries their **content hashes**, so a
// replay recorded before a corner was smoothed refuses to play back and names
// the file, rather than re-simulating against different collision geometry.
// Neither hash is computed here; whoever parses the file computes it.

namespace kart::core {

// Longest identifier this struct stores, including the terminator. A track id is
// a short slug — `autumn_ridge` — not a display name, and display names live on
// the script side per ADR-0044.
inline constexpr int SESSION_ID_CHARS = 48;

// A sanity ceiling on the entry list, not the design's field size.
//
// ADR-0046 is explicit that slot count is data and not a constant: the design's
// eight karts come from an audio measurement (`GAMEDESIGN.md` §6) and that
// measurement is expected to move the number without a code change. So this is
// the largest value that is not obviously nonsense — a full FIA Karting grid is
// roughly 34 — and it exists only so `is_valid()` can reject a parse that
// produced garbage.
inline constexpr int SESSION_MAX_ENTRIES = 34;

// The four session types of `GAMEDESIGN.md` §4, which are the four the FIA
// Karting Specific Prescriptions Art. 18 names: "Free Practice, Qualifying
// Practice, Qualifying Heats, Super Heat(s) and a final phase".
//
// Practice **is** time trial — one type reached through two doors, per
// `GAMEDESIGN.md` §3 — so there is no separate `TimeTrial`.
//
// `SuperHeat` is the FIA's word. *Prefinal* is WSK's, and the two series are not
// interchangeable: the FIA adds position points and the highest total wins, WSK
// adds penalty points and the lowest wins. Using the wrong word here would make
// the points tables in `race_rules.h` look like a choice rather than a citation.
enum class SessionType : int {
	Practice = 0,
	Qualifying = 1,
	Heat = 2,
	SuperHeat = 3,
	Final = 4,
};

inline constexpr int SESSION_TYPE_COUNT = 5;

inline const char *session_type_name(SessionType type) {
	switch (type) {
		case SessionType::Practice: return "practice";
		case SessionType::Qualifying: return "qualifying";
		case SessionType::Heat: return "heat";
		case SessionType::SuperHeat: return "super_heat";
		case SessionType::Final: return "final";
	}
	return "invalid";
}

// The two classes of `GAMEDESIGN.md` §5's career, both current FIA designations.
//
// `OK` is the direct-drive class: no gearbox, no clutch of any kind, rear brakes
// only by regulation, 16,000 rpm, 150.0 kg with driver. `KZ2` is what this
// repository already simulates — `kz_reference.h` carries the correction that
// 175.0 kg is KZ2's figure and not KZ's, and why.
//
// **Nothing in `src/` models OK yet.** The value exists because a session's
// class is part of its configuration and a save written today has to say which
// class a lap time belongs to; a best lap recorded with no class attached is a
// best lap that silently compares a shifter against a single-speed. Anything
// that has to *simulate* a class asserts on this rather than assuming.
enum class KartClass : int {
	OK = 0,
	KZ2 = 1,
};

inline constexpr int KART_CLASS_COUNT = 2;

inline const char *kart_class_name(KartClass kart_class) {
	switch (kart_class) {
		case KartClass::OK: return "ok";
		case KartClass::KZ2: return "kz2";
	}
	return "invalid";
}

// Which way round the circuit is run. ADR-0046: reverse is an **authored
// layout**, not a programmatic spline reversal, because curbs sit on the inside
// of the corner they serve and run-off is sized for an approach speed that
// changes. The geometry is shared; nothing else is assumed to be.
enum class TrackLayout : int {
	Forward = 0,
	Reverse = 1,
};

inline constexpr int TRACK_LAYOUT_COUNT = 2;

inline const char *track_layout_name(TrackLayout layout) {
	switch (layout) {
		case TrackLayout::Forward: return "forward";
		case TrackLayout::Reverse: return "reverse";
	}
	return "invalid";
}

// Track-wide condition, which is not `surface.h`'s per-patch `SurfaceType`.
//
// **`Dry` is the only value, and that is the honest state of the project rather
// than a placeholder.** Wet is not modeled anywhere: there is no water film in
// the tire model, no wet grip multiplier in `surface.h`, and no rain in the
// look. Naming a `Wet` value here would advertise an intention the code does not
// honor, which `GAMEDESIGN.md` §13 rules out in the same words. The field exists
// so that the day wet arrives it is a value in a hashed configuration rather
// than a schema change that invalidates every save and replay in existence.
enum class TrackCondition : int {
	Dry = 0,
};

inline constexpr int TRACK_CONDITION_COUNT = 1;

inline const char *track_condition_name(TrackCondition condition) {
	switch (condition) {
		case TrackCondition::Dry: return "dry";
	}
	return "invalid";
}

// What ends the session.
//
// The FIA specifies a race as a **distance** — a Final is 30 km — so the lap
// count falls out of the circuit rather than being authored per track, and
// `GAMEDESIGN.md` §4's compression is a scale factor on a distance rather than a
// table of lap counts. Qualifying Practice is a **time**: 6 minutes, compressed
// to 3. Practice is `Open`: it ends when the driver leaves.
//
// `Laps` exists because a player setting up a standalone session thinks in laps,
// and because it is the only limit a session can have on a track whose length is
// not yet known.
enum class SessionLimitKind : int {
	Open = 0,
	Laps = 1,
	Distance = 2,
	Duration = 3,
};

inline constexpr int SESSION_LIMIT_KIND_COUNT = 4;

inline const char *session_limit_kind_name(SessionLimitKind kind) {
	switch (kind) {
		case SessionLimitKind::Open: return "open";
		case SessionLimitKind::Laps: return "laps";
		case SessionLimitKind::Distance: return "distance";
		case SessionLimitKind::Duration: return "duration";
	}
	return "invalid";
}

// The limit and its one number. `value` is laps, meters or seconds according to
// `kind`, and is ignored when the kind is `Open`.
//
// One number and an enum rather than three optional fields: three fields make it
// possible to write a session that is both 6 laps and 4 kilometers, and the
// runner would then have to decide which it meant.
struct SessionLimit {
	SessionLimitKind kind = SessionLimitKind::Open;
	double value = 0.0;

	static SessionLimit open() { return SessionLimit{ SessionLimitKind::Open, 0.0 }; }
	static SessionLimit laps(double count) { return SessionLimit{ SessionLimitKind::Laps, count }; }
	static SessionLimit distance_m(double meters) {
		return SessionLimit{ SessionLimitKind::Distance, meters };
	}
	static SessionLimit duration_s(double seconds) {
		return SessionLimit{ SessionLimitKind::Duration, seconds };
	}
};

// The driving assists, which are configuration and therefore part of the
// fingerprint.
//
// Both default **on**, which is issue #40's decision and `ARCHITECTURE.md` §19's
// answer to "the KZ gearbox makes it unplayable for newcomers": an unassisted
// first lap in a KZ2 ends in a stall on the grid. `KartBody` already carries
// both flags with these defaults and this struct does not introduce them.
//
// They are hashed because they change the lap: a replay recorded with auto-shift
// on re-sims into a different gear on every corner without it, and the resulting
// state divergence would be reported as a determinism bug.
struct Assists {
	bool auto_clutch = true;
	bool auto_shift = true;
};

// Everything a session is.
//
// Ordinary aggregate, copyable, no invariants enforced by construction — a
// parser fills it field by field and then calls `is_valid()`. That ordering is
// deliberate: a constructor that validated would force a parser to either build
// a complete object or throw, and ADR-0042's migration chain wants to fill in a
// missing field from a defaulted value instead.
struct SessionConfig {
	// --- content ---------------------------------------------------------------

	// Track slug, NUL-terminated. `set_track_id` is the only writer that
	// guarantees the terminator.
	char track_id[SESSION_ID_CHARS] = {};

	// Content hash of `track.json`, computed by whoever parsed it. ADR-0046's
	// consequence for replays: this is what makes a smoothed corner refuse a
	// replay rather than silently re-simulating against different collision.
	uint64_t track_hash = 0;

	TrackLayout layout = TrackLayout::Forward;
	TrackCondition condition = TrackCondition::Dry;

	// --- what kind of session ---------------------------------------------------

	SessionType type = SessionType::Practice;
	KartClass kart_class = KartClass::KZ2;
	SessionLimit limit = SessionLimit::open();

	// --- the field --------------------------------------------------------------

	// Total karts on track including the player. **1 is a legal value and is what
	// Practice uses** — `GAMEDESIGN.md` §3's time trial has no field, and a field
	// of one is the player alone rather than a special case in the runner.
	int entry_count = 1;

	// Content hash of `data/drivers.json`, per ADR-0047, so a replay reproduces
	// the field it was recorded against rather than whichever entries today's
	// roster holds. Zero when there is no field.
	uint64_t roster_hash = 0;

	// --- the run ----------------------------------------------------------------

	Assists assists;

	// The full tuning set, not a path and not a diff-on-disk. ADR-0041 is explicit
	// about why a path is wrong — a replay that breaks when somebody tidies
	// `user://tuning/` cannot be trusted for anything — and `TuningSet` is already
	// a diff against the sourced defaults in its serialized form, so the file
	// stays small while the in-memory object stays complete.
	TuningSet tuning;

	// Seeds every stochastic thing in the session: ADR-0047's draw of eight
	// drivers from the roster, and anything `pcg32.h` is handed downstream.
	uint64_t seed = 0;

	// Whether a track id is a slug: lower case, digits, underscore and hyphen, at
	// least one character.
	//
	// **This exists because a track id reaches a filename.** ADR-0046 has the track
	// loaded from a file named for it, and `profile.h` keys a best lap on it and
	// pastes a ghost id into `user://ghosts/<id>.ghost`. `set_track_id` used to check
	// the *length* and nothing else, so `../../../etc/passwd` was a legal track id as
	// far as this type was concerned, and every consumer was left to notice. One of
	// them did, sanitized it in its own id minting, and reported that the boundary
	// was upstream. It is here now: the type that carries the id is the one place
	// that can promise every reader the same thing.
	//
	// Deliberately narrower than a filename needs to be. A display name for a
	// circuit lives on the script side per ADR-0044, so nothing is lost by refusing
	// spaces, dots and capitals — and a dot is what makes `..` reachable at all.
	static bool is_slug(const char *id) {
		if (id == nullptr || id[0] == '\0') {
			return false;
		}
		for (int i = 0; id[i] != '\0'; ++i) {
			const char c = id[i];
			const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
					c == '-';
			if (!ok) {
				return false;
			}
		}
		return true;
	}

	// Copy a slug in, terminating rather than overflowing. Returns false if the
	// source was not a slug or did not fit, and in either case leaves the id
	// **empty** rather than partly written.
	//
	// Empty, not truncated: an earlier version kept the truncated prefix on the
	// grounds that it would fail to resolve loudly at load. That is true of a
	// truncation and false of a rejected character — `circuit/../x` truncated to
	// `circuit/..` is worse than what it started as. An empty id fails `is_valid`
	// immediately, which is the loudest failure available and the nearest one to the
	// caller.
	bool set_track_id(const char *id) {
		for (int i = 0; i < SESSION_ID_CHARS; ++i) {
			track_id[i] = '\0';
		}
		if (!is_slug(id)) {
			return false;
		}
		int i = 0;
		while (i < SESSION_ID_CHARS - 1 && id[i] != '\0') {
			track_id[i] = id[i];
			++i;
		}
		if (id[i] != '\0') {
			// Did not fit. Leave nothing behind for the same reason as above.
			for (int j = 0; j < SESSION_ID_CHARS; ++j) {
				track_id[j] = '\0';
			}
			return false;
		}
		return true;
	}

	// Whether this describes a session that can actually be run. Cheap, total, and
	// called by every parser rather than trusted.
	bool is_valid() const {
		if (track_id[0] == '\0') {
			return false;
		}
		bool terminated = false;
		for (int i = 0; i < SESSION_ID_CHARS; ++i) {
			if (track_id[i] == '\0') {
				terminated = true;
				break;
			}
		}
		if (!terminated) {
			return false;
		}
		if (static_cast<int>(type) < 0 || static_cast<int>(type) >= SESSION_TYPE_COUNT) {
			return false;
		}
		if (static_cast<int>(kart_class) < 0 || static_cast<int>(kart_class) >= KART_CLASS_COUNT) {
			return false;
		}
		if (static_cast<int>(layout) < 0 || static_cast<int>(layout) >= TRACK_LAYOUT_COUNT) {
			return false;
		}
		if (static_cast<int>(condition) < 0 ||
				static_cast<int>(condition) >= TRACK_CONDITION_COUNT) {
			return false;
		}
		if (static_cast<int>(limit.kind) < 0 ||
				static_cast<int>(limit.kind) >= SESSION_LIMIT_KIND_COUNT) {
			return false;
		}
		if (limit.kind != SessionLimitKind::Open && !(limit.value > 0.0)) {
			return false;
		}
		if (entry_count < 1 || entry_count > SESSION_MAX_ENTRIES) {
			return false;
		}
		return true;
	}

	// Fingerprint of the whole configuration. ADR-0041's `config_hash`.
	//
	// Every field is hashed **alongside its own name**, exactly as
	// `TuningSet::hash()` hashes each key beside its value, and for the same
	// reason: a hash over values alone reports two different schemas as the same
	// configuration, so adding a field or renaming one would leave old replays
	// claiming to match. The names below are the schema, and changing one is a
	// deliberate invalidation rather than an accident.
	//
	// `SESSION_HASH_QUANTUM` rather than `StateHash`'s default because the values
	// here are configuration, not simulation state: a lap count and a distance in
	// meters are exact numbers a person typed, and `TUNING_QUANTUM` is the grid the
	// tuning set already quantizes to.
	//
	// **One thing here is safe by accident and the next field could break it.**
	// `track_id`'s characters go into the same byte stream as the field names, with
	// no length prefix and no separator, so in principle a string could impersonate
	// the name of the field that follows it. It cannot today, and the reason is that
	// `track_id` is the *only* variable-length field: a variable string followed by
	// a fixed known suffix is injective. Add a second string — a roster id, a driver
	// name, a preset label — and two different configurations can fingerprint
	// identically, which is exactly the false "yes" this hash exists not to give.
	// Whoever adds one mixes its length first.
	uint64_t hash() const {
		StateHash digest(SESSION_HASH_QUANTUM);
		mix_name(digest, "track_id");
		for (const char *c = track_id; *c != '\0'; ++c) {
			digest.add_uint64(static_cast<uint64_t>(static_cast<unsigned char>(*c)));
		}
		mix_name(digest, "track_hash");
		digest.add_uint64(track_hash);
		mix_name(digest, "layout");
		digest.add_int(static_cast<int64_t>(layout));
		mix_name(digest, "condition");
		digest.add_int(static_cast<int64_t>(condition));
		mix_name(digest, "type");
		digest.add_int(static_cast<int64_t>(type));
		mix_name(digest, "kart_class");
		digest.add_int(static_cast<int64_t>(kart_class));
		mix_name(digest, "limit_kind");
		digest.add_int(static_cast<int64_t>(limit.kind));
		mix_name(digest, "limit_value");
		digest.add_double(limit.value);
		mix_name(digest, "entry_count");
		digest.add_int(entry_count);
		mix_name(digest, "roster_hash");
		digest.add_uint64(roster_hash);
		mix_name(digest, "auto_clutch");
		digest.add_int(assists.auto_clutch ? 1 : 0);
		mix_name(digest, "auto_shift");
		digest.add_int(assists.auto_shift ? 1 : 0);
		mix_name(digest, "tuning");
		digest.add_uint64(tuning.hash());
		mix_name(digest, "seed");
		digest.add_uint64(seed);
		return digest.digest();
	}

	// The grid the configuration's own numbers are quantized to before hashing.
	// `TUNING_QUANTUM` is what the tuning set already uses and reusing it keeps one
	// grid across every configuration hash in the project.
	static constexpr double SESSION_HASH_QUANTUM = TUNING_QUANTUM;

private:
	static void mix_name(StateHash &digest, const char *name) {
		for (const char *c = name; *c != '\0'; ++c) {
			digest.add_uint64(static_cast<uint64_t>(static_cast<unsigned char>(*c)));
		}
	}
};

// --- naming the field that moved ----------------------------------------------
//
// ADR-0041's error message is the reason this exists: "This replay was recorded
// with `frame_torsion` at 210.0; the current default is 193.62" is a sentence a
// person can act on, and "config_hash mismatch" is not. A hash says whether two
// configurations differ; something has to say **where**.
//
// It lives here rather than in the replay code because the answer is a property
// of this struct's schema. Written on the replay side it would be a second list
// of the same field names, kept in step by hand, and the day they drift the
// error message names the wrong field — which is worse than naming none.

enum class SessionField : int {
	None = 0,
	TrackId,
	TrackHash,
	Layout,
	Condition,
	Type,
	KartClass,
	LimitKind,
	LimitValue,
	EntryCount,
	RosterHash,
	AutoClutch,
	AutoShift,
	Tuning,
	Seed,
};

inline const char *session_field_name(SessionField field) {
	switch (field) {
		case SessionField::None: return "none";
		case SessionField::TrackId: return "track_id";
		case SessionField::TrackHash: return "track_hash";
		case SessionField::Layout: return "layout";
		case SessionField::Condition: return "condition";
		case SessionField::Type: return "type";
		case SessionField::KartClass: return "kart_class";
		case SessionField::LimitKind: return "limit_kind";
		case SessionField::LimitValue: return "limit_value";
		case SessionField::EntryCount: return "entry_count";
		case SessionField::RosterHash: return "roster_hash";
		case SessionField::AutoClutch: return "auto_clutch";
		case SessionField::AutoShift: return "auto_shift";
		case SessionField::Tuning: return "tuning";
		case SessionField::Seed: return "seed";
	}
	return "invalid";
}

// Print one configuration field's value. Returns the length written or -1.
//
// The pair to `session_field_name` above, and it is here for the same reason: a
// switch over the field list belongs beside the list. It was first written in
// `replay.h`, by the agent that needed ADR-0041's error sentence, with a comment
// saying it wanted to live here — a second reader of this list is how a diagnostic
// ends up naming one field and printing another's value. There is no `default:`
// label, so adding a `SessionField` without adding a line here is a compiler
// warning rather than a field that prints nothing.
inline int session_field_value(const SessionConfig &config, SessionField field, char *out,
		int cap) {
	if (out == nullptr || cap < 2) {
		return -1;
	}
	auto copy = [&](const char *text) {
		int written = 0;
		while (text[written] != '\0') {
			if (written + 1 >= cap) {
				return -1;
			}
			out[written] = text[written];
			++written;
		}
		out[written] = '\0';
		return written;
	};
	switch (field) {
		case SessionField::None:
			return copy("none");
		case SessionField::TrackId:
			return copy(config.track_id);
		case SessionField::TrackHash:
			return format_hex64(config.track_hash, out, cap);
		case SessionField::Layout:
			return copy(track_layout_name(config.layout));
		case SessionField::Condition:
			return copy(track_condition_name(config.condition));
		case SessionField::Type:
			return copy(session_type_name(config.type));
		case SessionField::KartClass:
			return copy(kart_class_name(config.kart_class));
		case SessionField::LimitKind:
			return copy(session_limit_kind_name(config.limit.kind));
		case SessionField::LimitValue:
			return format_value(config.limit.value, out, cap);
		case SessionField::EntryCount:
			return format_int(config.entry_count, out, cap);
		case SessionField::RosterHash:
			return format_hex64(config.roster_hash, out, cap);
		case SessionField::AutoClutch:
			return copy(config.assists.auto_clutch ? "on" : "off");
		case SessionField::AutoShift:
			return copy(config.assists.auto_shift ? "on" : "off");
		case SessionField::Tuning:
			return format_hex64(config.tuning.hash(), out, cap);
		case SessionField::Seed:
			return format_hex64(config.seed, out, cap);
	}
	return copy("invalid");
}

// The first field on which two configurations disagree, in the order they are
// hashed, or `None` if they agree on all of them.
//
// **`Tuning` is reported as one field, not as the tunable that moved.** The
// tuning set has its own vocabulary and its own per-key comparison in
// `tuning.h` — `is_default`, `delta`, `format_entry` — and a caller that has
// been told the tuning differs asks that file which key. Reproducing the loop
// here would be the drift this function exists to prevent, one level down.
inline SessionField first_difference(const SessionConfig &a, const SessionConfig &b) {
	for (int i = 0; i < SESSION_ID_CHARS; ++i) {
		if (a.track_id[i] != b.track_id[i]) {
			return SessionField::TrackId;
		}
		if (a.track_id[i] == '\0') {
			break;
		}
	}
	if (a.track_hash != b.track_hash) {
		return SessionField::TrackHash;
	}
	if (a.layout != b.layout) {
		return SessionField::Layout;
	}
	if (a.condition != b.condition) {
		return SessionField::Condition;
	}
	if (a.type != b.type) {
		return SessionField::Type;
	}
	if (a.kart_class != b.kart_class) {
		return SessionField::KartClass;
	}
	if (a.limit.kind != b.limit.kind) {
		return SessionField::LimitKind;
	}
	// Integer comparison on the hashing grid, not `==` on the doubles: two values
	// that hash alike must compare alike here, or a mismatched hash could be
	// reported with no field to blame.
	if (tuning_micro(a.limit.value) != tuning_micro(b.limit.value)) {
		return SessionField::LimitValue;
	}
	if (a.entry_count != b.entry_count) {
		return SessionField::EntryCount;
	}
	if (a.roster_hash != b.roster_hash) {
		return SessionField::RosterHash;
	}
	if (a.assists.auto_clutch != b.assists.auto_clutch) {
		return SessionField::AutoClutch;
	}
	if (a.assists.auto_shift != b.assists.auto_shift) {
		return SessionField::AutoShift;
	}
	if (a.tuning.hash() != b.tuning.hash()) {
		return SessionField::Tuning;
	}
	if (a.seed != b.seed) {
		return SessionField::Seed;
	}
	return SessionField::None;
}

// --- the sessions of a weekend, as configuration ------------------------------
//
// `GAMEDESIGN.md` §4's compression, as arithmetic rather than as a table of lap
// counts. The FIA figures and the scale factor are both here so the compression
// is visible as a multiplication on a sourced number: a reader can see 15 km
// become 4 km, and no lap count is authored anywhere.
//
// Sources: FIA Karting 2026 Specific Prescriptions Art. 18 for the session list,
// and the Seniors distances quoted in `GAMEDESIGN.md` §4.

// FIA Qualifying Practice, one session, minutes.
inline constexpr double FIA_QUALIFYING_MINUTES = 6.0;
// FIA race distances for Seniors, meters. There are three Qualifying Heats.
inline constexpr double FIA_HEAT_DISTANCE_M = 15000.0;
inline constexpr double FIA_SUPER_HEAT_DISTANCE_M = 20000.0;
inline constexpr double FIA_FINAL_DISTANCE_M = 30000.0;

// **Ours, not the FIA's.** `GAMEDESIGN.md` §4 states the compression as a number
// so that it is one constant rather than four rounded lap counts: a round of
// three races plus qualifying comes to about 15 minutes, which was the design's
// only hard constraint. The three-heat structure is separately cut to one.
inline constexpr double SESSION_DISTANCE_SCALE = 0.25;

// Qualifying is compressed by **half, not by a quarter**, and the difference is
// not an inconsistency. `GAMEDESIGN.md` §4 puts 6 minutes at 3, which is 0.5, and
// the reason it is not 0.25 is that a qualifying session has to contain a warm-up
// lap, a timed lap and somewhere to put a mistake: at 90 seconds on a 1,200 m
// circuit that is one flying lap and no second chance, which makes the session a
// coin toss rather than a measurement. A race distance has no equivalent floor,
// so it takes the full quarter.
inline constexpr double SESSION_DURATION_SCALE = 0.5;

inline SessionLimit scheduled_limit(SessionType type) {
	switch (type) {
		case SessionType::Practice:
			return SessionLimit::open();
		case SessionType::Qualifying:
			return SessionLimit::duration_s(
					FIA_QUALIFYING_MINUTES * 60.0 * SESSION_DURATION_SCALE);
		case SessionType::Heat:
			return SessionLimit::distance_m(FIA_HEAT_DISTANCE_M * SESSION_DISTANCE_SCALE);
		case SessionType::SuperHeat:
			return SessionLimit::distance_m(FIA_SUPER_HEAT_DISTANCE_M * SESSION_DISTANCE_SCALE);
		case SessionType::Final:
			return SessionLimit::distance_m(FIA_FINAL_DISTANCE_M * SESSION_DISTANCE_SCALE);
	}
	return SessionLimit::open();
}

// Whether a session type runs the whole field or the player alone.
//
// Qualifying is the awkward one and `GAMEDESIGN.md` §4 leaves it deliberately
// open — "none, or field on track" — because the real thing puts everyone out
// together and the useful version for a solo player may not. It is answered here
// as **a field**, because the alternative is a session type whose entry count
// depends on a setting elsewhere, and because a qualifying session with nobody
// else on track is a practice session that ends after three minutes.
inline bool session_has_field(SessionType type) {
	switch (type) {
		case SessionType::Practice: return false;
		case SessionType::Qualifying: return true;
		case SessionType::Heat: return true;
		case SessionType::SuperHeat: return true;
		case SessionType::Final: return true;
	}
	return false;
}

} // namespace kart::core

#endif // KART_CORE_SESSION_H
