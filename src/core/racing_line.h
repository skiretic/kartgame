#ifndef KART_CORE_RACING_LINE_H
#define KART_CORE_RACING_LINE_H

#include "core/chassis.h"
#include "core/circuit_reference.h"
#include "core/engine.h"
#include "core/gearbox.h"
#include "core/tire.h"
#include "core/units.h"
#include "core/vehicle.h"

#include <cmath>

// The racing line and its speed profile. ROADMAP M7's first two bullets:
// "racing-line generation by curvature minimization from the track spline" and
// "quasi-static speed profile with backward and forward braking-point passes,
// gear-aware".
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// This header is also allocation-free and locale-free for the reasons `tuning.h`
// and `race_rules.h` give: fixed-size arrays, no `std::vector`, no `std::string`,
// no iostream. It is **large** - `sizeof(RacingLine)` is 164.7 kB of station
// arrays - so a `RacingLine` belongs on the heap or in a static, never on a
// stack frame. `solve()` costs 38 ms at 1,024 stations, which is a load-time
// figure and not a per-tick one; nothing here runs in the solver.
//
// This file produces geometry and numbers. It does not drive: pure pursuit, the
// speed PID, the shift hysteresis and the difficulty tiers are the AI driver's,
// and they consume what is published here.
//
// ## The one rule that shaped every number in this file
//
// **The grip ceiling is read from `tire.h` and `chassis.h` at run time and is
// never written down here.** Not 2.1 g, not anything. Issue #137 is open - the
// kart scrubs to a standstill past a quarter of steering lock - and the tire
// model is expected to move when it closes. A profile with a lateral figure
// baked into it would go quietly wrong on the day the tire got better, and the
// test that guarded it would go red in a way indistinguishable from a real
// regression.
//
// So `SpeedModel::lateral_limit()` solves the load-transfer fixed point against
// whatever `TireCurve::friction_at` currently says, and `tests/core/
// test_racing_line.cpp` asserts *relations* - "the profile never demands more
// than the model permits", "raising peak friction never lowers a corner speed" -
// rather than absolute speeds or a lap time. Done that way an improvement to the
// tire makes the line faster instead of making the gate fail.
//
// ## And grip is not the only ceiling
//
// `chassis.h` puts the center of mass 41 mm to the right of the centerline, so
// the kart tips at 2.4336 g turning left and 2.807 g turning right. Those are
// geometry, not friction, and no tire fix moves them. Every lateral limit below
// is `min(tire, rollover)` and the two are reported separately, because "which
// one bound" is the question a later session will ask.
//
// Measured against `tire.h` as it stands: on flat ground the tire binds at
// 2.0039 g turning left and 2.0396 g turning right, and rollover does not bind
// anywhere. The crossover is at a peak friction of **2.65**, where the tire
// reaches 2.4602 g against a left-hand tipping point of 2.4336; past that the
// left-handers stop getting faster however good the tire gets, which is the
// correct behavior and not a bug. Right-handers have 0.37 g more to give and
// cross at about 3.0.
//
// **§6.4's lateral band is deliberately not used as a validation target here.**
// ADR-0034: it was written from prose, never sourced, and read as sustained when
// it described transient peaks. The model's own answer is the target.
//
// ## How the line is found
//
// The line is a lateral offset `n(s)` from the centerline, one value per station
// on a uniform arc-length grid, bounded by the corridor `|n| <= half_width -
// margin`. Minimizing the integral of curvature squared over that box is a
// quadratic program, and the curvature of an offset curve linearizes to
//
//     k(s) = k0(s) + n''(s) + k0(s)^2 n(s)
//
// which is a five-point stencil - a *biharmonic* operator. That matters, because
// plain Gauss-Seidel on a fourth-order operator converges like N^4 and 1,024
// stations would need 10^12 sweeps. What is used instead is nested iteration:
// relax on 16 stations, interpolate onto 32, relax, and so on to the full grid.
// Each level starts from an answer that is already right at its own wavelength,
// so every level only has to remove the error the previous one could not see.
// `solve()` reports the objective at every level, always measured on the finest
// grid so the rungs are comparable, and `tests/core/test_racing_line.cpp`
// asserts that the last rung is the smallest and that it beats the centerline.
//
// The linearization is only good while `|k0 n|` is small, and in an 11 m hairpin
// with 3 m of corridor it is 0.28, where the linearized curvature is 8% off. So
// the optimizer runs on the linear model and the **delivered curvature is then
// measured off the built polyline** by the Menger three-point formula, which is
// exact for three points on a circle and needs no derivative of `k0` - a
// derivative that does not exist, because `track.h` stores curvature per span
// and it steps at every control point.
//
// ## The analytic case that makes the optimizer checkable
//
// On a circular corridor of centerline radius R and half-corridor w, the
// minimum-curvature closed line is the **outer** circle, radius R + w. It is not
// a matter of taste: any closed curve inside the annulus turns 2*pi in total, so
// by Cauchy-Schwarz the integral of k^2 is at least (2*pi)^2 / L and is
// minimized by the longest admissible curve. The test suite runs exactly that
// and checks the delivered curvature against 1/(R + w).

namespace kart::core::line {

// Stations on the solver's uniform grid.
//
// 1,024 at Valdirone's 1,375 m is 1.343 m apart, which puts 8.2 samples on the
// 21 m line radius of Il Pozzo - fine enough that the second difference is not
// the limiting error. It is also a power of two, which is what lets the nested
// iteration halve the stride without a second array.
inline constexpr int MAX_STATIONS = 1024;

// The coarsest level of the cascade. Below this the "grid" is shorter than one
// corner and relaxing it says nothing.
inline constexpr int COARSEST_STATIONS = 16;

// How far the kart's own body keeps the line off the white line, meters.
//
// Half of FIA Karting Art. 8.1.1's 1.400 m maximum kart width, plus T1 §7.2's
// 120 mm edge line, which is the same sum `track.h` rule 17 uses for a grid slot
// and the same rule `SessionRunner` polices track limits with. A line that put a
// wheel on the curb would be faster and would also be a lap under investigation.
inline constexpr double DEFAULT_EDGE_MARGIN_M = 0.5 * 1.400 + circuit::EDGE_LINE_MAX_WIDTH_M;

// How much harder than coasting the kart has to be slowing before a segment
// counts as braking, m/s^2. See `RacingLine::braking`.
//
// **estimated.** Half a meter per second squared is 0.05 g and it separates the
// two populations by an order of magnitude: on the test-track layout a shift cut
// at 145 km/h reads 0.00 m/s^2 against coasting and a braking segment reads
// -20.0. Nothing lands in between, so the threshold's exact value is not load
// bearing - what matters is that it is measured against coasting and not
// against zero.
inline constexpr double BRAKING_MARGIN_MS2 = 0.5;

// Bisections per segment in the two speed passes.
//
// Fixed, not a tolerance, for the reason `steering.h` bisects a fixed 60 times:
// a loop whose count depends on its input costs a different amount of time on
// two runs and can return a different answer on two machines. Twenty halvings of
// a 45 m/s bracket is 43 micrometers per second, which is six orders below
// anything downstream can see.
inline constexpr int SEGMENT_BISECTIONS = 20;

// --- what the optimizer is told ---------------------------------------------

struct LineOptions {
	// Corridor inset from each edge, meters. See `DEFAULT_EDGE_MARGIN_M`.
	double edge_margin_m = DEFAULT_EDGE_MARGIN_M;

