#ifndef KART_CORE_STEERING_H
#define KART_CORE_STEERING_H

#include <cmath>

#include "core/units.h"
#include "core/vec3.h"

// The front-end geometry. ARCHITECTURE.md §6: "Steering — Ackermann geometry
// plus caster jacking", and §6's "What makes a kart a kart", which is the reason
// this file is not a lookup table of steer angles.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017 —
// which is what lets the jacking below be tested against its own limiting cases
// instead of judged by whether the kart looks like it leans.
//
// ## Why jacking is the point
//
// A kart has no differential. Both rear wheels are keyed to one solid axle, so
// in a corner the inside rear is asked to turn at the same rate as the outside
// rear and cannot. Either it scrubs — which is a brake applied to the inside of
// the kart, and the kart pushes straight on — or it comes off the ground. The
// front end is what lifts it. Steering rotates each front wheel about a kingpin
// axis that is neither vertical nor in the plane of the chassis, and a point
// rotated about a tilted axis does not stay at the same height. That vertical
// movement is the jacking, and it is the kart's differential.
//
// The chain, spelled out because it is the thing a reader has to believe:
//
//   1. Steering left rotates both front wheels about their kingpin axes.
//   2. The inside (left) front contact patch moves **down** relative to the
//      chassis and the outside (right) front moves **up** — the caster term is
//      antisymmetric, so the two fronts go opposite ways.
//   3. A contact patch that has moved down relative to the chassis, held on the
//      ground, has raised the chassis at that corner. So the chassis is jacked up
//      at the inside front and pulled down at the outside front.
//   4. On a rigid four-point frame, lengthening one corner loads that corner and
//      its **diagonal opposite**, and unloads the other two. Raising the chassis
//      at the front left therefore unloads the rear left — the inside rear. The
//      short version is that a table with one long leg rocks on the diagonal
//      through the other two; `rigid_inside_rear_lift` in
//      tests/core/test_steering.cpp turns that into the number.
//   5. The inside rear lifts, the locked axle is free to rotate at the outside
//      wheel's speed, and the kart turns.
//
// Steps 2 and 4 are the ones worth pausing on, because getting either backwards
// produces a kart that lifts the *outside* rear, which is not a kart, it is a
// rollover. Both fall out of the derivation below rather than being imposed on
// it, and both then agree with a setup source that describes the same motion in
// words — the outer front tire rising and the inner pressing down, the chassis
// pivoting on the line from the inside front to the outside rear. That source is
// in docs/REFERENCES.md and was read after the fact, which is the only reason it
// counts as a check.
//
// ## The derivation is geometric, not remembered
//
// There is a closed form for steering-induced jacking in the literature. It is
// not used here. The kingpin axis is built in the chassis frame from the caster
// and kingpin-inclination angles, the contact patch is placed relative to it from
// the scrub radius and the wheel radius, and the patch is rotated about that axis
// with `Vec3::rotated` — Rodrigues' formula, which vec3.h carries for exactly
// this. Where the patch ends up *is* the jacking. That is exact at any steer
// angle rather than to first order, and it means the limiting cases are real
// tests rather than restatements of a formula:
//
//   * a perfectly vertical kingpin gives **exactly** zero jacking, at any angle;
//   * caster alone is **antisymmetric** in steer direction, which is what twists
//     the frame and lifts a diagonal;
//   * kingpin inclination alone is **symmetric** — both fronts jack the chassis
//     up whichever way you steer, which is why steering is heavy at a standstill
//     and why the wheel self-centers under the kart's own weight.
//
// ## What actually levers the chassis
//
// Deriving it rather than transcribing it produced one result worth writing down,
// because it contradicts what the setup literature says and the test that found
// it was written expecting the opposite. The antisymmetric part of the jacking —
// the part that twists the frame and lifts a wheel — goes as `sin(rotation)` with
// coefficient
//
//     axis.z * (scrub_radius + wheel_radius * tan(kingpin_inclination))
//
// where `axis.z` is the rearward component of the unit kingpin axis and is
// `sin(caster)` to within the inclination's effect on the normalization. The
// bracket is the **spindle arm**, the lateral distance from the kingpin
// to the wheel center, because the scrub radius is by construction the spindle
// arm less `wheel_radius * tan(inclination)`. The inclination cancels out. What
// levers the chassis is the perpendicular distance from the kingpin axis to the
// wheel, and the scrub radius is only that distance as it appears down at the
// ground.
//
// So caster and the spindle arm set the wheel lift, and kingpin inclination sets
// steering weight, self-centering and camber gain. Karting guidance that says
// more inclination means more jacking is describing the ride-height rise, which
// inclination does cause; the diagonal lift measures within four percent across
// the whole plausible range of the angle. tests/core/test_steering.cpp prints the
// sweep.
//
// ## Sign and coordinate conventions — read this before changing anything
//
// The frame is Godot's, per vec3.h: **+X right, +Y up, -Z forward**, so the
// kart's nose points along -Z and "rearward" is +Z.
//
//     caster              positive rakes the kingpin **top rearward** (toward
//                         +Z). Positive caster puts the tire's contact patch
//                         behind where the kingpin axis meets the ground, which
//                         is what makes the steering self-center.
//     kingpin_inclination positive leans the kingpin **top inboard**, toward the
//                         kart's centerline. For the left wheel that is +X, for
//                         the right wheel -X.
//     scrub_radius        signed, measured at the ground. **Positive** means the
//                         contact patch is *outboard* of the point where the
//                         kingpin axis meets the ground — the usual convention,
//                         and what a kart runs.
//     steer angle         positive turns the wheel **to the left**, which is a
//                         positive rotation about +Y. `test_vec3.cpp` pins the
//                         handedness this rests on.
//     contact_offset.y    **negative means the contact patch moved down relative
//                         to the chassis, which lifts the chassis at that
//                         corner.** A downstream solver reads this sign to decide
//                         which wheel comes off the ground; inverted, it lifts
//                         the outside rear.
//
// ## What is modeled, and what is deliberately not
//
// Modeled: the kingpin axis in three dimensions, Ackermann as a continuous
// fraction between parallel steer and the true geometric solution, the exact
// rotation of the contact patch about the kingpin, and the small difference
// between how far the kingpin turns and how far the wheel actually points.
//
// Not modeled, on purpose:
//
//   * **The linkage.** No tie rod, no steering arm, no pitman. The Ackermann
//     fraction is a property the linkage is assumed to deliver, because the
//     player's input is an angle and nothing in the sim can see the rod. What a
//     real kart adjusts — the inner or outer hole on the stub axle — moves this
//     one number.
//   * **Static camber.** Taken as zero, because a kart's stub axle is machined at
//     the kingpin inclination angle precisely so the wheel stands flat with it
//     (docs/REFERENCES-steering.md, NatSKA guide). Camber *change* with steer
//     falls out of the same rotation and is available from
//     `steer_induced_camber` — it is real, and the outside front gaining negative
//     camber under lock is part of why a kart bites.
//   * **Bump steer and compliance.** The uprights are rigid and the track rods
//     are infinitely stiff. Chassis flex is a separate term and a separate
//     ticket; putting a second flexing element here would make neither testable.
//   * **Caster offset.** The wheel center is taken to lie on the kingpin axis in
//     side view, so the mechanical trail is exactly `wheel_radius * tan(caster)`.
//     A kart's stub axle does have a little rake of its own, which manufacturers
//     do not publish; this is stated rather than guessed.
//
// Every dimension and angle below is sourced in docs/REFERENCES.md. Where a
// number could not be sourced, the comment says so.

