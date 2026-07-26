#ifndef KART_CORE_VEHICLE_H
#define KART_CORE_VEHICLE_H

#include "core/chassis.h"
#include "core/chassis_flex.h"
#include "core/drivetrain.h"
#include "core/steering.h"
#include "core/suspension.h"
#include "core/tire.h"
#include "core/units.h"
#include "core/vec3.h"
#include "core/vehicle_state.h"

#include <cmath>

// The vehicle solver. ROADMAP M3b, issues #33 and #41, and the file every other
// M3b header was written to be assembled by.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## What this file is
//
// `chassis.h`, `suspension.h`, `chassis_flex.h`, `steering.h`, `tire.h` and
// `drivetrain.h` each answer one question correctly and none of them touch each
// other. This is the file where they are wired together and where the wiring
// itself becomes the model: the solid rear axle that couples the two rear tires
// through one number, the substep loop that runs the whole assembly at 240 Hz,
// and the slip computation that turns a rigid body's motion into the two numbers
// a tire curve consumes.
//
// It owns exactly four pieces of integrated state — the rear axle's speed, the
// two front wheels' speeds, and the rate-limited steering position — plus
// whatever `Drivetrain` keeps behind its own interface. Everything else is a
// function of `(BodyState, DriverInput, GroundQuery[4])`, which is what makes
// `ARCHITECTURE.md` §8's replay promise hold: two runs fed the same inputs
// produce the same numbers because there is nothing else for them to differ by.
//
// ## The four things that are easy to get wrong, stated before any code
//
// **1. The substeps average, they do not sum.** ADR-0032 conclusion 3 and
// ADR-0033 finding 6, both measured: a 240 Hz solver cannot make the engine step
// twice, so its substeps accumulate and the total is applied once. "Accumulate"
// means the impulse is conserved. Each substep's force acts for `dt/N`, so the
// single force that delivers the same impulse over `dt` is the **mean** of the
// substep forces, not their sum. Summing them applies twice the impulse at two
// substeps and four times at four, and it looks exactly like a solver that needs
// its constants halved.
//
// **And the read-out averages with them.** Issue #134: `fill_telemetry` averaged
// the chassis scalars and left every per-wheel field at whatever the **last**
// substep produced, so `WheelTelemetry` described an instant the kart passed
// through rather than the tick that was applied. Nothing was applied wrongly —
// `step()`'s return value was always the mean — but a caller reconstructing an
// applied force from telemetry got the substep spread as an error, measured here
// at 8.3% mean and 49.7% peak on the normal load and 16.4% mean on the tire force
// in a steady full-lock corner, and found from outside as 3.4% on a pitch rate
// through the Godot boundary. `vehicle_state.h` states the contract at the
// declaration and `tests/core/test_vehicle.cpp` asserts it per corner per tick;
// what belongs here is the arithmetic that makes it exact rather than close: the
// contact normal is held at its tick value across the substeps, so it factors out
// of the mean and `normal * mean(load) + mean(tire force)` **is** the applied
// corner force.
//
// **2. `VehicleForces::application_point` is an offset from the body origin.**
// Not from the center of mass. ADR-0033 measured it after this project believed
// the opposite for a milestone, and the cost was every pitch and roll moment
// exactly doubled — because the kart's mesh origin sits on the ground and so does
// every contact patch, so the spurious second lever arm had the same vertical
// component as the real one.
//
// **3. A rigid body on four springs has no warp mode, and a kart is mostly warp
// mode.** This is the one place the dynamic solver had to add physics rather than
// assemble it. `chassis_flex.h` exists because a body on four stiff contact
// springs is statically indeterminate in warp and a real kart resolves it by
// twisting. A *dynamic* solver looks like it escapes that: six rigid degrees of
// freedom, four contact forces, no indeterminacy, nothing to solve. It does not
// escape it — it silently picks the **torsionally rigid** answer, which
// `chassis_flex.h` measures as lifting a different wheel than a real frame does.
// So the frame's torsion is carried here as one extra internal coordinate, solved
// in closed form each substep, and `warp_amplitude()` below shows the algebra is
// exactly `chassis_flex.h`'s series pair.
//
// **4. The rear wheels are not two wheels that agree.** They are one number.
// `axle_speed_` is integrated once from the sum of both tires' reaction torques,
// and each rear tire's slip ratio is computed from that same number against its
// own road speed. In a corner the inside wheel's road speed is lower, so at a
// shared axle speed it runs a **positive** slip ratio — it is being over-driven —
// and it spends grip on longitudinal force that it no longer has for cornering.
// That is the scrub, and it is why the kart pushes wide when the inside rear
// stays down. Nothing in this file splits torque between the two wheels; the
// split falls out of two Pacejka evaluations at two slip ratios against two
// normal loads, which is what issue #33 means by "split by available grip, not by
// a differential model".
//
// ## The honest limitation: a 120 Hz ground query inside a 240 Hz loop
//
// `step()` is handed one `GroundQuery` per corner per **tick**. The raycasts
// happen on the Godot side at 120 Hz and the solver cannot ask for more of them,
// so substeps 2..N would otherwise be integrating tire and suspension forces
// against geometry from the start of the tick — the exact defect substepping was
// adopted to avoid, reintroduced at the contact.
//
// What is done about it: **the ray length is extrapolated, the contact plane is
// not.** After each substep the mount point's velocity along the ray is known
// exactly (the solver just predicted the body forward, see below), so the ray
// length is advanced by it and the next substep sees a compression that is
// correct to first order. The contact *normal*, the surface type and the grip
// multiplier are held at their tick values.
//
// What that costs, stated rather than waved at: on flat ground the extrapolation
// is exact and the limitation is nil. On a surface whose slope changes under the
// wheel — a curb edge, a kerb-to-grass transition — the normal is up to one tick
// stale, which at 30 m/s is 0.25 m of travel. That is a real error and it is the
// same error a 120 Hz solver would have had; substepping does not make it worse,
// it just does not fix it. The fix is a second raycast per substep, which is a
// Godot-side cost decision and belongs to whoever owns the boundary, not here.
//
// ## Why the solver predicts the body forward at all
//
// Godot owns the integration. The body state handed in is frozen for the whole
// tick, so a naive substep loop would evaluate the same forces N times against
// the same velocities and produce exactly the 120 Hz answer N times over — all of
// the cost of substepping and none of the benefit.
//
// So the solver carries a *predicted* body state across its substeps: velocities
// advanced by the substep's own forces with **symplectic Euler**, because
// ADR-0032 finding 1 measured that to be the scheme the engine itself uses and a
// solver that predicts its own motion has to use the same one or its prediction
// disagrees with the body it is predicting. Positions and the basis are **not**
// advanced — a tick's rotation is under a degree at any yaw rate a kart reaches,
// and re-orthonormalizing a basis twice per tick to chase it would cost more
// determinism risk than it buys accuracy.
//
// ## Cost, against ARCHITECTURE.md §15's 2.0 ms
//
// §15 gives the vehicle sim, all karts, 2.0 ms per frame. That budget cannot be
// checked without an engine, but the two halves of the claim can be checked
// separately and both were.
//
// **The shape.** `step()` allocates nothing, reads no clock, calls no RNG, and
// contains no loop whose count depends on its inputs. Every iteration bound in
// it is a literal — 2 substeps, 4 corners, 2 warp passes — so the cost of a tick
// is the same on every tick, which is what `ARCHITECTURE.md` §8 needs as much as
// §15 does.
//
// **The number.** 200,000 ticks of a loaded steady state, one kart, measured off
// a wall clock outside the solver: **15.4 us per 120 Hz tick**, unchanged between
// -O1 and -O2. That is 0.77% of the budget for one kart.
//
// And **91% of it is `steering.h`**. `solve_steering` costs 7.05 us on its own
// and is called once per substep; the whole rest of the solver — four
// suspensions, four tires, the warp solve, the drivetrain, the axle and the body
// prediction — is 1.3 us. The cause is that `steering.h` inverts its own forward
// map by a fixed 60-step bisection and then calls it again inside
// `contact_offset`, three nested 60-step loops per call. That is a deliberate
// determinism choice in that file and it is the right one; it is also the entire
// cost of this solver, and a memo of the last input would remove it. Reported
// rather than worked around, because `steering.h` is not this file's to change.

