#include "core/racing_line.h"

#include "core/chassis.h"
#include "core/tire.h"
#include "core/units.h"

#include <doctest.h>

#include <cmath>
#include <memory>

// Tests for `src/core/racing_line.h`. Engine-free; `tests/run.sh` runs them.
//
// ## What is asserted and what deliberately is not
//
// Issue #137 is open and the tire model is expected to move. So **no test here
// names a lateral acceleration, a corner speed or a lap time.** What is asserted
// is a set of relations that hold whatever `tire.h` says:
//
//   * the profile never demands more grip than the model permits, on either
//     axis, with the model re-derived here from `tire.h` and `chassis.h`
//     directly rather than read back out of the class under test;
//   * more grip never produces a slower corner;
//   * the rollover ceiling is geometry and clamps the tire when the tire is
//     good enough to reach it;
//   * the line is inside the corridor and its curvature integral beats the
//     centerline's.
//
// Written the other way round - "Il Pozzo is taken at 71 km/h" - every one of
// these would go red on the day the scrub bug was fixed, and nobody reading the
// failure would be able to tell that from a regression.
//
// ## Why the analytic case is a circle
//
// On a circular corridor the minimum-curvature closed line is the **outer**
// circle, and that is provable rather than plausible: every closed curve inside
// the annulus turns 2*pi, so by Cauchy-Schwarz the integral of k^2 is at least
// (2*pi)^2 / L and is minimized by the longest admissible curve. It gives the
// optimizer an answer correct to six figures to be checked against, which no
// racetrack shape can.

using namespace kart::core;
using namespace kart::core::line;

namespace {

// A centerline walker: straights and constant-curvature arcs, evaluated exactly
// the way `track.h::sample` does. Enough to build every fixture below.
struct Walk {
	struct Segment {
		double length = 0.0;
		double curvature = 0.0;
	};
	Segment segments[8];
	int count = 0;
	double total = 0.0;

	void add(double length, double curvature) {
		segments[count].length = length;
		segments[count].curvature = curvature;
		++count;
		total += length;
	}

