#ifndef KART_CORE_LAP_TIMING_H
#define KART_CORE_LAP_TIMING_H

#include "core/session.h"

#include <cstdint>

// Laps, sectors and whether the lap counts. ROADMAP M3c's Practice, and M6's
// lap counting and track limits ahead of time because Practice needs all of it.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// Allocation-free, fixed-size arrays, no wall clock.
//
// ## Time is counted in ticks, never in seconds
//
// `advance` takes a tick count and multiplies by the step. It does not take a
// delta, and the reason is `kart_body.h`'s: `_physics_process`'s `delta` is a
// float that wobbles in its last bits, and `max_physics_steps_per_frame` clamps
// without banking, so a frame-rate collapse makes wall-clock and simulation time
// diverge by a measured 0.6476. A lap time built from wall-clock deltas is a lap
// time that gets faster on a slow machine. A lap time built from ticks is the
// same number in a replay, which is the whole point.
//
// ## A lap is a sequence of crossings, not a distance that wrapped
//
// The obvious implementation watches arc length and calls a lap when the number
// jumps from near the length back to near zero. It fails on the two cases that
// actually happen: a kart parked on the start line jitters across it, and a kart
// that spins and rejoins backwards counts a lap by driving the wrong way over a
// line it never left.
//
// So the track publishes **ordered marks in meters of arc length** — ADR-0046 puts
// furniture at arc length rather than at control-point indices — and a lap is
// crossing every one of them in order, forward. Crossing one out of order does not
// count it, and it costs the lap: that is the cheapest possible cut detection and
// it falls out of the same machinery rather than being a second system.
//
// ## Which rules here are the FIA's and which are ours
//
// **The off-track definition is theirs.** General Prescriptions Art. 2.14 B: "If
// the four wheels of a kart are outside these lines, the kart is considered as
// having left the track." This header takes a bool that means exactly that and
// does not decide it — counting wheels is the boundary's job.
//
// **The penalty is ours, and `docs/GAMEDESIGN.md` §4 says so in those words.** The
// regulations attach no penalty to leaving the track by yourself; Art. 2.24's
// 5 seconds is for *Incidents*, and the Incident list penalizes having "forced
// another Driver out of the track". Real enforcement appears to live in a per-event
// Race Director document that could not be obtained. So invalidating a lap for
// running wide is this project's rule, it is labeled as ours here, and it is not
// dressed up with a citation it does not have.
//
// **Two things the regulations do give us and which are cheap and correct.** A
// qualifying penalty deletes the driver's **fastest** lap of the session rather
// than the offending one — see `invalidate_fastest`, which exists for that and
// nothing else. And a kart restarted with outside help is disqualified from the
// session, which is what `respawned` records.

