#include "vehicle/ai_driver.h"

#include "core/replay.h"
#include "vehicle/kart_body.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

// Every `StringName` in this file is a function-local static, and it has to be.
// CLAUDE.md records why: a namespace-scope `StringName` in a GDExtension crashes
// Godot in `dyld4::callInitializer`, before any Godot frame exists.

inline double wrap_positive(double value, double period) {
	if (period <= 0.0) {
		return 0.0;
	}
	double wrapped = std::fmod(value, period);
	if (wrapped < 0.0) {
		wrapped += period;
	}
	return wrapped;
}

} // namespace

AiDriver::AiDriver() {
	controller_.set_tune(tune_);
}

void AiDriver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_line", "line"), &AiDriver::set_line);
	ClassDB::bind_method(D_METHOD("get_line"), &AiDriver::get_line);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "line", PROPERTY_HINT_RESOURCE_TYPE,
						 "KartRacingLine"),
			"set_line", "get_line");

	ClassDB::bind_method(D_METHOD("set_course", "course"), &AiDriver::set_course);
	ClassDB::bind_method(D_METHOD("get_course"), &AiDriver::get_course);

	ClassDB::bind_method(D_METHOD("configure_from_line"), &AiDriver::configure_from_line);
	ClassDB::bind_method(D_METHOD("limits"), &AiDriver::limits);
	ClassDB::bind_method(D_METHOD("decision"), &AiDriver::decision);
	ClassDB::bind_method(D_METHOD("reset"), &AiDriver::reset);

	ClassDB::bind_method(D_METHOD("get_max_cross_track_m"), &AiDriver::get_max_cross_track_m);
	ClassDB::bind_method(D_METHOD("get_max_body_slip_rad"), &AiDriver::get_max_body_slip_rad);
	ClassDB::bind_method(D_METHOD("get_cross_track_m"), &AiDriver::get_cross_track_m);
	ClassDB::bind_method(D_METHOD("get_station_m"), &AiDriver::get_station_m);
	ClassDB::bind_method(D_METHOD("get_target_speed_ms"), &AiDriver::get_target_speed_ms);
	ClassDB::bind_method(D_METHOD("get_ticks_driven"), &AiDriver::get_ticks_driven);

	ClassDB::bind_method(D_METHOD("set_lookahead_base", "meters"), &AiDriver::set_lookahead_base);
	ClassDB::bind_method(D_METHOD("get_lookahead_base"), &AiDriver::get_lookahead_base);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lookahead_base"), "set_lookahead_base",
			"get_lookahead_base");
	ClassDB::bind_method(D_METHOD("set_lookahead_gain", "seconds"), &AiDriver::set_lookahead_gain);
	ClassDB::bind_method(D_METHOD("get_lookahead_gain"), &AiDriver::get_lookahead_gain);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lookahead_gain"), "set_lookahead_gain",
			"get_lookahead_gain");
	ClassDB::bind_method(D_METHOD("set_steer_gain", "gain"), &AiDriver::set_steer_gain);
	ClassDB::bind_method(D_METHOD("get_steer_gain"), &AiDriver::get_steer_gain);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steer_gain"), "set_steer_gain", "get_steer_gain");
	ClassDB::bind_method(D_METHOD("set_brake_plan_fraction", "fraction"),
			&AiDriver::set_brake_plan_fraction);
	ClassDB::bind_method(D_METHOD("get_brake_plan_fraction"), &AiDriver::get_brake_plan_fraction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brake_plan_fraction"), "set_brake_plan_fraction",
			"get_brake_plan_fraction");
	ClassDB::bind_method(D_METHOD("set_traction_budget", "enabled"),
			&AiDriver::set_traction_budget);
	ClassDB::bind_method(D_METHOD("is_traction_budget"), &AiDriver::is_traction_budget);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "traction_budget"), "set_traction_budget",
			"is_traction_budget");
	ClassDB::bind_method(D_METHOD("set_preview_m", "meters"), &AiDriver::set_preview_m);
	ClassDB::bind_method(D_METHOD("get_preview_m"), &AiDriver::get_preview_m);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_m"), "set_preview_m", "get_preview_m");
}

