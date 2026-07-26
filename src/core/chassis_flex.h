#ifndef KART_CORE_CHASSIS_FLEX_H
#define KART_CORE_CHASSIS_FLEX_H

#include "core/suspension.h"
#include "core/units.h"

#include <cmath>

// The chassis as four contact springs and one twist, and the load split that
// falls out of it. Issue #32, ARCHITECTURE.md §6 "What makes a kart a kart".
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## The problem this file exists to solve
//
// A rigid body resting on four stiff contact springs is **statically
// indeterminate in warp**. Three equations hold it up — vertical force, roll
// moment, pitch moment — and there are four unknown contact loads. You can raise
// one corner and lower the diagonal one with no net force and no net moment, so
// nothing in rigid-body statics says how the load divides. A real kart resolves
// it by twisting, and that is the whole content of this file: the fourth
// equation is the frame's torsion.
//
// Which is why the ordinary vehicle-dynamics move — pick a roll-stiffness
// distribution and multiply — is the wrong shape here. It hides the indeterminacy
// behind a fudge factor with no units. The formulation below decomposes the four
// corner deflections into four modes,
//
//     heave   (+1, +1, +1, +1)   all four together
//     pitch   (+1, +1, -1, -1)   front pair against rear pair
//     roll    (+1, -1, +1, -1)   left pair against right pair
//     warp    (+1, -1, -1, +1)   the diagonals against each other
//
// gives the first three the tire and axle rates, and gives warp the frame's
// torsional rate — a quantity with units, a published value, and a real part
// behind it. The four vectors are the rows of a 4x4 Hadamard matrix, so they are
// mutually orthogonal and the decomposition is exact and invertible, which
// `tests/core/test_chassis_flex.cpp` checks because a sign error in the warp
// vector is the error to expect and it is invisible in a render.
//
// ## What warp stiffness does and does not do
//
// It does **not** set the front-to-rear load-transfer split on flat ground on
// its own. That is worth saying plainly because it is the intuition everybody
// arrives with. On perfectly flat ground with a torsionally rigid frame, the
// split is set by the roll stiffnesses of the two axles, `k * track^2 / 2`, and
// warp never moves. What the frame's torsion does is decide **how much of the
// front's share of the roll moment can reach the rear tires**, and the two
// limits are worth knowing:
//
//   * a torsionally free frame (K_T = 0) transfers load at each end in
//     proportion to that end's own mass — the two halves are independent;
//   * a torsionally rigid frame (K_T = infinity) transfers in proportion to
//     roll stiffness.
//
// A real kart is between them, and for this one those limits give a 0.44 and a
// 0.26 front share of the transfer. The measured consequence is in the tests and
// in the report: those two limits **lift different wheels**, and the crossover
// sits inside the range of published frame stiffnesses. That is not a detail.
//
// Warp stiffness also decides how much a non-planar input moves load, and the
// non-planar input a kart has all the time is the caster jacking at the front.
// That is the path by which steering unloads the inside rear, and it is the only
// path on flat ground.
//
// ## Sourcing
//
// `docs/REFERENCES-chassis.md` records where every number came from, including
// the two that could not be sourced and what was assumed instead. The frame
// torsional stiffness is measured and published; the kart tire's vertical rate
// is not, and is derived here from inflation pressure and contact geometry with
// the derivation written down so it can be argued with.

namespace kart::core {

// ---------------------------------------------------------------------------
// Geometry and mass
// ---------------------------------------------------------------------------

// The whole-kart numbers the load split is computed from.
//
// **These want to live in `kz_reference.h`.** They are not there yet, and this
// file does not own that header, so they sit here with defaults and are reported
// to the main thread rather than edited in. Every one of them is either a CIK
// class figure or a measurement off the generated kart, and none of them is
// invented.
struct ChassisGeometry {
	// CIK KZ minimum, kart plus driver. Matches `kz::MASS_WITH_DRIVER_KG`.
	double mass = 175.0;

