#include "track/kart_track.h"

#include "core/circuit_reference.h"
#include "core/surface.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cmath>
#include <cstdint>

using namespace godot;
namespace ct = kart::core::track;

namespace kartgame {

namespace {

// The verge, meters each side. Part I §7.5's minimum: the track *"must be bordered
// all along its length on both sides by compact verges having an even surface and
// having a minimum width of 1.80 m"*, grass-covered or compacted ground over at
// least 1 m of it. `circuit::VERGE_MIN_WIDTH_M` is the sourced constant; this is
// the built width, and they are the same number because a verge wider than the
// minimum is width spent on nothing.
constexpr double VERGE_WIDTH_M = kart::core::circuit::VERGE_MIN_WIDTH_M;

// --- reading a Variant without trusting it ---------------------------------
//
// Godot's JSON parser hands back `Dictionary` and `Array` of `Variant`, and a
// missing key silently reads as `nil`, which converts to 0.0. That is exactly the
// failure this project has already paid for once at the other end of the
// pipeline: an absent field that reads as a plausible number is a track that
// loads with a corner of zero radius. So every read goes through one of these,
// and the ones that matter record a problem when the key is absent.

double number_of(const Dictionary &p_dictionary, const char *p_key, double p_fallback) {
	if (!p_dictionary.has(p_key)) {
		return p_fallback;
	}
	return double(p_dictionary[p_key]);
}

String text_of(const Dictionary &p_dictionary, const char *p_key) {
	if (!p_dictionary.has(p_key)) {
		return String();
	}
	return String(p_dictionary[p_key]);
}

int side_from(const String &p_side) {
	if (p_side == "left") {
		return ct::SIDE_LEFT;
	}
	if (p_side == "right") {
		return ct::SIDE_RIGHT;
	}
	if (p_side == "both") {
		return ct::SIDE_BOTH;
	}
	return ct::SIDE_FULL;
}

// The strings in the file become `src/core/surface.h`'s integers here and in no
// other place. That header calls those integers a wire format and says nothing is
// ever renumbered, so the mapping is written out rather than derived from the
// order of an array.
int surface_from(const String &p_type) {
	if (p_type == "curb") {
		return kart::core::SURFACE_CURB;
	}
	if (p_type == "grass") {
		return kart::core::SURFACE_GRASS;
	}
	if (p_type == "dirt" || p_type == "gravel") {
		return kart::core::SURFACE_DIRT;
	}
	return kart::core::SURFACE_ASPHALT;
}

int profile_from(const String &p_profile) {
	if (p_profile == "rippled") {
		return ct::CURB_RIPPLED;
	}
	if (p_profile == "vertical") {
		return ct::CURB_VERTICAL;
	}
	return ct::CURB_FLAT;
}

// --- triangles -------------------------------------------------------------
//
// Godot's front face is the one whose vertices wind so that `(b-a) x (c-a)`
// points **against** the surface normal. That is the opposite of the usual
// right-hand convention and it was measured off `PlaneMesh.get_mesh_arrays()`
// rather than recalled - `scripts/track/track_ribbon.gd` carries the same note
// for the same reason. Inverted winding is invisible when a material has
// backface culling off, and this project has already shipped inward-wound
// geometry for two milestones with every render looking correct.
void add_triangle(PackedVector3Array &r_faces, const Vector3 &p_a, const Vector3 &p_b,
		const Vector3 &p_c, const Vector3 &p_facing) {
	const Vector3 cross = (p_b - p_a).cross(p_c - p_a);
	if (cross.length_squared() < 1e-12) {
		// A ramped kerb ends in a zero-height wall, and a zero-area triangle has no
		// normal to reason about. Dropped rather than emitted.
		return;
	}
	r_faces.push_back(p_a);
	if (cross.dot(p_facing) > 0.0) {
		r_faces.push_back(p_c);
		r_faces.push_back(p_b);
	} else {
		r_faces.push_back(p_b);
		r_faces.push_back(p_c);
	}
}

void add_quad(PackedVector3Array &r_faces, const Vector3 &p_a, const Vector3 &p_b,
		const Vector3 &p_c, const Vector3 &p_d, const Vector3 &p_facing) {
	add_triangle(r_faces, p_a, p_b, p_c, p_facing);
	add_triangle(r_faces, p_a, p_c, p_d, p_facing);
}

}  // namespace

// --- loading ---------------------------------------------------------------

Error KartTrack::load(const String &p_path) {
	_problems.clear();
	_loaded = false;
	_layout_index = -1;
	_layout_name = String();
	_track = ct::Track();
	_path = p_path;

	if (!FileAccess::file_exists(p_path)) {
		_problems.push_back(vformat("no track at %s", p_path));
		return ERR_FILE_NOT_FOUND;
	}
	const String text = FileAccess::get_file_as_string(p_path);
	if (text.is_empty()) {
		_problems.push_back(vformat("%s is empty or unreadable", p_path));
		return ERR_FILE_CANT_READ;
	}

	// FNV-1a over the file's own bytes rather than over the parsed structure, so
	// that a reformat is a content change and a replay recorded against the old
	// file says so. ADR-0041.
	{
		const PackedByteArray bytes = text.to_utf8_buffer();
		uint64_t hash = 1469598103934665603ULL;
		for (int64_t index = 0; index < bytes.size(); ++index) {
			hash ^= static_cast<uint64_t>(bytes[index]);
			hash *= 1099511628211ULL;
		}
		// `String.pad_zeros` counts only *digit* characters, so a hex string
		// starting with a letter gets its padding inserted after the letters and one
		// made entirely of letters is padded as if it were empty. Padded by hand,
		// the same way `KartStateHash::hex` does.
		String hex = String::num_uint64(hash, 16);
		while (hex.length() < 16) {
			hex = "0" + hex;
		}
		_content_hash = hex;
	}

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(text) != OK) {
		_problems.push_back(vformat("%s line %d: %s", p_path, json->get_error_line(),
				json->get_error_message()));
		return ERR_PARSE_ERROR;
	}
	const Variant parsed = json->get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		_problems.push_back("the track's top level is not an object");
		return ERR_PARSE_ERROR;
	}
	const Dictionary root = parsed;

	_track.schema_version = int(number_of(root, "schema_version", -1));
	const Dictionary meta = root.get("meta", Dictionary());
	_track.name = std::string(text_of(meta, "name").utf8().get_data());
	_track.length_m = number_of(meta, "length_m", 0.0);
	_track.grade = int(number_of(meta, "grade", 1));
	_track.net_turn_deg = number_of(meta, "net_turn_deg", 0.0);

	const Array spline = root.get("spline", Array());
	for (int index = 0; index < spline.size(); ++index) {
		const Dictionary entry = spline[index];
		ct::ControlPoint point;
		point.distance_m = number_of(entry, "distance_m", 0.0);
		const Array position = entry.get("position", Array());
		if (position.size() == 2) {
			point.x = double(position[0]);
			point.z = double(position[1]);
		}
		point.heading_rad = number_of(entry, "heading_deg", 0.0) * kart::core::PI / 180.0;
		point.curvature = number_of(entry, "curvature_1pm", 0.0);
		point.width_m = number_of(entry, "width_m", 8.0);
		point.crown_pct = number_of(entry, "crown_pct", 0.0);
		point.bank_pct = number_of(entry, "bank_pct", 0.0);
		point.elevation_m = number_of(entry, "elevation_m", 0.0);
		// The file carries percent because a human authors it; everything past this
		// line is a fraction, so that no trigonometric identity in `core/track.h`
		// has to remember which.
		point.grade = number_of(entry, "grade_pct", 0.0) / 100.0;
		point.segment = int(number_of(entry, "segment", 0));
		_track.points.push_back(point);
	}

	const Array corners = root.get("corners", Array());
	for (int index = 0; index < corners.size(); ++index) {
		const Dictionary entry = corners[index];
		ct::Corner corner;
		corner.name = std::string(text_of(entry, "name").utf8().get_data());
		corner.hand = std::string(text_of(entry, "hand").utf8().get_data());
		corner.from_m = number_of(entry, "from_m", 0.0);
		corner.to_m = number_of(entry, "to_m", 0.0);
		corner.apex_from_m = number_of(entry, "apex_arc_from_m", corner.from_m);
		corner.apex_to_m = number_of(entry, "apex_arc_to_m", corner.to_m);
		corner.direction_change_deg = number_of(entry, "direction_change_deg", 0.0);
		corner.min_radius_m = number_of(entry, "min_radius_m", 0.0);
		corner.width_m = number_of(entry, "width_m", 0.0);
		corner.line_radius_m = number_of(entry, "line_radius_m", 0.0);
		corner.defender_line_radius_m = number_of(entry, "defender_line_radius_m", 0.0);
		corner.grip_ceiling_kmh = number_of(entry, "grip_ceiling_kmh", 0.0);
		corner.lock_ceiling_kmh = number_of(entry, "lock_ceiling_kmh", 0.0);
		corner.apex_kmh = number_of(entry, "apex_kmh", 0.0);
		corner.reverse_apex_kmh = number_of(entry, "reverse_apex_kmh", 0.0);
		if (entry.has("runoff")) {
			const Dictionary runoff = entry["runoff"];
			corner.has_runoff = true;
			corner.runoff.side = side_from(text_of(runoff, "side"));
			corner.runoff.apron_m = number_of(runoff, "apron_m", 0.0);
			corner.runoff.outfield_m = number_of(runoff, "outfield_m", 0.0);
			corner.runoff.approach_kmh = number_of(runoff, "approach_kmh", 0.0);
			corner.runoff.outfield = std::string(text_of(runoff, "outfield").utf8().get_data());
			corner.runoff.barrier = std::string(text_of(runoff, "barrier").utf8().get_data());
			corner.runoff.sized_for = std::string(text_of(runoff, "sized_for").utf8().get_data());
		}
		_track.corners.push_back(corner);
	}

	const Dictionary elevation = root.get("elevation", Dictionary());
	const Array curves = elevation.get("vertical_curves", Array());
	for (int index = 0; index < curves.size(); ++index) {
		const Dictionary entry = curves[index];
		ct::VerticalCurve curve;
		curve.at_m = number_of(entry, "at_m", 0.0);
		curve.K = number_of(entry, "K", 0.0);
		curve.radius_m = number_of(entry, "radius_m", 0.0);
		curve.length_m = number_of(entry, "length_m", 0.0);
		curve.grade_in = number_of(entry, "grade_in_pct", 0.0) / 100.0;
		curve.grade_out = number_of(entry, "grade_out_pct", 0.0) / 100.0;
		curve.speed_forward_kmh = number_of(entry, "speed_forward_kmh", 0.0);
		curve.speed_reverse_kmh = number_of(entry, "speed_reverse_kmh", 0.0);
		curve.convex = text_of(entry, "profile") == "convex";
		_track.vertical_curves.push_back(curve);
	}

	const Array surfaces = root.get("surfaces", Array());
	for (int index = 0; index < surfaces.size(); ++index) {
		const Dictionary entry = surfaces[index];
		ct::SurfaceSpan span;
		span.from_m = number_of(entry, "from_m", 0.0);
		span.to_m = number_of(entry, "to_m", 0.0);
		span.side = side_from(text_of(entry, "side"));
		span.surface_type = surface_from(text_of(entry, "type"));
		span.width_m = number_of(entry, "width_m", 0.0);
		span.height_m = number_of(entry, "height_m", 0.0);
		span.profile = profile_from(text_of(entry, "profile"));
		_track.surfaces.push_back(span);
	}

	const Dictionary furniture = root.get("furniture", Dictionary());
	const Dictionary start_line = furniture.get("start_line", Dictionary());
	_track.start_line_m = number_of(start_line, "distance_m", 0.0);

	// The pit lane's parallel run is furniture and not a layout's, because it is one
	// piece of asphalt that both layouts branch onto - see `PitLane` in
	// `core/track.h`. Its `side` is in the **forward** frame, the same convention
	// `surfaces[].side` already uses, so that the file has one meaning of "left".
	if (furniture.has("pit_lane")) {
		const Dictionary lane = furniture["pit_lane"];
		_track.pit_lane.declared = true;
		_track.pit_lane.from_m = number_of(lane, "from_m", -1.0);
		_track.pit_lane.to_m = number_of(lane, "to_m", -1.0);
		_track.pit_lane.width_m = number_of(lane, "width_m", 0.0);
		_track.pit_lane.separation_m = number_of(lane, "separation_m", 0.0);
		_track.pit_lane.hand = side_from(text_of(lane, "side"));
	}

	const Array layouts = root.get("layouts", Array());
	for (int index = 0; index < layouts.size(); ++index) {
		const Dictionary entry = layouts[index];
		ct::Layout layout;
		layout.name = std::string(text_of(entry, "name").utf8().get_data());
		layout.reversed = text_of(entry, "direction") == "reverse";
		const Array marks = entry.get("sector_marks_m", Array());
		for (int mark = 0; mark < marks.size(); ++mark) {
			layout.sector_marks_m.push_back(double(marks[mark]));
		}
		const Array checkpoints = entry.get("checkpoints_m", Array());
		for (int checkpoint = 0; checkpoint < checkpoints.size(); ++checkpoint) {
			layout.checkpoints_m.push_back(double(checkpoints[checkpoint]));
		}
		const Dictionary grid = entry.get("grid", Dictionary());
		const Array slots = grid.get("positions", Array());
		for (int slot = 0; slot < slots.size(); ++slot) {
			const Dictionary source = slots[slot];
			ct::GridSlot placed;
			placed.position = int(number_of(source, "position", slot + 1));
			placed.distance_m = number_of(source, "distance_m", 0.0);
			placed.lateral_m = number_of(source, "lateral_m", 0.0);
			layout.grid.push_back(placed);
		}
		const Array seed = entry.get("racing_line_seed", Array());
		for (int point = 0; point < seed.size(); ++point) {
			const Dictionary source = seed[point];
			ct::SeedPoint placed;
			placed.at_m = number_of(source, "at_m", 0.0);
			placed.lateral_m = number_of(source, "lateral_m", 0.0);
			layout.racing_line_seed.push_back(placed);
		}
		const Array speeds = entry.get("vertical_curve_speeds_kmh", Array());
		for (int speed = 0; speed < speeds.size(); ++speed) {
			layout.vertical_curve_speeds_kmh.push_back(double(speeds[speed]));
		}
		layout.pit_entry_m = number_of(entry, "pit_entry_m", -1.0);
		layout.pit_exit_m = number_of(entry, "pit_exit_m", -1.0);
		if (entry.has("pit")) {
			const Dictionary pit = entry["pit"];
			// `side` here is in the LAYOUT's own frame and the lane's is in the
			// forward frame. That reads like a trap and is the point: Valdirone's
			// forward layout says "left", its reverse layout says "right", and both
			// name the same edge of the same road. A file that used one frame for both
			// could not say that.
			layout.pit_side = side_from(text_of(pit, "side"));
			layout.pit_entry_angle_deg = number_of(pit, "entry_angle_deg", 0.0);
			layout.pit_exit_angle_deg = number_of(pit, "exit_angle_deg", 0.0);
		}
		layout.estimated_lap_time_s = number_of(entry, "estimated_lap_time_s", 0.0);
		_track.layouts.push_back(layout);
	}

	// The vertical-curve check needs each layout's own speed at each break, and the
	// file carries them per layout because a crest is met at two different speeds.
	// K does not swap - `d2z/ds2` is invariant under `s -> L-s` - so only the speed
	// moves, and both are folded into the curve here before validation runs.
	for (const ct::Layout &layout : _track.layouts) {
		for (std::size_t index = 0;
				index < layout.vertical_curve_speeds_kmh.size()
				&& index < _track.vertical_curves.size();
				++index) {
			ct::VerticalCurve &curve = _track.vertical_curves[index];
			if (layout.reversed) {
				curve.speed_reverse_kmh = layout.vertical_curve_speeds_kmh[index];
			} else {
				curve.speed_forward_kmh = layout.vertical_curve_speeds_kmh[index];
			}
		}
	}

	const std::vector<std::string> found = _track.validate(SCHEMA_VERSION);
	for (const std::string &problem : found) {
		_problems.push_back(String::utf8(problem.c_str()));
	}
	if (!found.empty()) {
		// Refuses rather than warns. See the class comment.
		return ERR_INVALID_DATA;
	}
	if (_track.layouts.empty()) {
		_problems.push_back("the track declares no layouts, so there is nothing to drive");
		return ERR_INVALID_DATA;
	}

	_loaded = true;
	_layout_index = 0;
	_layout_name = String::utf8(_track.layouts[0].name.c_str());
	return OK;
}

