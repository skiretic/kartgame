#ifndef KART_CORE_DRIVETRAIN_H
#define KART_CORE_DRIVETRAIN_H

#include "core/clutch.h"
#include "core/engine.h"
#include "core/gearbox.h"
#include "core/units.h"

#include <cmath>

// The drivetrain: engine, gearbox and clutch assembled into the thing the
// vehicle solver talks to, plus the auto-clutch and auto-shift assists that
// ARCHITECTURE.md §6.3 says default on because "without them, a player new to
// shifter karts cannot complete a lap".
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## Who owns what
//
// This is the part that is easy to get wrong, so it is stated before any code.
// **The drivetrain does not own the rear axle's rotational state.** The vehicle
// solver does, because the tires apply reaction torque to that axle and only the
// solver knows what the tires are doing. So each substep the drivetrain is told
// the axle speed and answers with two things:
//
//     alpha_axle = (axle_torque - tire_reaction) / (I_axle + reflected_inertia)
//
// `axle_torque` is the easy half. `reflected_inertia` is the half that decides
// whether the whole thing is stable: when the clutch is locked, the crankshaft
// is rigidly geared to the axle, and a rigid connection means the crank's
// inertia appears at the axle multiplied by the **square** of the total ratio.
// In first gear that is 0.0055 x 14.69² = 1.19 kg·m², more than ten times the
// axle's own. Leave it out and the solver believes a locked driveline can change
// speed instantly; the engine speed then follows the axle in one step, the
// clutch torque that produced it is computed from the old speed, and the pair
// diverge — a 240 Hz solver that explodes on the third substep of a launch.
//
// It is zero whenever the clutch is not locked, because then nothing rigid
// connects the crank to the road: the clutch is a torque source, and a torque
// source has no inertia.
//
// ## The assists
//
// Auto-clutch and auto-shift are the only assists in the project that default
// on, and issue #40 is explicit about why: an unassisted first lap in a KZ ends
// in a stall on the grid, and a player who stalls on the grid does not come
// back. They are computed *from* the input stream and never write to anything a
// replay depends on — the two booleans below are session configuration, the
// engaged/disengaged flag is derived state that `reset()` clears, and a replay
// of the same inputs with the same assist settings produces the same numbers to
// the bit.

namespace kart::core {

// One substep of driver intent.
struct DrivetrainInput {
	double throttle = 0.0; // 0..1
	double clutch = 0.0; // 0..1, 1 = lever fully pulled, fully disengaged
	bool shift_up = false; // a request, true for one substep
	bool shift_down = false;
	double axle_speed = 0.0; // rad/s of the solid rear axle, from the solver
	double dt = 0.0; // seconds
};

struct DrivetrainOutput {
	double axle_torque = 0.0; // Nm delivered to the rear axle, signed
	double reflected_inertia = 0.0; // kg m^2 of engine+driveline seen AT THE AXLE
	double engine_rpm = 0.0;
	double engine_torque = 0.0; // Nm at the crank, telemetry
	int gear = 0; // 0 = neutral, 1..6
	double clutch_slip = 0.0; // rad/s across the clutch, telemetry
	double clutch_torque = 0.0; // Nm, telemetry
	bool shifting = false;
	bool over_rev = false;
};

struct DrivetrainAssists {
	// Both default on. ARCHITECTURE.md §6.3, issue #40.
	bool auto_clutch = true;
	bool auto_shift = true;

	// Auto-clutch. The lever is moved across the clutch's working range in
	// proportion to how far the engine is into its powerband, which sounds
	// arbitrary and is not: it makes the clutch's capacity meet the engine's
	// torque curve at a stable point. Below 8,000 rpm the engine makes less than
	// the clutch can hold at that lever position, so it accelerates; above about
	// 10,500 the clutch can hold more than the engine makes, so it is pulled
	// back. The launch therefore settles near 10,500 rpm with the clutch slipping
	// — which is what a KZ driver does by hand, and it is not a coincidence:
	// both are looking for the same equilibrium.
	double clutch_in_start_rpm = 8000.0;
	double clutch_in_full_rpm = 13000.0;

