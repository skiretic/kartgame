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
// "The solver did not supply a traction ceiling, so do not limit anything."
//
// Any negative value means this; the constant exists so the sentinel is written
// down once. See `DrivetrainInput::axle_traction_torque` for why it is negative
// rather than zero — zero is a legitimate answer and the two must not collide.
inline constexpr double NO_TRACTION_LIMIT = -1.0;

struct DrivetrainInput {
	double throttle = 0.0; // 0..1
	double clutch = 0.0; // 0..1, 1 = lever fully pulled, fully disengaged
	bool shift_up = false; // a request, true for one substep
	bool shift_down = false;
	double axle_speed = 0.0; // rad/s of the solid rear axle, from the solver

	// The most torque the rear tires can transmit to the road right now, N m at
	// the axle. **Negative means "not supplied"**, in which case the traction
	// assist below does nothing and this file behaves exactly as it did before it
	// existed. The solver fills it, because it is the only thing that knows the
	// current normal loads, the surface, and how much of the friction circle the
	// lateral force has already spent.
	//
	// The sentinel is negative rather than zero, and that is not a style choice.
	// It was `<= 0.0` for one measured iteration, and **zero is the single most
	// important value this field takes**: it is what the solver returns when the
	// rear axle is already past its peak slip ratio and the correct amount of drive
	// torque is none at all. Treating it as "no information" switched the assist off
	// in exactly the case it exists for, and the launch burnout was unchanged to
	// three significant figures — 1.49 s above a slip ratio of 0.5 before and after,
	// which reads exactly like an assist that does not work rather than like a
	// sentinel collision. Same family as `Dictionary.get(key, default)` hiding a
	// renamed key: a sentinel that overlaps a legitimate value hides it forever.
	//
	// **This is the signal the assists were missing, and its absence is #137.**
	// Every loop in this file used to close on `axle_speed`, and `axle_speed` is
	// not road speed once the tires are spinning: it is the *wheelspin*. So the
	// auto-clutch read a rising axle as a kart that was accelerating and kept
	// feeding the lever in, `can_lock` saw the axle "catch up" to the engine and
	// welded the crank to a wheel doing 8.7x road speed, and the auto-shift's
	// "what will the engine be doing in the next gear" guard computed its answer
	// from the same spinning axle and upshifted into a continuing burnout.
	//
	// Spelled `NO_TRACTION_LIMIT` rather than `-1.0` because it is now written in
	// two files — the solver returns it and this file tests for it — and a
	// sentinel duplicated as a literal is one edit away from the two halves
	// disagreeing about what "not supplied" means.
	double axle_traction_torque = NO_TRACTION_LIMIT;

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
	// torque curve at a stable point.
	//
	// **This used to end "the launch therefore settles near 10,500 rpm with the
	// clutch slipping — which is what a KZ driver does by hand", and it does not.**
	// Measured on the shipped launch: the engine ramps monotonically from 1,911 to
	// 10,411 rpm over 0.67 s, mean 6,068, and then the clutch **locks** at 0.73 s
	// and 1.14 m/s. It settles nowhere. The equilibrium the paragraph describes is
	// real arithmetic — capacity at 10,500 rpm is 22.5 N m against about 26 N m of
	// engine torque, so the crossing is nearer 11,500 — but the launch never gets
	// there, because the rear tires break traction first and the *axle* accelerates
	// up to meet the engine instead of the other way round. `can_lock` then sees a
	// synchronized clutch and welds the crank to a wheel doing 8.7x road speed.
	// Fourth case of a docstring describing behavior nobody built; the traction
	// assist below is what closed it.
	double clutch_in_start_rpm = 8000.0;
	double clutch_in_full_rpm = 13000.0;

