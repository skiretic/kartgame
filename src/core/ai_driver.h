#ifndef KART_CORE_AI_DRIVER_H
#define KART_CORE_AI_DRIVER_H

#include "core/replay.h"
#include "core/units.h"
#include "core/vehicle_state.h"

#include <cmath>

// The AI driver's controller. ROADMAP M7's second half.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017,
// which is also why `tests/run.sh` reaches every decision in this file in five
// seconds with no engine at all. Allocation-free and locale-free for the reason
// `racing_line.h` gives: fixed-size arrays, no `std::vector`, no `std::string`.
//
// ## The split against `racing_line.h`
//
// `src/kart_racing_line.h` draws it in its own words: *"what does the road
// allow"* against *"what does this driver do about it"*. The line owns the
// geometry, the corner speeds, the braking points and the gear per station.
// This file owns pure pursuit, the speed controller, the shift hysteresis, the
// clutch and the launch. `grip_usage` lives on the line and the difficulty
// *tier* lives on neither: the AI node picks a usage and the line answers with a
// slower profile, which this controller then follows exactly as it follows the
// fast one.
//
// ## The rule this file inherits and does not get to break
//
// **No lateral-g, brake-g or traction figure is written down here.** Issue #137
// is open, the tire model is expected to move when it closes, and a controller
// with a grip number baked into it would go quietly wrong on the day the tire
// got better. Every ceiling arrives in `AiLimits`, which the node fills from
// `KartRacingLine::model()` at build time — the same fixed points
// `SpeedModel::brake_limit()` and `lateral_limit()` solve against whatever
// `tire.h` currently says. Grep this file for a number in g and you will find
// none.
//
// ## What it emits, and through which door
//
// One `DriverInput` per tick, `replay_snap`-ed, handed to `KartBody::set_input`
// by the node — the same path `PlayerDriver` uses and the one ADR-0040 requires.
// Two consequences that are easy to get wrong and are stated rather than left:
//
//   * **`steer` is a lock fraction, not a stick position.** `PlayerDriver`
//     applies its cubic `steer_gamma` curve to the human's stick and to nothing
//     else, precisely because a scripted run asking for `--lock=0.4` is asking
//     for a lock fraction. This controller solves a *steering angle* out of a
//     bicycle model and divides it by the lock, so what it produces is already a
//     lock fraction. Putting it through the curve would cube it and the kart
//     would understeer out of every corner while the code read as correct.
//
//   * **`shift_up` / `shift_down` are edges.** True for exactly the tick the
//     request is made. `vehicle_state.h` says why: a level makes a held request
//     shift once per tick through the whole gearbox.

namespace kart::core::ai {

// How many preview samples the caller hands over per tick.
//
// The braking decision is a minimum over a window, so the resolution that
// matters is how finely the window is sampled rather than how far it reaches.
// 32 over the default 90 m is 2.8 m apart, which is two ticks of travel at
// 145 km/h; halving it moved the measured brake point by under a meter.
inline constexpr int PREVIEW_SAMPLES = 32;

// The controller's own dials. Every one of these is a controller decision, so
// every one of them is here rather than in `racing_line.h`.
struct AiTune {
	// --- pure pursuit -------------------------------------------------------

	// Lookahead is `base + speed * gain`, meters. A fixed lookahead is wrong at
	// both ends: short enough to be accurate in a hairpin it oscillates on a
	// straight, and long enough to be stable on a straight it cuts the hairpin.
	//
	// **estimated**, then measured: the gain is a time constant in disguise —
	// 0.55 s of travel — and the base is roughly the kart's own wheelbase times
	// three, which is the shortest arc a pure-pursuit circle can describe
	// without the aim point falling inside the turning circle.
	double lookahead_base_m = 3.2;
	double lookahead_gain_s = 0.55;

	// The lookahead is also **at least this many times the lateral error**, and
	// that clause is the difference between rejoining the line and spinning.
	//
	// A kart on the grid is 8.2 m off the line it is about to follow. At rest
	// the speed term contributes nothing, so the aim point sat 3.2 m ahead and
	// 8.2 m to the side — 69 degrees off the nose. Pure pursuit dutifully turned
	// almost perpendicular to the road, crossed the line at 25 degrees carrying
	// 19 m/s, and could not straighten out inside the grip: measured, the kart
	// reached the line at T360 and was spinning by T540, 180 degrees of body
	// slip and 101 m off. Every number above is from the trace.
	//
	// With this clause an 8.2 m error asks for a 24.6 m lookahead, which is an
	// 18-degree approach — a lane change rather than a lunge. The geometric
	// reading is that the pure-pursuit arc through a point `L` ahead and `e`
	// aside has curvature `2e/L^2`, so tying `L` to `e` bounds the demanded
	// curvature by `2/(k^2 e)` instead of letting it grow as the error does.
	double lookahead_error_gain = 3.0;

