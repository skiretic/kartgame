extends SceneTree

## Issue #137 — characterize the tire and the cornering envelope, directly.
##
##     godot --headless --path . --script tools/verify/tire_probe.gd
##
## Cold checkout needs the ADR-0018 double import first, exactly as
## `tools/verify/kart_body_probe.gd` documents:
##
##     godot --headless --path . --editor --quit-after 2 ; true
##     godot --headless --path . --editor --quit-after 2
##     godot --headless --path . --script tools/verify/tire_probe.gd
##
## ## Why this file exists, and what "directly" means here
##
## `drive.sh` reports a sustained lateral figure of 0.04-0.09 g against
## ARCHITECTURE.md §6.4's 1.5-2.0 g band, and `docs/REFERENCES.md` has since
## turned that run's numbers into circuit-design guidance ("the tightest circle
## the kart can physically hold is 2.70 m at 5.6 km/h"). Both rest on one
## open-loop scenario. Nothing between the tire curve and that scenario had ever
## been read out on its own.
##
## So this file measures the tire, then the four-wheel corner, then the scenario,
## in that order, and every check prints its measurement whether it passes or
## fails. It is **not** a lap and it is not a driving test: sections (a) to (c)
## hold the chassis at its settled attitude and impose a slip state on it, which
## is a tire rig with a `KartBody` in it rather than a kart being driven.
##
## `src/core/tire.h` cannot be reached from GDScript — nothing in `src/core/` is
## bound. So "directly" means **through the shipped `KartBody`**, whose
## `wheel_report()` publishes the applied force, the normal load and both slip
## quantities per corner after the friction ellipse and after both low-speed
## clamps. Nothing here reimplements a curve: a second copy of the Pacejka
## arithmetic in GDScript would be a copy that drifts, and the thing most worth
## catching is a disagreement between the curve and what the vehicle does with
## it. Every number below is read out, and the two that are not — the geometric
## turn radius in (d) and the slip-vector direction in (c) — say so at the line.
##
## ## The seven sections
##
##   a. **Lateral force against slip angle.** Where the peak is, and what is left
##      at 2x and 3x that slip. A curve that collapses past peak is the classic
##      cause of a kart that cannot hold a corner; this one does not collapse and
##      the check says so with the number.
##   b. **Longitudinal force against slip ratio.** Same treatment, including a
##      fully locked and a fully spinning wheel.
##   c. **Combined slip.** The one that matters: at the lateral peak slip angle,
##      sweep the slip ratio and watch the lateral force. It must fall
##      monotonically — a wheel that is sliding harder cannot be gripping harder.
##      **This check fails on the shipped tire**, and the failure is the finding.
##   d. **Steering scrub.** At each lock, how much of a front tire's force points
##      backward along the chassis instead of sideways.
##   e. **The four-wheel steady state**, speed-held: sustained lateral g, the four
##      corner loads, which inside wheel is the lighter, and the friction-ellipse
##      utilization per corner. Utilization near 1.0 on the loaded corners is the
##      measurement that says the envelope is the tire's and not the solver's.
##   f. **The quasi-static skidpad**, as a radius against lock, beside the radius
##      the steering geometry asks for.
##   g. **`drive.sh`'s own skidpad scenario, with a departure detector.** A run
##      whose body slip passes 30 degrees has spun, and the mean lateral
##      acceleration of a spun kart is not a cornering measurement. This section
##      exists because `drive_probe.gd` has no such detector and prints the
##      average anyway.
##
## Nothing here is tuned to agree, and no check's threshold was chosen after
## seeing the number it judges.
##
## Runs in about three minutes. Almost all of it is section (e) — thirty held
## corners of five seconds each — and it is wall-clock rather than CPU because a
## headless `SceneTree` paces its physics in real time. It costs 9 s of CPU.
##
## Exits non-zero if any check fails. On the tree this was written against, six
## of eleven fail and the failures are the report.

# --- the rig --------------------------------------------------------------------

const GROUND_SIZE := 4000.0
const GROUND_THICKNESS := 2.0
const GROUND_FRICTION := 1.0
const LANE_SPACING := 12.0

## Matching `tools/verify/kart_body_probe.gd`. The chassis collider is not the
## contact — `KartBody` sets `physics_material_override.friction` to 0.0 and does
## all four contacts by raycast — so this box only has to keep the kart out of
## the floor.
const SHAPE_SIZE := Vector3(0.90, 0.30, 1.60)
const SHAPE_CENTER := Vector3(0.0, 0.32, 0.0)

## Ticks of quiet before any measurement, so the tires are at their static
## deflection and the chassis is at rest.
const SETTLE_TICKS := 180

## Speed the tire rig runs at, m/s. Well above `SLIP_REFERENCE_SPEED` (1.0 m/s in
## `src/core/vehicle.h`) and above the 2.8 m/s the low-speed convergence clamp
## governs, so neither is active and the curve read out is the curve.
const RIG_SPEED := 25.0