	void at(double distance, double &x, double &z, double &heading, double &curvature) const {
		x = 0.0;
		z = 0.0;
		heading = 0.0;
		curvature = 0.0;
		double left = distance;
		for (int index = 0; index < count; ++index) {
			const double take = left < segments[index].length ? left : segments[index].length;
			const double k = segments[index].curvature;
			if (k == 0.0) {
				x += std::sin(heading) * take;
				z += -std::cos(heading) * take;
			} else {
				const double radius = 1.0 / k;
				const double centre_x = x + std::cos(heading) * radius;
				const double centre_z = z + std::sin(heading) * radius;
				heading += k * take;
				x = centre_x - std::cos(heading) * radius;
				z = centre_z - std::sin(heading) * radius;
			}
			left -= take;
			if (left <= 1e-12) {
				curvature = k;
				return;
			}
		}
		curvature = segments[count - 1].curvature;
	}
};

// `RacingLine` is about 150 kB of arrays and must not go on a stack frame.
std::unique_ptr<RacingLine> build(const Walk &walk, double width_m, double spacing_m = 1.5,
		double bank = 0.0, double grade = 0.0) {
	auto line = std::unique_ptr<RacingLine>(new RacingLine());
	const int count = line->begin(walk.total, spacing_m);
	for (int index = 0; index < count; ++index) {
		double x = 0.0;
		double z = 0.0;
		double heading = 0.0;
		double curvature = 0.0;
		walk.at(line->station_distance(index), x, z, heading, curvature);
		line->set_station(index, x, z, heading, curvature, 0.5 * width_m, 0.0, grade, bank);
	}
	return line;
}

Walk circle_walk(double radius) {
	Walk walk;
	walk.add(2.0 * PI * radius, 1.0 / radius);
	return walk;
}

// Two long straights joined by two 180 degree right-handers.
Walk paperclip_walk() {
	Walk walk;
	walk.add(220.0, 0.0);
	walk.add(PI * 30.0, 1.0 / 30.0);
	walk.add(220.0, 0.0);
	walk.add(PI * 30.0, 1.0 / 30.0);
	return walk;
}

// The test track's two shapes, mirrored so the lap closes: a pair of 11 m
// left-hand hairpins, which is where issue #137 lives and where the left-hand
// rollover threshold is the lower of the two.
Walk hairpin_walk() {
	Walk walk;
	walk.add(300.0, 0.0);
	walk.add(PI * 11.0, -1.0 / 11.0);
	walk.add(300.0, 0.0);
	walk.add(PI * 11.0, -1.0 / 11.0);
	return walk;
}

// The lateral ceiling, re-derived here from `tire.h` and `chassis.h` with no
// reference to the class under test. If this and `SpeedModel::lateral_limit`
// disagree, one of them is wrong and the test says which numbers.
double independent_lateral_limit(const Tire &tire, bool turning_left, double usage = 1.0) {
	const MassProperties properties = kz::kart_mass_properties();
	const double mass = properties.mass;
	const double height = properties.center_of_mass.y;
	const double arm_left = kz::rollover_threshold_g(properties, true) * height;
	const double arm_right = kz::rollover_threshold_g(properties, false) * height;
	const double arm = turning_left ? arm_left : arm_right;
	const double track = arm_left + arm_right;
	const double rollover = G * arm / height;

	double a = G;
	for (int iteration = 0; iteration < 200; ++iteration) {
		const double weight = mass * G;
		double transfer = mass * a * height / track;
		if (transfer < 0.0) {
			transfer = 0.0;
		}
		double outer = weight * (track - arm) / track + transfer;
		double inner = weight * arm / track - transfer;
		if (inner < 0.0) {
			inner = 0.0;
			outer = weight;
		}
		const double per_outer = 0.5 * outer;
		const double per_inner = 0.5 * inner;
		const double capacity = usage * (2.0 * tire.lateral.friction_at(per_outer) * per_outer +
											   2.0 * tire.lateral.friction_at(per_inner) * per_inner);
		const double next = capacity / mass;
		if (std::fabs(next - a) < 1e-11) {
			a = next;
			break;
		}
		a = 0.5 * (a + next);
	}
	return a < rollover ? a : rollover;
}

// An explicit relative comparison. `doctest::Approx(x).epsilon(e)` is not one -
// it compares against `e * (1 + max(|a|,|b|))`, which on a value of 0.03 makes
// an epsilon of 0.06 a tolerance of 0.062. It once passed a test wrong by 46%.
bool close_relative(double a, double b, double tolerance) {
	const double scale = std::fabs(a) > std::fabs(b) ? std::fabs(a) : std::fabs(b);
	if (scale < 1e-12) {
		return std::fabs(a - b) < 1e-12;
	}
	return std::fabs(a - b) / scale <= tolerance;
}

} // namespace

TEST_CASE("the speed model agrees with the headers it was read from") {
	const SpeedModel model = default_speed_model();
	const MassProperties properties = kz::kart_mass_properties();

	CHECK(model.mass_kg == doctest::Approx(properties.mass));
	CHECK(model.com_height_m == doctest::Approx(properties.center_of_mass.y));

	// The arms have to reproduce `chassis.h`'s own thresholds, or every rollover
	// number below is measured against a kart this project does not have.
	const double left = model.rollover_arm_left_m / model.com_height_m;
	const double right = model.rollover_arm_right_m / model.com_height_m;
	CHECK(close_relative(left, kz::rollover_threshold_g(properties, true), 1e-12));
	CHECK(close_relative(right, kz::rollover_threshold_g(properties, false), 1e-12));

	// And they must differ, because the engine hangs off the right. A model that
	// lost the asymmetry would pass every other test in this file.
	CHECK(right > left * 1.05);

	// Top gear at the hard cut has to sit above `kz_reference.h`'s top-speed
	// band, or the gearing cannot reach the speeds the class does.
	CHECK(ms_to_kmh(model.speed_ceiling()) > kz::TOP_SPEED_MAX_KMH);
}