	// Once locked the clutch stays locked down to here, which is what keeps the
	// kart drivable at walking pace instead of the clutch popping open every time
	// the engine drops out of its band. Well above the stall speed, because the
	// entire point of the assist is that the engine never gets near it.
	double clutch_dropout_rpm = 4500.0;

	// How close the two sides have to come before the assist stops modulating and
	// simply lets the lever go, rad/s. 40 rad/s is 380 rpm at the crank: near
	// enough that clamping fully produces a nudge rather than a bang, and far
	// enough that the assist is not waiting for a convergence a still-accelerating
	// kart will not give it. A driver feeds the last of the lever at exactly the
	// same moment and for exactly the same reason.
	double clutch_engage_slip = 40.0;

	// Auto-shift. Up just before the soft cut so the driver never hears the
	// limiter, down before the engine falls out of the band it needs.
	double upshift_rpm = 14000.0;
	double downshift_rpm = 9200.0;

	// An upshift the assist will not make, for the mirror image of the reason it
	// will not make some downshifts. A clutchless upshift with the throttle open
	// flares the engine while the dogs are out, and an assist that shifted on the
	// flare would take a kart from first to sixth in three tenths of a second
	// without the road speed having changed at all. It did, once. The floor asks
	// the only question that matters: what will the engine be doing in the gear
	// I am about to select?
	double upshift_floor_rpm = 9500.0;

	// A downshift the assist will not make. Sixth to fifth at 140 km/h puts the
	// crank at 16,700 rpm, which is an over-rev and a destroyed engine — a real
	// mistake, and one issue #36 wants *available*, which is why the guard lives
	// in the assist and not in the gearbox. Turn the assist off and the mistake
	// is yours to make.
	double downshift_guard_rpm = 14000.0;
};

class Drivetrain {
public:
	Engine engine;
	Gearbox gearbox;
	Clutch clutch;
	DrivetrainAssists assists;

	Drivetrain() { engine_speed_ = rpm_to_rads(engine.idle_rpm); }