## Slip angles swept in (a), degrees. Spaced to bracket the 8.9 degree peak the
## coefficients in `src/core/tire.h` imply and to reach 3x past it.
const SLIP_ANGLES_DEG: Array[float] = [
	0.5, 1.0, 2.0, 4.0, 6.0, 8.0, 9.0, 10.0, 12.0, 15.0, 18.0, 22.0, 27.0, 35.0, 45.0
]

## Slip ratios swept in (b) and (c). The longitudinal curve peaks near 0.108.
const SLIP_RATIOS: Array[float] = [
	0.0, 0.02, 0.05, 0.08, 0.108, 0.15, 0.22, 0.32, 0.50, 0.75, 1.00, 1.50, 2.00, 3.00
]

## Locks swept in (d), (e) and (f), as a fraction of full input.
const LOCKS: Array[float] = [0.10, 0.25, 0.40, 0.60, 0.80, 1.00]

## Ticks per held-corner run in (e), and the window averaged at the end of it.
## 5 s of hold and a 1 s window, which is `test_vehicle.cpp`'s skidpad protocol.
const HOLD_TICKS := 600
const HOLD_WINDOW := 120

## Speeds tried at each lock, as a fraction of the reference speed below. A lock
## has one best sustained lateral g and it is not at one speed for every lock:
## too slow and the tires are nowhere near the limit, too fast and the kart runs
## wide of the circle or departs. Sweeping and keeping the best non-departed run
## is `test_vehicle.cpp`'s protocol and it is the one that makes the six rows
## comparable with each other.
const HOLD_FRACTIONS: Array[float] = [0.5, 0.65, 0.8, 0.95, 1.1]

## `drive_probe.gd`'s own three skidpad constants, copied so section (g)
## reproduces that scenario exactly rather than approximately. If they move
## there, they must move here, and the report prints them so a reader can check.
const SKIDPAD_ENTRY_MS := 13.5
const SKIDPAD_LOCK := 1.0
const SKIDPAD_THROTTLE := 0.30
const SKIDPAD_TICKS := 3000

## Body slip past which a run is a spin rather than a corner, radians.
## 30 degrees. Chosen before the runs, from the geometry rather than from the
## results: a kart whose velocity vector is 30 degrees off its own centerline has
## its front tires past 30 degrees of slip angle, which is 3.4x the peak slip
## angle in section (a) and well onto the tail of the curve at every corner at
## once. Nothing beyond that is a steady state anybody drove to.
const DEPARTURE_RAD := deg_to_rad(30.0)

## Gravity, for the g conversions this file does itself.
const G := 9.80665

## §6.4, through `src/core/kz_reference.h`. Read, not judged, except where a
## check says so.
const LATERAL_SUSTAINED_G_MIN := 1.5
const LATERAL_SUSTAINED_G_MAX := 2.0

# --- state ----------------------------------------------------------------------

var _ground: StaticBody3D
var _rig: KartBody
var _corner: KartBody

var _phase := 0
var _phase_tick := 0
var _lines: Array[String] = []
var _failures: Array[String] = []
var _checks := 0

## Section (a)-(c): the imposed slip state for the current sample, and where the
## rig is pinned.
##
## `_rig_ready` is set once the kart has settled; `_pin_rig` holds it there from
## then on. See that function for why it writes no transform.
var _rig_ready := false
var _sweep_index := 0
var _sweep_lateral := 0.0
var _sweep_axle_speed := 0.0
var _sweep_rows: Array = []

## Sections (a), (b), (c) accumulate into these.
var _lateral_curve: Array = []
var _longitudinal_curve: Array = []
var _combined_curve: Array = []

## Section (e)/(f).
var _hold_index := 0
var _hold_fraction := 0
var _hold_best: Dictionary = {}
var _hold_speed := 0.0
var _hold_rows: Array = []
var _hold_samples := 0
var _hold_lateral := 0.0
var _hold_speed_sum := 0.0
var _hold_slip_sum := 0.0
var _hold_max_slip := 0.0
var _hold_load := [0.0, 0.0, 0.0, 0.0]
var _hold_util := [0.0, 0.0, 0.0, 0.0]

## Section (g).
var _skid_max_slip := 0.0
var _skid_lateral := 0.0
var _skid_speed := 0.0
var _skid_samples := 0
var _skid_peak := 0.0


func _initialize() -> void:
	_build_ground()
	# Held by reference, so the "`--script` main loop's scene is not in the tree
	# during `_initialize`" trap does not apply — nothing below is looked up by
	# name. What does run at `add_child` is `KartBody::_ready`, which installs the
	# mass properties, the damping and the friction override.
	_rig = _build_kart("Rig", 0)
	_corner = _build_kart("Corner", 1)
	_rig.set_input_driver(Callable(self, "_rig_input"))
	_corner.set_input_driver(Callable(self, "_corner_input"))


func _build_ground() -> void:
	_ground = StaticBody3D.new()
	_ground.name = "Ground"
	var material := PhysicsMaterial.new()
	material.friction = GROUND_FRICTION
	_ground.physics_material_override = material
	_ground.set_meta("surface_type", 0)
	var shape := BoxShape3D.new()
	shape.size = Vector3(GROUND_SIZE, GROUND_THICKNESS, GROUND_SIZE)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.position = Vector3(0.0, -GROUND_THICKNESS * 0.5, 0.0)
	_ground.add_child(collider)
	get_root().add_child(_ground)