TEST_CASE("the lateral ceiling matches an independent derivation") {
	const SpeedModel model = default_speed_model();
	const Tire tire;
	for (int hand = 0; hand < 2; ++hand) {
		const bool left = hand == 0;
		const double mine = model.lateral_limit(0.0, left);
		const double theirs = independent_lateral_limit(tire, left);
		CHECK(close_relative(mine, theirs, 1e-9));
	}
}

TEST_CASE("rollover is geometry and takes over when the tire is good enough") {
	SpeedModel model = default_speed_model();
	const MassProperties properties = kz::kart_mass_properties();

	double tire_g = 0.0;
	double rollover_g = 0.0;
	model.lateral_limit(0.0, true, &tire_g, &rollover_g);
	// With the tire as it stands the tire binds, not the geometry.
	CHECK(tire_g < rollover_g);

	// The rollover ceiling is exactly `chassis.h`'s threshold and nothing else.
	CHECK(close_relative(rollover_g / G, kz::rollover_threshold_g(properties, true), 1e-12));

	// Now make the tire absurd. The ceiling must stop at the tipping point and
	// the two hands must part company by the amount the offset engine says.
	model.tire.lateral.peak_friction = 8.0;
	const double clamped_left = model.lateral_limit(0.0, true) / G;
	const double clamped_right = model.lateral_limit(0.0, false) / G;
	CHECK(close_relative(clamped_left, kz::rollover_threshold_g(properties, true), 1e-12));
	CHECK(close_relative(clamped_right, kz::rollover_threshold_g(properties, false), 1e-12));
	CHECK(clamped_right > clamped_left);

	// A bank into the turn raises both ceilings. 8% is what Valdirone authors.
	CHECK(model.lateral_limit(0.08, true) > model.lateral_limit(0.0, true));
	// And an adverse one lowers them, which is the sign convention's own test.
	CHECK(model.lateral_limit(-0.08, true) < model.lateral_limit(0.0, true));
}

TEST_CASE("a circular corridor is solved to its analytic answer") {
	const double radius = 50.0;
	const double width = 10.0;
	auto line = build(circle_walk(radius), width);
	const SpeedModel model = default_speed_model();
	const LineSummary &summary = line->solve(model);

	const double half = 0.5 * width - DEFAULT_EDGE_MARGIN_M;
	REQUIRE(half > 0.0);

	// The optimum is the outer circle. This corridor turns right, so its outside
	// is the driver's left, which is a negative offset.
	for (int index = 0; index < line->station_count(); ++index) {
		CHECK(close_relative(line->offset(index), -half, 1e-6));
		CHECK(close_relative(line->curvature(index), 1.0 / (radius + half), 1e-5));
	}
	CHECK(close_relative(summary.min_radius_m, radius + half, 1e-5));
	CHECK(close_relative(summary.line_length_m, 2.0 * PI * (radius + half), 1e-4));

	// And Cauchy-Schwarz's bound is attained, because the curvature is constant.
	const double bound = (2.0 * PI) * (2.0 * PI) / summary.line_length_m;
	CHECK(close_relative(summary.line_objective, bound, 1e-4));
	CHECK(summary.line_objective < summary.centerline_objective);
}

TEST_CASE("the line stays inside the corridor and beats the centerline") {
	const double width = 10.0;
	auto line = build(paperclip_walk(), width);
	const SpeedModel model = default_speed_model();
	const LineSummary &summary = line->solve(model);

	const double half = 0.5 * width - DEFAULT_EDGE_MARGIN_M;
	for (int index = 0; index < line->station_count(); ++index) {
		CHECK(std::fabs(line->offset(index)) <= half + 1e-9);
	}
	CHECK(summary.corridor_slack_m >= -1e-9);
	CHECK(summary.line_objective < summary.centerline_objective);
	// The corner is opened out, not tightened.
	CHECK(summary.min_radius_m > summary.centerline_min_radius_m);

	// The last rung of the cascade is the best one it found.
	REQUIRE(summary.levels >= 2);
	for (int level = 0; level < summary.levels - 1; ++level) {
		CHECK(summary.level_objective[summary.levels - 1] <= summary.level_objective[level] + 1e-12);
	}
	CHECK(close_relative(summary.level_objective[summary.levels - 1], summary.line_objective, 1e-12));
}