namespace kart::core {

// Reference speed for the slip denominators, m/s.
//
// Slip ratio divides by road speed and slip angle divides by forward speed, and
// at standstill both are zero. Flooring the denominator is the standard fix and
// the floor is a real choice: too low and the ratio explodes into a force
// discontinuity at walking pace, too high and the tire goes soft at speeds a kart
// actually drives at.
//
// 1.0 m/s is chosen against the stiffness this vehicle actually has rather than
// by taste. The lateral cornering stiffness is `mu * N * C * B` = 2.10 * 500 *
// 1.55 * 9 = 14.6 kN per radian, and the velocity clamp below takes over from the
// curve wherever `C_alpha / u` exceeds the clamp's own slope — which on this kart
// is **everywhere below 2.8 m/s**. Putting the floor at 1.0 m/s therefore keeps
// it strictly inside the region the clamp already governs: the floor never has to
// be the thing that prevents a divergence, and the two never fight over the same
// range. Above 10 km/h neither is active at small slip and the tire is exactly
// what `tire.h` says it is.
inline constexpr double SLIP_REFERENCE_SPEED = 1.0;

// The most of a slip velocity that one substep's tire force may remove.
//
// The low-speed guard in `KartVehicle` limits each tire force to the value that
// would bring its own slip velocity to zero within the substep. That bound alone
// is **deadbeat** — it removes exactly the velocity that was there — and deadbeat
// is not stable, it is marginal, which is not a subtlety: it was measured. With
// the fraction at 1.0 a kart standing still on flat ground settles into a limit
// cycle of +/-200 N of lateral force per tire at 19 mm/s of contact-patch creep,
// alternating sign every substep, and it rolls the chassis far enough to put
// 775 N on one rear tire and 38 N on the other while the four forces still sum
// correctly to the kart's weight.
//
// The reason deadbeat is not enough is that each tire's clamp is computed as if
// its own force were the only one acting: four corners each removing all of their
// own slip velocity, through one rigid body that also rotates, overshoot. Half is
// the value used, which turns the bound into a geometric decay with a 1.4 ms time
// constant at 240 Hz — fast enough to be invisible, and unconditionally stable
// for any number of corners.
inline constexpr double SLIP_CONVERGENCE_FRACTION = 0.5;

// Air density at sea level, kg/m^3, and the kart's drag area.
//
// `drag_area` is Cd*A for a kart with a seated driver — the driver's torso and
// helmet are most of both terms, which is why a kart's figure is so much larger
// than its frontal area suggests. 0.7 m^2 is the value `tests/core/test_drivetrain.cpp`
// measured its 143.2 km/h against, and it is kept identical here so that the two
// top-speed figures differ only by the tire model and the load transfer, which is
// the comparison that is worth making.
inline constexpr double AIR_DENSITY = 1.2;

// Rotational inertia of one wheel, kg m^2.
//
// **Derived from `chassis.h`'s mass table, not sourced.** A tire and a 5-inch rim
// carry nearly all of their mass at the outside, so the ring fraction is well
// above a solid disc's 0.5 and below a thin hoop's 1.0; 0.55 is used, which puts
// a 2.6 kg front wheel at 0.028 kg m^2 and a 3.6 kg rear at 0.043.
//
// `chassis.h` gives each wheel a box extent for the *chassis* inertia tensor,
// where the wheel is a lump at the end of a lever and its own inertia barely
// matters. Here it is the whole quantity, so the box approximation is dropped in
// favor of the ring one and the difference is stated: the box gives 0.034 for the
// front wheel against 0.028 here, 21% apart, and neither is measured.
inline constexpr double WHEEL_RING_FRACTION = 0.55;

class KartVehicle {
public:
	// --- fixed solver configuration ------------------------------------------

	// Substeps per 120 Hz tick. **Fixed, and never derived from frame time.**
	// Issue #41 is explicit that a variable count breaks determinism, and it is
	// right: a solver whose substep count depends on how long the last frame took
	// is a solver whose output depends on the frame rate, which is exactly what
	// `ARCHITECTURE.md` §8 item 2 forbids. Two is the count that takes the
	// project's 120 Hz `_physics_process` to the 240 Hz §6 calls not tunable-down.
	static constexpr int SUBSTEP_COUNT = 2;

	// Number of grounded-set passes in the warp solve. Fixed, for the same reason
	// `steering.h` bisects a fixed 60 times: a loop whose count depends on its
	// input costs a different amount of time on two runs and can return a
	// different answer on two machines. Two passes is enough to place a corner
	// that the frame's own twist lifted, and a third pass has never changed the
	// answer in the scenarios in `tests/core/test_vehicle.cpp`.
	static constexpr int WARP_PASSES = 2;

	// --- tuning, public so a test or the §19 telemetry UI can move one ---------

	// Aerodynamic drag area, Cd*A in m^2, acting at the center of mass.
	// `ARCHITECTURE.md` §6 asks for light drag and negligible downforce, and
	// downforce is genuinely negligible on a kart: the bodywork is a nose cone and
	// two sidepods, and CIK rules forbid anything that would work.
	double drag_area = 0.7;

	// Rolling resistance coefficient, as a fraction of normal load. Applied as a
	// **torque** on the wheel rather than as a force at the patch, because that is
	// what it physically is — the pressure distribution in a rolling contact patch
	// is biased forward, and the resulting couple resists rotation. Entering it as
	// a torque means it reaches the road through the tire model and the slip
	// ratio, so a locked wheel does not mysteriously keep paying it.
	double rolling_resistance = 0.012;

