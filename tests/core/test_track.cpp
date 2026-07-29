#include "doctest.h"

#include "core/track.h"

#include <cmath>
#include <string>
#include <vector>

using namespace kart::core::track;

// `src/core/track.h` measured rather than described.
//
// Two things are being tested and they are not the same thing. The *geometry*
// tests assert that an exact arc is exact and that a Hermite profile reproduces a
// parabola, both against closed-form answers computed here rather than against
// the implementation's own output. The *validation* tests assert that the loader
// says no, one rule at a time, by breaking exactly one thing in a track that
// otherwise passes - which is the only way to know a rule fired for its own
// reason rather than being caught by a neighbour.
//
// No JSON anywhere. ADR-0017 keeps godot-cpp out of `src/core/`, and Godot's is
// the only JSON parser this project has, so the file format is exercised by
// `tools/verify/circuit.sh` against the real `data/tracks/*.track.json` and the
// arithmetic is exercised here in five seconds.

namespace {

constexpr double OVAL_STRAIGHT_HALF = 100.0;
constexpr double OVAL_STRAIGHT = 200.0;
constexpr double OVAL_RADIUS = 120.0;
constexpr double OVAL_WIDTH = 10.0;

// Fill in every control point's position and heading by walking its own
// curvatures, which is what the file's checksum is *supposed* to agree with. A
// test that hand-wrote the positions would be testing the arithmetic in the test.
void seat_positions(Track &track) {
	double x = 0.0;
	double z = 0.0;
	double heading = 0.0;
	for (std::size_t index = 0; index < track.points.size(); ++index) {
		ControlPoint &point = track.points[index];
		point.x = x;
		point.z = z;
		point.heading_rad = heading;
		const double length = track.span_length(index);
		if (point.curvature == 0.0) {
			double fx = 0.0;
			double fz = 0.0;
			Track::forward_of(heading, fx, fz);
			x += fx * length;
			z += fz * length;
		} else {
			const double radius = 1.0 / point.curvature;
			double rx = 0.0;
			double rz = 0.0;
			Track::right_of(heading, rx, rz);
			const double centre_x = x + rx * radius;
			const double centre_z = z + rz * radius;
			heading += point.curvature * length;
			Track::right_of(heading, rx, rz);
			x = centre_x - rx * radius;
			z = centre_z - rz * radius;
		}
	}
}

Corner oval_corner(const char *name, double from_m, double to_m) {
	Corner corner;
	corner.name = name;
	corner.hand = "right";
	corner.from_m = from_m;
	corner.to_m = to_m;
	corner.apex_from_m = from_m;
	corner.apex_to_m = to_m;
	corner.direction_change_deg = 180.0;
	corner.min_radius_m = OVAL_RADIUS;
	corner.width_m = OVAL_WIDTH;
	// rho = R + h(1 + cos(theta/2))/(1 - cos(theta/2)) with theta = 180 deg, where
	// the multiplier is exactly 1.00 and no width opens the line further than
	// R + h. h = (W - 1.400)/2.
	corner.line_radius_m = OVAL_RADIUS + (OVAL_WIDTH - 1.400) * 0.5;
	corner.defender_line_radius_m = OVAL_RADIUS;
	// 1.86 g on that radius, which is the figure `drive.sh` measures.
	corner.grip_ceiling_kmh = 3.6 * std::sqrt(1.86 * kart::core::G * corner.line_radius_m);
	corner.lock_ceiling_kmh = 140.0;
	corner.apex_kmh = 140.0;
	corner.reverse_apex_kmh = 140.0;
	corner.has_runoff = true;
	corner.runoff.side = SIDE_LEFT;
	corner.runoff.apron_m = 10.0;
	corner.runoff.outfield_m = 20.0;
	corner.runoff.approach_kmh = 145.0;
	return corner;
}

// A legal Grade 1 oval: 1,153.98 m, two 200 m straights, two 180-degree
// corners of 120 m radius, 10 m wide, dead flat. Everything the validator checks
// passes, so a single deliberate break can be attributed to its own rule.
Track make_oval() {
	Track track;
	track.schema_version = 1;
	track.name = "Test oval";
	track.grade = 1;

	const double arc = kart::core::PI * OVAL_RADIUS;
	track.length_m = 2.0 * OVAL_STRAIGHT + 2.0 * arc;

	const double stations[5] = {
		0.0,
		OVAL_STRAIGHT_HALF,
		OVAL_STRAIGHT_HALF + arc,
		OVAL_STRAIGHT_HALF + arc + OVAL_STRAIGHT,
		OVAL_STRAIGHT_HALF + 2.0 * arc + OVAL_STRAIGHT,
	};
	const double curvatures[5] = { 0.0, 1.0 / OVAL_RADIUS, 0.0, 1.0 / OVAL_RADIUS, 0.0 };

	for (int index = 0; index < 5; ++index) {
		ControlPoint point;
		point.distance_m = stations[index];
		point.curvature = curvatures[index];
		point.width_m = OVAL_WIDTH;
		point.crown_pct = curvatures[index] == 0.0 ? 2.0 : 0.0;
		// A right-hander falls to the right, which is positive bank. A bank whose
		// sign opposes its own turn is adverse camber and the validator says so.
		point.bank_pct = curvatures[index] == 0.0 ? 0.0 : 5.0;
		point.elevation_m = 0.0;
		point.grade = 0.0;
		point.segment = index;
		track.points.push_back(point);
	}
	seat_positions(track);

	track.corners.push_back(oval_corner("T1", stations[1], stations[2]));
	track.corners.push_back(oval_corner("T2", stations[3], stations[4]));

	Layout layout;
	layout.name = "forward";
	layout.reversed = false;
	layout.sector_marks_m = { 384.0, 769.0 };
	const int checkpoints = 12;
	for (int index = 0; index < checkpoints; ++index) {
		layout.checkpoints_m.push_back(track.length_m * index / checkpoints);
	}
	for (int index = 0; index < 8; ++index) {
		GridSlot slot;
		slot.position = index + 1;
		slot.distance_m = track.length_m - (4.0 + index * 4.0);
		slot.lateral_m = (index % 2 == 0) ? -3.0 : 3.0;
		layout.grid.push_back(slot);
	}
	track.layouts.push_back(layout);
	return track;
}

bool mentions(const std::vector<std::string> &problems, const char *fragment) {
	for (const std::string &problem : problems) {
		if (problem.find(fragment) != std::string::npos) {
			return true;
		}
	}
	return false;
}

}  // namespace