void AiDriver::_ready() {
	// The base's, deliberately: it sets `PHYSICS_PRIORITY` and subscribes to the
	// tuning registry, and both are wanted here. It also resolves its own kart
	// pointer, which this class does not use and cannot see.
	PlayerDriver::_ready();
	resolve_body();
	configure_from_line();
}

void AiDriver::resolve_body() {
	body_ = Object::cast_to<KartBody>(get_node_or_null(get_kart_path()));
}

bool AiDriver::configure_from_line() {
	if (line_.is_null() || !line_->is_solved()) {
		return false;
	}
	// **Resolved here as well as in `_ready`, and it has to be.** `KartRig` does
	// `add_child(driver)` and *then* assigns `kart_path`, because the path is
	// relative to the parent and cannot be formed before the node is in the
	// tree. So `_ready` runs with an empty path and finds nothing, and a
	// configure that only read the body there would leave `max_lock_rad` at zero
	// — which is `AiLimits::complete()` false, which is a driver that pushes
	// neutral for the whole session. That is exactly what the first run of this
	// class did: `line ok, course ok, limits MISSING`.
	if (body_ == nullptr) {
		resolve_body();
	}
	// **Every ceiling comes off the line's own model, at this moment.** Issue
	// #137 will move `tire.h`; `SpeedModel` re-solves its fixed points against
	// whatever it says, `KartRacingLine::model()` publishes the answer, and
	// nothing in `src/core/ai_driver.h` or in this file has a figure in g.
	const Dictionary model = line_->model();
	kart::core::ai::AiLimits limits;
	limits.brake_limit_ms2 = double(model["brake_g"]) * kart::core::G;
	// The tighter hand, because the controller has no way to know which way the
	// next corner goes and planning against the looser one would over-drive
	// every left-hander on a kart whose engine hangs off the right.
	const double left = double(model["lateral_left_g"]);
	const double right = double(model["lateral_right_g"]);
	limits.lateral_limit_ms2 = (left < right ? left : right) * kart::core::G;
	limits.wheelbase_m = kz::REAR_AXLE_Z - kz::FRONT_AXLE_Z;
	limits.gear_count = kz::GEAR_COUNT;
	if (body_ != nullptr) {
		limits.max_lock_rad = body_->get_max_lock();
		limits.soft_cut_rpm = body_->get_soft_cut_rpm();
		limits.hard_cut_rpm = body_->get_hard_cut_rpm();
	}
	controller_.configure(limits, tune_);

	// Snapshot the line into plain arrays, once.
	//
	// The alternative is 32 `at_station()` calls a tick and each one builds a
	// `Dictionary`; at 120 Hz that is nearly four thousand allocations a second
	// inside the physics loop, for numbers that do not change after `solve()`.
	// `driving_hud.gd`'s header makes the same argument about `wheel_report()`:
	// an allocation is fine for a panel that samples while it is open and wrong
	// for something that runs every tick.
	const int count = line_->station_count();
	spacing_ = line_->spacing();
	length_m_ = spacing_ * double(count);
	point_x_.resize(count);
	point_z_.resize(count);
	speed_.resize(count);
	curvature_.resize(count);
	gear_.resize(count);
	double fastest = 0.0;
	for (int index = 0; index < count; ++index) {
		const Dictionary station = line_->station(index);
		const Vector3 position = station["position"];
		point_x_[index] = position.x;
		point_z_[index] = position.z;
		speed_[index] = double(station["speed"]);
		curvature_[index] = double(station["curvature"]);
		gear_[index] = int(station["gear"]);
		if (speed_[index] > fastest) {
			fastest = speed_[index];
		}
	}
	build_gear_table(fastest);
	return controller_.configured();
}