	// Center-of-mass height, meters. Derived in `kart_debug_vehicle.gd` from two
	// masses rather than picked — an 80 kg kart carrying its frame, engine and
	// tires around 0.12 m, and a 95 kg driver reclined at about 0.32 m.
	//
	// **This default is not what the solver runs.** `vehicle.h::configure()`
	// replaces it with `kz::kart_mass_properties().center_of_mass.y` = 0.2197,
	// derived from twenty-one lumps rather than from two, and every threshold in
	// this file divides by it — so 0.23 is 1.4% low in a divisor and the tests
	// here use the configured value rather than this one. Issue #129 is what
	// happens when they do not: 0.23 is 4.7% high as a divisor, so a threshold
	// computed from it is 4.5% low.
	double com_height = 0.23;

	// **Centerline** track, meters, front and rear. `params.py` records
	// `track_front = 1.240` and `track_rear = 1.400`, and those are the kart's
	// *outside* width — issue #119. The centerline track is one tire width
	// narrower at each end, which is where 1.105 and 1.185 come from. Getting
	// this wrong inflates the rollover threshold by 6% and would hide a real
	// margin problem.
	double track_front = 1.105;
	double track_rear = 1.185;

	// Front axle to rear axle. CIK maximum and what a KZ runs.
	double wheelbase = 1.050;

	// Fraction of the static load carried by the front axle. A KZ carries its
	// engine outboard and behind the driver's hip.
	//
	// **This disagrees with `kart_debug_vehicle.gd`,** which places the center of
	// mass 5% of the wheelbase behind the midpoint and so implies 0.45. 0.42 is
	// the figure M3b's skidpad solution was built on. One of the two is wrong and
	// the difference is worth about 0.03 g in where the inside rear lifts; it is
	// reported rather than silently reconciled here.
	double front_mass_share = 0.42;

	// Lateral position of each corner, meters, +X right. Godot's frame.
	double corner_x(int corner) const {
		const double half = (corner == CORNER_FL || corner == CORNER_FR)
				? track_front * 0.5
				: track_rear * 0.5;
		return (corner == CORNER_FL || corner == CORNER_RL) ? -half : half;
	}

	// Longitudinal position of each corner relative to the center of mass,
	// meters, +Z rearward — so the front axle is negative, because Godot's
	// forward is -Z.
	double corner_z(int corner) const {
		const double to_rear = front_mass_share * wheelbase;
		const double to_front = wheelbase - to_rear;
		return (corner == CORNER_FL || corner == CORNER_FR) ? -to_front : to_rear;
	}

	double static_load(int corner) const {
		const double total = mass * G;
		return (corner == CORNER_FL || corner == CORNER_FR)
				? front_mass_share * total * 0.5
				: (1.0 - front_mass_share) * total * 0.5;
	}

	// Mass carried at one corner, for the natural frequency and damping ratios.
	double corner_mass(int corner) const { return static_load(corner) / G; }

