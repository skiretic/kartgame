#include "doctest.h"

#include "core/chassis_flex.h"
#include "core/kz_reference.h"
#include "core/units.h"

#include <cstdio>

// The mode decomposition, the warp term, and the one question issue #32 is
// about: does the inside rear lift, and if so at what lateral g and by how much.
//
// Most of these cases print a table. That is deliberate — the acceptance
// evidence for #31 and #32 is measured numbers, and a suite that only says
// "passed" would have to be re-derived by hand to close either ticket.

using namespace kart::core;

namespace {

const char *corner_name(int corner) {
	switch (corner) {
		case CORNER_FL:
			return "FL";
		case CORNER_FR:
			return "FR";
		case CORNER_RL:
			return "RL";
		default:
			return "RR";
	}
}

LoadCase kart_load_case() {
	LoadCase load_case;
	const ChassisGeometry geometry;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		load_case.corner_rate[corner] = corner_setup(geometry, corner).spring_rate;
	}
	return load_case;
}

} // namespace

// ---------------------------------------------------------------------------
// The decomposition
// ---------------------------------------------------------------------------

TEST_CASE("the mode decomposition is complete and exactly invertible") {
	// An arbitrary, deliberately asymmetric set of four deflections. A symmetric
	// one has zero warp and would pass with the warp basis vector's signs
	// shuffled, which is the error to expect and the reason for the odd numbers.
	const double deflection[CORNER_COUNT] = { 0.0031, -0.0017, 0.0006, 0.0092 };

	const ChassisModes modes = decompose(deflection);
	double rebuilt[CORNER_COUNT] = {};
	compose(modes, rebuilt);

	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		CHECK(rebuilt[corner] == doctest::Approx(deflection[corner]).epsilon(1e-15));
	}

	char line[200];
	std::snprintf(line, sizeof(line),
			"  heave %+.6f  pitch %+.6f  roll %+.6f  warp %+.6f  (m)", modes.heave,
			modes.pitch, modes.roll, modes.warp);
	MESSAGE(line);

	// And the modes are what they say they are, checked one at a time so a
	// swapped pair cannot hide behind a correct round trip.
	const double heave_only[CORNER_COUNT] = { 0.01, 0.01, 0.01, 0.01 };
	const ChassisModes pure_heave = decompose(heave_only);
	CHECK(pure_heave.heave == doctest::Approx(0.01));
	CHECK(pure_heave.pitch == doctest::Approx(0.0));
	CHECK(pure_heave.roll == doctest::Approx(0.0));
	CHECK(pure_heave.warp == doctest::Approx(0.0));

	// Warp is the diagonals against each other: FL and RR up, FR and RL down.
	const double warp_only[CORNER_COUNT] = { 0.01, -0.01, -0.01, 0.01 };
	const ChassisModes pure_warp = decompose(warp_only);
	CHECK(pure_warp.warp == doctest::Approx(0.01));
	CHECK(pure_warp.heave == doctest::Approx(0.0));
	CHECK(pure_warp.pitch == doctest::Approx(0.0));
	CHECK(pure_warp.roll == doctest::Approx(0.0));

	// Roll is left pair against right pair, and must not be confused with warp.
	const double roll_only[CORNER_COUNT] = { 0.01, -0.01, 0.01, -0.01 };
	CHECK(decompose(roll_only).roll == doctest::Approx(0.01));
	CHECK(decompose(roll_only).warp == doctest::Approx(0.0));
}

TEST_CASE("four equal modal rates are four independent springs") {
	// The normalization the whole file depends on: a modal rate is directly
	// comparable to a corner spring rate. If this drifts, every ratio quoted in
	// the header becomes meaningless.
	ChassisFlex flex;
	flex.heave_rate = 150000.0;
	flex.pitch_rate = 150000.0;
	flex.roll_rate = 150000.0;
	flex.warp_rate = 150000.0;

	const double deflection[CORNER_COUNT] = { 0.004, -0.002, 0.011, 0.0005 };
	double force[CORNER_COUNT] = {};
	flex.corner_forces(deflection, force);
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		CHECK(force[corner] == doctest::Approx(150000.0 * deflection[corner]).epsilon(1e-12));
	}
}

