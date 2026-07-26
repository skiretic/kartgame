extends SceneTree

## Issues #30 and #31 — measure the M3b boundary, `src/vehicle/kart_body.cpp`.
##
##     godot --headless --path . --script tools/verify/kart_body_probe.gd
##
## The first headless *editor* import of a cold project crashes on macOS and
## Linux (upstream issue #2, ADR-0018), so a cold checkout needs the same double
## import `tools/verify/verify.sh` does:
##
##     godot --headless --path . --editor --quit-after 2 ; true
##     godot --headless --path . --editor --quit-after 2
##     godot --headless --path . --script tools/verify/kart_body_probe.gd
##
## ## Why this file exists
##
## `src/core/` is held by 175 test cases and 244,091 assertions that run with no
## engine at all, and `tools/verify/contact_probe.gd` measured what the engine
## does at a contact. Neither of them can see the thing in between: a
## `RigidBody3D` that casts four rays, calls `KartVehicle::step` and applies what
## comes back. Every number below is a property of that seam and of nothing else.
##
## The discipline is `contact_probe.gd`'s and it is not decoration — **every
## measured number is printed beside the number theory predicts for it**, and
## where two conventions are plausible, both predictions are printed so the
## reader can see which one the boundary landed on rather than being told.
##
## ## The six measurements
##
##   a. **Settle.** Dropped from 0.30 m, does the kart come to rest at the height
##      the suspension's own free length predicts? The chassis origin is on the
##      ground by construction (`chassis.h`), so its resting height is *minus the
##      static tire deflection* and nothing else — a closed-form number that the
##      spring, the mass table and the ray geometry all have to agree on.
##   b. **Static corner loads**, against `kz::static_load_front_tire` and
##      `static_load_rear_tire`, summing to `m*g`.
##   c. **The lever arm.** ADR-0033's regression test, in two forms. See the
##      section header below — this is the one that catches a reintroduced
##      center-of-mass subtraction, and the failure it catches is not subtle.
##   d. **Straight-line acceleration**, against ROADMAP M3b's solver-only 143.9
##      km/h and 4.50 s. A large gap means the boundary is wrong, not the solver.
##   e. **Determinism**, two identical runs in one process.
##   f. **Cost**, against `ARCHITECTURE.md` §15's 2.0 ms.
##
## ## Rules this file keeps
##
## Nothing the measurement depends on reads wall-clock time, with one labeled
## exception: section (f), which is a duration and cannot be anything else.
##
## Nothing here is tuned to agree. A number that disagrees with its prediction is
## a defect in the boundary or in the prediction, and the report says which.

# --- the ground ---------------------------------------------------------------

## Square, and large: the acceleration run covers 700 m down -Z at full throttle.
const GROUND_SIZE := 4000.0
const GROUND_THICKNESS := 2.0

## Matching `scripts/game/proving_ground.gd`. The value is very nearly irrelevant
## — the kart body sets its own `physics_material_override.friction` to 0.0 and
## the combine rule is measured to be `min(a, b)` — and that is the point: if
## this number ever changes an answer below, convention 2 has stopped holding.
const GROUND_FRICTION := 1.0

## Lane spacing along X. The karts are 1.185 m wide over the rear track and
## nothing in this file moves sideways by more than a few meters.
const LANE_SPACING := 8.0

# --- the kart's collision shape ------------------------------------------------

## `KartBody` deliberately does not own its own collision shape — the scene adds
## it. This is that shape, sized so it cannot touch the ground: the suspension
## holds the chassis origin within a few millimeters of y = 0, and this box's
## bottom face sits 0.17 m above it. A chassis box that reached the road would
## have Jolt supplying a second normal force on top of the suspension's, and
## every load below would be wrong by however much of the kart Jolt decided to
## carry.
const SHAPE_SIZE := Vector3(0.90, 0.30, 1.60)
const SHAPE_CENTER := Vector3(0.0, 0.32, 0.0)

# --- constants restated from src/core/, for the analytic predictions -----------
#
# Restated rather than read, because a prediction computed from the same value
# the measurement used is not a prediction. Each names its owner.

## `chassis_flex.h`. Vertical rate of one tire, N/m — a kart has no springs, so
## the tire *is* the spring.
const TIRE_RATE_FRONT := 106000.0
const TIRE_RATE_REAR := 277500.0

## `chassis.h`. The published KZ static front share, which the driver's seating
## position was solved to reproduce.
const STATIC_FRONT_SHARE := 0.42

## `chassis.h`. Unloaded tire radii and axle positions, meters. +Z is rearward.
const FRONT_WHEEL_RADIUS := 0.140
const REAR_WHEEL_RADIUS := 0.1475
const FRONT_AXLE_Z := -0.525
const REAR_AXLE_Z := 0.525

## `units.h`. The core's gravity. **Not** the project's, which is 9.8 — a 0.068%
## difference that shows up in the third digit of every load below, so both are
## carried rather than one being rounded into the other.
const G_CORE := 9.80665

## ROADMAP M3b's solver-only figures, measured with no engine running.
const SOLVER_TOP_SPEED_KMH := 143.9
const SOLVER_ZERO_TO_100_S := 4.50
const SOLVER_COST_US := 15.4

## `ARCHITECTURE.md` §15's whole vehicle-sim budget, milliseconds per frame.
const BUDGET_MS := 2.0

# --- timing --------------------------------------------------------------------

## Long enough for a kart dropped from 0.30 m to stop bouncing. The tire's
## undamped natural frequency at the rear is sqrt(277500/50.75)/2pi = 11.8 Hz, so
## 3 s is thirty-five cycles of the slowest mode at 30-45% of critical damping.
const SETTLE_TICKS := 360

## The tail of the settle over which the resting statistics are gathered. Taking
## the last second rather than the last tick is what distinguishes "at rest" from
## "at the bottom of a slow oscillation".
const REST_WINDOW := 120

## Each determinism run, in ticks. 2 s of full throttle and a steering sweep is
## enough to put every part of the solver in play — a launch, an upshift, a
## corner, four loaded tires.
const DET_TICKS := 240

## Ticks of quiet after each `respawn()` before the scripted run starts, with the
## input at zero and nothing hashed.
##
## **This is part of the experiment, not a fudge.** `respawn()` teleports a
## `RigidBody3D` and zeroes its velocities from `_physics_process`, which is a
## discontinuity the engine is entitled to resolve over a tick or two — and run 1
## enters it from a kart at rest while run 2 enters it from a kart at 30 m/s in a
## corner. Hashing from the first tick after a teleport measures how Jolt
## recovers from two different discontinuities, which is a real question and is
## not the question here. A settle preamble puts both runs at the same physical
## state, which is what "the same scenario twice" means.
const DET_PREAMBLE_TICKS := 120

