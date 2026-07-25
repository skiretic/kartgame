#ifndef KART_CORE_GEARBOX_H
#define KART_CORE_GEARBOX_H

#include "core/kz_reference.h"
#include "core/units.h"

// The six-speed sequential gearbox and the chain final drive. ARCHITECTURE.md
// §6.3: "Gear ratios × primary reduction × final drive (sprocket) → wheel
// torque", with a 50-80 ms shift time and a torque cut.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## The ratios are teeth, not numbers
//
// Every ratio in this file is written as a pair of tooth counts taken from a
// parts catalog, because a ratio typed in as a decimal is a number nobody can
// check and a tooth count is a part somebody can buy. The counts below are the
// TM Racing KZ gearbox as sold for the KZ10B/KZ10C and the KZ-R1/R2/R3 — the
// engine family ADR-0011 picked when it chose KZ over TaG, and the same family
// as the reference photographs in docs/REFERENCES.md. Sources and part numbers
// are in docs/REFERENCES.md; two independent dealer listings agree on every
// count here.
//
// Reading the drive path from the crankshaft out:
//
//   crankshaft --[18 : 75]--> clutch --[mainshaft : countershaft]--> sprocket
//                                                  --[18 : 25]--> rear axle
//
// The primary reduction of 75/18 is large for a gearbox engine and the final
// drive of 25/18 is tiny compared with a motorcycle's, and the two facts are the
// same fact: a kart tire is 275 mm across, so the reduction that a motorcycle
// puts in its chain a kart has to put somewhere else.
//
// ## Why sixth is an overdrive
//
// Sixth is 25/27, i.e. the countershaft turns *faster* than the mainshaft. That
// looks wrong and is not: with a 4.17 primary the gearbox is stepping down so
// hard already that the top gear has to give some of it back. The complete
// picture is a spread of 14.69:1 in first to 5.36:1 in sixth, ratio steps
// between 1.13 and 1.40, and 52 km/h in first against 143 km/h in sixth at the
// rev limiter — the whole reason a KZ driver is busy.

namespace kart::core {

// Crankshaft gear to clutch gear. TM part 40318 is the Z18 primary drive gear on
// the crank; TM part 40385 is the Z75 gear on the clutch basket.
inline constexpr int PRIMARY_DRIVE_TEETH = 18;
inline constexpr int PRIMARY_DRIVEN_TEETH = 75;

// Mainshaft (the shaft the clutch drives) and countershaft (the shaft the engine
// sprocket sits on), first gear through sixth. First gear's mainshaft pinion is
// cut into the shaft itself, which is why it is sold as "primary shaft, Z13"
// rather than as a gear.
inline constexpr int MAINSHAFT_TEETH[kz::GEAR_COUNT] = { 13, 16, 18, 22, 22, 27 };
inline constexpr int COUNTERSHAFT_TEETH[kz::GEAR_COUNT] = { 33, 29, 27, 27, 23, 25 };

// A sequential box is doing exactly one of three things.
enum class ShiftState {
	engaged,
	shifting_up,
	shifting_down,
};

class Gearbox {
public:
	// Engine sprocket and rear axle sprocket, 428 chain. Dealer stock for a KZ
	// runs 14-21 on the engine and roughly 21-30 on the axle, and a track-to-
	// track change is a tooth at either end; 18/25 is a mid-length circuit
	// setting and it is what puts sixth gear's rev limit inside kz_reference.h's
	// 135-145 km/h. This pair is the one thing in the drivetrain a player or a
	// track author is expected to change.
	int engine_sprocket_teeth = 18;
	int axle_sprocket_teeth = 25;

	// Seconds of torque cut per shift. ARCHITECTURE.md §6.3 says 50-80 ms; 65 ms
	// is the middle of it. A clutchless upshift in a dog box really is this: the
	// driver unloads the drive, the dogs come out, the next set goes in, and for
	// the duration there is nothing connecting the engine to the road. Model it
	// as instantaneous and a KZ loses the pause that makes a shift a decision.
	double shift_time = 0.065;

	// Ratio of one gear alone, countershaft teeth over mainshaft teeth. Gear
	// numbering is 1-based to match every other statement about a gearbox
	// anywhere; 0 is neutral.
	static double gear_ratio(int gear) {
		if (gear < 1 || gear > kz::GEAR_COUNT) {
			return 0.0;
		}
		const int index = gear - 1;
		return static_cast<double>(COUNTERSHAFT_TEETH[index]) /
				static_cast<double>(MAINSHAFT_TEETH[index]);
	}