TEST_CASE("the cascade is what solves it, not the sweep budget") {
	// The whole justification for nested iteration, measured. Gauss-Seidel on a
	// biharmonic converges like N^4, so the same budget spent on the fine grid
	// alone barely moves off the centerline.
	const Walk walk = paperclip_walk();
	const SpeedModel model = default_speed_model();

	auto cascaded = build(walk, 10.0);
	LineOptions with_cascade;
	const double good = cascaded->solve(model, with_cascade).line_objective;

	auto flat = build(walk, 10.0);
	LineOptions no_cascade;
	no_cascade.coarsest_stations = 4096; // clamped to the station count: one level
	const double poor = flat->solve(model, no_cascade).line_objective;

	const double centerline = cascaded->summary().centerline_objective;
	CHECK(good < poor);
	// The cascade gets most of the way; the flat run gets a small fraction of it.
	CHECK((centerline - good) > 3.0 * (centerline - poor));
}

TEST_CASE("turning the optimizer off leaves the line on the centerline") {
	auto line = build(paperclip_walk(), 10.0);
	LineOptions options;
	options.optimize = false;
	const SpeedModel model = default_speed_model();
	const LineSummary &summary = line->solve(model, options);
	for (int index = 0; index < line->station_count(); ++index) {
		CHECK(line->offset(index) == doctest::Approx(0.0));
	}
	// The measured curvature of a zero-offset line is the centerline's, which is
	// also a check on the Menger formula against `track.h`'s stored curvature.
	CHECK(close_relative(summary.min_radius_m, summary.centerline_min_radius_m, 1e-3));

	// **And the two objectives must be the same number, not merely close.** They
	// have to be the same integral or "the line beats the centerline" is a
	// comparison between two different quantities: the first cut summed the
	// stored `curvature^2 * spacing` for one and Menger-over-chords for the
	// other, 0.7% apart on Valdirone, so a line that had not moved a millimeter
	// "beat" the centerline by 0.7%. `line_probe.gd --break=noline` is the
	// negative control that found it.
	CHECK(summary.line_objective == summary.centerline_objective);
}

TEST_CASE("a varying corridor is respected station by station") {
	// Valdirone runs 9 m to 14 m. Nothing may assume `TrackRibbon.TRACK_WIDTH`.
	auto line = std::unique_ptr<RacingLine>(new RacingLine());
	const Walk walk = paperclip_walk();
	const int count = line->begin(walk.total, 1.5);
	double widths[MAX_STATIONS];
	for (int index = 0; index < count; ++index) {
		double x = 0.0;
		double z = 0.0;
		double heading = 0.0;
		double curvature = 0.0;
		walk.at(line->station_distance(index), x, z, heading, curvature);
		// A taper from 14 m down to 9 m and back, so every station has its own.
		const double phase = static_cast<double>(index) / static_cast<double>(count);
		widths[index] = 11.5 + 2.5 * std::cos(2.0 * PI * phase);
		line->set_station(index, x, z, heading, curvature, 0.5 * widths[index], 0.0, 0.0, 0.0);
	}
	const SpeedModel model = default_speed_model();
	const LineSummary &summary = line->solve(model);
	for (int index = 0; index < count; ++index) {
		const double half = 0.5 * widths[index] - DEFAULT_EDGE_MARGIN_M;
		CHECK(std::fabs(line->offset(index)) <= half + 1e-9);
	}
	// It used the narrow end too - a line that just sat in the middle would pass
	// the bound above and prove nothing.
	CHECK(summary.max_offset_m > 0.5 * 9.0 - DEFAULT_EDGE_MARGIN_M - 0.2);
	CHECK(summary.corridor_slack_m >= -1e-9);
}

