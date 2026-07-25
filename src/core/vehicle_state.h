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
// One entry per corner plus a central term. The forces are already summed over
// however many substeps ran — ADR-0032 conclusion 3, re-confirmed against a
// contact in ADR-0033 finding 6: a 240 Hz solver cannot make the engine step
// twice, so its substeps accumulate and the total is applied once.
struct VehicleForces {
	Vec3 force[CORNER_COUNT]; // world, newtons
	Vec3 application_point[CORNER_COUNT]; // world offset FROM THE BODY ORIGIN
	Vec3 central_force; // world, newtons, drag and anything else acting at once
	Vec3 central_torque; // world, N m
};

// Per-wheel telemetry. Issue #43 lists exactly these.
struct WheelTelemetry {
	double normal_load = 0.0; // N
	double slip_angle = 0.0; // rad
	double slip_ratio = 0.0; // dimensionless
	double suspension_travel = 0.0; // m, positive compressed
	double lift = 0.0; // m off the ground, zero when loaded
	double utilization = 0.0; // fraction of the friction ellipse in use
	double steer_angle = 0.0; // rad
	Vec3 force; // world, N, what this tire produced
	bool grounded = false;
};

// Everything the telemetry panel and the HUD read. Issue #43 ships with the
// milestone rather than after it, because ARCHITECTURE.md §19 names unbounded
// vehicle tuning as a risk and this is the only defense named against it.
struct VehicleTelemetry {
	WheelTelemetry wheel[CORNER_COUNT];

	// Drivetrain, straight out of DrivetrainOutput.
	double engine_rpm = 0.0;
	double engine_torque = 0.0; // N m at the crank
	double axle_torque = 0.0; // N m at the rear axle
	double axle_speed = 0.0; // rad/s, the solid rear axle — one number, #33
	int gear = 0;
	double clutch_slip = 0.0; // rad/s
	double clutch_torque = 0.0; // N m
	bool shifting = false;
	bool over_rev = false;

	// Chassis.
	double speed_ms = 0.0;
	double lateral_g = 0.0;
	double longitudinal_g = 0.0;
	double frame_warp = 0.0; // m, the warp mode's amplitude — #32 made visible

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
	double time_ratio = 1.0;
};

} // namespace kart::core

#endif // KART_CORE_VEHICLE_STATE_H