	// Relaxation sweeps at each level of the cascade. A sweep is symmetric -
	// forward then backward - so the answer carries no directional bias.
	int sweeps_per_level = 400;

	// Stop a level early when the objective stops moving by this relative
	// amount. Cheap insurance rather than a tuning knob: with 400 sweeps most
	// levels converge long before the budget.
	double converge_tol = 1e-12;

	// Set false to leave the line on the centerline. The speed profile still
	// runs, which is how the gate measures what the line is worth.
	bool optimize = true;

	// The coarsest level of the cascade, in stations. Exposed so a test can force
	// `count` - a single-level run with no cascade at all - and measure what the
	// cascade is worth against it.
	int coarsest_stations = COARSEST_STATIONS;

	// Shortest run of one gear the profile will emit, meters.
	//
	// **This is chatter suppression and it is load bearing.** `best_gear` is
	// monotone in speed, so on its own it cannot oscillate; charging a shift
	// against the speed profile can, because a shift lowers the speed, a lower
	// speed can want the lower gear back, and at 1.3 m stations the loop closes.
	// The first cut of this file reported 136 shifts and 71 braking zones on a
	// two-corner paperclip, and a spurious 2.32 g of braking that was really a
	// 65 ms torque cut counted twice. 15 m is half a second at a corner exit and
	// short enough to leave room for the three downshifts a hairpin needs.
	double min_gear_run_m = 15.0;
};

// --- what the speed profile is told -----------------------------------------

// Every quantity the quasi-static profile needs, all of it read from the
// project's own headers by `default_speed_model()` rather than restated.
//
// It is a value type on purpose: a caller that wants to ask "what would this
// line be worth with one more tooth on the axle sprocket" copies one and moves a
// field, and nothing global changes underneath the tests.
struct SpeedModel {
	double mass_kg = 0.0;
	double com_height_m = 0.0;

	// The perpendicular distance from the center of mass to the line joining the
	// two loaded contact patches, meters - `chassis.h`'s rollover arm, which
	// differs by hand because the engine hangs off the right. Stored as the arm
	// rather than as a threshold in g so that the banked generalization below has
	// something to work with.
	double rollover_arm_left_m = 0.0; // turning left, loading the right tires
	double rollover_arm_right_m = 0.0;

	double wheelbase_m = 0.0;
	double front_share = 0.0;
	double rolling_radius_m = 0.0;

	double drag_area_m2 = 0.0;
	double air_density = 0.0;
	double rolling_resistance = 0.0;

	// Total braking force the hardware can apply at the contact patches, N.
	double brake_force_max_n = 0.0;

	Tire tire;
	Engine engine;
	Gearbox gearbox;

	// Surface multiplier, 1.0 on asphalt. `surface.h`'s table feeds this.
	double surface_grip = 1.0;

	// How much of the envelope the driver uses, 0..1. **1.0 is the reference
	// line and the only value the gate measures**; M7's difficulty tiers move it
	// and the AI owns that decision, not this header.
	double grip_usage = 1.0;

	// --- the limits, all of them solved rather than tabulated --------------

	// The lateral acceleration this kart can hold, m/s^2, and which ceiling bound
	// it. `tilt` is the road's cross-fall as a tangent, **signed so that positive
	// helps** - a right-hander on a road that falls to the right. `turning_left`
	// picks the rollover arm.
	//
	// Both halves are a fixed point rather than a formula, because the tire's
	// friction coefficient falls with load and load transfer is a function of the
	// answer. Damped by a half at each step, which converges monotonically for
	// any load sensitivity `tire.h` can produce and cannot oscillate.
	//
	// The banked forms are the point-mass balance resolved in the road plane:
	//
	//     tire      a (cos p - mu sin p) <= g (sin p + mu cos p)
	//     rollover  a = g (arm + t h) / (h - t arm),  t = tan p
	//
	// and both reduce to the flat case at t = 0, which is the check worth having
	// in your head: the second one becomes `g * arm / h`, exactly
	// `chassis.h::rollover_threshold_g` times g.
	//
	// **The track width and the rollover arm are two different lengths and using
	// one for the other double-counts the offset engine.** Load transfer is
	// `F h / T` over the full track `T`; the arm is the distance from the center
	// of mass to the *outer* contact line, which is shorter on the side the
	// engine hangs off. `T` is recovered as `arm_left + arm_right`, because the
	// two arms are the two halves the offset splits the track into, and the
	// static split follows from the same lever: the pair the mass sits nearer
	// carries `(T - arm)/T` of it. Written that way the fixed point reproduces
	// `chassis.h::rollover_threshold_g` exactly at the point the inner pair
	// unloads, which is the check that says the two files agree.
	double lateral_limit(double tilt, bool turning_left,
			double *out_tire = nullptr, double *out_rollover = nullptr) const {
		const double arm = turning_left ? rollover_arm_left_m : rollover_arm_right_m;
		const double track = rollover_arm_left_m + rollover_arm_right_m;
		const double height = com_height_m;

		double rollover = 1e30;
		const double denominator = height - tilt * arm;
		if (denominator > 1e-9) {
			rollover = G * (arm + tilt * height) / denominator;
		}

		const double angle = std::atan(tilt);
		const double c = std::cos(angle);
		const double s = std::sin(angle);
		if (track <= 0.0) {
			return 0.0;
		}
		const double outer_share = (track - arm) / track;
		const double inner_share = arm / track;
		double a = G;
		for (int iteration = 0; iteration < 48; ++iteration) {
			const double in_plane = mass_kg * (a * c - G * s);
			const double normal_total = mass_kg * (a * s + G * c);
			if (normal_total <= 0.0) {
				break;
			}
			double transfer = in_plane * height / track;
			if (transfer < 0.0) {
				transfer = 0.0;
			}
			double outer_pair = outer_share * normal_total + transfer;
			double inner_pair = inner_share * normal_total - transfer;
			if (inner_pair < 0.0) {
				// The inside pair has left the ground. Everything is on two tires,
				// which is also the state the rollover ceiling describes.
				inner_pair = 0.0;
				outer_pair = normal_total;
			}
			const double per_outer = 0.5 * outer_pair;
			const double per_inner = 0.5 * inner_pair;
			const double capacity = surface_grip * grip_usage *
					(2.0 * tire.lateral.friction_at(per_outer) * per_outer +
							2.0 * tire.lateral.friction_at(per_inner) * per_inner);
			const double next = (capacity / mass_kg + G * s) / c;
			if (std::fabs(next - a) < 1e-10) {
				a = next;
				break;
			}
			a = 0.5 * (a + next);
		}

		if (out_tire != nullptr) {
			*out_tire = a;
		}
		if (out_rollover != nullptr) {
			*out_rollover = rollover;
		}
		return a < rollover ? a : rollover;
	}

