#ifndef KART_CORE_SUSPENSION_H
#define KART_CORE_SUSPENSION_H

#include "core/units.h"
#include "core/vec3.h"

#include <cmath>

// One corner of the kart, between its mount point and the road.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## A kart has no suspension, and this file is not a suspension model
//
// There is no spring and no damper on a KZ chassis. Nothing here corresponds to
// a part you could unbolt. What actually deflects between the wheel hub and the
// driver's seat is, in order of how much it contributes:
//
//   1. the **tire sidewall**, at roughly 0.9 bar on a 5-inch rim;
//   2. the **rear axle** in bending, and the front stub axles and kingpins;
//   3. the **frame twisting**, which is the big one and lives in
//      `chassis_flex.h` rather than here because it couples all four corners.
//
// So `spring_rate` below is a tire's vertical rate, not a coil's, and the
// damping is not a shock absorber — it is tire hysteresis plus whatever the
// numerical integration needs to not ring. Both facts change what a plausible
// number looks like by an order of magnitude, which is why they are stated
// before any of the numbers: a car's corner spring is around 30 N/mm and a kart
// tire's is around 100-280 N/mm, and someone reaching for a remembered figure
// would be wrong by 5x in the direction that makes the kart feel like a sofa.
//
// ## What `rest_length`, `max_travel` and `max_droop` mean together
//
// The three are not independent, and reading them as if they were is the error
// this paragraph exists to prevent.
//
//     ray_length == rest_length                 static equilibrium, load = static
//     ray_length == rest_length + max_droop     force reaches zero, wheel leaving
//     ray_length == rest_length - max_travel    bump stop engages
//
// `max_droop` is therefore **the static deflection**, not a free parameter: a
// wheel carrying its static load leaves the ground exactly when the chassis has
// risen at that corner by however far the tire was squashed. That gives the
// spring its preload without a fourth field — the free length is
// `rest_length + max_droop` and the static load is `spring_rate * max_droop`,
// which `static_load()` returns and the tests check against the KZ mass.
//
// For this kart those deflections are **3.4 mm at the front and 1.8 mm at the
// rear**. That is the whole travel budget, and it is why the inside-rear lift in
// ARCHITECTURE.md §6 is such an abrupt event on a real kart: the wheel is not
// eased off the ground over 100 mm of droop the way a car's is, it runs out of
// tire in under two millimeters and then it is simply gone.
//
// ## Why the damping velocity is an input and not a difference of ray lengths
//
// `step()` is handed the closing speed rather than differencing `ray_length`
// itself, and that is a measurement, not a preference. A raycast in a
// single-precision engine returns a length with a few hundredths of a millimeter
// of noise on it, and differencing that at 240 Hz turns it into phantom velocity
// that the damper turns into force. Measured on the rear corner over 2,000
// substeps of a kart standing still on a flat floor:
//
//     ray noise      velocity from the chassis      differenced and filtered
//     +/-0.001 mm         0.55 N   (0.11%)              2.04 N   (0.41%)
//     +/-0.010 mm         5.55 N   (1.11%)             20.44 N   (4.11%)
//     +/-0.050 mm        27.74 N   (5.57%)            102.20 N  (20.53%)
//
// The chassis body already knows its own velocity exactly, and projecting the
// mount point's velocity onto the contact normal costs nothing and has no noise
// in it. `RaySpeed` below is the fallback for a caller that genuinely has only
// two ray lengths; the right-hand column is what it costs.
//
// **What does not come out in this file** is the left-hand column, and it is
// worth saying so. That is the *spring* acting on the noise — 277 kN/m against
// a tenth of a millimeter is 28 N — and no amount of filtering here removes it.
// Issue #31's "no jitter at any speed" therefore has a hard dependency on the
// raycast's own precision, which is a Godot-side number.

namespace kart::core {

// The four corners, in the order everything in the solver uses. ARCHITECTURE.md
// §8 rule 4 asks for stable iteration order; this is that order, and the tire
// arrays, the telemetry rows and the debug draw all index by it. Nothing may
// reorder them.
enum Corner : int {
	CORNER_FL = 0,
	CORNER_FR = 1,
	CORNER_RL = 2,
	CORNER_RR = 3
};

inline constexpr int CORNER_COUNT = 4;

// One corner's ground query, filled by the Godot side from a raycast.
struct WheelContact {
	bool hit = false;
	double ray_length = 0.0; // mount point to contact, meters