	// One 240 Hz solver substep. Everything here is one pass of straight
	// arithmetic: no allocation, no iteration over anything unordered, no clock,
	// no randomness. ARCHITECTURE.md §8.
	DrivetrainOutput step(const DrivetrainInput &input) {
		DrivetrainOutput output;
		const double dt = input.dt;

		// A stalled engine is still a gearbox. The driver can find neutral and
		// be push-started, which is how a KZ is started in the first place —
		// there is no starter motor on the engine.
		if (stalled_) {
			gearbox.step(dt, input.shift_up, input.shift_down);
			output.gear = gearbox.gear();
			output.shifting = gearbox.shifting();
			return output;
		}

		bool request_up = input.shift_up;
		bool request_down = input.shift_down;
		if (assists.auto_shift) {
			apply_auto_shift(input, request_up, request_down);
		}

		gearbox.step(dt, request_up, request_down);

		const int gear = gearbox.gear();
		const double ratio = gearbox.total_ratio(gear);
		const double driveline_speed = input.axle_speed * ratio;

		double lever = input.clutch < 0.0 ? 0.0 : (input.clutch > 1.0 ? 1.0 : input.clutch);
		if (assists.auto_clutch) {
			lever = auto_clutch_lever(gear, driveline_speed);
		}

		const double rpm = rads_to_rpm(engine_speed_);

		// The auto-shift closes the throttle for the duration of the cut. A KZ
		// has no quickshifter and a real driver does this with his foot — the
		// dogs will not come out under load — so an assist that shifts for the
		// player has to do the lifting for them too. Without it the engine flares
		// against an unloaded gearbox and arrives in the next gear 4,000 rpm
		// higher than it left the last one.
		double throttle = input.throttle;
		if (assists.auto_shift && gearbox.shifting()) {
			throttle = 0.0;
		}

		const double engine_torque = engine.torque(rpm, throttle);
		const double slip = engine_speed_ - driveline_speed;

		// Three ways to be disconnected: neutral, mid-shift with the dogs out,
		// and a clutch lever that is holding the plates apart. The mid-shift case
		// is the one that produces the torque cut ARCHITECTURE.md §6.3 asks for,
		// and it is a disconnection rather than a torque multiplier of zero
		// because the engine really is free during those 65 ms — which is why a
		// clutchless upshift with the throttle still open flares the revs.
		const bool connected = gear != 0 && !gearbox.shifting() && clutch.capacity_at(lever) > 0.0;

		if (!connected) {
			locked_ = false;
			engine_speed_ += engine_torque / engine.inertia * dt;
			output.clutch_slip = slip;
		} else {
			// Staying locked and becoming locked are different questions. Staying
			// locked only asks whether the torque still fits; becoming locked also
			// asks whether the speeds have converged.
			const bool holds = std::fabs(engine_torque) <= clutch.capacity_at(lever);
			const bool lock = locked_
					? holds
					: clutch.can_lock(slip, engine_torque, lever, engine.inertia, dt);

			if (lock) {
				locked_ = true;
				// Rigid: the crank is geared to the axle, so its speed is not
				// integrated, it is *read* from the axle. The torque handed to
				// the solver is the engine's, geared up, with no inertia term in
				// it — the inertia goes back as `reflected_inertia` and the
				// solver divides by the sum. Putting it in both places would
				// count the crank twice.
				//
				// The assignment is a discontinuity — the crank's speed jumps to
				// the driveline's on the substep the lock is made, and the energy
				// difference goes nowhere. That is the grab a real clutch has, and
				// the size of it is bounded rather than arbitrary: `can_lock` only
				// allows the lock when the clutch could have absorbed the
				// remaining slip within the substep anyway.
				engine_speed_ = driveline_speed;
				output.axle_torque = engine_torque * ratio;
				output.reflected_inertia = engine.inertia * ratio * ratio;
				output.clutch_torque = engine_torque;
				output.clutch_slip = 0.0;
			} else {
				locked_ = false;
				const double transmitted =
						clutch.slipping_torque(lever, slip, engine.inertia, dt);
				engine_speed_ += (engine_torque - transmitted) / engine.inertia * dt;
				output.axle_torque = transmitted * ratio;
				// Zero on purpose. A slipping clutch is a torque source, not a
				// rigid connection, so nothing of the engine's inertia reaches
				// the axle. What does reach it is the gearbox's own shafts, and
				// referred through the final drive they come to about 0.002
				// kg·m² against an axle assembly of 0.05 — below the noise, and
				// left out rather than guessed at.
				output.reflected_inertia = 0.0;
				output.clutch_torque = transmitted;
				output.clutch_slip = slip;
			}
		}

		const double new_rpm = rads_to_rpm(engine_speed_);

		// The over-rev, and the reason a rev limiter cannot prevent it: the
		// limiter removes drive, and this engine is not being driven, it is being
		// turned by the road through a gearbox that has no idea it is doing
		// something stupid.
		if (engine.over_rev(new_rpm)) {
			engine.accumulate_over_rev(new_rpm, dt);
			output.over_rev = true;
		}

		// The stall. It can only happen with the assists off, which is exactly
		// the point of issue #38: the clutch has to be able to drag the engine
		// below the speed at which it can keep itself running, or a launch is not
		// a skill.
		if (new_rpm < engine.stall_rpm) {
			stalled_ = true;
			locked_ = false;
			engine_speed_ = 0.0;
			output.axle_torque = 0.0;
			output.reflected_inertia = 0.0;
			output.clutch_torque = 0.0;
		}

		output.engine_rpm = rads_to_rpm(engine_speed_);
		output.engine_torque = engine_torque;
		output.gear = gearbox.gear();
		output.shifting = gearbox.shifting();
		return output;
	}

	bool stalled() const { return stalled_; }
	bool clutch_locked() const { return locked_; }
	double engine_speed() const { return engine_speed_; }
	double engine_rpm() const { return rads_to_rpm(engine_speed_); }