	// Pure straight-line braking, m/s^2, gravity and drag excluded - those are
	// station quantities and the pass adds them. Same fixed point, with the
	// transfer running fore-and-aft instead of side to side.
	//
	// Capped by the hardware, and on this kart the hardware is not the cap:
	// `vehicle.h`'s 180/160 N m is 3,656 N at the patches, 2.13 g on 175 kg,
	// against a tire that answers about 1.95. Stated because it is the sort of
	// thing that changes when somebody re-sizes a brake and nobody re-reads this.
	double brake_limit() const {
		const double weight = mass_kg * G;
		double a = G;
		for (int iteration = 0; iteration < 48; ++iteration) {
			const double transfer = mass_kg * a * com_height_m / wheelbase_m;
			double front = weight * front_share + transfer;
			double rear = weight * (1.0 - front_share) - transfer;
			if (rear < 0.0) {
				rear = 0.0;
				front = weight;
			}
			const double per_front = 0.5 * front;
			const double per_rear = 0.5 * rear;
			const double capacity = surface_grip * grip_usage *
					(2.0 * tire.longitudinal.friction_at(per_front) * per_front +
							2.0 * tire.longitudinal.friction_at(per_rear) * per_rear);
			const double next = capacity / mass_kg;
			if (std::fabs(next - a) < 1e-10) {
				a = next;
				break;
			}
			a = 0.5 * (a + next);
		}
		const double hardware = brake_force_max_n / mass_kg;
		return hardware < a ? hardware : a;
	}

	// The most the **rear axle alone** can put down, newtons. A kart is rear
	// drive through a solid axle, so the driven pair carries only its share of
	// the weight plus whatever squat has moved onto it, and that is what makes
	// the corner-exit traction limit a different number from the braking one.
	double traction_force() const {
		const double weight = mass_kg * G;
		double a = G;
		for (int iteration = 0; iteration < 48; ++iteration) {
			const double transfer = mass_kg * a * com_height_m / wheelbase_m;
			double rear = weight * (1.0 - front_share) + transfer;
			if (rear > weight) {
				rear = weight;
			}
			const double per_rear = 0.5 * rear;
			const double capacity = surface_grip * grip_usage * 2.0 *
					tire.longitudinal.friction_at(per_rear) * per_rear;
			const double next = capacity / mass_kg;
			if (std::fabs(next - a) < 1e-10) {
				a = next;
				break;
			}
			a = 0.5 * (a + next);
		}
		return a * mass_kg;
	}

	// --- the gear-aware half ------------------------------------------------

	// Tractive force at the contact patch, N, at wide-open throttle in one gear.
	// Negative means the gear cannot be used at this speed because it would be
	// past the hard cut.
	//
	// Below idle in the lowest usable gear the real answer is a slipping clutch,
	// which this profile does not model; the torque is looked up at idle instead
	// and the case is confined to a standing start, which a lap does not contain.
	double gear_force(double speed_ms, int gear) const {
		if (gear < 1 || gear > kz::GEAR_COUNT || rolling_radius_m <= 0.0) {
			return -1.0;
		}
		double rpm = gearbox.engine_rpm_at(speed_ms, gear, rolling_radius_m);
		if (rpm > engine.hard_cut_rpm) {
			return -1.0;
		}
		if (rpm < engine.idle_rpm) {
			rpm = engine.idle_rpm;
		}
		const double crank = engine.torque(rpm, 1.0);
		if (crank <= 0.0) {
			return 0.0;
		}
		return crank * gearbox.total_ratio(gear) / rolling_radius_m;
	}

	// The gear a driver would be in, which is the one that pulls hardest here.
	// Returns 0 and a zero force if no gear can carry the speed at all.
	//
	// **This is the whole of M7's "gear-aware", and on a KZ it is not a
	// formality.** The powerband is 2,660 rpm wide; out of Il Pozzo at 52 km/h
	// first gear is worth 2,120 N and third is worth 785 N, so a profile that
	// assumed a single effective ratio would be wrong about the exit of that
	// corner by a factor of nearly three.
	//
	// Hysteresis is deliberately absent. This answers "which gear is best at this
	// speed", the AI driver answers "is it worth the shift", and putting the
	// second question here would bury a controller decision inside a geometry
	// file.
	int best_gear(double speed_ms, double *out_force) const {
		int best = 0;
		double best_force = 0.0;
		for (int gear = 1; gear <= kz::GEAR_COUNT; ++gear) {
			const double force = gear_force(speed_ms, gear);
			if (force > best_force) {
				best_force = force;
				best = gear;
			}
		}
		if (out_force != nullptr) {
			*out_force = best_force;
		}
		return best;
	}

	// Everything resisting the kart at a speed, newtons: air plus rolling.
	double resistance(double speed_ms) const {
		const double air = 0.5 * air_density * drag_area_m2 * speed_ms * speed_ms;
		return air + rolling_resistance * mass_kg * G;
	}

	// The rev limiter's road speed in top gear, m/s. The forward pass will
	// usually stop short of it on drag alone; this is the wall behind that.
	double speed_ceiling() const {
		return gearbox.road_speed(engine.hard_cut_rpm, kz::GEAR_COUNT, rolling_radius_m);
	}
};

// The model this project's own headers describe, read at construction.
//
// `KartVehicle::configure()` is called for one number - the loaded rolling
// radius, which is a suspension rest length and not a wheel radius. Doing it
// this way rather than copying 0.1457 into a constant is the difference between
// a figure that tracks the suspension and a figure that used to.
inline SpeedModel default_speed_model() {
	SpeedModel model;

	const MassProperties properties = kz::kart_mass_properties();
	model.mass_kg = properties.mass;
	model.com_height_m = properties.center_of_mass.y;
	// Recovered from the threshold rather than recomputed, so that the arm this
	// file uses and the threshold `chassis.h` publishes can never disagree.
	model.rollover_arm_left_m =
			kz::rollover_threshold_g(properties, true) * properties.center_of_mass.y;
	model.rollover_arm_right_m =
			kz::rollover_threshold_g(properties, false) * properties.center_of_mass.y;
	model.wheelbase_m = kz::REAR_AXLE_Z - kz::FRONT_AXLE_Z;
	model.front_share = kz::STATIC_FRONT_SHARE;

	KartVehicle vehicle;
	vehicle.configure();
	model.rolling_radius_m = vehicle.rolling_radius(CORNER_RL);
	model.drag_area_m2 = vehicle.drag_area;
	model.air_density = AIR_DENSITY;
	model.rolling_resistance = vehicle.rolling_resistance;
	model.brake_force_max_n =
			2.0 * vehicle.brake_torque_front / vehicle.rolling_radius(CORNER_FL) +
			vehicle.brake_torque_rear / vehicle.rolling_radius(CORNER_RL);
	model.tire = Tire();
	model.gearbox = vehicle.drivetrain.gearbox;

	return model;
}

// --- what comes out ----------------------------------------------------------

struct LineSummary {
	int stations = 0;
	double centerline_length_m = 0.0;
	double line_length_m = 0.0;

	// The objective, integral of k^2 ds, on the centerline and on the line. The
	// second must be the smaller of the two or the optimizer did nothing.
	double centerline_objective = 0.0;
	double line_objective = 0.0;
	// Per level of the cascade, in order. Must fall monotonically.
	int levels = 0;
	double level_objective[12] = { 0.0 };

	double max_curvature = 0.0; // 1/m, unsigned
	double min_radius_m = 0.0;
	double centerline_max_curvature = 0.0;
	double centerline_min_radius_m = 0.0;
	double max_offset_m = 0.0; // largest |n| used
	double corridor_slack_m = 0.0; // smallest (half_width - margin - |n|)

	// The largest longitudinal accelerations the delivered profile contains, in
	// g. **Total, air included** - at 145 km/h drag alone is 0.40 g, so the
	// braking figure legitimately exceeds `brake_limit_g` and a reader who
	// compares the two without this sentence will file a bug.
	double max_lateral_g = 0.0;
	double max_lateral_g_at_m = 0.0;
	double max_braking_g = 0.0;
	double max_braking_g_at_m = 0.0;
	double max_traction_g = 0.0;

