#ifndef KARTGAME_VEHICLE_PLAYER_DRIVER_H
#define KARTGAME_VEHICLE_PLAYER_DRIVER_H

#include "core/vehicle_state.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>

namespace kartgame {

class KartBody;

// The human at the controls, as a node. ADR-0040, and the first of the four
// producers it names.
//
// `KartBody` used to fill its own `DriverInput` from `Input::get_singleton()`.
// That works for exactly one configuration — one kart, one human, one process,
// now — and five things this project has committed to need input as *data*: a
// replay that re-sims to an identical state hash, a ghost, an AI emitting the
// same struct through the same path, eight karts in one scene, and a headless
// gate that asserts the advertised controls are the real ones (#169).
//
// The symptom that proved the arrow was backwards is already in the tree, and it
// is not any of those five. The tuning overlay cannot use the arrow keys, because
// they are the second binding on throttle, brake and steer and `KartBody` polled
// them through a singleton where `set_input_as_handled` cannot reach. A node that
// fetches global input cannot be told to stop, so a menu, a pause, an overlay or
// a replay has no authority over it. This node is where that authority now lives:
// `set_enabled(false)` and the kart coasts, whatever the keyboard is doing.
//
// ## What it owns that the vehicle does not
//
// **The steering curve.** ADR-0036 established that the curve is a controller
// property and that `steering.h`'s position — no speed-dependent input aid in the
// vehicle — is correct; the code kept the exponent in `kart_body.cpp` anyway.
// It is here now, with its derivation, and `tuning.h`'s `steer_gamma` row points
// at this file.
//
// **The curve is applied to the player's stick and to nothing else.** A scripted
// run reaching `KartBody::input_driver` with `--lock=0.4` is asking for a *lock
// fraction*, with a recorded sweep table attached to it in
// `tools/verify/drive_probe.gd`. Curving that would silently rewrite every figure
// in ROADMAP M3b and the measurements would still look plausible. A scripted run
// asks for a steering angle; a driver asks for a stick position. They are not the
// same request, and `steer_gamma = 1.0` collapses the two exactly.
//
// ## Ordering, which is not left to the tree
//
// A push model has one failure mode: the producer runs *after* the consumer and
// the vehicle spends every tick on last tick's intent, which reads as a physics
// bug. `_ready` sets this node's physics priority below the body's default so
// tree position cannot decide it, and `KartBody` checks the tick stamp anyway and
// falls to neutral if it is stale. Two defenses, because the failure is silent and
// looks like something else.
class PlayerDriver : public godot::Node {
	GDCLASS(PlayerDriver, godot::Node)

protected:
	static void _bind_methods();

public:
	// Runs before a `KartBody`, which leaves its own priority at the default 0.
	// `set_physics_process_priority` orders physics processing independently of
	// tree order, so a scene is free to put this node wherever it reads best.
	static constexpr int PHYSICS_PRIORITY = -1;