TEST_CASE("the test oval is a legal circuit, so a later break is attributable") {
	const Track track = make_oval();
	const std::vector<std::string> problems = track.validate(1);
	for (const std::string &problem : problems) {
		MESSAGE(problem);
	}
	CHECK(problems.empty());
}

TEST_CASE("an arc is sampled exactly, not chorded") {
	const Track track = make_oval();
	// A quarter of the way round the first 180-degree corner, the kart has turned
	// 90 degrees. Starting at heading zero (-Z) on the +X side of a right-hand
	// corner whose centre is 120 m to the right, the analytic answer is one radius
	// across and one radius along.
	const double arc = kart::core::PI * OVAL_RADIUS;
	const Frame quarter = track.sample(OVAL_STRAIGHT_HALF + arc * 0.5);
	CHECK(quarter.heading_rad == doctest::Approx(kart::core::PI * 0.5).epsilon(1e-12));
	CHECK(quarter.x == doctest::Approx(OVAL_RADIUS).epsilon(1e-9));
	CHECK(quarter.z == doctest::Approx(-OVAL_STRAIGHT_HALF - OVAL_RADIUS).epsilon(1e-9));

	// And the sagitta identity the polyline is built on: a chord subtending t
	// radians of radius R leaves R(1 - cos(t/2)). Sample either end of a small
	// step and check the true arc's midpoint stands off the chord by that much.
	const double step = 4.0;
	const Frame a = track.sample(OVAL_STRAIGHT_HALF + 40.0);
	const Frame b = track.sample(OVAL_STRAIGHT_HALF + 40.0 + step);
	const Frame middle = track.sample(OVAL_STRAIGHT_HALF + 40.0 + step * 0.5);
	const double chord_x = 0.5 * (a.x + b.x);
	const double chord_z = 0.5 * (a.z + b.z);
	const double sagitta = std::sqrt((middle.x - chord_x) * (middle.x - chord_x)
			+ (middle.z - chord_z) * (middle.z - chord_z));
	const double predicted = OVAL_RADIUS * (1.0 - std::cos(0.5 * step / OVAL_RADIUS));
	CHECK(sagitta == doctest::Approx(predicted).epsilon(1e-6));
}