// The gear the line would be in at a **road speed**, rather than at a station.
//
// **This is not a refinement, it is the difference between driving and not.**
// The line's `gear` array answers "what gear is the line in here", and the line
// is doing 145 km/h on the start straight. A kart sitting on the grid at that
// station is doing nothing, and handing it the line's answer put it in sixth on
// the green light: measured, 15 shifts in the first five seconds and a kart that
// never got out of the pits.
//
// `racing_line.h::best_gear` is monotone in speed and says so, which is what
// makes a speed-indexed table well defined: the same speed always wants the same
// gear, wherever on the circuit it happens. So the table is built by scanning
// the solved stations, and the empty low buckets — the line's slowest corner is
// 77 km/h, so everything below that is empty — are filled downward with the
// lowest gear anybody used. That is first, and first is what a standing start
// wants.
void AiDriver::build_gear_table(double p_fastest_ms) {
	gear_by_speed_.resize(GEAR_TABLE_BUCKETS);
	gear_speed_step_ = (p_fastest_ms > 1.0 ? p_fastest_ms : 1.0) * 1.05 /
			double(GEAR_TABLE_BUCKETS - 1);
	for (int bucket = 0; bucket < GEAR_TABLE_BUCKETS; ++bucket) {
		gear_by_speed_[bucket] = 0;
	}
	const int count = speed_.size();
	for (int index = 0; index < count; ++index) {
		int bucket = int(speed_[index] / gear_speed_step_);
		if (bucket < 0) {
			bucket = 0;
		}
		if (bucket >= GEAR_TABLE_BUCKETS) {
			bucket = GEAR_TABLE_BUCKETS - 1;
		}
		// The highest gear seen in the bucket. Two stations at the same speed
		// can disagree only across a `min_gear_run_m` hold, and the taller of
		// the two is the one that is not about to be shifted out of.
		if (gear_[index] > gear_by_speed_[bucket]) {
			gear_by_speed_[bucket] = gear_[index];
		}
	}
	// Fill upward first, so a gap between two used speeds inherits the lower
	// gear rather than the next one up.
	int carried = 0;
	for (int bucket = 0; bucket < GEAR_TABLE_BUCKETS; ++bucket) {
		if (gear_by_speed_[bucket] > 0) {
			carried = gear_by_speed_[bucket];
		} else if (carried > 0) {
			gear_by_speed_[bucket] = carried;
		}
	}
	// Then downward — and **not by carrying the lowest gear the line used**,
	// which was the first cut and was wrong in a way that reads as correct.
	//
	// The line never goes below 77 km/h, so the slowest gear it has any opinion
	// about is **third**. Carrying that down put the AI in third on the grid,
	// wide open, at 0.26 km/h: measured, and it is worse than the sixth the
	// station-indexed version gave it because it looks reasonable.
	//
	// So the buckets under the line's own slowest corner are filled from the
	// gearbox itself — the same `Gearbox` the line was solved with, sprockets
	// and all, taken off `KartRacingLine::model()` so there is one source. The
	// rule is the **lowest** gear that still has the engine under its shift
	// point at that speed, floored at first. Lowest, not highest: the lowest
	// usable gear is the one pulling hardest, which is `best_gear`'s own answer
	// without the torque curve. Written the other way round it picks sixth at
	// walking pace, because sixth is under the limiter at every speed below it —
	// measured, gear 6 at 0.25 km/h, and it reads as a shift bug rather than as
	// a comparison facing the wrong way.
	kart::core::Gearbox gearbox;
	const Dictionary model = line_->model();
	gearbox.engine_sprocket_teeth = int(model["engine_sprocket_teeth"]);
	gearbox.axle_sprocket_teeth = int(model["axle_sprocket_teeth"]);
	const double radius = double(model["rolling_radius"]);
	// Shift up before the limiter rather than at it, so the gear chosen at a
	// speed still has road left in it when the kart accelerates through the
	// bucket. `hard_cut` comes off the running `KartBody`.
	const double shift_up_rpm = controller_.limits().hard_cut_rpm > 0.0
			? 0.92 * controller_.limits().hard_cut_rpm
			: 0.0;
	for (int bucket = 0; bucket < GEAR_TABLE_BUCKETS; ++bucket) {
		if (gear_by_speed_[bucket] > 0) {
			break;
		}
		const double speed = (double(bucket) + 0.5) * gear_speed_step_;
		int chosen = 1;
		if (radius > 0.0 && shift_up_rpm > 0.0) {
			for (int gear = 1; gear <= kz::GEAR_COUNT; ++gear) {
				if (gearbox.engine_rpm_at(speed, gear, radius) <= shift_up_rpm) {
					chosen = gear;
					break;
				}
			}
		}
		gear_by_speed_[bucket] = chosen;
	}
}