	// **The traction half of the auto-clutch.** Issue #137's launch, which is a
	// necessary part of #137 and is not the whole of it — the ticket is still open
	// and the skidpad still departs. See the ADR.
	//
	// The rpm schedule above is a feed-forward: it decides how hard to clamp the
	// plates from how far the engine is into its band, and it cannot see the road.
	// That is fine right up until the rear tires let go, at which point every
	// number it is reading becomes a description of the wheelspin rather than of
	// the kart, and it keeps feeding the lever in against a wheel that has already
	// stopped transmitting. Measured on a full-throttle launch before this existed:
	// the rear axle blew through its peak slip ratio in **75 ms**, reached 8.68x
	// road speed, and stayed above a slip ratio of 1.0 for **1.18 s**. After:
	// 2.08x, and 0.22 s.
	//
	// So the assist also holds the torque below what the tires can actually take.
	// `DrivetrainInput::axle_traction_torque` is that number and `vehicle.h`'s
	// `rear_traction_torque` computes it; the rule here is one line with no
	// constant in it — do not ask the rear tires for more than they can transmit.
	//
	// It is an **assist** and it is switched with the rest of them. With
	// `auto_clutch` off nothing here runs, and a driver who dumps the lever at
	// 10,000 rpm gets the burnout he asked for, which is the behavior issue #38
	// wants available. What it must never become is a hidden traction limit on the
	// unassisted car.
	bool traction_clutch = true;

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
			// **A manual request wins, and auto-shift stays on.** This used to pass
			// `request_up`/`request_down` straight into `apply_auto_shift`, which
			// writes both flags on every one of its paths -- so with `auto_shift`
			// defaulting to **true**, a driver's shift was read off the pad, latched
			// by `KartBody::request_shift_up`, and then overwritten and thrown away
			// before the gearbox ever saw it. E/Q and Square/Cross did nothing at
			// all on a fresh launch, on a shifter kart, with `control_hints.gd`
			// printing both bindings on screen. That is the "advertised controls
			// drift from the real ones" family, one level worse because the key is
			// on the screen -- and it was found sideways, by an audio probe
			// reporting zero gearshift strikes at seven speeds with the shift layer
			// working perfectly.
			//
			// Anthony chose this policy over "a manual shift switches auto off":
			// the buttons always do what the hints say, and G still turns the assist
			// off outright.
			//
			// Safe to OR rather than to branch. `gearbox.step` refuses both flags
			// while a shift is in flight and resolves up-and-down-together as "up
			// wins", so a manual request surviving `apply_auto_shift`'s own shifting
			// guard changes nothing -- it meets the same refusal one call later.
			bool auto_up = false;
			bool auto_down = false;
			apply_auto_shift(input, auto_up, auto_down);
			request_up = request_up || auto_up;
			request_down = request_down || auto_down;
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

		double engine_torque = engine.torque(rpm, throttle);

		// **The traction assist, and issue #137's fix.** See
		// `DrivetrainAssists::traction_clutch` and `DrivetrainInput::axle_traction_torque`.
		//
		// It is a **torque cut at the crank**, which is both what real traction
		// control does — it pulls ignition or fuel, it does not touch the clutch —
		// and, measured, the only actuator here that does not cost more than it
		// buys. Capping the *clutch capacity* instead was tried first and is
		// falsified: it holds the pack open, `reflected_inertia` is zero for a
		// slipping clutch by construction, and the low-speed longitudinal clamp in
		// `vehicle.h` is inversely proportional to that inertia — so the tire force
		// ceiling collapsed from 1,270 N to about 600 N per rear corner and 0-100
		// went **4.50 s -> 6.04 s** while the burnout it was aimed at did close. A
		// torque cut leaves the lock state, the reflected inertia and the clamp
		// exactly where they were.
		//
		// `ratio` converts the axle-side limit to the crank side, which is the side
		// every torque in this branch is expressed on, and it is the same ratio the
		// torque is multiplied by on the way out — so capping the crank at
		// `axle_traction_torque / ratio` caps the axle at `axle_traction_torque` and
		// at nothing else.
		//
		// Only ever downward and only ever on drive. A negative `engine_torque` is
		// engine braking, which no traction assist has any business touching.
		if (assists.auto_clutch && assists.traction_clutch &&
				input.axle_traction_torque >= 0.0 && ratio > 0.0 && engine_torque > 0.0) {
			const double crank_limit = input.axle_traction_torque / ratio;
			if (crank_limit < engine_torque) {
				engine_torque = crank_limit;
			}
		}

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
				double transmitted =
						clutch.slipping_torque(lever, slip, engine.inertia, dt);
				// **The same cut, on the other branch, and it is not optional.** A
				// slipping clutch transmits its *capacity*, which does not depend on
				// engine torque at all — so cutting the crank alone limits the axle
				// only while the plates are locked, and a launch is precisely the case
				// where they are not. Modeled as the assist feathering the lever,
				// which is what the driver it stands in for is doing with his left
				// hand, rather than as the plates transmitting less than they are
				// clamped for.
				if (assists.auto_clutch && assists.traction_clutch &&
						input.axle_traction_torque >= 0.0 && ratio > 0.0 && transmitted > 0.0) {
					const double crank_limit = input.axle_traction_torque / ratio;
					if (crank_limit < transmitted) {
						transmitted = crank_limit;
					}
				}
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
