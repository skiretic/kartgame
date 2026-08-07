#ifndef KARTGAME_VEHICLE_AI_DRIVER_H
#define KARTGAME_VEHICLE_AI_DRIVER_H

#include "core/ai_driver.h"
#include "kart_racing_line.h"
#include "vehicle/player_driver.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

namespace kartgame {

class KartBody;

// An AI at the controls, as a node. ROADMAP M7, and ADR-0040's third producer —
// the header of `player_driver.h` names it: *"an AI emitting the same struct
// through the same path"*.
//
// Thin, in the sense `kart_racing_line.h` is thin. It unpacks the world into a
// `kart::core::ai::AiObservation`, calls `kart::core::ai::AiDriver::step`, snaps
// the answer onto the replay grid and pushes it. Pure pursuit, the speed
// controller, the shift hysteresis and the launch are all in `src/core/`, where
// `tests/run.sh` reaches them in five seconds with no engine at all.
//
// ## Why it derives from `PlayerDriver` rather than sitting beside it
//
// `SessionRunner.configure()` takes `driver: PlayerDriver` as a **statically
// typed** GDScript argument, and it is the single writer of `PlayerDriver.enabled`
// — the flag a countdown, a pause and a free camera all get their authority from.
// An AI that were a sibling class could not be handed to a session at all, and
// every headless gate would then have to build its own harness rather than
// driving the real one, which is exactly the thing `ai.sh` exists not to do.
//
// So this **is** a `PlayerDriver`, with `_physics_process` replaced. It inherits
// `PHYSICS_PRIORITY`, `kart_path`, `enabled` and the tuning subscription; it
// never reads the `Input` singleton, because the override means the base's
// `_physics_process` does not run at all.
//
// **Both `_ready` and `_physics_process` are re-declared here and that is not
// decoration.** godot-cpp registers a virtual only when the derived class's
// member function differs from its parent's, walking one level at a time. A
// virtual defined in `PlayerDriver` and not re-declared here would therefore not
// be registered for this class — `_ready` would silently never run, the physics
// priority would stay at the default 0, and the kart would spend every tick on
// last tick's intent. Which reads as a physics bug.
//
// ## What it is handed and what it refuses
//
// A **line** (`KartRacingLine`, already solved) and a **course** (anything with
// `project(Vector3, float)` and `length()` — `KartTrack` in practice). Without
// both it pushes neutral and says so once, rather than driving something it
// cannot see. `configure_from` reads every ceiling off the line's own
// `model()` at that moment, so no grip figure is written down anywhere in the
// AI: issue #137 will move the tire, the line will answer differently, and this
// node will follow it without an edit.
class AiDriver : public PlayerDriver {
	GDCLASS(AiDriver, PlayerDriver)

protected:
	static void _bind_methods();

public:
	AiDriver();
	~AiDriver() override = default;

	// Re-declared so godot-cpp registers them for *this* class. See the header.
	void _ready() override;
	void _physics_process(double p_delta) override;

	// The solved line to follow. Setting it re-reads every ceiling from
	// `KartRacingLine::model()`, so a caller that re-solves at a different
	// `grip_usage` must set it again — which is what a difficulty change is.
	void set_line(const godot::Ref<KartRacingLine> &p_line);
	godot::Ref<KartRacingLine> get_line() const;

	// Where the kart is on the circuit. Duck typed for the reason
	// `SessionRunner.configure()` is: `KartTrack` is a GDExtension `RefCounted`
	// and `TrackLayout` is a GDScript `Node3D`, so they cannot share a base
	// class. Two methods, checked by name: `length()` and `project()`.
	void set_course(godot::Object *p_course);
	godot::Object *get_course() const;

	// Re-read the ceilings from the line as it stands now. Called by
	// `set_line`; exposed so a probe can prove the figures came from the model
	// rather than from this file.
	bool configure_from_line();

	// Everything the controller was told, in SI. A gate reports what the run was
	// measured against rather than restating it — the same reason
	// `KartRacingLine::model()` exists.
	godot::Dictionary limits() const;

	// Everything it decided this tick, plus what it decided it from.
	godot::Dictionary decision() const;