int AiDriver::gear_for_speed(double p_speed_ms) const {
	if (gear_by_speed_.size() <= 0 || gear_speed_step_ <= 0.0) {
		return 0;
	}
	int bucket = int(p_speed_ms / gear_speed_step_);
	if (bucket < 0) {
		bucket = 0;
	}
	if (bucket >= gear_by_speed_.size()) {
		bucket = gear_by_speed_.size() - 1;
	}
	return gear_by_speed_[bucket];
}

void AiDriver::_physics_process(double p_delta) {
	// Unused for the same reason `PlayerDriver` ignores it: the solver runs on a
	// constant step and this node produces one struct per tick, not per second.
	(void)p_delta;

	if (body_ == nullptr) {
		resolve_body();
		if (body_ == nullptr) {
			return;
		}
		// The body arrived after `_ready`, so the ceilings that come off it were
		// never read. Read them now rather than driving with an incomplete set.
		configure_from_line();
	}

	DriverInput input;
	const bool ready = is_enabled() && line_.is_valid() && line_->is_solved() &&
			course_ != nullptr && controller_.configured() && speed_.size() > 0;
	if (!ready) {
		if (!warned_ && is_enabled()) {
			warned_ = true;
			// Once, and it names which of the three is missing. Silence here is
			// a kart that coasts through a whole session with no reason on the
			// console, which is the failure `player_driver.cpp` refuses for the
			// human path and it is worse for a driver nobody is watching.
			ERR_PRINT(vformat("AiDriver '%s': not driving - line %s, course %s, limits %s",
					get_name(), line_.is_valid() && line_->is_solved() ? "ok" : "MISSING",
					course_ != nullptr ? "ok" : "MISSING",
					controller_.configured() ? "ok" : "MISSING"));
		}
		// Neutral, and still pushed. Going silent would leave `KartBody`'s
		// freshness check failing every tick.
		body_->set_input(replay_snap(input), godot::Engine::get_singleton()->get_physics_frames());
		return;
	}

	static const StringName METHOD_PROJECT("project");

	const Transform3D pose = body_->get_global_transform();
	const Vector3 origin = pose.origin;
	const Vector3 forward = -pose.basis.get_column(2);

	// `track.h`'s heading out of Godot's basis: forward is `(sin h, -cos h)`, so
	// `h = atan2(f.x, -f.z)`. The check to keep in your head is h = 0, where
	// forward is -Z.
	const double heading = std::atan2(forward.x, -forward.z);

	const Dictionary placed = course_->call(METHOD_PROJECT, origin, hint_);
	// **Indexed, not `get(key, default)`.** These are a contract, and a renamed
	// key read with a default draws a zero forever rather than failing.
	const double station = double(placed["distance"]);
	const double track_heading = double(placed["heading"]);
	hint_ = station;
	station_m_ = station;

	kart::core::ai::AiObservation observation;
	observation.speed_ms = body_->get_speed_ms();
	observation.x = origin.x;
	observation.z = origin.z;
	observation.heading_rad = heading;
	observation.station_m = station;
	observation.gear = body_->get_gear();
	observation.rpm = body_->get_engine_rpm();
	observation.wheels_on_ground = body_->get_wheels_on_ground();

	// Body slip, the same expression `drive_probe.gd` measures a departure with:
	// the angle between where the kart is pointing and where it is going.
	const Vector3 velocity = body_->get_linear_velocity();
	if (velocity.length_squared() > 0.25) {
		body_slip_rad_ = std::atan2(velocity.dot(pose.basis.get_column(0)),
				-velocity.dot(pose.basis.get_column(2)));
	} else {
		body_slip_rad_ = 0.0;
	}
	observation.body_slip_rad = body_slip_rad_;
	const double slip_magnitude = body_slip_rad_ < 0.0 ? -body_slip_rad_ : body_slip_rad_;
	if (slip_magnitude > max_body_slip_rad_) {
		max_body_slip_rad_ = slip_magnitude;
	}

	// The line here, and the line at the lookahead.
	double line_x = 0.0;
	double line_z = 0.0;
	sample_point(station, line_x, line_z);
	observation.line_curvature = sample_curvature(station);
	// **Indexed by the kart's own speed, not by its station.** See
	// `build_gear_table` for the five seconds and fifteen shifts that bought
	// this sentence.
	observation.line_gear = gear_for_speed(observation.speed_ms);
	target_speed_ms_ = sample_speed(station);

	// Signed cross-track error: how far right of the line the kart is, measured
	// on the *track's* normal at this station rather than as a distance to a
	// polyline. A distance cannot say which side, and which side is the half of
	// it a driver can act on.
	//
	// **Computed before the lookahead, because the lookahead depends on it.**
	// See `AiTune::lookahead_error_gain`: an aim point closer than the error is
	// a lunge across the road.
	const double sin_t = std::sin(track_heading);
	const double cos_t = std::cos(track_heading);
	cross_track_m_ = (origin.x - line_x) * cos_t + (origin.z - line_z) * sin_t;
	observation.cross_track_m = cross_track_m_;

	const double lookahead =
			controller_.lookahead_for(observation.speed_ms, cross_track_m_);
	sample_point(station + lookahead, observation.aim_x, observation.aim_z);
	const double cross_magnitude = cross_track_m_ < 0.0 ? -cross_track_m_ : cross_track_m_;
	if (cross_magnitude > max_cross_track_m_) {
		max_cross_track_m_ = cross_magnitude;
	}

	aim_x_ = observation.aim_x;
	aim_z_ = observation.aim_z;
	kart_x_ = origin.x;
	kart_z_ = origin.z;
	heading_ = heading;
	line_x_ = line_x;
	line_z_ = line_z;

	fill_preview(station, observation);

	input = controller_.step(observation);

	// Snapped **here**, upstream of the solver, exactly where
	// `player_driver.cpp` snaps the human's. `replay.h` argues it at length: if
	// input were quantized on write, the live run would consume full-precision
	// values and the replay rounded ones, and the two would diverge at the rate
	// any chaotic system does. `replay_encode_input` refuses off-grid input
	// rather than rounding it, so without this an AI lap could not be recorded
	// at all.
	input = replay_snap(input);
	// **What was pushed, not what the controller answered.** `decision()` reports
	// this rather than `controller_.last_input()`, because a gate asking "is
	// every value this producer emits recordable" must be shown the value that
	// crossed the boundary. Reading the raw answer had `ai_probe.gd` reporting
	// 1,180 of 1,201 ticks off grid against a driver that was snapping correctly
	// — a confident red naming the wrong file.
	pushed_ = input;
	body_->set_input(input, godot::Engine::get_singleton()->get_physics_frames());
	++ticks_driven_;
}

