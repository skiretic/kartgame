#ifndef KART_CORE_TRACK_H
#define KART_CORE_TRACK_H

#include "core/circuit_reference.h"
#include "core/kz_reference.h"
#include "core/units.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

// `track.json`'s geometry and its validation pass, with no engine in it.
//
// `docs/TRACK_SCHEMA.md` is the prose; this header is the executable copy and the
// two must not drift. ADR-0046 owns the decisions, ROADMAP M5 is where they were
// implemented.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
// That boundary is the whole reason the validation pass is here: a rule that says
// "no radius the kart physically cannot take" is arithmetic over a spline, and
// arithmetic over a spline should be testable in five seconds without a rendering
// server. `src/track/kart_track.cpp` parses the JSON and fills these structs;
// nothing in this file opens a file or knows what JSON is.
//
// ## The one thing to understand before reading further
//
// **A span between two control points is a circular arc of constant curvature,
// and it is evaluated exactly.** Not a chord, not a polyline, not a spline
// through sampled positions. `distance_m` and `curvature_1pm` are normative;
// `position` and `heading_deg` are a checksum the loader verifies to a millimeter
// and then never uses again.
//
// That matters most where it is least visible. Valdirone's hairpin is a 15 m
// radius; sampled at any spacing coarse enough to be cheap, its *radius* becomes
// a function of the sampling rate, and the radius is what decides whether the
// corner is on the safe side of #137. Exact arcs mean the schema's numbers and
// the driven geometry are the same numbers.
//
// ## Why elevation is Hermite and not linear
//
// A regulation vertical curve is a parabola - Part I §7.2 fixes its radius and
// therefore its second derivative - and cubic Hermite through a parabola's two
// endpoints with its two endpoint slopes reproduces that parabola *identically*,
// because a parabola is a cubic and the interpolant is the unique cubic matching
// four constraints. So a circuit's whole longitudinal profile needs two control
// points per vertical curve and is then exact everywhere.
//
// Linear would need a control point every few meters and would still be wrong in
// a way that is felt rather than seen: at 5 m spacing on Valdirone's crest the
// grade steps 0.26% at every point, which is 0.10 m/s of vertical velocity
// arriving inside one tick at 140 km/h. That is a bump the road does not have.

namespace kart::core::track {

// --- the schema, as structs -------------------------------------------------

enum SurfaceSide {
	SIDE_LEFT = -1,
	SIDE_FULL = 0,
	SIDE_RIGHT = 1,
	SIDE_BOTH = 2,
};

enum CurbProfile {
	CURB_FLAT = 0,
	CURB_RIPPLED = 1,
	CURB_VERTICAL = 2,
};

// One control point, and the span that leaves it.
struct ControlPoint {
	double distance_m = 0.0;
	// The checksum, in Godot's ground plane. Never read after validation.
	double x = 0.0;
	double z = 0.0;
	double heading_rad = 0.0;
	// Normative. Signed, positive for a right-hand corner, zero for a straight.
	double curvature = 0.0;
	double width_m = 8.0;
	double crown_pct = 2.0;
	double bank_pct = 0.0;
	double elevation_m = 0.0;
	// A fraction, not a percent. The file carries percent and the parser divides,
	// so that every trigonometric identity in this header is in one unit system.
	double grade = 0.0;
	int segment = 0;
};

struct Runoff {
	int side = SIDE_LEFT;
	double apron_m = 0.0;
	double outfield_m = 0.0;
	double approach_kmh = 0.0;
	std::string outfield;
	std::string barrier;
	std::string sized_for;
};

struct Corner {
	std::string name;
	std::string hand;
	double from_m = 0.0;
	double to_m = 0.0;
	double apex_from_m = 0.0;
	double apex_to_m = 0.0;
	double direction_change_deg = 0.0;
	double min_radius_m = 0.0;
	double width_m = 0.0;
	// The number the "no radius the kart cannot take" gate runs against. NOT the
	// centerline radius: see the header comment on `validate`.
	double line_radius_m = 0.0;
	double defender_line_radius_m = 0.0;
	double grip_ceiling_kmh = 0.0;
	double lock_ceiling_kmh = 0.0;
	double apex_kmh = 0.0;
	double reverse_apex_kmh = 0.0;
	bool has_runoff = false;
	Runoff runoff;
};

struct VerticalCurve {
	double at_m = 0.0;
	double K = 0.0;
	double radius_m = 0.0;
	double length_m = 0.0;
	double grade_in = 0.0;
	double grade_out = 0.0;
	double speed_forward_kmh = 0.0;
	double speed_reverse_kmh = 0.0;
	bool convex = false;
};

struct SurfaceSpan {
	double from_m = 0.0;
	double to_m = 0.0;
	int side = SIDE_FULL;
	int surface_type = 0;
	double width_m = 0.0;
	double height_m = 0.0;
	int profile = CURB_FLAT;
};

struct GridSlot {
	int position = 0;
	double distance_m = 0.0;
	double lateral_m = 0.0;
};

struct SeedPoint {
	double at_m = 0.0;
	double lateral_m = 0.0;
};

struct Layout {
	std::string name;
	// True for the authored reverse layout. Every distance in a layout is a
	// station in that layout's own direction; `to_station` and `to_forward`
	// convert, and they are the only place the direction is consulted.
	bool reversed = false;
	std::vector<double> sector_marks_m;
	std::vector<double> checkpoints_m;
	std::vector<GridSlot> grid;
	std::vector<SeedPoint> racing_line_seed;
	std::vector<double> vertical_curve_speeds_kmh;
	double pit_entry_m = -1.0;
	double pit_exit_m = -1.0;
	double estimated_lap_time_s = 0.0;
};

// A point on the centerline, everything the two consumers need about it.
struct Frame {
	double distance_m = 0.0;
	double x = 0.0;
	double z = 0.0;
	double elevation_m = 0.0;
	double heading_rad = 0.0;
	double curvature = 0.0;
	double grade = 0.0;
	double width_m = 8.0;
	double crown_pct = 0.0;
	double bank_pct = 0.0;
};

struct Projection {
	double distance_m = 0.0;
	// Signed, positive to the right of the direction of travel. Same sign
	// convention as `scripts/track/track_layout.gd`, deliberately.
	double lateral_m = 0.0;
	double gap_m = 0.0;
	double heading_rad = 0.0;
};

// --- the track --------------------------------------------------------------

struct Track {
	int schema_version = 0;
	std::string name;
	double length_m = 0.0;
	int grade = 1;
	double net_turn_deg = 0.0;