PackedStringArray KartTrack::problems() const {
	return _problems;
}

String KartTrack::track_name() const {
	return String::utf8(_track.name.c_str());
}

PackedStringArray KartTrack::layout_names() const {
	PackedStringArray names;
	for (const ct::Layout &layout : _track.layouts) {
		names.push_back(String::utf8(layout.name.c_str()));
	}
	return names;
}

bool KartTrack::select_layout(const String &p_name) {
	for (std::size_t index = 0; index < _track.layouts.size(); ++index) {
		if (String::utf8(_track.layouts[index].name.c_str()) == p_name) {
			_layout_index = static_cast<int>(index);
			_layout_name = p_name;
			return true;
		}
	}
	return false;
}

bool KartTrack::is_reversed() const {
	const ct::Layout *layout = _layout();
	return layout != nullptr && layout->reversed;
}

const ct::Layout *KartTrack::_layout() const {
	if (_layout_index < 0 || _layout_index >= static_cast<int>(_track.layouts.size())) {
		return nullptr;
	}
	return &_track.layouts[_layout_index];
}

double KartTrack::to_station(double p_forward_distance) const {
	const ct::Layout *layout = _layout();
	if (layout == nullptr) {
		return _track.wrap(p_forward_distance);
	}
	return _track.to_station(*layout, p_forward_distance);
}