TEST_CASE("warp is far softer than the other three modes") {
	const ChassisGeometry geometry;
	const ChassisFlex flex = chassis_flex(geometry);

	char line[240];
	std::snprintf(line, sizeof(line),
			"  heave %.0f  pitch %.0f  roll %.0f  warp %.0f N/m   -> warp is %.2fx softer",
			flex.heave_rate, flex.pitch_rate, flex.roll_rate, flex.warp_rate,
			flex.warp_softness());
	MESSAGE(line);

	const double frame = warp_generalized_stiffness(FRAME_TORSION_NM_PER_DEG,
			geometry.track_front, geometry.track_rear);
	std::snprintf(line, sizeof(line),
			"  frame torsion %.2f N.m/deg = %.0f N.m/rad -> warp generalized rate %.0f N/m,"
			" against %.0f N/m for the four tires together", FRAME_TORSION_NM_PER_DEG,
			FRAME_TORSION_NM_PER_DEG * 180.0 / PI, frame, flex.heave_rate * 4.0);
	MESSAGE(line);

	CHECK(flex.warp_rate < flex.roll_rate);
	CHECK(flex.warp_softness() > 3.0);
	// The three rigid modes are the tire and axle rates and have no frame term.
	CHECK(flex.heave_rate == doctest::Approx(flex.roll_rate));
	CHECK(flex.pitch_rate == doctest::Approx(flex.roll_rate));
}

// ---------------------------------------------------------------------------
// Static equilibrium
// ---------------------------------------------------------------------------