namespace kart::core {

// Most marks a lap may have: a start line plus sector splits plus intermediate
// checkpoints. Generous, because ADR-0046 makes the count data and a circuit that
// wants eight checkpoints is a content decision rather than a code change.
//
// **32 and not 16, because Valdirone Nuova needs exactly 16.** Its forward layout
// authors two sector marks and fourteen checkpoints, one of which is the start
// line, so the merged list is 1 + 13 + 2 = 16 marks — sitting precisely on the old
// ceiling, where adding a single checkpoint to any circuit refuses the layout. The
// array is 32 doubles per kart and the timer touches a handful of them per tick,
// so the headroom costs nothing measurable and the alternative is a content edit
// that fails at load with an arithmetic reason nobody expects.
inline constexpr int LAP_MAX_MARKS = 32;

// Most sectors a lap may be divided into. Three is the convention every timing
// screen in the sport uses and what `GAMEDESIGN.md` §7's timing screens assume;
// the array is larger so a long circuit is not a rebuild.
inline constexpr int LAP_MAX_SECTORS = 8;

// A lap time that does not exist yet.
//
// Not zero, and not a small number: zero is a plausible-looking lap time and it
// sorts first, so a fresh session's "best" would beat every real lap forever. Any
// consumer comparing against this gets a number no kart can produce.
inline constexpr double LAP_TIME_NONE = -1.0;

inline constexpr bool lap_time_exists(double seconds) {
	return seconds > 0.0;
}

// How much of the circuit a kart must have driven forward before a wrap in arc
// length is taken as a completed lap.
//
// Not 1.0, because a cut has to still complete its lap so that the missed mark can
// invalidate it — swallowing the lap entirely would leave a driver who cut a corner
// with no lap on the screen at all and no explanation. Not small either: at 0.5 a
// kart shuttling back and forth over the line region is safe, and half a circuit is
// far more than any legal shortcut a checkpointed track allows.
inline constexpr double LAP_TRAVEL_FRACTION = 0.5;

// Fastest a kart is believed to be moving, meters per second. Above this a step is
// treated as a discontinuity rather than as driving: a teleport, a cut across the
// infield, or a scene that moved the body without telling the timer.
//
// 60 m/s is 216 km/h against `kz_reference.h`'s 135-145 km/h top speed — half again
// above what the kart can do, so no amount of downhill or curb launch reaches it,
// and it is still 24 times the 0.4 m a kart covers in a 120 Hz tick at full speed.
// Anything faster is not a kart driving.
inline constexpr double LAP_MAX_SPEED_MS = 60.0;

// Why a lap does not count. One reason, the first one that applied, because a
// timing screen has room for one and "off track" is more useful to a driver than
// "off track and also you missed a checkpoint because you were off track".
enum class LapInvalidReason : int {
	Valid = 0,
	OffTrack, // all four wheels outside the line. Ours, per the header.
	MissedMark, // a mark crossed out of order, or a mark never crossed
	Respawned, // restarted with outside help, which is a real disqualification
	Deleted, // taken away afterwards: a qualifying penalty deletes the fastest lap
	OutLap, // the first lap of a session, which starts from a standing kart
};

inline constexpr int LAP_INVALID_REASON_COUNT = 6;

inline const char *lap_invalid_reason_name(LapInvalidReason reason) {
	switch (reason) {
		case LapInvalidReason::Valid: return "valid";
		case LapInvalidReason::OffTrack: return "off_track";
		case LapInvalidReason::MissedMark: return "missed_mark";
		case LapInvalidReason::Respawned: return "respawned";
		case LapInvalidReason::Deleted: return "deleted";
		case LapInvalidReason::OutLap: return "out_lap";
	}
	return "invalid";
}

// Two marks that land this close together are one mark, meters.
//
// A millimeter, and it is the same figure and the same reason as the schema's rule
// for coincident control points: two authored stations that mean the same place
// arrive separated by float rounding, and a sector of 0.0001 m divides by nearly
// zero in every consumer downstream. The merge in `from_stations` is where a
// checkpoint that was placed on top of a sector mark becomes that sector mark
// rather than a second mark a tenth of a millimeter past it.
inline constexpr double LAP_MARK_MERGE_M = 1e-3;

// Where the timing marks are, in meters of arc length from the start line.
//
// `marks[0]` is the start line and is always 0.0. The rest ascend and are all
// below `length_m`.
//
// ## A mark is not a sector, and conflating them showed a timing screen 16 sectors
//
// ADR-0046 puts two independent lists in `track.json`, and they are independent
// because they answer different questions. **Sector marks** are where the timing
// screen splits the lap — Valdirone authors two, at 524 m and 902 m, chosen as a
// diagnostic partition rather than as thirds. **Checkpoints** are the anti-cut
// resolution — fourteen of them, evenly spaced, and `circuit_reference.h` sets the
// spacing so that no legal line skips one. Both have to be crossed in order,
// because that is how a cut is detected; only the sector marks may produce a split.
//
// This struct held one array and defined `sector_count()` as `mark_count`, which
// is correct exactly while the two lists are the same list. Feeding it a real
// circuit's merged marks reported sixteen sectors, wrote `sector_s[8..15]` off the
// end of a `LAP_MAX_SECTORS` array — guarded, so it silently dropped them — and
// left `optimal_lap_s` summing eight bests out of sixteen sectors that had none.
//
// So `checkpoint_only[i]` says a mark is an anti-cut checkpoint that starts no
// sector. **The flag is inverted on purpose**: zero-initialization means "every
// mark starts a sector", which is what a hand-built `LapMarks` and the authored-
// marks path both meant before this existed, so nothing that predates the flag
// changes behavior by acquiring it.
//
// One struct rather than parallel arrays because a layout with its sector count out
// of step with its mark count is the kind of thing that produces a timing screen
// showing sector 4 of 3.
struct LapMarks {
	double length_m = 0.0;
	double mark_m[LAP_MAX_MARKS] = {};
	// See above: false means this mark starts a sector.
	bool checkpoint_only[LAP_MAX_MARKS] = {};
	int mark_count = 0;