double KartTrack::to_forward(double p_station) const {
	return to_station(p_station);
}

// --- geometry ---------------------------------------------------------------

Dictionary KartTrack::_frame_to_dictionary(const ct::Frame &p_frame) const {
	const ct::Layout *layout = _layout();
	const bool reversed = layout != nullptr && layout->reversed;
	Dictionary out;
	out["distance"] = to_station(p_frame.distance_m);
	out["position"] = Vector3(p_frame.x, p_frame.elevation_m, p_frame.z);
	// Reversed, the same asphalt is driven the other way: the heading turns by
	// half a revolution, the road still falls the way it falls but that is now the
	// *left* of travel, and the grade a driver climbs is the negative of the one
	// the file stores. Curvature flips for the same reason - a left-hander becomes
	// a right-hander - and this is the only place any of it happens.
	out["heading"] = reversed ? p_frame.heading_rad + kart::core::PI : p_frame.heading_rad;
	out["curvature"] = reversed ? -p_frame.curvature : p_frame.curvature;
	out["grade"] = reversed ? -p_frame.grade : p_frame.grade;
	out["bank_pct"] = reversed ? -p_frame.bank_pct : p_frame.bank_pct;
	out["width"] = p_frame.width_m;
	out["crown_pct"] = p_frame.crown_pct;
	out["elevation"] = p_frame.elevation_m;
	return out;
}

