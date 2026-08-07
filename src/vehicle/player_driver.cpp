#include "vehicle/player_driver.h"

#include "core/replay.h"
#include "tuning/tuning_registry.h"
#include "vehicle/kart_body.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

inline double clamp01(double value) {
	return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

inline double clamp_signed_one(double value) {
	return value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value);
}

// Every `StringName` in this file is a function-local static, and it has to be.
// CLAUDE.md records why: a namespace-scope `StringName` in a GDExtension crashes
// Godot in `dyld4::callInitializer`, before any Godot frame exists, because static
// initializers run inside `dlopen` before godot-cpp has bound its interface. It
// reads exactly like a corrupt build.

} // namespace

void PlayerDriver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_kart_path", "path"), &PlayerDriver::set_kart_path);
	ClassDB::bind_method(D_METHOD("get_kart_path"), &PlayerDriver::get_kart_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "kart_path",
						 PROPERTY_HINT_NODE_PATH_VALID_TYPES, "KartBody"),
			"set_kart_path", "get_kart_path");

	ClassDB::bind_method(D_METHOD("set_tuning_path", "path"), &PlayerDriver::set_tuning_path);
	ClassDB::bind_method(D_METHOD("get_tuning_path"), &PlayerDriver::get_tuning_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "tuning_path",
						 PROPERTY_HINT_NODE_PATH_VALID_TYPES, "KartTuning"),
			"set_tuning_path", "get_tuning_path");

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &PlayerDriver::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &PlayerDriver::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	ClassDB::bind_method(D_METHOD("set_steer_gamma", "gamma"), &PlayerDriver::set_steer_gamma);
	ClassDB::bind_method(D_METHOD("get_steer_gamma"), &PlayerDriver::get_steer_gamma);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steer_gamma", PROPERTY_HINT_RANGE, "1.0,6.0,0.05"),
			"set_steer_gamma", "get_steer_gamma");

	ClassDB::bind_method(D_METHOD("steering_curve", "input"), &PlayerDriver::steering_curve);

	// Bound because `resolve_tuning` connects it by name. A signal handler that is
	// not in ClassDB fails at connect time with a message about the method, not
	// about the signal, which is a confusing ten minutes.
	ClassDB::bind_method(D_METHOD("on_tuning_changed", "key", "value", "owner"),
			&PlayerDriver::on_tuning_changed);

	ClassDB::bind_method(D_METHOD("get_throttle"), &PlayerDriver::get_throttle);
	ClassDB::bind_method(D_METHOD("get_brake"), &PlayerDriver::get_brake);
	ClassDB::bind_method(D_METHOD("get_steer"), &PlayerDriver::get_steer);
	ClassDB::bind_method(D_METHOD("get_clutch"), &PlayerDriver::get_clutch);
}

void PlayerDriver::_ready() {
	// Ahead of a `KartBody`, whose priority is the default 0. This is the first of
	// ADR-0040's two ordering defenses; the tick stamp is the second.
	set_physics_process_priority(PHYSICS_PRIORITY);
	set_physics_process(true);
	resolve_kart();
	resolve_tuning();
}

void PlayerDriver::_physics_process(double p_delta) {
	// Unused for the same reason `KartBody` ignores it: the solver runs on a
	// constant step and this node produces one struct per tick, not per second.
	(void)p_delta;

	if (kart_ == nullptr) {
		return;
	}

	static const StringName ACTION_THROTTLE("throttle");
	static const StringName ACTION_BRAKE("brake");
	static const StringName ACTION_STEER_LEFT("steer_left");
	static const StringName ACTION_STEER_RIGHT("steer_right");
	static const StringName ACTION_CLUTCH("clutch");
	static const StringName ACTION_SHIFT_UP("shift_up");
	static const StringName ACTION_SHIFT_DOWN("shift_down");

	DriverInput input;
	if (enabled_) {
		Input *map = Input::get_singleton();
		input.throttle = clamp01(map->get_action_strength(ACTION_THROTTLE));
		input.brake = clamp01(map->get_action_strength(ACTION_BRAKE));
		// The curve, applied here and nowhere else in the project. See the header
		// for the four rows that pick the exponent, and for why a scripted run
		// reaching `KartBody::input_driver` must not pass through it.
		//
		// Not rate limited here either: `steering.h` owns the rate limit and the
		// solver applies it every substep, so a scenario and a stick reach the front
		// wheels through the same one.
		input.steer = steering_curve(map->get_action_strength(ACTION_STEER_LEFT) -
				map->get_action_strength(ACTION_STEER_RIGHT));
		input.clutch = clamp01(map->get_action_strength(ACTION_CLUTCH));
		// Edges, not levels. `is_action_just_pressed` is evaluated against the
		// physics frame when it is called from one, so a held button produces one
		// request and not one per tick — which `vehicle_state.h` says is the
		// difference between a shift and a sweep through the whole gearbox.
		input.shift_up = map->is_action_just_pressed(ACTION_SHIFT_UP);
		input.shift_down = map->is_action_just_pressed(ACTION_SHIFT_DOWN);
	}
	// Disabled falls through with every axis at zero, and still pushes. See the
	// header: going silent would make the body's freshness check fail every tick
	// and log a warning about a situation nobody has a problem with.

	// Quantize to the replay storage grid **here**, upstream of the solver, and not
	// when a recording is written. `replay.h` argues this at length and then names
	// this exact call site: if input were quantized on write, the live run would
	// consume full-precision values and the replay would consume rounded ones, the
	// two would diverge at the rate any chaotic system does, and it would look
	// exactly like a solver bug.
	//
	// The call was missing for a milestone, and because `replay_encode_input`
	// **refuses** off-grid input rather than rounding it -- deliberately, so the
	// defect cannot be introduced silently -- the consequence was not a subtle
	// divergence but a flat refusal: **a human-driven lap could not be recorded at
	// all.** A stick axis is never on the grid. Found when `KartReplay` gave
	// `replay.h` its first consumer; the header had described this call in the
	// present tense since it was written.
	//
	// Free for a scripted run, which reaches `KartBody::input_driver` directly and
	// does its own snapping, and free here too: it is four multiplies on a struct
	// that is about to cross a frame boundary anyway.
	input = kart::core::replay_snap(input);

	last_input_ = input;
	kart_->set_input(input, godot::Engine::get_singleton()->get_physics_frames());
}