	static double primary_reduction() {
		return static_cast<double>(PRIMARY_DRIVEN_TEETH) /
				static_cast<double>(PRIMARY_DRIVE_TEETH);
	}

	double final_drive() const {
		return static_cast<double>(axle_sprocket_teeth) /
				static_cast<double>(engine_sprocket_teeth);
	}

	// Crankshaft revolutions per axle revolution. Multiply a crank torque by this
	// to get an axle torque; divide an axle speed by it — no, multiply — to get a
	// crank speed. Stated once so the direction is never guessed:
	//
	//     engine_speed = axle_speed * total_ratio(gear)
	//     axle_torque  = engine_torque * total_ratio(gear)
	//
	// Both because the ratio is a reduction: the engine turns faster and the axle
	// receives more torque, and an ideal gear train conserves power.
	double total_ratio(int gear) const {
		return primary_reduction() * gear_ratio(gear) * final_drive();
	}

	double total_ratio() const { return total_ratio(gear_); }

	int gear() const { return gear_; }
	int target_gear() const { return target_gear_; }
	bool shifting() const { return state_ != ShiftState::engaged; }
	ShiftState state() const { return state_; }

	// What fraction of engine torque reaches the axle. Zero across a shift and
	// one otherwise — the cut is total, not partial, because the dogs are
	// physically out.
	double torque_scale() const { return shifting() ? 0.0 : 1.0; }

	// Seconds left in the current shift, for telemetry and for the tests that
	// have to prove the cut is 50-80 ms rather than a frame.
	double shift_remaining() const { return shift_timer_; }

	// Advance the box. `up` and `down` are requests, each true for one substep.
	//
	// Sequential means what it says: one gear per request, no skipping, and a
	// request arriving during a shift is dropped rather than queued. Queuing it
	// would let a player who mashed the paddle three times find themselves two
	// gears further along than they thought, which is the kind of input handling
	// that gets described as "the kart shifted on its own".
	void step(double dt, bool up, bool down) {
		if (state_ != ShiftState::engaged) {
			shift_timer_ -= dt;
			if (shift_timer_ <= 0.0) {
				shift_timer_ = 0.0;
				gear_ = target_gear_;
				state_ = ShiftState::engaged;
			}
			return;
		}

		// Both at once is a hardware impossibility on a hand shifter and a very
		// possible one on a gamepad. Up wins; the alternative is to drop both,
		// which loses an input the player definitely meant.
		if (up && gear_ < kz::GEAR_COUNT) {
			begin_shift(gear_ + 1, ShiftState::shifting_up);
		} else if (down && gear_ > 0) {
			// Down from first is neutral. A KZ box has neutral at the bottom of
			// the gate, and having it reachable matters for more than realism:
			// it is how a stalled engine gets restarted and how a kart sits on
			// the grid.
			begin_shift(gear_ - 1, ShiftState::shifting_down);
		}
	}

	// Put the box somewhere with no shift time at all. For setting up a scenario,
	// for a respawn, and for tests — never for driving.
	void force_gear(int gear) {
		if (gear < 0 || gear > kz::GEAR_COUNT) {
			return;
		}
		gear_ = gear;
		target_gear_ = gear;
		state_ = ShiftState::engaged;
		shift_timer_ = 0.0;
	}

	// Road speed, m/s, at a given engine speed in a given gear. Closed form, and
	// the single most checkable statement the drivetrain makes: sixth gear at the
	// rev limiter has to land inside kz_reference.h's top-speed range or the
	// gearing is wrong no matter how good the engine curve is.
	double road_speed(double engine_rpm, int gear, double rolling_radius) const {
		const double ratio = total_ratio(gear);
		if (ratio <= 0.0) {
			return 0.0;
		}
		return rpm_to_rads(engine_rpm) / ratio * rolling_radius;
	}

	double engine_rpm_at(double road_speed_ms, int gear, double rolling_radius) const {
		if (rolling_radius <= 0.0) {
			return 0.0;
		}
		return rads_to_rpm(road_speed_ms / rolling_radius * total_ratio(gear));
	}

private:
	void begin_shift(int gear, ShiftState state) {
		target_gear_ = gear;
		state_ = state;
		shift_timer_ = shift_time;
	}

	int gear_ = 0;
	int target_gear_ = 0;
	ShiftState state_ = ShiftState::engaged;
	double shift_timer_ = 0.0;
};

} // namespace kart::core

#endif // KART_CORE_GEARBOX_H