Dictionary KartTrack::sample(double p_station) const {
	return _frame_to_dictionary(_track.sample(to_forward(p_station)));
}

Dictionary KartTrack::project(const Vector3 &p_position, double p_hint) const {
	const double hint = p_hint < 0.0 ? -1.0 : to_forward(p_hint);
	const ct::Projection found = _track.project(p_position.x, p_position.z, hint);
	const ct::Layout *layout = _layout();
	const bool reversed = layout != nullptr && layout->reversed;
	Dictionary out;
	out["distance"] = to_station(found.distance_m);
	// Positive to the right of travel. Reversed, the right of travel is the other
	// edge of the same road, so the sign flips with the heading rather than
	// independently of it - a caller that used `lateral` for a track-limits call
	// would otherwise invalidate the wrong side of the lap.
	out["lateral"] = reversed ? -found.lateral_m : found.lateral_m;
	out["gap"] = found.gap_m;
	out["heading"] = reversed ? found.heading_rad + kart::core::PI : found.heading_rad;
	return out;
}

PackedVector3Array KartTrack::centerline(double p_sagitta, double p_max_spacing) const {
	PackedVector3Array out;
	const std::vector<ct::Frame> line = _track.polyline(p_sagitta, p_max_spacing);
	for (const ct::Frame &frame : line) {
		out.push_back(Vector3(frame.x, frame.elevation_m, frame.z));
	}
	return out;
}

Vector3 KartTrack::_surface_point(const ct::Frame &p_frame, double p_lateral, double p_lift) const {
	double rx = 0.0;
	double rz = 0.0;
	ct::Track::right_of(p_frame.heading_rad, rx, rz);
	// `docs/TRACK_SCHEMA.md`'s one cross-section formula, implemented here and in
	// `tools/blender/tracklib/section.py` and nowhere else:
	//
	//     y(u) = elevation - (crown/100)|u| - (bank/100)u
	//
	// piecewise linear in u, which is why three columns of vertices reproduce it
	// exactly rather than approximately.
	const double y = p_frame.elevation_m
			- p_frame.crown_pct * 0.01 * std::fabs(p_lateral)
			- p_frame.bank_pct * 0.01 * p_lateral;
	return Vector3(p_frame.x + rx * p_lateral, y + p_lift, p_frame.z + rz * p_lateral);
}

// --- furniture ---------------------------------------------------------------

PackedFloat64Array KartTrack::checkpoints() const {
	PackedFloat64Array out;
	const ct::Layout *layout = _layout();
	if (layout == nullptr) {
		return out;
	}
	for (const double station : layout->checkpoints_m) {
		out.push_back(station);
	}
	return out;
}

PackedFloat64Array KartTrack::sector_marks() const {
	PackedFloat64Array out;
	const ct::Layout *layout = _layout();
	if (layout == nullptr) {
		return out;
	}
	for (const double station : layout->sector_marks_m) {
		out.push_back(station);
	}
	return out;
}

int KartTrack::grid_count() const {
	const ct::Layout *layout = _layout();
	return layout == nullptr ? 0 : static_cast<int>(layout->grid.size());
}

