#ifndef KART_CORE_TUNING_H
#define KART_CORE_TUNING_H

#include "state_hash.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

// The tuning vocabulary. ROADMAP M3b's last unbuilt bullet, and issue #159.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// This header is also allocation-free and locale-free: every string is a literal
// and every number crosses the file format through integer arithmetic, for the
// reasons the two sections below give.
//
// ## What this is for, which is not "a settings file"
//
// A dozen constants in this project can only be set by driving or by listening.
// Issue #159 is the running checklist of them, and until now each one needed a
// rebuild to change, so "judged by feel" was in practice "whatever the first
// guess was". This header is the machine that fixes that.
//
// The table below is **wider than that checklist**, and deliberately: it also
// carries constants that *do* have a source, because the point is the contrast.
// A registry holding only the guesses would be a list of things it is fine to
// turn, which teaches exactly the wrong lesson the first time somebody wants to
// turn something else.
//
// But `ARCHITECTURE.md` §19 names **unbounded vehicle tuning** as the live risk,
// and ADR-0033 refused to retune M3a's constants rather than restore a pretty
// number by moving one. A sliders-and-a-save-button implementation of this would
// be the risk itself, shipped. So the thing being built is an *audit trail* that
// happens to be adjustable, not a tuning UI that happens to save:
//
//   * every tunable declares where its default came from — `Provenance` below;
//   * a preset is a **diff against the defaults**, so a file that is empty means
//     "nothing has been tuned" and a file with three lines is the complete list
//     of what has;
//   * a default that came from a source or a measurement is **defended**: moving
//     it is possible, requires an explicit acknowledgement, and is marked in the
//     saved file with a `!` and the citation it is overriding.
//
// "What has been tuned away from its source, and by how much" is then one grep,
// which is the property `tools/verify/tuning.sh` exists to serve.
//
// ## Why four provenance classes and not a boolean
//
// The obvious design is `has_source: bool`. It is wrong here, and the file that
// proves it is `docs/REFERENCES.md`:
//
//   * `frame_torsion_nm_per_deg` is 193.62 because Fu and Wang measured a real
//     competition frame and published the number.
//   * `max_lock` is 25 degrees because REFERENCES.md §"Steering lock" records
//     that **no CIK or KZ source for a maximum steer angle was found**, and the
//     figure is inherited from the bodywork clearance tables in issues #109 and
//     #110 — a measurement this repository made of its own kart.
//   * `steer_gamma` is 3.0 because of four rows of arithmetic in ADR-0036.
//   * `noise_gain` is 0.12 because somebody picked it.
//
// A boolean puts the first two in one bucket and cannot say that the second is
// only as good as this project's own geometry. Four classes say it. And this is
// not a new convention: `src/core/kz_audio_reference.h` already carries
// `LIMITER_CHATTER_MEASURED = false` beside its constants for exactly this
// reason. This header generalizes that flag rather than inventing anything.
//
// ## Determinism: a preset does NOT enter the state hash. Deliberately.
//
// A preset changes the solver's constants, so two runs under different presets
// produce different states — that is the point of it. The temptation is to mix
// the tuning into `StateHash` so a divergence is attributable. That is wrong,
// and the reason is the same one `state_hash.h` gives for quantizing: a hash
// answers one question, and a hash that answers two answers neither.
//
//   * `StateHash` asks **"did these two runs of the same configuration
//     diverge?"** Mixing configuration in makes every per-tick hash change when
//     an unrelated audio tunable moves, so a mismatch can no longer distinguish
//     "the physics diverged" from "the config differed".
//   * `TuningSet::hash()` asks **"is this the configuration I think it is?"** —
//     one number for the whole set, compared once per run rather than per tick.
//
// Two numbers, two questions, and the gate is the pair of them: `drive.sh` and
// every §6.4 measurement run at defaults, and the probe asserts
// `TuningSet::hash() == default_tuning_hash()` before it reports a figure. A
// tuned run cannot be quietly recorded as a reference figure, because the
// harness refuses to call it one. That is the whole determinism story and it is
// stronger than mixing would have been: mixing would have made a tuned run
// produce a *different* hash, which is not the same as making it produce a
// *loud* one.
//
// A preset also names the defaults it was written against — `defaults` in the
// file header — so a preset made before a default moved loads with a stated
// warning rather than silently meaning something else.
//
// ## The file format
//
// Line oriented, one tunable per line, sorted by declaration order so two saves
// of the same set are byte-identical and a diff is a diff:
//
//     # kartgame tuning preset. A diff against the defaults; absent means default.
//     format 1
//     name hairpin-v3
//     defaults 0x8f1c4a2b6d0e7391
//     tuned 0x2b77d0114ae5c862
//
//     steer_gamma = 2.400000                  # default 3.000000, -0.600000 (derived)
//     ! max_lock = 0.523599                   # default 0.436332, +0.087266 (measured) OVERRIDE: inherited from the #109/#110 bodywork clearance tables
//
// The `!` is first on the line on purpose. A defended override is meant to be
// visible in a diff hunk, in a grep, and in a terminal that has wrapped the
// comment off the right-hand edge.
//
// Values are written with exactly six decimals and are quantized to 1e-6 **on
// the way in**, so the file is the truth: what a preset saves is exactly what it
// loads, and there is no float that round-trips to a neighbor. The conversion is
// integer arithmetic rather than `snprintf`, because `%f` respects the C locale
// and a decimal comma would produce a file this parser cannot read — a failure
// that would appear on somebody else's machine and nowhere near here.
namespace kart::core {

// Where a default came from. Ordered most to least defensible; the numeric order
// is meaningful and `is_defended` reads it.
enum class Provenance : int {
	// An external authority: a published measurement, a manufacturer figure, a
	// regulation. The citation names it well enough to find it again.
	Sourced = 0,