	// Multiplies the solved lock fraction. 1.0 is the bicycle model taken at
	// face value; the model is a linearization of an Ackermann rack, so a little
	// over one compensates for the arc the two front wheels actually describe.
	double steer_gain = 1.0;

	// Ceiling on the lock fraction the controller will ask for.
	//
	// **1.0 by default, and that is deliberate even with issue #137 open.** The
	// solved line's tightest radius on Valdirone is 19.86 m, which is 0.05 of
	// lock — nowhere near the quarter-lock scrub. Capping it would only ever bite
	// during a recovery, and a recovery that cannot steer is a kart in the grass.
	double max_lock_fraction = 1.0;

	// Fraction of the lateral ceiling the *steering* is allowed to ask for. See
	// the curvature cap in `step`.
	//
	// Under one because the cap is a ceiling on the geometry and the kart is
	// simultaneously braking or driving out of the corner, and the ellipse is
	// shared. It is the steering half of the same reserve `brake_plan_fraction`
	// keeps on the longitudinal half.
	double curve_margin = 0.85;

	// --- speed --------------------------------------------------------------

	// How far ahead the braking search looks, meters. It has to cover the
	// longest stop on the circuit: 145 km/h to the hairpin is about 65 m at the
	// model's own brake limit, so 90 m carries it with room and costs 32
	// interpolations a tick.
	double preview_m = 90.0;

	// Fraction of the model's brake ceiling the controller plans against.
	//
	// Under one on purpose. The profile was solved as a quasi-static optimum;
	// braking at exactly its ceiling means any error is on the wrong side of it,
	// and the kart arrives at the corner too fast rather than too slow. This is
	// the AI's margin and it is the AI's to spend.
	double brake_plan_fraction = 0.92;

	// Proportional gains on the speed error, per (m/s). Full authority at
	// 2.0 m/s of error on the throttle and 1.4 m/s on the brake — the brake is
	// the more urgent of the two because being fast into a corner cannot be
	// undone later.
	double throttle_gain = 0.50;
	double brake_gain = 0.71;

	// Speed error band, m/s, inside which the controller neither drives nor
	// brakes. Without it the two gains fight across zero at every station and
	// the throttle trace is a square wave, which is audible.
	double coast_band_ms = 0.35;

	// --- the traction budget ------------------------------------------------

	// Reserve longitudinal grip against what the corner is already using, on the
	// friction ellipse: the longitudinal fraction available is
	// `sqrt(1 - (a_lat / a_lat_max)^2)`. `a_lat_max` is read at run time and is
	// never written down. Set false and the controller drives the speed error
	// alone, which is the negative control for this term.
	bool traction_budget = true;

	// --- gears --------------------------------------------------------------

	// Ticks a disagreement with the line's gear has to persist before a shift is
	// requested, and ticks of quiet afterwards.
	//
	// **This is the hysteresis `racing_line.h` deliberately does not have.** Its
	// `best_gear` answers "which gear pulls hardest here" and says in its own
	// words that "is it worth the shift" is the controller's question. At 120 Hz
	// 10 ticks is 83 ms, which is about how long the gearbox is out of gear
	// anyway, and 24 ticks of quiet is 200 ms — long enough that a corner exit
	// cannot chatter and short enough for the three downshifts a hairpin needs.
	int shift_confirm_ticks = 10;
	int shift_cooldown_ticks = 24;

	// --- the standing start -------------------------------------------------

	// Below this road speed the controller is launching rather than driving,
	// m/s. The clutch is feathered from fully disengaged to fully home across
	// it and the throttle is held at `launch_throttle`.
	//
	// A lap does not contain a standing start; a session does. `racing_line.h`
	// says as much where `gear_force` confines the sub-idle case to one.
	double launch_speed_ms = 4.0;
	double launch_throttle = 0.65;