	// How far the ray was cast, meters. Read **only when `hit` is false**, and
	// then it is the whole of what a miss tells you: there is no ground within
	// this distance of the mount, so the tire is at least `cast_length` less the
	// free length clear of it. `lift_height` reports that bound rather than zero.
	//
	// A caller that leaves this at zero gets a lift of zero for a miss, which is
	// exactly the read-out issue #136 was filed about — a kart in flight
	// reporting no lift on any corner. `KartVehicle::step` fills it from
	// `ray_length(corner)`; a test or a probe that builds a `WheelContact` by hand
	// has to fill it too, and `tests/core/test_suspension.cpp` pins both halves.
	double cast_length = 0.0;

	Vec3 normal; // world frame, unit
	double surface_grip = 1.0;
};

// One corner's fixed geometry and rates.
struct CornerSetup {
	double rest_length = 0.0; // ray length at static equilibrium
	double max_travel = 0.0; // meters of compression before the bump stop
	double max_droop = 0.0; // meters of extension before the wheel is off the ground
	double spring_rate = 0.0; // N/m
	double bump_damping = 0.0; // N per m/s, compressing
	double rebound_damping = 0.0; // N per m/s, extending

	// Load carried at `rest_length`. Not stored, because a stored copy is a
	// second place for the static mass split to live and the two would drift.
	double static_load() const { return spring_rate * max_droop; }

	// Ray length at which the spring produces nothing.
	double free_length() const { return rest_length + max_droop; }

	// Undamped natural frequency of this corner, hertz, against the mass it
	// carries. Reported rather than asserted because it is the number that says
	// whether the integration below is safe: a kart's is 8-12 Hz, an order above
	// a road car's 1.5 Hz, because the only spring is the tire.
	double natural_frequency_hz(double corner_mass) const {
		if (corner_mass <= 0.0 || spring_rate <= 0.0) {
			return 0.0;
		}
		return std::sqrt(spring_rate / corner_mass) / (2.0 * PI);
	}

	// Critical damping for this corner, N per m/s.
	double critical_damping(double corner_mass) const {
		if (corner_mass <= 0.0 || spring_rate <= 0.0) {
			return 0.0;
		}
		return 2.0 * std::sqrt(spring_rate * corner_mass);
	}

	// `bump_damping` as a fraction of critical. A real kart's is near 0.05 —
	// tire hysteresis and nothing else, which is why karts skip over curbs
	// rather than absorbing them. What is used is higher; see
	// `front_corner_setup()` for why and what it costs.
	double bump_damping_ratio(double corner_mass) const {
		const double critical = critical_damping(corner_mass);
		return critical > 0.0 ? bump_damping / critical : 0.0;
	}

	double rebound_damping_ratio(double corner_mass) const {
		const double critical = critical_damping(corner_mass);
		return critical > 0.0 ? rebound_damping / critical : 0.0;
	}

	// `omega * dt`, the number that decides whether explicit integration of this
	// spring is stable at a given step. Symplectic Euler — which ADR-0032
	// measured Godot to be using — is stable while this is below 2. Anything
	// approaching 1 is ringing rather than simulating.
	double stability_number(double corner_mass, double dt) const {
		if (corner_mass <= 0.0 || spring_rate <= 0.0) {
			return 0.0;
		}
		return std::sqrt(spring_rate / corner_mass) * dt;
	}

	// The damper's own explicit-integration limit, `c * dt / m`. A damper
	// integrated explicitly inverts above 2 — it adds energy instead of removing
	// it — and that limit is separate from the spring's and is the one that bites
	// first when someone raises the damping to cure a bounce.
	double damping_stability_number(double corner_mass, double dt) const {
		if (corner_mass <= 0.0) {
			return 0.0;
		}
		const double larger = bump_damping > rebound_damping ? bump_damping : rebound_damping;
		return larger * dt / corner_mass;
	}
};

// What the solver hands in each substep, per corner, and gets back.
struct CornerState {
	double compression = 0.0; // meters, positive is compressed
	double velocity = 0.0; // m/s of compression, positive compressing
	double geometric_offset = 0.0; // meters, from steering jacking; positive lifts the chassis
	double normal_force = 0.0; // N, never negative — a tire cannot pull
	bool grounded = false; // carrying load: `normal_force > 0`

