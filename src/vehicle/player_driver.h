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

	// The steering curve's exponent, and every one of the numbers below is why it
	// is 3.0. With `x^3`, against the measured 0.065-of-lock limit at 100 km/h.
	//
	// **The curve's input is not the stick position.** It is what
	// `Input::get_action_strength` returns, which for a joypad axis is the
	// deadzone-rescaled value, and the two columns differ by 0.15 of travel at
	// every row. Both are given here because writing one and meaning the other is
	// the mistake this table used to make:
	//
	//   raw stick  strength    lock    inner deg  radius m  asked g   what it is
	//      0.1500   0.00000  0.00000     0.0000       inf     0.00   the deadzone edge
	//      0.1569   0.00807  0.00000     0.0000       inf     0.00   one 8-bit code past it
	//      0.1700   0.02353  0.00000     0.0000       inf     0.00   the last dead position
	//      0.1711   0.02482  0.00003     0.0008         -        -   the first live one
	//      0.2775   0.15000  0.00339     0.0847    713.57     0.11   strength 0.15
	//      0.4220   0.32003  0.03278     0.8194         -     2.10   asks the 140 km/h limit
	//      0.4900   0.40000  0.06400     1.5999     38.14     2.06   the 100 km/h limit
	//      0.6770   0.62000  0.23832     5.9580     10.61     7.41   Turn 2's 11 m hairpin
	//      1.0000   1.00000  1.00000    25.0000      2.80    28.06   full lock, still there
	//
	// The exponent is chosen from the 100 km/h row: it puts the fastest corner on
	// the track at 49% of stick travel, which is where a thumb has resolution,
	// instead of at 17.7% where it has none. The hairpin row is the check that it
	// did not overshoot — a curve that made fast corners comfortable by pushing the
	// hairpin past the end of the stick would have traded one unreachable corner
	// for another.
	//
	// The raw-stick, strength, lock and inner-degree columns are **measured** by
	// `tools/verify/stick_probe.gd --case=lock`, which walks a synthesized joypad
	// axis through the real InputMap and reads the angle back off `wheel_report()`
	// in `valdirone.tscn`. The radius and asked-g columns come from
	// `tests/core/test_vehicle.cpp`'s steering-step case and are left blank on the
	// two rows that case does not print — the radius column is the Ackermann
	// solve's answer and is **not** `wheelbase / tan(inner)`, so a value invented
	// for a new row by that arithmetic would be wrong and would look right. The
	// 0.4220 row's 2.10 g is ADR-0072's measured lateral ceiling by construction:
	// that is the row's definition, `atan(1.050 / 73.411)`, on `steering.h`'s
	// sourced 1.050 m wheelbase.
	//
	// ## What this comment said before, and why it mattered
	//
	// It said Godot's `get_action_strength` *"returns the raw value above the
	// deadzone with no rescaling, so the smallest input a stick can produce is 0.15
	// of lock"*. **Measured on 4.7.1, that is false.** A joypad axis is rescaled:
	//
	//     strength = clamp((|raw| - deadzone) / (1 - deadzone), 0, 1)
	//
	// fitted to a worst residual of 9.6e-8 across sixteen positions, while the
	// pass-through model it claimed misses by 0.1488. So the deadzone edge does not
	// deliver 0.15 of anything — it delivers **exactly zero**, and leaves zero as
	// `(raw - 0.15)^3`. The first three rows above are all zero for a second reason
	// on top of that: `replay_snap` quantizes steer to 32,767 codes, so any lock
	// below half a code is rounded to nothing, and **no raw stick below 0.1711
	// produces a single count of steering.** Measured: 0.1700 delivers 0.000000 deg
	// and 0.1711 delivers 0.000763 deg, which is one code.
	//
	// The conclusion survives — 3.0 is still the right order of exponent and the
	// constant does not move — but the argument it rested on was the wrong way
	// round. Without the curve the smallest producible stick would ask 0.0081 of
	// lock and 0.005 g, not 4.8 g; what the curve buys is resolution in the middle
	// of the travel, not protection at the bottom of it. Issue #239, which was
	// filed on the belief that the bottom of the travel was dangerous, and closed
	// by measuring that it delivers nothing at all.
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