	// Whether this describes a lap that can be timed. Checked rather than trusted:
	// ADR-0046 makes validation part of the schema, because furniture can be wrong
	// in ways geometry cannot and a track that loads and cannot be raced is worse
	// than one that refuses to load.
	bool is_valid() const {
		if (!(length_m > 0.0)) {
			return false;
		}
		if (mark_count < 1 || mark_count > LAP_MAX_MARKS) {
			return false;
		}
		if (mark_m[0] != 0.0) {
			return false;
		}
		// The start line opens sector 0. A layout whose first mark is a checkpoint has
		// no sector at all until the second mark, so the first split of every lap would
		// be attributed to a sector that had not started.
		if (checkpoint_only[0]) {
			return false;
		}
		for (int i = 1; i < mark_count; ++i) {
			// Strictly ascending: two marks at the same arc length are one mark and a
			// sector of zero length, which divides by nothing and reports a 0.000 split
			// that looks like a very fast sector.
			if (!(mark_m[i] > mark_m[i - 1])) {
				return false;
			}
		}
		// More sectors than there is room to time. Refused here rather than clamped in
		// `complete_lap`, which is what used to happen: the splits past the eighth were
		// dropped by a bounds guard and the timing screen showed a lap whose sectors did
		// not add up to it.
		if (sector_count() > LAP_MAX_SECTORS) {
			return false;
		}
		return mark_m[mark_count - 1] < length_m;
	}

	int sector_count() const {
		int count = 0;
		for (int i = 0; i < mark_count && i < LAP_MAX_MARKS; ++i) {
			if (!checkpoint_only[i]) {
				++count;
			}
		}
		return count;
	}

	// Which sector the kart is in once it has crossed mark `index`.
	//
	// Computed rather than cached. The count is at most 32 and this is called on a
	// mark crossing rather than per tick, and a cache would be a second copy of the
	// flags that a hand edit in a test — or a `begin_marks` caller filling the arrays
	// directly — could leave disagreeing with the first.
	int sector_of_mark(int index) const {
		if (index < 0 || index >= mark_count) {
			return 0;
		}
		int sector = -1;
		for (int i = 0; i <= index; ++i) {
			if (!checkpoint_only[i]) {
				++sector;
			}
		}
		return sector < 0 ? 0 : sector;
	}

	// The mark that opens sector `index`, or -1.
	int mark_of_sector(int index) const {
		if (index < 0) {
			return -1;
		}
		int sector = -1;
		for (int i = 0; i < mark_count; ++i) {
			if (!checkpoint_only[i] && ++sector == index) {
				return i;
			}
		}
		return -1;
	}

	// Length of sector `index`, meters. The last one wraps to the start line.
	double sector_length_m(int index) const {
		const int mark = mark_of_sector(index);
		if (mark < 0) {
			return 0.0;
		}
		const int next = mark_of_sector(index + 1);
		const double to = next >= 0 ? mark_m[next] : length_m;
		return to - mark_m[mark];
	}

	// Three equal sectors on a closed lap, which is what a circuit gets before
	// anybody has authored splits for it. Convenience, and deliberately not the
	// only way to build one: real splits go in `track.json` per ADR-0046, and a
	// generated even split is a placeholder that says so by being one line.
	static LapMarks even(double length_m, int sectors) {
		LapMarks marks;
		if (!(length_m > 0.0) || sectors < 1 || sectors > LAP_MAX_SECTORS) {
			return marks;
		}
		marks.length_m = length_m;
		marks.mark_count = sectors;
		for (int i = 0; i < sectors; ++i) {
			marks.mark_m[i] = length_m * static_cast<double>(i) / static_cast<double>(sectors);
		}
		return marks;
	}