	// A KZ is push-started: no starter, no battery, and a driver whose engine has
	// died waits for a marshal. Modeled as a method rather than a key because
	// where it is called from is a gameplay decision, not a physics one.
	void push_start() {
		stalled_ = false;
		engine_speed_ = rpm_to_rads(engine.idle_rpm);
	}

	// Put the drivetrain back to a known state. Used by the respawn in
	// scenes/game/proving_ground.tscn and by every test that wants a second run
	// to be comparable with the first — including the determinism check, which is
	// only meaningful if nothing survives a reset.
	// Put the kart in a gear at a road speed with the clutch already locked, the
	// way it would be if it had driven there. Needed by the respawn — dropping a
	// player back onto the track with a stalled engine at 100 km/h would be
	// absurd — and by any test or scenario that starts mid-corner rather than on
	// the grid.
	void engage(int gear, double axle_speed) {
		gearbox.force_gear(gear);
		stalled_ = false;
		engine_speed_ = axle_speed * gearbox.total_ratio(gear);
		locked_ = gear != 0;
		auto_clutch_engaged_ = locked_;
	}

	void reset() {
		stalled_ = false;
		locked_ = false;
		auto_clutch_engaged_ = false;
		engine_speed_ = rpm_to_rads(engine.idle_rpm);
		engine.repair();
		gearbox.force_gear(0);
	}

private:
	// The assist's clutch lever, 0..1.
	//
	// It moves the lever only across the range where the lever does anything —
	// `free_play` down to `bite_end` — so the fraction it computes is the clamping
	// fraction it gets. Mapping 0..1 onto the whole travel instead would make the
	// assist's numbers meaningless the moment the lever geometry was retuned.
	double auto_clutch_lever(int gear, double driveline_speed) {
		if (gear == 0 || gearbox.shifting()) {
			auto_clutch_engaged_ = false;
			return 1.0;
		}

		const double rpm = rads_to_rpm(engine_speed_);
		if (rpm < assists.clutch_dropout_rpm) {
			// Anti-stall. This single line is most of why the assist exists.
			auto_clutch_engaged_ = false;
			return 1.0;
		}
		if (auto_clutch_engaged_) {
			return 0.0;
		}
		if (std::fabs(engine_speed_ - driveline_speed) <= assists.clutch_engage_slip) {
			auto_clutch_engaged_ = true;
			return 0.0;
		}

		const double span = assists.clutch_in_full_rpm - assists.clutch_in_start_rpm;
		double fraction = span > 0.0 ? (rpm - assists.clutch_in_start_rpm) / span : 1.0;
		fraction = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
		return clutch.free_play - fraction * (clutch.free_play - clutch.bite_end);
	}

	void apply_auto_shift(const DrivetrainInput &input, bool &up, bool &down) const {
		if (gearbox.shifting()) {
			up = false;
			down = false;
			return;
		}

		const int gear = gearbox.gear();
		const double rpm = rads_to_rpm(engine_speed_);

		if (gear == 0) {
			// Sitting in neutral with the throttle open is a player asking to
			// go. Manual mode makes them select the gear.
			up = input.throttle > 0.05;
			down = false;
			return;
		}

		if (gear < kz::GEAR_COUNT && rpm > assists.upshift_rpm) {
			const double landed =
					rads_to_rpm(input.axle_speed * gearbox.total_ratio(gear + 1));
			up = landed > assists.upshift_floor_rpm;
			down = false;
			return;
		}

		if (gear > 1 && rpm < assists.downshift_rpm) {
			const double landed =
					rads_to_rpm(input.axle_speed * gearbox.total_ratio(gear - 1));
			down = landed < assists.downshift_guard_rpm;
			up = false;
			return;
		}

		up = false;
		down = false;
	}

	double engine_speed_ = 0.0; // rad/s at the crankshaft
	bool locked_ = false;
	bool stalled_ = false;
	bool auto_clutch_engaged_ = false;
};

} // namespace kart::core

#endif // KART_CORE_DRIVETRAIN_H