	// The tire is touching: the spring has travel left, whether or not the corner
	// produced any force with it. `grounded` implies this and not the other way
	// round — the rebound damper can cancel the spring on a tire that is still on
	// the road, which is the state issue #136 found nobody had named.
	// `vehicle_state.h`'s `WheelTelemetry` has the three-way definition.
	bool tire_contact = false;
};

// One corner's spring, damper and travel limits.
//
// Deliberately holds no state of its own. Everything that changes lives in the
// caller's `CornerState`, so a substep is a pure function of `(setup, contact,
// state, dt)` and ARCHITECTURE.md §8's replay promise does not depend on
// whatever this object happened to remember from the last tick.
class CornerSuspension {
public:
	CornerSetup setup;

	// Rate beyond `max_travel`, as a multiple of `spring_rate`.
	//
	// A bump stop, on a kart, is the tire's sidewall folding onto the rim and
	// then the frame rail reaching the road. Neither is soft. 10x is chosen to
	// be firm enough that a curb strike stops the wheel inside a millimeter or
	// two rather than letting the ray run out — issue #31's "curb strikes do not
	// tunnel" — and low enough that it does not blow past this corner's
	// stability number: at 10x the effective frequency rises by sqrt(10), which
	// takes the rear from 0.31 to 0.97 at a 240 Hz step. Still under 2, and
	// close enough to it that raising this further needs the check re-run.
	double bump_stop_ratio = 10.0;

	// Advance one corner by one substep.
	//
	// `closing_speed` is the rate at which the mount point is approaching the
	// contact, meters per second, positive compressing, taken from the chassis
	// body's own velocity. See the header comment on why it is not differenced
	// from `ray_length` here.
	//
	// `state.geometric_offset` is an input, filled by `steering.h` from the
	// caster jacking. Positive lifts the chassis at this corner, so it enters as
	// an addition to the rest position: the wheel is pushed further down
	// relative to its mount, the spring is compressed more, and the extra force
	// is what raises the chassis. Everything else in `state` is output.
	//
	// There is no `dt`, because nothing here integrates. A corner is an algebraic
	// function of where the ground is and how fast it is being approached; the
	// integration is Jolt's, and ADR-0032 measured what that costs — the substeps
	// sum their forces and the body sees one application per 120 Hz tick, so the
	// stability numbers that matter are the 120 Hz ones and both are reported.
	void step(const WheelContact &contact, double closing_speed, CornerState &state) const {
		state.velocity = closing_speed;

		if (!contact.hit) {
			// Nothing under the wheel at all. The ray found no ground inside its
			// length, so the corner is airborne and the compression is reported
			// at full droop rather than left at whatever it was last tick — a
			// stale compression is what makes a telemetry trace of a jump look
			// like the wheel was still loaded.
			state.compression = -setup.max_droop + state.geometric_offset;
			state.normal_force = 0.0;
			state.grounded = false;
			state.tire_contact = false;
			return;
		}

		// Positive is compressed. `geometric_offset` moves the rest position, so
		// jacking at a corner reads as compression there without the chassis
		// having moved yet.
		state.compression = setup.rest_length + state.geometric_offset - contact.ray_length;

		// Deflection measured from the free length, which is where the spring
		// produces nothing. `max_droop` is the static deflection, so a corner at
		// its rest length is already carrying `spring_rate * max_droop`.
		const double deflection = setup.max_droop + state.compression;
		if (deflection <= 0.0) {
			state.normal_force = 0.0;
			state.grounded = false;
			state.tire_contact = false;
			return;
		}
		// Past this line the spring has travel left, so the tire is on the road.
		// Whether it *carries* anything is decided at the bottom, after the damper
		// has had its say — and the damper can take all of it. See
		// `vehicle_state.h`'s `WheelTelemetry`, which owns the definition.
		state.tire_contact = true;

		double force = setup.spring_rate * deflection;

		// Past the bump stop the rate steps up. Applied to the excess only, so
		// the force is continuous across the limit — a discontinuity here is a
		// bang the driver feels and the integrator has to absorb in one step.
		const double over_travel = state.compression - setup.max_travel;
		if (over_travel > 0.0) {
			force += setup.spring_rate * bump_stop_ratio * over_travel;
		}

		// Split bump and rebound. Rebound is the higher of the two, which is the
		// convention on every real damper and matters more here than usual: it
		// is what stops a wheel that has been unloaded in a corner from pumping
		// itself back down and reloading before the corner is finished, and the
		// inside rear staying off the ground is the entire point of issue #32.
		const double damping = state.velocity > 0.0 ? setup.bump_damping : setup.rebound_damping;
		force += damping * state.velocity;

		// A tire pushes and cannot pull. Clamped here rather than trusted to the
		// tire model's own guard: `tire.h` returns zero force at a negative load,
		// but by the time it does that the number has already been through the
		// chassis force sum, and a negative normal load is a tire that
		// accelerates the kart sideways.
		//
		// Note this also means the damper cannot yank a lightly loaded wheel back
		// down; extension at speed simply reaches zero and stops.
		state.normal_force = force > 0.0 ? force : 0.0;
		state.grounded = state.normal_force > 0.0;
	}