	// This repository measured it — a probe, a unit test that prints a table, a
	// dimension taken off the generated kart. Real evidence, but evidence about
	// *this* kart rather than about a real one, and the citation says which.
	Measured = 1,

	// Arithmetic on top of sourced or measured numbers. No independent evidence
	// for the value itself; the derivation is the justification and the citation
	// points at where it is written down.
	Derived = 2,

	// A guess with the guess labeled. Nothing constrains it and the citation says
	// what to listen or feel for instead. Issue #159's list is mostly these.
	Unsourced = 3,
};

// Whether moving this default off its value needs an explicit acknowledgement.
//
// The rule, stated once: **a default with evidence behind it is defended.** A
// derivation is not evidence about the world — it is arithmetic this project did
// — and a guess is not evidence at all, so both are free to move. That keeps the
// friction on exactly the two classes where a slider would otherwise quietly
// discard a real number, and off `steer_gamma`, which is the knob this whole
// system exists to let somebody turn.
constexpr bool is_defended(Provenance p) {
	return p == Provenance::Sourced || p == Provenance::Measured;
}

// Lower case, and stable: these strings are written into saved presets.
inline const char *provenance_name(Provenance p) {
	switch (p) {
		case Provenance::Sourced: return "sourced";
		case Provenance::Measured: return "measured";
		case Provenance::Derived: return "derived";
		case Provenance::Unsourced: return "unsourced";
	}
	return "?";
}

// Who applies the value. The registry does not know how to set anything — it
// holds numbers and tells its bridge which subsystem a changed number belongs
// to, which is what keeps this header free of every engine type.
enum class TuningOwner : int {
	Vehicle = 0, // kart::core::KartVehicle, through KartBody's setters
	Controller = 1, // the human input path only — ADR-0036's distinction
	Audio = 2, // the synth and its emitter
};

inline const char *owner_name(TuningOwner o) {
	switch (o) {
		case TuningOwner::Vehicle: return "vehicle";
		case TuningOwner::Controller: return "controller";
		case TuningOwner::Audio: return "audio";
	}
	return "?";
}

// The identifiers. Declaration order is file order and is therefore part of the
// format: inserting in the middle changes no meaning, because every line carries
// its key, but it does change the order a saved file is written in. Append.
enum TunableId : int {
	TUNE_PEAK_FRICTION = 0,
	TUNE_MAX_LOCK,
	TUNE_FRAME_TORSION,
	TUNE_DRAG_AREA,
	TUNE_ROLLING_RESISTANCE,
	TUNE_BRAKE_TORQUE_FRONT,
	TUNE_BRAKE_TORQUE_REAR,
	TUNE_STEER_RATE,
	TUNE_STEER_GAMMA,
	TUNE_VOICE_GAIN,
	TUNE_VOICE_UNIT_SIZE,
	TUNE_VOICE_MAX_DISTANCE,
	TUNE_COMB_DEPTH,
	TUNE_NOISE_GAIN,

