#ifndef KART_CORE_VEHICLE_STATE_H
#define KART_CORE_VEHICLE_STATE_H

#include "core/chassis_flex.h"
#include "core/vec3.h"

// The vocabulary the vehicle solver and the Godot boundary share.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## Why this file is separate from the solver
//
// Three things are written against these structs — the solver, the `RigidBody3D`
// that feeds it, and the telemetry panel that reads it — and they are built by
// different hands at different times. Putting the vocabulary in its own header
// means none of them can quietly widen it, and it means the telemetry panel does
// not have to include the solver to know what a wheel reports.
//
// ## The one rule that is not obvious
//
// `VehicleForces::application_point` is an offset from the **body origin**, in
// world coordinates. Not from the center of mass. That is Godot's convention for
// `apply_force`, it is the opposite of what this project believed for a
// milestone, and it doubled every pitch and roll moment in M3a until
// `tools/verify/contact_probe.gd` measured it. See ADR-0033. The field is named
// for what it is so that a caller passing a center-of-mass-relative vector has
// to actively ignore the name.

namespace kart::core {

// One tick of driver intent, in the units the input map produces.
//
// Shift requests are edges rather than levels: they are true for exactly the
// tick the button went down. A level would make a held button shift once per
// tick through the whole gearbox, which is what the first version of every
// sequential gearbox does.
struct DriverInput {
	double throttle = 0.0; // 0..1
	double brake = 0.0; // 0..1
	double steer = 0.0; // -1..1, positive to the left
	double clutch = 0.0; // 0..1, 1 = lever fully pulled, fully disengaged
	bool shift_up = false;
	bool shift_down = false;
};

// The rigid-body state Godot owns, handed to the solver each tick.
//
// The basis arrives as three axis vectors rather than as a transform, which is
// vec3.h's stated position: a transform type invites code that has stopped
// saying which frame it is in. These are the chassis axes expressed in world
// coordinates, so `basis_x` is where the kart's right points.
struct BodyState {
	Vec3 origin; // body origin, world
	Vec3 center_of_mass; // world, already transformed
	Vec3 basis_x; // chassis +X (right), world, unit
	Vec3 basis_y; // chassis +Y (up), world, unit
	Vec3 basis_z; // chassis +Z (rearward), world, unit
	Vec3 linear_velocity; // world, m/s, of the center of mass
	Vec3 angular_velocity; // world, rad/s