	std::vector<ControlPoint> points;
	std::vector<Corner> corners;
	std::vector<VerticalCurve> vertical_curves;
	std::vector<SurfaceSpan> surfaces;
	std::vector<Layout> layouts;

	double start_line_m = 0.0;

	// --- geometry ---------------------------------------------------------

	// Forward at a heading. Heading zero is -Z, Godot's own forward, and this
	// identity is the coordinate convention: getting it wrong is invisible until
	// the kart drives backwards. Same two functions as `track_layout.gd`.
	static void forward_of(double heading, double &out_x, double &out_z) {
		out_x = std::sin(heading);
		out_z = -std::cos(heading);
	}

	static void right_of(double heading, double &out_x, double &out_z) {
		out_x = std::cos(heading);
		out_z = std::sin(heading);
	}

	std::size_t span_count() const { return points.size(); }

	double span_length(std::size_t index) const {
		const std::size_t next = (index + 1) % points.size();
		if (next == 0) {
			return length_m - points[index].distance_m;
		}
		return points[next].distance_m - points[index].distance_m;
	}

	double wrap(double distance) const {
		if (length_m <= 0.0) {
			return 0.0;
		}
		double wrapped = std::fmod(distance, length_m);
		if (wrapped < 0.0) {
			wrapped += length_m;
		}
		return wrapped;
	}

	// The span whose arc *leaves* this station. Binary search: this is on the
	// physics path through `project`, and a linear scan over 79 spans five times
	// a tick is not free at 120 Hz.
	std::size_t span_at(double distance) const {
		const double d = wrap(distance);
		std::size_t low = 0;
		std::size_t high = points.size() - 1;
		while (low < high) {
			const std::size_t middle = (low + high + 1) / 2;
			if (points[middle].distance_m <= d) {
				low = middle;
			} else {
				high = middle - 1;
			}
		}
		return low;
	}

	// Exact. An arc's point is computed from its centre and its own final heading
	// rather than from any accumulation, so sampling density changes what a
	// consumer draws and never what the road *is*.
	Frame sample(double distance) const {
		Frame frame;
		if (points.empty()) {
			return frame;
		}
		const double d = wrap(distance);
		const std::size_t index = span_at(d);
		const std::size_t next = (index + 1) % points.size();
		const ControlPoint &a = points[index];
		const ControlPoint &b = points[next];
		const double span = span_length(index);
		const double travel = d - a.distance_m;
		const double t = span > 0.0 ? travel / span : 0.0;

		frame.distance_m = d;
		frame.curvature = a.curvature;
		frame.heading_rad = a.heading_rad + a.curvature * travel;

		if (a.curvature == 0.0) {
			double fx = 0.0;
			double fz = 0.0;
			forward_of(a.heading_rad, fx, fz);
			frame.x = a.x + fx * travel;
			frame.z = a.z + fz * travel;
		} else {
			const double radius = 1.0 / a.curvature;
			double rx = 0.0;
			double rz = 0.0;
			right_of(a.heading_rad, rx, rz);
			const double centre_x = a.x + rx * radius;
			const double centre_z = a.z + rz * radius;
			right_of(frame.heading_rad, rx, rz);
			frame.x = centre_x - rx * radius;
			frame.z = centre_z - rz * radius;
		}

		frame.width_m = a.width_m + (b.width_m - a.width_m) * t;
		frame.crown_pct = a.crown_pct + (b.crown_pct - a.crown_pct) * t;
		frame.bank_pct = a.bank_pct + (b.bank_pct - a.bank_pct) * t;

		// Cubic Hermite on (elevation, grade). Exact for a parabolic vertical
		// curve; see the header.
		const double t2 = t * t;
		const double t3 = t2 * t;
		const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
		const double h10 = t3 - 2.0 * t2 + t;
		const double h01 = -2.0 * t3 + 3.0 * t2;
		const double h11 = t3 - t2;
		frame.elevation_m = h00 * a.elevation_m + h10 * span * a.grade
				+ h01 * b.elevation_m + h11 * span * b.grade;
		// The derivative of the same polynomial, so the grade a consumer reads and
		// the height it draws cannot disagree.
		const double dh00 = 6.0 * t2 - 6.0 * t;
		const double dh10 = 3.0 * t2 - 4.0 * t + 1.0;
		const double dh01 = -6.0 * t2 + 6.0 * t;
		const double dh11 = 3.0 * t2 - 2.0 * t;
		frame.grade = span > 0.0
				? (dh00 * a.elevation_m + dh01 * b.elevation_m) / span
						+ dh10 * a.grade + dh11 * b.grade
				: a.grade;
		return frame;
	}

