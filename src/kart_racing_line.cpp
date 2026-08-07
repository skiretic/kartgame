#include "kart_racing_line.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace kl = kart::core::line;

namespace kartgame {

KartRacingLine::KartRacingLine() {
	_model = kl::default_speed_model();
}

void KartRacingLine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin", "length_m", "spacing_m"), &KartRacingLine::begin,
			DEFVAL(1.5));
	ClassDB::bind_method(D_METHOD("set_station", "index", "position", "heading", "curvature",
								 "half_width", "grade", "bank", "surface_grip"),
			&KartRacingLine::set_station, DEFVAL(0.0), DEFVAL(0.0), DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("build_from_course", "course", "spacing_m"),
			&KartRacingLine::build_from_course, DEFVAL(1.5));
	ClassDB::bind_method(D_METHOD("solve"), &KartRacingLine::solve);

	ClassDB::bind_method(D_METHOD("set_grip_usage", "usage"), &KartRacingLine::set_grip_usage);
	ClassDB::bind_method(D_METHOD("get_grip_usage"), &KartRacingLine::get_grip_usage);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grip_usage"), "set_grip_usage", "get_grip_usage");
	ClassDB::bind_method(D_METHOD("set_edge_margin", "margin_m"), &KartRacingLine::set_edge_margin);
	ClassDB::bind_method(D_METHOD("get_edge_margin"), &KartRacingLine::get_edge_margin);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_margin"), "set_edge_margin", "get_edge_margin");
	ClassDB::bind_method(D_METHOD("set_sprockets", "engine_teeth", "axle_teeth"),
			&KartRacingLine::set_sprockets);
	ClassDB::bind_method(D_METHOD("set_peak_friction", "peak"), &KartRacingLine::set_peak_friction);
	ClassDB::bind_method(D_METHOD("get_peak_friction"), &KartRacingLine::get_peak_friction);

	ClassDB::bind_method(D_METHOD("is_solved"), &KartRacingLine::is_solved);
	ClassDB::bind_method(D_METHOD("station_count"), &KartRacingLine::station_count);
	ClassDB::bind_method(D_METHOD("spacing"), &KartRacingLine::spacing);
	ClassDB::bind_method(D_METHOD("length"), &KartRacingLine::length);
	ClassDB::bind_method(D_METHOD("summary"), &KartRacingLine::summary);
	ClassDB::bind_method(D_METHOD("model"), &KartRacingLine::model);
	ClassDB::bind_method(D_METHOD("station", "index"), &KartRacingLine::station);
	ClassDB::bind_method(D_METHOD("at_station", "centerline_distance_m"),
			&KartRacingLine::at_station);
	ClassDB::bind_method(D_METHOD("points"), &KartRacingLine::points);
	ClassDB::bind_method(D_METHOD("speeds"), &KartRacingLine::speeds);
	ClassDB::bind_method(D_METHOD("gears"), &KartRacingLine::gears);
	ClassDB::bind_method(D_METHOD("braking_points"), &KartRacingLine::braking_points);
	ClassDB::bind_method(D_METHOD("worst_lateral_utilization"),
			&KartRacingLine::worst_lateral_utilization);
	ClassDB::bind_method(D_METHOD("worst_combined_utilization"),
			&KartRacingLine::worst_combined_utilization);
}

// --- building ----------------------------------------------------------------

int KartRacingLine::begin(double p_length_m, double p_spacing_m) {
	_solved = false;
	_filled = _line.begin(p_length_m, p_spacing_m);
	return _filled;
}

void KartRacingLine::set_station(int p_index, const Vector3 &p_position, double p_heading,
		double p_curvature, double p_half_width, double p_grade, double p_bank,
		double p_surface_grip) {
	// `position.y` is the elevation. Godot's ground plane is x/z and every other
	// consumer in this project agrees, so unpacking it here rather than asking a
	// caller for four numbers keeps the two conventions in one place.
	_line.set_station(p_index, p_position.x, p_position.z, p_heading, p_curvature, p_half_width,
			p_position.y, p_grade, p_bank, p_surface_grip);
}