	TUNABLE_COUNT,
};

// One tunable's declaration. Everything a UI, a file and an audit need, and no
// pointer to the thing it sets — see `TuningOwner`.
struct Tunable {
	// Stable identifier. Written to the file, so renaming one breaks every
	// preset that mentions it; the loader reports an unknown key rather than
	// dropping it silently.
	const char *key;

	// For the overlay. Short enough to fit a row beside a value.
	const char *label;

	// Displayed after the value. Empty string, not null, when dimensionless.
	const char *unit;

	// Where the number lives, for anyone who wants to read the full argument.
	const char *home;

	double default_value;

	// Hard bounds. A value outside them is clamped on the way in rather than
	// rejected, because the caller is a thumb on a d-pad and the alternative is a
	// UI that stops responding at the end of its range with no explanation.
	//
	// These are **plausibility limits, not opinions about the right value**: the
	// range is wide enough to contain every number anyone might reasonably drive,
	// and narrow enough that a typo in a hand-edited preset cannot put the solver
	// somewhere it will produce NaNs.
	double min_value;
	double max_value;

	// One press of the adjust control. Chosen so about 20 to 40 presses cross the
	// interesting part of the range — fine enough to hear or feel one press,
	// coarse enough to get somewhere while holding a corner.
	double step;

	Provenance provenance;

	// Where the default came from, in one line, in the form a reader can check.
	// For an `Unsourced` default this says what to listen or feel for instead,
	// because that is the only guidance that exists for it.
	const char *citation;