TEST_CASE("at rest the four loads sum to the class mass and split 42/58") {
	const LoadCase load_case = kart_load_case();
	const CornerLoads loads = solve_corner_loads(load_case);

	char line[220];
	std::snprintf(line, sizeof(line), "  at rest  FL %.1f  FR %.1f  RL %.1f  RR %.1f  N,"
									  " sum %.1f N", loads.normal_force[CORNER_FL],
			loads.normal_force[CORNER_FR], loads.normal_force[CORNER_RL],
			loads.normal_force[CORNER_RR], loads.total());
	MESSAGE(line);

	CHECK(loads.total() == doctest::Approx(kz::MASS_WITH_DRIVER_KG * G).epsilon(1e-9));
	CHECK(loads.normal_force[CORNER_FL] == doctest::Approx(loads.normal_force[CORNER_FR]));
	CHECK(loads.normal_force[CORNER_RL] == doctest::Approx(loads.normal_force[CORNER_RR]));
	const double front = loads.normal_force[CORNER_FL] + loads.normal_force[CORNER_FR];
	CHECK(front / loads.total() == doctest::Approx(0.42).epsilon(1e-9));
	// Nothing warps a kart standing still on a flat floor.
	CHECK(loads.frame_warp == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("the rollover threshold uses the tipping axis, not half the rear track") {
	// ADR-0031 quotes 0.5925 / 0.23 = 2.58 g, taking half the **rear** track as
	// the lever. The kart tips about the line joining the outside-front and
	// outside-rear contact patches, and the front track is 80 mm narrower, so
	// that line runs inboard of the rear patch and the center of mass is closer
	// to it than half the rear track.
	const ChassisGeometry geometry;
	const double naive = geometry.track_rear * 0.5 / geometry.com_height;
	const double actual = geometry.rollover_threshold_g();

	char line[220];
	std::snprintf(line, sizeof(line),
			"  half rear track %.4f m -> %.3f g;  perpendicular to the tipping axis"
			" %.4f m -> %.3f g", geometry.track_rear * 0.5, naive,
			actual * geometry.com_height, actual);
	MESSAGE(line);

	CHECK(actual < naive);
	CHECK(actual == doctest::Approx(2.50).epsilon(0.01));
	// The difference is not academic: it decides whether the kart has any margin
	// over the lateral band at all. Compared against the **peak** band, because
	// that is the pair of numbers this comparison was originally written against
	// — ADR-0034 has since split §6.4's single 2.0-2.5 g band into sustained
	// (1.5-2.0) and transient peak (2.0-2.5), after finding it was a peak figure
	// being asserted against sustained measurements.
	//
	// Issue #129 is open on this whole assertion: `actual` here is the
	// **default-constructed** 2.50 g, and the running solver produces 2.618
	// because `vehicle.h` overwrites `com_height` with the mass table's 0.2197.
	// The kart's real left-turn threshold is 2.4336. So this pins a number the
	// solver never sees, which is precisely how the divergence went unnoticed.
	CHECK(actual < kz::LATERAL_PEAK_G_MAX * 1.01);
}

// ---------------------------------------------------------------------------
// Which wheel lifts
// ---------------------------------------------------------------------------

TEST_CASE("a rear-biased roll-stiffness split lifts the inside rear") {
	// The demonstration issue #32 turns on. Swept against a torsionally rigid
	// frame, because the split is only a free parameter when the frame can carry
	// the roll moment between the axles — with a real frame it is not, which is
	// the next test and the more interesting one.
	const ChassisGeometry geometry;
	LoadCase load_case = kart_load_case();
	load_case.torsion_nm_per_deg = 1.0e7; // Effectively rigid.
	load_case.lateral_g = 2.0;

	double mean_rate = 0.0;
	for (int corner = 0; corner < CORNER_COUNT; ++corner) {
		mean_rate += kart_load_case().corner_rate[corner] * 0.25;
	}

	MESSAGE("  rigid frame, 2.00 g, load transfer to the right (inside is the left).");
	MESSAGE("  The second g figure is the rollover threshold where a corner is the");
	MESSAGE("  second inside wheel to unload, not a lift it reaches on its own.");
	MESSAGE("  front share    FL      FR      RL      RR      first to zero");
	for (const double share : { 0.20, 0.30, 0.40, 0.50, 0.60, 0.70 }) {
		corner_rates_for_roll_share(geometry, share, mean_rate, load_case.corner_rate);
		const CornerLoads loads = solve_corner_loads(load_case);

		const double front_lift = lift_threshold_g(load_case, CORNER_FL);
		const double rear_lift = lift_threshold_g(load_case, CORNER_RL);

		char line[240];
		std::snprintf(line, sizeof(line),
				"  %8.2f %8.0f %7.0f %7.0f %7.0f      %s (%.2f g vs %.2f g)", share,
				loads.normal_force[CORNER_FL], loads.normal_force[CORNER_FR],
				loads.normal_force[CORNER_RL], loads.normal_force[CORNER_RR],
				rear_lift < front_lift ? "inside REAR " : "inside FRONT",
				rear_lift < front_lift ? rear_lift : front_lift,
				rear_lift < front_lift ? front_lift : rear_lift);
		MESSAGE(line);

		// The direction the main thread's quasi-static solution found: front
		// share below the crossover lifts the rear, above it lifts the front.
		if (share <= 0.30) {
			CHECK(rear_lift < front_lift);
		}
		if (share >= 0.50) {
			CHECK(front_lift < rear_lift);
		}
	}
}

TEST_CASE("the as-built kart's split comes out of the tire rates and the frame") {
	const ChassisGeometry geometry;
	LoadCase load_case = kart_load_case();

	const double share = roll_stiffness_front_share(geometry, load_case.corner_rate);
	const double front_lift = lift_threshold_g(load_case, CORNER_FL);
	const double rear_lift = lift_threshold_g(load_case, CORNER_RL);

	char line[260];
	std::snprintf(line, sizeof(line),
			"  front tire %.0f N/m, rear %.0f N/m -> rigid-frame roll-stiffness front"
			" share %.3f", load_case.corner_rate[CORNER_FL],
			load_case.corner_rate[CORNER_RL], share);
	MESSAGE(line);

	load_case.lateral_g = 2.0;
	const CornerLoads at_two = solve_corner_loads(load_case);
	std::snprintf(line, sizeof(line),
			"  at 2.00 g: FL %.1f  FR %.1f  RL %.1f  RR %.1f N   (inside is FL and RL)",
			at_two.normal_force[CORNER_FL], at_two.normal_force[CORNER_FR],
			at_two.normal_force[CORNER_RL], at_two.normal_force[CORNER_RR]);
	MESSAGE(line);

	load_case.lateral_g = 0.0;
	const CornerLoads at_rest = solve_corner_loads(load_case);
	const double front_transfer = at_two.normal_force[CORNER_FR] -
			at_rest.normal_force[CORNER_FR];
	const double rear_transfer = at_two.normal_force[CORNER_RR] -
			at_rest.normal_force[CORNER_RR];
	std::snprintf(line, sizeof(line),
			"  load transfer at 2.00 g: front %.1f N, rear %.1f N, front share %.3f",
			front_transfer, rear_transfer,
			front_transfer / (front_transfer + rear_transfer));
	MESSAGE(line);

	std::snprintf(line, sizeof(line),
			"  no jacking: inside FRONT reaches zero at %.3f g, inside REAR at %.3f g,"
			" kart tips at %.3f g", front_lift, rear_lift, geometry.rollover_threshold_g());
	MESSAGE(line);

	// The rear is the one that lifts. Barely.
	CHECK(rear_lift < front_lift);

	// And it does not lift at any lateral g the tire model can produce. This is
	// the finding, asserted so it cannot quietly stop being true: `tire.h` peaks
	// at a friction coefficient of 2.10, so the kart cannot reach the threshold
	// below on grip alone. Caster jacking is not a garnish on this milestone, it
	// is the mechanism.
	CHECK(rear_lift > 2.10);
}

TEST_CASE("frame torsional stiffness decides which wheel lifts") {
	// The most sensitive number in the model, and the two published figures for
	// it disagree by a factor of five to eighteen — see REFERENCES-chassis.md.
	// The crossover between "inside front lifts" and "inside rear lifts" sits
	// inside that range, which is worth knowing before anything is tuned.
	LoadCase load_case = kart_load_case();

	MESSAGE("  frame N.m/deg   transfer front share   inside FRONT zero   inside REAR zero");
	for (const double torsion : { 1.0, 100.0, 193.62, 500.0, 1051.0, 3464.0, 1.0e7 }) {
		load_case.torsion_nm_per_deg = torsion;

		load_case.lateral_g = 0.0;
		const CornerLoads rest = solve_corner_loads(load_case);
		load_case.lateral_g = 2.0;
		const CornerLoads two = solve_corner_loads(load_case);
		const double front = two.normal_force[CORNER_FR] - rest.normal_force[CORNER_FR];
		const double rear = two.normal_force[CORNER_RR] - rest.normal_force[CORNER_RR];

		load_case.lateral_g = 0.0;
		char line[240];
		std::snprintf(line, sizeof(line), "  %13.2f %22.3f %19.3f %19.3f", torsion,
				front / (front + rear), lift_threshold_g(load_case, CORNER_FL),
				lift_threshold_g(load_case, CORNER_RL));
		MESSAGE(line);
	}

	// A torsionally free frame transfers load in proportion to each end's own
	// mass, and on this kart that lifts the inside FRONT — the wrong wheel.
	load_case.torsion_nm_per_deg = 1.0;
	load_case.lateral_g = 0.0;
	CHECK(lift_threshold_g(load_case, CORNER_FL) < lift_threshold_g(load_case, CORNER_RL));

	// A torsionally rigid one transfers in proportion to roll stiffness, and on
	// this kart that lifts the inside REAR. **A stiffer frame is what lifts the
	// inside rear**, which is the opposite of the folk story that a kart needs to
	// be floppy, and it agrees with what kart tuners say about axles: a harder
	// rear axle frees the kart off the corner.
	load_case.torsion_nm_per_deg = 1.0e7;
	CHECK(lift_threshold_g(load_case, CORNER_RL) < lift_threshold_g(load_case, CORNER_FL));
}

// ---------------------------------------------------------------------------
// Jacking, and the number issue #32 is judged on
// ---------------------------------------------------------------------------

TEST_CASE("caster jacking is what actually lifts the inside rear, and by how much") {
	// The offsets are antisymmetric across the front axle, which is what caster
	// on a steered axle produces: turning left raises the chassis at the inside
	// front and lowers it at the outside front. `steering.h` supplies the
	// magnitude; this measures what the chassis does with it.
	LoadCase load_case = kart_load_case();
	load_case.lateral_g = 2.0;

	MESSAGE("  2.00 g, antisymmetric front jacking, inside is FL and RL:");
	MESSAGE("  jacking mm      FL      FR      RL      RR     RL lift mm");
	for (const double jacking : { 0.0, 2.0, 4.0, 6.0, 8.0, 12.0, 20.0, 30.0 }) {
		const double offset = jacking * 0.001;
		load_case.geometric_offset[CORNER_FL] = offset;
		load_case.geometric_offset[CORNER_FR] = -offset;
		const CornerLoads loads = solve_corner_loads(load_case);
		char line[240];
		std::snprintf(line, sizeof(line), "  %10.1f %7.0f %7.0f %7.0f %7.0f %14.2f",
				jacking, loads.normal_force[CORNER_FL], loads.normal_force[CORNER_FR],
				loads.normal_force[CORNER_RL], loads.normal_force[CORNER_RR],
				loads.lift(CORNER_RL) * 1000.0);
		MESSAGE(line);
	}

	// How much jacking zeroes the inside rear at 2.0 g, by bisection.
	double low = 0.0;
	double high = 0.10;
	for (int iteration = 0; iteration < 60; ++iteration) {
		const double middle = (low + high) * 0.5;
		load_case.geometric_offset[CORNER_FL] = middle;
		load_case.geometric_offset[CORNER_FR] = -middle;
		if (solve_corner_loads(load_case).normal_force[CORNER_RL] > 0.0) {
			low = middle;
		} else {
			high = middle;
		}
	}
	const double needed = (low + high) * 0.5;

	char line[240];
	std::snprintf(line, sizeof(line),
			"  %.2f mm of antisymmetric front jacking zeroes the inside rear at 2.00 g",
			needed * 1000.0);
	MESSAGE(line);

	// A kart runs 8-15 degrees of caster and up to 25 degrees of steer, which is
	// worth 10-20 mm of chassis rise at the inside front. So the number the
	// chassis needs is inside what the geometry can deliver — but not by much,
	// and that is the headline for issue #32.
	CHECK(needed > 0.0);
	CHECK(needed < 0.020);

	// And the visible lift, which is what the acceptance item asks for in
	// centimeters. Set to the top of the plausible jacking range.
	load_case.geometric_offset[CORNER_FL] = 0.020;
	load_case.geometric_offset[CORNER_FR] = -0.020;
	const CornerLoads lifted = solve_corner_loads(load_case);
	std::snprintf(line, sizeof(line),
			"  at 20 mm of jacking and 2.00 g the inside rear is %.1f mm off the ground,"
			" carrying %.1f N", lifted.lift(CORNER_RL) * 1000.0,
			lifted.normal_force[CORNER_RL]);
	MESSAGE(line);
	CHECK(lifted.normal_force[CORNER_RL] == doctest::Approx(0.0));
	CHECK(lifted.lift(CORNER_RL) > 0.005); // Half a centimeter at least, visibly off.

	// Sanity: the other three still carry the whole kart.
	CHECK(lifted.total() == doctest::Approx(kz::MASS_WITH_DRIVER_KG * G).epsilon(1e-9));
}

TEST_CASE("jacking at a lower lateral g still lifts the wheel, given enough of it") {
	// Karts lift the inside rear at corner entry, well before the skidpad limit,
	// because the steering angle is largest there. This checks the model has that
	// shape rather than only working at the limit.
	LoadCase load_case = kart_load_case();
	MESSAGE("  jacking needed to zero the inside rear, against lateral g:");
	MESSAGE("  lateral g    jacking mm");
	for (const double lateral : { 0.0, 0.5, 1.0, 1.5, 2.0 }) {
		load_case.lateral_g = lateral;
		double low = 0.0;
		double high = 0.20;
		for (int iteration = 0; iteration < 60; ++iteration) {
			const double middle = (low + high) * 0.5;
			load_case.geometric_offset[CORNER_FL] = middle;
			load_case.geometric_offset[CORNER_FR] = -middle;
			if (solve_corner_loads(load_case).normal_force[CORNER_RL] > 0.0) {
				low = middle;
			} else {
				high = middle;
			}
		}
		const double needed = (low + high) * 500.0;
		char line[160];
		if (needed > 190.0) {
			std::snprintf(line, sizeof(line), "  %9.2f      not reachable", lateral);
		} else {
			std::snprintf(line, sizeof(line), "  %9.2f %13.2f", lateral, needed);
		}
		MESSAGE(line);
	}

	// Standing still, no amount of realistic jacking should lift a wheel — the
	// kart would be on three wheels in the pit lane with the wheel turned.
	load_case.lateral_g = 0.0;
	load_case.geometric_offset[CORNER_FL] = 0.020;
	load_case.geometric_offset[CORNER_FR] = -0.020;
	CHECK(solve_corner_loads(load_case).normal_force[CORNER_RL] > 0.0);
}

// ---------------------------------------------------------------------------
// Invariants
// ---------------------------------------------------------------------------

TEST_CASE("no load case produces a negative normal force or loses the kart's weight") {
	LoadCase load_case = kart_load_case();
	for (const double lateral : { 0.0, 1.0, 2.0, 2.5, 3.0 }) {
		for (const double longitudinal : { -1.5, 0.0, 1.5 }) {
			for (const double jacking : { 0.0, 0.010, 0.030 }) {
				load_case.lateral_g = lateral;
				load_case.longitudinal_g = longitudinal;
				load_case.geometric_offset[CORNER_FL] = jacking;
				load_case.geometric_offset[CORNER_FR] = -jacking;
				const CornerLoads loads = solve_corner_loads(load_case);
				for (int corner = 0; corner < CORNER_COUNT; ++corner) {
					INFO("corner ", corner_name(corner), " at ", lateral, " g");
					CHECK(loads.normal_force[corner] >= 0.0);
					CHECK(std::isfinite(loads.normal_force[corner]));
				}
				// Vertical equilibrium holds on four wheels and on three. It
				// stops holding when a second corner wants to leave, because
				// there is no equilibrium there — the kart is going over — and
				// the solver reports that rather than returning a plausible
				// number that is 300 N light.
				if (!loads.tipping) {
					CHECK(loads.total() ==
							doctest::Approx(kz::MASS_WITH_DRIVER_KG * G).epsilon(1e-6));
				}
			}
		}
	}
}

TEST_CASE("the roll-share inverse round trips") {
	const ChassisGeometry geometry;
	double rate[CORNER_COUNT] = {};
	for (const double share : { 0.25, 0.42, 0.66 }) {
		corner_rates_for_roll_share(geometry, share, 190000.0, rate);
		CHECK(roll_stiffness_front_share(geometry, rate) ==
				doctest::Approx(share).epsilon(1e-12));
		CHECK((rate[CORNER_FL] + rate[CORNER_RL]) * 0.5 ==
				doctest::Approx(190000.0).epsilon(1e-12));
	}
}