bool KartRacingLine::build_from_course(Object *p_course, double p_spacing_m) {
	if (p_course == nullptr) {
		UtilityFunctions::push_error("KartRacingLine: no course");
		return false;
	}
	// Checked by name, because a duck-typed course cannot be checked by type.
	// Named individually rather than as a set so the error says which one is
	// missing - "a course needs four methods" is not a message anybody can act on.
	const char *required[2] = { "length", "sample" };
	for (int index = 0; index < 2; ++index) {
		if (!p_course->has_method(required[index])) {
			UtilityFunctions::push_error(
					String("KartRacingLine: the course has no ") + String(required[index]) + String("()"));
			return false;
		}
	}

	const double length = double(p_course->call("length"));
	const int count = begin(length, p_spacing_m);
	if (count <= 0) {
		UtilityFunctions::push_error("KartRacingLine: the course reports no length");
		return false;
	}

	for (int index = 0; index < count; ++index) {
		const double station = _line.station_distance(index);
		const Dictionary sample = p_course->call("sample", station);
		if (!sample.has("position") || !sample.has("heading") || !sample.has("curvature") ||
				!sample.has("width")) {
			UtilityFunctions::push_error(
					"KartRacingLine: a course sample needs position, heading, curvature and width");
			return false;
		}
		// **Indexed, not `get(key, default)`.** These four are a contract, and a
		// renamed key read with a default does not fail loudly - it draws a zero
		// forever, which is how `KartRig` steered a kart whose front wheels never
		// moved for a milestone.
		const Vector3 position = sample["position"];
		const double heading = sample["heading"];
		const double curvature = sample["curvature"];
		const double width = sample["width"];
		// These three are genuinely optional: a flat, unbanked test track has no
		// business inventing keys it does not have.
		const double grade = sample.has("grade") ? double(sample["grade"]) : 0.0;
		const double bank = sample.has("bank_pct") ? double(sample["bank_pct"]) / 100.0 : 0.0;
		const double grip = sample.has("surface_grip") ? double(sample["surface_grip"]) : 1.0;

		set_station(index, position, heading, curvature, 0.5 * width, grade, bank, grip);
	}
	return solve();
}

bool KartRacingLine::solve() {
	if (_filled < kl::COARSEST_STATIONS) {
		UtilityFunctions::push_error("KartRacingLine: nothing to solve; call begin() first");
		return false;
	}
	_line.solve(_model, _options);
	_solved = true;
	return true;
}

// --- the driver's dials -------------------------------------------------------

void KartRacingLine::set_grip_usage(double p_usage) {
	// Clamped rather than asserted: this is reachable from a difficulty setting,
	// and a zero would divide the whole profile by nothing.
	double usage = p_usage;
	if (usage < 0.05) {
		usage = 0.05;
	}
	if (usage > 1.0) {
		usage = 1.0;
	}
	_model.grip_usage = usage;
	_solved = false;
}

double KartRacingLine::get_grip_usage() const {
	return _model.grip_usage;
}

void KartRacingLine::set_edge_margin(double p_margin_m) {
	_options.edge_margin_m = p_margin_m > 0.0 ? p_margin_m : 0.0;
	_solved = false;
}

double KartRacingLine::get_edge_margin() const {
	return _options.edge_margin_m;
}

void KartRacingLine::set_sprockets(int p_engine_teeth, int p_axle_teeth) {
	if (p_engine_teeth > 0) {
		_model.gearbox.engine_sprocket_teeth = p_engine_teeth;
	}
	if (p_axle_teeth > 0) {
		_model.gearbox.axle_sprocket_teeth = p_axle_teeth;
	}
	_solved = false;
}

void KartRacingLine::set_peak_friction(double p_peak) {
	if (p_peak <= 0.0) {
		return;
	}
	_model.tire.lateral.peak_friction = p_peak;
	_model.tire.longitudinal.peak_friction = p_peak;
	// The cached peak slips go stale the moment a coefficient moves, and
	// `tire.h` says in its own words that a caller who mutates a curve must
	// refresh them: a silently stale peak makes `slide` wrong in a way nothing
	// else notices.
	_model.tire.refresh_peaks();
	_solved = false;
}

double KartRacingLine::get_peak_friction() const {
	return _model.tire.lateral.peak_friction;
}

// --- reading it back ----------------------------------------------------------

bool KartRacingLine::is_solved() const {
	return _solved;
}

int KartRacingLine::station_count() const {
	return _line.station_count();
}

double KartRacingLine::spacing() const {
	return _line.spacing_m();
}

double KartRacingLine::length() const {
	return _line.summary().line_length_m;
}