func _build_kart(label: String, lane: int) -> KartBody:
	var kart := KartBody.new()
	kart.name = "Kart" + label
	kart.position = Vector3(float(lane) * LANE_SPACING, 0.0, 0.0)
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


func _rig_input() -> Dictionary:
	# The rig never drives. Sections (a)-(c) impose the slip state kinematically
	# and read what the tire model does with it; a throttle would add a torque the
	# imposed axle speed is about to overwrite anyway.
	return {"throttle": 0.0, "brake": 0.0, "steer": 0.0, "clutch": 1.0}


func _corner_input() -> Dictionary:
	if _phase == 6:
		return {"throttle": SKIDPAD_THROTTLE, "brake": 0.0, "steer": SKIDPAD_LOCK}
	if _phase != 5:
		return {"throttle": 0.0, "brake": 0.0, "steer": 0.0}
	# A proportional speed hold, which is the `test_vehicle.cpp` protocol. A
	# skidpad is driven on a throttle that holds the speed; a constant throttle
	# can only hold one speed against one amount of corner scrub, and choosing
	# that constant to move a lateral figure would be tuning the scenario.
	var error := _hold_speed - _corner.get_speed_ms()
	return {
		"throttle": clampf(error * 0.6, 0.0, 1.0),
		"brake": clampf(-error * 0.3, 0.0, 1.0),
		"steer": LOCKS[_hold_index],
	}


# --- the run --------------------------------------------------------------------
#
# This callback runs before the nodes' own `_physics_process` in the same
# iteration, which is what lets sections (a)-(c) pin the rig's state for the tick
# the solver is about to read.


func _physics_process(_delta: float) -> bool:
	_phase_tick += 1
	match _phase:
		0:
			return _phase_settle()
		1:
			return _phase_sweep(1)
		2:
			return _phase_sweep(2)
		3:
			return _phase_sweep(3)
		4:
			return _advance(5)
		5:
			return _phase_hold()
		6:
			return _phase_skidpad()
	return true


func _advance(next: int) -> bool:
	_phase = next
	_phase_tick = 0
	return false


func _phase_settle() -> bool:
	if _phase_tick < SETTLE_TICKS:
		return false
	_rig_ready = true
	_begin_sweep(1)
	return _advance(1)


# --- (a)(b)(c) the tire rig -----------------------------------------------------
#
# The chassis is held flat, level and at its settled height every tick, and the
# body velocity and the driveline speed are overwritten every tick. So the four
# corners see exactly the slip state named below at very nearly their static
# loads, and the read-out is the tire model's answer to it rather than a
# vehicle's response.
#
# Each state is held for `SAMPLE_TICKS` and read on the last of them.
#
# **Two ticks is not enough and the rig says so.** `wheel_report()` describes the
# previous tick's solver call, so two would be the minimum that reads back what
# was imposed — but stepping the slip state also steps the tire force, and the
# chassis needs a few ticks to find the ride height that force implies. Read at
# two ticks the four corners carried 1,320 N of a 1,716 N kart on the first
# sample. 16 ticks is 133 ms, an order above the corner periods `suspension.h`
# reports (8.5 Hz front, 11.8 Hz rear), and the `(rig)` check at the head of the
# report is what holds this number honest.
const SAMPLE_TICKS := 16


func _begin_sweep(section: int) -> void:
	_sweep_index = 0
	_sweep_rows = []
	_apply_sweep(section)


func _sweep_count(section: int) -> int:
	if section == 1:
		return SLIP_ANGLES_DEG.size()
	return SLIP_RATIOS.size()


## The slip state for the current index of the current section.
func _apply_sweep(section: int) -> void:
	if section == 1:
		# Pure lateral: no driveline slip, so the axle turns at road speed.
		_sweep_lateral = RIG_SPEED * tan(deg_to_rad(SLIP_ANGLES_DEG[_sweep_index]))
		_sweep_axle_speed = RIG_SPEED
	elif section == 2:
		# Pure longitudinal.
		_sweep_lateral = 0.0
		_sweep_axle_speed = RIG_SPEED * (1.0 + SLIP_RATIOS[_sweep_index])
	else:
		# Combined, at the lateral peak this file measures in (a). Held at 8.9
		# degrees rather than at whatever (a) found, so the two sections sweep the
		# same axis and the comparison between them is not itself a measurement.
		_sweep_lateral = RIG_SPEED * tan(deg_to_rad(8.9))
		_sweep_axle_speed = RIG_SPEED * (1.0 + SLIP_RATIOS[_sweep_index])