	// A layout's two authored lists, merged into one ordered set of marks.
	//
	// `sectors` are the split points and do **not** include the start line — that is
	// how `track.json` writes them, because a sector mark at 0.0 would be the line
	// twice. `checkpoints` are the anti-cut stations and normally *do* include 0.0,
	// which merges into the start line rather than becoming a second mark on top of
	// it.
	//
	// Returns an invalid `LapMarks` — check `is_valid()` — rather than a clamped one
	// when the lists cannot be timed: over capacity, out of range, or unsorted after
	// the merge. A caller that got a clamped set would be racing a lap missing the
	// checkpoints that were dropped, which is a cut detector with holes in it.
	static LapMarks from_stations(double length_m, const double *sectors, int sector_count,
			const double *checkpoints, int checkpoint_count) {
		LapMarks marks;
		if (!(length_m > 0.0)) {
			return marks;
		}
		marks.length_m = length_m;

		// The start line, always, and always a sector boundary.
		marks.mark_m[0] = 0.0;
		marks.checkpoint_only[0] = false;
		marks.mark_count = 1;

		// **Sector marks are inserted first and keep their exact authored station.** A
		// checkpoint within the merge tolerance of one is absorbed by it; doing this the
		// other way round would let a checkpoint half a millimeter short of 524.0 become
		// the sector mark, and the split a driver reads would not be the split the design
		// document specifies.
		for (int pass = 0; pass < 2; ++pass) {
			const double *list = pass == 0 ? sectors : checkpoints;
			const int count = pass == 0 ? sector_count : checkpoint_count;
			if (list == nullptr) {
				continue;
			}
			for (int i = 0; i < count; ++i) {
				const double station = list[i];
				if (!(station >= 0.0) || !(station < length_m)) {
					// Off the lap. Refused rather than wrapped: a station past the length is an
					// authoring error, and wrapping it would put a mark at a place the design
					// never asked for.
					return LapMarks();
				}
				if (!marks.insert(station, pass == 1)) {
					return LapMarks();
				}
			}
		}
		return marks;
	}

private:
	// Insert one station in order, merging into an existing mark within the
	// tolerance. Returns false when there is no room. A sector mark that lands on a
	// checkpoint promotes it — the union of the two lists is what has to be crossed,
	// and a mark that is both is a sector boundary.
	bool insert(double station, bool checkpoint) {
		for (int i = 0; i < mark_count; ++i) {
			const double gap = station - mark_m[i];
			if (gap > LAP_MARK_MERGE_M) {
				continue;
			}
			if (gap >= -LAP_MARK_MERGE_M) {
				if (!checkpoint) {
					mark_m[i] = station;
					checkpoint_only[i] = false;
				}
				return true;
			}
			// Strictly before mark `i` and not touching it: open a hole and drop it in.
			if (mark_count >= LAP_MAX_MARKS) {
				return false;
			}
			for (int j = mark_count; j > i; --j) {
				mark_m[j] = mark_m[j - 1];
				checkpoint_only[j] = checkpoint_only[j - 1];
			}
			mark_m[i] = station;
			checkpoint_only[i] = checkpoint;
			++mark_count;
			return true;
		}
		if (mark_count >= LAP_MAX_MARKS) {
			return false;
		}
		mark_m[mark_count] = station;
		checkpoint_only[mark_count] = checkpoint;
		++mark_count;
		return true;
	}
};

// One completed lap.
struct LapRecord {
	double time_s = LAP_TIME_NONE;
	double sector_s[LAP_MAX_SECTORS] = {};
	int sector_count = 0;
	LapInvalidReason reason = LapInvalidReason::Valid;
	int lap_number = 0; // 1 for the first lap completed

	bool is_valid() const {
		return reason == LapInvalidReason::Valid && lap_time_exists(time_s);
	}
};

// What the driver is doing right now, for the HUD.
struct LapProgress {
	double lap_time_s = 0.0; // time on the current lap
	int sector = 0; // 0-based, which sector the kart is in
	double sector_time_s = 0.0; // time in the current sector
	int lap_number = 0; // laps completed
	double distance_m = 0.0; // arc length along the current lap
	LapInvalidReason reason = LapInvalidReason::Valid; // of the lap in progress
};

// The timer.
//
// One kart. A field is `entry_count` of these, which is why nothing in here is a
// singleton and why the marks are passed in rather than owned: eight karts share
// one layout and the layout is not eight copies.
class LapTimer {
public:
	LapTimer() = default;

	// `step_s` is the fixed physics step, and it is the only place time comes from.
	void begin(const LapMarks &marks, double step_s) {
		marks_ = marks;
		step_s_ = step_s;
		reset();
	}

	void reset() {
		lap_ticks_ = 0;
		sector_ticks_ = 0;
		sector_ = 0;
		laps_done_ = 0;
		next_mark_ = 1;
		distance_m_ = 0.0;
		lap_travel_m_ = 0.0;
		have_distance_ = false;
		// **The first lap is an out lap and cannot be a best lap.** A session starts
		// with a standing kart on the line, so its first crossing is a rolling start
		// from zero and the "lap" before it is not a lap at all. Practice is worse than
		// a race here: a driver leaves the pits, drives half a circuit and crosses the
		// line, and a timer that trusted its own zero would record a 24-second lap
		// record nothing can ever beat.
		reason_ = LapInvalidReason::OutLap;
		for (int i = 0; i < LAP_MAX_SECTORS; ++i) {
			sector_ticks_done_[i] = 0;
		}
		last_ = LapRecord();
		best_ = LapRecord();
		valid_laps_ = 0;
		invalid_laps_ = 0;
		for (int i = 0; i < LAP_MAX_SECTORS; ++i) {
			best_sector_s_[i] = LAP_TIME_NONE;
		}
	}