	// The **most** disengaged the clutch gets while launching, 0..1.
	//
	// Not 1.0, and the difference is the whole launch. `clutch = 1` is the lever
	// fully pulled and *nothing reaches the axle*: with `auto_clutch` off — which
	// is how an AI kart runs, because two clutches on one gearbox is the same
	// mistake as two shifters — a controller that asked for 1.0 at rest sat on
	// the grid at 0.37 km/h with the throttle wide open and the speed-indexed
	// ramp never started, because the ramp is indexed on the speed it was
	// preventing. Measured. 0.75 leaves the clutch slipping and transmitting,
	// which is what a driver's foot is doing at that moment.
	double launch_clutch_max = 0.75;

	// --- giving up ----------------------------------------------------------

	// Body slip past which the controller stops asking for power, radians.
	//
	// Issue #137's departure is a spin, and a spun kart at full throttle is a
	// kart that stays spun. Beyond this the throttle is cut and the steering is
	// pointed at the line, which is the least this controller can do and is not
	// a recovery strategy — see the header of `ai.sh` for what is and is not
	// claimed.
	double slip_abort_rad = 0.61; // 35 degrees
};

// The ceilings, all read at run time from the model the line was solved with.
//
// There is no default that means anything here: a zero `lateral_limit_ms2`
// disables the traction budget rather than inventing a grip figure, and a zero
// `brake_limit_ms2` makes the controller plan no braking at all. Both are
// reported by `AiDriver::configured()` so a gate can refuse a run that was never
// told what the kart can do, rather than measuring one.
struct AiLimits {
	double brake_limit_ms2 = 0.0; // SpeedModel::brake_limit(), m/s^2
	double lateral_limit_ms2 = 0.0; // the tighter of the two hands, m/s^2
	double wheelbase_m = 0.0;
	double max_lock_rad = 0.0; // KartBody::get_max_lock()
	double soft_cut_rpm = 0.0;
	double hard_cut_rpm = 0.0;
	int gear_count = 6;

	bool complete() const {
		return brake_limit_ms2 > 0.0 && lateral_limit_ms2 > 0.0 && wheelbase_m > 0.0 &&
				max_lock_rad > 0.0 && hard_cut_rpm > 0.0;
	}
};

// One preview sample: how far ahead, and how fast the line is there.
struct AiPreview {
	double distance_m = 0.0; // ahead of the kart, along the centerline
	double speed_ms = 0.0; // what the line's profile allows there
};

// One tick of the world, as the node measured it.
//
// Positions and headings follow `track.h`'s conventions throughout, which is not
// a matter of taste: heading zero is -Z, heading increases through a right-hand
// turn, and the kart's forward direction is therefore `(sin h, -cos h)` in
// Godot's x/z ground plane. Getting that backwards is invisible until the kart
// drives away from the line at exactly the rate it should be driving toward it.
struct AiObservation {
	double speed_ms = 0.0;

	// The kart, world x/z, and its yaw expressed as a track heading.
	double x = 0.0;
	double z = 0.0;
	double heading_rad = 0.0;

	// Where `Track::project` put it, and how far off the *line* that is. The
	// second is the number `ai.sh` gates on and it is a lateral difference, not
	// a distance to a polyline: the line's own offset at this station minus the
	// kart's.
	double station_m = 0.0;
	double cross_track_m = 0.0;

	// The line's point to aim at, world x/z, at the lookahead the controller
	// asked for last tick.
	double aim_x = 0.0;
	double aim_z = 0.0;

	// The line's signed curvature here, 1/m, positive for a right-hander. Feeds
	// the traction budget and nothing else — the steering comes from the aim
	// point, because a curvature-following steer has no way back to a line it
	// has drifted off.
	double line_curvature = 0.0;

	// What the line wants of the gearbox at this station.
	int line_gear = 0;

	// The vehicle.
	int gear = 0;
	double rpm = 0.0;
	double body_slip_rad = 0.0;
	int wheels_on_ground = 4;

	// The braking window, nearest first. `count` may be under `PREVIEW_SAMPLES`;
	// the controller reads exactly that many.
	int preview_count = 0;
	AiPreview preview[PREVIEW_SAMPLES];
};

// What the controller decided and why, so a probe reports the reason rather than
// inferring it from the input it produced.
struct AiDecision {
	double allowed_speed_ms = 0.0; // the minimum over the braking window
	double binding_distance_m = 0.0; // where that minimum came from
	double speed_error_ms = 0.0; // allowed minus actual
	double lookahead_m = 0.0;
	double demanded_curvature = 0.0; // 1/m, positive to the right
	double lateral_demand_ms2 = 0.0; // v^2 k on the line's own curvature
	double longitudinal_budget = 0.0; // 0..1 left on the ellipse
	int target_gear = 0;
	bool braking = false;
	bool launching = false;
	bool aborting = false; // slip past `slip_abort_rad`
};

// The controller. One instance per AI kart; it carries the shift hysteresis and
// the lookahead it asked for last tick, and nothing else.
class AiDriver {
public:
	void configure(const AiLimits &limits, const AiTune &tune) {
		limits_ = limits;
		tune_ = tune;
		reset();
	}