	// Peak brake torque, N m, per front wheel and for the rear axle as a whole.
	//
	// **These are front-biased and `ARCHITECTURE.md` §6 says "rear-biased". The
	// disagreement is deliberate and is reported rather than hidden.** At the
	// contact patch the pair below is 2636 N at the front axle (180 N m on a
	// 0.1366 m loaded radius, twice) and 1098 N at the rear (160 N m on 0.1457 m),
	// a 71/29 split.
	//
	// The ideal split — the one that brings all four tires to their limit together
	// — is set by the load distribution *during* braking, and on this kart at
	// 1.8 g that is 80/20: the center of mass is 0.2197 m up on a 1.050 m
	// wheelbase, so 1.8 g moves 38% of the kart's weight onto the front axle. So
	// 71/29 **is** rear-biased, by nine points, relative to the split that would be
	// optimal, and the consequence is the readable one §6 asked for: the rear
	// reaches its limit first and the kart slides its tail rather than ploughing on
	// locked fronts. `tests/core/test_vehicle.cpp` measures exactly that — the
	// front locks from 0.6 pedal and the rear only joins it at 1.0.
	//
	// What §6 cannot mean is a split with more than half the braking at the rear.
	// A kart under 1.5 g carries about 27% of its weight on the rear axle, so a
	// rear-majority split cannot exceed roughly 0.8 g however good the tires are,
	// and §6.4 asks for 1.5-2.0 g. This is also why a KZ has front brakes at all
	// when so many kart classes do not.
	//
	// The magnitudes are sized to make locking *possible* rather than automatic:
	// 1318 N per front wheel against a peak tire force of roughly 1380 N at the
	// load a front tire carries under heavy braking, so the pedal reaches the limit
	// at about 0.9 and goes past it at 1.0.
	double brake_torque_front = 180.0;
	double brake_torque_rear = 160.0;

	// Steering rate limit, in normalized input per second. Carried over from the
	// M3a debug vehicle unchanged so the feel does not change when the solver does
	// — see the note in `steering.h`'s `rate_limited_steering`, which also records
	// that the M3a file misdescribes this unit as radians per second. 3.4 is center
	// to full lock in 294 ms.
	double steer_rate = 3.4;

	// Frame torsional stiffness, N m per degree. Exposed because
	// `chassis_flex.h` records that two published figures for this quantity
	// disagree by a factor of five to eighteen, that they are not the same
	// measurement, and that the crossover between "the inside front lifts" and
	// "the inside rear lifts" sits inside the range. A constant that consequential
	// should be reachable from a test.
	double frame_torsion_nm_per_deg = FRAME_TORSION_NM_PER_DEG;

	// Caster jacking on or off.
	//
	// This is not a tuning knob, it is the instrument issue #33's second
	// acceptance criterion is measured with: "with wheel lift disabled, the kart
	// audibly and visibly scrubs and pushes wide". Setting it false zeroes the
	// front geometric offsets and leaves everything else — the locked axle, the
	// tires, the frame torsion — exactly as it was, so the difference between the
	// two runs is the jacking and nothing else.
	bool jacking_enabled = true;

	// World gravity, used **only** to predict the body forward between substeps.
	// The solver never applies it; Godot does. It is here because a prediction
	// that ignores gravity is wrong by 9.81 m/s^2 in the one direction the
	// suspension is most sensitive to, and because an airborne kart would
	// otherwise be predicted to hang.
	Vec3 gravity = Vec3(0.0, -G, 0.0);

	// The drivetrain, public so a scenario can turn the assists off (issue #40) or
	// change the sprockets (`gearbox.h` calls that the one thing a track author is
	// expected to change).
	Drivetrain drivetrain;

	// --- the interface --------------------------------------------------------

	// Build the corner setups, the mass properties and the steering geometry.
	// Cheap, and idempotent, so a test that moves `frame_torsion_nm_per_deg` can
	// simply call it again.
	void configure() {
		mass_properties_ = kz::kart_mass_properties();

		geometry_.mass = mass_properties_.mass;
		// The center-of-mass height comes from the twenty-one lump table rather
		// than from `ChassisGeometry`'s own default of 0.23, which its comment
		// says was derived from two masses. The same reconciliation
		// `tests/core/test_kart_lift.cpp` makes, for the same reason: every
		// threshold in this solver divides by it.
		geometry_.com_height = mass_properties_.center_of_mass.y;
		geometry_.front_mass_share = kz::STATIC_FRONT_SHARE;
		geometry_.track_front = kz::FRONT_HALF_TRACK * 2.0;
		geometry_.track_rear = kz::REAR_HALF_TRACK * 2.0;
		geometry_.wheelbase = kz::REAR_AXLE_Z - kz::FRONT_AXLE_Z;

		steering_ = kz_front_geometry();

		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			suspension_[corner].setup = corner_setup(geometry_, corner);
		}

		// One tire model for all four corners. The front and rear slicks are
		// genuinely different parts — 135 mm against 215 mm of section — and
		// `tire.h` anchors its curve at a 500 N nominal load, which is the load a
		// *rear* tire carries. Giving the front its own nominal load is the
		// obvious refinement and it is deliberately not made here: `tire.h` is
		// another agent's file, the change would move the front/rear balance, and
		// a balance change smuggled in as a configuration detail is exactly the
		// unbounded tuning `ARCHITECTURE.md` §19 names as a risk. It is reported
		// instead.
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			tire_[corner] = Tire();
		}

		wheel_inertia_[CORNER_FL] = WHEEL_RING_FRACTION * FRONT_WHEEL_MASS *
				kz::FRONT_WHEEL_RADIUS * kz::FRONT_WHEEL_RADIUS;
		wheel_inertia_[CORNER_FR] = wheel_inertia_[CORNER_FL];
		const double rear_wheel = WHEEL_RING_FRACTION * REAR_WHEEL_MASS *
				kz::REAR_WHEEL_RADIUS * kz::REAR_WHEEL_RADIUS;
		// The rear axle is one body: two wheels, the 50 mm bar, the sprocket and
		// the brake disc. The bar contributes almost nothing (9 kg at a 25 mm
		// radius is 0.003 kg m^2) and the sprocket and disc are the rest of the
		// 0.010 allowed for them.
		axle_inertia_ = 2.0 * rear_wheel + 0.010;
		wheel_inertia_[CORNER_RL] = axle_inertia_;
		wheel_inertia_[CORNER_RR] = axle_inertia_;