	// A polyline for the mesh, the collider and the O(n^2) checks.
	//
	// `sagitta` is the largest gap allowed between the true arc and the chord that
	// replaces it. Stated as a tolerance rather than as a segment count because
	// this circuit's radii differ by a factor of five between the hairpin and
	// T8b, and a fixed count would make one polygonal and the other wasteful.
	// The identity: a chord subtending `t` radians of radius `R` leaves
	// `R(1 - cos(t/2))`, which is `R t^2 / 8` for small `t`.
	std::vector<Frame> polyline(double sagitta, double max_spacing) const {
		std::vector<Frame> out;
		if (points.empty() || length_m <= 0.0) {
			return out;
		}
		for (std::size_t index = 0; index < points.size(); ++index) {
			const double span = span_length(index);
			const double curvature = points[index].curvature;
			double step = max_spacing;
			if (curvature != 0.0) {
				const double radius = std::fabs(1.0 / curvature);
				const double step_angle = std::sqrt(8.0 * sagitta / radius);
				step = std::fmin(max_spacing, step_angle * radius);
			}
			int steps = static_cast<int>(std::ceil(span / step));
			if (steps < 1) {
				steps = 1;
			}
			for (int sub = 0; sub < steps; ++sub) {
				out.push_back(sample(points[index].distance_m + span * sub / steps));
			}
		}
		// The closing sample is the first one again, bit-identical, so the ribbon's
		// seam is watertight rather than merely close. A seam that disagrees in the
		// last bit is a hairline crack in the road for a suspension raycast to find.
		Frame closing = out.front();
		closing.distance_m = length_m;
		out.push_back(closing);
		return out;
	}

	// Where along the lap, and how far to the side. Exact on the arc.
	//
	// `hint` is the previous call's distance and passing it is not an
	// optimization: it is what tells the two ends of a hairpin apart. Pass a
	// negative hint for the first call and after anything that moves the kart
	// without driving it there, because a stale hint confines the search to a
	// window the kart is no longer in and returns the nearest point of it with
	// every appearance of success.
	Projection project(double x, double z, double hint = -1.0,
			double window = 30.0) const {
		Projection best;
		best.gap_m = 1e30;
		if (points.empty()) {
			return best;
		}

		std::size_t first = 0;
		std::size_t span = points.size();
		if (hint >= 0.0) {
			first = span_at(wrap(hint - window));
			span = 0;
			double covered = 0.0;
			for (std::size_t step = 0; step < points.size(); ++step) {
				covered += span_length((first + step) % points.size());
				++span;
				if (covered >= window * 2.0) {
					break;
				}
			}
		}

		for (std::size_t step = 0; step < span; ++step) {
			const std::size_t index = (first + step) % points.size();
			const ControlPoint &a = points[index];
			const double length = span_length(index);
			if (length <= 0.0) {
				continue;
			}
			double travel = 0.0;
			double lateral = 0.0;
			if (a.curvature == 0.0) {
				double fx = 0.0;
				double fz = 0.0;
				forward_of(a.heading_rad, fx, fz);
				const double ox = x - a.x;
				const double oz = z - a.z;
				travel = ox * fx + oz * fz;
				if (travel < 0.0) {
					travel = 0.0;
				}
				if (travel > length) {
					travel = length;
				}
				double rx = 0.0;
				double rz = 0.0;
				right_of(a.heading_rad, rx, rz);
				lateral = (x - (a.x + fx * travel)) * rx + (z - (a.z + fz * travel)) * rz;
			} else {
				const double radius = 1.0 / a.curvature;
				double rx = 0.0;
				double rz = 0.0;
				right_of(a.heading_rad, rx, rz);
				const double centre_x = a.x + rx * radius;
				const double centre_z = a.z + rz * radius;
				const double from_angle = std::atan2(a.z - centre_z, a.x - centre_x);
				const double point_angle = std::atan2(z - centre_z, x - centre_x);
				double swept = point_angle - from_angle;
				// The branch cut of atan2 is somewhere on the circle and a span that
				// straddles it would otherwise sweep the long way round.
				while (swept > kart::core::PI) {
					swept -= 2.0 * kart::core::PI;
				}
				while (swept < -kart::core::PI) {
					swept += 2.0 * kart::core::PI;
				}
				travel = swept * radius;
				if (travel < 0.0) {
					travel = 0.0;
				}
				if (travel > length) {
					travel = length;
				}
				const double to_point = std::sqrt((x - centre_x) * (x - centre_x)
						+ (z - centre_z) * (z - centre_z));
				// A point at lateral `l` sits at `|r| - l * sign(r)` from the centre.
				// Inverting that is one expression right for both hands, where a
				// left-hander's negative radius flips the sign of the term as well as
				// the offset.
				lateral = radius - (radius < 0.0 ? -1.0 : 1.0) * to_point;
			}

			const Frame here = sample(a.distance_m + travel);
			const double gap = std::sqrt((x - here.x) * (x - here.x) + (z - here.z) * (z - here.z));
			if (gap >= best.gap_m) {
				continue;
			}
			best.gap_m = gap;
			best.distance_m = here.distance_m;
			best.lateral_m = lateral;
			best.heading_rad = here.heading_rad;
		}
		// `[0, length)` rather than `[0, length]`. `lap_timing.h` reads a *fall* in
		// arc length as the forward crossing of the start line, so a distance equal
		// to the length is simultaneously the end of one lap and the start of the
		// next: one tick of ambiguity at the one place it costs a lap.
		if (best.distance_m >= length_m) {
			best.distance_m -= length_m;
		}
		return best;
	}

	// --- layouts ----------------------------------------------------------

	// A layout's own station from a forward centerline distance, and back.
	//
	// Every distance inside a `Layout` is a station in that layout's direction,
	// zero at that layout's start line and increasing the way it is driven. The
	// conversion happens here and nowhere else, so nothing downstream of a
	// selected layout knows or asks which way it is facing.
	double to_station(const Layout &layout, double forward_distance) const {
		if (!layout.reversed) {
			return wrap(forward_distance);
		}
		return wrap(length_m - forward_distance);
	}

	double to_forward(const Layout &layout, double station) const {
		// Its own inverse, which is what makes a reversal an involution rather than
		// a pair of functions that can disagree.
		return to_station(layout, station);
	}

	const Layout *layout_named(const std::string &wanted) const {
		for (const Layout &layout : layouts) {
			if (layout.name == wanted) {
				return &layout;
			}
		}
		return nullptr;
	}

	// --- surfaces ---------------------------------------------------------