TEST_CASE("the profile never demands more than the tire and the geometry permit") {
	const SpeedModel model = default_speed_model();
	const Tire tire;

	Walk shapes[3] = { circle_walk(50.0), paperclip_walk(), hairpin_walk() };
	for (int which = 0; which < 3; ++which) {
		auto line = build(shapes[which], 9.0);
		line->solve(model);

		CHECK(line->worst_lateral_utilization() <= 1.0 + 1e-6);
		// The friction ellipse, at both ends of every segment. This is the check
		// the two speed passes were given an implicit solve for; before it they
		// were self-consistent and 5.8% over the ellipse at a turn-in.
		CHECK(line->worst_combined_utilization() <= 1.0 + 1e-6);

		// And against a ceiling this file derived for itself, not one read back
		// out of the object under test.
		for (int index = 0; index < line->station_count(); ++index) {
			const double demand = line->lateral_demand(index);
			const bool left = line->curvature(index) < 0.0;
			CHECK(demand <= independent_lateral_limit(tire, left) * (1.0 + 1e-6));
		}
	}
}

TEST_CASE("more grip is never a slower corner") {
	// The property that makes this file survive issue #137. Whatever the fix
	// does to the tire, it may not make the racing line worse.
	const Walk walk = hairpin_walk();
	double previous_apex = 0.0;
	double previous_lap = 1e30;
	const double frictions[4] = { 1.6, 1.9, 2.1, 2.3 };
	for (int step = 0; step < 4; ++step) {
		SpeedModel model = default_speed_model();
		model.tire.lateral.peak_friction = frictions[step];
		model.tire.longitudinal.peak_friction = frictions[step];
		model.tire.refresh_peaks();

		auto line = build(walk, 9.0);
		const LineSummary &summary = line->solve(model);
		CHECK(summary.min_speed_ms >= previous_apex - 1e-9);
		CHECK(summary.lap_time_s <= previous_lap + 1e-9);
		CHECK(line->worst_lateral_utilization() <= 1.0 + 1e-6);
		previous_apex = summary.min_speed_ms;
		previous_lap = summary.lap_time_s;
	}
	CHECK(previous_apex > 0.0);
}

TEST_CASE("a lower grip usage is a slower lap and never a broken one") {
	const Walk walk = hairpin_walk();
	double previous_lap = 0.0;
	const double usages[3] = { 1.0, 0.9, 0.8 };
	for (int step = 0; step < 3; ++step) {
		SpeedModel model = default_speed_model();
		model.grip_usage = usages[step];
		auto line = build(walk, 9.0);
		const LineSummary &summary = line->solve(model);
		CHECK(summary.lap_time_s >= previous_lap - 1e-9);
		CHECK(line->worst_lateral_utilization() <= 1.0 + 1e-6);
		previous_lap = summary.lap_time_s;
	}
}

TEST_CASE("the backward pass puts the braking point where the arithmetic does") {
	// One long straight into one corner, so the braking distance is checkable in
	// closed form: v_entry^2 = v_apex^2 + 2 a d.
	auto line = build(hairpin_walk(), 9.0);
	const SpeedModel model = default_speed_model();
	const LineSummary &summary = line->solve(model);

	REQUIRE(summary.braking_zones >= 2);
	CHECK(summary.max_braking_g > 1.0);
	// Total deceleration includes the air, so it legitimately exceeds the pure
	// brake ceiling - but not by more than drag at the speed it happened at.
	const double drag_g = model.resistance(summary.max_speed_ms) / model.mass_kg / G;
	CHECK(summary.max_braking_g <= summary.brake_limit_g + drag_g + 1e-6);

	// Integrate the deceleration the profile actually delivered over one braking
	// zone and check it reaches the apex speed the lateral pass asked for.
	double zone_start[8];
	double zone_speed[8];
	const int zones = line->braking_points(zone_start, zone_speed, 8);
	REQUIRE(zones >= 1);
	// Walk forward from the first braking point to the next station that is not
	// braking; the speed there must be the corner speed the lateral pass set.
	int index = 0;
	while (index < line->station_count() &&
			line->station_distance(index) < zone_start[0] - 1e-9) {
		++index;
	}
	int steps = 0;
	while (line->braking(index) && steps < line->station_count()) {
		index = (index + 1) % line->station_count();
		++steps;
	}
	CHECK(steps > 4);
	CHECK(line->speed(index) <= line->corner_speed(index) + 1e-6);
}