TEST_CASE("the polyline honours its sagitta tolerance and closes on itself") {
	const Track track = make_oval();
	const std::vector<Frame> line = track.polyline(0.020, 2.0);
	REQUIRE(line.size() > 100);
	// The closing sample is the first one again, bit-identical, so the ribbon's
	// seam is watertight rather than merely close.
	CHECK(line.back().x == line.front().x);
	CHECK(line.back().z == line.front().z);
	CHECK(line.back().distance_m == doctest::Approx(track.length_m));

	double worst = 0.0;
	for (std::size_t index = 0; index + 1 < line.size(); ++index) {
		const double middle_distance = 0.5 * (line[index].distance_m + line[index + 1].distance_m);
		const Frame middle = track.sample(middle_distance);
		const double chord_x = 0.5 * (line[index].x + line[index + 1].x);
		const double chord_z = 0.5 * (line[index].z + line[index + 1].z);
		worst = std::fmax(worst, std::sqrt((middle.x - chord_x) * (middle.x - chord_x)
				+ (middle.z - chord_z) * (middle.z - chord_z)));
	}
	CHECK(worst <= 0.020 + 1e-9);
}

TEST_CASE("Hermite elevation reproduces a parabolic vertical curve exactly") {
	// This is the whole reason elevation is not linear. A regulation vertical curve
	// is a parabola of length L between grades g1 and g2; two control points, one
	// at each end, carrying the elevation and the grade, must reproduce it to
	// double precision everywhere in between - not approximately, exactly.
	Track track = make_oval();
	const double length = 102.355;
	const double g1 = 0.00787;
	const double g2 = -0.046;

	// Lay the curve over the first straight's span, which is 100 m, by rebuilding
	// two control points around a span of exactly `length`.
	track.points.clear();
	ControlPoint a;
	a.distance_m = 0.0;
	a.curvature = 0.0;
	a.width_m = OVAL_WIDTH;
	a.crown_pct = 2.0;
	a.elevation_m = 0.0;
	a.grade = g1;
	ControlPoint b = a;
	b.distance_m = length;
	b.elevation_m = g1 * length + (g2 - g1) * length * 0.5;
	b.grade = g2;
	ControlPoint c = a;
	c.distance_m = 2.0 * length;
	c.elevation_m = b.elevation_m + g2 * length;
	c.grade = g2;
	track.points = { a, b, c };
	track.length_m = 3.0 * length;
	seat_positions(track);

	for (int step = 0; step <= 40; ++step) {
		const double s = length * step / 40.0;
		const double truth = g1 * s + (g2 - g1) * s * s / (2.0 * length);
		const double truth_grade = g1 + (g2 - g1) * s / length;
		const Frame frame = track.sample(s);
		// An explicit relative comparison, not `Approx(...).epsilon(...)`: doctest
		// scales its epsilon by `1 + max(|a|,|b|)`, so on a value of 0.03 an
		// epsilon of 1e-9 is a tolerance a hundred times looser than it reads.
		CHECK(std::fabs(frame.elevation_m - truth) < 1e-9);
		CHECK(std::fabs(frame.grade - truth_grade) < 1e-9);
	}
}