void AiDriver::fill_preview(
		double p_station, kart::core::ai::AiObservation &r_observation) const {
	const int samples = kart::core::ai::PREVIEW_SAMPLES;
	const double span = tune_.preview_m;
	r_observation.preview_count = samples;
	for (int index = 0; index < samples; ++index) {
		// The first sample is at the kart, at distance zero, and it has to be:
		// `AiDriver::step`'s minimum is over `sqrt(v^2 + 2 a d)`, so without a
		// zero-distance entry the corner the kart is standing in cannot bind and
		// the controller drives straight through its own apex.
		const double distance = span * double(index) / double(samples - 1);
		r_observation.preview[index].distance_m = distance;
		r_observation.preview[index].speed_ms = sample_speed(p_station + distance);
	}
}

// --- the line snapshot, sampled -----------------------------------------------
//
// Linear between stations, wrapping at the lap. The same interpolation
// `KartRacingLine::at_station` does, on arrays this node owns, so the tick path
// allocates nothing.

void AiDriver::sample_point(double p_station, double &r_x, double &r_z) const {
	int low = 0;
	int high = 0;
	double t = 0.0;
	if (!locate(p_station, low, high, t)) {
		r_x = 0.0;
		r_z = 0.0;
		return;
	}
	r_x = point_x_[low] + (point_x_[high] - point_x_[low]) * t;
	r_z = point_z_[low] + (point_z_[high] - point_z_[low]) * t;
}