## Ticks between state hashes. ARCHITECTURE.md §8's "every N ticks".
const HASH_EVERY := 4

## The acceleration run, in ticks. 20 s: the solver reaches 143.9 km/h
## asymptotically, so this reports where the kart is after 20 s as well as the
## time to 100 km/h, which is the figure with a reference band.
const ACCEL_TICKS := 2400

## Each half of the cost measurement, in ticks.
const COST_TICKS := 240

## 100 km/h in m/s, the §6.4 acceleration figure's target.
const HUNDRED_KMH := 100.0 / 3.6


var _ground: StaticBody3D
var _kart_settle: KartBody
var _kart_accel: KartBody
var _kart_det: KartBody

var _tick := 0
var _phase := 0
var _phase_tick := 0
var _delta := 0.0
var _lines: Array[String] = []

## Section (a) and (b): resting statistics, gathered over the last `REST_WINDOW`
## ticks of the settle.
var _rest_samples := 0
var _rest_y_sum := 0.0
var _rest_y_min := INF
var _rest_y_max := -INF
var _rest_speed_max := 0.0
var _rest_omega_max := 0.0
var _rest_load := [0.0, 0.0, 0.0, 0.0]
var _rest_travel := [0.0, 0.0, 0.0, 0.0]
var _rest_pitch := 0.0
var _rest_grounded := 0
var _rest_latched := 0

## Section (c2): one tick of the acceleration run, reconstructed.
var _lever_done := false
var _lever_omega_before := Vector3.ZERO
var _lever_basis_before := Basis()
var _lever_report: Array = []
var _lever_measured := Vector3.ZERO
var _lever_com := Vector3.ZERO
var _lever_origin := Vector3.ZERO

## Section (d).
var _accel_top_speed := 0.0
var _accel_hundred_tick := -1
var _accel_hundred_prev := 0.0
var _accel_hundred_at := 0.0
var _accel_first_motion_tick := -1
var _accel_end_speed := 0.0
var _accel_end_gear := 0

## Section (e).
var _det_run := 0
var _det_tick := 0
var _det_pre := 0
var _det_hash: KartStateHash
var _det_digests: Array[Array] = [[], []]
## The first two samples of each run, kept unhashed so that a divergence can be
## read in meters and m/s instead of in hex.
var _det_states: Array[Array] = [[], []]
var _det_preamble: Array[Array] = [[], []]

## Section (f). The one wall-clock measurement, and it gates nothing.
var _cost_sum := [0.0, 0.0]
var _cost_samples := [0, 0]
var _cost_min := [INF, INF]
## Which half this tick belongs to, and when the tick's physics span opened.
var _cost_half := -1
var _cost_opened := 0


func _initialize() -> void:
	_build_ground()
	# Every kart is built here and held by reference, so the trap the CLAUDE.md
	# note describes — a `--script` main loop's scene is not in the tree during
	# `_initialize`, so `get_root().add_child()` parents it but `_ready` has not
	# run — does not apply: nothing below is looked up by name. What does happen
	# at `add_child` is `KartBody::_ready`, which is where the mass properties,
	# the damping, the friction override and the fixed step are installed, so
	# every property read after this point is the configured one.
	_kart_settle = _build_kart("Settle", 0, 0.30)
	_kart_accel = _build_kart("Accel", 1, 0.0)
	_kart_det = _build_kart("Det", 2, 0.0)

	# Only the determinism kart is driven from a script. The other two are driven
	# by the phase machine below through `set_input_driver` as they are needed.
	_kart_det.set_input_driver(Callable(self, "_det_input"))
	_kart_accel.set_input_driver(Callable(self, "_accel_input"))
	_kart_settle.set_input_driver(Callable(self, "_idle_input"))

	_det_hash = KartStateHash.new()


func _build_ground() -> void:
	_ground = StaticBody3D.new()
	_ground.name = "Ground"
	var material := PhysicsMaterial.new()
	material.friction = GROUND_FRICTION
	_ground.physics_material_override = material
	# The metadata key `KartBody::query_ground` reads. Asphalt is 0 and is also
	# what a collider with no metadata falls back to, so this exercises the
	# lookup path without changing any grip number.
	_ground.set_meta("surface_type", 0)

	var shape := BoxShape3D.new()
	shape.size = Vector3(GROUND_SIZE, GROUND_THICKNESS, GROUND_SIZE)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	# Sunk so the top face is exactly y = 0. Every analytic height in this file is
	# written against that plane.
	collider.position = Vector3(0.0, -GROUND_THICKNESS * 0.5, 0.0)
	_ground.add_child(collider)
	get_root().add_child(_ground)


func _build_kart(label: String, lane: int, drop: float) -> KartBody:
	var kart := KartBody.new()
	kart.name = "Kart" + label
	# Set before `add_child`, because `KartBody::_ready` captures the spawn from
	# the transform it finds.
	kart.position = Vector3(float(lane) * LANE_SPACING, drop, 0.0)

	var collider := CollisionShape3D.new()
	collider.name = "Chassis"
	var shape := BoxShape3D.new()
	shape.size = SHAPE_SIZE
	collider.shape = shape
	collider.position = SHAPE_CENTER
	kart.add_child(collider)

	get_root().add_child(kart)
	return kart


# --- the scripted drivers -------------------------------------------------------
#
# Open-loop functions of a tick counter, which is what makes a run reproducible
# from the file rather than from a recording. `kart_body.h` keeps M3a's Callable
# shape for exactly this.


func _idle_input() -> Dictionary:
	return {"throttle": 0.0, "brake": 0.0, "steer": 0.0}


func _accel_input() -> Dictionary:
	return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}


func _det_input() -> Dictionary:
	# A launch, an upshift and a steering sweep, all from one integer. 0.05
	# rad/tick is a 2.4 s period at 120 Hz, which puts the kart through full lock
	# both ways inside the 2 s run.
	return {
		"throttle": 1.0,
		"brake": 0.0,
		"steer": sin(float(_det_tick) * 0.05),
	}


# --- the run --------------------------------------------------------------------


func _physics_process(delta: float) -> bool:
	# This callback runs *before* the nodes' own `_physics_process` in the same
	# iteration, and the physics server steps after both. So a body state read
	# here is the result of the previous tick's forces, and a telemetry read here
	# describes the solver call that produced them. Every one-tick comparison
	# below depends on that ordering.
	_delta = delta
	_tick += 1

	match _phase:
		0:
			return _phase_settle()
		1:
			return _phase_determinism()
		2:
			return _phase_accel()
		3:
			return _phase_cost()
	return true