	// **There is no rollover threshold on this struct, and that is deliberate.**
	// `chassis.h` owns it: `kz::rollover_threshold_g(properties, turning_left)`
	// returns **2.434 g turning left and 2.806 g turning right**. Two numbers,
	// because the center of mass sits 41 mm right of the centerline — 27 kg of
	// engine, exhaust and radiator hung outboard, against 7.3 kg of ballast on
	// the left that does not cover it — so a kart turning left rolls onto its
	// right-hand tires with the mass already leaning that way.
	//
	// A `rollover_threshold_g()` stood here and returned one number. It was wrong
	// twice, and issue #129 is the cleanup:
	//
	//   * It measured from the **origin** rather than from the center of mass's
	//     ground projection, which silently assumes that projection is on the
	//     centerline. It is not: the 41 mm is `com_x / com_height` = **±0.186 g**,
	//     7.1% either way, and it is the whole of the 0.372 g between the two
	//     figures above.
	//   * It took the **perpendicular** distance to the tipping axis. The lever
	//     the lateral force works against is the distance measured along the
	//     kart's X axis at the center of mass's longitudinal station, which is
	//     the perpendicular distance divided by the cosine of the axis' skew.
	//     The tipping axis runs 2.18 degrees off the kart's axis — 40 mm of track
	//     difference over a 1.05 m wheelbase — so this one is only 0.07%, and it
	//     is recorded rather than fixed because the function is gone. The old
	//     comment was right that half the rear track is the wrong lever and that
	//     the tipping axis is the right one; it just stopped one cosine short.
	//
	// It also cited ARCHITECTURE.md §6.4's "2.0-2.5 g" band, which ADR-0034 has
	// since split into a **sustained** 1.5-2.0 g and a **transient peak**
	// 2.0-2.5 g, exactly because the old band's top edge sat above the kart's own
	// tipping point. The comparison the function turned on no longer exists.
	//
	// **Adding a `com_x` here would not have fixed it.** `solve_corner_loads`
	// below balances `m*a_y*h` about the origin too, so the tipping point this
	// model implies — `lift_threshold_g` on the *second* inside corner to unload
	// — is centerline-symmetric as well, and it lands on the **mean** of the two
	// `kz::` figures rather than on either: 2.6198 g, equal to machine epsilon,
	// which the tests assert because it says the two models differ by the lateral
	// offset and by nothing else at all. A lateral offset honored by an
	// accessor and ignored by the equilibrium solve would put two disagreeing
	// numbers straight back. Issue #123 is that field, and it has to reach
	// `static_load`, `corner_x` and the roll equation together or not at all.
};

// ---------------------------------------------------------------------------
// Rates, and where they come from
// ---------------------------------------------------------------------------

// Vertical rate of one front tire, N/m.
//
// **Not sourced. Derived, and the derivation is the argument.** No published
// load-deflection curve for a kart slick was found; see
// `docs/REFERENCES-chassis.md`. What is used is the pressure-membrane model — a
// pneumatic tire pressed against a flat plate carries its load on a contact
// patch of area `F/p`, and for a torus of crown radius R and patch width w the
// patch half-length is `sqrt(2*R*d)` at deflection d, so
//
//     F = 2*p*w*sqrt(2*R*d)      and therefore      k = dF/dd = 4*p^2*w^2*R/F
//
// At 0.75 bar, a 110 mm patch width on a 135 mm section and a 0.140 m rolling
// radius, carrying 360 N, that is 106 kN/m and 1.7 mm of static deflection.
//
// The model ignores carcass stiffness, so it is a lower bound on a stiff tire
// and roughly right on a soft low-pressure one, which a kart slick is. It is
// stated as a derivation rather than a measurement because the **ratio** of this
// to the rear rate is what decides which wheel lifts, and anyone re-deriving it
// should be able to see exactly which assumption to attack.
inline constexpr double TIRE_RATE_FRONT_N_PER_M = 106000.0;

// Vertical rate of one rear tire, N/m. Same derivation at 0.85 bar, a 180 mm
// patch width on a 215 mm section, a 0.1475 m radius and 498 N: 277 kN/m and
// 0.9 mm of static deflection.
//
// The rear tire being 2.6x the front's rate is the single most consequential
// number in this file. It is what makes the kart's roll stiffness rear-biased,
// and rear-biased roll stiffness is what lifts the inside rear rather than the
// inside front. It comes out of the tire sizes — 215 mm of rear against 135 mm
// of front, at a higher pressure — and not out of a preference for the answer.
inline constexpr double TIRE_RATE_REAR_N_PER_M = 277500.0;

// Frame torsional stiffness, N·m per degree.
//
// **Sourced.** Fu and Wang, "A study on torsional stiffness of the competition
// go-kart frame", WIT Transactions on The Built Environment Vol 91 (2007),
// measure 193,620 N·mm/deg for the baseline competition frame in a twisting test
// with the rear anchors fixed and a vertical force at one kingpin. Biancolini et
// al., cited there, recommend at least 165,000-169,000 N·mm/deg. So 193.6 is a
// real frame near the bottom of the acceptable band.
//
// **A second published figure disagrees by a factor of five to eighteen.**
// Sampayo et al. (2021) quote 1,051 to 3,464 N·m/deg for their designs, from a
// test that applies a couple at two front hard points rather than a single force
// at one kingpin. The two are not the same measurement. The tests sweep the
// range because the crossover between "inside front lifts" and "inside rear
// lifts" sits inside it — this is the number the model is most sensitive to and
// the one most worth measuring on a real frame.
inline constexpr double FRAME_TORSION_NM_PER_DEG = 193.62;

// Damping as a fraction of critical.
//
// A real kart's is nearer 0.05: tire hysteresis, a little in the frame, and
// nothing else, which is why a kart skips over a curb instead of absorbing it.
// What is used is far higher, and that is a numerical decision stated as one.
//
// The reason is the contact, not the ride. A 277 kN/m spring against a 51 kg
// corner, integrated explicitly at 240 Hz, sits at `omega*dt = 0.31` — stable,
// but a 5%-damped mode there takes about 130 substeps to settle and every one of
// those is a normal load the tire model turns into a lateral force. The kart
// would buzz. 0.30 bump and 0.45 rebound settle it in about a fifth of a second
// and cost realism only in how a curb strike rebounds, which is a thing to tune
// against a render later rather than a thing to get exactly right now.
inline constexpr double BUMP_DAMPING_RATIO = 0.30;
inline constexpr double REBOUND_DAMPING_RATIO = 0.45;

// Corner setup for one wheel, with every derived quantity derived rather than
// restated. `max_droop` is the static deflection — see `suspension.h` — so it
// falls out of the load and the rate and cannot drift from either.
inline CornerSetup corner_setup(const ChassisGeometry &geometry, int corner) {
	const bool is_front = corner == CORNER_FL || corner == CORNER_FR;
	CornerSetup setup;
	setup.spring_rate = is_front ? TIRE_RATE_FRONT_N_PER_M : TIRE_RATE_REAR_N_PER_M;
	setup.max_droop = geometry.static_load(corner) / setup.spring_rate;

	// The ray from the mount to the ground at static equilibrium. The mount sits
	// at the wheel center, so this is the loaded rolling radius: the free radius
	// less the static deflection. Front 10x4.50-5 and rear 11x7.10-5 slicks give
	// 0.140 and 0.1475 m unloaded.
	const double free_radius = is_front ? 0.140 : 0.1475;
	setup.rest_length = free_radius - setup.max_droop;

	// Compression before the bump stop, meters. Set by the frame's ground
	// clearance rather than by the tire: the rails sit 35 mm off the road, so
	// 30 mm of tire compression is where a curb starts hitting the frame instead
	// of the wheel, and past that the model should be firm.
	setup.max_travel = 0.030;

	const double corner_mass = geometry.corner_mass(corner);
	const double critical = setup.critical_damping(corner_mass);
	setup.bump_damping = BUMP_DAMPING_RATIO * critical;
	setup.rebound_damping = REBOUND_DAMPING_RATIO * critical;
	return setup;
}

// ---------------------------------------------------------------------------
// The mode decomposition
// ---------------------------------------------------------------------------

// Sign of each corner in each mode. Rows of a 4x4 Hadamard matrix, in
// FL, FR, RL, RR order, which is why they are mutually orthogonal.
inline constexpr double MODE_HEAVE[CORNER_COUNT] = { 1.0, 1.0, 1.0, 1.0 };
inline constexpr double MODE_PITCH[CORNER_COUNT] = { 1.0, 1.0, -1.0, -1.0 };
inline constexpr double MODE_ROLL[CORNER_COUNT] = { 1.0, -1.0, 1.0, -1.0 };
inline constexpr double MODE_WARP[CORNER_COUNT] = { 1.0, -1.0, -1.0, 1.0 };

// Four amplitudes, in meters, one per mode.
struct ChassisModes {
	double heave = 0.0;
	double pitch = 0.0;
	double roll = 0.0;
	double warp = 0.0;
};

// Corner deflections to modes. Exact: the basis is orthogonal with every entry
// +/-1, so the projection is a dot product over four and a divide by four.
inline ChassisModes decompose(const double deflection[CORNER_COUNT]) {
	ChassisModes modes;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		modes.heave += MODE_HEAVE[corner] * deflection[corner];
		modes.pitch += MODE_PITCH[corner] * deflection[corner];
		modes.roll += MODE_ROLL[corner] * deflection[corner];
		modes.warp += MODE_WARP[corner] * deflection[corner];
	}
	modes.heave *= 0.25;
	modes.pitch *= 0.25;
	modes.roll *= 0.25;
	modes.warp *= 0.25;
	return modes;
}