Dictionary KartRacingLine::summary() const {
	const kl::LineSummary &found = _line.summary();
	Dictionary out;
	out["stations"] = found.stations;
	out["centerline_length"] = found.centerline_length_m;
	out["line_length"] = found.line_length_m;
	out["centerline_objective"] = found.centerline_objective;
	out["line_objective"] = found.line_objective;
	out["max_curvature"] = found.max_curvature;
	out["min_radius"] = found.min_radius_m;
	out["centerline_max_curvature"] = found.centerline_max_curvature;
	out["centerline_min_radius"] = found.centerline_min_radius_m;
	out["max_offset"] = found.max_offset_m;
	out["corridor_slack"] = found.corridor_slack_m;
	out["max_lateral_g"] = found.max_lateral_g;
	out["max_lateral_g_at"] = found.max_lateral_g_at_m;
	out["max_braking_g"] = found.max_braking_g;
	out["max_braking_g_at"] = found.max_braking_g_at_m;
	out["max_traction_g"] = found.max_traction_g;
	out["brake_limit_g"] = found.brake_limit_g;
	out["traction_limit_g"] = found.traction_limit_g;
	out["min_tire_ceiling_g"] = found.min_tire_ceiling_g;
	out["min_rollover_ceiling_g"] = found.min_rollover_ceiling_g;
	out["rollover_bound_stations"] = found.rollover_bound_stations;
	out["min_speed"] = found.min_speed_ms;
	out["max_speed"] = found.max_speed_ms;
	out["min_speed_kmh"] = found.min_speed_ms * kart::core::KMH_PER_MS;
	out["max_speed_kmh"] = found.max_speed_ms * kart::core::KMH_PER_MS;
	// **Reported, never gated.** A lap time is a function of the tire model and
	// issue #137 is open; a gate on it would go red the day the tire got better.
	out["lap_time"] = found.lap_time_s;
	out["braking_zones"] = found.braking_zones;
	out["shifts"] = found.shifts;
	Array gears;
	for (int gear = 0; gear <= kart::core::kz::GEAR_COUNT; ++gear) {
		gears.push_back(found.gear_stations[gear]);
	}
	out["gear_stations"] = gears;
	Array levels;
	for (int level = 0; level < found.levels; ++level) {
		levels.push_back(found.level_objective[level]);
	}
	out["level_objective"] = levels;
	return out;
}

Dictionary KartRacingLine::model() const {
	Dictionary out;
	out["mass"] = _model.mass_kg;
	out["com_height"] = _model.com_height_m;
	out["rollover_arm_left"] = _model.rollover_arm_left_m;
	out["rollover_arm_right"] = _model.rollover_arm_right_m;
	out["rollover_left_g"] = _model.rollover_arm_left_m / _model.com_height_m;
	out["rollover_right_g"] = _model.rollover_arm_right_m / _model.com_height_m;
	double tire_left = 0.0;
	double tire_right = 0.0;
	const double flat_left = _model.lateral_limit(0.0, true, &tire_left, nullptr);
	const double flat_right = _model.lateral_limit(0.0, false, &tire_right, nullptr);
	out["lateral_left_g"] = flat_left / kart::core::G;
	out["lateral_right_g"] = flat_right / kart::core::G;
	out["tire_left_g"] = tire_left / kart::core::G;
	out["tire_right_g"] = tire_right / kart::core::G;
	out["brake_g"] = _model.brake_limit() / kart::core::G;
	out["traction_force"] = _model.traction_force();
	out["traction_g"] = _model.traction_force() / _model.mass_kg / kart::core::G;
	out["peak_friction"] = _model.tire.lateral.peak_friction;
	out["grip_usage"] = _model.grip_usage;
	out["rolling_radius"] = _model.rolling_radius_m;
	out["speed_ceiling_kmh"] = _model.speed_ceiling() * kart::core::KMH_PER_MS;
	out["engine_sprocket_teeth"] = _model.gearbox.engine_sprocket_teeth;
	out["axle_sprocket_teeth"] = _model.gearbox.axle_sprocket_teeth;
	return out;
}

Dictionary KartRacingLine::_station_dictionary(int p_index) const {
	Dictionary out;
	out["index"] = p_index;
	out["station"] = _line.station_distance(p_index);
	out["line_distance"] = _line.line_distance(p_index);
	out["position"] = Vector3(_line.x(p_index), _line.elevation(p_index), _line.z(p_index));
	out["offset"] = _line.offset(p_index);
	out["curvature"] = _line.curvature(p_index);
	const double curvature = _line.curvature(p_index);
	out["radius"] = curvature != 0.0 ? 1.0 / (curvature < 0.0 ? -curvature : curvature) : 0.0;
	out["centerline_curvature"] = _line.centerline_curvature(p_index);
	out["speed"] = _line.speed(p_index);
	out["speed_kmh"] = _line.speed(p_index) * kart::core::KMH_PER_MS;
	out["corner_speed"] = _line.corner_speed(p_index);
	out["gear"] = _line.gear(p_index);
	out["rpm"] = _line.engine_rpm(p_index);
	out["braking"] = _line.braking(p_index);
	out["lateral_g"] = _line.lateral_demand(p_index) / kart::core::G;
	out["lateral_ceiling_g"] = _line.lateral_ceiling(p_index) / kart::core::G;
	out["longitudinal_g"] = _line.longitudinal(p_index) / kart::core::G;
	out["combined_utilization"] = _line.combined_utilization(p_index);
	return out;
}