	// One or more physics ticks at a known place on the track.
	//
	// `distance_m` is arc length along the lap, which the boundary computes by
	// projecting the kart onto the centerline. `off_track` is the FIA's definition
	// and nothing else: all four wheels outside the line.
	//
	// Returns true when a lap was completed on this call, so the caller can read
	// `last()` without polling for a change.
	bool advance(double distance_m, bool off_track, int ticks = 1) {
		if (ticks < 1) {
			return false;
		}
		lap_ticks_ += ticks;
		sector_ticks_ += ticks;

		// Off track taints the lap in progress and nothing else. It does not end the
		// lap: the FIA definition is a state, the driver is expected to rejoin, and a
		// lap that ended the moment somebody put a wheel on the grass would make the
		// timing screen lose the kart entirely.
		if (off_track) {
			taint(LapInvalidReason::OffTrack);
		}

		if (!have_distance_) {
			// First sight of the kart. Arm from where it is rather than from the line —
			// see `arm_marks_from` for what arming from zero would report.
			distance_m_ = distance_m;
			have_distance_ = true;
			arm_marks_from(distance_m);
			return false;
		}
		const double previous = distance_m_;
		distance_m_ = distance_m;

		return cross_marks(previous, distance_m);
	}

	// How far the kart has actually travelled forward since the last line crossing.
	// Exposed because it is the number that decides whether a wrap was a lap, and a
	// timing bug that hinges on it is otherwise invisible from outside.
	double lap_travel_m() const { return lap_travel_m_; }

	// Ticks on the current lap, for a caller that wants the count rather than the
	// seconds. The two differ by exactly `step_s`, and this is the one that is
	// integral and therefore comparable across runs.
	int64_t lap_ticks() const { return lap_ticks_;
	}

	// The kart was put back on the track by something other than the driver.
	//
	// A real rule rather than ours: a kart restarted with outside help is
	// disqualified from that session, and a respawn is exactly that. It kills the
	// lap in progress; whether it kills the session is the session runner's
	// decision and not this class's.
	void respawn(double distance_m) {
		taint(LapInvalidReason::Respawned);
		distance_m_ = distance_m;
		have_distance_ = true;
		// The kart has been moved, so the marks it had already crossed no longer say
		// where it is. Re-arm from wherever it was put down: every mark ahead of it is
		// still owed, which is what makes a respawn past a checkpoint a missed mark
		// rather than a free lap.
		arm_marks_from(distance_m);
	}

	// Take away the fastest valid lap of the session.
	//
	// **This is a real regulation and it is the surprising one.** A qualifying
	// penalty deletes the driver's *fastest* lap, not the lap the offense happened
	// on. Written as its own function because implementing it as "invalidate the
	// current lap" is the obvious wrong thing and would look correct in every test
	// that only drives one lap.
	//
	// Returns the time that was deleted, or `LAP_TIME_NONE` if there was nothing to
	// delete. The best lap becomes the next best valid one, which this class cannot
	// recover — it does not keep every lap — so the caller owns the lap history if
	// it needs one. Deliberate: a 108,000-tick session times eight karts and a full
	// lap history per kart is the session runner's storage decision, not the
	// timer's.
	double invalidate_fastest() {
		if (!best_.is_valid()) {
			return LAP_TIME_NONE;
		}
		const double deleted = best_.time_s;
		best_ = LapRecord();
		best_.reason = LapInvalidReason::Deleted;
		for (int i = 0; i < LAP_MAX_SECTORS; ++i) {
			best_sector_s_[i] = LAP_TIME_NONE;
		}
		if (valid_laps_ > 0) {
			--valid_laps_;
		}
		++invalid_laps_;
		return deleted;
	}

	const LapRecord &last() const { return last_; }
	const LapRecord &best() const { return best_; }
	int valid_lap_count() const { return valid_laps_; }
	int invalid_lap_count() const { return invalid_laps_; }
	int laps_completed() const { return laps_done_; }

	double best_sector_s(int index) const {
		if (index < 0 || index >= LAP_MAX_SECTORS) {
			return LAP_TIME_NONE;
		}
		return best_sector_s_[index];
	}

	// The theoretical best: every sector's best, added up. A number a driver
	// chases and a number that is never a lap time, so it is reported separately
	// rather than mixed into `best()`.
	double optimal_lap_s() const {
		double total = 0.0;
		for (int i = 0; i < marks_.sector_count(); ++i) {
			if (!lap_time_exists(best_sector_s_[i])) {
				return LAP_TIME_NONE;
			}
			total += best_sector_s_[i];
		}
		return marks_.sector_count() > 0 ? total : LAP_TIME_NONE;
	}