	TuningOwner owner;
};

// The table. **This is issue #159's checklist as data**, and the two are meant
// to be kept in step: a constant that gets a row here gets a line there.
//
// Every citation below is copied from the file the constant lives in rather than
// restated, which is `ARCHITECTURE.md` §5 item 10 applied to this header — a
// second owner of a justification is how justifications rot.
//
// **The strings are pure ASCII, and that is a constraint rather than a style.**
// They cross into Godot as `const char *`, which `godot::String` decodes as
// Latin-1 rather than UTF-8 — so an em dash in a citation reaches the tuning
// overlay as two garbage characters. It reached the audit output as `â` once,
// which is how this is known. Comments in this file are prose and may say
// whatever they like; anything inside quotes is ASCII.
inline constexpr Tunable TUNABLES[TUNABLE_COUNT] = {
	{
			"peak_friction", "tire peak friction", "", "src/core/tire.h",
			2.10, 1.20, 3.00, 0.02, Provenance::Derived,
			"tire.h: on a kart with no downforce mu IS the cornering g. 2.10 is "
			"argued from compound and load, not fitted to tire data. The envelope "
			"test rejected 1.75; ROADMAP M3b records that 2.70 buys only 0.265 g.",
			TuningOwner::Vehicle,
	},
	{
			"max_lock", "steering lock, inner wheel", "rad", "src/core/steering.h",
			0.4363323129985824, 0.1745329252, 0.6981317008, 0.0087266463,
			Provenance::Measured,
			"REFERENCES.md 'Steering lock': NO CIK or KZ source was found. 25 deg "
			"is inherited from the bodywork clearance tables measured in issues "
			"#109 and #110, applied to the inner wheel so no front wheel exceeds it.",
			TuningOwner::Vehicle,
	},
	{
			"frame_torsion", "frame torsional stiffness", "N m/deg",
			"src/core/chassis_flex.h",
			193.62, 100.0, 3500.0, 10.0, Provenance::Sourced,
			"Fu and Wang, 'A study on torsional stiffness of the competition "
			"go-kart frame', WIT Trans. Built Env. Vol 91 (2007): 193,620 N mm/deg. "
			"Sampayo et al. (2021) quote 1,051-3,464 from a different test; the "
			"range is swept because the front/rear lift crossover sits inside it.",
			TuningOwner::Vehicle,
	},
	{
			"drag_area", "drag area, Cd*A", "m^2", "src/core/vehicle.h",
			0.7, 0.30, 1.20, 0.01, Provenance::Derived,
			"vehicle.h: the value tests/core/test_drivetrain.cpp measured its "
			"143.2 km/h against, kept identical so the two top-speed figures "
			"differ only by the tire model and the load transfer.",
			TuningOwner::Vehicle,
	},
	{
			"rolling_resistance", "rolling resistance", "", "src/core/vehicle.h",
			0.012, 0.004, 0.040, 0.001, Provenance::Unsourced,
			"REFERENCES.md records rolling resistance as 'not in the table at "
			"all'. 0.012 is the textbook order of magnitude for a pneumatic tire "
			"on asphalt and nothing here measured it. Feel: it is what decides how "
			"fast the kart coasts down off the pipe.",
			TuningOwner::Vehicle,
	},
	{
			"brake_torque_front", "brake torque, per front wheel", "N m",
			"src/core/vehicle.h",
			180.0, 60.0, 400.0, 5.0, Provenance::Derived,
			"vehicle.h: sized so the pedal reaches the front tire's limit at about "
			"0.9 and past it at 1.0 -- 1318 N per wheel against roughly 1380 N of "
			"tire. The 71/29 split is nine points rear-biased of the 80/20 that "
			"would be optimal at 1.8 g.",
			TuningOwner::Vehicle,
	},
	{
			"brake_torque_rear", "brake torque, rear axle", "N m",
			"src/core/vehicle.h",
			160.0, 40.0, 400.0, 5.0, Provenance::Derived,
			"vehicle.h, same derivation as the front: the rear reaches its limit "
			"first so the kart slides its tail rather than ploughing on locked "
			"fronts, which is what ARCHITECTURE.md 6 asks to be readable.",
			TuningOwner::Vehicle,
	},
	{
			"steer_rate", "steering rate limit", "1/s", "src/core/vehicle.h",
			3.4, 1.0, 12.0, 0.1, Provenance::Unsourced,
			"Carried over from the M3a debug vehicle unchanged and never tuned by "
			"feel, only recorded -- issue #159. 3.4 is center to full lock in "
			"294 ms. It is the rate a driver's hands can actually move the wheel, "
			"and it interacts with steer_gamma, so judge it after that one settles.",
			TuningOwner::Vehicle,
	},
	{
			"steer_gamma", "steering curve exponent", "", "src/vehicle/kart_body.h",
			3.0, 1.0, 6.0, 0.1, Provenance::Derived,
			"ADR-0036's four rows. 3.0 puts the measured 100 km/h limit (0.065 of "
			"lock) at 40% of stick travel instead of 6.5%, which is inside "
			"project.godot's 0.15 deadzone. 1.0 is the exact linear mapping and is "
			"what issue #40's 'assists off' wants. Controller only: nothing in "
			"src/core/ can see it.",
			TuningOwner::Controller,
	},
	{
			"voice_gain", "engine voice gain", "", "scripts/game/engine_voice_rig.gd",
			0.18, 0.0, 1.0, 0.01, Provenance::Unsourced,
			"Deliberately low so the first drive could not be painful. Set against "
			"the other layers once they exist; issue #83 owns the mixing pass.",
			TuningOwner::Audio,
	},
	{
			"voice_unit_size", "voice unit size", "m",
			"scripts/game/engine_voice_rig.gd",
			3.0, 0.5, 20.0, 0.25, Provenance::Unsourced,
			"ADR-0035 lists Godot's 3D attenuation as unsettled, and an "
			"inverse-distance law fits worst for something bolted 600 mm behind "
			"the driver's right ear. Symptom of a wrong value: the note swings in "
			"level as the chase camera moves.",
			TuningOwner::Audio,
	},
	{
			"voice_max_distance", "voice max distance", "m",
			"scripts/game/engine_voice_rig.gd",
			150.0, 20.0, 500.0, 5.0, Provenance::Unsourced,
			"Paired with voice_unit_size and guessed with it. Matters for M7's "
			"twelve karts, where it is the difference between hearing the field "
			"and hearing a wall of engines.",
			TuningOwner::Audio,
	},
	{
			"comb_depth", "exhaust comb depth", "", "src/core/audio_state.h",
			0.30, 0.0, 0.80, 0.01, Provenance::Derived,
			"The smallest default landing inside the measured 1.6-2.6 dB RMS "
			"ripple band, whose midpoint would be 0.34 -- so there is a defensible "
			"range rather than a point. tests/core/test_engine_synth.cpp prints "
			"the depth-to-ripple table it was read off.",
			TuningOwner::Audio,
	},
	{
			"noise_gain", "noise layer gain", "", "src/core/audio_state.h",
			0.12, 0.0, 0.60, 0.01, Provenance::Unsourced,
			"No measurement at all -- issue #159 names it as the clearest example "
			"of a guess in this project. Listen for: too little and the note is a "
			"synthesizer, too much and it stops being a two-stroke.",
			TuningOwner::Audio,
	},
};

inline const Tunable &tunable(int id) {
	return TUNABLES[id];
}

// -1 when the key is unknown, which the loader reports rather than swallowing: a
// preset naming a tunable that no longer exists is either a renamed constant or
// a typo, and both are worth a line on stderr.
//
// `p_len` of -1 means the key is null terminated.
inline int tunable_by_key(const char *key, int p_len = -1) {
	if (key == nullptr) {
		return -1;
	}
	for (int id = 0; id < TUNABLE_COUNT; ++id) {
		const char *candidate = TUNABLES[id].key;
		int index = 0;
		for (;; ++index) {
			const bool key_ended = (p_len >= 0 && index >= p_len) || key[index] == '\0';
			if (key_ended) {
				if (candidate[index] == '\0') {
					return id;
				}
				break;
			}
			if (candidate[index] == '\0' || candidate[index] != key[index]) {
				break;
			}
		}
	}
	return -1;
}

// The grid every stored value snaps to, and the grid the file writes on. One
// millionth: finer than any of these constants is judged to, and coarse enough
// that six decimals is the exact printed form of a stored value rather than a
// rounding of it.
inline constexpr double TUNING_QUANTUM = 1e-6;
inline constexpr int64_t TUNING_SCALE = 1000000;

// Micro-units. The only representation the file format and the hash ever see, so
// that "what was saved" and "what is running" is an integer comparison.
inline int64_t tuning_micro(double value) {
	if (std::isnan(value)) {
		return 0;
	}
	return static_cast<int64_t>(std::llround(value * static_cast<double>(TUNING_SCALE)));
}

inline double tuning_quantize(double value) {
	return static_cast<double>(tuning_micro(value)) / static_cast<double>(TUNING_SCALE);
}

// A complete configuration: one value per tunable, always. Not a sparse map of
// overrides — the sparseness lives in the *file*, and the difference matters,
// because a set that only knows its overrides cannot answer "what is running"
// without also carrying the defaults it was built from.
class TuningSet {
public:
	TuningSet() { reset(); }

