#ifndef KART_CORE_CLUTCH_H
#define KART_CORE_CLUTCH_H

#include <cmath>

// The clutch. ARCHITECTURE.md §6.3: "Slip element with a torque capacity; locks
// above a threshold."
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## What a KZ clutch is
//
// A dry multi-plate pack — five friction discs and five steels on an IAME
// Screamer, alternating coated and steel plates on a TM — squeezed by coil
// springs and released by a lever on the left of the steering wheel. It is not
// the centrifugal clutch a TaG kart has, and the difference is the whole of why
// a KZ is hard: nothing engages it for you, and a KZ launches at 10,000 rpm on a
// deliberately slipping clutch, which is a skill and not a button.
//
// Racing upshifts do not use it at all. The driver unloads the throttle for the
// 65 ms the dogs need and the lever never moves; §6.3 says as much, and it means
// the clutch's real job is launches, saves, and the pit lane.
//
// ## The model: Coulomb friction with a capacity
//
// A friction clutch does not transmit "some fraction of the torque". It
// transmits whatever the two sides ask of it up to a limit set by how hard the
// plates are clamped, and above that limit it slips and transmits exactly that
// limit. Two regimes and a rule for switching between them, and the rule is what
// makes it feel like a clutch:
//
//   * **Slipping** — the plates are moving relative to each other, so the torque
//     is exactly the capacity, in the direction that would bring them together.
//   * **Locked** — the plates move as one, and the torque is whatever is needed
//     to keep it that way, which the drivetrain computes. The lock survives only
//     while that torque stays inside the capacity.
//
// Where this is a simplification: real friction packs have a static coefficient
// slightly above the dynamic one, so a clutch grabs a little as it locks and the
// lockup point is not exactly where the slip reaches zero. That is left out —
// one coefficient, and the lock threshold below does the same job with a number
// a tester can see.

namespace kart::core {

class Clutch {
public:
	// Torque the pack transmits fully clamped, N·m.
	//
	// **Not sourced.** No manufacturer publishes a clutch capacity and no dealer
	// listing gives spring rates or a friction coefficient, so this is a design
	// figure: 45 N·m is 1.7x the engine's 26.2 N·m peak, which is the usual
	// sizing margin for a clutch that must not slip in service. If it is ever
	// measured, it is the one number in this file that should move.
	double capacity = 45.0;

	// Lever travel, 0 (released, fully clamped) to 1 (pulled, fully open).
	//
	// A real lever does nothing for the first part of its travel — free play, so
	// the plates cannot drag when the driver's hand is resting on it — and the
	// plates are fully clamped well before the lever reaches the grip. So the
	// entire clutch lives in the middle of the travel: from `free_play` down to
	// `bite_end`. Getting this wrong makes the clutch feel like a switch, which
	// is the most common complaint about clutch modeling in a driving game.
	double free_play = 0.75;
	double bite_end = 0.35;

	// Below this relative speed the plates are stuck no matter what, rad/s.
	// 2 rad/s is 19 rpm at the crank. It is a floor and not the main test — see
	// `can_lock`, which was rewritten after the first version of it never locked
	// at all.
	double lock_slip = 2.0;

	// How hard the plates are clamped, 0 to 1, for a given lever position.
	//
	// Smoothstep across the bite range rather than a straight line: the clamping
	// force of a spring pack against a released lever is not linear in travel,
	// and more to the point a linear ramp gives the same sensitivity at the
	// moment of first contact as at full clamp, which is precisely where a driver
	// wants the fine control.
	double engagement(double lever) const {
		const double clamped = lever < 0.0 ? 0.0 : (lever > 1.0 ? 1.0 : lever);
		if (clamped >= free_play) {
			return 0.0;
		}
		if (clamped <= bite_end) {
			return 1.0;
		}
		const double travel = (free_play - clamped) / (free_play - bite_end);
		return travel * travel * (3.0 - 2.0 * travel);
	}

	// Torque capacity at a lever position, N·m.
	double capacity_at(double lever) const {
		return capacity * engagement(lever);
	}

	// May the plates be treated as one body?
	//
	// Two conditions, and the second one is the one that had to be measured
	// rather than assumed. The torque the lock would have to carry must fit
	// inside the capacity — a clutch that locks on speed alone will happily
	// transmit more than it can hold, which shows up as a kart whose clutch
	// cannot be slipped under power no matter where the lever is. And the plates
	// have to be close enough in speed to be brought together.
	//
	// "Close enough" is *not* a fixed slip threshold, which is what this first
	// was, and it did not work. Under a first-gear launch the driveline gains
	// about 5 rad/s of speed per 240 Hz substep, so the slip left after a substep
	// of slipping is around 5 rad/s and never falls under a 2 rad/s threshold:
	// the clutch slipped from a standing start all the way to the top of sixth
	// and the reflected inertia was zero for the entire run. The trace that found
	// it is in the commit message.
	//
	// The condition that is actually meaningful is whether the clutch could
	// absorb the remaining relative speed inside one substep — `I * slip / dt`
	// against the capacity. If it could, calling the plates "slipping" is a
	// statement about the step size and not about the clutch. It also scales
	// correctly on its own: a barely-engaged clutch has a small capacity and
	// therefore a small window, which is exactly right.
	bool can_lock(double slip, double demanded_torque, double lever,
			double engine_inertia, double dt) const {
		const double limit = capacity_at(lever);
		if (std::fabs(demanded_torque) > limit) {
			return false;
		}
		if (std::fabs(slip) <= lock_slip) {
			return true;
		}
		if (dt <= 0.0) {
			return false;
		}
		return std::fabs(engine_inertia * slip / dt) <= limit;
	}

	// The torque a slipping clutch transmits, N·m, signed to oppose the slip.
	//
	// `slip` is engine speed minus driveline speed at the clutch, rad/s: positive
	// means the engine is turning faster and the clutch is driving the kart.
	//
	// The clamp is a fixed-step integration guard and it is worth spelling out.
	// Applying a constant capacity for a whole substep can remove more relative
	// speed than there was, which flips the sign of the slip, which flips the
	// sign of the torque next substep — a clutch buzzing at 120 Hz that sounds
	// exactly like a broken solver. Limiting the torque to the amount that brings
	// the engine side exactly to the driveline speed within `dt` is the standard
	// fix. It errs conservative because it ignores the axle side moving toward
	// the engine at the same time, and being conservative here costs a little
	// grab and buys unconditional stability at 240 Hz.
	double slipping_torque(double lever, double slip, double engine_inertia, double dt) const {
		const double limit = capacity_at(lever);
		if (limit <= 0.0 || dt <= 0.0) {
			return 0.0;
		}
		const double convergence = std::fabs(engine_inertia * slip / dt);
		const double magnitude = limit < convergence ? limit : convergence;
		return slip >= 0.0 ? magnitude : -magnitude;
	}
};

} // namespace kart::core

#endif // KART_CORE_CLUTCH_H