	LapProgress progress() const {
		LapProgress out;
		out.lap_time_s = static_cast<double>(lap_ticks_) * step_s_;
		out.sector = sector_;
		out.sector_time_s = static_cast<double>(sector_ticks_) * step_s_;
		out.lap_number = laps_done_;
		out.distance_m = distance_m_;
		out.reason = reason_;
		return out;
	}

	// Whether there is anything to compare the current sector against.
	//
	// **Ask this before reading the delta, because the delta cannot say so itself.**
	// A delta is negative when the driver is ahead and `LAP_TIME_NONE` is -1.0, so
	// "one second up" and "no comparison" are the same number. Every guard that
	// matters is in this one function rather than repeated at each caller — a second
	// copy of the condition is how a HUD ends up showing a plausible -1.000 all
	// session.
	bool has_sector_delta(const LapRecord &reference) const {
		if (sector_ < 0 || sector_ >= reference.sector_count) {
			return false;
		}
		return lap_time_exists(reference.sector_s[sector_]);
	}

	// Split against a reference lap: negative is ahead. `LAP_TIME_NONE` when there
	// is nothing to compare, which is not zero — zero means dead level and is a
	// thing a driver reads as good news. See `has_sector_delta` above.
	double sector_delta_s(const LapRecord &reference) const {
		if (!has_sector_delta(reference)) {
			return LAP_TIME_NONE;
		}
		return static_cast<double>(sector_ticks_) * step_s_ - reference.sector_s[sector_];
	}

private:
	void taint(LapInvalidReason reason) {
		// First reason wins. A lap that went off, then missed the checkpoint it was
		// standing next to, is reported as having gone off — which is the thing the
		// driver did and the thing they can fix.
		if (reason_ == LapInvalidReason::Valid) {
			reason_ = reason;
		}
	}

	// Did the kart pass any marks between two positions, and in the right order?
	//
	// Three things make this harder than `previous < m <= current`, and each one is a
	// bug the tests in `test_lap_timing.cpp` found before this comment existed.
	//
	// **1. Arc length wraps, so the step that closes a lap runs downhill.** A
	// forward crossing of the line goes from near the length to near zero, which is
	// the largest negative step there is.
	//
	// **2. A jitter across the line is indistinguishable from a lap, by size.** A
	// kart parked on the line sits at 1199.95 m one tick and 0.05 m the next: a fall
	// of 1,199.9 m going forward and a rise of 1,199.9 m going back. Thresholding on
	// the size of the step counts a lap per wobble in one direction and credits a
	// whole circuit of travel in the other. So the *signed shortest way round* is
	// what a step means — `circular_delta` — and never the raw subtraction.
	//
	// **3. A discontinuity must not consume the marks it flew over.** A kart that
	// cuts the infield appears 400 m further on in one tick. Consuming every mark in
	// that interval is exactly backwards: those are the marks it skipped, and they
	// are the only evidence the cut happened. So a step larger than a kart can
	// physically travel in one tick consumes nothing, banks no travel, and closes no
	// lap. Nothing legitimate reaches that size, because `max_step_m` is set from a
	// speed half again above this kart's top speed.
	//
	// What is left is small: a lap closes when a *plausible forward* step crosses the
	// line and at least one intermediate mark has been consumed since the last
	// crossing. The mark is what a jittering kart can never produce and what a real
	// lap cannot avoid.
	bool cross_marks(double previous, double current) {
		if (marks_.mark_count < 1) {
			return false;
		}

		const double delta = circular_delta(previous, current);
		if (delta > max_step_m() || delta < -max_step_m()) {
			// A teleport, a cut, or a scene that moved the kart without saying so. The
			// marks in between stay owed, and the lap is spoiled *now* rather than at
			// the line: a driver who cuts the infield should see the lap struck out as
			// it happens, and a respawn does not come through here — `respawn` sets the
			// position itself, so the following step is an ordinary small one.
			taint(LapInvalidReason::MissedMark);
			return false;
		}

		if (delta > 0.0) {
			lap_travel_m_ += delta;
		}

		// Forward across the start line.
		//
		// **`current < previous` and not `previous + delta >= length`.** The second
		// form reconstructs the unwrapped position by adding the circular delta back
		// on, and in floating point it lands a hair under the length on the exact step
		// that closes the lap — 1199.7916 + 0.2083 is not 1200.0 — so laps went
		// uncounted and the tests reported an out lap forever. A forward step that
		// crosses zero always makes the raw arc length fall, and that comparison is
		// exact.
		const bool crossed_line = delta > 0.0 && current < previous;

		if (!crossed_line) {
			// Ordinary progress. Only forward motion consumes a mark, which is what
			// leaves one owed after a spin and costs the lap at the line.
			if (delta > 0.0) {
				consume_marks_in(previous, current);
			}
			return false;
		}

		// **The arming condition.** A lap needs one witness that the kart actually went
		// round, and there are two: a mark consumed in order, or enough forward travel
		// banked. A parked kart has neither however many times it nudges over the line,
		// and neither does a kart that skipped every mark by cutting the infield. Both
		// get nothing, which is correct — neither did a lap — and the cut self-heals,
		// because the taint survives until a lap does close.
		//
		// **The travel arm is not just the no-marks fallback, and making it one was a
		// bug.** This read `mark_count > 1 ? next_mark_ > 1 : travelled`, so a cut over
		// the *first* mark of the lap left `next_mark_` at 1 for the whole lap — the
		// marks a cut skipped stay owed, deliberately — and the lap never closed at all.
		// The header two functions up says what should happen instead, in those words:
		// *"a cut has to still complete its lap so that the missed mark can invalidate
		// it — swallowing the lap entirely would leave a driver who cut a corner with no
		// lap on the screen at all and no explanation."* On a fourteen-checkpoint circuit
		// the first mark is 98 m past the line, so this is a cut anybody could make.
		//
		// `LAP_TRAVEL_FRACTION` is half a circuit and a discontinuity banks none of the
		// distance it flew over, so a kart that cut enough to matter still fails both
		// arms. `tools/verify/circuit_probe.gd --case=timing` is the gate that found it.
		const bool went_round =
				next_mark_ > 1 || lap_travel_m_ >= marks_.length_m * LAP_TRAVEL_FRACTION;
		if (!went_round) {
			return false;
		}

		// Every remaining mark should already have been consumed; any that was not is
		// a cut, and it costs this lap rather than the next one.
		if (next_mark_ < marks_.mark_count) {
			taint(LapInvalidReason::MissedMark);
		}
		complete_lap();

		// The new lap starts at the line. Marks between the line and where the kart
		// actually is were passed on this same tick.
		next_mark_ = 1;
		// From the line, because that is where this lap started.
		consume_marks_in(0.0, current);
		return true;
	}