	// The pure straight-line ceilings the model answered with, in g. Reported
	// beside the demands so the margin is visible without re-deriving it.
	double brake_limit_g = 0.0;
	double traction_limit_g = 0.0;

	// The tightest the two ceilings got anywhere on the lap, in g. Reported so a
	// reader can see which one is binding without re-deriving it.
	double min_tire_ceiling_g = 0.0;
	double min_rollover_ceiling_g = 0.0;
	int rollover_bound_stations = 0;

	double min_speed_ms = 0.0;
	double max_speed_ms = 0.0;
	// Reported, never gated. A lap time is a function of the tire model and issue
	// #137 is open; an assertion on it would go red the day the tire got better.
	double lap_time_s = 0.0;

	int braking_zones = 0;
	int shifts = 0;
	int gear_stations[kz::GEAR_COUNT + 1] = { 0 };
};

// The whole thing. Heap it - see the header comment on size.
class RacingLine {
public:
	// --- filling it in ------------------------------------------------------

	// Choose the grid and clear it. Returns the station count, which is the
	// largest power of two at or below `MAX_STATIONS` whose spacing is no coarser
	// than `target_spacing_m`, and never below `COARSEST_STATIONS`.
	//
	// A power of two is not decoration: the cascade halves the stride, and a
	// count that is not a power of two would leave the coarse levels landing
	// between stations.
	int begin(double length_m, double target_spacing_m = 1.5) {
		count_ = 0;
		length_m_ = 0.0;
		solved_ = false;
		if (length_m <= 0.0 || target_spacing_m <= 0.0) {
			return 0;
		}
		int wanted = COARSEST_STATIONS;
		while (wanted < MAX_STATIONS &&
				length_m / static_cast<double>(wanted) > target_spacing_m) {
			wanted *= 2;
		}
		count_ = wanted;
		length_m_ = length_m;
		spacing_ = length_m / static_cast<double>(count_);
		// Cleared rather than left over, so a second `begin` on the same object
		// cannot inherit the previous circuit's corridor or its gears.
		for (int index = 0; index < count_; ++index) {
			x_[index] = 0.0;
			z_[index] = 0.0;
			heading_[index] = 0.0;
			curvature_[index] = 0.0;
			half_width_[index] = 0.0;
			elevation_[index] = 0.0;
			grade_[index] = 0.0;
			bank_[index] = 0.0;
			grip_[index] = 1.0;
			offset_[index] = 0.0;
			gear_[index] = 0;
		}
		return count_;
	}

	// One station of the centerline, at `begin`'s uniform spacing.
	//
	// `x`, `z` and `heading` come from the consumer's own exact evaluation of the
	// spline, not from anything reconstructed here. `track.h` calls the stored
	// position a checksum verified to a millimeter at load; that is the sense in
	// which it is safe to build on - it has already been proven to agree with the
	// curvature that is normative.
	//
	// `bank` and `grade` are fractions, not percent. `curvature` is signed,
	// positive for a right-hander, and `half_width` is half the road.
	void set_station(int index, double x, double z, double heading_rad,
			double curvature, double half_width_m, double elevation_m, double grade,
			double bank, double surface_grip = 1.0) {
		if (index < 0 || index >= count_) {
			return;
		}
		x_[index] = x;
		z_[index] = z;
		heading_[index] = heading_rad;
		curvature_[index] = curvature;
		half_width_[index] = half_width_m;
		elevation_[index] = elevation_m;
		grade_[index] = grade;
		bank_[index] = bank;
		grip_[index] = surface_grip;
	}

	// --- solving ------------------------------------------------------------

	// Find the line, build it, measure it and run the speed profile.
	const LineSummary &solve(const SpeedModel &model, const LineOptions &options = LineOptions()) {
		summary_ = LineSummary();
		if (count_ < COARSEST_STATIONS) {
			return summary_;
		}
		model_ = model;
		margin_ = options.edge_margin_m;
		min_gear_run_m_ = options.min_gear_run_m;

		measure_centerline();
		if (options.optimize) {
			relax(options);
		} else {
			for (int index = 0; index < count_; ++index) {
				offset_[index] = 0.0;
			}
		}
		build_geometry();
		profile();
		fill_summary();
		solved_ = true;
		return summary_;
	}

	// --- reading it back ----------------------------------------------------

	int station_count() const { return count_; }
	double spacing_m() const { return spacing_; }
	bool solved() const { return solved_; }
	const LineSummary &summary() const { return summary_; }

	// The centerline station this line station sits on, meters.
	double station_distance(int index) const { return spacing_ * static_cast<double>(index); }
	// Arc length **along the line**, meters. Not the same as the station: the
	// line is longer than the centerline on the outside of a corner and shorter
	// on the inside, and the speed profile integrates along the line.
	double line_distance(int index) const { return at(index, line_s_); }
	double offset(int index) const { return at(index, offset_); }
	double x(int index) const { return at(index, line_x_); }
	double z(int index) const { return at(index, line_z_); }
	double elevation(int index) const { return at(index, elevation_); }
	// Signed, measured off the delivered polyline by the Menger formula.
	double curvature(int index) const { return at(index, line_k_); }
	double centerline_curvature(int index) const { return at(index, curvature_); }
	double segment_length(int index) const { return at(index, ds_); }
	double speed(int index) const { return at(index, speed_); }
	// The lateral-limited speed before the braking and acceleration passes. The
	// difference between this and `speed` is the whole of what the two passes did.
	double corner_speed(int index) const { return at(index, limit_); }
	double lateral_ceiling(int index) const { return at(index, lateral_cap_); }
	int gear(int index) const {
		if (index < 0 || index >= count_) {
			return 0;
		}
		return gear_[index];
	}
	double engine_rpm(int index) const {
		const int g = gear(index);
		if (g < 1) {
			return 0.0;
		}
		return model_.gearbox.engine_rpm_at(speed(index), g, model_.rolling_radius_m);
	}
	// Longitudinal acceleration between this station and the next, m/s^2, read
	// back out of the delivered speeds rather than out of the model that made
	// them - so a bug in a pass shows up here instead of being restated.
	double longitudinal(int index) const {
		if (index < 0 || index >= count_ || ds_[index] <= 1e-9) {
			return 0.0;
		}
		const int after = (index + 1) % count_;
		return (speed_[after] * speed_[after] - speed_[index] * speed_[index]) /
				(2.0 * ds_[index]);
	}

	// What lifting off alone would do here, m/s^2: air, rolling resistance and
	// the hill, and nothing else.
	double coasting(int index) const {
		if (index < 0 || index >= count_) {
			return 0.0;
		}
		return -model_.resistance(speed_[index]) / model_.mass_kg + gravity_along(index);
	}

	// Is the kart braking here?
	//
	// **Not "is it slowing down".** A 65 ms shift cut at 145 km/h loses 0.13 m/s
	// and so does a foot off the throttle, and counting either as a braking zone
	// turned a two-corner circuit into eighteen of them. Braking is deceleration
	// the road and the air cannot account for, which is a statement about the
	// driver rather than about the sign of a difference.
	bool braking(int index) const {
		if (index < 0 || index >= count_) {
			return false;
		}
		return longitudinal(index) < coasting(index) - BRAKING_MARGIN_MS2;
	}

	// The lateral acceleration this station's speed demands, m/s^2.
	double lateral_demand(int index) const {
		const double v = speed(index);
		return v * v * std::fabs(curvature(index));
	}