TEST_CASE("projection is exact on the arc and signs lateral to the right") {
	const Track track = make_oval();
	const double arc = kart::core::PI * OVAL_RADIUS;
	const double station = OVAL_STRAIGHT_HALF + arc * 0.30;
	const Frame on_line = track.sample(station);

	double rx = 0.0;
	double rz = 0.0;
	Track::right_of(on_line.heading_rad, rx, rz);
	const double offset = 3.5;
	const Projection right = track.project(
			on_line.x + rx * offset, on_line.z + rz * offset, station);
	CHECK(right.distance_m == doctest::Approx(station).epsilon(1e-9));
	CHECK(right.lateral_m == doctest::Approx(offset).epsilon(1e-9));

	const Projection left = track.project(
			on_line.x - rx * offset, on_line.z - rz * offset, station);
	CHECK(left.lateral_m == doctest::Approx(-offset).epsilon(1e-9));

	// The hint is not an optimization: it is what tells the two ends of a corner
	// apart. Without one, a point 3.5 m inside a 120 m corner is still nearest to
	// its own station, so this is the easy case - the hard one is the hairpin, and
	// what is checked here is that the unhinted search agrees rather than that it
	// is fast.
	const Projection unhinted = track.project(on_line.x + rx * offset, on_line.z + rz * offset);
	CHECK(unhinted.distance_m == doctest::Approx(station).epsilon(1e-9));
}

TEST_CASE("a chord search would misplace a corner and the arc refinement does not") {
	// The failure this replaces, measured rather than asserted. A point offset
	// inside a corner projects onto a chord a fraction of a meter along from where
	// it truly is - `lateral * segment_turn / 2` - and the bias reverses sign at
	// every sample, so the arc length steps backwards. `lap_timing.h` reads a fall
	// in arc length as a start-line crossing.
	const Track track = make_oval();
	const double arc = kart::core::PI * OVAL_RADIUS;
	double previous = -1.0;
	bool monotonic = true;
	for (int step = 0; step <= 400; ++step) {
		const double station = OVAL_STRAIGHT_HALF + arc * step / 400.0;
		const Frame frame = track.sample(station);
		double rx = 0.0;
		double rz = 0.0;
		Track::right_of(frame.heading_rad, rx, rz);
		const Projection back = track.project(frame.x - rx * 3.5, frame.z - rz * 3.5, station);
		if (back.distance_m < previous - 1e-9) {
			monotonic = false;
		}
		previous = back.distance_m;
	}
	CHECK(monotonic);
}

TEST_CASE("the longest straight merges across the start line") {
	const Track track = make_oval();
	double start = 0.0;
	// 100 m of it is before the line and 100 m after. Measured as two runs it is
	// 100 m and legal; measured whole it is 200 m and exactly on Grade 1's cap.
	CHECK(track.longest_straight(&start) == doctest::Approx(OVAL_STRAIGHT).epsilon(1e-9));
	CHECK(track.start_to_first_corner() == doctest::Approx(OVAL_STRAIGHT_HALF).epsilon(1e-9));
	CHECK(track.last_corner_to_start() == doctest::Approx(OVAL_STRAIGHT_HALF).epsilon(1e-9));
}

TEST_CASE("a reversed layout's station conversion is its own inverse") {
	const Track track = make_oval();
	Layout reverse;
	reverse.name = "reverse";
	reverse.reversed = true;
	for (double d = 0.0; d < track.length_m; d += 37.0) {
		const double station = track.to_station(reverse, d);
		CHECK(track.to_forward(reverse, station) == doctest::Approx(d).epsilon(1e-9));
	}
	// And a forward layout leaves everything alone, which is the property that
	// keeps the conversion in one place instead of in every caller.
	const Layout &forward = track.layouts.front();
	CHECK(track.to_station(forward, 412.0) == doctest::Approx(412.0));
}

TEST_CASE("the schema version refuses rather than migrates") {
	const Track track = make_oval();
	const std::vector<std::string> problems = track.validate(2);
	REQUIRE(problems.size() == 1);
	CHECK(mentions(problems, "schema_version"));
	CHECK(mentions(problems, "not migrated at load"));
}

TEST_CASE("a moved control point fails its own checksum") {
	Track track = make_oval();
	// Two millimeters. Small enough to look like rounding in a diff and large
	// enough to be a corner in the wrong place; the whole point of storing position
	// alongside curvature is that this cannot pass.
	track.points[2].x += 0.002;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "stored position"));
	CHECK(mentions(problems, "checksum"));
}

TEST_CASE("a spline that does not close is rejected") {
	Track track = make_oval();
	// Lengthen the lap without lengthening any span: the last span grows, the walk
	// overshoots, and the loop no longer closes.
	track.length_m += 0.5;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "does not close"));
}