## Closes the cost span opened in `_physics_process`. The idle frame runs after
## the physics iteration in the same `Main::iteration`, so this is the first
## point at which the tick's physics work is known to be finished.
func _process(_unused: float) -> bool:
	if _phase == 3 and _cost_half >= 0 and _cost_opened > 0:
		var span := float(Time.get_ticks_usec() - _cost_opened)
		_cost_sum[_cost_half] = float(_cost_sum[_cost_half]) + span
		_cost_samples[_cost_half] = int(_cost_samples[_cost_half]) + 1
		_cost_min[_cost_half] = minf(float(_cost_min[_cost_half]), span)
		_cost_opened = 0
	return false


func _advance_phase() -> void:
	_phase += 1
	_phase_tick = 0


# --- (a) and (b): settle and static loads ---------------------------------------


func _phase_settle() -> bool:
	if _phase_tick >= SETTLE_TICKS - REST_WINDOW:
		_sample_rest()
	_phase_tick += 1
	if _phase_tick < SETTLE_TICKS:
		return false

	# Everything that is not the determinism kart is frozen for the next phase, so
	# that two runs of the same scenario see the same world. A `RigidBody3D` left
	# free would be in a different place on run 2 than on run 1, and a shared
	# broadphase is a shared thing for two runs to differ by.
	_park(_kart_settle)
	_park(_kart_accel)
	_start_det_run(0)
	_advance_phase()
	return false


func _start_det_run(run: int) -> void:
	_det_run = run
	_det_tick = 0
	_det_pre = 0
	# One call, not two. `respawn()` resets the solver as well as the body —
	# `kart_body.h` says why, and a run that started with the previous run's axle
	# speed would be a determinism failure that was really a test bug.
	_kart_det.respawn()
	_kart_det.set_input_driver(Callable(self, "_idle_input"))
	# One force-free tick after the teleport, re-enabled at `_det_pre == 0`.
	#
	# Without it the two runs are not bit-identical one step after `respawn()`,
	# and the signature says exactly why: the velocity difference is 0.000000000
	# m/s while the spin difference is 2.1e-3 rad/s. Identical forces, different
	# torque — which is what happens when the body's transform is teleported in
	# the same tick that forces are applied at an offset, because the torque is
	# resolved against a center-of-mass position that the teleport has not reached
	# yet. Run 1 respawns a kart that was standing still and run 2 respawns one at
	# 30 m/s, so the two stale frames differ. See the report below.
	_kart_det.set_physics_process(false)


func _park(kart: KartBody) -> void:
	kart.set_physics_process(false)
	kart.freeze_mode = RigidBody3D.FREEZE_MODE_STATIC
	kart.freeze = true


func _unpark(kart: KartBody) -> void:
	kart.freeze = false
	kart.set_physics_process(true)


func _sample_rest() -> void:
	var kart := _kart_settle
	var y := kart.global_position.y
	_rest_samples += 1
	_rest_y_sum += y
	_rest_y_min = minf(_rest_y_min, y)
	_rest_y_max = maxf(_rest_y_max, y)
	_rest_speed_max = maxf(_rest_speed_max, kart.linear_velocity.length())
	_rest_omega_max = maxf(_rest_omega_max, kart.angular_velocity.length())
	# The pitch the settled kart sits at, read off the basis rather than from
	# Euler angles: the front tire deflects twice as far as the rear, so a settled
	# kart is nose-down by a predictable fraction of a degree.
	# `Basis.z` is column 2, the chassis's rearward axis, so its negation is
	# forward. GDScript exposes the columns as `x`/`y`/`z` and has no
	# `get_column()`.
	var forward := -kart.global_transform.basis.z
	_rest_pitch += asin(clampf(forward.y, -1.0, 1.0))

	var report := kart.wheel_report()
	var grounded := 0
	var latched := 0
	for corner in 4:
		var wheel: Dictionary = report[corner]
		_rest_load[corner] = float(_rest_load[corner]) + float(wheel["load"])
		_rest_travel[corner] = float(_rest_travel[corner]) + float(wheel["travel"])
		if bool(wheel["contact"]):
			grounded += 1
		if bool(wheel["latched"]):
			latched += 1
	_rest_grounded = grounded
	_rest_latched = latched


# --- (e): determinism ------------------------------------------------------------


func _phase_determinism() -> bool:
	if _det_pre < DET_PREAMBLE_TICKS:
		# Two extra states per run, outside the hashed window: one step after the
		# `respawn()` and one at the end of the preamble. They answer the question
		# a bare "diverged at tick N" cannot — whether the two runs ever started
		# from the same state at all, or whether `respawn()` is the thing that
		# does not reproduce.
		if _det_pre == 0:
			_kart_det.set_physics_process(true)
		if _det_pre == 0 or _det_pre == DET_PREAMBLE_TICKS - 1:
			_det_preamble[_det_run].append({
				"position": _kart_det.global_position,
				"velocity": _kart_det.linear_velocity,
				"spin": _kart_det.angular_velocity,
			})
		_det_pre += 1
		if _det_pre == DET_PREAMBLE_TICKS:
			_kart_det.set_input_driver(Callable(self, "_det_input"))
		return false

	# Hashed *before* this tick's forces are applied, so the sample at
	# `_det_tick == 0` is the settled state and every later one is the result of
	# an identical number of identical steps.
	if _det_tick % HASH_EVERY == 0:
		_det_hash.reset()
		_det_hash.add_transform(_kart_det.global_transform)
		_det_hash.add_vector3(_kart_det.linear_velocity)
		_det_hash.add_vector3(_kart_det.angular_velocity)
		_det_digests[_det_run].append(_det_hash.hex())
		if _det_states[_det_run].size() < 2:
			_det_states[_det_run].append({
				"position": _kart_det.global_position,
				"velocity": _kart_det.linear_velocity,
				"spin": _kart_det.angular_velocity,
			})

	_det_tick += 1
	if _det_tick < DET_TICKS:
		return false

	if _det_run == 0:
		_start_det_run(1)
		return false

	_park(_kart_det)
	_unpark(_kart_accel)
	_kart_accel.respawn()
	_advance_phase()
	return false


# --- (c2) and (d): the lever arm and the acceleration run ------------------------