	// How far this corner is off the ground, meters, or zero if it is not.
	//
	// The number issue #32 is judged on is in centimeters, and this is what
	// reports it. Positive means airborne.
	//
	// Exactly `max(0, -deflection)` on the hit path, so it is the same quantity
	// `step` decides `tire_contact` from and the two cannot disagree: a positive
	// lift is a negative deflection is a corner that produced no force.
	//
	// **A miss reports a lower bound, not zero.** The ray was `cast_length` long
	// and found nothing, so the tire is at least that far past its free length
	// clear of the ground. Returning zero — which this did until issue #136 —
	// makes the one unambiguous case, a kart with all four wheels in the air, read
	// as the most ordinary one. The bound is not an estimate and never overstates:
	// it is the largest gap the query is able to rule out, and the true height is
	// anything from that to the top of the jump. A miss is also not the ambiguous
	// case ADR-0033 finding 4 warns about by the time it reaches here — a buried
	// wheel is latched into a *hit* by the Godot boundary, precisely so that this
	// file may read a miss as "there is no ground there".
	double lift_height(const WheelContact &contact, const CornerState &state) const {
		const double free = setup.free_length() + state.geometric_offset;
		if (!contact.hit) {
			const double least = contact.cast_length - free;
			return least > 0.0 ? least : 0.0;
		}
		const double gap = contact.ray_length - free;
		return gap > 0.0 ? gap : 0.0;
	}
};

// Fallback closing speed from two ray lengths, with the noise problem attenuated
// rather than solved.
//
// A caller that has the chassis body's velocity should not use this. It exists
// because a probe or a test harness may genuinely have only a sequence of ray
// lengths, and because the difference between the two paths is worth being able
// to measure rather than assert.
//
// The filter is a one-pole low pass at `cutoff_hz`. 40 Hz is chosen against this
// kart and not by taste: the corner frequencies are 8.5 Hz front and 11.8 Hz
// rear, so 40 Hz leaves the damper's own band untouched — a filter down at the
// suspension frequency would phase-shift the damping force toward being a
// spring, which is worse than the jitter it removed.
struct RaySpeed {
	double cutoff_hz = 40.0;
	double filtered = 0.0;
	double previous_ray_length = 0.0;
	bool primed = false;

	double update(double ray_length, double dt) {
		if (!primed) {
			previous_ray_length = ray_length;
			primed = true;
			filtered = 0.0;
			return 0.0;
		}
		// Shortening ray means compressing, so the sign is the way round it is.
		const double raw = (previous_ray_length - ray_length) / dt;
		previous_ray_length = ray_length;
		const double alpha = 1.0 - std::exp(-2.0 * PI * cutoff_hz * dt);
		filtered += alpha * (raw - filtered);
		return filtered;
	}
};

// The component of a suspension force that acts along the contact normal.
//
// The spring works along the ray, which points down through the chassis. On a
// slope the contact normal is not that direction, and the load the tire actually
// carries is the projection. Kept as a named function because "did anyone take
// the cosine" is exactly the question that never gets asked until a kart on a
// banked curb pulls more grip than one on the flat.
//
// Both arguments must be unit vectors. `ray_direction` points from the mount
// toward the ground.
inline double normal_scale(const Vec3 &normal, const Vec3 &ray_direction) {
	const double alignment = normal.dot(-ray_direction);
	// Floored rather than clamped to zero: a wall-like normal would otherwise
	// silently delete the corner's load, and a wheel against a wall should
	// report a small load and let the collision solver deal with it.
	return alignment > 0.1 ? alignment : 0.1;
}

} // namespace kart::core

#endif // KART_CORE_SUSPENSION_H