	// The largest fraction of the lateral ceiling any station demands. **This is
	// the gate.** It must not exceed 1, and it says so without ever naming a
	// number of g - so it stays green when issue #137's fix moves the tire and
	// goes red only if the profile really does ask for grip the model refuses.
	double worst_lateral_utilization() const {
		double worst = 0.0;
		for (int index = 0; index < count_; ++index) {
			if (lateral_cap_[index] <= 0.0) {
				continue;
			}
			const double used = lateral_demand(index) / lateral_cap_[index];
			if (used > worst) {
				worst = used;
			}
		}
		return worst;
	}

	// How much of the tire's longitudinal grip a segment demands, once the air,
	// the hill and the engine's own limit have each been given credit for their
	// share. 1.0 is a locked or spinning wheel.
	double longitudinal_utilization(int index) const {
		if (index < 0 || index >= count_ || ds_[index] <= 1e-9) {
			return 0.0;
		}
		const double from_tire = longitudinal(index) - coasting(index);
		const double pure = from_tire >= 0.0 ? traction_pure_ / model_.mass_kg : brake_pure_;
		if (pure <= 1e-9) {
			return 0.0;
		}
		return std::fabs(from_tire) / pure;
	}

	// Both axes against the friction ellipse, which is the constraint the passes
	// enforce - **at both ends of the segment**, which is the strictest honest
	// reading and the one the passes were changed to satisfy.
	//
	// **Written as the ellipse rather than as "how much was left over" for a
	// reason that is easy to miss.** At an apex the lateral term is exactly 1 by
	// construction, so the leftover longitudinal capacity is exactly zero, and a
	// ratio against it divides by zero and reports either infinity or nothing
	// depending on rounding. The ellipse is the same statement with no
	// singularity in it.
	double combined_utilization(int index) const {
		if (index < 0 || index >= count_) {
			return 0.0;
		}
		const double along = longitudinal_utilization(index);
		double worst = 0.0;
		for (int end = 0; end < 2; ++end) {
			const int station = end == 0 ? index : (index + 1) % count_;
			if (lateral_cap_[station] <= 0.0) {
				continue;
			}
			const double lateral = lateral_demand(station) / lateral_cap_[station];
			const double combined = std::sqrt(lateral * lateral + along * along);
			if (combined > worst) {
				worst = combined;
			}
		}
		return worst;
	}

	double worst_combined_utilization() const {
		double worst = 0.0;
		for (int index = 0; index < count_; ++index) {
			const double used = combined_utilization(index);
			if (used > worst) {
				worst = used;
			}
		}
		return worst;
	}

	// The start of each braking zone, in centerline stations. Returns how many
	// were written, up to `capacity`.
	int braking_points(double *out_station_m, double *out_speed_ms, int capacity) const {
		int written = 0;
		for (int index = 0; index < count_; ++index) {
			const int before = (index + count_ - 1) % count_;
			if (braking(index) && !braking(before)) {
				if (written < capacity) {
					if (out_station_m != nullptr) {
						out_station_m[written] = station_distance(index);
					}
					if (out_speed_ms != nullptr) {
						out_speed_ms[written] = speed_[index];
					}
				}
				++written;
			}
		}
		return written;
	}

private:
	// --- the optimizer ------------------------------------------------------

	double half_corridor(int index) const {
		const double room = half_width_[index] - margin_;
		return room > 0.0 ? room : 0.0;
	}

	// The centerline curvature a coarse level should see: the **mean** over the
	// cell it stands for, not the value at its own station.
	//
	// This is a restriction operator and leaving it out was a real defect, not a
	// refinement. `track.h` stores curvature per span, so it is piecewise
	// constant and it steps; at a 39 m coarse spacing a 94 m corner is two point
	// samples, and whether either of them lands inside the corner is an accident
	// of where the corner starts. The coarse level then optimizes a circuit that
	// is not this one and hands the next level down a worse starting guess than
	// zero - measured on the paperclip case, the objective ladder went
	// 0.183, 0.254, 0.229 before this existed and 0.183, 0.190, 0.213 after.
	//
	// It is still not monotone, and it is not going to be: the prolongation
	// clamps into the corridor, so it is not a projection and a level can be
	// handed a start that is worse than the one it produced. What the cascade
	// buys is the *answer*, and that is measured rather than asserted -
	// 400 sweeps a level lands on 0.17134 where 200,000 sweeps on the fine grid
	// alone reach 0.17313, and 400 sweeps on the fine grid alone manage 0.20034
	// against a centerline of 0.20868. The cascade is the difference between
	// solving this and barely moving.
	double coarse_curvature(int index, int stride) const {
		if (stride <= 1) {
			return curvature_[index];
		}
		const int half = stride / 2;
		double total = 0.0;
		for (int step = -half; step < stride - half; ++step) {
			total += curvature_[(index + step + count_) % count_];
		}
		return total / static_cast<double>(stride);
	}

	// The linearized curvature of the offset line at one station, on a grid of
	// stride `stride`. `k0 + n'' + k0^2 n`, with the second difference taken over
	// the strided neighbors so a coarse level sees a coarse wavelength.
	double model_curvature(int index, int stride) const {
		const double h = spacing_ * static_cast<double>(stride);
		const int before = (index - stride + count_) % count_;
		const int after = (index + stride) % count_;
		const double second = (offset_[after] - 2.0 * offset_[index] + offset_[before]) / (h * h);
		const double k0 = coarse_curvature(index, stride);
		return k0 + second + k0 * k0 * offset_[index];
	}

	double objective(int stride) const {
		double total = 0.0;
		const double h = spacing_ * static_cast<double>(stride);
		for (int index = 0; index < count_; index += stride) {
			const double k = model_curvature(index, stride);
			total += k * k * h;
		}
		return total;
	}

	// One coordinate's exact minimizer, clamped into the corridor.
	//
	// Moving `n_i` changes exactly three curvatures - its own and its two strided
	// neighbors' - so the derivative of the objective is three terms and the
	// quadratic is solved outright rather than stepped toward.
	void relax_one(int index, int stride) {
		const double h = spacing_ * static_cast<double>(stride);
		const int before = (index - stride + count_) % count_;
		const int after = (index + stride) % count_;
		const double inverse = 1.0 / (h * h);
		const double k0 = coarse_curvature(index, stride);

		const double gradient_self = k0 * k0 - 2.0 * inverse;
		const double numerator = model_curvature(before, stride) * inverse +
				model_curvature(index, stride) * gradient_self +
				model_curvature(after, stride) * inverse;
		const double denominator = inverse * inverse + gradient_self * gradient_self + inverse * inverse;
		if (denominator <= 0.0) {
			return;
		}
		double next = offset_[index] - numerator / denominator;
		const double bound = half_corridor(index);
		if (next > bound) {
			next = bound;
		}
		if (next < -bound) {
			next = -bound;
		}
		offset_[index] = next;
	}

	// Every station that is not on the current level's grid, linearly
	// interpolated from the two that are.
	//
	// It is a *prolongation*, and it does two jobs. The next finer level needs a
	// starting guess at its new points, and the objective reported for this level
	// has to be measured on the full grid or it is a different integral from the
	// next level's and the ladder means nothing. Linear is a poor prolongation for
	// a fourth-order operator - it puts a kink at every coarse point - and that is
	// fine, because a kink is high-frequency error and high-frequency error is the
	// one thing Gauss-Seidel removes immediately. That trade is the whole reason
	// the cascade is cheap.
	void fill_between(int stride) {
		if (stride <= 1) {
			return;
		}
		for (int base = 0; base < count_; base += stride) {
			const int after = (base + stride) % count_;
			for (int step = 1; step < stride; ++step) {
				const int index = base + step;
				const double t = static_cast<double>(step) / static_cast<double>(stride);
				double value = offset_[base] + (offset_[after] - offset_[base]) * t;
				const double bound = half_corridor(index);
				if (value > bound) {
					value = bound;
				}
				if (value < -bound) {
					value = -bound;
				}
				offset_[index] = value;
			}
		}
	}