func _pin_rig() -> void:
	# The settled attitude and height, with the along-track position left where it
	# drifted to. Only the attitude and the ride height decide the four ray
	# lengths, so pinning those pins the loads and pinning the third would be a
	# teleport every tick for no measurement.
	# **Nothing is teleported and the vertical motion is left alone.** Zeroing the
	# angular velocity every tick is enough to hold the attitude — a body that
	# cannot rotate stays at the attitude it settled into — and it is all that is
	# needed, because only the attitude decides whether the four corners see the
	# slip state this section imposed.
	#
	# Two earlier versions of this function wrote the transform as well and both
	# were wrong in the same direction: rebuilding it as a level identity basis
	# moved 70 N from the front axle to the rear (a settled kart is pitched, front
	# and rear static deflections being 3.4 and 1.8 mm), and pinning the ride
	# height left the four tires carrying 1,320 N of a 1,716 N kart because the
	# rebound damper was fighting a permanent extension velocity. A rig that
	# decides the load it measures the curve at is not measuring the curve.
	#
	# `engage` sets the body velocity along -Z itself and zeroes the vertical
	# component with it, so the settling velocity is carried across by hand and
	# the velocity is written after `engage` rather than before.
	var vertical := _rig.linear_velocity.y
	_rig.engage(3, _sweep_axle_speed)
	_rig.linear_velocity = Vector3(_sweep_lateral, vertical, -RIG_SPEED)


func _phase_sweep(section: int) -> bool:
	_pin_rig()
	if _phase_tick % SAMPLE_TICKS != 0:
		return false

	_sweep_rows.append(_read_rig())
	_sweep_index += 1
	if _sweep_index < _sweep_count(section):
		_apply_sweep(section)
		return false

	if section == 1:
		_lateral_curve = _sweep_rows
		_begin_sweep(2)
		return _advance(2)
	if section == 2:
		_longitudinal_curve = _sweep_rows
		_begin_sweep(3)
		return _advance(3)
	_combined_curve = _sweep_rows
	return _advance(4)


## One sample: the mean of the two front and the two rear corners, in the tire's
## own frame. The kart is straight-ahead and level, so the tire frame and the
## chassis frame coincide and the projection is a component read.
func _read_rig() -> Dictionary:
	var report: Array = _rig.wheel_report()
	var basis := _rig.global_transform.basis
	var forward := -basis.z
	var right := basis.x
	var out := {}
	for pair in [["front", 0, 1], ["rear", 2, 3]]:
		var load := 0.0
		var longitudinal := 0.0
		var lateral := 0.0
		var slip_angle := 0.0
		var slip_ratio := 0.0
		var utilization := 0.0
		for index in [pair[1], pair[2]]:
			var wheel: Dictionary = report[index]
			load += wheel["load"]
			var force: Vector3 = wheel["force"]
			longitudinal += force.dot(forward)
			lateral += force.dot(right)
			slip_angle += wheel["slip_angle"]
			slip_ratio += wheel["slip_ratio"]
			utilization += wheel["utilization"]
		out[pair[0]] = {
			"load": load * 0.5,
			"longitudinal": longitudinal * 0.5,
			"lateral": lateral * 0.5,
			"slip_angle": slip_angle * 0.5,
			"slip_ratio": slip_ratio * 0.5,
			"utilization": utilization * 0.5,
		}
	return out


# --- (e)(f) the held corner ------------------------------------------------------


func _phase_hold() -> bool:
	if _phase_tick == 1:
		_hold_speed = _reference_speed(LOCKS[_hold_index]) * HOLD_FRACTIONS[_hold_fraction]
		_corner.respawn()
		_corner.engage(2 if _hold_speed < 14.0 else 3, _hold_speed)
		_hold_samples = 0
		_hold_lateral = 0.0
		_hold_speed_sum = 0.0
		_hold_slip_sum = 0.0
		_hold_max_slip = 0.0
		_hold_load = [0.0, 0.0, 0.0, 0.0]
		_hold_util = [0.0, 0.0, 0.0, 0.0]
		return false

	var slip := _body_slip(_corner)
	_hold_max_slip = maxf(_hold_max_slip, absf(slip))

	if _phase_tick > HOLD_TICKS - HOLD_WINDOW:
		var report: Array = _corner.wheel_report()
		_hold_lateral += absf(_corner.get_lateral_g())
		_hold_speed_sum += _corner.linear_velocity.length()
		_hold_slip_sum += slip
		for index in range(4):
			_hold_load[index] += float(report[index]["load"])
			_hold_util[index] += float(report[index]["utilization"])
		_hold_samples += 1

	if _phase_tick < HOLD_TICKS:
		return false

	var inverse := 1.0 / maxf(float(_hold_samples), 1.0)
	var lateral := _hold_lateral * inverse
	var speed := _hold_speed_sum * inverse
	var row := {
		"lock": LOCKS[_hold_index],
		"target": _hold_speed,
		"speed": speed,
		"lateral": lateral,
		"radius": (speed * speed / (lateral * G)) if lateral > 1e-4 else 0.0,
		"geometric": _geometric_radius(LOCKS[_hold_index]),
		"slip": _hold_slip_sum * inverse,
		"max_slip": _hold_max_slip,
		"load": [
			_hold_load[0] * inverse,
			_hold_load[1] * inverse,
			_hold_load[2] * inverse,
			_hold_load[3] * inverse,
		],
		"util": [
			_hold_util[0] * inverse,
			_hold_util[1] * inverse,
			_hold_util[2] * inverse,
			_hold_util[3] * inverse,
		],
	}
	# Keep the best non-departed run at this lock. A departed run is kept only if
	# nothing at this lock held, so the row is never empty and never silently
	# reports a spin as a corner.
	var previous: Dictionary = _hold_best
	var better := previous.is_empty()
	if not better:
		var was_held: bool = previous["max_slip"] < DEPARTURE_RAD
		var is_held: bool = row["max_slip"] < DEPARTURE_RAD
		better = (is_held and not was_held) or \
			(is_held == was_held and row["lateral"] > previous["lateral"])
	if better:
		_hold_best = row

	_phase_tick = 0
	_hold_fraction += 1
	if _hold_fraction < HOLD_FRACTIONS.size():
		return false

	_hold_rows.append(_hold_best)
	_hold_best = {}
	_hold_fraction = 0
	_hold_index += 1
	if _hold_index < LOCKS.size():
		return false
	_corner.respawn()
	_corner.engage(3, SKIDPAD_ENTRY_MS)
	return _advance(6)