double AiDriver::sample_speed(double p_station) const {
	int low = 0;
	int high = 0;
	double t = 0.0;
	if (!locate(p_station, low, high, t)) {
		return 0.0;
	}
	return speed_[low] + (speed_[high] - speed_[low]) * t;
}

double AiDriver::sample_curvature(double p_station) const {
	int low = 0;
	int high = 0;
	double t = 0.0;
	if (!locate(p_station, low, high, t)) {
		return 0.0;
	}
	return curvature_[low] + (curvature_[high] - curvature_[low]) * t;
}

int AiDriver::sample_gear(double p_station) const {
	int low = 0;
	int high = 0;
	double t = 0.0;
	if (!locate(p_station, low, high, t)) {
		return 0;
	}
	(void)high;
	// **Not interpolated**, for `at_station`'s own reason: half a gear is not a
	// gear, and rounding one would put the AI in fifth for one tick on the way
	// from fourth to sixth.
	return gear_[t < 0.5 ? low : high];
}

bool AiDriver::locate(double p_station, int &r_low, int &r_high, double &r_t) const {
	const int count = speed_.size();
	if (count <= 0 || spacing_ <= 0.0) {
		return false;
	}
	const double distance = wrap_positive(p_station, length_m_);
	const double exact = distance / spacing_;
	int low = int(std::floor(exact));
	if (low < 0) {
		low = 0;
	}
	if (low >= count) {
		low = count - 1;
	}
	r_low = low;
	r_high = (low + 1) % count;
	r_t = exact - double(low);
	return true;
}

// --- accessors ----------------------------------------------------------------

void AiDriver::set_line(const Ref<KartRacingLine> &p_line) {
	line_ = p_line;
	warned_ = false;
	configure_from_line();
}

Ref<KartRacingLine> AiDriver::get_line() const {
	return line_;
}

void AiDriver::set_course(Object *p_course) {
	course_ = nullptr;
	warned_ = false;
	if (p_course == nullptr) {
		return;
	}
	// Checked by name and named individually, so the error says which one is
	// missing rather than "a course needs two methods".
	const char *required[2] = { "length", "project" };
	for (int index = 0; index < 2; ++index) {
		if (!p_course->has_method(required[index])) {
			UtilityFunctions::push_error(
					String("AiDriver: the course has no ") + String(required[index]) + String("()"));
			return;
		}
	}
	course_ = p_course;
}

Object *AiDriver::get_course() const {
	return course_;
}

Dictionary AiDriver::limits() const {
	const kart::core::ai::AiLimits &found = controller_.limits();
	Dictionary out;
	out["brake_limit_ms2"] = found.brake_limit_ms2;
	out["brake_limit_g"] = found.brake_limit_ms2 / kart::core::G;
	out["lateral_limit_ms2"] = found.lateral_limit_ms2;
	out["lateral_limit_g"] = found.lateral_limit_ms2 / kart::core::G;
	out["wheelbase_m"] = found.wheelbase_m;
	out["max_lock_rad"] = found.max_lock_rad;
	out["soft_cut_rpm"] = found.soft_cut_rpm;
	out["hard_cut_rpm"] = found.hard_cut_rpm;
	out["complete"] = found.complete();
	// The speed -> gear table, sampled. A gate that could not see this had no way
	// to tell "the AI is in fifth at walking pace" from "the AI cannot shift".
	Array gears;
	for (int kmh = 0; kmh <= 150; kmh += 10) {
		gears.push_back(gear_for_speed(double(kmh) / kart::core::KMH_PER_MS));
	}
	out["gear_by_speed_10kmh"] = gears;
	out["gear_speed_step"] = gear_speed_step_;
	return out;
}