	void set_tune(const AiTune &tune) { tune_ = tune; }
	const AiTune &tune() const { return tune_; }
	const AiLimits &limits() const { return limits_; }
	bool configured() const { return limits_.complete(); }

	// Forget the hysteresis. Called on a respawn, because a shift confirmed
	// against the corner the kart used to be in is a shift into the wrong gear
	// at the place it was put down.
	void reset() {
		shift_pending_ = 0;
		shift_confirm_ = 0;
		cooldown_ = 0;
		lookahead_m_ = tune_.lookahead_base_m;
		decision_ = AiDecision();
	}

	// The lookahead this tick wants, meters. Asked *before* `step`, because the
	// caller has to sample the line at it and only the caller can evaluate the
	// line. Two-phase rather than handing this class a sampler: a callback would
	// mean either a virtual or a function pointer in a header that is otherwise
	// allocation-free and trivially testable.
	double lookahead_for(double speed_ms, double cross_track_m = 0.0) const {
		double ahead = tune_.lookahead_base_m + tune_.lookahead_gain_s * speed_ms;
		if (ahead < tune_.lookahead_base_m) {
			ahead = tune_.lookahead_base_m;
		}
		const double error = cross_track_m < 0.0 ? -cross_track_m : cross_track_m;
		const double by_error = tune_.lookahead_error_gain * error;
		return by_error > ahead ? by_error : ahead;
	}