// Modes back to corner deflections. The exact inverse of `decompose`, which the
// tests check on an arbitrary input rather than on a symmetric one — a symmetric
// input has zero warp and would pass with the warp basis vector's signs shuffled.
inline void compose(const ChassisModes &modes, double deflection[CORNER_COUNT]) {
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		deflection[corner] = modes.heave * MODE_HEAVE[corner] +
				modes.pitch * MODE_PITCH[corner] +
				modes.roll * MODE_ROLL[corner] +
				modes.warp * MODE_WARP[corner];
	}
}

// The frame's warp stiffness as a generalized rate, N/m, against the warp
// amplitude in meters.
//
// A warp amplitude `a` raises FL and RR by `a` and lowers FR and RL by `a`. That
// rolls the front by `2a/track_front` and the rear by `-2a/track_rear`, so the
// frame twists through the sum of those, and the strain energy `K_T * twist^2 /
// 2` gives a generalized rate of
//
//     S = 4 * K_T * (1/track_front + 1/track_rear)^2
//
// with `K_T` in N·m per **radian**. For this kart at 193.6 N·m/deg that is about
// 136 kN/m, against roughly 764 kN/m for the four tires together — so the frame
// is the softer half of the series pair by a factor of five, which is exactly the
// statement that a kart resolves warp by twisting rather than by squashing tires.
inline double warp_generalized_stiffness(double torsion_nm_per_deg, double track_front,
		double track_rear) {
	const double per_radian = torsion_nm_per_deg * 180.0 / PI;
	const double lever = 1.0 / track_front + 1.0 / track_rear;
	return 4.0 * per_radian * lever * lever;
}