func _phase_accel() -> bool:
	var kart := _kart_accel
	var speed := kart.linear_velocity.length()

	# (c2), taken on the fourth tick of the run: the clutch is engaged, the rear
	# tires are pushing hard, and the kart has barely moved, so the two substeps
	# have not yet diverged.
	if _phase_tick == 4:
		_lever_omega_before = kart.angular_velocity
		_lever_basis_before = kart.global_transform.basis
		_lever_com = kart.to_global(kart.center_of_mass)
		_lever_origin = kart.global_position
	elif _phase_tick == 5 and not _lever_done:
		_lever_measured = kart.angular_velocity - _lever_omega_before
		_lever_report = kart.wheel_report()
		_lever_done = true

	if _accel_first_motion_tick < 0 and speed > 0.05:
		_accel_first_motion_tick = _phase_tick
	if _accel_hundred_tick < 0:
		if speed >= HUNDRED_KMH:
			_accel_hundred_tick = _phase_tick
			# Linear interpolation between the two straddling ticks. The kart is
			# gaining about 0.05 m/s per tick here, so the tick grid alone is worth
			# 8 ms of uncertainty in a 4.5 s figure and interpolating removes it.
			var span := speed - _accel_hundred_prev
			var fraction := 0.0 if span <= 0.0 else (HUNDRED_KMH - _accel_hundred_prev) / span
			_accel_hundred_at = (float(_phase_tick) - 1.0 + fraction) * _delta
		else:
			_accel_hundred_prev = speed
	_accel_top_speed = maxf(_accel_top_speed, speed)

	_phase_tick += 1
	if _phase_tick < ACCEL_TICKS:
		return false

	_accel_end_speed = speed
	_accel_end_gear = kart.get_gear()
	_advance_phase()
	return false


# --- (f): cost -------------------------------------------------------------------


func _phase_cost() -> bool:
	# The kart coasts for both halves — throttle released, no steering — so the
	# only difference between them is whether `KartBody::_physics_process` runs at
	# all. Godot paces physics ticks off a wall clock, so a stopwatch around this
	# callback measures the pacing and not the work; `Performance` is the only
	# instrument that reports the time actually spent inside the physics
	# iteration.
	if _phase_tick == 0:
		_kart_accel.set_input_driver(Callable(self, "_idle_input"))

	# On for even ticks, off for odd ones, rather than one long block each. Two
	# adjacent ticks share the same thermal and scheduling conditions; two blocks
	# 2 s apart do not, and the first version of this measurement had a baseline
	# that drifted by more than the quantity it was trying to resolve.
	_cost_half = _phase_tick % 2
	_kart_accel.set_physics_process(_cost_half == 0)
	# This callback runs at the top of the physics iteration, before any node's
	# own `_physics_process` and before the server steps. `_process` below runs
	# after both. The span between them is the physics work, and it does NOT
	# include the frame limiter's sleep, which is what made a stopwatch around the
	# whole tick useless.
	_cost_opened = Time.get_ticks_usec()

	_phase_tick += 1
	if _phase_tick < COST_TICKS * 2:
		return false

	_report()
	quit(0)
	return true


# --- the report -------------------------------------------------------------------


func _report() -> void:
	_environment()
	_report_settle()
	_report_loads()
	_report_lever()
	_report_accel()
	_report_determinism()
	_report_cost()
	print("\n".join(_lines))


func _gravity() -> float:
	return float(ProjectSettings.get_setting("physics/3d/default_gravity", 9.8))


func _environment() -> void:
	# A fresh space's default solver iteration count is 8 under Jolt and 16 under
	# GodotPhysics3D — ADR-0030's behavioral assertion. Without it every number
	# below could be a measurement of the wrong engine.
	var space := PhysicsServer3D.space_create()
	var iterations := PhysicsServer3D.space_get_param(
		space, PhysicsServer3D.SPACE_PARAM_SOLVER_ITERATIONS
	)
	PhysicsServer3D.free_rid(space)

	var kart := _kart_settle
	_lines.append("=== environment =======================================================")
	_lines.append("")
	_lines.append("    engine                       %s" % Engine.get_version_info()["string"])
	_lines.append("    physics_engine setting       %s" % ProjectSettings.get_setting(
		"physics/3d/physics_engine", "(unset)"
	))
	_lines.append("    fresh space solver iters     %d   (Jolt 8, GodotPhysics3D 16)" % iterations)
	_lines.append("    physics_ticks_per_second     %d" % Engine.physics_ticks_per_second)
	_lines.append("    physics delta                %.9f s" % _delta)
	_lines.append("    gravity, project setting     %.6f m/s^2" % _gravity())
	_lines.append("    gravity, src/core/units.h    %.6f m/s^2" % G_CORE)
	_lines.append("")
	_lines.append("    The kart, as KartBody::_ready configured it from")
	_lines.append("    kz::kart_mass_properties(). No literal in kart_body.cpp produced any")
	_lines.append("    of these:")
	_lines.append("")
	_lines.append("    mass                         %.4f kg" % kart.mass)
	_lines.append("    center_of_mass               (%+.6f, %+.6f, %+.6f) m" % [
		kart.center_of_mass.x, kart.center_of_mass.y, kart.center_of_mass.z,
	])
	_lines.append("    inertia (diagonal)           (%.4f, %.4f, %.4f) kg m^2" % [
		kart.inertia.x, kart.inertia.y, kart.inertia.z,
	])
	_lines.append("    physics_material friction    %.4f   (convention 2: Jolt's own is off)" % (
		kart.physics_material_override.friction if kart.physics_material_override != null else NAN
	))
	_lines.append("    linear_damp / mode           %.4f / %d   (1 = DAMP_MODE_REPLACE)" % [
		kart.linear_damp, kart.linear_damp_mode,
	])
	_lines.append("    angular_damp / mode          %.4f / %d" % [
		kart.angular_damp, kart.angular_damp_mode,
	])
	_lines.append("    can_sleep                    %s" % kart.can_sleep)
	_lines.append("    steer lock at full input     %.6f rad (%.3f deg)" % [
		kart.get_steer_lock(), rad_to_deg(kart.get_steer_lock()),
	])
	_lines.append("    time_ratio                   %.6f   (telemetry only, never fed back)" % (
		kart.get_time_ratio()
	))


## The static deflection of one tire, meters. `suspension.h` defines `max_droop`
## as exactly this — the load divided by the rate — and `chassis_flex.h` sets
## `rest_length = free_radius - max_droop`, so the chassis origin's resting
## height is minus this and nothing else.
func _droop(front: bool) -> float:
	var g := _gravity()
	var share := STATIC_FRONT_SHARE if front else 1.0 - STATIC_FRONT_SHARE
	var rate := TIRE_RATE_FRONT if front else TIRE_RATE_REAR
	return _kart_settle.mass * g * share * 0.5 / rate