	// One tick. The returned input is **not** snapped to the replay grid; the
	// node does that, for the reason `replay.h` gives — quantization belongs
	// upstream of the solver at one call site, not scattered through the
	// producers.
	DriverInput step(const AiObservation &observation) {
		DriverInput input;
		decision_ = AiDecision();
		lookahead_m_ = lookahead_for(observation.speed_ms, observation.cross_track_m);
		decision_.lookahead_m = lookahead_m_;

		// --- steering, pure pursuit -----------------------------------------
		//
		// The aim point in the kart's own frame. Forward is `(sin h, -cos h)`
		// and right is `(cos h, sin h)`; both fall straight out of `track.h`'s
		// heading convention and the check to keep in your head is h = 0, where
		// forward is -Z and right is +X.
		const double to_x = observation.aim_x - observation.x;
		const double to_z = observation.aim_z - observation.z;
		const double sin_h = std::sin(observation.heading_rad);
		const double cos_h = std::cos(observation.heading_rad);
		const double ahead = to_x * sin_h - to_z * cos_h;
		const double right = to_x * cos_h + to_z * sin_h;
		const double reach_sq = to_x * to_x + to_z * to_z;

		// The pure-pursuit circle through the aim point: curvature 2y/L^2, with
		// y the lateral offset in the kart frame. Positive is a right-hand turn,
		// matching `track.h`'s curvature sign.
		double curvature = 0.0;
		if (reach_sq > 1e-6) {
			curvature = 2.0 * right / reach_sq;
			// Behind the kart the pure-pursuit circle is still defined and is
			// still the wrong answer — it asks for the shortest arc back, which
			// at any speed is a spin. Saturate instead and let the speed
			// controller sort it out.
			if (ahead <= 0.0) {
				curvature = right >= 0.0 ? 1e3 : -1e3;
			}
		}
		// **The curvature is capped at what the grip permits at this speed, and
		// that cap is the single most load-bearing line in this file.**
		//
		// Pure pursuit answers a geometry question and has no idea how fast the
		// kart is going. A kart rejoining the line from a grid slot is 8 m off
		// it, and 8 m of lateral error over a 12 m lookahead is a curvature of
		// 0.1 — which at 126 km/h asks for **12 g**. Issue #137 says what this
		// kart does when asked: it scrubs past a quarter of lock and departs.
		// Measured before the cap: 179.99 degrees of body slip, 44 m off line,
		// no lap.
		//
		// `k_max = a_lat_max / v^2` is not a fudge factor, it is the definition
		// of the limit, and `a_lat_max` is read at run time out of
		// `KartRacingLine::model()` like every other ceiling here. The kart then
		// rejoins the line as fast as it physically can and no faster, which is
		// also what a driver does.
		//
		// `curve_margin` is the AI's own reserve on top, so a rejoin does not
		// spend the entire envelope and leave nothing for the corner it is
		// rejoining into.
		if (limits_.lateral_limit_ms2 > 0.0 && observation.speed_ms > 1.0) {
			const double max_curvature = tune_.curve_margin * limits_.lateral_limit_ms2 /
					(observation.speed_ms * observation.speed_ms);
			if (curvature > max_curvature) {
				curvature = max_curvature;
			}
			if (curvature < -max_curvature) {
				curvature = -max_curvature;
			}
		}
		decision_.demanded_curvature = curvature;

		// Bicycle model, then divided by the lock. This is the one place a
		// steering *angle* becomes a lock *fraction* and it is why the input
		// this class produces must not go through `PlayerDriver`'s curve.
		double lock = 0.0;
		if (limits_.max_lock_rad > 0.0) {
			const double angle = std::atan(curvature * limits_.wheelbase_m);
			lock = tune_.steer_gain * angle / limits_.max_lock_rad;
		}
		const double cap = tune_.max_lock_fraction;
		if (lock > cap) {
			lock = cap;
		}
		if (lock < -cap) {
			lock = -cap;
		}
		// `DriverInput::steer` is **positive to the left** and `lock` is
		// positive to the right. One negation, in one place, stated twice.
		input.steer = -lock;

		// --- how fast the kart is allowed to be going here ------------------
		double allowed = 1e30;
		double binding = 0.0;
		const double plan_brake = limits_.brake_limit_ms2 * tune_.brake_plan_fraction;
		for (int index = 0; index < observation.preview_count && index < PREVIEW_SAMPLES;
				++index) {
			const AiPreview &sample = observation.preview[index];
			// The speed that can still be shed to `sample.speed_ms` over
			// `sample.distance_m` at the planned deceleration. At distance zero
			// this is the corner speed itself, which is why the caller's first
			// sample must be at the kart.
			const double reachable = std::sqrt(sample.speed_ms * sample.speed_ms +
					2.0 * plan_brake * (sample.distance_m > 0.0 ? sample.distance_m : 0.0));
			if (reachable < allowed) {
				allowed = reachable;
				binding = sample.distance_m;
			}
		}
		if (allowed > 1e29) {
			allowed = observation.speed_ms;
		}
		decision_.allowed_speed_ms = allowed;
		decision_.binding_distance_m = binding;

		const double error = allowed - observation.speed_ms;
		decision_.speed_error_ms = error;

		// --- the traction budget --------------------------------------------
		//
		// What the corner is already spending, against what the model says is
		// there. The ceiling is `limits_.lateral_limit_ms2`, read at run time
		// from `KartRacingLine::model()`, and it is the only grip figure this
		// class ever sees.
		const double curvature_now = observation.line_curvature < 0.0
				? -observation.line_curvature
				: observation.line_curvature;
		const double lateral = observation.speed_ms * observation.speed_ms * curvature_now;
		decision_.lateral_demand_ms2 = lateral;
		double budget = 1.0;
		if (tune_.traction_budget && limits_.lateral_limit_ms2 > 0.0) {
			double used = lateral / limits_.lateral_limit_ms2;
			if (used > 1.0) {
				used = 1.0;
			}
			budget = std::sqrt(1.0 - used * used);
		}
		decision_.longitudinal_budget = budget;

		// --- throttle and brake ---------------------------------------------
		if (error > tune_.coast_band_ms) {
			input.throttle = clamp01(tune_.throttle_gain * error) * budget;
			input.brake = 0.0;
		} else if (error < -tune_.coast_band_ms) {
			input.throttle = 0.0;
			input.brake = clamp01(tune_.brake_gain * (-error));
			decision_.braking = true;
		}

		// --- the standing start ---------------------------------------------
		//
		// Feathered rather than dumped: `clutch` is 1 fully disengaged, so this
		// walks it home as the road speed comes up. `auto_clutch` may be doing
		// the same job underneath, and the two do not fight — the body takes the
		// larger of the assist's and the driver's disengagement.
		if (observation.speed_ms < tune_.launch_speed_ms) {
			decision_.launching = true;
			double engaged = observation.speed_ms / tune_.launch_speed_ms;
			if (engaged < 0.0) {
				engaged = 0.0;
			}
			input.clutch = tune_.launch_clutch_max * (1.0 - engaged);
			if (input.throttle < tune_.launch_throttle && error > 0.0) {
				input.throttle = tune_.launch_throttle;
			}
		}

		// --- giving up -------------------------------------------------------
		const double slip = observation.body_slip_rad < 0.0 ? -observation.body_slip_rad
														   : observation.body_slip_rad;
		if (slip > tune_.slip_abort_rad) {
			decision_.aborting = true;
			input.throttle = 0.0;
		}

		// --- gears -----------------------------------------------------------
		input.shift_up = false;
		input.shift_down = false;
		decision_.target_gear = choose_gear(observation, input.clutch);
		if (cooldown_ > 0) {
			--cooldown_;
		} else if (decision_.target_gear > 0 && observation.gear >= 0) {
			// **`>= 0`, not `> 0`, and that is the difference between driving and
			// sitting in neutral with the throttle wide open.** With `auto_shift`
			// off — which is how an AI kart runs — the gearbox starts in neutral,
			// and a shift rule that required a gear already engaged had no way to
			// select first. Measured: 1,201 ticks, gear 0, 0.30 km/h.
			const int want = decision_.target_gear;
			if (want == observation.gear) {
				shift_pending_ = 0;
				shift_confirm_ = 0;
			} else {
				if (want == shift_pending_) {
					++shift_confirm_;
				} else {
					shift_pending_ = want;
					shift_confirm_ = 1;
				}
				if (shift_confirm_ >= tune_.shift_confirm_ticks) {
					// One gear at a time. A hairpin wants three downshifts and
					// it gets them one cooldown apart, which is what a sequential
					// box does anyway.
					input.shift_up = want > observation.gear;
					input.shift_down = want < observation.gear;
					cooldown_ = tune_.shift_cooldown_ticks;
					shift_pending_ = 0;
					shift_confirm_ = 0;
				}
			}
		}

		last_ = input;
		return input;
	}