	// Signed distance from `from` to `to` the short way round, so a step across the
	// line is a small positive number rather than a nearly-whole-lap negative one.
	double circular_delta(double from, double to) const {
		double delta = to - from;
		const double half = marks_.length_m * 0.5;
		if (delta > half) {
			delta -= marks_.length_m;
		} else if (delta < -half) {
			delta += marks_.length_m;
		}
		return delta;
	}

	// The furthest a kart may move in one tick and still be believed, meters.
	//
	// `LAP_MAX_SPEED_MS` over the physics step. Anything past it is a discontinuity
	// rather than driving — see the third numbered point above.
	double max_step_m() const { return LAP_MAX_SPEED_MS * step_s_; }

	// Consume the marks in `(from, to]`, in order.
	//
	// **The lower bound is load-bearing.** Without it a mark the kart flew over
	// during a discontinuity is consumed by the *next* ordinary tick, simply because
	// the kart is now past it — so a cut across the infield paid for itself one tick
	// later and the lap came out clean. The marks a cut skipped have to stay owed,
	// and staying owed means never being consumed by a step that did not cross them.
	void consume_marks_in(double from, double to) {
		while (next_mark_ < marks_.mark_count) {
			const double mark = marks_.mark_m[next_mark_];
			if (!(mark > from && mark <= to)) {
				break;
			}
			enter_sector(next_mark_);
			++next_mark_;
		}
	}

	// Which mark is owed next, given that the kart is *here* and has not been seen
	// before. Called on the first tick of a session and after a respawn.
	//
	// A session does not always begin on the start line: Practice puts a kart on
	// track wherever the scene spawned it, and `GAMEDESIGN.md` §3 has a driver
	// leaving the pits mid-circuit. Arming from zero would leave every mark behind
	// the kart permanently owed, so its first crossing of the line would report a cut
	// it never made — and, worse, the lap would never close at all, because the
	// arming condition needs a consumed mark.
	void arm_marks_from(double distance_m) {
		next_mark_ = 1;
		for (int i = 1; i < marks_.mark_count; ++i) {
			if (marks_.mark_m[i] <= distance_m) {
				next_mark_ = i + 1;
			}
		}
		// And the sector the kart is standing in, so the first split is not attributed
		// to sector 0 from halfway round the lap. Not `next_mark_ - 1`: with checkpoints
		// in the list that is a *mark* index, and on Valdirone it would report a kart
		// sitting in sector 11 of 3.
		sector_ = marks_.sector_of_mark(next_mark_ - 1);
	}