func _report_settle() -> void:
	var droop_front := _droop(true)
	var droop_rear := _droop(false)
	# The chassis origin sits midway between the axles (`chassis.h`), so its
	# height is the mean of the two axle heights, and each axle sits its own tire
	# deflection below the ground plane. Nose-down, because the front tire is a
	# third as stiff as the rear and carries only a fifth less load.
	var predicted_y := -(droop_front + droop_rear) * 0.5
	# Nose **down**, hence the negation: the front tire is a third as stiff as the
	# rear and carries only a fifth less load, so the front axle settles 1.6 mm
	# lower and the kart sits on its nose by that over the wheelbase.
	var predicted_pitch := -asin(clampf(
		(droop_front - droop_rear) / (REAR_AXLE_Z - FRONT_AXLE_Z), -1.0, 1.0
	))
	var mean_y := _rest_y_sum / maxf(float(_rest_samples), 1.0)
	var mean_pitch := _rest_pitch / maxf(float(_rest_samples), 1.0)

	_lines.append("")
	_lines.append("=== a. settle, dropped from 0.30 m ====================================")
	_lines.append("")
	_lines.append("    The chassis origin is on the ground by construction, so its resting")
	_lines.append("    height is exactly minus the static tire deflection: load over rate,")
	_lines.append("    per axle, averaged. Nothing is fitted.")
	_lines.append("")
	_lines.append("    static deflection, front     %.6f m   (%.2f N / %.0f N/m)" % [
		droop_front, _kart_settle.mass * _gravity() * STATIC_FRONT_SHARE * 0.5, TIRE_RATE_FRONT,
	])
	_lines.append("    static deflection, rear      %.6f m   (%.2f N / %.0f N/m)" % [
		droop_rear, _kart_settle.mass * _gravity() * (1.0 - STATIC_FRONT_SHARE) * 0.5,
		TIRE_RATE_REAR,
	])
	_lines.append("")
	_lines.append("    %-32s %14s %14s" % ["", "measured", "predicted"])
	_lines.append("    %-32s %14.6f %14.6f   residual %+.6f m" % [
		"origin height, mean over 1 s", mean_y, predicted_y, mean_y - predicted_y,
	])
	_lines.append("    %-32s %14.6f %14.6f   residual %+.6f rad" % [
		"pitch, nose up positive", mean_pitch, predicted_pitch, mean_pitch - predicted_pitch,
	])
	_lines.append("")
	_lines.append("    origin height, min / max     %.6f / %.6f m   (band %.6f m)" % [
		_rest_y_min, _rest_y_max, _rest_y_max - _rest_y_min,
	])
	_lines.append("    penetration below prediction %.6f m" % maxf(predicted_y - mean_y, 0.0))
	_lines.append("    residual speed, max over 1 s %.6f m/s" % _rest_speed_max)
	_lines.append("    residual spin, max over 1 s  %.6f rad/s" % _rest_omega_max)
	_lines.append("    wheels reporting contact     %d of 4     latched %d" % [
		_rest_grounded, _rest_latched,
	])
	_lines.append("")
	_lines.append("    Suspension travel per corner, positive compressed. The prediction is")
	_lines.append("    zero: `rest_length` is *defined* as the ray length at which the")
	_lines.append("    corner carries its static load, so a settled kart sits at zero travel")
	_lines.append("    by construction and any offset is the boundary's, not the solver's.")
	_lines.append("")
	var names := ["FL", "FR", "RL", "RR"]
	for corner in 4:
		_lines.append("      %-4s travel %+.6f m" % [
			names[corner], float(_rest_travel[corner]) / maxf(float(_rest_samples), 1.0),
		])


func _report_loads() -> void:
	var g := _gravity()
	var mass: float = _kart_settle.mass
	var predicted_front := mass * g * STATIC_FRONT_SHARE * 0.5
	var predicted_rear := mass * g * (1.0 - STATIC_FRONT_SHARE) * 0.5
	# The same two figures at `units.h`'s gravity, which is what
	# `kz::static_load_front_tire` returns and what every test in
	# `tests/core/` compares against. The project's 9.8 is 0.068% lower.
	var core_front := mass * G_CORE * STATIC_FRONT_SHARE * 0.5
	var core_rear := mass * G_CORE * (1.0 - STATIC_FRONT_SHARE) * 0.5

	var names := ["FL", "FR", "RL", "RR"]
	# Corner positions in the chassis frame, which is also this node's local frame.
	var corner_x := [-0.5525, 0.5525, -0.5925, 0.5925]
	var corner_z := [FRONT_AXLE_Z, FRONT_AXLE_Z, REAR_AXLE_Z, REAR_AXLE_Z]

	var load := [0.0, 0.0, 0.0, 0.0]
	var total := 0.0
	for corner in 4:
		load[corner] = float(_rest_load[corner]) / maxf(float(_rest_samples), 1.0)
		total += float(load[corner])

	_lines.append("")
	_lines.append("=== b. static corner loads ============================================")
	_lines.append("")
	_lines.append("    The per-corner columns are the AXLE prediction, which each corner")
	_lines.append("    only meets if the kart is laterally symmetric. It is not: the engine,")
	_lines.append("    exhaust and radiator are 27 kg hung outboard on the right, and")
	_lines.append("    `chassis.h` says in as many words that the resulting bias is not")
	_lines.append("    cosmetic. So the left/right residuals below are the MODEL, not error,")
	_lines.append("    and the two moment balances underneath are what actually checks them.")
	_lines.append("")
	_lines.append("    %-6s %14s %14s %14s" % ["", "measured", "m*g_project", "kz:: (g=9.80665)"])
	for corner in 4:
		var front := corner < 2
		_lines.append("    %-6s %14.4f %14.4f %14.4f   residual %+.4f N" % [
			names[corner], load[corner],
			predicted_front if front else predicted_rear,
			core_front if front else core_rear,
			float(load[corner]) - (predicted_front if front else predicted_rear),
		])
	_lines.append("")
	_lines.append("    %-6s %14.4f %14.4f %14.4f   residual %+.4f N" % [
		"sum", total, mass * g, mass * G_CORE, total - mass * g,
	])
	var front_axle := float(load[0]) + float(load[1])
	_lines.append("    %-6s %14.4f %14.4f   residual %+.4f N" % [
		"front", front_axle, mass * g * STATIC_FRONT_SHARE, front_axle - mass * g * STATIC_FRONT_SHARE,
	])
	var rear_axle := float(load[2]) + float(load[3])
	_lines.append("    %-6s %14.4f %14.4f   residual %+.4f N" % [
		"rear", rear_axle, mass * g * (1.0 - STATIC_FRONT_SHARE),
		rear_axle - mass * g * (1.0 - STATIC_FRONT_SHARE),
	])
	var front_share := 0.0
	if total > 0.0:
		front_share = front_axle / total
	_lines.append("    %-6s %14.4f %14.4f" % ["share", front_share, STATIC_FRONT_SHARE])

	# The two balances that make the four numbers a distribution rather than four
	# numbers. A kart at rest must carry its weight through contact patches whose
	# load-weighted centroid is the center of mass, in both axes at once —
	# nothing in the boundary is told this and it has to fall out.
	var roll_moment := 0.0
	var pitch_moment := 0.0
	for corner in 4:
		roll_moment += float(load[corner]) * float(corner_x[corner])
		pitch_moment += float(load[corner]) * float(corner_z[corner])
	var com: Vector3 = _kart_settle.center_of_mass

	_lines.append("")
	_lines.append("    %-34s %14s %14s" % ["load-weighted centroid", "measured", "center of mass"])
	_lines.append("    %-34s %14.6f %14.6f   residual %+.6f m" % [
		"lateral, m right of center", roll_moment / total, com.x, roll_moment / total - com.x,
	])
	_lines.append("    %-34s %14.6f %14.6f   residual %+.6f m" % [
		"longitudinal, m rearward", pitch_moment / total, com.z, pitch_moment / total - com.z,
	])
	_lines.append("")
	_lines.append("    The measured sum is the load the *suspension* is carrying. It must")
	_lines.append("    equal m*g at the project's gravity, not at units.h's, because Godot")
	_lines.append("    supplies the weight and src/core/ only supplies the spring.")