	void reset() {
		for (int id = 0; id < TUNABLE_COUNT; ++id) {
			value_[id] = tuning_quantize(TUNABLES[id].default_value);
		}
	}

	double get(int id) const { return value_[id]; }

	// Clamped to the declared range, quantized, stored, and returned — so a
	// caller that shows the user what it set shows the truth rather than what it
	// asked for.
	//
	// **Quantized first and clamped second, and the order is the whole
	// correctness of it.** Clamping to the raw bound and then quantizing lets
	// `llround` round back out of the range: `max_lock`'s maximum is
	// 0.6981317008, which is 698131.7008 micro-units, which rounds to 698132 —
	// 0.698132, three ten-millionths *above* the declared maximum. Small enough
	// that no solver would notice and large enough that "a stored value is
	// inside its bounds" would have been false for the four tunables whose
	// bounds are not round numbers, and any test comparing a clamped value to
	// `max_value` exactly would fail on them.
	//
	// Quantizing the bounds too means the range this class enforces is a range
	// storable values actually live in.
	double set(int id, double value) {
		const Tunable &t = TUNABLES[id];
		const int64_t low = tuning_micro(t.min_value);
		const int64_t high = tuning_micro(t.max_value);
		int64_t micro = std::isnan(value) ? tuning_micro(t.default_value) : tuning_micro(value);
		if (micro < low) {
			micro = low;
		}
		if (micro > high) {
			micro = high;
		}
		value_[id] = static_cast<double>(micro) / static_cast<double>(TUNING_SCALE);
		return value_[id];
	}