	// Nested iteration: coarsest grid first, each finer level seeded by the
	// prolongation of the one above it.
	void relax(const LineOptions &options) {
		int coarsest = options.coarsest_stations;
		if (coarsest < 4) {
			coarsest = 4;
		}
		if (coarsest > count_) {
			coarsest = count_;
		}
		int stride = count_ / coarsest;
		if (stride < 1) {
			stride = 1;
		}
		summary_.levels = 0;
		while (stride >= 1) {
			double previous = objective(stride);
			for (int sweep = 0; sweep < options.sweeps_per_level; ++sweep) {
				for (int index = 0; index < count_; index += stride) {
					relax_one(index, stride);
				}
				for (int index = count_ - stride; index >= 0; index -= stride) {
					relax_one(index, stride);
				}
				const double now = objective(stride);
				if (previous > 0.0 && (previous - now) <= options.converge_tol * previous) {
					previous = now;
					break;
				}
				previous = now;
			}
			fill_between(stride);
			if (summary_.levels < 12) {
				// The **exact** integral off the built polyline, not the linearized
				// one the sweeps minimize. Two reasons. The rungs have to be the same
				// quantity as `centerline_objective` and `line_objective` or the
				// ladder cannot be compared with either; and the linearized objective
				// is the thing being optimized, so reporting it would be the
				// optimizer marking its own homework - it falls by construction even
				// where the delivered geometry got worse.
				build_geometry();
				summary_.level_objective[summary_.levels] = exact_objective();
				++summary_.levels;
			}
			if (stride == 1) {
				break;
			}
			stride /= 2;
		}
	}

	// --- geometry -----------------------------------------------------------

	static double cross_z(double ax, double az, double bx, double bz) {
		return ax * bz - az * bx;
	}

	// Menger curvature of three consecutive points, signed with the same
	// convention as `track.h`: positive for a right-hander. Exact for three
	// points on a circle, and free of any derivative of the centerline's own
	// curvature - which is what makes it usable at all, because `track.h` stores
	// curvature per span and it steps at every control point.
	static double menger(double x1, double z1, double x2, double z2, double x3, double z3) {
		const double ax = x2 - x1;
		const double az = z2 - z1;
		const double bx = x3 - x2;
		const double bz = z3 - z2;
		const double cx = x3 - x1;
		const double cz = z3 - z1;
		const double la = std::sqrt(ax * ax + az * az);
		const double lb = std::sqrt(bx * bx + bz * bz);
		const double lc = std::sqrt(cx * cx + cz * cz);
		const double denominator = la * lb * lc;
		if (denominator < 1e-12) {
			return 0.0;
		}
		return 2.0 * cross_z(ax, az, bx, bz) / denominator;
	}

	// The integral of curvature squared over the built line, in 1/m. Needs
	// `build_geometry()` to have run.
	double exact_objective() const {
		double total = 0.0;
		for (int index = 0; index < count_; ++index) {
			total += line_k_[index] * line_k_[index] * ds_[index];
		}
		return total;
	}

	// The centerline's own numbers, measured **the same way the line's are**.
	//
	// The first cut summed the stored `curvature^2 * spacing`, which is a
	// different integral from the Menger-over-chords one the line is measured
	// with - 0.7% different on Valdirone. That is small and it is exactly large
	// enough to break the check it exists for: with the corridor pinned shut the
	// line *is* the centerline, and "the line beats the centerline" passed by
	// 0.7% on a line that had not moved a millimeter. Found by
	// `line_probe.gd --break=noline`, which is what a negative control is for.
	void measure_centerline() {
		double worst = 0.0;
		for (int index = 0; index < count_; ++index) {
			offset_[index] = 0.0;
			const double k = std::fabs(curvature_[index]);
			if (k > worst) {
				worst = k;
			}
		}
		build_geometry();
		summary_.centerline_max_curvature = worst;
		summary_.centerline_min_radius_m = worst > 0.0 ? 1.0 / worst : 0.0;
		summary_.centerline_objective = exact_objective();
		summary_.centerline_length_m = length_m_;
	}

	void build_geometry() {
		for (int index = 0; index < count_; ++index) {
			// `right_of(heading)` is `(cos h, sin h)` - `track.h`'s convention, and
			// the same one `offset` is signed against.
			line_x_[index] = x_[index] + offset_[index] * std::cos(heading_[index]);
			line_z_[index] = z_[index] + offset_[index] * std::sin(heading_[index]);
		}
		double running = 0.0;
		for (int index = 0; index < count_; ++index) {
			const int after = (index + 1) % count_;
			const double dx = line_x_[after] - line_x_[index];
			const double dz = line_z_[after] - line_z_[index];
			ds_[index] = std::sqrt(dx * dx + dz * dz);
			line_s_[index] = running;
			running += ds_[index];
		}
		summary_.line_length_m = running;
		for (int index = 0; index < count_; ++index) {
			const int before = (index + count_ - 1) % count_;
			const int after = (index + 1) % count_;
			line_k_[index] = menger(line_x_[before], line_z_[before], line_x_[index],
					line_z_[index], line_x_[after], line_z_[after]);
		}
	}

	// --- the speed profile --------------------------------------------------

	// Gravity's component along the direction of travel, m/s^2, positive when it
	// is helping the kart along. `grade` is dz/ds, positive climbing, so a climb
	// resists.
	double gravity_along(int index) const {
		return -G * grade_[index];
	}

	// How much longitudinal grip is left once the corner has taken its share.
	// The friction ellipse, on accelerations rather than on forces, which is the
	// standard quasi-static simplification and is stated so nobody mistakes it
	// for the per-tire ellipse `tire.h` applies inside the solver.
	double longitudinal_fraction(int index, double v) const {
		const double cap = lateral_cap_[index];
		if (cap <= 0.0) {
			return 0.0;
		}
		double used = v * v * std::fabs(line_k_[index]) / cap;
		if (used >= 1.0) {
			return 0.0;
		}
		return std::sqrt(1.0 - used * used);
	}

	// The ellipse over a whole segment: the tighter of its two ends.
	//
	// **Both ends, not one, and that forces an implicit solve.** A pass that
	// constrains only the end it is walking toward is self-consistent and still
	// lets the kart exceed the ellipse at the other end - measured on the
	// paperclip's turn-in, where the line's curvature bumps, a segment came out
	// at 0.866 of the lateral limit while braking at 0.607 of the longitudinal
	// one, which is 1.058 of a circle whose radius is 1.
	//
	// Taking the tighter end and then evaluating it at the *previous* iteration's
	// speed does not fix it, it ratchets: on a constant-radius circle both ends
	// start exactly at the lateral limit, the fraction is therefore zero, the
	// forward pass finds no drive at all, and since these passes only ever lower
	// a speed the whole lap grinds down - measured, a 50 m skidpad settled 14%
	// slow and the lap time went from 10.4 s to 17.5 s. The fraction has to be
	// evaluated at the answer, so `SEGMENT_BISECTIONS` of bisection is what the
	// two passes do instead of one substitution.
	double span_fraction(int index, double from_speed, double to_speed) const {
		const int after = (index + 1) % count_;
		const double here = longitudinal_fraction(index, from_speed);
		const double there = longitudinal_fraction(after, to_speed);
		return here < there ? here : there;
	}