Dictionary AiDriver::decision() const {
	const kart::core::ai::AiDecision &found = controller_.decision();
	Dictionary out;
	out["allowed_speed"] = found.allowed_speed_ms;
	out["allowed_speed_kmh"] = found.allowed_speed_ms * kart::core::KMH_PER_MS;
	out["binding_distance"] = found.binding_distance_m;
	out["speed_error"] = found.speed_error_ms;
	out["lookahead"] = found.lookahead_m;
	out["demanded_curvature"] = found.demanded_curvature;
	out["lateral_demand_ms2"] = found.lateral_demand_ms2;
	out["lateral_demand_g"] = found.lateral_demand_ms2 / kart::core::G;
	out["longitudinal_budget"] = found.longitudinal_budget;
	out["target_gear"] = found.target_gear;
	out["braking"] = found.braking;
	out["launching"] = found.launching;
	out["aborting"] = found.aborting;
	out["cross_track"] = cross_track_m_;
	out["station"] = station_m_;
	out["target_speed"] = target_speed_ms_;
	out["body_slip"] = body_slip_rad_;
	out["aim_x"] = aim_x_;
	out["aim_z"] = aim_z_;
	out["kart_x"] = kart_x_;
	out["kart_z"] = kart_z_;
	out["heading"] = heading_;
	out["line_x"] = line_x_;
	out["line_z"] = line_z_;
	out["throttle"] = pushed_.throttle;
	out["brake"] = pushed_.brake;
	out["steer"] = pushed_.steer;
	out["clutch"] = pushed_.clutch;
	out["shift_up"] = pushed_.shift_up;
	out["shift_down"] = pushed_.shift_down;
	return out;
}

double AiDriver::get_max_cross_track_m() const {
	return max_cross_track_m_;
}

double AiDriver::get_max_body_slip_rad() const {
	return max_body_slip_rad_;
}

double AiDriver::get_cross_track_m() const {
	return cross_track_m_;
}

double AiDriver::get_station_m() const {
	return station_m_;
}

double AiDriver::get_target_speed_ms() const {
	return target_speed_ms_;
}

uint64_t AiDriver::get_ticks_driven() const {
	return ticks_driven_;
}

void AiDriver::reset() {
	controller_.reset();
	hint_ = -1.0;
	max_cross_track_m_ = 0.0;
	max_body_slip_rad_ = 0.0;
	ticks_driven_ = 0;
}

void AiDriver::set_lookahead_base(double p_meters) {
	tune_.lookahead_base_m = p_meters > 0.1 ? p_meters : 0.1;
	controller_.set_tune(tune_);
}

double AiDriver::get_lookahead_base() const {
	return tune_.lookahead_base_m;
}

void AiDriver::set_lookahead_gain(double p_seconds) {
	tune_.lookahead_gain_s = p_seconds >= 0.0 ? p_seconds : 0.0;
	controller_.set_tune(tune_);
}

double AiDriver::get_lookahead_gain() const {
	return tune_.lookahead_gain_s;
}

void AiDriver::set_steer_gain(double p_gain) {
	tune_.steer_gain = p_gain;
	controller_.set_tune(tune_);
}

double AiDriver::get_steer_gain() const {
	return tune_.steer_gain;
}

void AiDriver::set_brake_plan_fraction(double p_fraction) {
	double fraction = p_fraction;
	if (fraction < 0.05) {
		fraction = 0.05;
	}
	if (fraction > 2.0) {
		fraction = 2.0;
	}
	tune_.brake_plan_fraction = fraction;
	controller_.set_tune(tune_);
}

double AiDriver::get_brake_plan_fraction() const {
	return tune_.brake_plan_fraction;
}

void AiDriver::set_traction_budget(bool p_enabled) {
	tune_.traction_budget = p_enabled;
	controller_.set_tune(tune_);
}

bool AiDriver::is_traction_budget() const {
	return tune_.traction_budget;
}

void AiDriver::set_preview_m(double p_meters) {
	tune_.preview_m = p_meters > 1.0 ? p_meters : 1.0;
	controller_.set_tune(tune_);
}

double AiDriver::get_preview_m() const {
	return tune_.preview_m;
}

} // namespace kartgame