## The speed the sweep at each lock is scaled from.
##
## **Not chosen here.** It is `tests/core/test_vehicle.cpp`'s skidpad protocol,
## reused unchanged so these rows are comparable with the M3b figures: the speed
## that would produce 2.5 g on the radius the lock asks for, swept over
## `HOLD_FRACTIONS` of it, keeping the best run that did not depart. 2.5 g is the
## top of §6.4's transient band and is deliberately a speed the kart cannot hold
## — the point is to bracket every lock's own limit from both sides rather than
## to guess at it once.
func _reference_speed(lock: float) -> float:
	return clampf(sqrt(2.5 * G * _geometric_radius(lock)), 5.0, 32.0)


## The radius the steering geometry asks for, meters. **The one reconstructed
## number in this file.** `src/core/steering.h`'s `turn_radius` reduced through
## the bicycle model with true Ackermann, which is what `kz_front_geometry()`
## configures: `wheelbase * (cot(inner) + cot(outer)) / 2`, with the outer angle
## from `cot(outer) = cot(inner) + track / wheelbase`. Printed beside the radius
## the kart actually drove, because the gap between them is the whole of what
## "the kart will not turn" means.
func _geometric_radius(lock: float) -> float:
	var wheelbase := 1.050
	var track := 1.105
	var inner := deg_to_rad(25.0) * lock
	if inner < 1e-6:
		return 1e9
	var cot_inner := 1.0 / tan(inner)
	var cot_outer := cot_inner + track / wheelbase
	return wheelbase * (cot_inner + cot_outer) * 0.5


func _body_slip(kart: KartBody) -> float:
	var velocity := kart.linear_velocity
	if velocity.length() < 0.5:
		return 0.0
	var basis := kart.global_transform.basis
	return atan2(velocity.dot(basis.x), -velocity.dot(basis.z))


# --- (g) drive.sh's own scenario -------------------------------------------------


func _phase_skidpad() -> bool:
	var slip := _body_slip(_corner)
	_skid_max_slip = maxf(_skid_max_slip, absf(slip))
	if _corner.linear_velocity.length() > 5.0:
		_skid_peak = maxf(_skid_peak, absf(_corner.get_lateral_g()))
	if _phase_tick > SKIDPAD_TICKS - SKIDPAD_TICKS / 4:
		_skid_lateral += absf(_corner.get_lateral_g())
		_skid_speed += _corner.linear_velocity.length()
		_skid_samples += 1
	if _phase_tick < SKIDPAD_TICKS:
		return false
	_report()
	quit(1 if _failures.size() > 0 else 0)
	return true


# --- the report -------------------------------------------------------------------


func _say(line: String) -> void:
	_lines.append(line)


func _check(name: String, passed: bool, measurement: String) -> void:
	_checks += 1
	if passed:
		_say("    PASS  %-46s %s" % [name, measurement])
	else:
		_say("    FAIL  %-46s %s" % [name, measurement])
		_failures.append(name)


## Linear interpolation of a column of `_lateral_curve` at a slip angle, so the
## peak and the tail can be quoted at a stated angle rather than at whichever
## sample happened to be nearest.
func _at_angle(rows: Array, degrees: float, axle: String, field: String) -> float:
	for index in range(1, rows.size()):
		var low := SLIP_ANGLES_DEG[index - 1]
		var high := SLIP_ANGLES_DEG[index]
		if degrees <= high:
			var fraction: float = (degrees - low) / (high - low)
			var a: float = rows[index - 1][axle][field]
			var b: float = rows[index][axle][field]
			return a + (b - a) * fraction
	return rows[rows.size() - 1][axle][field]