	void profile() {
		brake_pure_ = model_.brake_limit();
		traction_pure_ = model_.traction_force();
		const double brake_pure = brake_pure_;
		const double traction_pure = traction_pure_;
		const double ceiling = model_.speed_ceiling();
		for (int index = 0; index < count_; ++index) {
			gear_[index] = 0;
		}

		// 1. The lateral pass. Every station at the fastest it can be taken on
		//    its own, with no regard to how it is reached or left.
		for (int index = 0; index < count_; ++index) {
			const double k = line_k_[index];
			const bool left = k < 0.0;
			// The cross-fall helps when it falls into the turn. `bank` is signed
			// positive falling right, and a right-hander has positive curvature.
			const double tilt = k >= 0.0 ? bank_[index] : -bank_[index];
			SpeedModel station = model_;
			station.surface_grip = model_.surface_grip * grip_[index];
			double tire_g = 0.0;
			double roll_g = 0.0;
			lateral_cap_[index] = station.lateral_limit(tilt, left, &tire_g, &roll_g);
			tire_cap_[index] = tire_g;
			rollover_cap_[index] = roll_g;
			const double magnitude = std::fabs(k);
			double v = ceiling;
			if (magnitude > 1e-9) {
				v = std::sqrt(lateral_cap_[index] / magnitude);
			}
			if (v > ceiling) {
				v = ceiling;
			}
			limit_[index] = v;
			speed_[index] = v;
		}

		// 2 and 3. Backward for the braking points, forward for what the engine
		//    can do about it, alternating until neither moves anything. They have
		//    to alternate: the forward pass can only lower a speed, and lowering
		//    one tightens the backward constraint that reaches it.
		//
		// Run three times, and the reason is the shift. A torque cut costs speed,
		// less speed can want a different gear, and a different gear is a
		// different cut - so the gear plan cannot be decided inside the pass that
		// depends on it. The first sweep runs with no cut at all and produces the
		// speeds a gear plan can be read off; the plan is then frozen and charged,
		// twice, and the second charge is what the reported speeds and gears both
		// come from.
		plan_valid_ = false;
		converge(brake_pure, traction_pure, ceiling);
		for (int round = 0; round < 2; ++round) {
			assign_gears(min_gear_run_m_);
			plan_valid_ = true;
			converge(brake_pure, traction_pure, ceiling);
		}
	}

	void converge(double brake_pure, double traction_pure, double ceiling) {
		for (int round = 0; round < 16; ++round) {
			bool changed = backward_pass(brake_pure);
			changed = forward_pass(traction_pure, ceiling) || changed;
			if (!changed) {
				break;
			}
		}
	}

	// The gear the driver is in at each station: whichever pulls hardest, with a
	// minimum distance held before a change is allowed.
	//
	// The hold is a distance and not a hysteresis in force, because a distance is
	// what a driver's hand is short of. Walked twice round the lap so the value
	// at the start line is the one the previous lap arrived with rather than
	// whatever the array happened to start at.
	void assign_gears(double min_run_m) {
		double force = 0.0;
		int current = model_.best_gear(speed_[0], &force);
		double held = min_run_m;
		for (int lap = 0; lap < 2; ++lap) {
			for (int index = 0; index < count_; ++index) {
				const int wanted = model_.best_gear(speed_[index], &force);
				if (wanted != 0 && wanted != current && held >= min_run_m) {
					current = wanted;
					held = 0.0;
				}
				gear_[index] = current;
				held += ds_[index];
			}
		}
	}

	// The entry speed a segment supports if the entry speed were `entry`. A
	// larger entry uses more of the ellipse laterally, so this **decreases** as
	// its argument grows, which is what makes the bisection below well posed.
	double braking_entry(int index, double entry, double target, double brake_pure) const {
		const double fraction = span_fraction(index, entry, target);
		double decel = brake_pure * fraction;
		// Drag and rolling resistance help; a climb helps; a descent does not.
		decel += model_.resistance(target) / model_.mass_kg;
		decel -= gravity_along(index);
		if (decel < 0.0) {
			decel = 0.0;
		}
		return std::sqrt(target * target + 2.0 * decel * ds_[index]);
	}

	bool backward_pass(double brake_pure) {
		bool changed = false;
		for (int pass = 0; pass < 2; ++pass) {
			for (int step = count_; step > 0; --step) {
				const int index = step - 1;
				const int after = (index + 1) % count_;
				const double target = speed_[after];
				double high = speed_[index];
				if (braking_entry(index, high, target, brake_pure) >= high - 1e-12) {
					continue; // this entry speed is already reachable
				}
				// `target` itself is always reachable, so it brackets the root from
				// below and the interval only ever shrinks.
				double low = target;
				if (low > high) {
					low = 0.0;
				}
				for (int bisection = 0; bisection < SEGMENT_BISECTIONS; ++bisection) {
					const double middle = 0.5 * (low + high);
					if (braking_entry(index, middle, target, brake_pure) >= middle) {
						low = middle;
					} else {
						high = middle;
					}
				}
				if (low < speed_[index] - 1e-9) {
					speed_[index] = low;
					changed = true;
				}
			}
		}
		return changed;
	}

	bool forward_pass(double traction_pure, double ceiling) {
		bool changed = false;
		for (int pass = 0; pass < 2; ++pass) {
			for (int index = 0; index < count_; ++index) {
				const int after = (index + 1) % count_;
				const double v = speed_[index];
				double engine_force = 0.0;
				int gear = 0;
				if (plan_valid_ && gear_[index] >= 1) {
					gear = gear_[index];
					engine_force = model_.gear_force(v, gear);
					if (engine_force < 0.0) {
						// The plan's gear cannot carry this speed any more, which can
						// only happen because a later pass slowed the kart down. Fall
						// back rather than report a negative force.
						gear = model_.best_gear(v, &engine_force);
					}
				} else {
					gear = model_.best_gear(v, &engine_force);
				}
				// A shift costs `gearbox.shift_time` with the dogs out and nothing
				// connecting the engine to the road. On a KZ that is 65 ms, which at
				// a 100 km/h corner exit is 1.8 m of the road covered coasting - real
				// enough to be worth modeling.
				//
				// Charged only where the kart is **accelerating**, which is decided
				// below once the drive force is known. Under braking the dogs being
				// out changes nothing a brake pedal was not already doing, and
				// charging it there was how a 65 ms cut turned into 2.32 g of phantom
				// deceleration in the first cut of this file.
				const int previous_gear = gear_[(index + count_ - 1) % count_];
				const bool shifting = plan_valid_ && gear >= 1 && previous_gear >= 1 &&
						gear != previous_gear;

				// Same implicit problem as the braking pass and the same answer: the
				// exit speed decides how much of the ellipse the corner is using at
				// the far end, so it cannot be read off before it is known.
				double high = speed_[after];
				double low = 0.0;
				if (drive_exit(index, v, high, engine_force, traction_pure, shifting, ceiling) <
						high - 1e-12) {
					for (int bisection = 0; bisection < SEGMENT_BISECTIONS; ++bisection) {
						const double middle = 0.5 * (low + high);
						if (drive_exit(index, v, middle, engine_force, traction_pure, shifting,
									ceiling) >= middle) {
							low = middle;
						} else {
							high = middle;
						}
					}
					if (low < speed_[after] - 1e-9) {
						speed_[after] = low;
						changed = true;
					}
				}
			}
		}
		return changed;
	}