	// Which corner a station is inside, or -1. Used by the separation check to
	// exclude a corner's own two ends from being reported as two bits of track.
	int corner_at(double distance) const {
		const double d = wrap(distance);
		for (std::size_t index = 0; index < corners.size(); ++index) {
			if (d >= corners[index].from_m - 1e-9 && d <= corners[index].to_m + 1e-9) {
				return static_cast<int>(index);
			}
		}
		return -1;
	}

	double corner_arc_at(double distance) const {
		const int index = corner_at(distance);
		if (index < 0) {
			return 0.0;
		}
		return corners[index].to_m - corners[index].from_m;
	}

	// Which continuous piece of road a station is on: a maximal run of spans that
	// are all straight or all curving. Runs wrap across the start line, because the
	// start line is a furniture position and not a geometric break - Valdirone's
	// Rettifilo del Banco is one 165 m straight with the line 77 m into it, and
	// counting it as two would both understate the longest straight and report the
	// straight as passing close to itself.
	int feature_at(double distance) const {
		if (points.empty()) {
			return 0;
		}
		const std::size_t index = span_at(distance);
		const bool curving = points[index].curvature != 0.0;
		// Walk back to the first span of this run. Bounded by the span count, so a
		// track that is entirely one feature returns span zero rather than looping.
		std::size_t first = index;
		for (std::size_t step = 0; step < points.size(); ++step) {
			const std::size_t before = (first + points.size() - 1) % points.size();
			if ((points[before].curvature != 0.0) != curving) {
				break;
			}
			first = before;
		}
		return static_cast<int>(first);
	}

	// --- the checks the validator is made of ------------------------------

	struct Separation {
		double clear_ground_m = 1e30;
		double required_m = 0.0;
		double at_a_m = 0.0;
		double at_b_m = 0.0;
		double worst_slope_pct = 0.0;
		double slope_at_a_m = 0.0;
		double slope_at_b_m = 0.0;
	};

	// The self-intersection gate, both halves of it.
	//
	// **Horizontal.** Part I §7.5: *"The minimum distance between two adjacent
	// sections of the track is 6 m in any case."* Six meters of *clear ground*,
	// so the requirement between two sections is `6 + h_a + h_b` and not a flat
	// constant - a flat 14 m is only correct at the 8 m width floor and passes an
	// illegal layout anywhere wider. Four independent circuit designs published
	// that constant.
	//
	// **Vertical, which is written down nowhere and is why this returns it.** Two
	// sections can clear the horizontal rule and be stacked at a slope no legal
	// verge can bridge. §7.5 requires 1.80 m of verge on both sides continuing the
	// track's profile with no negative slope, and caps a rising run-off at 10%; a
	// 76.6% face between two sections satisfies neither. One of the four candidate
	// designs cleared the horizontal rule at 20.56 m while those same two sections
	// were 8.09 m apart vertically across 10.56 m of ground.
	//
	// The along-lap exclusion is `max(100 m, both corners' arcs)` and not the 40 m
	// an earlier brief used. A 180 deg corner of 15 m radius has 47.1 m of arc, so
	// its own entry and exit tangents are "more than 40 m apart along the lap" and
	// 30 m apart in plan - and that self-pair was the reported minimum of all four
	// candidate designs. Four for four, measuring the mouth of one hairpin.
	Separation separation(double sample_spacing = 1.0) const {
		Separation worst;
		const std::vector<Frame> line = polyline(0.05, sample_spacing);
		if (line.size() < 3) {
			return worst;
		}
		std::vector<double> arcs(line.size(), 0.0);
		std::vector<int> features(line.size(), 0);
		for (std::size_t index = 0; index < line.size(); ++index) {
			arcs[index] = corner_arc_at(line[index].distance_m);
			features[index] = feature_at(line[index].distance_m);
		}
		for (std::size_t i = 0; i + 1 < line.size(); ++i) {
			for (std::size_t j = i + 1; j + 1 < line.size(); ++j) {
				// Two points on the same continuous feature - one straight, or one
				// corner - are never two bits of track, whatever the along-lap
				// window says. This is the generalization of the design's own
				// recommendation ("exclude pairs that belong to the same corner")
				// to the case that actually bites first: the start/finish straight,
				// whose two ends are a hundred meters apart along the lap and a
				// hundred meters apart in plan, and which was reported as this
				// circuit's closest approach until it was excluded.
				if (features[i] == features[j]) {
					continue;
				}
				double along = std::fabs(line[j].distance_m - line[i].distance_m);
				along = std::fmin(along, length_m - along);
				const double window = std::fmax(100.0, std::fmax(arcs[i], arcs[j]));
				if (along < window) {
					continue;
				}
				const double dx = line[j].x - line[i].x;
				const double dz = line[j].z - line[i].z;
				const double plan = std::sqrt(dx * dx + dz * dz);
				const double required = circuit::MIN_CLEAR_GROUND_BETWEEN_SECTIONS_M
						+ 0.5 * line[i].width_m + 0.5 * line[j].width_m;
				const double clear = plan - 0.5 * line[i].width_m - 0.5 * line[j].width_m;
				if (clear < worst.clear_ground_m) {
					worst.clear_ground_m = clear;
					worst.required_m = required;
					worst.at_a_m = line[i].distance_m;
					worst.at_b_m = line[j].distance_m;
				}
				// The vertical companion. Only over ground close enough for the verge
				// and run-off rules to be about the same piece of land; 40 m is ours
				// and is stated in `docs/TRACK_SCHEMA.md`.
				if (clear > 0.1 && clear <= 40.0) {
					const double rise = std::fabs(line[j].elevation_m - line[i].elevation_m);
					const double slope = 100.0 * rise / clear;
					if (slope > worst.worst_slope_pct) {
						worst.worst_slope_pct = slope;
						worst.slope_at_a_m = line[i].distance_m;
						worst.slope_at_b_m = line[j].distance_m;
					}
				}
			}
		}
		return worst;
	}