func _report_lever() -> void:
	_lines.append("")
	_lines.append("=== c. the lever arm — ADR-0033's regression test ======================")
	_lines.append("")
	_lines.append("    `VehicleForces::application_point` is an offset from the body ORIGIN,")
	_lines.append("    in world coordinates. `KartBody::_physics_process` passes it to")
	_lines.append("    `apply_force` untouched. Subtracting the center of mass — which this")
	_lines.append("    project believed correct for a whole milestone — adds a second torque")
	_lines.append("    (origin - com) x F to every corner, and because the kart's origin and")
	_lines.append("    its contact patches are both at ground level, that DOUBLES every")
	_lines.append("    pitch and roll moment at any center-of-mass height.")
	_lines.append("")
	_lines.append("    If someone reintroduces that subtraction, the two numbers below")
	_lines.append("    change: c1's residual spin stops being zero and grows without bound,")
	_lines.append("    and c2's measured pitch rate jumps to the 'com-subtracted' column.")

	# --- c1: the resting kart --------------------------------------------------
	#
	# At rest the four corner forces sum to the kart's weight and, with the
	# correct convention, produce no net torque about the center of mass — that is
	# what equilibrium means. With the center of mass subtracted the extra torque
	# is (origin - com) x (0, W, 0), which does not vanish: the center of mass is
	# 84 mm rearward and 41 mm right of the origin, so the kart would pitch and
	# roll off its own stand.
	var kart := _kart_settle
	var com: Vector3 = kart.to_global(kart.center_of_mass)
	var origin: Vector3 = kart.global_position
	var weight := Vector3(0.0, kart.mass * _gravity(), 0.0)
	var spurious := (origin - com).cross(weight)
	var spurious_alpha := _angular_response(kart.global_transform.basis, kart.inertia, spurious)

	_lines.append("")
	_lines.append("    c1. a kart standing still")
	_lines.append("")
	_lines.append("    com - origin                 (%+.6f, %+.6f, %+.6f) m" % [
		(com - origin).x, (com - origin).y, (com - origin).z,
	])
	_lines.append("    spurious torque if com were subtracted   (%+.3f, %+.3f, %+.3f) N m" % [
		spurious.x, spurious.y, spurious.z,
	])
	_lines.append("")
	_lines.append("    %-40s %14s" % ["", "rad/s per tick"])
	_lines.append("    %-40s %14.9f" % ["measured, max over the last 1 s", _rest_omega_max])
	_lines.append("    %-40s %14.9f" % ["predicted, offset from origin", 0.0])
	_lines.append("    %-40s %14.9f" % [
		"predicted, com subtracted", spurious_alpha.length() * _delta,
	])

	# --- c2: one tick under drive ----------------------------------------------
	_lines.append("")
	_lines.append("    c2. one tick of the acceleration run, reconstructed")
	_lines.append("")
	if not _lever_done:
		_lines.append("    NOT MEASURED — the acceleration phase did not reach tick 5.")
		return

	# Reconstruct what was applied from what the solver reported. The corner force
	# is the contact normal times the normal load, plus the tire force; the
	# application point is the contact patch. Both come out of `wheel_report()`.
	#
	# **The one honest caveat.** `wheel_report()` serves `WheelTelemetry`, which
	# the solver fills on its LAST substep, while the force it returns is the MEAN
	# over both substeps. On the fourth tick of a launch the two differ by well
	# under a percent, which is far below the factor of two the two conventions
	# are apart — but it is why the residual below is not zero to nine digits.
	var torque_origin := Vector3.ZERO
	var force_sum := Vector3.ZERO
	for corner in 4:
		var wheel: Dictionary = _lever_report[corner]
		var force: Vector3 = Vector3(wheel["normal"]) * float(wheel["load"]) + Vector3(wheel["force"])
		var patch: Vector3 = wheel["point"]
		torque_origin += (patch - _lever_com).cross(force)
		force_sum += force
	# What the wrong convention would have produced: every corner's arm lengthened
	# by (origin - com), which factors out of the sum.
	var torque_com := torque_origin + (_lever_origin - _lever_com).cross(force_sum)

	var predicted_origin := _angular_response(_lever_basis_before, _kart_accel.inertia, torque_origin) * _delta
	var predicted_com := _angular_response(_lever_basis_before, _kart_accel.inertia, torque_com) * _delta

	_lines.append("    corner forces, world sum     (%+.2f, %+.2f, %+.2f) N" % [
		force_sum.x, force_sum.y, force_sum.z,
	])
	_lines.append("    torque about com, from origin-relative points  (%+.3f, %+.3f, %+.3f) N m" % [
		torque_origin.x, torque_origin.y, torque_origin.z,
	])
	_lines.append("    the same with com subtracted                  (%+.3f, %+.3f, %+.3f) N m" % [
		torque_com.x, torque_com.y, torque_com.z,
	])
	_lines.append("")
	_lines.append("    %-34s %13s %13s %13s" % ["one tick, rad/s", "x (pitch)", "y (yaw)", "z (roll)"])
	_lines.append("    %-34s %13.9f %13.9f %13.9f" % [
		"measured", _lever_measured.x, _lever_measured.y, _lever_measured.z,
	])
	_lines.append("    %-34s %13.9f %13.9f %13.9f" % [
		"predicted, offset from origin", predicted_origin.x, predicted_origin.y,
		predicted_origin.z,
	])
	_lines.append("    %-34s %13.9f %13.9f %13.9f" % [
		"predicted, com subtracted", predicted_com.x, predicted_com.y, predicted_com.z,
	])
	_lines.append("")
	_lines.append("    residual against origin-relative  %.9f rad/s" % (
		(_lever_measured - predicted_origin).length()
	))
	_lines.append("    residual against com-subtracted   %.9f rad/s" % (
		(_lever_measured - predicted_com).length()
	))
	var ratio := 0.0
	if absf(predicted_origin.x) > 1e-12:
		ratio = predicted_com.x / predicted_origin.x
	_lines.append("    pitch rate, com-subtracted / correct   %.6f x" % ratio)