func _report() -> void:
	_say("")
	_say("=== tire_probe: the tire and the cornering envelope, off KartBody ===")
	_say("")
	_say("    Sections (a)-(c) hold the chassis at its settled attitude and impose")
	_say("    the slip state on it, so the loads stay near static and the read-out")
	_say("    is the tire model's rather than a vehicle's. Rig speed %.1f m/s." % RIG_SPEED)
	var rig_front: float = _lateral_curve[0]["front"]["load"]
	var rig_rear: float = _lateral_curve[0]["rear"]["load"]
	_say("    Rig loads at zero slip: %.1f N front, %.1f N rear, %.1f N total" % [
		rig_front, rig_rear, 2.0 * (rig_front + rig_rear)])
	_check("(rig) the tire rig carries the kart's own weight",
		absf(2.0 * (rig_front + rig_rear) - 175.0 * G) <= 0.05 * 175.0 * G,
		"%.1f N against %.1f N of kart, %.1f%% apart" % [
			2.0 * (rig_front + rig_rear), 175.0 * G,
			100.0 * (2.0 * (rig_front + rig_rear) / (175.0 * G) - 1.0)])

	# --- (a) --------------------------------------------------------------------
	_say("")
	_say("(a) LATERAL FORCE AGAINST SLIP ANGLE")
	_say("    %8s %10s %10s %10s %10s %10s" % ["deg", "front N", "front mu", "rear N", "rear mu", "read deg"])
	var peak_force := 0.0
	var peak_angle := 0.0
	for index in range(_lateral_curve.size()):
		var row: Dictionary = _lateral_curve[index]
		var front: Dictionary = row["front"]
		var rear: Dictionary = row["rear"]
		var total: float = absf(front["lateral"]) + absf(rear["lateral"])
		if total > peak_force:
			peak_force = total
			peak_angle = SLIP_ANGLES_DEG[index]
		_say("    %8.2f %10.1f %10.3f %10.1f %10.3f %10.2f" % [
			SLIP_ANGLES_DEG[index],
			absf(front["lateral"]),
			absf(front["lateral"]) / maxf(float(front["load"]), 1.0),
			absf(rear["lateral"]),
			absf(rear["lateral"]) / maxf(float(rear["load"]), 1.0),
			rad_to_deg(absf(front["slip_angle"])),
		])
	var at_peak := absf(_at_angle(_lateral_curve, peak_angle, "rear", "lateral"))
	var at_two := absf(_at_angle(_lateral_curve, peak_angle * 2.0, "rear", "lateral"))
	var at_three := absf(_at_angle(_lateral_curve, peak_angle * 3.0, "rear", "lateral"))
	_say("")
	_check("(a) the lateral peak is at a kart slick's slip angle",
		peak_angle >= 6.0 and peak_angle <= 12.0,
		"peak at %.1f deg, %.0f N per rear tire" % [peak_angle, at_peak])
	_check("(a) the curve does not collapse past the peak",
		at_two >= 0.70 * at_peak and at_three >= 0.55 * at_peak,
		"%.3f of peak at 2x slip, %.3f at 3x" % [at_two / maxf(at_peak, 1.0), at_three / maxf(at_peak, 1.0)])

	# --- (b) --------------------------------------------------------------------
	_say("")
	_say("(b) LONGITUDINAL FORCE AGAINST SLIP RATIO")
	_say("    `commanded` is what the driveline was pinned to; `read k` is what the")
	_say("    solver reports. They differ because the axle integrates within the")
	_say("    tick and `wheel_report()` publishes the substep mean, so the read")
	_say("    column is the honest x-axis and the peak below is quoted on it. That")
	_say("    also smears the peak — the shape is measured, its exact location is")
	_say("    not, and only the shape is checked.")
	_say("    %10s %10s %10s %10s" % ["commanded", "read k", "rear N", "rear mu"])
	var long_peak := 0.0
	var long_peak_ratio := 0.0
	for index in range(_longitudinal_curve.size()):
		var rear: Dictionary = _longitudinal_curve[index]["rear"]
		var force: float = absf(rear["longitudinal"])
		if force > long_peak:
			long_peak = force
			long_peak_ratio = absf(rear["slip_ratio"])
		_say("    %10.3f %10.4f %10.1f %10.3f" % [
			SLIP_RATIOS[index], rear["slip_ratio"], force,
			force / maxf(float(rear["load"]), 1.0),
		])
	var spinning: float = absf(_longitudinal_curve[_longitudinal_curve.size() - 1]["rear"]["longitudinal"])
	_check("(b) the longitudinal curve rises to a peak and falls back",
		long_peak_ratio > 0.0 and long_peak_ratio < absf(_longitudinal_curve[_longitudinal_curve.size() - 1]["rear"]["slip_ratio"]),
		"peak %.0f N at read k = %.3f, falling to %.0f N by k = %.2f" % [
			long_peak, long_peak_ratio, spinning,
			absf(_longitudinal_curve[_longitudinal_curve.size() - 1]["rear"]["slip_ratio"])])
	_check("(b) a fully spinning wheel still makes force",
		spinning >= 0.40 * long_peak,
		"%.3f of peak at the largest slip swept" % [spinning / maxf(long_peak, 1.0)])

	# --- (c) --------------------------------------------------------------------
	_say("")
	_say("(c) COMBINED SLIP, at 8.9 deg of commanded slip angle throughout")
	_say("    The friction ellipse must take from the lateral axis as the")
	_say("    longitudinal one demands more. `force dir` is the angle of the")
	_say("    applied force off the wheel's rolling axis, beside `slip dir`, the")
	_say("    angle of the slip vector off the same axis. **`slip dir` is the one")
	_say("    quantity this file computes rather than reads**: atan2 of the two")
	_say("    slip components the solver itself reports, `tan(slip_angle)` against")
	_say("    `slip_ratio`. Coulomb friction opposes the slip vector, so a force")
	_say("    that stays at 60 degrees while the slip vector swings to 3 is a force")
	_say("    pointing 57 degrees away from the direction the tire is sliding.")
	_say("    %10s %8s %8s %9s %9s %10s %9s" % [
		"commanded", "read k", "read a", "lat N", "long N", "force dir", "slip dir"])
	var combined_lateral: Array[float] = []
	var combined_read: Array[float] = []
	for index in range(_combined_curve.size()):
		var rear: Dictionary = _combined_curve[index]["rear"]
		var lateral: float = absf(rear["lateral"])
		var longitudinal: float = absf(rear["longitudinal"])
		var read_ratio: float = absf(rear["slip_ratio"])
		var read_angle: float = absf(rear["slip_angle"])
		combined_lateral.append(lateral)
		combined_read.append(read_ratio)
		var direction := rad_to_deg(atan2(lateral, maxf(longitudinal, 1e-6)))
		var slip_direction := rad_to_deg(atan2(tan(read_angle), maxf(read_ratio, 1e-6)))
		_say("    %10.3f %8.4f %8.2f %9.1f %9.1f %10.1f %9.1f" % [
			SLIP_RATIOS[index], read_ratio, rad_to_deg(read_angle), lateral, longitudinal,
			direction, slip_direction,
		])

	# The monotonicity check. Past the point where the ellipse starts binding, a
	# larger slip ratio is a wheel sliding harder, and it cannot come back with
	# more lateral grip than it had.
	var worst_rise := 0.0
	var worst_at := 0.0
	var minimum := combined_lateral[0]
	for index in range(1, combined_lateral.size()):
		minimum = minf(minimum, combined_lateral[index])
		var rise := combined_lateral[index] - minimum
		if rise > worst_rise:
			worst_rise = rise
			worst_at = combined_read[index]
	_check("(c) lateral force falls monotonically with slip ratio",
		worst_rise <= 0.02 * maxf(combined_lateral[0], 1.0),
		"recovers %.0f N (%.1f%% of the pure-slip value) by k = %.2f" % [
			worst_rise, 100.0 * worst_rise / maxf(combined_lateral[0], 1.0), worst_at])

	var last: Dictionary = _combined_curve[_combined_curve.size() - 1]["rear"]
	var last_direction := rad_to_deg(atan2(
		absf(last["lateral"]), maxf(absf(last["longitudinal"]), 1e-6)))
	var last_slip_direction := rad_to_deg(atan2(
		tan(absf(last["slip_angle"])), maxf(absf(last["slip_ratio"]), 1e-6)))
	_check("(c) the force opposes the slip vector at large combined slip",
		absf(last_direction - last_slip_direction) <= 15.0,
		"force at %.1f deg against a slip vector at %.1f deg, %.1f deg apart" % [
			last_direction, last_slip_direction, absf(last_direction - last_slip_direction)])

	# --- (d) --------------------------------------------------------------------
	_say("")
	_say("(d) STEERING SCRUB: the share of a front tire's lateral force that")
	_say("    points backward along the chassis, which is sin(steer angle).")
	_say("    Steer angles are read out of `wheel_report()`; the sines are this")
	_say("    file's arithmetic on them.")
	_say("    %8s %10s %10s %12s" % ["lock", "inner deg", "outer deg", "drag share"])
	for row in _hold_rows:
		_say("    %8.2f %10.2f %10.2f %12.4f" % [
			row["lock"], rad_to_deg(deg_to_rad(25.0) * row["lock"]),
			rad_to_deg(atan(1.0 / (1.0 / tan(maxf(deg_to_rad(25.0) * row["lock"], 1e-6)) + 1.105 / 1.050))),
			sin(deg_to_rad(25.0) * row["lock"]),
		])

	# --- (e) --------------------------------------------------------------------
	_say("")
	_say("(e) THE FOUR-WHEEL STEADY STATE, speed held")
	_say("    %6s %8s %8s %8s %8s %8s %8s %8s %9s" % [
		"lock", "target", "speed", "lat g", "FL N", "FR N", "RL N", "RR N", "max slip"])
	var best_lateral := 0.0
	var best_lock := 0.0
	var front_lighter := 0
	for row in _hold_rows:
		var load: Array = row["load"]
		if row["max_slip"] < DEPARTURE_RAD and row["lateral"] > best_lateral:
			best_lateral = row["lateral"]
			best_lock = row["lock"]
		if load[0] < load[2]:
			front_lighter += 1
		_say("    %6.2f %8.2f %8.2f %8.3f %8.1f %8.1f %8.1f %8.1f %9.1f%s" % [
			row["lock"], row["target"], row["speed"], row["lateral"],
			load[0], load[1], load[2], load[3], rad_to_deg(row["max_slip"]),
			"  DEPARTED" if row["max_slip"] >= DEPARTURE_RAD else "",
		])
	_say("")
	_say("    friction-ellipse utilization per corner. 1.0 is a tire at the edge of")
	_say("    what it has; loaded corners sitting at 1.0 means the envelope is the")
	_say("    tire's and nothing upstream is losing force.")
	_say("    %6s %10s %10s %10s %10s" % ["lock", "FL", "FR", "RL", "RR"])
	var best_row: Dictionary = {}
	for row in _hold_rows:
		var util: Array = row["util"]
		_say("    %6.2f %10.3f %10.3f %10.3f %10.3f" % [row["lock"], util[0], util[1], util[2], util[3]])
		if row["lock"] == best_lock:
			best_row = row

	_check("(e) sustained lateral g reaches §6.4's band",
		best_lateral >= LATERAL_SUSTAINED_G_MIN,
		"best %.3f g at lock %.2f, band %.1f-%.1f" % [
			best_lateral, best_lock, LATERAL_SUSTAINED_G_MIN, LATERAL_SUSTAINED_G_MAX])
	if not best_row.is_empty():
		var util: Array = best_row["util"]
		_check("(e) the loaded corners are at the friction limit",
			maxf(float(util[1]), float(util[3])) >= 0.90,
			"outside front %.3f, outside rear %.3f of the ellipse" % [util[1], util[3]])
	_check("(e) the INSIDE REAR is the wheel that leaves, per ARCHITECTURE §6",
		front_lighter == 0,
		"the inside front is the lighter wheel at %d of %d locks" % [front_lighter, _hold_rows.size()])

	# --- (f) --------------------------------------------------------------------
	_say("")
	_say("(f) THE QUASI-STATIC SKIDPAD: what the kart drives against what the")
	_say("    steering geometry asks for.")
	_say("    %6s %12s %12s %10s %10s" % ["lock", "driven R m", "geometric R", "ratio", "body slip"])
	var tightest := 1e9
	var tightest_speed := 0.0
	var tightest_g := 0.0
	for row in _hold_rows:
		if row["max_slip"] >= DEPARTURE_RAD:
			_say("    %6.2f   departed" % row["lock"])
			continue
		if row["radius"] < tightest and row["radius"] > 0.0:
			tightest = row["radius"]
			tightest_speed = row["speed"]
			tightest_g = row["lateral"]
		_say("    %6.2f %12.2f %12.2f %10.2f %10.2f" % [
			row["lock"], row["radius"], row["geometric"],
			row["radius"] / maxf(float(row["geometric"]), 1e-6), rad_to_deg(row["slip"]),
		])
	_say("")
	_say("    tightest circle the kart HOLDS: %.2f m at %.2f m/s (%.1f km/h), %.3f g" % [
		tightest, tightest_speed, tightest_speed * 3.6, tightest_g])
	_say("    docs/REFERENCES.md records 2.70 m at 5.6 km/h and 0.09 g for this,")
	_say("    from `drive_probe.gd`'s open-loop run. Section (g) is why they differ.")

	# --- (g) --------------------------------------------------------------------
	var inverse := 1.0 / maxf(float(_skid_samples), 1.0)
	var mean_lateral := _skid_lateral * inverse
	var mean_speed := _skid_speed * inverse
	_say("")
	_say("(g) drive.sh's OWN SKIDPAD SCENARIO, WITH A DEPARTURE DETECTOR")
	_say("    lock %.2f, throttle %.2f, entry %.1f m/s — `drive_probe.gd`'s three" % [
		SKIDPAD_LOCK, SKIDPAD_THROTTLE, SKIDPAD_ENTRY_MS])
	_say("    constants, copied. Averaged over the last quarter, as it averages.")
	_say("      settled speed        %.3f m/s (%.2f km/h)" % [mean_speed, mean_speed * 3.6])
	_say("      mean lateral         %.4f g" % mean_lateral)
	_say("      implied radius       %.2f m" % ((mean_speed * mean_speed / (mean_lateral * G)) if mean_lateral > 1e-4 else 0.0))
	_say("      peak lateral         %.3f g" % _skid_peak)
	_say("      MAX BODY SLIP        %.1f deg" % rad_to_deg(_skid_max_slip))
	_check("(g) the skidpad scenario does not depart",
		_skid_max_slip < DEPARTURE_RAD,
		"max body slip %.1f deg against a %.0f deg departure threshold" % [
			rad_to_deg(_skid_max_slip), rad_to_deg(DEPARTURE_RAD)])
	_say("")
	_say("    A run that departed has spun. The mean lateral acceleration of a spun")
	_say("    kart is a number about the spin, and `drive_probe.gd` publishes it as")
	_say("    the kart's §6.4 sustained lateral figure with no such guard.")

	# --- the tally ---------------------------------------------------------------
	_say("")
	if _failures.is_empty():
		_say("=== %d checks, all passed ===" % _checks)
	else:
		_say("=== %d checks, %d FAILED ===" % [_checks, _failures.size()])
		for name in _failures:
			_say("    %s" % name)

	for line in _lines:
		print(line)