// The four modal rates, N/m, in the convention where setting all four equal to a
// corner rate `k` reproduces four independent springs of rate `k`. That property
// is what makes the numbers comparable to each other and to `spring_rate`, and it
// is checked in the tests.
struct ChassisFlex {
	double heave_rate = 0.0;
	double pitch_rate = 0.0;
	double roll_rate = 0.0;
	double warp_rate = 0.0;

	// Corner forces from corner deflections, N.
	//
	// Not clamped at zero here. This is the elastic model; the "a tire cannot
	// pull" clamp belongs at the corner, in `suspension.h`, where the contact
	// state is known.
	void corner_forces(const double deflection[CORNER_COUNT], double force[CORNER_COUNT]) const {
		const ChassisModes modes = decompose(deflection);
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			force[corner] = heave_rate * modes.heave * MODE_HEAVE[corner] +
					pitch_rate * modes.pitch * MODE_PITCH[corner] +
					roll_rate * modes.roll * MODE_ROLL[corner] +
					warp_rate * modes.warp * MODE_WARP[corner];
		}
	}

	double warp_softness() const {
		const double stiffest = heave_rate > roll_rate ? heave_rate : roll_rate;
		return warp_rate > 0.0 ? stiffest / warp_rate : 0.0;
	}
};

// Modal rates for a kart: tire rates for the three rigid modes, tire in series
// with the frame for warp.
//
// The series is the physics. A warp deflection has to go somewhere, and it
// divides between squashing tires and twisting the frame in proportion to their
// compliances. Nothing else in the four modes has that property, because heave,
// pitch and roll do not deform the frame at all.
inline ChassisFlex chassis_flex(const ChassisGeometry &geometry,
		double torsion_nm_per_deg = FRAME_TORSION_NM_PER_DEG) {
	double rate_sum = 0.0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		rate_sum += corner_setup(geometry, corner).spring_rate;
	}
	const double mean_rate = rate_sum * 0.25;

	ChassisFlex flex;
	flex.heave_rate = mean_rate;
	flex.pitch_rate = mean_rate;
	flex.roll_rate = mean_rate;

	// The generalized rate is per unit warp amplitude across all four corners;
	// the modal convention above is per corner, hence the quarter.
	const double frame_rate = warp_generalized_stiffness(torsion_nm_per_deg,
								   geometry.track_front, geometry.track_rear) *
			0.25;
	flex.warp_rate = (frame_rate * mean_rate) / (frame_rate + mean_rate);
	return flex;
}

// ---------------------------------------------------------------------------
// The quasi-static corner loads
// ---------------------------------------------------------------------------

// A steady cornering condition to solve for.
//
// Quasi-static on purpose. The runtime solver integrates; this predicts the
// steady state analytically so the tests can measure "at what lateral g does the
// inside rear lift" without running a kart around a circle, and so the tuning UI
// and the M3b validation scenarios have something to check the driven figure
// against. It is the skidpad, solved.
struct LoadCase {
	ChassisGeometry geometry;