## `alpha = R I^-1 R^T tau`, the world angular response ADR-0032 finding 5
## measured to 4.4e-7 rad/s on a body deliberately rolled 45 degrees so the
## tensor could not be mistaken for diagonal. `RigidBody3D.inertia` is a Vector3,
## so the body-frame tensor is the diagonal and this is the whole of it.
func _angular_response(basis: Basis, inertia: Vector3, torque: Vector3) -> Vector3:
	var body_torque := basis.transposed() * torque
	var body_alpha := Vector3(
		body_torque.x / inertia.x, body_torque.y / inertia.y, body_torque.z / inertia.z
	)
	return basis * body_alpha


func _report_accel() -> void:
	var top_kmh := _accel_top_speed * 3.6
	var end_kmh := _accel_end_speed * 3.6

	_lines.append("")
	_lines.append("=== d. straight-line acceleration =====================================")
	_lines.append("")
	_lines.append("    Full throttle from rest with the assists on, %d ticks (%.1f s)." % [
		ACCEL_TICKS, float(ACCEL_TICKS) * _delta,
	])
	_lines.append("    The comparison is against ROADMAP M3b's SOLVER-ONLY figures, which")
	_lines.append("    were measured with no engine running at all. A large gap here is the")
	_lines.append("    boundary being wrong, not the solver.")
	_lines.append("")
	_lines.append("    %-30s %14s %14s" % ["", "measured", "solver only"])
	_lines.append("    %-30s %14.3f %14.3f   %+.3f km/h" % [
		"speed after the run, km/h", end_kmh, SOLVER_TOP_SPEED_KMH,
		end_kmh - SOLVER_TOP_SPEED_KMH,
	])
	_lines.append("    %-30s %14.3f %14.3f" % ["peak speed seen, km/h", top_kmh, SOLVER_TOP_SPEED_KMH])
	if _accel_hundred_tick >= 0:
		_lines.append("    %-30s %14.3f %14.3f   %+.3f s" % [
			"0-100 km/h, s", _accel_hundred_at, SOLVER_ZERO_TO_100_S,
			_accel_hundred_at - SOLVER_ZERO_TO_100_S,
		])
	else:
		_lines.append("    %-30s %14s" % ["0-100 km/h, s", "not reached"])
	_lines.append("")
	_lines.append("    first motion at tick         %d (%.3f s)" % [
		_accel_first_motion_tick, float(maxi(_accel_first_motion_tick, 0)) * _delta,
	])
	_lines.append("    gear at the end              %d" % _accel_end_gear)
	_lines.append("")
	_lines.append("    ROADMAP records the 0-100 figure as being outside its 3.0-3.5 s band")
	_lines.append("    and attributes it entirely to the auto-clutch launch: time from first")
	_lines.append("    motion is 3.90 s however the throttle is fed in. Issue #121.")
	if _accel_hundred_tick >= 0 and _accel_first_motion_tick >= 0:
		_lines.append("    from first motion, s         %.3f" % (
			_accel_hundred_at - float(_accel_first_motion_tick) * _delta
		))


func _report_determinism() -> void:
	var run_a: Array = _det_digests[0]
	var run_b: Array = _det_digests[1]
	var first_mismatch := -1
	var compared := mini(run_a.size(), run_b.size())
	for index in compared:
		if String(run_a[index]) != String(run_b[index]):
			first_mismatch = index
			break

	_lines.append("")
	_lines.append("=== e. determinism, two runs in one process ===========================")
	_lines.append("")
	_lines.append("    The same scripted input — full throttle and a steering sine, both")
	_lines.append("    pure functions of a tick counter — run twice, with `respawn()` and a")
	_lines.append("    %d-tick quiet preamble in between. Transform, linear velocity and" % (
		DET_PREAMBLE_TICKS
	))
	_lines.append("    angular velocity hashed every %d ticks with KartStateHash." % HASH_EVERY)
	_lines.append("")
	_lines.append("    The protocol has one non-obvious step in it, and it is a finding")
	_lines.append("    rather than a convenience: the kart's `_physics_process` is suppressed")
	_lines.append("    for the single tick the `respawn()` happens on. Without that, the two")
	_lines.append("    runs are NOT bit-identical one step later, and the signature says why")
	_lines.append("    — the velocity difference is 0.000000000 m/s while the spin difference")
	_lines.append("    is 2.1e-3 rad/s. Identical forces, different torque. Applying a force")
	_lines.append("    at an offset in the same tick a body is teleported resolves the lever")
	_lines.append("    arm against a center-of-mass position the teleport has not reached,")
	_lines.append("    and run 1 respawns a kart at rest while run 2 respawns one at 30 m/s.")
	_lines.append("    The transient decays to the engine's single-precision floor (2e-7 m)")
	_lines.append("    within the preamble, but it does not decay to zero, and 92 ticks later")
	_lines.append("    it had grown back through a hash quantum.")
	_lines.append("")
	_lines.append("    ticks per run                %d" % DET_TICKS)
	_lines.append("    samples compared             %d" % compared)
	if compared > 0:
		_lines.append("    run 1, last digest           %s" % run_a[compared - 1])
		_lines.append("    run 2, last digest           %s" % run_b[compared - 1])
	if first_mismatch < 0:
		_lines.append("    result                       IDENTICAL")
	else:
		_lines.append("    result                       DIVERGED at sample %d (tick %d)" % [
			first_mismatch, first_mismatch * HASH_EVERY,
		])
		_lines.append("      run 1  %s" % run_a[first_mismatch])
		_lines.append("      run 2  %s" % run_b[first_mismatch])
		_lines.append("")
		_lines.append("    Before the hashed window: one step after `respawn()`, and at the")
		_lines.append("    end of the quiet preamble. If the first line is already nonzero,")
		_lines.append("    the two runs never started from the same state and everything")
		_lines.append("    after it is amplification, not a solver that disagrees with itself.")
		var labels := ["one step after respawn ", "end of quiet preamble  "]
		for sample in mini(_det_preamble[0].size(), _det_preamble[1].size()):
			var pa: Dictionary = _det_preamble[0][sample]
			var pb: Dictionary = _det_preamble[1][sample]
			_lines.append("      %s d_position %.9f m  d_velocity %.9f m/s  d_spin %.9f rad/s" % [
				labels[sample],
				(Vector3(pa["position"]) - Vector3(pb["position"])).length(),
				(Vector3(pa["velocity"]) - Vector3(pb["velocity"])).length(),
				(Vector3(pa["spin"]) - Vector3(pb["spin"])).length(),
			])
		_lines.append("")
		_lines.append("    The first two hashed samples, in meters and m/s, so the size of")
		_lines.append("    the disagreement is readable rather than hashed away:")
		for sample in mini(_det_states[0].size(), _det_states[1].size()):
			var a: Dictionary = _det_states[0][sample]
			var b: Dictionary = _det_states[1][sample]
			_lines.append("      sample %d  d_position %.9f m  d_velocity %.9f m/s  d_spin %.9f rad/s" % [
				sample,
				(Vector3(a["position"]) - Vector3(b["position"])).length(),
				(Vector3(a["velocity"]) - Vector3(b["velocity"])).length(),
				(Vector3(a["spin"]) - Vector3(b["spin"])).length(),
			])