void PlayerDriver::set_kart_path(const NodePath &p_path) {
	kart_path_ = p_path;
	if (is_inside_tree()) {
		resolve_kart();
	}
}

NodePath PlayerDriver::get_kart_path() const {
	return kart_path_;
}

void PlayerDriver::set_tuning_path(const NodePath &p_path) {
	tuning_path_ = p_path;
	if (is_inside_tree()) {
		resolve_tuning();
	}
}

NodePath PlayerDriver::get_tuning_path() const {
	return tuning_path_;
}

void PlayerDriver::set_enabled(bool p_enabled) {
	enabled_ = p_enabled;
}

bool PlayerDriver::is_enabled() const {
	return enabled_;
}

void PlayerDriver::set_steer_gamma(double p_gamma) {
	steer_gamma_ = p_gamma < MIN_STEER_GAMMA
			? MIN_STEER_GAMMA
			: (p_gamma > MAX_STEER_GAMMA ? MAX_STEER_GAMMA : p_gamma);
}

double PlayerDriver::get_steer_gamma() const {
	return steer_gamma_;
}

double PlayerDriver::steering_curve(double p_input) const {
	const double clamped = clamp_signed_one(p_input);
	if (steer_gamma_ <= 1.0) {
		return clamped;
	}
	// Sign preserved and the exponent applied to the magnitude, so the curve is
	// symmetric and `std::pow` never sees a negative base.
	const double magnitude = std::pow(std::fabs(clamped), steer_gamma_);
	return clamped < 0.0 ? -magnitude : magnitude;
}

double PlayerDriver::get_throttle() const {
	return last_input_.throttle;
}

double PlayerDriver::get_brake() const {
	return last_input_.brake;
}

double PlayerDriver::get_steer() const {
	return last_input_.steer;
}

double PlayerDriver::get_clutch() const {
	return last_input_.clutch;
}

void PlayerDriver::resolve_kart() {
	kart_ = nullptr;
	if (kart_path_.is_empty()) {
		// A driver with no body is not an error on its own — a scene may assign the
		// path after adding the node, which is what `proving_ground.gd` does.
		return;
	}
	Node *node = get_node_or_null(kart_path_);
	kart_ = Object::cast_to<KartBody>(node);
	if (kart_ == nullptr) {
		// Loud, because the alternative is a kart that never responds to the
		// controls and a driver concluding the physics is broken. That exact failure
		// mode is why #169 exists.
		ERR_PRINT(vformat("PlayerDriver '%s': kart_path '%s' does not name a KartBody, so "
						  "nothing will drive.",
				get_name(), String(kart_path_)));
	}
}

void PlayerDriver::resolve_tuning() {
	if (tuning_path_.is_empty()) {
		return;
	}
	KartTuning *tuning = Object::cast_to<KartTuning>(get_node_or_null(tuning_path_));
	if (tuning == nullptr) {
		ERR_PRINT(vformat("PlayerDriver '%s': tuning_path '%s' does not name a KartTuning, so "
						  "steer_gamma stays at its default.",
				get_name(), String(tuning_path_)));
		return;
	}

	static const StringName SIGNAL_TUNING_CHANGED("tuning_changed");
	const Callable handler = Callable(this, "on_tuning_changed");
	if (!tuning->is_connected(SIGNAL_TUNING_CHANGED, handler)) {
		tuning->connect(SIGNAL_TUNING_CHANGED, handler);
	}

	// Read the current value as well as subscribing. `KartTuning::apply_all` may
	// already have run — it does on `set_vehicle_path`, which a scene typically
	// calls before this node is ready — and a subscriber that only listened would
	// sit on the default until somebody moved the value by hand.
	const int id = tuning->id_of(String("steer_gamma"));
	if (id >= 0) {
		set_steer_gamma(tuning->get_value(id));
	}
}

void PlayerDriver::on_tuning_changed(const String &p_key, double p_value, int p_owner) {
	// The owner is not checked against `Controller` here. The key is what
	// identifies a tunable — `tuning.h` says so, and it is why a saved preset
	// carries keys rather than indices — and a second condition on the owner would
	// silently stop working the day a value is reclassified.
	(void)p_owner;
	if (p_key == String("steer_gamma")) {
		set_steer_gamma(p_value);
	}
}

} // namespace kartgame
