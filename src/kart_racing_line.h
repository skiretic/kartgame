#ifndef KARTGAME_KART_RACING_LINE_H
#define KARTGAME_KART_RACING_LINE_H

#include "core/racing_line.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace kartgame {

// GDScript's handle on `kart::core::line::RacingLine`. ROADMAP M7.
//
// Thin, in the sense `kart_state_hash.h` is thin: it unpacks Variants, hands
// doubles to the core class and packs the answer back. Every decision - the
// curvature minimization, the corridor, the friction ellipse, the gear choice -
// is in `src/core/racing_line.h`, where `tests/run.sh` reaches it in five
// seconds with no engine at all. ADR-0017 is the reason and this class is where
// that boundary is crossed.
//
// ## What a caller has to do, in order
//
//     var line := KartRacingLine.new()
//     line.build_from_course(track, 1.5)     # any course with length() + sample()
//     print(line.summary()["lap_time"])
//
// `build_from_course` is duck typed, exactly as `SessionRunner.configure` is and
// for the same reason: `KartTrack` is a GDExtension `RefCounted` and
// `TrackLayout` is a GDScript `Node3D` script, so they cannot share a base
// class. Two methods are required - `length()` and `sample(station)` - and the
// sample must carry `curvature`, `width`, `position` and `heading`. `grade`,
// `bank_pct` and `elevation` are read when present and are zero when not.
//
// A course that has neither - the test track's `TrackLayout` publishes samples
// with no curvature and no width at all - goes through `begin` and
// `set_station` instead, which is the same path with the sampling left to the
// caller. That is not a fallback: it is the honest interface, because only the
// course knows how to evaluate itself exactly.
//
// ## What this class is for
//
// The AI driver, next wave. It owns pure pursuit, the speed PID, the shift
// hysteresis and the difficulty tiers; this owns the geometry and the numbers
// those consume. The split is drawn at "what does the road allow" against "what
// does this driver do about it", which is also why `grip_usage` lives here and
// the difficulty *tier* does not.
class KartRacingLine : public godot::RefCounted {
	GDCLASS(KartRacingLine, godot::RefCounted)

public:
	KartRacingLine();
	~KartRacingLine() override = default;

	// --- building -----------------------------------------------------------

	// Choose the grid. Returns the station count, a power of two.
	int begin(double p_length_m, double p_spacing_m = 1.5);

	// One centerline station. `position` is (x, elevation, z) in Godot's frame -
	// the same shape `KartTrack.sample()["position"]` returns - and `heading`,
	// `curvature`, `grade` and `bank` follow `track.h`'s conventions: heading
	// zero is -Z, curvature is positive for a right-hander, grade and bank are
	// fractions rather than percent.
	void set_station(int p_index, const godot::Vector3 &p_position, double p_heading,
			double p_curvature, double p_half_width, double p_grade, double p_bank,
			double p_surface_grip);

	// Fill the grid from any course that can measure itself, then solve. Returns
	// false and pushes an error if the course cannot answer.
	bool build_from_course(godot::Object *p_course, double p_spacing_m = 1.5);

	// Solve what has been filled in. `build_from_course` calls this for you.
	bool solve();

	// --- the driver's dials -------------------------------------------------

	// 0..1. **1.0 is the reference line**; the AI's difficulty tiers move it and
	// nothing else in this class knows what a tier is.
	void set_grip_usage(double p_usage);
	double get_grip_usage() const;

	// Corridor inset from each edge, meters. Defaults to half the kart plus the
	// white line, which is the same sum track limits are policed with.
	void set_edge_margin(double p_margin_m);
	double get_edge_margin() const;

	// Sprockets, because `gearbox.h` calls them the one thing a track author is
	// expected to change and the whole speed profile turns on them.
	void set_sprockets(int p_engine_teeth, int p_axle_teeth);

	// Override `tire.h`'s peak friction coefficient on both axes.
	//
	// **Not a tuning knob and not for driving.** The default is whatever `tire.h`
	// says at run time, which is the whole point of this file. This exists so a
	// gate can ask the one question a real tire cannot: what happens when the
	// grip is good enough that the *geometry* is what stops the kart. Issue #137
	// will move the real number; `line_probe.gd --break=rollover` moves it to 8
	// and requires the rollover ceiling to hold anyway.
	void set_peak_friction(double p_peak);
	double get_peak_friction() const;

	// --- reading it back ----------------------------------------------------

	bool is_solved() const;
	int station_count() const;
	double spacing() const;
	double length() const;

	// Everything `LineSummary` holds, by name.
	godot::Dictionary summary() const;
	// The ceilings the model answered with, in g and in newtons, so a probe can
	// report what the profile was measured against rather than restating it.
	godot::Dictionary model() const;

	// One station: position, offset, curvature, radius, speed, gear, rpm, the
	// two ceilings and what the profile demands of them.
	godot::Dictionary station(int p_index) const;

	// The same, at an arbitrary **centerline** distance, linearly interpolated.
	// This is what a pure-pursuit controller wants: it already has a station from
	// `KartTrack.project`, and it needs the line's point and speed there.
	godot::Dictionary at_station(double p_centerline_distance_m) const;

	godot::PackedVector3Array points() const;
	godot::PackedFloat64Array speeds() const;
	godot::PackedInt32Array gears() const;

	// One entry per braking zone: `{station, speed, speed_kmh}`.
	godot::TypedArray<godot::Dictionary> braking_points() const;

	// The two gates, as numbers. Neither names an acceleration, which is the
	// whole point: they stay meaningful when issue #137's fix moves the tire.
	double worst_lateral_utilization() const;
	double worst_combined_utilization() const;

protected:
	static void _bind_methods();

private:
	kart::core::line::SpeedModel _model;
	kart::core::line::LineOptions _options;
	kart::core::line::RacingLine _line;
	bool _solved = false;
	int _filled = 0;

	godot::Dictionary _station_dictionary(int p_index) const;
};

} // namespace kartgame

#endif // KARTGAME_KART_RACING_LINE_H