	// The longest straight, merging across control points and across the start
	// line. The regulation caps it at 200 m for Grade 1 and that cap is what
	// decides where a kart circuit can pass at all.
	double longest_straight(double *out_start = nullptr) const {
		if (points.empty()) {
			return 0.0;
		}
		// Walked twice round so a run that crosses the start line is measured whole
		// rather than as two pieces. Valdirone's longest is exactly that: 165.0 m of
		// Rettifilo del Banco, 77 m of it before the line and 88 m after.
		double best = 0.0;
		double best_start = 0.0;
		const std::size_t count = points.size();
		for (std::size_t start = 0; start < count; ++start) {
			if (points[start].curvature != 0.0) {
				continue;
			}
			// Only start a run at a point whose predecessor turns, or the same run is
			// measured once per control point in it.
			const std::size_t before = (start + count - 1) % count;
			if (points[before].curvature == 0.0) {
				continue;
			}
			double run = 0.0;
			for (std::size_t step = 0; step < count; ++step) {
				const std::size_t index = (start + step) % count;
				if (points[index].curvature != 0.0) {
					break;
				}
				run += span_length(index);
			}
			if (run > best) {
				best = run;
				best_start = points[start].distance_m;
			}
		}
		if (out_start != nullptr) {
			*out_start = best_start;
		}
		return best;
	}

	// Distance from the start line forward to the first corner's turn-in, and back
	// from the last corner's exit to the line. Part I / Appendix 13 fix both.
	double start_to_first_corner() const {
		double best = length_m;
		for (const Corner &corner : corners) {
			const double gap = wrap(corner.from_m - start_line_m);
			if (gap < best) {
				best = gap;
			}
		}
		return best;
	}

	double last_corner_to_start() const {
		double best = length_m;
		for (const Corner &corner : corners) {
			const double gap = wrap(start_line_m - corner.to_m);
			if (gap < best) {
				best = gap;
			}
		}
		return best;
	}

	// --- validation --------------------------------------------------------

