#ifndef KART_CORE_TIRE_H
#define KART_CORE_TIRE_H

#include <cmath>

// The tire model. ARCHITECTURE.md §6: "Simplified Pacejka: lateral by slip
// angle, longitudinal by slip ratio, combined through a friction ellipse, grip
// falling with normal load."
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017 —
// which is what lets this file be unit tested against its own curve shape rather
// than judged by driving.
//
// ## What "simplified Pacejka" means here
//
// The Magic Formula proper has more than twenty coefficients per axis, fitted to
// rig data nobody has for a kart slick. What is used is its core shape:
//
//     F(x) = D * sin(C * atan(B*x - E*(B*x - atan(B*x))))
//
// with four coefficients whose jobs are separable, which is the reason to use
// this form rather than a hand-drawn curve:
//
//   * `D` is the peak — the force at the top of the curve, and the only place
//     the friction coefficient and the normal load enter;
//   * `B` sets the initial slope, so `B*C*D` is the cornering stiffness, the
//     quantity that decides how the kart responds to small steering inputs;
//   * `C` decides the shape past the peak — how much force is left at large
//     slip, which is what "the rear steps out and then holds" versus "the rear
//     goes and keeps going" feels like;
//   * `E` moves where the peak sits without changing the initial slope.
//
// ## Why a kart is not a car here
//
// Kart slicks on a hot track run a friction coefficient well above 1 — the
// values below peak near 2.1 at nominal load — and they reach that peak at a
// small slip angle, around 8 degrees, then fall away gently. That combination is
// most of why a kart feels sharp: the grip arrives immediately and the warning
// before it leaves is short.
//
// ## Load sensitivity is not a refinement
//
// A tire's friction coefficient *falls* as vertical load rises, which is why
// weight transfer costs a vehicle grip rather than merely moving it around. With
// a constant coefficient, a kart in a corner would keep exactly the grip it had
// in a straight line, load transfer would be free, and the inside-rear lift that
// ARCHITECTURE.md §6 calls the defining kart dynamic would produce no
// understeer at all. It is modeled linearly against a nominal load, which is the
// standard first approximation and is stated here so nobody mistakes it for a
// fitted curve.

namespace kart::core {

// Pacejka-style coefficients for one axis of one tire.
struct TireCurve {
	double stiffness = 9.0; // B
	double shape = 1.55; // C
	double curvature = -0.45; // E

	// Peak friction coefficient at `nominal_load`, dimensionless.
	//
	// **On a vehicle with no downforce this number *is* the cornering g.** Four
	// tires each producing `mu * N` sum to `mu * m * g`, so the lateral
	// acceleration is `mu` and nothing else — which makes ARCHITECTURE.md §6.4's
	// 2.0-2.5 g band a direct statement about the tire and not about the chassis.
	// The first value here was 1.75 and the test that checks the envelope caught
	// it immediately: it could only ever reach 1.77 g.
	//
	// 2.10 is high for a tire and correct for this one. Kart slicks run the
	// highest friction coefficients in motorsport outside downforce cars — no
	// suspension to absorb load variation, a soft compound, and a 175 kg vehicle
	// that never overheats them.
	double peak_friction = 2.10;

	// The load the peak coefficient was measured at, newtons. A KZ at 175 kg
	// with a rearward bias carries roughly 500 N on each rear tire, so that is
	// the load the curve is anchored to rather than a round number.
	double nominal_load = 500.0;

	// How fast the friction coefficient falls as load rises, per unit of
	// relative load. 0.10 means a tire at twice its nominal load has 10% less
	// grip per unit load available.
	double load_sensitivity = 0.10;

	// Friction coefficient at a given vertical load. Clamped below so that a
	// wildly overloaded tire degrades rather than inverting: without the floor,
	// a large enough load produces negative friction, and negative friction is a
	// tire that accelerates the kart sideways.
	double friction_at(double normal_load) const {
		if (normal_load <= 0.0) {
			return 0.0;
		}
		const double relative = (normal_load - nominal_load) / nominal_load;
		const double scaled = peak_friction * (1.0 - load_sensitivity * relative);
		return scaled > 0.05 * peak_friction ? scaled : 0.05 * peak_friction;
	}