	// Vertical rate of each corner's contact, N/m.
	double corner_rate[CORNER_COUNT] = {
		TIRE_RATE_FRONT_N_PER_M, TIRE_RATE_FRONT_N_PER_M,
		TIRE_RATE_REAR_N_PER_M, TIRE_RATE_REAR_N_PER_M
	};

	// Frame torsional stiffness, N·m/deg. Raising it toward infinity gives the
	// torsionally rigid kart; lowering it to zero gives two independent halves,
	// and the solver is singular there on three wheels, which is physically
	// correct — a hinged frame on three contacts is a mechanism, not a structure.
	double torsion_nm_per_deg = FRAME_TORSION_NM_PER_DEG;

	// Lateral acceleration, in g. Positive transfers load to the right, so the
	// left-hand corners are the inside ones.
	double lateral_g = 0.0;

	// Longitudinal acceleration, in g. Positive is accelerating, which transfers
	// load rearward.
	double longitudinal_g = 0.0;

	// Per-corner geometric offset, meters, positive lifting the chassis. This is
	// where `steering.h`'s caster jacking arrives. On flat ground it is the only
	// thing that excites the warp mode, and therefore the only thing that can
	// move the load split at a fixed lateral g.
	double geometric_offset[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
};

struct CornerLoads {
	double normal_force[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };

	// Tire deflection from the free length, meters. Negative means the corner is
	// off the ground and the magnitude is how far.
	double deflection[CORNER_COUNT] = { 0.0, 0.0, 0.0, 0.0 };

	bool grounded[CORNER_COUNT] = { true, true, true, true };

	// Set when a second corner wanted to leave the ground.
	//
	// There is no equilibrium there. A kart on two wheels is either exactly at
	// its rollover threshold or past it, and the solver says so rather than
	// returning a load split that sums to something other than the kart's weight
	// — which is what it did before this flag existed, and it was out by 300 N
	// with nothing to say anything had gone wrong.
	bool tipping = false;

	// The internal frame warp amplitude, meters. Positive twists the frame so
	// that FL and RR rise relative to FR and RL.
	double frame_warp = 0.0;

	// How far the corner is off the ground, meters. Zero when it is loaded. This
	// is the number issue #32 is judged on.
	double lift(int corner) const {
		return deflection[corner] < 0.0 ? -deflection[corner] : 0.0;
	}

	double total() const {
		double sum = 0.0;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			sum += normal_force[corner];
		}
		return sum;
	}
};

namespace detail {

// Gaussian elimination with partial pivoting on a 4x4. No allocation, no
// library, and a fixed pivot order so two runs cannot disagree.
inline bool solve4(double matrix[4][4], double rhs[4], double solution[4]) {
	for (int column = 0; column < 4; ++column) {
		int pivot = column;
		double best = std::fabs(matrix[column][column]);
		for (int row = column + 1; row < 4; ++row) {
			const double candidate = std::fabs(matrix[row][column]);
			if (candidate > best) {
				best = candidate;
				pivot = row;
			}
		}
		if (best < 1e-12) {
			return false;
		}
		if (pivot != column) {
			for (int k = 0; k < 4; ++k) {
				const double swap = matrix[column][k];
				matrix[column][k] = matrix[pivot][k];
				matrix[pivot][k] = swap;
			}
			const double swap = rhs[column];
			rhs[column] = rhs[pivot];
			rhs[pivot] = swap;
		}
		for (int row = column + 1; row < 4; ++row) {
			const double factor = matrix[row][column] / matrix[column][column];
			if (factor == 0.0) {
				continue;
			}
			for (int k = column; k < 4; ++k) {
				matrix[row][k] -= factor * matrix[column][k];
			}
			rhs[row] -= factor * rhs[column];
		}
	}
	for (int row = 3; row >= 0; --row) {
		double sum = rhs[row];
		for (int k = row + 1; k < 4; ++k) {
			sum -= matrix[row][k] * solution[k];
		}
		solution[row] = sum / matrix[row][row];
	}
	return true;
}

} // namespace detail