	// Velocity of a point rigidly attached to the body, world frame. The tire
	// code needs this at every contact patch and it is the one piece of rigid
	// body kinematics that is easy to write down wrong: the lever arm is from
	// the **center of mass**, because that is what the body rotates about,
	// even though forces are applied relative to the origin.
	Vec3 velocity_at(const Vec3 &world_point) const {
		return linear_velocity + angular_velocity.cross(world_point - center_of_mass);
	}
};

// What the Godot side found under one wheel, filled from a raycast.
//
// `hit` false does not mean "no contact" — see ADR-0033 finding 4. A ray that
// starts below a surface returns nothing at all, which is exactly what happens
// when a wheel is buried deepest in a curb. The boundary is responsible for
// latching the last valid normal rather than passing a miss straight through.
struct GroundQuery {
	bool hit = false;
	double distance = 0.0; // ray origin to contact, meters
	Vec3 point; // world
	Vec3 normal; // world, unit
	double surface_grip = 1.0; // the §6 surface table's multiplier
	int surface = 0; // SurfaceType, for particle and audio hooks
};

// What the solver produces for one tick, for the engine to apply.
//
// One entry per corner plus a central term. The forces are the **mean** over
// however many substeps ran — ADR-0032 conclusion 3, re-confirmed against a
// contact in ADR-0033 finding 6: a 240 Hz solver cannot make the engine step
// twice, so its substeps accumulate and the total is applied once. "Accumulate"
// means the impulse is conserved, and since each substep's force acted for
// `dt / N`, the single force delivering the same impulse over `dt` is the
// average and not the sum. This comment said "summed" for a milestone;
// `KartVehicle::step` has always divided, and the word was the thing that was
// wrong.
struct VehicleForces {
	Vec3 force[CORNER_COUNT]; // world, newtons
	Vec3 application_point[CORNER_COUNT]; // world offset FROM THE BODY ORIGIN
	Vec3 central_force; // world, newtons, drag and anything else acting at once
	Vec3 central_torque; // world, N m
};

// Per-wheel telemetry. Issue #43 lists exactly these.
//
// ## Every number here describes the whole tick, not an instant inside it
//
// The solver substeps and returns the **mean** of the substep forces, because
// that is the single force delivering the same impulse — see `VehicleForces`
// above. These fields are averaged over the same substeps, for the same reason
// and so that they agree with what was applied. Issue #134: they used to be
// whatever the last substep happened to produce, which made this struct a
// read-out that did not describe what the solver did with it. The forces were
// never wrong; the description of them was, which is the same class of defect as
// `get_contact_impulse` reading 1.000371x high (ADR-0033 finding 1) and gets the
// same treatment — stated here rather than left for a caller to discover.
//
// **The identity that makes it worth doing.** One corner's applied force is
//
//     VehicleForces::force[corner] == normal * normal_load + force
//
// where `normal` is the contact normal from the `GroundQuery` that produced the
// tick. That is exact, not approximate: `KartVehicle::step` extrapolates the ray
// length across its substeps and deliberately holds the contact plane, so the
// normal is common to every substep and factors straight out of the mean. There
// is no normal in this struct because the side that filled the `GroundQuery`
// already has one, and duplicating it would create two that could disagree.
// `tests/core/test_vehicle.cpp` asserts the identity per corner per tick.
//
// **What averaging is and is not.** For a force or a load it is exactly right —
// the mean is the value that conserves the impulse. For the kinematic fields it
// is a choice, and the choice is the same one for two reasons. A last-substep
// read-out samples a 240 Hz signal at 120 Hz, which is aliasing, and the thing it
// aliases is precisely what this solver's stability guards exist to suppress: the
// note above `SLIP_CONVERGENCE_FRACTION` records a measured limit cycle that
// alternated sign **every substep**, and a graph of every other substep would
// have shown a steady bias where the mean shows the truth. And a caller that
// reads a slip angle beside the force it produced should be looking at one tick,
// not at two halves of one.
//
// The one number that could have been made meaningless by averaging is
// `slip_angle`, because a wrapping angle averaged across the wrap is a value that
// occurred at neither end. It cannot wrap: it is `atan2(-lateral, reference)`
// with a strictly positive second argument, so it lives in (-pi/2, pi/2) and the
// mean of two of them is between them.
//
// ## Three fields describe the wheel's relationship with the road, and they are
// ## three different questions
//
// Issue #136 was filed because two of them looked like they contradicted each
// other. They did not — nobody had written down which question each one answers,
// so a reader was free to assume they answered the same one. They are:
//
//     grounded      the tire is **carrying load**   (normal_force > 0)
//     tire_contact  the tire is **touching**        (the spring has travel left)
//     lift          how far the tire is **clear of the ground**, meters
//
// and the only relation between them, at the tick level, is
//
//     grounded == true  =>  tire_contact == true
//
// which holds by construction; see the two flags' own notes below for why the
// substep combiner preserves it.
//
// **The state that looked like a contradiction is `grounded` false with
// `tire_contact` true and `lift` zero, and it is a damper doing its job.**
// `suspension.h` splits bump and rebound damping and the rebound rate is
// deliberately the higher of the two, because a wheel that has been unloaded in a
// corner must not pump itself back down before the corner is over — issue #32.
// The front corner carries 360 N on 3.4 mm of tire deflection and its rebound
// damping is 1776 N per m/s, so at 1.6 mm of remaining deflection the 174 N of
// spring force is cancelled by **98 mm/s** of extension. The tire is still on the
// road; it is carrying nothing; and `lift` is correctly zero because there is no
// daylight under it. That is a real state on a real kart and it is not a defect to
// be tuned away. It is now reportable rather than inferable, which is the whole of
// what #136 fixed.
//
// **The consumer's rule.** "This wheel is not working" is `!grounded` — that is
// the flag the solver itself uses to zero the force, so it is the one the panel's
// banner and the wheel count key off. `lift` is the number issue #32 is judged on
// and it is a *distance*, not a state: a wheel can be doing nothing with `lift`
// exactly zero.
struct WheelTelemetry {
	// All doubles below are the mean over the tick's substeps.
	double normal_load = 0.0; // N
	double slip_angle = 0.0; // rad
	double slip_ratio = 0.0; // dimensionless
	double suspension_travel = 0.0; // m, positive compressed

	// Daylight under the tire, meters, measured along the suspension ray.
	// Positive means the tire is clear of the ground; zero means it is touching,
	// whether or not it is carrying anything.
	//
	// **A lower bound and not an exact height whenever the ray missed.** A ray is
	// only cast as far as `KartVehicle::ray_length` — the wheel radius plus a
	// 100 mm margin — so a corner that finds no ground reports the whole of what
	// is known, which is that the tire is at least the margin clear. It used to
	// report zero for that case, so a kart in flight read as "all four down" on
	// every corner (issue #136). A miss is not ambiguous by the time it arrives
	// here: ADR-0033 finding 4's buried wheel is latched into a hit by the Godot
	// boundary, so a miss reaching the solver means the ground is genuinely not
	// there.
	double lift = 0.0;

	double utilization = 0.0; // fraction of the friction ellipse in use
	double steer_angle = 0.0; // rad
	Vec3 force; // world, N, what this tire produced

	// True if the corner was carrying load on **any** substep — one of the two
	// fields that cannot be averaged, so it is an OR rather than a mean.
	//
	// The alternative, the last substep's value, breaks the only invariant this
	// flag has: a wheel that extends past its free length on the second substep
	// contributed force on the first, so the mean force is non-zero while the
	// flag says the wheel is in the air. With the OR,
	//
	//     grounded == false  =>  normal_load == 0 and force == 0
	//
	// holds by construction. **One way only**, and `tire_contact` below is where
	// the other direction went: `suspension.h` sets its own flag from
	// `normal_force > 0`, so a tire the rebound damper has unloaded is on the road
	// with this false, and reading it as "the wheel is in the air" is the misread
	// issue #136 was filed about.
	bool grounded = false;