TEST_CASE("the self-intersection gate rejects a figure-eight") {
	// The negative control, in the shape `data/tracks/self_intersecting.track.json`
	// has: two long straights crossing in the middle, joined by two 270-degree
	// corners of opposite hand. It closes, it is 8 m wide, its camber is legal -
	// and it crosses itself, and it turns zero degrees net, and the validator has
	// to name both.
	Track track;
	track.schema_version = 1;
	track.grade = 1;
	track.name = "figure eight";

	const double radius = 90.0;
	const double straight = 260.0;
	const double arc = 1.5 * kart::core::PI * radius;  // 270 degrees
	track.length_m = 2.0 * straight + 2.0 * arc;

	struct Span {
		double length;
		double curvature;
	};
	const Span spans[4] = {
		{ straight, 0.0 },
		{ arc, 1.0 / radius },
		{ straight, 0.0 },
		{ arc, -1.0 / radius },
	};
	double station = 0.0;
	for (const Span &span : spans) {
		// Subdivided so the O(n^2) scan has samples to find the crossing with; the
		// real file is subdivided the same way for the same reason.
		const int steps = span.curvature == 0.0 ? 8 : 12;
		for (int step = 0; step < steps; ++step) {
			ControlPoint point;
			point.distance_m = station + span.length * step / steps;
			point.curvature = span.curvature;
			point.width_m = 8.0;
			point.crown_pct = span.curvature == 0.0 ? 2.0 : 0.0;
			point.bank_pct = span.curvature == 0.0 ? 0.0 : (span.curvature > 0.0 ? 5.0 : -5.0);
			track.points.push_back(point);
		}
		station += span.length;
	}
	seat_positions(track);

	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "within"));
	CHECK(mentions(problems, "clear ground of itself"));
	CHECK(mentions(problems, "not a loop"));
}

TEST_CASE("one piece of road is never reported as two bits of track") {
	// The exclusion that four independent circuit designs got wrong in the same
	// way, and that this one got wrong first in a *different* way. Their version
	// was a hairpin's own two ends: a 180-degree corner of 15 m radius has 47.1 m
	// of arc, so its entry and exit tangents are "more than 40 m apart along the
	// lap" and 30 m apart in plan, and that self-pair was all four designs'
	// reported minimum. Four for four, measuring the mouth of one corner.
	//
	// The 100 m window fixes theirs. What it does not fix is the start/finish
	// straight: two points 100 m apart on the same straight are 100 m apart in
	// plan, clear the window by a hair, and were this oval's reported minimum at
	// 90 m until same-feature pairs were excluded outright.
	const Track track = make_oval();
	const Track::Separation gap = track.separation(1.0);
	CHECK(gap.clear_ground_m > kart::core::circuit::MIN_CLEAR_GROUND_BETWEEN_SECTIONS_M);
	CHECK(track.feature_at(gap.at_a_m) != track.feature_at(gap.at_b_m));
	// And the reported pair is not a corner against itself.
	const int corner_a = track.corner_at(gap.at_a_m);
	CHECK((corner_a < 0 || corner_a != track.corner_at(gap.at_b_m)));
	MESSAGE("closest approach " << gap.clear_ground_m << " m of clear ground, "
			<< gap.at_a_m << " m against " << gap.at_b_m << " m");
}

TEST_CASE("adverse camber is rejected and the message says which way") {
	Track track = make_oval();
	// A right-hander banked to fall to the left.
	track.points[1].bank_pct = -5.0;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "the wrong way"));
	CHECK(mentions(problems, "adverse camber"));
}

TEST_CASE("a flat straight is rejected, because a straight is never flat") {
	Track track = make_oval();
	track.points[0].crown_pct = 0.0;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "a straight is never flat"));
}

TEST_CASE("a road under the grade's width floor is rejected") {
	Track track = make_oval();
	track.points[2].width_m = 7.5;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "under the grade's"));
}

TEST_CASE("a corner taken above min(grip, lock) is rejected") {
	Track track = make_oval();
	// Steering lock, not grip, is the binding limit on six of Valdirone's eight
	// corners, so a check written against the grip ceiling alone passes a corner
	// the kart cannot steer round.
	track.corners[0].lock_ceiling_kmh = 110.0;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "above min(grip"));
}