	// The Magic Formula's normalized shape, peaking at 1.0. Kept separate from
	// the force so that the curve can be tested without a load, and so the
	// combined-slip code below can ask for "how much of the peak is left".
	double normalized(double slip) const {
		const double bx = stiffness * slip;
		return std::sin(shape * std::atan(bx - curvature * (bx - std::atan(bx))));
	}

	// Force, newtons. Positive slip gives positive force.
	double force(double slip, double normal_load) const {
		if (normal_load <= 0.0) {
			return 0.0;
		}
		return friction_at(normal_load) * normal_load * normalized(slip);
	}

	// Slope at zero slip, in newtons per unit slip. For the lateral axis this is
	// cornering stiffness, and it is the number a vehicle dynamicist asks for
	// first. Derived rather than tuned: at small `x` the formula reduces to
	// `D * C * B * x`.
	double stiffness_at(double normal_load) const {
		return friction_at(normal_load) * normal_load * shape * stiffness;
	}

	// Where the curve peaks, in the same unit as `slip`. Found by bisection on
	// the derivative rather than in closed form — the Magic Formula has no
	// analytic peak — and used by the tests to assert the peak is where a kart
	// slick's is, and by the tuning UI to show it.
	double peak_slip(double search_limit = 1.0) const {
		double low = 0.0;
		double high = search_limit;
		for (int iteration = 0; iteration < 60; ++iteration) {
			const double third = (high - low) / 3.0;
			const double a = low + third;
			const double b = high - third;
			if (normalized(a) < normalized(b)) {
				low = a;
			} else {
				high = b;
			}
		}
		return (low + high) * 0.5;
	}
};

// One tire's slip state for one substep.
struct TireSlip {
	// Radians. The angle between where the wheel points and where it is going.
	double slip_angle = 0.0;

	// Dimensionless. `(wheel surface speed - road speed) / |road speed|`:
	// positive under drive, negative under braking, and it is why a locked wheel
	// sits at -1 and a spinning one is unbounded above.
	double slip_ratio = 0.0;

	// Newtons, never negative. A wheel off the ground carries no load and
	// produces no force, which is the case that makes the inside-rear lift in
	// ARCHITECTURE.md §6 do anything.
	double normal_load = 0.0;

	// Surface multiplier from the material zone — 1.0 asphalt, less on grass or
	// dirt. ARCHITECTURE.md §6's surface table feeds this.
	double surface_grip = 1.0;
};

// The forces one tire produces, in the tire's own frame: `longitudinal` along
// the direction the wheel rolls, `lateral` along its axle.
struct TireForce {
	double longitudinal = 0.0;
	double lateral = 0.0;

	// How much of the available grip the two axes together demanded, before the
	// ellipse scaled them back. Above 1 means it had to scale them, which is the
	// definition of *combined-slip* saturation.
	//
	// **It is not a measure of sliding, and it used to claim to be.** The comment
	// here said "above 1 ... is the definition of sliding — telemetry and the
	// tire-scrub audio in §12 both want it". Measured, that is exactly backwards
	// in pure slip:
	//
	//     slip ratio   longitudinal force   utilization
	//        +0.108              1050.0 N         1.000    at the peak
	//        +0.300               840.2 N         0.800
	//        +1.000               642.5 N         0.612    fully locked
	//
	// Each axis's fraction reduces algebraically to `normalized(slip)`, which
	// peaks at 1.0 *and falls past the peak*. So a locked wheel reports 0.612 and
	// keeps falling as it slides harder, and pure slip can never exceed 1 at all
	// — only combined slip can, and at most sqrt(2). Both named consumers would
	// have read a sliding tire as a gripping one.
	//
	// The quantity they actually want is `slide` below. This one is still worth
	// returning, because "did the ellipse bind" is a real question about combined
	// slip, but it is now named for what it does.
	double utilization = 0.0;