	double reset(int id) {
		value_[id] = tuning_quantize(TUNABLES[id].default_value);
		return value_[id];
	}

	// One step of the overlay's adjust control, in either direction.
	double nudge(int id, int steps) {
		return set(id, value_[id] + static_cast<double>(steps) * TUNABLES[id].step);
	}

	// Integer comparison, not `==` on doubles: both sides are quantized, so this
	// is exact and a value nudged up and back down is default again rather than
	// almost-default.
	bool is_default(int id) const {
		return tuning_micro(value_[id]) == tuning_micro(TUNABLES[id].default_value);
	}

	double delta(int id) const {
		return tuning_quantize(value_[id] - tuning_quantize(TUNABLES[id].default_value));
	}

	int changed_count() const {
		int count = 0;
		for (int id = 0; id < TUNABLE_COUNT; ++id) {
			count += is_default(id) ? 0 : 1;
		}
		return count;
	}

	// How many defended defaults have been moved. **This is the number §19 is
	// about**, and it is the one `tools/verify/tuning.sh` prints first: a session
	// can turn every guess it likes and still be within the rules, and one moved
	// sourced constant is a different kind of event.
	int defended_override_count() const {
		int count = 0;
		for (int id = 0; id < TUNABLE_COUNT; ++id) {
			if (!is_default(id) && is_defended(TUNABLES[id].provenance)) {
				++count;
			}
		}
		return count;
	}