Dictionary KartRacingLine::station(int p_index) const {
	Dictionary out;
	const int count = _line.station_count();
	if (count <= 0 || p_index < 0 || p_index >= count) {
		return out;
	}
	return _station_dictionary(p_index);
}

Dictionary KartRacingLine::at_station(double p_centerline_distance_m) const {
	Dictionary out;
	const int count = _line.station_count();
	if (count <= 0) {
		return out;
	}
	const double length = _line.spacing_m() * double(count);
	double distance = std::fmod(p_centerline_distance_m, length);
	if (distance < 0.0) {
		distance += length;
	}
	const double exact = distance / _line.spacing_m();
	int low = static_cast<int>(std::floor(exact));
	if (low < 0) {
		low = 0;
	}
	if (low >= count) {
		low = count - 1;
	}
	const int high = (low + 1) % count;
	const double t = exact - double(low);

	out = _station_dictionary(low);
	// Only the continuous quantities are interpolated. The gear is not, and the
	// braking flag is not: half a gear is not a gear, and rounding one would put
	// the AI in fifth for one tick on the way from fourth to sixth.
	const Vector3 a(_line.x(low), _line.elevation(low), _line.z(low));
	const Vector3 b(_line.x(high), _line.elevation(high), _line.z(high));
	out["position"] = a.lerp(b, t);
	out["station"] = distance;
	out["offset"] = _line.offset(low) + (_line.offset(high) - _line.offset(low)) * t;
	const double speed = _line.speed(low) + (_line.speed(high) - _line.speed(low)) * t;
	out["speed"] = speed;
	out["speed_kmh"] = speed * kart::core::KMH_PER_MS;
	const double curvature =
			_line.curvature(low) + (_line.curvature(high) - _line.curvature(low)) * t;
	out["curvature"] = curvature;
	out["radius"] = curvature != 0.0 ? 1.0 / (curvature < 0.0 ? -curvature : curvature) : 0.0;
	out["lateral_g"] = speed * speed * (curvature < 0.0 ? -curvature : curvature) / kart::core::G;
	return out;
}

PackedVector3Array KartRacingLine::points() const {
	PackedVector3Array out;
	const int count = _line.station_count();
	out.resize(count + 1);
	for (int index = 0; index < count; ++index) {
		out[index] = Vector3(_line.x(index), _line.elevation(index), _line.z(index));
	}
	// The closing point is the first one again, bit-identical, so a caller that
	// draws the line gets a closed loop rather than one with a gap in it. Same
	// rule `track.h::polyline` follows and for the same reason.
	if (count > 0) {
		out[count] = out[0];
	}
	return out;
}

PackedFloat64Array KartRacingLine::speeds() const {
	PackedFloat64Array out;
	const int count = _line.station_count();
	out.resize(count);
	for (int index = 0; index < count; ++index) {
		out[index] = _line.speed(index);
	}
	return out;
}

PackedInt32Array KartRacingLine::gears() const {
	PackedInt32Array out;
	const int count = _line.station_count();
	out.resize(count);
	for (int index = 0; index < count; ++index) {
		out[index] = _line.gear(index);
	}
	return out;
}

TypedArray<Dictionary> KartRacingLine::braking_points() const {
	TypedArray<Dictionary> out;
	const int count = _line.station_count();
	for (int index = 0; index < count; ++index) {
		const int before = (index + count - 1) % count;
		if (!_line.braking(index) || _line.braking(before)) {
			continue;
		}
		Dictionary entry;
		entry["index"] = index;
		entry["station"] = _line.station_distance(index);
		entry["speed"] = _line.speed(index);
		entry["speed_kmh"] = _line.speed(index) * kart::core::KMH_PER_MS;
		entry["gear"] = _line.gear(index);
		out.push_back(entry);
	}
	return out;
}

double KartRacingLine::worst_lateral_utilization() const {
	return _line.worst_lateral_utilization();
}

double KartRacingLine::worst_combined_utilization() const {
	return _line.worst_combined_utilization();
}

} // namespace kartgame