		configured_ = true;
	}

	// One 120 Hz tick. Substeps internally to 240 Hz and returns the single set of
	// forces that delivers the same impulse — see note 1 in the header comment.
	//
	// No allocation, no clock, no randomness, and every loop bound is a literal.
	VehicleForces step(const BodyState &body, const DriverInput &input,
			const GroundQuery contacts[CORNER_COUNT], double dt) {
		if (!configured_) {
			configure();
		}

		VehicleForces total;
		if (!(dt > 0.0) || SUBSTEP_COUNT <= 0) {
			return total;
		}

		const double h = dt / static_cast<double>(SUBSTEP_COUNT);

		// The tick's contact geometry. The ray length is the only part that is
		// extrapolated across substeps; see the header comment on why the normal
		// is not.
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			contact_[corner].hit = contacts[corner].hit;
			contact_[corner].ray_length = contacts[corner].distance;
			// How far the query looked, which is the only thing a miss reports.
			// `suspension.h`'s `lift_height` turns it into "the tire is at least
			// this far clear" instead of the zero it used to return for a kart in
			// mid-air — issue #136. Filled here rather than left to the boundary
			// because this file owns `ray_length(corner)` and a second copy of it
			// arriving through `GroundQuery` is a second copy that could disagree.
			contact_[corner].cast_length = ray_length(corner);
			contact_[corner].surface_grip = contacts[corner].surface_grip;
			// A missed ray still needs *a* normal for the frames below. Nothing
			// is applied at a corner that missed, so the value only has to be
			// finite and unit; the chassis up axis is the least surprising
			// choice. Latching the last valid normal is the boundary's job —
			// ADR-0033 finding 4, and `vehicle_state.h` says so at `GroundQuery`.
			contact_[corner].normal = contacts[corner].hit
					? contacts[corner].normal
					: body.basis_y;
		}

		// The tick's per-wheel read-out starts empty and is accumulated across the
		// substeps exactly as the forces are. Zeroed here rather than in
		// `fill_telemetry` so that the accumulator and the forces it has to agree
		// with are cleared in the same place and cannot fall out of step.
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			wheel_sum_[corner] = WheelTelemetry();
		}

		// The predicted body state, advanced by each substep's own forces.
		BodyState predicted = body;

		for (int substep = 0; substep < SUBSTEP_COUNT; ++substep) {
			// Shift requests are edges — `vehicle_state.h` says so — so they are
			// delivered on the first substep only. Passing them to every substep
			// would be harmless today, because `Gearbox::step` drops a request
			// arriving mid-shift, and it would be a bug the moment the shift time
			// went below a substep.
			const bool first = substep == 0;
			solve_substep(predicted, input, first, h, accumulator_);
			accumulate_wheel_telemetry();

			for (int corner = 0; corner < CORNER_COUNT; ++corner) {
				total.force[corner] += accumulator_.force[corner];
				total.application_point[corner] += accumulator_.point[corner];
			}
			total.central_force += accumulator_.central_force;
			total.central_torque += accumulator_.central_torque;

			predict_forward(predicted, accumulator_, body.origin, h);
			extrapolate_rays(predicted, h);
		}

		// The mean, not the sum. Each substep's force acted for `dt / N`, so the
		// single force delivering the same impulse over `dt` is the average. Note
		// 1 in the header comment, ADR-0032 conclusion 3.
		const double inverse = 1.0 / static_cast<double>(SUBSTEP_COUNT);
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			total.force[corner] *= inverse;
			// The application point is a position, not an impulse, so averaging
			// it is averaging the patch's location across the tick. That is the
			// point at which the mean force acted, to first order, and it moves by
			// under a millimeter across a tick.
			total.application_point[corner] *= inverse;
		}
		total.central_force *= inverse;
		total.central_torque *= inverse;

		fill_telemetry(body, total);
		return total;
	}

	const VehicleTelemetry &telemetry() const { return telemetry_; }

	// Where the suspension ray starts, in the **chassis** frame: the wheel center,
	// at the unloaded tire radius above the body origin.
	//
	// `chassis.h` puts the body origin on the ground, laterally centered, midway
	// between the axles, which is where `params.py` puts the exported mesh's
	// origin. So a hub at `free_radius` above it is exactly a wheel resting on
	// flat ground with no load in the tire, and `suspension.h`'s `free_length()`
	// — the ray length at which the spring produces nothing — comes out equal to
	// that radius without either file having been told about the other.
	Vec3 ray_origin(int corner) const {
		const bool front = corner == CORNER_FL || corner == CORNER_FR;
		const double half_track = front ? kz::FRONT_HALF_TRACK : kz::REAR_HALF_TRACK;
		const double side = (corner == CORNER_FL || corner == CORNER_RL) ? -1.0 : 1.0;
		return Vec3(side * half_track, wheel_radius(corner),
				front ? kz::FRONT_AXLE_Z : kz::REAR_AXLE_Z);
	}

	// How long to cast, meters.
	//
	// Long enough to reach the ground at full droop plus a margin, because a ray
	// that stops at the free length reports "no hit" the instant the wheel leaves
	// and `suspension.h`'s `lift_height` then has nothing to measure. The margin
	// is 100 mm, which is the range issue #32 wants the inside-rear lift reported
	// over — it is judged in centimeters. Beyond it the corner is airborne and the
	// exact number stops mattering.
	//
	// It is also the number a miss reports, which is the second thing this
	// constant now decides: past the margin `lift_height` has no measurement, so it
	// hands back the margin itself as the lower bound rather than the zero that
	// made a kart in flight read as a kart on the road. Issue #136. Raising the
	// margin therefore costs one more raycast meter and buys a longer honest lift
	// range; there is no third effect to check.
	double ray_length(int corner) const { return wheel_radius(corner) + RAY_MARGIN; }

	// The **unloaded** tire radius, which is what a wheel mesh is that big and
	// what the ray geometry above is built on. The solver's own slip arithmetic
	// uses the loaded rolling radius instead — see `rolling_radius`, and see
	// `chassis_flex.h`'s `corner_setup`, where `rest_length` is defined as exactly
	// that. The two differ by the static tire deflection: 3.4 mm front, 1.8 mm
	// rear.
	double wheel_radius(int corner) const {
		return (corner == CORNER_FL || corner == CORNER_FR) ? kz::FRONT_WHEEL_RADIUS
															: kz::REAR_WHEEL_RADIUS;
	}

	// The loaded rolling radius, meters. `omega * rolling_radius` is the wheel's
	// surface speed and it is the numerator of every slip ratio here.
	double rolling_radius(int corner) const { return suspension_[corner].setup.rest_length; }

	// Back to the grid.
	void reset() {
		axle_speed_ = 0.0;
		front_speed_[0] = 0.0;
		front_speed_[1] = 0.0;
		steer_position_ = 0.0;
		warp_ = 0.0;
		drivetrain.reset();
		telemetry_ = VehicleTelemetry();
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			contact_[corner] = WheelContact();
			corner_state_[corner] = CornerState();
		}
	}

	// Put the kart in a gear at a road speed with the driveline already turning,
	// the way it would be if it had driven there. The braking and skidpad
	// scenarios both start mid-lap rather than on the grid, and a respawn does
	// too — `drivetrain.h`'s `engage` exists for the same reason and this is the
	// vehicle-level version that also spins the wheels up.
	void engage(int gear, double road_speed_ms) {
		if (!configured_) {
			configure();
		}
		axle_speed_ = road_speed_ms / rolling_radius(CORNER_RL);
		front_speed_[0] = road_speed_ms / rolling_radius(CORNER_FL);
		front_speed_[1] = road_speed_ms / rolling_radius(CORNER_FR);
		drivetrain.engage(gear, axle_speed_);
	}

	// --- read-only state, for tests and telemetry -----------------------------

	// The solid rear axle. One number, by construction — issue #33's first
	// acceptance criterion is that the two rear wheel speeds are identical at all
	// times, and there is only one variable for them to be identical in.
	double axle_speed() const { return axle_speed_; }
	double front_wheel_speed(int corner) const {
		return front_speed_[corner == CORNER_FR ? 1 : 0];
	}
	double steer_position() const { return steer_position_; }

	// The frame's internal warp amplitude, meters, positive twisting FL and RR up
	// relative to FR and RL. Same sign convention as
	// `chassis_flex.h`'s `CornerLoads::frame_warp`.
	double warp_amplitude() const { return warp_; }

	// The tire model at one corner, after `configure()` has built it.
	//
	// Writable, because `ARCHITECTURE.md` §19 names unbounded vehicle tuning as a
	// risk and names telemetry as the defense — and a tuning UI that can show a
	// curve but not move it is not a defense. It is also the only way a test can
	// answer "what would `peak_friction` have to be for §6.4's lateral band to be
	// reachable", which is a question about the tire that only the whole vehicle
	// can measure.
	Tire &tire(int corner) { return tire_[corner]; }
	const Tire &tire(int corner) const { return tire_[corner]; }

	// The front steering geometry, after `configure()` has built it.
	//
	// Writable for the same reason `tire()` is, and with one extra caution that
	// the tire accessor does not need: **`configure()` rebuilds this wholesale**
	// from `kz_front_geometry()`, so anything written here is discarded by a
	// later `configure()`. Nothing calls that after `_ready`, and the lazy guards
	// in `step()` and `engage()` only fire when it has never run, but a caller
	// that adds one has to re-apply its tuning afterwards.
	//
	// Nothing in `SteeringGeometry` is a cache: `solve_steering` reads `max_lock`
	// and the rest directly on every call, so a value written here takes effect
	// on the next substep with no refresh — unlike `Tire`, which caches where its
	// curves peak and says so.
	SteeringGeometry &steering() { return steering_; }
	const SteeringGeometry &steering() const { return steering_; }

	const MassProperties &mass_properties() const { return mass_properties_; }
	const ChassisGeometry &chassis_geometry() const { return geometry_; }
	const CornerSetup &corner_setup_for(int corner) const { return suspension_[corner].setup; }