Transform3D KartTrack::grid_transform(int p_index, double p_lift) const {
	const ct::Layout *layout = _layout();
	if (layout == nullptr || p_index < 0 || p_index >= static_cast<int>(layout->grid.size())) {
		return Transform3D();
	}
	const ct::GridSlot &slot = layout->grid[p_index];
	const ct::Frame frame = _track.sample(to_forward(slot.distance_m));
	// The slot's lateral offset is in the *layout's* frame, so it flips with the
	// direction: pole is the inside of the first corner, and reversed the first
	// corner is a different corner with a different hand.
	const double lateral = layout->reversed ? -slot.lateral_m : slot.lateral_m;
	const Vector3 origin = _surface_point(frame, lateral, p_lift);
	const double heading = layout->reversed ? frame.heading_rad + kart::core::PI : frame.heading_rad;

	// Basis from the road, not from the world: a kart on 5% banking that spawns
	// level drops one wheel onto the road on the first tick.
	double fx = 0.0;
	double fz = 0.0;
	ct::Track::forward_of(heading, fx, fz);
	double rx = 0.0;
	double rz = 0.0;
	ct::Track::right_of(heading, rx, rz);
	const double crossfall = -frame.crown_pct * 0.01 * (lateral < 0.0 ? -1.0 : 1.0)
			- frame.bank_pct * 0.01 * (layout->reversed ? -1.0 : 1.0);
	const double grade = layout->reversed ? -frame.grade : frame.grade;
	Vector3 forward(fx, grade, fz);
	Vector3 right(rx, crossfall, rz);
	forward.normalize();
	right = (right - forward * right.dot(forward)).normalized();
	const Vector3 up = right.cross(forward).normalized();
	// Godot's -Z is forward for a Node3D, so the basis' third column is -forward.
	Transform3D placed;
	placed.basis = Basis(right, up, -forward);
	placed.origin = origin;
	return placed;
}

int KartTrack::corner_count() const {
	return static_cast<int>(_track.corners.size());
}

Dictionary KartTrack::corner(int p_index) const {
	Dictionary out;
	if (p_index < 0 || p_index >= static_cast<int>(_track.corners.size())) {
		return out;
	}
	const ct::Corner &corner = _track.corners[p_index];
	const ct::Layout *layout = _layout();
	const bool reversed = layout != nullptr && layout->reversed;
	out["name"] = String::utf8(corner.name.c_str());
	// Driven the other way, the entry is the exit and the hand is the other hand.
	out["from"] = to_station(reversed ? corner.to_m : corner.from_m);
	out["to"] = to_station(reversed ? corner.from_m : corner.to_m);
	out["hand"] = reversed ? (corner.hand == "left" ? "right" : "left")
						   : String::utf8(corner.hand.c_str());
	out["direction_change_deg"] = corner.direction_change_deg;
	out["min_radius"] = corner.min_radius_m;
	out["line_radius"] = corner.line_radius_m;
	out["defender_line_radius"] = corner.defender_line_radius_m;
	out["grip_ceiling_kmh"] = corner.grip_ceiling_kmh;
	out["lock_ceiling_kmh"] = corner.lock_ceiling_kmh;
	out["apex_kmh"] = reversed ? corner.reverse_apex_kmh : corner.apex_kmh;
	out["width"] = corner.width_m;
	out["has_runoff"] = corner.has_runoff;
	if (corner.has_runoff) {
		out["runoff_apron_m"] = corner.runoff.apron_m;
		out["runoff_outfield_m"] = corner.runoff.outfield_m;
		out["runoff_sized_for"] = String::utf8(corner.runoff.sized_for.c_str());
		out["runoff_approach_kmh"] = corner.runoff.approach_kmh;
	}
	return out;
}

Dictionary KartTrack::pit_lane() const {
	Dictionary out;
	out["declared"] = _track.pit_lane.declared;
	out["from"] = _track.pit_lane.from_m;
	out["to"] = _track.pit_lane.to_m;
	out["width"] = _track.pit_lane.width_m;
	out["separation"] = _track.pit_lane.separation_m;
	out["side"] = _track.pit_lane.hand == ct::SIDE_LEFT ? "left" : "right";
	// The length of the parallel run, which is the number a pit-limiter zone would
	// be timed against and the one figure here that is not in the file.
	out["run"] = _track.pit_lane.declared
			? _track.wrap(_track.pit_lane.to_m - _track.pit_lane.from_m)
			: 0.0;
	return out;
}

TypedArray<Dictionary> KartTrack::pit_stubs() const {
	TypedArray<Dictionary> out;
	for (const ct::PitStub &stub : _track.pit_stubs()) {
		Dictionary entry;
		entry["layout"] = String::utf8(stub.layout.c_str());
		entry["is_entry"] = stub.is_entry;
		entry["junction"] = stub.junction_m;
		entry["outboard"] = stub.outboard_m;
		entry["angle_deg"] = stub.angle_deg;
		entry["separation"] = stub.separation_m;
		entry["side"] = stub.hand == ct::SIDE_LEFT ? "left" : "right";
		// Signed, so a consumer can see which way along the lap the gore opens
		// without re-deriving the layout's direction. Negative means the gore runs
		// back down the lap, which every reverse-layout entry does.
		entry["reach"] = _track.signed_gap(stub.junction_m, stub.outboard_m);
		out.push_back(entry);
	}
	return out;
}

Dictionary KartTrack::measurements() const {
	Dictionary out;
	double straight_at = 0.0;
	const ct::Track::Separation gap = _track.separation(1.0);
	out["length"] = _track.length_m;
	out["longest_straight"] = _track.longest_straight(&straight_at);
	out["longest_straight_at"] = straight_at;
	out["start_to_first_corner"] = _track.start_to_first_corner();
	out["last_corner_to_start"] = _track.last_corner_to_start();
	out["starting_straight"] = _track.start_to_first_corner() + _track.last_corner_to_start();
	out["min_clear_ground"] = gap.clear_ground_m;
	out["min_clear_ground_required"] = gap.required_m;
	out["min_clear_ground_at"] = Vector2(gap.at_a_m, gap.at_b_m);
	out["worst_ground_slope_pct"] = gap.worst_slope_pct;
	out["worst_ground_slope_at"] = Vector2(gap.slope_at_a_m, gap.slope_at_b_m);

	double low = 1e30;
	double high = -1e30;
	double low_at = 0.0;
	double high_at = 0.0;
	double min_width = 1e30;
	double max_width = 0.0;
	const std::vector<ct::Frame> line = _track.polyline(0.05, 1.0);
	for (const ct::Frame &frame : line) {
		if (frame.elevation_m < low) {
			low = frame.elevation_m;
			low_at = frame.distance_m;
		}
		if (frame.elevation_m > high) {
			high = frame.elevation_m;
			high_at = frame.distance_m;
		}
		min_width = std::fmin(min_width, frame.width_m);
		max_width = std::fmax(max_width, frame.width_m);
	}
	out["elevation_range"] = high - low;
	out["elevation_low"] = low;
	out["elevation_low_at"] = low_at;
	out["elevation_high"] = high;
	out["elevation_high_at"] = high_at;
	out["min_width"] = min_width;
	out["max_width"] = max_width;
	out["corners"] = corner_count();
	// Appendix 13's capacity: L/28, capped at 36 for a race.
	out["capacity_karts"] = _track.length_m / kart::core::circuit::METERS_OF_TRACK_PER_KART;
	return out;
}