	// The steering curve's exponent, and every one of the four numbers below is
	// why it is 3.0. With `x^3`, against the measured 0.065-of-lock limit at
	// 100 km/h and `project.godot`'s 0.15 deadzone:
	//
	//     stick   lock    inner deg  radius m  asked g   what it is
	//      0.15   0.0034      0.08    713.57     0.11    the deadzone edge
	//      0.40   0.0640      1.60     38.14     2.06    the 100 km/h limit
	//      0.62   0.2383      5.96     10.61     7.41    Turn 2's 11 m hairpin
	//      1.00   1.0000     25.00      2.80    28.06    full lock, still there
	//
	// The exponent is chosen from the second row: it puts the fastest corner on the
	// track at 40% of stick travel, which is where a thumb has resolution, instead
	// of at 6.5% where it has none. The first row is the fix — the deadzone edge
	// asked 4.8 g before the curve and asks 0.11 g after it, so the smallest input
	// a stick can make is now a corner instead of a slide. The third row is the
	// check that it did not overshoot: a curve that made fast corners comfortable
	// by pushing the hairpin past the end of the stick would have traded one
	// unreachable corner for another.
	//
	// Those five columns are printed by `tests/core/test_vehicle.cpp`'s
	// steering-step case, which is where they were measured rather than computed in
	// this comment.
	//
	// `project.godot` sets the steer actions' deadzone to 0.15 and Godot's
	// `get_action_strength` returns the raw value above the deadzone with no
	// rescaling, so the smallest input a stick can produce is 0.15 of lock. Without
	// the curve the entire followable range at 100 km/h is *inside the deadzone* —
	// there is no stick position that produces a corner the kart can hold. That is
	// not a difficult car, it is an unreachable one.
	//
	// Tunable because it is judged by feel and this is a first estimate from
	// arithmetic. 1.0 restores the linear mapping exactly, which is what issue #40's
	// "assists off" wants and what makes a driven run comparable with a scripted one.
	static constexpr double DEFAULT_STEER_GAMMA = 3.0;
	static constexpr double MIN_STEER_GAMMA = 1.0;
	static constexpr double MAX_STEER_GAMMA = 6.0;

	void _ready() override;
	void _physics_process(double p_delta) override;

	// The body this driver drives. Resolved in `_ready` and on assignment, and an
	// unresolvable path is an error at that point rather than a kart that silently
	// never moves.
	void set_kart_path(const godot::NodePath &p_path);
	godot::NodePath get_kart_path() const;

	// A `KartTuning` node to take `steer_gamma` from, or an empty path to use the
	// default above.
	//
	// **The registry does not push to this node and deliberately cannot.**
	// `tuning.h` marks `steer_gamma` as `TuningOwner::Controller` — "the human
	// input path only" — and `KartTuning::apply` only knows how to reach a
	// `KartBody`. Every other non-vehicle consumer in the project takes its value
	// off the `tuning_changed` signal, which is what makes a consumer that is not
	// in the scene cost nothing rather than needing a null check in the registry.
	// This node is one more of those.
	void set_tuning_path(const godot::NodePath &p_path);
	godot::NodePath get_tuning_path() const;

	// Stop driving without detaching. The free camera turns this off so the kart
	// keeps simulating and coasts while WASD flies the camera.
	//
	// **Disabled still pushes, and it pushes neutral.** Going silent instead would
	// leave `KartBody`'s freshness check failing every tick, which would fill the
	// log with a stale-input warning describing a situation nobody has a problem
	// with — and would train a reader to ignore the one message that means a
	// producer really did break.
	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_steer_gamma(double p_gamma);
	double get_steer_gamma() const;

	// The curve itself, exposed so `tests/` and the on-screen readout can ask what
	// a stick position becomes without going through an `Input` singleton.
	double steering_curve(double p_input) const;

	// What was last handed to the body, for a readout or a gate.
	double get_throttle() const;
	double get_brake() const;
	double get_steer() const;
	double get_clutch() const;

private:
	// Resolve `kart_path_`, complaining if it names nothing or names the wrong
	// type. Called from `_ready` and from `set_kart_path` once in the tree.
	void resolve_kart();

	// Take the current `steer_gamma` from the registry and subscribe to changes.
	// Reads the value as well as subscribing, because `KartTuning::apply_all` may
	// already have run by the time this node is ready and a subscriber that only
	// listens would sit on the default until the next edit.
	void resolve_tuning();

	// The `tuning_changed` handler. Ignores every key but its own.
	void on_tuning_changed(const godot::String &p_key, double p_value, int p_owner);

	godot::NodePath kart_path_;
	KartBody *kart_ = nullptr;

	godot::NodePath tuning_path_;

	bool enabled_ = true;
	double steer_gamma_ = DEFAULT_STEER_GAMMA;

	kart::core::DriverInput last_input_;
};

} // namespace kartgame

#endif // KARTGAME_VEHICLE_PLAYER_DRIVER_H