	// True if the tire was touching the road on **any** substep: the spring still
	// had travel left, whether or not the corner produced any force with it.
	//
	// The negation is the useful reading and it is exact — `!tire_contact` means
	// **the spring is at full droop**, either because the tire ran out of
	// deflection or because the ray found no ground at all. `lift` then says which
	// and by how much.
	//
	// **An OR across the substeps, like `grounded` and for its sake.** Per substep
	// `suspension.h` gives `normal_force > 0 => deflection > 0`, so grounded
	// implies contact there; OR-ing both keeps that implication at the tick level,
	// where an AND would not — a corner loaded on the first substep and clear on
	// the second would report `grounded` true and `tire_contact` false, which is
	// the same class of read-out-that-contradicts-itself #136 was filed about. A
	// mean is not available and would be meaningless if it were: "the tire was
	// half touching" describes nothing.
	bool tire_contact = false;
};

// The value `VehicleTelemetry::time_ratio` carries until something that can see a
// wall clock overwrites it.
//
// A ratio of two durations cannot be negative, so this is unreachable as a
// measurement and unambiguous as a sentinel. The field used to default to 1.0,
// which is not a sentinel at all — it is the value that means "the simulation is
// keeping up", so a caller reading telemetry straight off the solver was handed a
// plausible lie rather than an obvious absence.
//
// **Negative and not NaN**, and the difference is not a matter of taste. The one
// consumer is `scripts/game/telemetry_panel.gd`, which frames the entire panel in
// its alert color when `absf(ratio - 1.0) > 0.02`. Every comparison against NaN
// is false, so NaN would be silent in exactly the place built to shout — and it
// would then propagate through the graph's `maxf` span update, which decays
// multiplicatively and so would never recover, blanking that graph for the rest
// of the session even after real values started arriving. The panel also reads
// the key with `float(sample.get("time_ratio", 1.0))`, so its default only covers
// a **missing** key: a present-and-NaN value goes straight through. A negative
// trips the alert on the first frame and is arithmetically inert.
inline constexpr double TIME_RATIO_UNMEASURED = -1.0;

// Everything the telemetry panel and the HUD read. Issue #43 ships with the
// milestone rather than after it, because ARCHITECTURE.md §19 names unbounded
// vehicle tuning as a risk and this is the only defense named against it.
struct VehicleTelemetry {
	WheelTelemetry wheel[CORNER_COUNT];

	// Drivetrain, straight out of DrivetrainOutput, and **end of tick** rather than
	// averaged over the substeps the way `WheelTelemetry` is. That is not an
	// inconsistency: every number below is integrated state, and the value of an
	// integrated state at the end of the tick is the state — it is what the next
	// tick starts from and what `axle_speed()` and `drivetrain` themselves report.
	// Averaging an rpm across two substeps would produce a figure that disagrees
	// with the engine that is turning at it. The per-wheel fields are averaged
	// because they are not state: they are what the solver *did* during the tick.
	double engine_rpm = 0.0;
	double engine_torque = 0.0; // N m at the crank
	double axle_torque = 0.0; // N m at the rear axle
	double axle_speed = 0.0; // rad/s, the solid rear axle — one number, #33
	int gear = 0;
	double clutch_slip = 0.0; // rad/s
	double clutch_torque = 0.0; // N m
	bool shifting = false;
	bool over_rev = false;

	// Chassis. The two accelerations are computed from the tick's applied forces,
	// so they are already averages and agree with `VehicleForces` by construction.
	double speed_ms = 0.0;
	double lateral_g = 0.0;
	double longitudinal_g = 0.0;
	// The warp mode's amplitude — #32 made visible. End of tick, and deliberately:
	// the solver re-solves it in closed form every substep and keeps the answer in
	// the member `warp_amplitude()` returns and the state hash quantizes, so a
	// telemetry copy that was a mean would be a fifth number disagreeing with the
	// three that are the same one.
	double frame_warp = 0.0; // m

	// Solver health.
	int substeps = 0;

	// Simulated seconds per wall-clock second.
	//
	// Not a vanity metric. ADR-0033 finding 7 measured that
	// `max_physics_steps_per_frame` clamps and does not bank: under a frame-rate
	// collapse Godot runs its eight ticks and stops, simulation time falls behind
	// at a measured 0.6476 of real time, and it never catches up. A replay that
	// counts ticks cannot see it and a driver can see nothing else. This is the
	// only place it becomes visible.
	//
	// **The solver never writes it** — it would have to read a clock, which
	// `ARCHITECTURE.md` §8 item 1 forbids it — so it arrives from the Godot
	// boundary or it does not arrive at all. Which is why it starts at a sentinel
	// and not at 1.0: see `TIME_RATIO_UNMEASURED`.
	double time_ratio = TIME_RATIO_UNMEASURED;
};

} // namespace kart::core

#endif // KART_CORE_VEHICLE_STATE_H