	// Fingerprint of the whole configuration. See the header note: this is
	// deliberately NOT mixed into `StateHash`.
	//
	// The key is hashed alongside the value so that appending a tunable, or
	// renaming one, changes the digest. A hash over values alone would report two
	// different vocabularies as the same configuration.
	uint64_t hash() const {
		StateHash digest(TUNING_QUANTUM);
		for (int id = 0; id < TUNABLE_COUNT; ++id) {
			for (const char *c = TUNABLES[id].key; *c != '\0'; ++c) {
				digest.add_uint64(static_cast<uint64_t>(static_cast<unsigned char>(*c)));
			}
			digest.add_int(tuning_micro(value_[id]));
		}
		return digest.digest();
	}

private:
	double value_[TUNABLE_COUNT] = {};
};

// The digest of an untouched set. The gate compares against this, so a §6.4
// figure measured under a preset cannot be recorded as a reference figure.
inline uint64_t default_tuning_hash() {
	return TuningSet().hash();
}

// --- text ------------------------------------------------------------------
//
// Hand-rolled, and the reason is in the header: `snprintf("%f")` respects the C
// locale, and a machine running a decimal-comma locale would write a file this
// parser rejects. `KartStateHash::hex` pads by hand for the same class of
// reason. Every function below writes into a caller-supplied buffer and returns
// the length written, or -1 if it did not fit.

inline constexpr int TUNING_VALUE_CHARS = 32; // -9223372036854.775808 and change
inline constexpr int TUNING_LINE_CHARS = 512;
inline constexpr int TUNING_KEY_CHARS = 64;
inline constexpr int TUNING_TEXT_CHARS = 96;

// "2.400000", "-0.600000", "0.000000". Always six decimals, always a leading
// zero, and a minus sign only when the micro-value is genuinely negative — so
// -0.0000004 prints as "0.000000" rather than "-0.000000" and two values that
// hash alike also print alike.
inline int format_value(double value, char *out, int cap) {
	int64_t micro = tuning_micro(value);
	const bool negative = micro < 0;
	uint64_t magnitude = negative ? static_cast<uint64_t>(-micro) : static_cast<uint64_t>(micro);

	char digits[TUNING_VALUE_CHARS];
	int count = 0;
	do {
		digits[count++] = static_cast<char>('0' + static_cast<int>(magnitude % 10));
		magnitude /= 10;
	} while (magnitude != 0 && count < TUNING_VALUE_CHARS);
	// Six decimals plus at least one integer digit.
	while (count < 7) {
		digits[count++] = '0';
	}

	const int length = count + 1 + (negative ? 1 : 0);
	if (length + 1 > cap) {
		return -1;
	}
	int written = 0;
	if (negative) {
		out[written++] = '-';
	}
	for (int i = count - 1; i >= 0; --i) {
		if (i == 5) {
			out[written++] = '.';
		}
		out[written++] = digits[i];
	}
	out[written] = '\0';
	return written;
}

// "0x8f1c4a2b6d0e7391", sixteen digits, always. Written by hand rather than with
// `%016llx` for consistency with the above, and because CLAUDE.md records that
// GDScript's `pad_zeros` gets exactly this wrong on the other side of the wire.
inline int format_hex64(uint64_t value, char *out, int cap) {
	const int length = 18;
	if (length + 1 > cap) {
		return -1;
	}
	out[0] = '0';
	out[1] = 'x';
	for (int i = 0; i < 16; ++i) {
		const int nibble = static_cast<int>((value >> ((15 - i) * 4)) & 0xF);
		out[2 + i] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
	}
	out[length] = '\0';
	return length;
}

// Parse a decimal number written by `format_value`, or by a person editing the
// file: an optional sign, digits, an optional fractional part of any length.
// Anything else fails rather than parsing a prefix, because a silently truncated
// "3.0abc" is a tuned kart nobody asked for.
//
// The fractional part is accumulated in integer micro-units and digits past the
// sixth are read only to round the last one, so the result is exactly the value
// `format_value` would print back.
inline bool parse_value(const char *text, int len, double &out) {
	if (text == nullptr || len <= 0) {
		return false;
	}
	int index = 0;
	bool negative = false;
	if (text[index] == '+' || text[index] == '-') {
		negative = text[index] == '-';
		++index;
	}
	int64_t whole = 0;
	int whole_digits = 0;
	while (index < len && text[index] >= '0' && text[index] <= '9') {
		whole = whole * 10 + (text[index] - '0');
		++whole_digits;
		++index;
		if (whole > 9000000000LL) {
			return false;
		}
	}
	int64_t fraction = 0;
	int fraction_digits = 0;
	bool round_up = false;
	if (index < len && text[index] == '.') {
		++index;
		while (index < len && text[index] >= '0' && text[index] <= '9') {
			const int digit = text[index] - '0';
			if (fraction_digits < 6) {
				fraction = fraction * 10 + digit;
				++fraction_digits;
			} else if (fraction_digits == 6) {
				round_up = digit >= 5;
				++fraction_digits;
			}
			++index;
		}
		if (fraction_digits == 0) {
			return false;
		}
	}
	if (whole_digits == 0 && fraction_digits == 0) {
		return false;
	}
	if (index != len) {
		return false;
	}
	while (fraction_digits < 6) {
		fraction *= 10;
		++fraction_digits;
	}
	int64_t micro = whole * TUNING_SCALE + fraction + (round_up ? 1 : 0);
	if (negative) {
		micro = -micro;
	}
	out = static_cast<double>(micro) / static_cast<double>(TUNING_SCALE);
	return true;
}

// One entry line, complete with its trailing comment. Returns -1 if the tunable
// is at its default — a preset is a diff, so a default has no line.
//
//     ! max_lock = 0.523599    # default 0.436332, +0.087266 (measured) OVERRIDE: ...
//
// The comment is not decoration and is not re-read on load. It is there so the
// file answers "what has been tuned away from its source, and by how much"
// without a tool, which is the property that makes a preset reviewable in a pull
// request rather than only runnable.
inline int format_entry(const TuningSet &set, int id, char *out, int cap) {
	if (set.is_default(id)) {
		return -1;
	}
	const Tunable &t = TUNABLES[id];

	char value_text[TUNING_VALUE_CHARS];
	char default_text[TUNING_VALUE_CHARS];
	char delta_text[TUNING_VALUE_CHARS];
	if (format_value(set.get(id), value_text, TUNING_VALUE_CHARS) < 0 ||
			format_value(t.default_value, default_text, TUNING_VALUE_CHARS) < 0) {
		return -1;
	}
	const double difference = set.delta(id);
	int delta_written = 0;
	if (difference > 0.0) {
		delta_text[delta_written++] = '+';
	}
	if (format_value(difference, delta_text + delta_written,
				TUNING_VALUE_CHARS - delta_written) < 0) {
		return -1;
	}

	const bool defended = is_defended(t.provenance);
	int written = 0;
	auto append = [&](const char *text) {
		for (const char *c = text; *c != '\0'; ++c) {
			if (written + 1 >= cap) {
				written = -1;
				return;
			}
			out[written++] = *c;
		}
	};

	if (defended) {
		append("! ");
	}
	append(t.key);
	append(" = ");
	append(value_text);
	append("  # default ");
	append(default_text);
	append(", ");
	append(delta_text);
	append(" (");
	append(provenance_name(t.provenance));
	append(")");
	if (defended) {
		append(" OVERRIDE: ");
		append(t.citation);
	}
	if (written < 0 || written >= cap) {
		return -1;
	}
	out[written] = '\0';
	return written;
}

// What a line turned out to be. `Header` covers `format`, `name`, `defaults` and
// `tuned` — anything of the shape `word text`, with the word in `key` and the
// rest in `text`.
struct ParsedLine {
	enum Kind {
		Blank,
		Comment,
		Header,
		Entry,
		Invalid,
	};