	// Every rule in `docs/TRACK_SCHEMA.md`, in its own order, returning one line
	// per problem. Empty means the track loads.
	//
	// **The loader refuses rather than warning.** A track that loads and cannot be
	// raced is worse than one that will not load: the failure surfaces at a corner
	// nobody can take, three hundred meters into a session, as a physics bug.
	//
	// ## The rule this project got wrong twice, stated once
	//
	// ARCHITECTURE §11 and ADR-0046 both say "no radius the kart physically cannot
	// take", and `track.json` stores the *centerline*. Written from those words
	// alone the check rejects every driveable circuit ever designed: nobody drives
	// the centerline, and Valdirone exceeds 1.86 g on its own centerline radius at
	// all eight corners - 3.66 g at Il Ciglione. The check has to be against the
	// racing line's radius, and the threshold against that radius is
	// `min(grip, lock)` and not the grip ceiling alone, because six of Valdirone's
	// eight corners are limited by steering lock and not by the tire.
	std::vector<std::string> validate(int expected_schema_version) const {
		std::vector<std::string> problems;
		char buffer[512];

		auto say = [&](const char *format, auto... arguments) {
			std::snprintf(buffer, sizeof(buffer), format, arguments...);
			problems.push_back(std::string(buffer));
		};

		// 1. Version refuses rather than migrates. Opposite to ADR-0042 and for the
		// same reason it applies there in reverse: a save is user data, a track is
		// authored project data under version control.
		if (schema_version != expected_schema_version) {
			say("schema_version %d is not this build's %d; a track is edited by the "
				"commit that bumps the schema, not migrated at load",
					schema_version, expected_schema_version);
			return problems;
		}

		// 2. The control points themselves.
		if (points.size() < 3) {
			say("spline has %d control points; at least 3 are needed to close a loop",
					static_cast<int>(points.size()));
			return problems;
		}
		if (std::fabs(points.front().distance_m) > 1e-9) {
			say("the first control point is at %.6f m, not at the start line",
					points.front().distance_m);
		}
		for (std::size_t index = 1; index < points.size(); ++index) {
			if (points[index].distance_m <= points[index - 1].distance_m) {
				say("control point %d is at %.6f m, not past its predecessor at %.6f m",
						static_cast<int>(index), points[index].distance_m,
						points[index - 1].distance_m);
			}
		}
		if (points.back().distance_m >= length_m) {
			say("the last control point is at %.6f m, at or past the lap length %.6f m",
					points.back().distance_m, length_m);
		}

		// 3-5. The walk closes, and the stored positions are what the walk says.
		double x = points.front().x;
		double z = points.front().z;
		double heading = points.front().heading_rad;
		double turn_total = 0.0;
		double worst_position = 0.0;
		double worst_heading = 0.0;
		std::size_t worst_index = 0;
		for (std::size_t index = 0; index < points.size(); ++index) {
			const ControlPoint &a = points[index];
			const double position_error = std::sqrt((x - a.x) * (x - a.x) + (z - a.z) * (z - a.z));
			double heading_error = heading - a.heading_rad;
			while (heading_error > kart::core::PI) {
				heading_error -= 2.0 * kart::core::PI;
			}
			while (heading_error < -kart::core::PI) {
				heading_error += 2.0 * kart::core::PI;
			}
			if (position_error > worst_position) {
				worst_position = position_error;
				worst_index = index;
			}
			worst_heading = std::fmax(worst_heading, std::fabs(heading_error));

			const double length = span_length(index);
			const double turn = a.curvature * length;
			turn_total += turn;
			if (a.curvature == 0.0) {
				double fx = 0.0;
				double fz = 0.0;
				forward_of(heading, fx, fz);
				x += fx * length;
				z += fz * length;
			} else {
				const double radius = 1.0 / a.curvature;
				double rx = 0.0;
				double rz = 0.0;
				right_of(heading, rx, rz);
				const double centre_x = x + rx * radius;
				const double centre_z = z + rz * radius;
				heading += turn;
				right_of(heading, rx, rz);
				x = centre_x - rx * radius;
				z = centre_z - rz * radius;
			}
		}
		const double closure = std::sqrt(
				(x - points.front().x) * (x - points.front().x)
				+ (z - points.front().z) * (z - points.front().z));
		if (closure > 1e-3) {
			say("the spline does not close: the walk misses its own start by %.6f m",
					closure);
		}
		if (worst_position > 1e-3) {
			say("control point %d's stored position is %.6f m from where its own "
				"curvature puts it; distance and curvature are normative and position "
				"is a checksum",
					static_cast<int>(worst_index), worst_position);
		}
		if (worst_heading > 1e-4) {
			say("a stored heading is %.6f rad from the walk's", worst_heading);
		}
		const double turns = turn_total / (2.0 * kart::core::PI);
		if (std::fabs(turns - std::round(turns)) > 1e-4) {
			say("the spline turns %.4f full turns; a closed loop turns a whole number",
					turns);
		}
		if (std::fabs(std::round(turns)) < 0.5) {
			say("the spline turns %.4f full turns net, so it is not a loop - a "
				"figure-eight closes and crosses itself",
					turns);
		}

		// 6-7. Self-intersection, both halves.
		const Separation gap = separation(1.0);
		if (gap.clear_ground_m < circuit::MIN_CLEAR_GROUND_BETWEEN_SECTIONS_M) {
			say("the track passes within %.3f m of clear ground of itself at %.1f m "
				"and %.1f m; Part I art 7.5 requires %.1f m",
					gap.clear_ground_m, gap.at_a_m, gap.at_b_m,
					circuit::MIN_CLEAR_GROUND_BETWEEN_SECTIONS_M);
		}
		if (gap.worst_slope_pct > circuit::RUNOFF_UPSLOPE_MAX_PCT) {
			say("the ground between %.1f m and %.1f m rises at %.2f%%, over art 7.5's "
				"%.1f%% cap, with no retaining structure declared",
					gap.slope_at_a_m, gap.slope_at_b_m, gap.worst_slope_pct,
					circuit::RUNOFF_UPSLOPE_MAX_PCT);
		}

		// 8. The kart, against the racing line and not against the centerline.
		for (const Corner &corner : corners) {
			if (corner.line_radius_m <= 0.0) {
				say("%s declares no line_radius_m, so no corner-speed check is possible",
						corner.name.c_str());
				continue;
			}
			// The declared grip ceiling has to be a *tire* figure. Recomputed from
			// the corner's own line radius rather than trusted: `v^2 / (rho g)` is
			// the lateral acceleration the ceiling implies, and it must land inside
			// the sourced sustained band.
			const double v = corner.grip_ceiling_kmh / 3.6;
			const double implied_g = v * v / (corner.line_radius_m * kart::core::G);
			if (implied_g < kz::LATERAL_SUSTAINED_G_MIN || implied_g > kz::LATERAL_SUSTAINED_G_MAX) {
				say("%s's grip ceiling of %.1f km/h on a %.2f m line implies %.2f g, "
					"outside the sourced sustained band %.2f-%.2f g",
						corner.name.c_str(), corner.grip_ceiling_kmh, corner.line_radius_m,
						implied_g, kz::LATERAL_SUSTAINED_G_MIN, kz::LATERAL_SUSTAINED_G_MAX);
			}
			const double ceiling = std::fmin(corner.grip_ceiling_kmh, corner.lock_ceiling_kmh);
			for (int direction = 0; direction < 2; ++direction) {
				const double apex = direction == 0 ? corner.apex_kmh : corner.reverse_apex_kmh;
				if (apex > ceiling + 0.05) {
					say("%s is taken at %.1f km/h %s, above min(grip %.1f, lock %.1f)",
							corner.name.c_str(), apex, direction == 0 ? "forward" : "reversed",
							corner.grip_ceiling_kmh, corner.lock_ceiling_kmh);
				}
			}
			// 15. Run-off is mandatory over 80 degrees, and it is measured on the
			// *corner*: Vigna is 24 deg into 71 with no straight between, and both
			// halves clear 80 on their own while the corner is 95 and mandatory.
			if (std::fabs(corner.direction_change_deg) > circuit::RUNOFF_MANDATORY_OVER_DEG
					&& !corner.has_runoff) {
				say("%s changes direction by %.1f deg and declares no run-off; art 7.5 "
					"makes one mandatory over %.0f deg",
						corner.name.c_str(), std::fabs(corner.direction_change_deg),
						circuit::RUNOFF_MANDATORY_OVER_DEG);
			}
		}

		// 9-10. Width and cross-section, at every control point.
		const double min_width = grade == 1 ? circuit::GRADE1_MIN_WIDTH_M : 6.0;
		for (std::size_t index = 0; index < points.size(); ++index) {
			const ControlPoint &point = points[index];
			if (point.width_m < min_width - 1e-9) {
				say("the road is %.2f m wide at %.1f m, under the grade's %.2f m floor",
						point.width_m, point.distance_m, min_width);
			}
			if (point.curvature == 0.0 && std::fabs(point.bank_pct) < 1e-9) {
				if (point.crown_pct < circuit::STRAIGHT_CAMBER_MIN_PCT - 1e-9
						|| point.crown_pct > circuit::STRAIGHT_CAMBER_MAX_PCT + 1e-9) {
					say("the straight at %.1f m has %.2f%% camber, outside art 7.2's "
						"%.1f-%.1f%%; a straight is never flat",
							point.distance_m, point.crown_pct,
							circuit::STRAIGHT_CAMBER_MIN_PCT, circuit::STRAIGHT_CAMBER_MAX_PCT);
				}
			}
			if (std::fabs(point.bank_pct) > circuit::CORNER_BANKING_MAX_PCT + 1e-9) {
				say("the banking at %.1f m is %.2f%%, over art 7.2's %.1f%% cap",
						point.distance_m, point.bank_pct, circuit::CORNER_BANKING_MAX_PCT);
			}
			// Adverse camber: a bank whose sign opposes its own turn. Positive
			// curvature is a right-hander and the road must fall to the right, which
			// is positive bank.
			if (point.curvature != 0.0 && point.bank_pct != 0.0) {
				const bool right_hand = point.curvature > 0.0;
				const bool falls_right = point.bank_pct > 0.0;
				if (right_hand != falls_right) {
					say("the corner at %.1f m is banked %.2f%% the wrong way; art 7.2 "
						"calls adverse camber not generally acceptable",
							point.distance_m, point.bank_pct);
				}
			}
		}

		// 11-14. The regulation's own dimensions.
		if (grade == 1 && length_m < circuit::GRADE1_MIN_LENGTH_M) {
			say("the lap is %.1f m, under Grade 1's %.0f m minimum",
					length_m, circuit::GRADE1_MIN_LENGTH_M);
		}
		double straight_start = 0.0;
		const double longest = longest_straight(&straight_start);
		if (grade == 1 && longest > circuit::GRADE1_LONGEST_STRAIGHT_MAX_M + 1e-6) {
			say("the longest straight is %.1f m from %.1f m, over Grade 1's %.0f m cap",
					longest, straight_start, circuit::GRADE1_LONGEST_STRAIGHT_MAX_M);
		}
		const double to_first = start_to_first_corner();
		if (grade == 1 && to_first < circuit::GRADE1_START_TO_FIRST_CORNER_MIN_M) {
			say("the start line is %.1f m from the first corner, under the %.0f m minimum",
					to_first, circuit::GRADE1_START_TO_FIRST_CORNER_MIN_M);
		}
		const double from_last = last_corner_to_start();
		if (grade == 1 && from_last < circuit::GRADE1_LAST_CORNER_TO_START_MIN_M) {
			say("the last corner is %.1f m from the start line, under the %.0f m minimum",
					from_last, circuit::GRADE1_LAST_CORNER_TO_START_MIN_M);
		}
		// The starting straight is the run through the line: back to the previous
		// corner plus forward to the next.
		const double starting_straight = to_first + from_last;
		if (grade == 1
				&& (starting_straight < circuit::GRADE1_STARTING_STRAIGHT_MIN_M - 1e-6
						|| starting_straight > circuit::GRADE1_STARTING_STRAIGHT_MAX_M + 1e-6)) {
			say("the starting straight is %.1f m, outside Grade 1's %.0f-%.0f m",
					starting_straight, circuit::GRADE1_STARTING_STRAIGHT_MIN_M,
					circuit::GRADE1_STARTING_STRAIGHT_MAX_M);
		}
		{
			const Frame line = sample(start_line_m);
			if (std::fabs(line.grade) * 100.0 > circuit::STARTING_STRAIGHT_MAX_GRADE_PCT + 1e-6) {
				say("the start line sits on a %.2f%% gradient, over art 7.2's %.1f%% cap",
						line.grade * 100.0, circuit::STARTING_STRAIGHT_MAX_GRADE_PCT);
			}
		}

		// 16. Vertical curves, in both directions at the same K.
		//
		// d2z/ds2 is invariant under s -> L-s, so a crest is a crest driven either
		// way: K does not swap and only the speed changes. Two of the four candidate
		// designs got this wrong in opposite directions - one swapped K and declared
		// six reverse minima legal when one was not, the other never ran a curve
		// backwards at all.
		for (std::size_t index = 0; index < vertical_curves.size(); ++index) {
			const VerticalCurve &curve = vertical_curves[index];
			const double expected_k = curve.convex ? circuit::VERTICAL_K_CONVEX
													: circuit::VERTICAL_K_CONCAVE;
			if (std::fabs(curve.K - expected_k) > 1e-9) {
				say("vertical curve at %.1f m is %s and declares K = %.1f, not %.1f",
						curve.at_m, curve.convex ? "convex" : "concave", curve.K, expected_k);
			}
			const double speed = std::fmax(curve.speed_forward_kmh, curve.speed_reverse_kmh);
			const double needed = circuit::min_vertical_radius_m(speed, expected_k);
			if (curve.radius_m < needed - 1e-6) {
				say("vertical curve at %.1f m has radius %.0f m against %.0f m needed "
					"at %.1f km/h",
						curve.at_m, curve.radius_m, needed, speed);
			}
			// The declaration has to agree with the spline it describes: the profile
			// is authored once, and a curve edited in one place and declared in
			// another is exactly the class of defect this cross-check exists for.
			const Frame entering = sample(wrap(curve.at_m - curve.length_m * 0.5));
			const Frame leaving = sample(wrap(curve.at_m + curve.length_m * 0.5));
			if (std::fabs(entering.grade - curve.grade_in) > 5e-5) {
				say("vertical curve at %.1f m declares an entry grade of %.4f%% and the "
					"spline reads %.4f%%",
						curve.at_m, curve.grade_in * 100.0, entering.grade * 100.0);
			}
			if (std::fabs(leaving.grade - curve.grade_out) > 5e-5) {
				say("vertical curve at %.1f m declares an exit grade of %.4f%% and the "
					"spline reads %.4f%%",
						curve.at_m, curve.grade_out * 100.0, leaving.grade * 100.0);
			}
		}
		// The profile *closes* structurally and there is nothing to check there:
		// elevation is stored per control point rather than integrated from grades,
		// so the spans' height changes telescope to zero over a lap by construction.
		// A check would only ever restate the format.
		//
		// What can go wrong is the other thing, and it is worth a rule. Cubic
		// Hermite will happily interpolate a 3 m height change across a span whose
		// two ends are both flat, and what it draws is a hump the design does not
		// contain and the collider agrees with. The invariant that rules it out is
		// exact for both shapes the schema allows: on a constant-grade band the
		// mean grade is the grade, and on a parabolic vertical curve it is the mean
		// of the two end grades. Either way it lies between them.
		for (std::size_t index = 0; index < points.size(); ++index) {
			const ControlPoint &a = points[index];
			const ControlPoint &b = points[(index + 1) % points.size()];
			const double span = span_length(index);
			if (span <= 1e-3) {
				// A span of no length is a division by nearly zero in every consumer
				// and a landmine in the projection. It comes from two authored
				// stations landing on the same place - a taper that ends exactly
				// where a segment begins - and the authoring tool merges them.
				say("control points %d and %d are %.6f m apart; a span must have length",
						static_cast<int>(index),
						static_cast<int>((index + 1) % points.size()), span);
				continue;
			}
			const double mean = (b.elevation_m - a.elevation_m) / span;
			const double low = std::fmin(a.grade, b.grade);
			const double high = std::fmax(a.grade, b.grade);
			// Two microns of authored rounding over the span, plus a floor for the
			// short ones.
			const double tolerance = std::fmax(1e-5, 2e-6 / span);
			if (mean < low - tolerance || mean > high + tolerance) {
				say("the span at %.1f m climbs at a mean %.4f%% while its ends read "
					"%.4f%% and %.4f%%; Hermite would draw a hump the design does not have",
						a.distance_m, mean * 100.0, a.grade * 100.0, b.grade * 100.0);
			}
		}

		// 17-20. Furniture, per layout.
		for (const Layout &layout : layouts) {
			validate_layout(layout, problems);
		}
		return problems;
	}