TEST_CASE("a grip ceiling that does not match its own line radius is rejected") {
	Track track = make_oval();
	track.corners[0].grip_ceiling_kmh = 260.0;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "outside the sourced sustained band"));
}

TEST_CASE("a corner over 80 degrees with no run-off is rejected") {
	Track track = make_oval();
	track.corners[1].has_runoff = false;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "declares no run-off"));
}

TEST_CASE("checkpoints must close the loop, not merely be ordered") {
	Track track = make_oval();
	// Delete the checkpoint that covers the wrap. Every remaining spacing is legal
	// and the list is still sorted; the only thing wrong is the gap from the last
	// one back to the first, which is exactly where a kart would rejoin.
	track.layouts[0].checkpoints_m.pop_back();
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "over the 100 m anti-cut spacing"));
}

TEST_CASE("a grid slot that does not fit on the road is rejected") {
	Track track = make_oval();
	track.layouts[0].grid[3].lateral_m = 4.5;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "grid slot"));
	CHECK(mentions(problems, "of half-road"));
}

TEST_CASE("a grid slot inside a corner is rejected") {
	Track track = make_oval();
	track.layouts[0].grid[0].distance_m = 300.0;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "inside a corner"));
}

TEST_CASE("a vertical curve is checked in both directions at the same K") {
	Track track = make_oval();
	VerticalCurve curve;
	curve.at_m = 500.0;
	curve.convex = true;
	curve.K = kart::core::circuit::VERTICAL_K_CONVEX;
	curve.length_m = 40.0;
	curve.grade_in = 0.0;
	curve.grade_out = 0.0;
	curve.speed_forward_kmh = 100.0;
	// Reversed, the same crest is met 40 km/h faster. The radius that was legal
	// forward is not, and `d2z/ds2` is invariant under `s -> L-s` so K does not
	// swap to help. One candidate design swapped K and declared six reverse minima
	// legal when one was not.
	curve.speed_reverse_kmh = 140.0;
	curve.radius_m = kart::core::circuit::min_vertical_radius_m(100.0, curve.K);
	track.vertical_curves.push_back(curve);

	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "vertical curve at 500.0 m has radius"));
	CHECK(mentions(problems, "at 140.0 km/h"));
}

TEST_CASE("a vertical curve whose declaration disagrees with the spline is rejected") {
	Track track = make_oval();
	VerticalCurve curve;
	curve.at_m = 500.0;
	curve.convex = true;
	curve.K = kart::core::circuit::VERTICAL_K_CONVEX;
	curve.length_m = 40.0;
	// The oval is dead flat, so declaring a grade change here is a profile edited
	// in one place and declared in another - the class of defect the cross-check
	// exists for.
	curve.grade_in = 0.01;
	curve.grade_out = -0.03;
	curve.speed_forward_kmh = 60.0;
	curve.speed_reverse_kmh = 60.0;
	curve.radius_m = 100000.0;
	track.vertical_curves.push_back(curve);

	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "declares an entry grade"));
	CHECK(mentions(problems, "declares an exit grade"));
}

TEST_CASE("an elevation that disagrees with its own grades is rejected") {
	// The profile closes structurally - elevation is stored per control point, so
	// the spans telescope - which is why the rule is about consistency instead.
	// Both ends of this span read flat and the span climbs three meters; Hermite
	// draws a hump, the collider agrees with it, and nothing in the design put it
	// there.
	Track track = make_oval();
	track.points[3].elevation_m = 3.0;
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "climbs at a mean"));
	CHECK(mentions(problems, "a hump the design does not have"));
}

TEST_CASE("a span of no length is rejected") {
	// It comes from two authored stations landing on the same place - a taper that
	// ends exactly where a segment begins - and it is a division by nearly zero in
	// every consumer. Valdirone produced one at 0.0001 m before the authoring tool
	// merged coincident stations.
	Track track = make_oval();
	ControlPoint twin = track.points[1];
	twin.distance_m += 0.0001;
	track.points.insert(track.points.begin() + 2, twin);
	seat_positions(track);
	const std::vector<std::string> problems = track.validate(1);
	CHECK(mentions(problems, "a span must have length"));
}