	Kind kind = Blank;
	char key[TUNING_KEY_CHARS] = {};
	char text[TUNING_TEXT_CHARS] = {};
	double value = 0.0;
	int id = -1; // resolved tunable, or -1 for an unknown key on an Entry
	bool defended_marker = false; // the leading '!'
};

// Parse one line. Never allocates, never reads past `len`, and treats a `#`
// anywhere outside a header's text as the start of a comment.
//
// A malformed entry returns `Invalid` rather than being skipped. A preset that
// half-loaded would be a kart tuned to something nobody wrote down.
inline ParsedLine parse_line(const char *line, int len) {
	ParsedLine result;
	if (line == nullptr) {
		result.kind = ParsedLine::Blank;
		return result;
	}
	if (len < 0) {
		len = 0;
		while (line[len] != '\0') {
			++len;
		}
	}
	// Trim, including a stray carriage return from a file written on Windows.
	int begin = 0;
	int end = len;
	auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while (begin < end && is_space(line[begin])) {
		++begin;
	}
	while (end > begin && is_space(line[end - 1])) {
		--end;
	}
	if (begin == end) {
		result.kind = ParsedLine::Blank;
		return result;
	}
	if (line[begin] == '#') {
		result.kind = ParsedLine::Comment;
		return result;
	}
	if (line[begin] == '!') {
		result.defended_marker = true;
		++begin;
		while (begin < end && is_space(line[begin])) {
			++begin;
		}
	}

	// Strip a trailing comment.
	for (int i = begin; i < end; ++i) {
		if (line[i] == '#') {
			end = i;
			break;
		}
	}
	while (end > begin && is_space(line[end - 1])) {
		--end;
	}
	if (begin == end) {
		result.kind = result.defended_marker ? ParsedLine::Invalid : ParsedLine::Comment;
		return result;
	}

	// The key runs to whitespace or '='.
	int key_end = begin;
	while (key_end < end && !is_space(line[key_end]) && line[key_end] != '=') {
		++key_end;
	}
	const int key_length = key_end - begin;
	if (key_length <= 0 || key_length >= TUNING_KEY_CHARS) {
		result.kind = ParsedLine::Invalid;
		return result;
	}
	for (int i = 0; i < key_length; ++i) {
		result.key[i] = line[begin + i];
	}

	int rest = key_end;
	while (rest < end && is_space(line[rest])) {
		++rest;
	}
	const bool assignment = rest < end && line[rest] == '=';
	if (!assignment) {
		// A header line: the key, then everything else verbatim.
		if (result.defended_marker) {
			result.kind = ParsedLine::Invalid;
			return result;
		}
		const int text_length = end - rest;
		if (text_length >= TUNING_TEXT_CHARS) {
			result.kind = ParsedLine::Invalid;
			return result;
		}
		for (int i = 0; i < text_length; ++i) {
			result.text[i] = line[rest + i];
		}
		result.kind = ParsedLine::Header;
		return result;
	}

	++rest;
	while (rest < end && is_space(line[rest])) {
		++rest;
	}
	if (!parse_value(line + rest, end - rest, result.value)) {
		result.kind = ParsedLine::Invalid;
		return result;
	}
	result.id = tunable_by_key(result.key, key_length);
	result.kind = ParsedLine::Entry;
	return result;
}

} // namespace kart::core

#endif // KART_CORE_TUNING_H