	void validate_layout(const Layout &layout, std::vector<std::string> &problems) const {
		char buffer[512];
		auto say = [&](const char *format, auto... arguments) {
			std::snprintf(buffer, sizeof(buffer), format, arguments...);
			problems.push_back(std::string(buffer));
		};

		// 17. A grid slot has to be on the road, with the kart's own width and the
		// edge line it must stay inside of. Slot count is data and not a constant -
		// ADR-0046 - so this is a check and not an assumption about eight karts.
		for (const GridSlot &slot : layout.grid) {
			const Frame here = sample(to_forward(layout, slot.distance_m));
			const double needed = std::fabs(slot.lateral_m) + 0.5 * 1.400 + circuit::EDGE_LINE_MAX_WIDTH_M;
			if (needed > 0.5 * here.width_m + 1e-9) {
				say("%s grid slot %d at %.1f m needs %.2f m of half-road and has %.2f m",
						layout.name.c_str(), slot.position, slot.distance_m, needed,
						0.5 * here.width_m);
			}
			if (here.curvature != 0.0) {
				say("%s grid slot %d at %.1f m is inside a corner; a standing start on "
					"camber is not a standing start",
						layout.name.c_str(), slot.position, slot.distance_m);
			}
		}

		// 18. Sector marks: ordered, distinct, on the lap. A mark is a diagnostic
		// partition and two marks at the same station partition nothing.
		for (std::size_t index = 0; index < layout.sector_marks_m.size(); ++index) {
			const double mark = layout.sector_marks_m[index];
			if (mark <= 0.0 || mark >= length_m) {
				say("%s sector mark %d is at %.1f m, off a lap of %.1f m",
						layout.name.c_str(), static_cast<int>(index), mark, length_m);
			}
			if (index > 0 && mark <= layout.sector_marks_m[index - 1] + 1e-6) {
				say("%s sector marks are not increasing: %.1f m after %.1f m",
						layout.name.c_str(), mark, layout.sector_marks_m[index - 1]);
			}
		}

		// 19. Checkpoints: ordered, distinct, spaced, and closing the loop. The
		// wrap from the last back to the first counts as a spacing, which is the
		// half that is easy to leave out and is exactly where a kart would cut.
		if (layout.checkpoints_m.size() < 3) {
			say("%s has %d checkpoints; a lap cannot be validated with fewer than 3",
					layout.name.c_str(), static_cast<int>(layout.checkpoints_m.size()));
		} else {
			for (std::size_t index = 0; index < layout.checkpoints_m.size(); ++index) {
				const double here = layout.checkpoints_m[index];
				if (here < 0.0 || here >= length_m) {
					say("%s checkpoint %d is at %.1f m, off a lap of %.1f m",
							layout.name.c_str(), static_cast<int>(index), here, length_m);
				}
				const std::size_t next_index = (index + 1) % layout.checkpoints_m.size();
				double spacing = layout.checkpoints_m[next_index] - here;
				if (spacing <= 0.0) {
					spacing += length_m;
				}
				if (spacing > circuit::ours::CHECKPOINT_MAX_SPACING_M + 1e-6) {
					say("%s checkpoints %d and %d are %.1f m apart, over the %.0f m "
						"anti-cut spacing",
							layout.name.c_str(), static_cast<int>(index),
							static_cast<int>(next_index), spacing,
							circuit::ours::CHECKPOINT_MAX_SPACING_M);
				}
			}
		}

		// 20. Pit stations. A deceleration lane branches off a straight; branching
		// off an arc means a merge angle that changes along the junction.
		const double pit_stations[2] = { layout.pit_entry_m, layout.pit_exit_m };
		const char *pit_names[2] = { "entry", "exit" };
		for (int index = 0; index < 2; ++index) {
			if (pit_stations[index] < 0.0) {
				continue;
			}
			if (pit_stations[index] >= length_m) {
				say("%s pit %s is at %.1f m, off a lap of %.1f m", layout.name.c_str(),
						pit_names[index], pit_stations[index], length_m);
				continue;
			}
			const Frame here = sample(to_forward(layout, pit_stations[index]));
			if (here.curvature != 0.0) {
				say("%s pit %s at %.1f m is inside a corner", layout.name.c_str(),
						pit_names[index], pit_stations[index]);
			}
		}
	}
};

}  // namespace kart::core::track

#endif  // KART_CORE_TRACK_H