	// Consume a mark. Only a sector boundary closes a split — a checkpoint is crossed
	// for the anti-cut ordering and leaves the running sector time alone, which is the
	// whole reason the two lists stay distinguishable this far down.
	void enter_sector(int mark_index) {
		if (mark_index >= 0 && mark_index < LAP_MAX_MARKS && marks_.checkpoint_only[mark_index]) {
			return;
		}
		if (sector_ >= 0 && sector_ < LAP_MAX_SECTORS) {
			sector_ticks_done_[sector_] = sector_ticks_;
		}
		sector_ = marks_.sector_of_mark(mark_index);
		sector_ticks_ = 0;
	}

	void complete_lap() {
		// Close the final sector before the totals are read.
		if (sector_ >= 0 && sector_ < LAP_MAX_SECTORS) {
			sector_ticks_done_[sector_] = sector_ticks_;
		}

		++laps_done_;
		LapRecord record;
		record.time_s = static_cast<double>(lap_ticks_) * step_s_;
		record.sector_count = marks_.sector_count();
		for (int i = 0; i < record.sector_count && i < LAP_MAX_SECTORS; ++i) {
			record.sector_s[i] = static_cast<double>(sector_ticks_done_[i]) * step_s_;
		}
		record.reason = reason_;
		record.lap_number = laps_done_;
		last_ = record;

		if (record.is_valid()) {
			++valid_laps_;
			if (!best_.is_valid() || record.time_s < best_.time_s) {
				best_ = record;
			}
			// Sector bests are per sector and are only taken from a valid lap. A best
			// sector set on a lap that cut the corner after it is a best sector nobody
			// drove, and it feeds `optimal_lap_s`.
			for (int i = 0; i < record.sector_count && i < LAP_MAX_SECTORS; ++i) {
				if (!lap_time_exists(best_sector_s_[i]) ||
						record.sector_s[i] < best_sector_s_[i]) {
					best_sector_s_[i] = record.sector_s[i];
				}
			}
		} else {
			++invalid_laps_;
		}

		lap_ticks_ = 0;
		sector_ticks_ = 0;
		sector_ = 0;
		lap_travel_m_ = 0.0;
		reason_ = LapInvalidReason::Valid;
		for (int i = 0; i < LAP_MAX_SECTORS; ++i) {
			sector_ticks_done_[i] = 0;
		}
	}

	LapMarks marks_;
	double step_s_ = 1.0 / 120.0;

	int64_t lap_ticks_ = 0;
	int64_t sector_ticks_ = 0;
	int64_t sector_ticks_done_[LAP_MAX_SECTORS] = {};
	int sector_ = 0;
	int laps_done_ = 0;
	int next_mark_ = 1;
	double distance_m_ = 0.0;
	double lap_travel_m_ = 0.0;
	bool have_distance_ = false;
	LapInvalidReason reason_ = LapInvalidReason::OutLap;

	LapRecord last_;
	LapRecord best_;
	int valid_laps_ = 0;
	int invalid_laps_ = 0;
	double best_sector_s_[LAP_MAX_SECTORS] = {};
};

// Whether this session type invalidates a lap for running wide.
//
// **Ours, and only where it means something.** A timed session is measuring one
// lap, so a lap set with two wheels on the grass is not a lap. A race is measuring
// a finishing order, and the regulations attach no penalty to going off by
// yourself — so invalidating a race lap would be inventing a rule the sport does
// not have *and* one that has no effect, since nothing in a race is scored per lap.
inline bool session_invalidates_laps(SessionType type) {
	switch (type) {
		case SessionType::Practice: return true;
		case SessionType::Qualifying: return true;
		case SessionType::Heat: return false;
		case SessionType::SuperHeat: return false;
		case SessionType::Final: return false;
	}
	return false;
}

// Whether a track-limits offense in this session deletes the fastest lap rather
// than the current one. Qualifying only, and it is the FIA's rule — see the header.
inline bool session_deletes_fastest_lap(SessionType type) {
	return type == SessionType::Qualifying;
}

} // namespace kart::core

#endif // KART_CORE_LAP_TIMING_H