	const AiDecision &decision() const { return decision_; }
	const DriverInput &last_input() const { return last_; }

private:
	// Below this the clutch is home enough that engine rpm says something about
	// the gear. See `choose_gear`.
	static constexpr double CLUTCH_ENGAGED_BELOW = 0.05;

	static double clamp01(double value) {
		return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
	}

	// The gear the line wants at this **road speed**, overridden by the rev
	// limiter when — and only when — the limiter means what it looks like.
	//
	// The override exists because the caller's speed-indexed answer is a table
	// and the kart is not exactly on it. Over the soft cut the engine is being
	// held back and one more gear is free.
	//
	// **It is suppressed while the clutch is slipping, and that is not a corner
	// case — it is the standing start.** With the clutch feathered and the
	// throttle wide open, a stationary kart's engine is against the limiter
	// *because it is stationary*, not because the gear is too short. Without
	// this condition the override read that as "upshift", and it upshifted every
	// cooldown: first to sixth in a second and a half, 32 shifts in ten seconds,
	// a kart that never left the grid. The engine revving on a slipping clutch
	// is the one case where a high rpm carries no information about the gear.
	int choose_gear(const AiObservation &observation, double clutch) const {
		int want = observation.line_gear;
		if (want < 1) {
			want = observation.gear > 0 ? observation.gear : 1;
		}
		if (observation.gear >= 1 && clutch < CLUTCH_ENGAGED_BELOW) {
			// **`==`, not `<=`, and the difference is a ratchet.** With `<=` the
			// override fires whenever the table's answer is at or below the
			// engaged gear — so one transient over-rev in second takes it to
			// third, where the table still says second, so it takes it to
			// fourth, and on to sixth. Measured: 18 shifts and sixth gear at
			// 72 km/h against a table that says second. `==` means "the table
			// agrees with the gear I am in and the engine is still against the
			// limiter", which is the only case the override is arguing about.
			if (observation.rpm > limits_.soft_cut_rpm && limits_.soft_cut_rpm > 0.0 &&
					want == observation.gear && observation.gear < limits_.gear_count) {
				want = observation.gear + 1;
			}
		}
		if (want > limits_.gear_count) {
			want = limits_.gear_count;
		}
		if (want < 1) {
			want = 1;
		}
		return want;
	}

	AiLimits limits_;
	AiTune tune_;

	int shift_pending_ = 0;
	int shift_confirm_ = 0;
	int cooldown_ = 0;
	double lookahead_m_ = 0.0;

	AiDecision decision_;
	DriverInput last_;
};

} // namespace kart::core::ai

#endif // KART_CORE_AI_DRIVER_H