TEST_CASE("the profile is gear aware and the gear is a real choice") {
	auto line = build(hairpin_walk(), 9.0);
	const SpeedModel model = default_speed_model();
	const LineSummary &summary = line->solve(model);

	// A KZ uses its box. A profile that found one gear enough would mean the
	// gear-awareness had done nothing.
	int used = 0;
	for (int gear = 1; gear <= kz::GEAR_COUNT; ++gear) {
		if (summary.gear_stations[gear] > 0) {
			++used;
		}
	}
	CHECK(used >= 4);
	CHECK(summary.shifts >= 4);
	// Neutral is never selected while the kart is moving.
	CHECK(summary.gear_stations[0] == 0);

	for (int index = 0; index < line->station_count(); ++index) {
		const int gear = line->gear(index);
		REQUIRE(gear >= 1);
		REQUIRE(gear <= kz::GEAR_COUNT);
		// Never past the hard cut. A profile that over-revved the engine every
		// corner exit would still produce plausible speeds.
		CHECK(line->engine_rpm(index) <= model.engine.hard_cut_rpm + 1e-6);
	}

	// The narrow powerband, stated as a fact about the corner exit rather than
	// as a comment: at the slowest point on the lap the chosen gear must beat
	// two gears higher by a wide margin, or gear choice was never worth modeling.
	const double apex = summary.min_speed_ms;
	double chosen = 0.0;
	const int gear = model.best_gear(apex, &chosen);
	REQUIRE(gear >= 1);
	if (gear + 2 <= kz::GEAR_COUNT) {
		const double tall = model.gear_force(apex, gear + 2);
		CHECK(chosen > 1.5 * (tall > 0.0 ? tall : 0.0));
	}
}

TEST_CASE("a shift costs the kart something and a chattering plan does not") {
	const Walk walk = hairpin_walk();
	const SpeedModel model = default_speed_model();

	auto normal = build(walk, 9.0);
	const LineSummary &with_cut = normal->solve(model, LineOptions());

	SpeedModel free_shift = model;
	free_shift.gearbox.shift_time = 0.0;
	auto instant = build(walk, 9.0);
	const LineSummary &without = instant->solve(free_shift, LineOptions());

	CHECK(with_cut.lap_time_s > without.lap_time_s);
	// And not by an absurd amount: a handful of shifts at 65 ms each.
	CHECK(with_cut.lap_time_s - without.lap_time_s < 0.065 * (with_cut.shifts + 2));

	// The gear plan is a plan and not a stutter. One shift every 15 m would be
	// the chatter the run-length hold exists to stop.
	CHECK(with_cut.shifts * 15.0 < with_cut.line_length_m);
}

TEST_CASE("a hill moves the braking point and the top speed") {
	const Walk walk = paperclip_walk();
	const SpeedModel model = default_speed_model();

	auto flat = build(walk, 9.0, 1.5, 0.0, 0.0);
	const double flat_lap = flat->solve(model).lap_time_s;

	// A constant 4% climb everywhere is not a closed elevation profile, which is
	// exactly why this is a unit test and not a circuit: it isolates the gravity
	// term. The lap must get slower and the profile must stay legal.
	auto uphill = build(walk, 9.0, 1.5, 0.0, 0.04);
	const LineSummary &climbed = uphill->solve(model);
	CHECK(climbed.lap_time_s > flat_lap);
	CHECK(climbed.max_speed_ms < flat->summary().max_speed_ms);
	CHECK(uphill->worst_lateral_utilization() <= 1.0 + 1e-6);
}