namespace kart::core {

// Which corner. The kingpin leans inboard, and "inboard" is a different
// direction on each side of the kart, so almost nothing here is side-agnostic.
enum class Side {
	left,
	right
};

// Fixed geometry of one front corner's steering. All lengths in meters, angles
// in radians, expressed in the chassis frame.
struct SteeringGeometry {
	double caster = 0.0; // positive rakes the kingpin top rearward
	double kingpin_inclination = 0.0; // positive leans the kingpin top inboard
	double scrub_radius = 0.0; // signed, kingpin axis to contact patch at the ground
	double max_lock = 0.0; // radians at the wheel, inner wheel at full input
	double ackermann = 0.0; // 0 = parallel steer, 1 = true Ackermann
	double wheelbase = 0.0;
	double track_front = 0.0; // centerline, not outside width
};

// What one steered wheel does at a given input.
struct SteeredWheel {
	double angle = 0.0; // radians, positive turns the wheel to the left
	// Displacement of the contact patch relative to the chassis, chassis frame.
	// The vertical component is the jacking: with the contact patch held on the
	// ground, a downward displacement here lifts the chassis at that corner, and
	// that is what the suspension solver consumes.
	Vec3 contact_offset;
};

struct SteeringOutput {
	SteeredWheel left;
	SteeredWheel right;
};

// --- the kart's own numbers -------------------------------------------------

// Front tire radius, meters. `tire_front_diameter` in
// tools/blender/kartlib/params.py is 0.280, and this is half of it.
//
// It is a free parameter of the jacking functions rather than a field of
// `SteeringGeometry` because the struct above is an interface other files are
// written against and this file does not get to widen it. It belongs in
// kz_reference.h with the rest of the shared figures; that file has a single
// owner and this is a request, not an edit.
inline constexpr double FRONT_WHEEL_RADIUS = 0.140;

// Kingpin to front hub center, meters — `stub_axle_length` in params.py. This is
// the lateral arm from the pivot to the wheel: it sets the scrub radius once the
// kingpin inclination is chosen, and it is the lever the whole jacking effect
// works through. Along with the caster it is the number the wheel lift is most
// sensitive to, and it is the one number here taken from the kart's own geometry
// rather than from a source, because nobody publishes it.
inline constexpr double FRONT_SPINDLE_OFFSET = 0.090;

// Scrub radius from the parts it is actually made of.
//
// The kingpin axis leans inboard at the top, so between hub height and the
// ground it travels *outboard* by `wheel_radius * tan(inclination)`, eating into
// the spindle's lateral offset. More inclination therefore means less scrub, and
// with enough of it the scrub goes negative — which a badly set up front end
// really does. It is computed rather than typed in as a third free angle,
// because a scrub radius that disagreed with the inclination and the spindle it
// is made of would describe a kart that cannot be built.
//
// Note what this does *not* do: taking the scrub negative this way does not
// reverse the jacking, because the lever is the spindle arm and that is still
// positive. Only a genuinely negative spindle arm — the wheel center inboard of
// the kingpin — turns the jacking round.
inline double scrub_radius_from_spindle(double spindle_offset, double wheel_radius,
		double kingpin_inclination) {
	return spindle_offset - wheel_radius * std::tan(kingpin_inclination);
}

// The KZ front end. Sources, and the spread each number was picked out of, are
// in docs/REFERENCES.md; the short version is below.
inline SteeringGeometry kz_front_geometry() {
	SteeringGeometry geometry;

	// 18 degrees. Racing kart casters sit far beyond a road car's 3-7 degrees:
	// forum measurements of a Kosmic chassis' kingpin carriers give 18.0 and
	// 19.7 degrees, and the range quoted by people who measure them is 15-20.
	// A published go-kart design paper uses 8-14 degrees, but that is a slower
	// vehicle with a different job, and the higher figure is the one that
	// belongs on a KZ. Eccentric kingpin pills give about 3 degrees of total
	// range, half a degree per dot, which is what "one dot of caster" means and
	// is roughly the range a setup screen should expose around this value.
	geometry.caster = 18.0 * PI / 180.0;

	// 11 degrees, the middle of the 10-12 the NatSKA guide gives for a kart with
	// no suspension. A published go-kart steering design uses 14-15. The two
	// disagree and neither is a KZ manufacturer's drawing; 11 is taken because
	// the source that quotes it is the one that also explains what the angle is
	// for. The choice matters less than it looks: it moves the steering weight
	// and the camber gain, and it moves the wheel lift by under four percent
	// across its whole plausible range.
	geometry.kingpin_inclination = 11.0 * PI / 180.0;

	// Derived, and large — 63 mm, against the +/-15 mm a road car runs. A kart
	// steers by levering the whole front end about a pivot well inboard of the
	// tire, and that lever is what makes the jacking worth having: the published
	// setup advice is that moving the front wheels outboard increases the jacking
	// effect on the rear, and moving them outboard is exactly lengthening the
	// spindle arm. The one published number for a go-kart is 93 mm, on a kart
	// with a longer spindle arm and more inclination than this one.
	geometry.scrub_radius = scrub_radius_from_spindle(
			FRONT_SPINDLE_OFFSET, FRONT_WHEEL_RADIUS, geometry.kingpin_inclination);

	// 25 degrees at the inner wheel. **Inherited, not chosen.** It is the figure
	// the bodywork tire-clearance tables in issues #109 and #110 were measured
	// at, and since the inner wheel is the one that turns furthest under
	// Ackermann, holding the *inner* wheel to 25 degrees means no front wheel
	// ever exceeds the angle those clearances were measured at.
	geometry.max_lock = 25.0 * PI / 180.0;

	// True Ackermann. The construction a kart is built to is that lines through
	// the kingpins and the track-rod ends meet at the center of the rear axle,
	// which is the geometric solution exactly. Karts commonly run past it —
	// moving the track rod to the inner hole on the stub axle adds Ackermann and
	// lifts the inside rear harder — so values above 1 are meaningful here and
	// are what a setup screen would expose. No manufacturer publishes the
	// percentage, so 1.0 is the honest default rather than a guessed 1.15.
	geometry.ackermann = 1.0;

	// params.py, `wheelbase`.
	geometry.wheelbase = 1.050;

	// **Centerline**, which is params.py's `track_front` (1.240, outside to
	// outside) less one `tire_front_width` (0.135). Issue #119: the outside
	// figure is one tire wider than the figure the Ackermann relation wants, and
	// using it here would put the theoretical turn center 68 mm off.
	geometry.track_front = 1.105;

	return geometry;
}

// --- the kingpin axis and what rotating about it does ------------------------

// Unit vector along the kingpin axis, pointing **up**, in the chassis frame.
//
// Built so that the two angles are exactly what a person with a gauge would
// measure: the axis projected into the side view leans back by `caster`, and
// projected into the front view leans inboard by `kingpin_inclination`. Building
// it as two successive rotations instead would leave the side-view caster a
// fraction of a degree larger than the number that was typed in, and a
// setup value that does not read back is a small lie that costs an afternoon.
inline Vec3 kingpin_axis(const SteeringGeometry &geometry, Side side) {
	const double inboard = (side == Side::left) ? 1.0 : -1.0;
	return Vec3(inboard * std::tan(geometry.kingpin_inclination),
			1.0,
			std::tan(geometry.caster))
			.normalized();
}

// Where the wheel points after the kingpin has been rotated by `kingpin_rotation`.
//
// These are not the same angle. The wheel's spin axis is horizontal and across
// the kart at rest; rotating it about an axis that is tilted in two planes
// sweeps it out of the horizontal, and the heading you read on the ground is the
// projection of that. At 18 degrees of caster and 11 of inclination the wheel
// ends up about a degree shy of the kingpin's rotation at full lock. Small, but
// it is the difference between the model's steer angle and the tire's slip
// angle, and those must be the same number.
inline double wheel_angle_for_kingpin_rotation(const SteeringGeometry &geometry, Side side,
		double kingpin_rotation) {
	const Vec3 spin_axis = Vec3(1.0, 0.0, 0.0).rotated(kingpin_axis(geometry, side), kingpin_rotation);
	// Heading from the spin axis: at rest the axis is +X and the wheel points at
	// -Z, and a positive (leftward) steer swings the axis toward -Z.
	return std::atan2(-spin_axis.z, spin_axis.x);
}

// Camber of the steered wheel, radians, positive leaning the top **outboard**.
//
// Falls out of the same rotation: the spin axis tilts out of horizontal, and the
// angle it makes with the ground plane is the camber. Not part of the steering
// contract and not consumed by anything yet — it is here because it is free once
// the axis has been rotated, because it is a real effect a kart driver feels,
// and because a tire model that wants it later should not have to re-derive the
// kingpin.
inline double steer_induced_camber(const SteeringGeometry &geometry, Side side,
		double kingpin_rotation) {
	const Vec3 spin_axis = Vec3(1.0, 0.0, 0.0).rotated(kingpin_axis(geometry, side), kingpin_rotation);
	const double outboard = (side == Side::left) ? -1.0 : 1.0;
	// The spin axis points along +X for both wheels; on the left that is inboard,
	// so the sense of "top leaning out" flips with the side.
	return -outboard * std::asin(spin_axis.y);
}

// The kingpin rotation that puts the wheel at `wheel_angle`.
//
// The forward map above has no useful inverse in closed form, so it is inverted
// by bisection over a bracket wider than any lock a kart can reach. Bisection
// rather than Newton because it cannot diverge and because a fixed iteration
// count is the same work on every tick — ARCHITECTURE.md §8 wants the frame's
// cost and its result to be identical between two runs, and an iteration count
// that depends on the input satisfies neither. 60 halvings of a 2.4 radian
// bracket land well inside double precision, so the round trip through this
// function and back is exact to about 1e-15 radians; the test asserts it.
inline double kingpin_rotation_for_wheel_angle(const SteeringGeometry &geometry, Side side,
		double wheel_angle) {
	double low = -1.2;
	double high = 1.2;
	for (int iteration = 0; iteration < 60; ++iteration) {
		const double middle = (low + high) * 0.5;
		if (wheel_angle_for_kingpin_rotation(geometry, side, middle) < wheel_angle) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return (low + high) * 0.5;
}

// --- Ackermann ---------------------------------------------------------------

// The outer wheel's angle, given the inner wheel's. Both magnitudes, both
// non-negative.
//
// True Ackermann is the condition that the perpendiculars to all four wheels
// meet at one point, which for a kart with a solid rear axle means that point
// lies on the rear axle line. Written on the steer angles that is
//
//     cot(outer) - cot(inner) = track / wheelbase
//
// and it is a *geometric* statement, not a ratio of two angles — which is why
// test_steering.cpp asserts the meeting point rather than comparing degrees.
//
// The `ackermann` fraction interpolates the angle between parallel steer and
// that solution, which is what "percentage Ackermann" means everywhere it is
// quoted. Values above 1 are legal and are what a kart run with the track rod in
// the inner hole is doing.
//
// A kart needs far more of this correction than a car does. Its track and its
// wheelbase are nearly equal — 1.105 against 1.050 — where a car's ratio is
// about half that, so `track / wheelbase` is roughly twice as large and the
// inner and outer wheels disagree about twice as much.
inline double ackermann_outer_angle(const SteeringGeometry &geometry, double inner_angle) {
	if (inner_angle <= 0.0 || geometry.wheelbase <= 0.0) {
		return inner_angle;
	}
	const double cot_inner = 1.0 / std::tan(inner_angle);
	const double cot_outer = cot_inner + geometry.track_front / geometry.wheelbase;
	const double true_ackermann = std::atan(1.0 / cot_outer);
	return inner_angle + geometry.ackermann * (true_ackermann - inner_angle);
}

// Radius of the circle the kart's mid-rear-axle point traces, meters, from the
// two steer angles.
//
// Reduced through the bicycle model: each front wheel implies a turn center on
// the rear axle line, at `wheelbase * cot(angle)` from that wheel, and averaging
// the two cotangents puts the center on the kart's centerline. At true Ackermann
// the two agree exactly and the average is the answer; away from it they do not,
// and the average is the honest summary of a front end that is arguing with
// itself.
//
// Returns a large finite number rather than infinity at zero steer, so that a
// caller plotting it does not have to special-case straight ahead.
inline double turn_radius(const SteeringGeometry &geometry, double inner_angle,
		double outer_angle) {
	const double smallest = 1e-6;
	if (std::fabs(inner_angle) < smallest || std::fabs(outer_angle) < smallest) {
		return 1e9;
	}
	const double mean_cotangent =
			(1.0 / std::tan(std::fabs(inner_angle)) + 1.0 / std::tan(std::fabs(outer_angle))) * 0.5;
	return geometry.wheelbase * mean_cotangent;
}

// --- jacking ------------------------------------------------------------------

// Displacement of one contact patch, chassis frame, for a wheel steered to
// `wheel_angle`.
//
// The derivation, in four lines of geometry:
//
//   1. Take the point A where the kingpin axis crosses the ground. Distances are
//      measured from there; a rotation about a *line* does not care which point
//      on it you pick, so this choice costs nothing.
//   2. At rest the contact patch sits `scrub_radius` outboard of A, and
//      `wheel_radius * tan(caster)` behind it — that second term is the
//      mechanical trail, and it appears here without being asked for, which is
//      the sort of thing that says the frame is set up correctly.
//   3. Steering rotates the wheel, and the contact patch with it, about the
//      kingpin axis.
//   4. Where the patch went, minus where it was, is the answer. Its `y` is the
//      jacking.
//
// The patch is treated as a material point of the upright rather than as
// "wherever the tire currently touches". Those differ once the wheel is
// cambered — the real contact point migrates a few millimeters across the tread
// — and the material-point version is the standard treatment and the one the
// limiting cases are clean for.
inline Vec3 contact_offset(const SteeringGeometry &geometry, Side side, double wheel_angle,
		double wheel_radius = FRONT_WHEEL_RADIUS) {
	const double outboard = (side == Side::left) ? -1.0 : 1.0;
	const Vec3 rest(outboard * geometry.scrub_radius,
			0.0,
			wheel_radius * std::tan(geometry.caster));
	const double rotation = kingpin_rotation_for_wheel_angle(geometry, side, wheel_angle);
	return rest.rotated(kingpin_axis(geometry, side), rotation) - rest;
}

// --- the whole front end ------------------------------------------------------

// Both front wheels, from one normalized input.
//
// `input` runs -1 to 1, positive to the left, and is clamped. Clamping rather
// than scaling is what a steering stop does: past full lock the wheels simply
// stop, the output holds its value, and nothing folds back — issue #35 asks for
// no snap at full lock and the sweep in test_steering.cpp measures it.
//
// The wheel that is on the inside of the turn takes `max_lock` at full input and
// the outer takes the Ackermann angle. Which wheel that is swaps at center, and
// that swap is the one place this function could have had a kink in it: it does
// not, because both angles pass through zero together and the Ackermann
// correction vanishes quadratically there — the outer angle's slope against the
// inner is exactly 1 at center. Measured, not assumed.
inline SteeringOutput solve_steering(const SteeringGeometry &geometry, double input,
		double wheel_radius = FRONT_WHEEL_RADIUS) {
	const double clamped = input > 1.0 ? 1.0 : (input < -1.0 ? -1.0 : input);
	const double inner = std::fabs(clamped) * geometry.max_lock;
	const double outer = ackermann_outer_angle(geometry, inner);
	const bool turning_left = clamped >= 0.0;

	SteeringOutput output;
	output.left.angle = turning_left ? inner : -outer;
	output.right.angle = turning_left ? outer : -inner;
	output.left.contact_offset =
			contact_offset(geometry, Side::left, output.left.angle, wheel_radius);
	output.right.contact_offset =
			contact_offset(geometry, Side::right, output.right.angle, wheel_radius);
	return output;
}

// --- rate limit ---------------------------------------------------------------

// Move a steering input toward its target at no more than `max_rate` per second.
//
// Issue #35 asks for a steering rate limit and this is all one is: a kart's
// steering is direct — no rack, no assistance, a short arm on the kingpin — so
// the limit exists to stop a keyboard's step input from teleporting the front
// wheels between frames, not to model a mechanism.
//
// It works in **normalized input per second**, not radians per second, which
// keeps it independent of `max_lock`. The M3a debug vehicle's `STEER_RATE` of
// 3.4 is the same unit — its comment calls it radians per second, but it is
// applied to a -1..1 input, so what it really means is center to full lock in
// 294 ms. Carried over as-is so the feel does not change when the solver does;
// the misdescription is reported against that file rather than repeated here.
//
// Reads `dt` from its caller and never from a clock. ARCHITECTURE.md §8.
inline double rate_limited_steering(double current, double target, double max_rate, double dt) {
	const double step = max_rate * dt;
	const double difference = target - current;
	if (difference > step) {
		return current + step;
	}
	if (difference < -step) {
		return current - step;
	}
	return target;
}

} // namespace kart::core

#endif // KART_CORE_STEERING_H