// Solve the four corner loads for a steady cornering condition.
//
// ## The four equations
//
// The chassis has four generalized coordinates: heave, roll gradient, pitch
// gradient, and the internal frame warp. The tire deflection at corner `i` is
//
//     u_i = q0 + q1 * x_i + q2 * z_i + q3 * w_i + offset_i
//
// with `x` lateral, `z` longitudinal, `w` the warp signs. The first three are
// the rigid pose; `q3` is the frame twisting, and it is the coordinate that
// resolves the indeterminacy. The four equations are the three rigid-body
// balances plus one internal one:
//
//     sum N_i           = m*g
//     sum N_i * x_i     = m*a_y*h
//     sum N_i * z_i     = m*a_x*h
//     sum N_i * w_i + S*q3 = Q_w
//
// `Q_w` is the generalized force conjugate to warp, and it is where the mass
// distribution enters. The lateral inertia force at each end does work when that
// end rolls, and warp rolls the two ends by different amounts, so
//
//     Q_w = 2 * (M_rear/track_rear - M_front/track_front)
//
// with `M_front = front_mass_share * m * a_y * h`. Setting `S` to zero and
// solving reproduces the two-body model of a hinged frame exactly, and setting it
// to infinity reproduces the rigid one; that equivalence was checked by hand
// before this was written, because a warp generalized force derived wrongly is a
// model that looks fine and lifts the wrong wheel.
//
// ## Wheels that leave the ground
//
// Solved by iteration, not by clamping. Solve with all four grounded; if any load
// came out negative, drop the most negative corner and solve again with three.
// Lowest index wins a tie, so the order is fixed.
//
// It stops there. If a second corner also wants to leave, the kart is at or past
// the rollover threshold and there is no equilibrium to find — two contact points
// and a torsion spring do not span the four coordinates, and the 4x4 goes very
// nearly singular. Partial pivoting will happily return a number for it, and that
// number was out by 300 N of the kart's weight. `CornerLoads::tipping` says so
// instead.
inline CornerLoads solve_corner_loads(const LoadCase &load_case) {
	const ChassisGeometry &geometry = load_case.geometry;
	const double lateral = load_case.lateral_g * G;
	const double longitudinal = load_case.longitudinal_g * G;
	const double roll_moment = geometry.mass * lateral * geometry.com_height;

	const double front_moment = geometry.front_mass_share * roll_moment;
	const double rear_moment = (1.0 - geometry.front_mass_share) * roll_moment;
	const double warp_force = 2.0 * (rear_moment / geometry.track_rear -
											front_moment / geometry.track_front);

	const double frame_rate = warp_generalized_stiffness(load_case.torsion_nm_per_deg,
			geometry.track_front, geometry.track_rear);

	double basis[4][CORNER_COUNT];
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		basis[0][corner] = 1.0;
		basis[1][corner] = geometry.corner_x(corner);
		basis[2][corner] = geometry.corner_z(corner);
		basis[3][corner] = MODE_WARP[corner];
	}

	bool grounded[CORNER_COUNT] = { true, true, true, true };
	CornerLoads loads;

	// Two passes: all four down, then three. See the note above on why there is
	// no third.
	for (int pass = 0; pass < 2; ++pass) {
		double matrix[4][4] = {};
		double rhs[4] = {
			geometry.mass * G,
			roll_moment,
			geometry.mass * longitudinal * geometry.com_height,
			warp_force
		};

		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				double sum = 0.0;
				for (int corner = 0; corner < CORNER_COUNT; ++corner) {
					if (!grounded[corner]) {
						continue;
					}
					sum += load_case.corner_rate[corner] * basis[column][corner] *
							basis[row][corner];
				}
				matrix[row][column] = sum;
			}
			double offset_sum = 0.0;
			for (int corner = 0; corner < CORNER_COUNT; ++corner) {
				if (!grounded[corner]) {
					continue;
				}
				offset_sum += load_case.corner_rate[corner] *
						load_case.geometric_offset[corner] * basis[row][corner];
			}
			rhs[row] -= offset_sum;
		}
		// The frame's own warp spring. Without it a three-wheeled stance is
		// singular, which is the correct statement about a hinged frame and the
		// reason this term cannot be dropped as a simplification.
		matrix[3][3] += frame_rate;

		double solution[4] = {};
		if (!detail::solve4(matrix, rhs, solution)) {
			return loads;
		}

		int worst = -1;
		double worst_force = 0.0;
		for (int corner = 0; corner < CORNER_COUNT; ++corner) {
			double deflection = load_case.geometric_offset[corner];
			for (int column = 0; column < 4; ++column) {
				deflection += solution[column] * basis[column][corner];
			}
			loads.deflection[corner] = deflection;
			loads.grounded[corner] = grounded[corner] && deflection > 0.0;
			const double force = grounded[corner] ? load_case.corner_rate[corner] * deflection
												  : 0.0;
			loads.normal_force[corner] = force > 0.0 ? force : 0.0;
			if (grounded[corner] && force < worst_force) {
				worst_force = force;
				worst = corner;
			}
		}
		loads.frame_warp = solution[3];

		if (worst < 0) {
			break;
		}
		if (pass == 1) {
			// A second corner wants to leave. Report it and stop.
			loads.tipping = true;
			loads.grounded[worst] = false;
			loads.normal_force[worst] = 0.0;
			break;
		}
		grounded[worst] = false;
	}
	return loads;
}