func _report_cost() -> void:
	var with_kart := float(_cost_sum[0]) / maxf(float(_cost_samples[0]), 1.0)
	var without := float(_cost_sum[1]) / maxf(float(_cost_samples[1]), 1.0)
	var difference := with_kart - without
	var difference_min := float(_cost_min[0]) - float(_cost_min[1])
	var ray_us := _measure_ray_cost()

	_lines.append("")
	_lines.append("=== f. cost ============================================================")
	_lines.append("")
	_lines.append("    The only wall-clock measurements in this file, and they gate nothing.")
	_lines.append("")
	_lines.append("    Godot paces physics ticks off a clock, so a stopwatch around a whole")
	_lines.append("    tick measures the pacing and not the work. What is timed instead is")
	_lines.append("    the span from the top of the physics iteration to the start of the")
	_lines.append("    following idle frame, which contains every node's `_physics_process`")
	_lines.append("    and the server step and contains none of the frame limiter's sleep.")
	_lines.append("    The kart is switched on for even ticks and off for odd ones over %d," % (
		COST_TICKS * 2
	))
	_lines.append("    coasting throughout, so the two halves share their conditions.")
	_lines.append("")
	_lines.append("    %-28s %12s %12s" % ["", "mean us", "min us"])
	_lines.append("    %-28s %12.1f %12.1f" % [
		"physics span, kart on", with_kart, float(_cost_min[0]),
	])
	_lines.append("    %-28s %12.1f %12.1f" % [
		"physics span, kart off", without, float(_cost_min[1]),
	])
	_lines.append("    %-28s %12.1f %12.1f" % ["KartBody's share", difference, difference_min])
	_lines.append("")
	_lines.append("    And the one component that can be attributed on its own, measured the")
	_lines.append("    way `contact_probe.gd` measures a raycast: the four suspension rays,")
	_lines.append("    cast from the kart's own mounts with its own RID excluded.")
	_lines.append("")
	_lines.append("    four suspension rays         %10.2f us/tick" % ray_us)
	_lines.append("    solver alone, ROADMAP M3b    %10.2f us/tick   (no engine running)" % (
		SOLVER_COST_US
	))
	_lines.append("    sum of the two known parts   %10.2f us/tick" % (ray_us + SOLVER_COST_US))
	_lines.append("")
	_lines.append("    ARCHITECTURE.md §15 budget   %10.1f us/frame  (the whole vehicle sim)" % (
		BUDGET_MS * 1000.0
	))
	_lines.append("    measured share of the budget %10.2f %%" % (
		difference / (BUDGET_MS * 1000.0) * 100.0
	))
	_lines.append("    known parts, share of budget %10.2f %%" % (
		(ray_us + SOLVER_COST_US) / (BUDGET_MS * 1000.0) * 100.0
	))
	_lines.append("")
	_lines.append("    time_ratio at the end        %.6f" % _kart_accel.get_time_ratio())
	_lines.append("")
	_lines.append("    That last number is telemetry and only telemetry — it is never fed to")
	_lines.append("    the solver, never scales a force and is never hashed. It reads below")
	_lines.append("    1.0 here for a reason that is a property of its definition rather than")
	_lines.append("    of the frame rate: it counts ticks in which `_physics_process` ran,")
	_lines.append("    against wall-clock since `_ready`, and this probe parks the kart for")
	_lines.append("    hundreds of ticks. A node that is frozen or disabled reads low")
	_lines.append("    forever, because the average has no window.")


## Four rays as `KartBody::query_ground` casts them, timed. There is no analytic
## answer for a duration; this is here because a per-tick figure that cannot be
## attributed to anything is not useful, and this is the half of it that can be.
func _measure_ray_cost() -> float:
	var space := get_root().get_world_3d().direct_space_state
	var kart := _kart_accel
	var queries: Array[PhysicsRayQueryParameters3D] = []
	var report := kart.wheel_report()
	for corner in 4:
		var wheel: Dictionary = report[corner]
		var lift: Vector3 = Vector3.UP * 0.060
		var from: Vector3 = Vector3(wheel["origin"]) + lift
		var to: Vector3 = from + Vector3(wheel["direction"]) * (0.060 + float(wheel["length"]))
		var query := PhysicsRayQueryParameters3D.create(from, to)
		query.exclude = [kart.get_rid()]
		queries.append(query)

	# Three passes, fastest reported. The slow passes are contaminated by
	# scheduling and the fast one is not — `contact_probe.gd` learned that the
	# hard way, with `exclude` looking 24% cheaper on one run and 86% dearer on
	# the next purely from which loop warmed the broadphase.
	var samples := 5000
	var best := INF
	for _pass in 3:
		var started := Time.get_ticks_usec()
		for _i in samples:
			for corner in 4:
				space.intersect_ray(queries[corner])
		best = minf(best, float(Time.get_ticks_usec() - started))
	return best / float(samples)