// --- collision ---------------------------------------------------------------

TypedArray<Dictionary> KartTrack::surface_meshes(double p_sagitta, double p_max_spacing) const {
	TypedArray<Dictionary> out;
	if (!_loaded) {
		return out;
	}
	const std::vector<ct::Frame> line = _track.polyline(p_sagitta, p_max_spacing);
	if (line.size() < 2) {
		return out;
	}

	PackedVector3Array asphalt;
	PackedVector3Array kerb;
	PackedVector3Array grass;
	PackedVector3Array gravel;
	PackedVector3Array barrier;
	PackedVector3Array pit;

	// The road. Three columns and not two, because a crown is a roof and a two-
	// column quad would draw its two edges joined by a flat plane through the
	// middle - the one shape a drainage camber is not.
	for (std::size_t index = 0; index + 1 < line.size(); ++index) {
		const ct::Frame &near = line[index];
		const ct::Frame &far = line[index + 1];
		const double near_half = near.width_m * 0.5;
		const double far_half = far.width_m * 0.5;
		const double columns[3] = { -1.0, 0.0, 1.0 };
		for (int column = 0; column < 2; ++column) {
			add_quad(asphalt,
					_surface_point(near, columns[column] * near_half, 0.0),
					_surface_point(near, columns[column + 1] * near_half, 0.0),
					_surface_point(far, columns[column + 1] * far_half, 0.0),
					_surface_point(far, columns[column] * far_half, 0.0),
					Vector3(0.0, 1.0, 0.0));
		}
	}

	// The verge: 1.80 m of it, both sides, the whole way round. Part I §7.5 is a
	// per-segment requirement and not corner prose, which is why it comes off the
	// polyline rather than off the corner list, and it *continues the track's
	// transversal profile with no negative slope* - so it extends the road's own
	// cross-section outward rather than sitting flat at centerline height, which
	// would put a lip at the white line exactly where the rule forbids one.
	//
	// It is a collider and not just a mesh, and that is the fix for a real defect
	// rather than a completeness pass: the run-off apron used to start at the road
	// edge, so it and the verge occupied the same 1.80 m band. The verge vanished
	// under the apron in every render, and the collider had two coplanar faces
	// there - the exact condition that makes a suspension raycast's answer
	// arbitrary along a whole boundary, which is what `ROAD_LIP` exists to prevent
	// on the other side of the same edge.
	for (std::size_t index = 0; index + 1 < line.size(); ++index) {
		const ct::Frame &near = line[index];
		const ct::Frame &far = line[index + 1];
		for (int which = 0; which < 2; ++which) {
			const double hand = which == 0 ? -1.0 : 1.0;
			const double near_edge = hand * near.width_m * 0.5;
			const double far_edge = hand * far.width_m * 0.5;
			add_quad(grass,
					_surface_point(near, near_edge, -0.002),
					_surface_point(near, near_edge + hand * VERGE_WIDTH_M, -0.002),
					_surface_point(far, far_edge + hand * VERGE_WIDTH_M, -0.002),
					_surface_point(far, far_edge, -0.002),
					Vector3(0.0, 1.0, 0.0));
		}
	}

	// Kerbs and any other declared surface span, outboard of the road edge.
	for (const ct::SurfaceSpan &span : _track.surfaces) {
		PackedVector3Array *target = &asphalt;
		if (span.surface_type == kart::core::SURFACE_CURB) {
			target = &kerb;
		} else if (span.surface_type == kart::core::SURFACE_DIRT) {
			target = &gravel;
		}
		const int hands[2] = { -1, 1 };
		for (int which = 0; which < 2; ++which) {
			if (span.side != ct::SIDE_BOTH && span.side != hands[which]) {
				continue;
			}
			const double hand = static_cast<double>(hands[which]);
			for (std::size_t index = 0; index + 1 < line.size(); ++index) {
				const double from = line[index].distance_m;
				const double to = line[index + 1].distance_m;
				// A span that wraps past the start line is two spans on the lap; both
				// halves are tested rather than one comparison that silently drops the
				// piece before zero.
				const bool inside = span.from_m <= span.to_m
						? (from >= span.from_m - 1e-6 && to <= span.to_m + 1e-6)
						: (from >= span.from_m - 1e-6 || to <= span.to_m + 1e-6);
				if (!inside) {
					continue;
				}
				const ct::Frame &near = line[index];
				const ct::Frame &far = line[index + 1];
				// Ramped in at both ends along the direction of travel, but never
				// laterally: the lateral face stays vertical at full height for the
				// whole kerb, because that face is the one issue #139 wants driven at.
				const double ramp_near = _ramp(span, near.distance_m);
				const double ramp_far = _ramp(span, far.distance_m);
				const double near_edge = hand * near.width_m * 0.5;
				const double far_edge = hand * far.width_m * 0.5;
				const double near_out = near_edge + hand * span.width_m;
				const double far_out = far_edge + hand * span.width_m;
				const Vector3 inner_near = _surface_point(near, near_edge, span.height_m * ramp_near);
				const Vector3 inner_far = _surface_point(far, far_edge, span.height_m * ramp_far);
				const Vector3 outer_near = _surface_point(near, near_out, span.height_m * ramp_near);
				const Vector3 outer_far = _surface_point(far, far_out, span.height_m * ramp_far);
				add_quad(*target, inner_near, outer_near, outer_far, inner_far,
						Vector3(0.0, 1.0, 0.0));
				// The face a wheel climbs, and the whole reason this is a kerb rather
				// than a painted strip.
				double rx = 0.0;
				double rz = 0.0;
				ct::Track::right_of(near.heading_rad, rx, rz);
				const Vector3 facing(-hand * rx, 0.0, -hand * rz);
				const Vector3 base_near = _surface_point(near, near_edge, -0.15);
				const Vector3 base_far = _surface_point(far, far_edge, -0.15);
				add_quad(*target, base_near, inner_near, inner_far, base_far, facing);
			}
		}
	}

	// Run-off: an apron of asphalt, then a gravel bed where one is declared, then
	// the barrier the design hands the energy to. Only where a corner declares it -
	// this is not a barrier ring round the whole circuit, and the scene's own
	// ground slab is what catches everything else.
	for (const ct::Corner &corner : _track.corners) {
		if (!corner.has_runoff || corner.runoff.apron_m <= 0.0) {
			continue;
		}
		const bool gravel_bed = corner.runoff.outfield == "gravel";
		const int hands[2] = { -1, 1 };
		for (int which = 0; which < 2; ++which) {
			if (corner.runoff.side != ct::SIDE_BOTH && corner.runoff.side != hands[which]) {
				continue;
			}
			const double hand = static_cast<double>(hands[which]);
			for (std::size_t index = 0; index + 1 < line.size(); ++index) {
				const double from = line[index].distance_m;
				const double to = line[index + 1].distance_m;
				// The run-off covers the corner plus a lead-in and a lead-out: a kart
				// leaves the road under braking, before the turn-in, more often than
				// it leaves it at the apex.
				const double start = corner.from_m - 30.0;
				const double end = corner.to_m + 30.0;
				if (to < start || from > end) {
					continue;
				}
				const ct::Frame &near = line[index];
				const ct::Frame &far = line[index + 1];
				// Outboard of the verge, not of the white line: Part I §7.5 orders
				// them track, verge, run-off, and the run-off "must grade to the
				// verge without a negative slope". See the verge loop above for what
				// building it from the road edge cost.
				const double near_edge = hand * (near.width_m * 0.5 + VERGE_WIDTH_M);
				const double far_edge = hand * (far.width_m * 0.5 + VERGE_WIDTH_M);
				const double apron = corner.runoff.apron_m;
				add_quad(asphalt,
						_surface_point(near, near_edge, 0.0),
						_surface_point(near, near_edge + hand * apron, 0.0),
						_surface_point(far, far_edge + hand * apron, 0.0),
						_surface_point(far, far_edge, 0.0),
						Vector3(0.0, 1.0, 0.0));
				if (gravel_bed && corner.runoff.outfield_m > 0.0) {
					const double outer = apron + corner.runoff.outfield_m;
					add_quad(gravel,
							_surface_point(near, near_edge + hand * apron, 0.0),
							_surface_point(near, near_edge + hand * outer, 0.0),
							_surface_point(far, far_edge + hand * outer, 0.0),
							_surface_point(far, far_edge + hand * apron, 0.0),
							Vector3(0.0, 1.0, 0.0));
				}
				const double limit = apron + corner.runoff.outfield_m;
				const Vector3 foot_near = _surface_point(near, near_edge + hand * limit, 0.0);
				const Vector3 foot_far = _surface_point(far, far_edge + hand * limit, 0.0);
				const Vector3 head_near = foot_near + Vector3(0.0, 1.0, 0.0);
				const Vector3 head_far = foot_far + Vector3(0.0, 1.0, 0.0);
				double rx = 0.0;
				double rz = 0.0;
				ct::Track::right_of(near.heading_rad, rx, rz);
				add_quad(barrier, foot_near, head_near, head_far, foot_far,
						Vector3(-hand * rx, 0.0, -hand * rz));
			}
		}
	}

	// The pit lane: one parallel band shared by both layouts, plus one gore per
	// junction. `docs/TRACK_SCHEMA.md`'s "Pit geometry, in arithmetic" is the
	// specification and `tools/blender/tracklib/surfaces.py` is the other
	// implementation of it.
	//
	// Sampled on its own stations rather than off the shared polyline, and that is
	// not an optimization: the polyline is subdivided to a chord tolerance, so its
	// samples land wherever they land, and a gore whose tip is 0.6 m past its own
	// junction is a wedge of asphalt starting in the middle of the verge. Stepping
	// from the junction to the outboard station puts both ends exactly where the
	// schema says, in both consumers, which is what makes `--case=agree` a
	// measurement rather than a coincidence.
	if (_track.pit_lane.declared) {
		const double hand = static_cast<double>(_track.pit_lane.hand);
		const double separation = _track.pit_lane.separation_m;
		const double lane_width = _track.pit_lane.width_m;

		const double run = _track.wrap(_track.pit_lane.to_m - _track.pit_lane.from_m);
		int steps = static_cast<int>(std::ceil(run / p_max_spacing));
		if (steps < 1) {
			steps = 1;
		}
		for (int step = 0; step < steps; ++step) {
			const ct::Frame near = _track.sample(
					_track.pit_lane.from_m + run * step / steps);
			const ct::Frame far = _track.sample(
					_track.pit_lane.from_m + run * (step + 1) / steps);
			const double near_in = hand * (near.width_m * 0.5 + separation);
			const double far_in = hand * (far.width_m * 0.5 + separation);
			add_quad(pit,
					_surface_point(near, near_in, 0.0),
					_surface_point(near, near_in + hand * lane_width, 0.0),
					_surface_point(far, far_in + hand * lane_width, 0.0),
					_surface_point(far, far_in, 0.0),
					Vector3(0.0, 1.0, 0.0));
		}

		for (const ct::PitStub &stub : _track.pit_stubs()) {
			const double reach = _track.signed_gap(stub.junction_m, stub.outboard_m);
			int gore_steps = static_cast<int>(std::ceil(std::fabs(reach) / p_max_spacing));
			if (gore_steps < 1) {
				gore_steps = 1;
			}
			const double gore_hand = static_cast<double>(stub.hand);
			for (int step = 0; step < gore_steps; ++step) {
				const double near_t = static_cast<double>(step) / gore_steps;
				const double far_t = static_cast<double>(step + 1) / gore_steps;
				const ct::Frame near = _track.sample(stub.junction_m + reach * near_t);
				const ct::Frame far = _track.sample(stub.junction_m + reach * far_t);
				// The gore's inner edge is the white line itself and its outer edge is
				// the opening separation, so at the junction the two coincide and the
				// first cell is a triangle. `add_triangle` drops the zero-area half.
				const double near_edge = gore_hand * near.width_m * 0.5;
				const double far_edge = gore_hand * far.width_m * 0.5;
				add_quad(pit,
						_surface_point(near, near_edge, 0.0),
						_surface_point(near, near_edge + gore_hand * separation * near_t, 0.0),
						_surface_point(far, far_edge + gore_hand * separation * far_t, 0.0),
						_surface_point(far, far_edge, 0.0),
						Vector3(0.0, 1.0, 0.0));
			}
		}
	}

	auto publish = [&](const char *name, int surface_type, const PackedVector3Array &faces) {
		if (faces.is_empty()) {
			return;
		}
		Dictionary entry;
		entry["name"] = String(name);
		entry["surface_type"] = surface_type;
		entry["faces"] = faces;
		out.push_back(entry);
	};
	publish("Asphalt", kart::core::SURFACE_ASPHALT, asphalt);
	publish("Kerbs", kart::core::SURFACE_CURB, kerb);
	publish("Verge", kart::core::SURFACE_GRASS, grass);
	publish("Gravel", kart::core::SURFACE_DIRT, gravel);
	publish("Barriers", kart::core::SURFACE_ASPHALT, barrier);
	// Asphalt by surface type and a body of its own by name: a pit lane is the
	// same stuff the road is made of, and a caller that wants to know whether a
	// kart is in the pits asks the *station*, not the surface. Named separately
	// so `--mesh=false` can draw it on its own and so `circuit_probe.gd` can
	// measure these triangles rather than the whole circuit's.
	publish("PitLane", kart::core::SURFACE_ASPHALT, pit);
	return out;
}