	// How far past its peak the tire is, 1.0 at the peak and unbounded above.
	//
	// This is the sliding measure. Unlike `utilization` it is monotone in slip,
	// so it keeps rising as a wheel locks or spins, which is what the §12 scrub
	// audio modulates on and what the telemetry panel needs in order to say "this
	// tire is past the peak" rather than "this tire is producing less force than
	// it could, for one of two opposite reasons".
	//
	// Combined as a quadrature sum of each axis against its own peak slip, so it
	// reaches sqrt(2) when both axes are at peak simultaneously.
	double slide = 0.0;
};

class Tire {
public:
	TireCurve lateral;
	TireCurve longitudinal;

	// Where each curve peaks, cached. `peak_slip` is a 60-step bisection and the
	// `slide` figure needs it on every tire on every substep, so it is computed
	// once here rather than 1,920 times a second.
	//
	// The cost of caching is that it goes stale if a coefficient is changed after
	// construction, which the tuning UI will want to do. `refresh_peaks()` is the
	// answer and callers that mutate a curve must call it — stated here because a
	// silently stale peak makes `slide` wrong in a way nothing else notices.
	double lateral_peak_slip = 0.0;
	double longitudinal_peak_slip = 0.0;

	Tire() {
		// The longitudinal curve is stiffer and peaks earlier than the lateral
		// one, which is true of real tires. Measured from these coefficients:
		// peak braking arrives at a slip ratio of 0.108, and a fully locked wheel
		// still returns 61% of it.
		//
		// This comment used to end "and is gone by 1.0", which is wrong by 61
		// percentage points and contradicted test_tire.cpp's own deliberate
		// assertion that the force must *not* fall away to nothing.
		longitudinal.stiffness = 12.0;
		longitudinal.shape = 1.65;
		longitudinal.curvature = -0.30;
		refresh_peaks();
	}

	void refresh_peaks() {
		lateral_peak_slip = lateral.peak_slip();
		longitudinal_peak_slip = longitudinal.peak_slip();
	}

	// Combined-slip forces through a friction ellipse.
	//
	// Each axis is evaluated on its own curve first, then the pair is scaled back
	// onto the ellipse if together they ask for more than the tire has. That is
	// the standard simplification, and its one honest weakness is worth stating:
	// it under-predicts the *shape* change real combined slip produces, because a
	// tire's lateral curve does not merely shrink under braking, it also shifts.
	// The pure-slip cases are exact; the diagonal is approximate.
	TireForce evaluate(const TireSlip &slip) const {
		TireForce result;
		if (slip.normal_load <= 0.0) {
			return result;
		}

		const double grip = slip.surface_grip > 0.0 ? slip.surface_grip : 0.0;
		const double lateral_capacity =
				lateral.friction_at(slip.normal_load) * slip.normal_load * grip;
		const double longitudinal_capacity =
				longitudinal.friction_at(slip.normal_load) * slip.normal_load * grip;

		result.lateral = lateral.force(slip.slip_angle, slip.normal_load) * grip;
		result.longitudinal = longitudinal.force(slip.slip_ratio, slip.normal_load) * grip;

		if (lateral_capacity <= 0.0 || longitudinal_capacity <= 0.0) {
			return result;
		}

		const double lateral_fraction = result.lateral / lateral_capacity;
		const double longitudinal_fraction = result.longitudinal / longitudinal_capacity;
		const double demand = std::sqrt(
				lateral_fraction * lateral_fraction +
				longitudinal_fraction * longitudinal_fraction);
		result.utilization = demand;

		// How far past peak each axis is, in quadrature. Monotone in slip, so it
		// keeps climbing as the wheel locks or spins — see `TireForce::slide`.
		if (lateral_peak_slip > 0.0 && longitudinal_peak_slip > 0.0) {
			const double lateral_slide = slip.slip_angle / lateral_peak_slip;
			const double longitudinal_slide = slip.slip_ratio / longitudinal_peak_slip;
			result.slide = std::sqrt(
					lateral_slide * lateral_slide +
					longitudinal_slide * longitudinal_slide);
		}

		if (demand > 1.0) {
			result.lateral /= demand;
			result.longitudinal /= demand;
		}
		return result;
	}
};

} // namespace kart::core

#endif // KART_CORE_TIRE_H