// Fraction of the roll stiffness the front axle contributes, for a torsionally
// rigid frame. `k * track^2 / 2` is one axle's roll stiffness on two contact
// springs.
//
// This is the number the ordinary vehicle-dynamics story is told in, and it is
// exposed so the tests can sweep it and so the story can be checked. It is a
// derived measurement here, not an input: on this kart it comes out of the front
// and rear tire rates and the two track widths, and the only way to change it is
// to change one of those.
inline double roll_stiffness_front_share(const ChassisGeometry &geometry,
		const double corner_rate[CORNER_COUNT]) {
	const double front = corner_rate[CORNER_FL] * geometry.track_front *
			geometry.track_front * 0.5;
	const double rear = corner_rate[CORNER_RL] * geometry.track_rear *
			geometry.track_rear * 0.5;
	const double total = front + rear;
	return total > 0.0 ? front / total : 0.0;
}

// Corner rates that produce a target front share of roll stiffness at a fixed
// mean rate. The inverse of the function above, for sweeping the split.
inline void corner_rates_for_roll_share(const ChassisGeometry &geometry, double front_share,
		double mean_rate, double corner_rate[CORNER_COUNT]) {
	const double front_track_squared = geometry.track_front * geometry.track_front;
	const double rear_track_squared = geometry.track_rear * geometry.track_rear;
	const double denominator = front_share / front_track_squared +
			(1.0 - front_share) / rear_track_squared;
	const double total = mean_rate / denominator;
	corner_rate[CORNER_FL] = 2.0 * front_share * total / front_track_squared;
	corner_rate[CORNER_FR] = corner_rate[CORNER_FL];
	corner_rate[CORNER_RL] = 2.0 * (1.0 - front_share) * total / rear_track_squared;
	corner_rate[CORNER_RR] = corner_rate[CORNER_RL];
}

// The lateral g at which a named corner's load first reaches zero.
//
// Found by bisection on the solver rather than in closed form, because the
// solver is piecewise linear — it changes shape the moment a wheel leaves — and
// a closed form would be right only on the first piece.
//
// For the **second** inside corner to lift, this returns the rollover threshold
// rather than a lift: once one inside wheel is off, the other only reaches zero
// when the kart goes over. That is the correct answer and it is worth knowing
// which of the two numbers you are reading.
inline double lift_threshold_g(LoadCase load_case, int corner, double search_limit = 6.0) {
	load_case.lateral_g = 0.0;
	if (solve_corner_loads(load_case).normal_force[corner] <= 0.0) {
		return 0.0;
	}
	load_case.lateral_g = search_limit;
	if (solve_corner_loads(load_case).normal_force[corner] > 0.0) {
		return search_limit;
	}
	double low = 0.0;
	double high = search_limit;
	for (int iteration = 0; iteration < 60; ++iteration) {
		const double middle = (low + high) * 0.5;
		load_case.lateral_g = middle;
		if (solve_corner_loads(load_case).normal_force[corner] > 0.0) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return (low + high) * 0.5;
}

} // namespace kart::core

#endif // KART_CORE_CHASSIS_FLEX_H