double KartTrack::_ramp(const ct::SurfaceSpan &p_span, double p_distance) const {
	// Zero at either end of a kerb, one in the middle. 4 m, the same figure
	// `track_ribbon.gd` uses and for the same reason: a driver who meets the end of
	// a kerb head-on at 140 km/h is meeting a 30 mm step at zero degrees of
	// incidence, which is a modeling artifact rather than a test.
	const double from_start = p_distance - p_span.from_m;
	const double from_end = p_span.to_m - p_distance;
	const double nearest = std::fmin(from_start, from_end);
	return std::fmax(0.0, std::fmin(nearest / 4.0, 1.0));
}

// --- bindings ---------------------------------------------------------------

void KartTrack::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load", "path"), &KartTrack::load);
	ClassDB::bind_method(D_METHOD("is_loaded"), &KartTrack::is_loaded);
	ClassDB::bind_method(D_METHOD("problems"), &KartTrack::problems);
	ClassDB::bind_method(D_METHOD("track_name"), &KartTrack::track_name);
	ClassDB::bind_method(D_METHOD("source_path"), &KartTrack::source_path);
	ClassDB::bind_method(D_METHOD("length"), &KartTrack::length);
	ClassDB::bind_method(D_METHOD("grade"), &KartTrack::grade);
	ClassDB::bind_method(D_METHOD("content_hash"), &KartTrack::content_hash);
	ClassDB::bind_method(D_METHOD("layout_names"), &KartTrack::layout_names);
	ClassDB::bind_method(D_METHOD("select_layout", "name"), &KartTrack::select_layout);
	ClassDB::bind_method(D_METHOD("layout"), &KartTrack::layout);
	ClassDB::bind_method(D_METHOD("is_reversed"), &KartTrack::is_reversed);
	ClassDB::bind_method(D_METHOD("sample", "station"), &KartTrack::sample);
	ClassDB::bind_method(D_METHOD("project", "position", "hint"), &KartTrack::project,
			DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("centerline", "sagitta", "max_spacing"),
			&KartTrack::centerline, DEFVAL(0.02), DEFVAL(2.0));
	ClassDB::bind_method(D_METHOD("to_station", "forward_distance"), &KartTrack::to_station);
	ClassDB::bind_method(D_METHOD("to_forward", "station"), &KartTrack::to_forward);
	ClassDB::bind_method(D_METHOD("checkpoints"), &KartTrack::checkpoints);
	ClassDB::bind_method(D_METHOD("sector_marks"), &KartTrack::sector_marks);
	ClassDB::bind_method(D_METHOD("grid_count"), &KartTrack::grid_count);
	ClassDB::bind_method(D_METHOD("grid_transform", "index", "lift"),
			&KartTrack::grid_transform, DEFVAL(0.12));
	ClassDB::bind_method(D_METHOD("corner_count"), &KartTrack::corner_count);
	ClassDB::bind_method(D_METHOD("corner", "index"), &KartTrack::corner);
	ClassDB::bind_method(D_METHOD("measurements"), &KartTrack::measurements);
	ClassDB::bind_method(D_METHOD("pit_lane"), &KartTrack::pit_lane);
	ClassDB::bind_method(D_METHOD("pit_stubs"), &KartTrack::pit_stubs);
	ClassDB::bind_method(D_METHOD("surface_meshes", "sagitta", "max_spacing"),
			&KartTrack::surface_meshes, DEFVAL(0.02), DEFVAL(2.0));
}

}  // namespace kartgame