	// The exit speed a segment supports if the exit speed were `exit`. Decreasing
	// in `exit` for the same reason `braking_entry` is decreasing in its own
	// argument, which is what the bisection above relies on.
	double drive_exit(int index, double entry, double exit, double engine_force,
			double traction_pure, bool shifting, double ceiling) const {
		const double fraction = span_fraction(index, entry, exit);
		const double grip_force = traction_pure * fraction;
		double force = engine_force < grip_force ? engine_force : grip_force;
		force -= model_.resistance(entry);
		const double accel = force / model_.mass_kg + gravity_along(index);

		double distance = ds_[index];
		double reached = entry;
		if (shifting && accel > 0.0) {
			double cut = entry * model_.gearbox.shift_time;
			if (cut > distance) {
				cut = distance;
			}
			const double coast = -model_.resistance(entry) / model_.mass_kg + gravity_along(index);
			const double coasted = reached * reached + 2.0 * coast * cut;
			reached = coasted > 0.0 ? std::sqrt(coasted) : 0.0;
			distance -= cut;
		}
		const double squared = reached * reached + 2.0 * accel * distance;
		double allowed = squared > 0.0 ? std::sqrt(squared) : 0.0;
		if (allowed > ceiling) {
			allowed = ceiling;
		}
		return allowed;
	}

	// --- reporting ----------------------------------------------------------

	void fill_summary() {
		summary_.stations = count_;

		double objective_total = 0.0;
		double worst_k = 0.0;
		double worst_offset = 0.0;
		double slack = 1e30;
		double worst_lateral = 0.0;
		double worst_lateral_at = 0.0;
		double worst_brake = 0.0;
		double worst_brake_at = 0.0;
		double worst_traction = 0.0;
		double tire_floor = 1e30;
		double rollover_floor = 1e30;
		double slowest = 1e30;
		double fastest = 0.0;
		double time = 0.0;
		int zones = 0;
		int shifts = 0;

		for (int index = 0; index < count_; ++index) {
			const int after = (index + 1) % count_;
			const int before = (index + count_ - 1) % count_;

			objective_total += line_k_[index] * line_k_[index] * ds_[index];
			const double k = std::fabs(line_k_[index]);
			if (k > worst_k) {
				worst_k = k;
			}
			const double magnitude = std::fabs(offset_[index]);
			if (magnitude > worst_offset) {
				worst_offset = magnitude;
			}
			const double room = half_corridor(index) - magnitude;
			if (room < slack) {
				slack = room;
			}

			const double lateral = lateral_demand(index) / G;
			if (lateral > worst_lateral) {
				worst_lateral = lateral;
				worst_lateral_at = station_distance(index);
			}
			if (tire_cap_[index] / G < tire_floor) {
				tire_floor = tire_cap_[index] / G;
			}
			if (rollover_cap_[index] < 1e29 && rollover_cap_[index] / G < rollover_floor) {
				rollover_floor = rollover_cap_[index] / G;
			}
			if (rollover_cap_[index] < tire_cap_[index] - 1e-9) {
				++summary_.rollover_bound_stations;
			}

			if (ds_[index] > 1e-9) {
				const double along = longitudinal(index) / G;
				if (-along > worst_brake) {
					worst_brake = -along;
					worst_brake_at = station_distance(index);
				}
				if (along > worst_traction) {
					worst_traction = along;
				}
				const double mean = 0.5 * (speed_[index] + speed_[after]);
				if (mean > 1e-6) {
					time += ds_[index] / mean;
				}
			}

			if (speed_[index] < slowest) {
				slowest = speed_[index];
			}
			if (speed_[index] > fastest) {
				fastest = speed_[index];
			}
			if (braking(index) && !braking(before)) {
				++zones;
			}
			if (gear_[index] != gear_[before]) {
				++shifts;
			}
			if (gear_[index] >= 0 && gear_[index] <= kz::GEAR_COUNT) {
				++summary_.gear_stations[gear_[index]];
			}
		}

		summary_.line_objective = objective_total;
		summary_.max_curvature = worst_k;
		summary_.min_radius_m = worst_k > 0.0 ? 1.0 / worst_k : 0.0;
		summary_.max_offset_m = worst_offset;
		summary_.corridor_slack_m = slack;
		summary_.max_lateral_g = worst_lateral;
		summary_.max_lateral_g_at_m = worst_lateral_at;
		summary_.max_braking_g = worst_brake;
		summary_.max_braking_g_at_m = worst_brake_at;
		summary_.max_traction_g = worst_traction;
		summary_.brake_limit_g = brake_pure_ / G;
		summary_.traction_limit_g = traction_pure_ / model_.mass_kg / G;
		summary_.min_tire_ceiling_g = tire_floor < 1e29 ? tire_floor : 0.0;
		summary_.min_rollover_ceiling_g = rollover_floor < 1e29 ? rollover_floor : 0.0;
		summary_.min_speed_ms = slowest;
		summary_.max_speed_ms = fastest;
		summary_.lap_time_s = time;
		summary_.braking_zones = zones;
		summary_.shifts = shifts;
	}

	double at(int index, const double *array) const {
		if (index < 0 || index >= count_) {
			return 0.0;
		}
		return array[index];
	}

	int count_ = 0;
	double length_m_ = 0.0;
	double spacing_ = 0.0;
	double margin_ = DEFAULT_EDGE_MARGIN_M;
	double min_gear_run_m_ = 15.0;
	double brake_pure_ = 0.0;
	double traction_pure_ = 0.0;
	// True once `assign_gears` has produced a plan the forward pass may charge
	// shifts against. False on the first sweep, which is the one that produces
	// the speeds the plan is read off.
	bool plan_valid_ = false;
	bool solved_ = false;
	SpeedModel model_;
	LineSummary summary_;

	// The centerline, as handed in.
	double x_[MAX_STATIONS] = { 0.0 };
	double z_[MAX_STATIONS] = { 0.0 };
	double heading_[MAX_STATIONS] = { 0.0 };
	double curvature_[MAX_STATIONS] = { 0.0 };
	double half_width_[MAX_STATIONS] = { 0.0 };
	double elevation_[MAX_STATIONS] = { 0.0 };
	double grade_[MAX_STATIONS] = { 0.0 };
	double bank_[MAX_STATIONS] = { 0.0 };
	double grip_[MAX_STATIONS] = { 0.0 };

	// The line.
	double offset_[MAX_STATIONS] = { 0.0 };
	double line_x_[MAX_STATIONS] = { 0.0 };
	double line_z_[MAX_STATIONS] = { 0.0 };
	double line_k_[MAX_STATIONS] = { 0.0 };
	double line_s_[MAX_STATIONS] = { 0.0 };
	double ds_[MAX_STATIONS] = { 0.0 };

	// The profile.
	double limit_[MAX_STATIONS] = { 0.0 };
	double speed_[MAX_STATIONS] = { 0.0 };
	double lateral_cap_[MAX_STATIONS] = { 0.0 };
	double tire_cap_[MAX_STATIONS] = { 0.0 };
	double rollover_cap_[MAX_STATIONS] = { 0.0 };
	int gear_[MAX_STATIONS] = { 0 };
};

} // namespace kart::core::line

#endif // KART_CORE_RACING_LINE_H