private:
	// Ray margin past the free length. See `ray_length`.
	static constexpr double RAY_MARGIN = 0.10;

	// Wheel masses, from `chassis.h`'s table. Restated as named constants rather
	// than indexed out of `KART_LUMPS`, because indexing a table by position is a
	// silent breakage the moment a lump is inserted.
	static constexpr double FRONT_WHEEL_MASS = 2.6;
	static constexpr double REAR_WHEEL_MASS = 3.6;

	// What one substep produces. Kept as a member rather than a local so that
	// `step()` allocates nothing and so the sizes are visible in one place.
	struct SubstepForces {
		Vec3 force[CORNER_COUNT];
		Vec3 point[CORNER_COUNT];
		Vec3 central_force;
		Vec3 central_torque;

		void clear() {
			for (int corner = 0; corner < CORNER_COUNT; ++corner) {
				force[corner] = Vec3();
				point[corner] = Vec3();
			}
			central_force = Vec3();
			central_torque = Vec3();
		}
	};

	// Chassis-frame vector to world.
	static Vec3 to_world(const BodyState &body, const Vec3 &local) {
		return body.basis_x * local.x + body.basis_y * local.y + body.basis_z * local.z;
	}

	static double clamp01(double value) {
		return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
	}

	static double clamp_signed(double value, double limit) {
		if (value > limit) {
			return limit;
		}
		return value < -limit ? -limit : value;
	}

	// --- one 240 Hz substep ---------------------------------------------------

	void solve_substep(const BodyState &body, const DriverInput &input, bool deliver_shift,
			double h, SubstepForces &out) {
		out.clear();

		// 1. Steering, rate limited. In normalized input per second, so the limit
		//    is independent of `max_lock` — see `steering.h`.
		const double target = input.steer > 1.0 ? 1.0 : (input.steer < -1.0 ? -1.0 : input.steer);
		steer_position_ = rate_limited_steering(steer_position_, target, steer_rate, h);
		const SteeringOutput steered = solve_steering(steering_, steer_position_);

		double steer_angle[CORNER_COUNT] = { steered.left.angle, steered.right.angle, 0.0, 0.0 };

		// The jacking adapter, and **the only place the sign lives**. The two
		// files use opposite conventions for good reasons of their own:
		// `steering.h` says a negative `contact_offset.y` means the patch moved
		// down, which lifts the chassis at that corner; `suspension.h` says a
		// positive `geometric_offset` lifts the chassis. So this is a negation,
		// and without it the kart lifts its *outside* rear, which is not a kart
		// understeering, it is a kart on its roof.
		//
		// `tests/core/test_kart_lift.cpp` asserts exactly this negation against
		// the quasi-static solver. If that test and this line ever disagree, one
		// of the two files was edited without the other.
		double jack[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
		if (jacking_enabled) {
			jack[CORNER_FL] = -steered.left.contact_offset.y;
			jack[CORNER_FR] = -steered.right.contact_offset.y;
			// The rear wheels do not steer, so nothing jacks them directly. The
			// rear rises because the frame twists, which is what the warp
			// coordinate below solves for.
		}

		// 2. Contact frames and the mount kinematics.
		const Vec3 down = -body.basis_y;
		Vec3 mount[CORNER_COUNT];
		Vec3 patch[CORNER_COUNT];
		double closing[CORNER_COUNT];
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			mount[corner] = body.origin + to_world(body, ray_origin(corner));
			patch[corner] = mount[corner] + down * contact_[corner].ray_length;
			// The closing speed is taken from the chassis body's own velocity and
			// never differenced from two ray lengths. `suspension.h`'s header has
			// the measurement: differencing a raycast at 240 Hz turns 0.01 mm of
			// ray noise into 20 N of phantom damping force, four times what the
			// velocity path produces from the same noise.
			closing[corner] = body.velocity_at(mount[corner]).dot(down);
		}

		// 3. The frame's warp coordinate. See note 3 in the header comment.
		solve_warp(jack);

		// 4. The four corners.
		double normal_load[CORNER_COUNT];
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			corner_state_[corner].geometric_offset = jack[corner] + warp_ * MODE_WARP[corner];
			suspension_[corner].step(contact_[corner], closing[corner], corner_state_[corner]);
			// The spring works along the ray; the ground can only push along the
			// contact normal, and the load the tire carries is the projection.
			normal_load[corner] = corner_state_[corner].normal_force *
					normal_scale(contact_[corner].normal, down);
		}

		// 5. The drivetrain, once per substep, told the axle speed it must answer
		//    against. It does not own the axle — the tires do, and only this file
		//    knows what they are doing.
		DrivetrainInput drive_input;
		drive_input.throttle = clamp01(input.throttle);
		drive_input.clutch = clamp01(input.clutch);
		drive_input.shift_up = deliver_shift && input.shift_up;
		drive_input.shift_down = deliver_shift && input.shift_down;
		drive_input.axle_speed = axle_speed_;
		drive_input.dt = h;
		const DrivetrainOutput drive = drivetrain.step(drive_input);

		// 6. The tires.
		const double brake = clamp01(input.brake);
		double tire_reaction_rear = 0.0;
		double rear_rolling_torque = 0.0;

		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			WheelTelemetry &wheel = telemetry_.wheel[corner];
			wheel.normal_load = normal_load[corner];
			wheel.steer_angle = steer_angle[corner];
			wheel.suspension_travel = corner_state_[corner].compression;
			wheel.grounded = corner_state_[corner].grounded && contact_[corner].hit;
			// Carrying load, touching, and how far clear — three questions, and
			// `vehicle_state.h` is where they are defined. The only implication
			// between them is `grounded => tire_contact`, which holds here because
			// `suspension.h` cannot set `normal_force > 0` without deflection left.
			wheel.tire_contact = corner_state_[corner].tire_contact;
			wheel.lift = suspension_[corner].lift_height(contact_[corner], corner_state_[corner]);

			const double radius = rolling_radius(corner);
			const bool rear = corner == CORNER_RL || corner == CORNER_RR;
			const double spin = rear ? axle_speed_ : front_speed_[corner == CORNER_FR ? 1 : 0];

			// The wheel's rolling direction and its axle direction, both in the
			// contact plane. `-Z` is forward (vec3.h), a positive steer angle
			// turns the wheel to the left, and `roll x normal` comes out as the
			// kart's right — checked rather than assumed, because a lateral force
			// with the wrong sign is a kart that steers backwards.
			const Vec3 heading = to_world(body,
					Vec3(-std::sin(steer_angle[corner]), 0.0, -std::cos(steer_angle[corner])));
			const Vec3 roll_dir =
					heading.reject_from_unit(contact_[corner].normal).normalized();
			if (roll_dir.length_squared() <= 0.0) {
				wheel.slip_angle = 0.0;
				wheel.slip_ratio = 0.0;
				wheel.utilization = 0.0;
				wheel.force = Vec3();
				continue;
			}
			const Vec3 right_dir = roll_dir.cross(contact_[corner].normal);

			const Vec3 patch_velocity = body.velocity_at(patch[corner]);
			const double forward_speed = patch_velocity.dot(roll_dir);
			const double lateral_speed = patch_velocity.dot(right_dir);
			const double reference = std::fabs(forward_speed) > SLIP_REFERENCE_SPEED
					? std::fabs(forward_speed)
					: SLIP_REFERENCE_SPEED;

			TireSlip slip;
			// Negated so that a tire sliding to its right produces a force to its
			// left. The magnitude in the denominator, not the signed speed, so
			// that a kart rolling backwards still generates a lateral force that
			// opposes the slide rather than one that reinforces it.
			slip.slip_angle = std::atan2(-lateral_speed, reference);
			slip.slip_ratio = (spin * radius - forward_speed) / reference;
			slip.normal_load = normal_load[corner];
			slip.surface_grip = contact_[corner].surface_grip;

			TireForce force = tire_[corner].evaluate(slip);

			// The low-speed guard, and issue #34's last open acceptance item:
			// "no force instability at any speed, including near zero".
			//
			// The floor on the slip denominator is not enough on its own. What
			// makes a tire model explode near standstill is that its force is
			// stiff against a velocity that the same force is about to reverse:
			// at 1 m/s the lateral force responds to slip with a time constant of
			// 3 ms against a 4.2 ms substep, and the wheel-spin loop is worse
			// still — the reaction torque's effect on the slip ratio has a time
			// constant of `I*u / (r^2 * dF/dslip)`, which at walking pace is two
			// orders of magnitude below the substep.
			//
			// So each component is clamped to the force that would bring its own
			// slip velocity exactly to zero within the substep, and no further.
			// That is unconditionally stable — a force that cannot overshoot
			// cannot oscillate — and it is the same argument `clutch.h` makes for
			// its convergence clamp, which is not a coincidence: both are a stiff
			// contact integrated explicitly at a fixed step.
			//
			// It costs nothing where it does not bind, and it binds only below
			// about 1.4 m/s of slip velocity. In a steady 2 g corner the lateral
			// slip velocity is around 2 m/s and the cap is twenty times the tire's
			// own peak.
			const double corner_mass = mass_properties_.mass * 0.25;
			const double lateral_cap =
					SLIP_CONVERGENCE_FRACTION * corner_mass * std::fabs(lateral_speed) / h;
			if (std::fabs(force.lateral) > lateral_cap) {
				force.lateral = force.lateral > 0.0 ? lateral_cap : -lateral_cap;
			}

			// The rotational half of the same clamp. `wheel_inertia_` for a rear
			// corner is the **whole** axle's, not half of it: a force at one rear
			// tire accelerates the entire axle, which is what makes them one
			// degree of freedom.
			const double rotational = wheel_inertia_[corner] +
					(rear ? drive.reflected_inertia : 0.0);
			const double slip_speed = spin * radius - forward_speed;
			const double inverse_effective =
					radius * radius / rotational + 1.0 / corner_mass;
			const double longitudinal_cap =
					SLIP_CONVERGENCE_FRACTION * std::fabs(slip_speed) / (h * inverse_effective);
			if (std::fabs(force.longitudinal) > longitudinal_cap) {
				force.longitudinal =
						force.longitudinal > 0.0 ? longitudinal_cap : -longitudinal_cap;
			}

			const Vec3 tire_force = roll_dir * force.longitudinal + right_dir * force.lateral;
			const Vec3 corner_force = contact_[corner].normal * normal_load[corner] + tire_force;

			out.force[corner] = corner_force;
			// **Offset from the body ORIGIN, in world coordinates.** ADR-0033,
			// and note 2 in the header comment. Passing a center-of-mass-relative
			// vector here doubles every pitch and roll moment.
			out.point[corner] = patch[corner] - body.origin;

			wheel.slip_angle = slip.slip_angle;
			wheel.slip_ratio = slip.slip_ratio;
			// `vehicle_state.h` defines this as "fraction of the friction ellipse
			// in use", which is `TireForce::utilization` and not
			// `TireForce::slide`. `tire.h` measured that the two are different
			// questions and that only `slide` is monotone in slip — a locked wheel
			// reports 0.612 utilization and rising slide. The §12 scrub audio and
			// the telemetry panel want `slide`, and there is no field for it here.
			// Reported rather than smuggled into this one, whose name would then
			// be the second wrong name for the same quantity.
			wheel.utilization = force.utilization;
			wheel.force = tire_force;

			// Rolling resistance, as a torque. Tapered to nothing across the last
			// 1 rad/s so that a stopped wheel does not chatter between two signs
			// of a force that should not exist at all when it is not rolling.
			const double taper = std::fabs(spin) < 1.0 ? std::fabs(spin) : 1.0;
			const double rolling_torque = rolling_resistance * normal_load[corner] * radius *
					taper * (spin >= 0.0 ? 1.0 : -1.0);

			if (rear) {
				tire_reaction_rear += force.longitudinal * radius;
				rear_rolling_torque += rolling_torque;
			} else {
				integrate_front_wheel(corner, force.longitudinal * radius, rolling_torque,
						brake, h);
			}
		}

		// 7. **The solid rear axle.** One rotational degree of freedom, integrated
		//    once, from the sum of both tires' reaction torques.
		//
		//    **This is issue #33 and it does not get its own header.** The
		//    question was asked and the answer is these six lines: the axle's
		//    entire state is one double and its entire behavior is one division.
		//    A `src/core/axle.h` would contain no arithmetic that could be tested
		//    without the two tires that load it and the drivetrain that drives it,
		//    so its unit test would have to reconstruct this solver to say
		//    anything — which is what `tests/core/test_vehicle.cpp` already is.
		//    What #33 actually asks to be verifiable is that the two rear wheels
		//    cannot disagree, and the strongest way to make that verifiable is to
		//    give them one variable and no accessor that could return two, which
		//    is what `axle_speed()` is.
		//
		//    `reflected_inertia` is `I_engine * ratio^2` and is already computed
		//    by `drivetrain.h`, which explains at length why leaving it out makes
		//    a locked driveline appear able to change speed instantly: in first
		//    gear it is 1.19 kg m^2 against the axle's own 0.096, more than ten
		//    times, and without it the launch diverges on the third substep.
		{
			const double inertia = axle_inertia_ + drive.reflected_inertia;
			const double other = drive.axle_torque - tire_reaction_rear - rear_rolling_torque;
			const double capacity = brake * brake_torque_rear;
			const double brake_torque = brake_axle_torque(axle_speed_, other, inertia, capacity, h);
			axle_speed_ += (other + brake_torque) / inertia * h;
		}

		// 8. Aerodynamic drag, at the center of mass. `ARCHITECTURE.md` §6: light
		//    drag, negligible downforce.
		const double speed = body.linear_velocity.length();
		out.central_force = body.linear_velocity * (-0.5 * AIR_DENSITY * drag_area * speed);

		telemetry_.engine_rpm = drive.engine_rpm;
		telemetry_.engine_torque = drive.engine_torque;
		telemetry_.axle_torque = drive.axle_torque;
		telemetry_.axle_speed = axle_speed_;
		telemetry_.gear = drive.gear;
		telemetry_.clutch_slip = drive.clutch_slip;
		telemetry_.clutch_torque = drive.clutch_torque;
		telemetry_.shifting = drive.shifting;
		telemetry_.over_rev = drive.over_rev;
		telemetry_.frame_warp = warp_;
	}

	// The frame's warp coordinate, in closed form.
	//
	// ## The derivation
	//
	// Add one internal coordinate `w` to the four contact deflections, along
	// `chassis_flex.h`'s warp mode `(+1, -1, -1, +1)`. The frame stores
	// `S*w^2/2` of strain energy in it, where `S` is
	// `warp_generalized_stiffness` — the same function, so the two files cannot
	// drift. At equilibrium the derivative of the total energy vanishes:
	//
	//     d/dw [ sum_i k_i (d_i + w*m_i)^2 / 2 + S*w^2 / 2 ] = 0
	//     sum_i k_i (d_i + w*m_i) m_i + S*w = 0
	//     w = -sum_i k_i d_i m_i / (sum_i k_i + S)          because m_i^2 == 1
	//
	// One divide, no iteration, deterministic.
	//
	// ## Why this is the same model as `chassis_flex.h` and not merely similar
	//
	// Feed it a pure warp deflection of amplitude `a`, so `d_i = a*m_i`. Then
	// `w = -a*4k/(4k + S)`, and the resulting corner force is
	// `k*a*S/(4k + S)`, i.e. an effective modal rate of `k*(S/4)/(k + S/4)` —
	// exactly `chassis_flex.h`'s series of the tire rate with `frame_rate = S/4`.
	// The two arrive at the same number from opposite directions, which is the
	// check worth having: that file solves a 4x4 for the loads, this one solves a
	// scalar for the twist, and they agree because they are the same physics.
	//
	// The one difference is deliberate. `chassis_flex.h` gives all four modes the
	// **mean** corner rate; this uses each corner's own, which matters here
	// because a corner that has left the ground contributes no rate at all and
	// the frame then has only three springs to twist against.
	void solve_warp(const double jack[CORNER_COUNT]) {
		const double frame_rate = warp_generalized_stiffness(frame_torsion_nm_per_deg,
				geometry_.track_front, geometry_.track_rear);

		double amplitude = 0.0;
		for (int pass = 0; pass < WARP_PASSES; ++pass) {
			double numerator = 0.0;
			double rate_sum = 0.0;
			for (int corner = 0; corner < CORNER_COUNT; ++corner) {
				if (!contact_[corner].hit) {
					continue;
				}
				const CornerSetup &setup = suspension_[corner].setup;
				// Deflection from the free length, which is where the spring
				// produces nothing. `max_droop` is the static deflection —
				// `suspension.h` spends a paragraph on why those are the same
				// quantity — so a corner at its rest length is already carrying
				// its static load.
				const double deflection = setup.max_droop + setup.rest_length +
						jack[corner] + amplitude * MODE_WARP[corner] -
						contact_[corner].ray_length;
				if (deflection <= 0.0) {
					continue; // Off the ground; it twists nothing.
				}
				const double base = setup.max_droop + setup.rest_length + jack[corner] -
						contact_[corner].ray_length;
				numerator += setup.spring_rate * base * MODE_WARP[corner];
				rate_sum += setup.spring_rate;
			}
			const double denominator = rate_sum + frame_rate;
			amplitude = denominator > 0.0 ? -numerator / denominator : 0.0;
		}
		warp_ = amplitude;
	}

	// The brake torque a wheel or axle actually receives, N m.
	//
	// A brake cannot spin a wheel backwards, so its torque is whatever is needed
	// to bring the rotation to zero within the substep, clamped by the capacity
	// the pedal is asking for. Written this way rather than as
	// `-sign(omega) * capacity` for a reason that shows up immediately in a test:
	// the sign form chatters at every stop, because a wheel brought just past zero
	// by a full-capacity torque gets a full-capacity torque back the other way on
	// the next substep, at 240 Hz, forever. This form cannot overshoot.
	static double brake_axle_torque(double speed, double other_torque, double inertia,
			double capacity, double h) {
		if (capacity <= 0.0 || inertia <= 0.0 || h <= 0.0) {
			return 0.0;
		}
		// Where the rotation would end up with no brake at all.
		const double free_speed = speed + other_torque / inertia * h;
		// The torque that would land it exactly on zero instead.
		const double needed = -inertia * free_speed / h;
		return clamp_signed(needed, capacity);
	}

	void integrate_front_wheel(int corner, double tire_reaction, double rolling_torque,
			double brake, double h) {
		const int index = corner == CORNER_FR ? 1 : 0;
		const double inertia = wheel_inertia_[corner];
		const double other = -tire_reaction - rolling_torque;
		const double capacity = brake * brake_torque_front;
		const double brake_torque =
				brake_axle_torque(front_speed_[index], other, inertia, capacity, h);
		front_speed_[index] += (other + brake_torque) / inertia * h;
	}

	// --- prediction between substeps ------------------------------------------

	// Advance the predicted body velocities by one substep, symplectic Euler.
	//
	// ADR-0032 finding 1 measured the engine's integrator to be symplectic Euler,
	// and finding 5 measured the angular response to be `I^-1 (r x F)` with the
	// world tensor `R I^-1 R^T` — to 4.4e-7 rad/s on a body deliberately rolled
	// 45 degrees so the tensor could not be mistaken for diagonal. This reproduces
	// both, so the solver's prediction of its own motion agrees with the body it
	// is predicting.
	//
	// The **diagonal** of the chassis-frame tensor is used, because that is all
	// Godot's `RigidBody3D.inertia` accepts. `chassis.h` measures the largest
	// off-diagonal term at 3.6% of the largest diagonal one, so this is an
	// approximation of known size rather than an assumption.
	//
	// Position and basis are deliberately not advanced. A kart's fastest yaw rate
	// is around 2 rad/s, which is 0.5 degrees per tick and a quarter of that per
	// substep; re-orthonormalizing a basis to chase it would add a normalization
	// per substep and a new way for two runs to disagree, in exchange for an
	// accuracy nobody could measure.
	void predict_forward(BodyState &predicted, const SubstepForces &forces, const Vec3 &origin,
			double h) const {
		Vec3 total_force = forces.central_force + gravity * mass_properties_.mass;
		Vec3 torque = forces.central_torque;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			total_force += forces.force[corner];
			// The lever arm about the center of mass. `point` is an offset from
			// the body origin, so the world point is `origin + point` and the arm
			// is that minus the center of mass — which is the arithmetic ADR-0033
			// found the project doing twice.
			const Vec3 arm = origin + forces.point[corner] - predicted.center_of_mass;
			torque += arm.cross(forces.force[corner]);
		}

		predicted.linear_velocity += total_force / mass_properties_.mass * h;

		const InertiaTensor &inertia = mass_properties_.inertia;
		if (inertia.xx > 0.0 && inertia.yy > 0.0 && inertia.zz > 0.0) {
			const Vec3 body_torque(torque.dot(predicted.basis_x), torque.dot(predicted.basis_y),
					torque.dot(predicted.basis_z));
			const Vec3 body_alpha(body_torque.x / inertia.xx, body_torque.y / inertia.yy,
					body_torque.z / inertia.zz);
			predicted.angular_velocity += (predicted.basis_x * body_alpha.x +
												  predicted.basis_y * body_alpha.y +
												  predicted.basis_z * body_alpha.z) *
					h;
		}
	}

	// Advance each ray length by the mount point's motion along it. See the header
	// comment on what this does and does not fix.
	void extrapolate_rays(const BodyState &predicted, double h) {
		const Vec3 down = -predicted.basis_y;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			if (!contact_[corner].hit) {
				continue;
			}
			const Vec3 mount = predicted.origin + to_world(predicted, ray_origin(corner));
			const double closing = predicted.velocity_at(mount).dot(down);
			double length = contact_[corner].ray_length - closing * h;
			if (length < 0.0) {
				length = 0.0;
			}
			contact_[corner].ray_length = length;
		}
	}

	// --- telemetry -------------------------------------------------------------

	// Add the substep that just ran to the tick's per-wheel accumulator.
	//
	// `solve_substep` writes its per-wheel numbers straight into `telemetry_`,
	// which is where they are read from here. That is deliberate rather than lazy:
	// the alternative is to pass an accumulator down into the corner loop, which
	// would put the averaging arithmetic in the middle of the tire model where the
	// next person to touch it has to think about it. Here the substep's read-out is
	// still a complete `WheelTelemetry` — every field is written on every substep,
	// on both paths through the corner loop — and this is the only place that knows
	// there is more than one substep.
	//
	// `grounded` and `tire_contact` are the two fields that are an OR rather than a
	// sum, because a bool has no mean. `vehicle_state.h` has the invariants that
	// choice preserves — in particular they are OR-ed **together**: OR-ing one and
	// AND-ing the other would let a tick report a corner that carried load without
	// touching the ground.
	void accumulate_wheel_telemetry() {
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			const WheelTelemetry &wheel = telemetry_.wheel[corner];
			WheelTelemetry &sum = wheel_sum_[corner];
			sum.normal_load += wheel.normal_load;
			sum.slip_angle += wheel.slip_angle;
			sum.slip_ratio += wheel.slip_ratio;
			sum.suspension_travel += wheel.suspension_travel;
			sum.lift += wheel.lift;
			sum.utilization += wheel.utilization;
			sum.steer_angle += wheel.steer_angle;
			sum.force += wheel.force;
			sum.grounded = sum.grounded || wheel.grounded;
			sum.tire_contact = sum.tire_contact || wheel.tire_contact;
		}
	}

	void fill_telemetry(const BodyState &body, const VehicleForces &total) {
		// The per-wheel mean, for the same reason and over the same substeps as
		// the forces above — issue #134, and note 1 in the header comment. The
		// division is the last write to these fields in the tick, so what a caller
		// reads is never a substep's own value.
		//
		// The whole read-out costs 80 adds and 40 multiplies per tick — ten fields
		// at four corners over two substeps, then one divide each — against the
		// solver's measured 15.4 us, which is not a number worth reporting. The
		// cost that was worth checking is the one §8 and §15 both care about: this
		// allocates nothing, reads no clock, and has no bound that depends on its
		// input.
		const double inverse = 1.0 / static_cast<double>(SUBSTEP_COUNT);
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			const WheelTelemetry &tick = wheel_sum_[corner];
			WheelTelemetry &wheel = telemetry_.wheel[corner];
			wheel.normal_load = tick.normal_load * inverse;
			wheel.slip_angle = tick.slip_angle * inverse;
			wheel.slip_ratio = tick.slip_ratio * inverse;
			wheel.suspension_travel = tick.suspension_travel * inverse;
			wheel.lift = tick.lift * inverse;
			wheel.utilization = tick.utilization * inverse;
			wheel.steer_angle = tick.steer_angle * inverse;
			wheel.force = tick.force * inverse;
			wheel.grounded = tick.grounded;
			wheel.tire_contact = tick.tire_contact;
		}

		Vec3 sum = total.central_force + gravity * mass_properties_.mass;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			sum += total.force[corner];
		}
		const Vec3 acceleration = sum / mass_properties_.mass;

		telemetry_.speed_ms = body.linear_velocity.length();
		telemetry_.lateral_g = acceleration.dot(body.basis_x) / G;
		// Forward is -Z, so a positive longitudinal g is acceleration.
		telemetry_.longitudinal_g = -acceleration.dot(body.basis_z) / G;
		telemetry_.substeps = SUBSTEP_COUNT;
		// `time_ratio` is deliberately left alone. It is simulated seconds per
		// wall-clock second — ADR-0033 finding 7 — and a solver that read a clock
		// to fill it in would break ARCHITECTURE.md §8 item 1 to report a number
		// only the boundary can honestly measure.
	}

	// --- state -----------------------------------------------------------------

	bool configured_ = false;

	ChassisGeometry geometry_;
	MassProperties mass_properties_;
	SteeringGeometry steering_;
	CornerSuspension suspension_[CORNER_COUNT];
	Tire tire_[CORNER_COUNT];
	double wheel_inertia_[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
	double axle_inertia_ = 0.0;

	// The integrated state. Four doubles and the drivetrain's own, and that is the
	// complete list of what a replay has to reproduce.
	double axle_speed_ = 0.0; // rad/s. **One** number for both rear wheels — #33.
	double front_speed_[2] = { 0.0, 0.0 }; // rad/s, FL and FR
	double steer_position_ = 0.0; // normalized, rate limited
	double warp_ = 0.0; // meters, the frame's twist

	// Scratch, held as members so `step()` allocates nothing.
	WheelContact contact_[CORNER_COUNT];
	CornerState corner_state_[CORNER_COUNT];
	SubstepForces accumulator_;
	// The per-wheel read-out summed over the tick's substeps, divided in
	// `fill_telemetry`. A member for the same reason `accumulator_` is one: so
	// `step()` allocates nothing.
	WheelTelemetry wheel_sum_[CORNER_COUNT];

	VehicleTelemetry telemetry_;
};

} // namespace kart::core

#endif // KART_CORE_VEHICLE_H