TEST_CASE("bank buys corner speed") {
	const Walk walk = paperclip_walk(); // right-handers, so a positive bank helps
	const SpeedModel model = default_speed_model();

	auto flat = build(walk, 9.0, 1.5, 0.0, 0.0);
	const double flat_apex = flat->solve(model).min_speed_ms;

	auto banked = build(walk, 9.0, 1.5, 0.08, 0.0);
	const LineSummary &helped = banked->solve(model);
	CHECK(helped.min_speed_ms > flat_apex);
	CHECK(banked->worst_lateral_utilization() <= 1.0 + 1e-6);

	auto adverse = build(walk, 9.0, 1.5, -0.08, 0.0);
	CHECK(adverse->solve(model).min_speed_ms < flat_apex);
}

TEST_CASE("the grid is a power of two and honors the spacing asked for") {
	auto line = std::unique_ptr<RacingLine>(new RacingLine());
	const int count = line->begin(1375.119417, 1.5);
	CHECK(count == 1024);
	CHECK(line->spacing_m() <= 1.5);
	// A power of two, because the cascade halves the stride.
	CHECK((count & (count - 1)) == 0);

	// A short track gets a coarser grid, not a broken one.
	const int small = line->begin(120.0, 1.5);
	CHECK(small >= COARSEST_STATIONS);
	CHECK((small & (small - 1)) == 0);
	CHECK(line->spacing_m() <= 1.5);

	// And nothing at all is refused rather than divided by.
	CHECK(line->begin(0.0, 1.5) == 0);
	CHECK(line->begin(100.0, 0.0) == 0);
}

TEST_CASE("two solves of the same input agree to the bit") {
	const Walk walk = hairpin_walk();
	const SpeedModel model = default_speed_model();
	auto first = build(walk, 9.0);
	auto second = build(walk, 9.0);
	const LineSummary a = first->solve(model);
	const LineSummary b = second->solve(model);

	CHECK(a.line_objective == b.line_objective);
	CHECK(a.lap_time_s == b.lap_time_s);
	CHECK(a.shifts == b.shifts);
	for (int index = 0; index < first->station_count(); ++index) {
		CHECK(first->offset(index) == second->offset(index));
		CHECK(first->speed(index) == second->speed(index));
		CHECK(first->gear(index) == second->gear(index));
	}

	// And re-solving the same object gives the same answer, which is the check
	// that `begin` really does clear everything it owns.
	auto reused = build(walk, 9.0);
	reused->solve(model);
	const int count = reused->begin(walk.total, 1.5);
	for (int index = 0; index < count; ++index) {
		double x = 0.0;
		double z = 0.0;
		double heading = 0.0;
		double curvature = 0.0;
		walk.at(reused->station_distance(index), x, z, heading, curvature);
		reused->set_station(index, x, z, heading, curvature, 0.5 * 9.0, 0.0, 0.0, 0.0);
	}
	const LineSummary c = reused->solve(model);
	CHECK(c.line_objective == a.line_objective);
	CHECK(c.lap_time_s == a.lap_time_s);
}

TEST_CASE("the left-hand rollover ceiling is the one that binds first") {
	// `chassis.h`'s asymmetry, as a statement about a lap rather than about a
	// formula. Two identical circuits of opposite hand, on a tire good enough
	// that the geometry decides: the left-handed one must be the slower.
	SpeedModel model = default_speed_model();
	model.tire.lateral.peak_friction = 4.0;
	model.tire.longitudinal.peak_friction = 4.0;
	model.tire.refresh_peaks();

	auto right = build(circle_walk(40.0), 9.0);
	Walk mirrored;
	mirrored.add(2.0 * PI * 40.0, -1.0 / 40.0);
	auto left = build(mirrored, 9.0);

	const LineSummary &clockwise = right->solve(model);
	const LineSummary &anticlockwise = left->solve(model);

	CHECK(anticlockwise.min_speed_ms < clockwise.min_speed_ms);
	CHECK(anticlockwise.rollover_bound_stations > 0);
	CHECK(clockwise.rollover_bound_stations > 0);
	// The ratio of the two ceilings is the ratio of the two arms, and the speeds
	// go as its square root.
	const double expected = std::sqrt(model.rollover_arm_right_m / model.rollover_arm_left_m);
	CHECK(close_relative(clockwise.min_speed_ms / anticlockwise.min_speed_ms, expected, 0.02));
}