	// The running maxima, so a headless probe does not have to sample at frame
	// rate to catch a peak that happened between two of its reads.
	double get_max_cross_track_m() const;
	double get_max_body_slip_rad() const;
	double get_cross_track_m() const;
	double get_station_m() const;
	double get_target_speed_ms() const;
	uint64_t get_ticks_driven() const;

	// Forget the hysteresis, the station hint and the maxima. A respawn
	// invalidates all three: a hint after a teleport confines the next search to
	// a window the kart is no longer in and returns the nearest point of it with
	// every appearance of success, which is the defect `Track::project` was
	// carrying until last night.
	void reset();

	// --- the controller's dials, forwarded ----------------------------------

	// 0..1 of the *line's* envelope is `KartRacingLine::set_grip_usage`, not
	// this. These are the controller's own, and each one is a `AiTune` field
	// with its derivation in `core/ai_driver.h`.
	void set_lookahead_base(double p_meters);
	double get_lookahead_base() const;
	void set_lookahead_gain(double p_seconds);
	double get_lookahead_gain() const;
	void set_steer_gain(double p_gain);
	double get_steer_gain() const;
	void set_brake_plan_fraction(double p_fraction);
	double get_brake_plan_fraction() const;
	void set_traction_budget(bool p_enabled);
	bool is_traction_budget() const;
	void set_preview_m(double p_meters);
	double get_preview_m() const;

private:
	// Resolve the body from `PlayerDriver`'s own `kart_path`. This class keeps
	// its own pointer because the base's is private, and going through the
	// public path getter is one `get_node_or_null` at `_ready` either way.
	void resolve_body();

	// Fill the braking window from the line. Nearest first, first sample at the
	// kart — `AiDriver::step` needs a zero-distance entry or the corner it is
	// standing in cannot bind.
	void fill_preview(double p_station, kart::core::ai::AiObservation &r_observation) const;

	// The line's own arrays, snapshotted at `configure_from_line` so the tick
	// path allocates nothing. `locate` is the wrapping index lookup the four
	// samplers share; it returns false only when there is no line.
	bool locate(double p_station, int &r_low, int &r_high, double &r_t) const;
	void sample_point(double p_station, double &r_x, double &r_z) const;
	double sample_speed(double p_station) const;
	double sample_curvature(double p_station) const;
	int sample_gear(double p_station) const;

	// Speed -> gear, built once from the solved line. See the definition: the
	// line's gear *at a station* is the wrong answer for a kart that is not at
	// the line's speed there, and on a standing start it is sixth.
	static constexpr int GEAR_TABLE_BUCKETS = 128;
	void build_gear_table(double p_fastest_ms);
	int gear_for_speed(double p_speed_ms) const;

	godot::PackedInt32Array gear_by_speed_;
	double gear_speed_step_ = 0.0;

	godot::PackedFloat64Array point_x_;
	godot::PackedFloat64Array point_z_;
	godot::PackedFloat64Array speed_;
	godot::PackedFloat64Array curvature_;
	godot::PackedInt32Array gear_;
	double spacing_ = 0.0;
	double length_m_ = 0.0;

	godot::Ref<KartRacingLine> line_;
	godot::Object *course_ = nullptr;

	KartBody *body_ = nullptr;
	bool warned_ = false;

	kart::core::ai::AiDriver controller_;
	kart::core::ai::AiTune tune_;

	// The carried station hint. `SessionRunner` hints every tick and so does
	// this, which is the exact path that was 687.5 m wrong until `b783c61`.
	double hint_ = -1.0;

	double cross_track_m_ = 0.0;
	double max_cross_track_m_ = 0.0;
	double body_slip_rad_ = 0.0;
	double max_body_slip_rad_ = 0.0;
	// The last struct handed to the body, already snapped. See `decision()`.
	kart::core::DriverInput pushed_;
	double aim_x_ = 0.0;
	double aim_z_ = 0.0;
	double kart_x_ = 0.0;
	double kart_z_ = 0.0;
	double heading_ = 0.0;
	double line_x_ = 0.0;
	double line_z_ = 0.0;
	double station_m_ = 0.0;
	double target_speed_ms_ = 0.0;
	uint64_t ticks_driven_ = 0;
};

} // namespace kartgame

#endif // KARTGAME_VEHICLE_AI_DRIVER_H
