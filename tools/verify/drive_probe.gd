extends SceneTree

## ROADMAP M3a's acceptance harness: drive the kart with scripted input, measure
## what it does, and hash what it did.
##
##     tools/verify/drive.sh
##     godot --headless --path . --script tools/verify/drive_probe.gd -- --ticks=1800
##
## Three things are being answered, and only the third is about numbers looking
## reasonable:
##
##   1. **Does the integration boundary work at all** — a force-driven body, a
##      120 Hz `_physics_process`, and Jolt's stepping, which ARCHITECTURE.md §19
##      names as the least-charted part of the plan.
##   2. **Is it deterministic** — the same scenario run twice must produce the
##      same state hash, tick for tick. §8 item 6. This is the check the whole
##      replay and ghost feature rests on, and it is cheap to run now and
##      expensive to retrofit later.
##   3. **Is it in the right country** — 0-100 km/h, top speed, braking and
##      lateral g against the §6.4 KZ reference figures, which the extension
##      publishes through `KartCore.kz_reference()` so this file cannot hold its
##      own copy of them.
##
## It runs headless. There are no pixels involved, so the ADR-0018 rendering
## crash does not apply — but the *import* still does, which is why `drive.sh`
## imports twice before calling this.
##
## ## Why the scenarios are open-loop
##
## Each scenario is a fixed function from tick number to driver input. No
## feedback, no controller, no seeking a target speed. That is deliberate: a
## closed-loop scenario measures the controller as much as the kart, and two runs
## of a controller that reads its own noisy state are not guaranteed to agree.
## An open-loop input stream is reproducible by construction, which is what makes
## the hash comparison meaningful.

const SCENE_PATH := "res://scenes/game/proving_ground.tscn"

## Physics ticks per scenario. 1,200 at 120 Hz is 10 s — long enough for a kart
## to reach terminal velocity down the straight with room to spare.
const DEFAULT_TICKS := 1200

## How often the state hash absorbs a sample. Every tick would be honest and slow;
## §8 says "every N ticks" for the same reason. 4 at 120 Hz is 30 Hz, which is
## still far finer than the timescale any real divergence takes to grow.
const HASH_INTERVAL := 4

var _kart: KartDebugVehicle
var _root: Node
var _hash: KartStateHash

var _scenario := "accel"
var _ticks := DEFAULT_TICKS
var _tick := 0

## Ticks ignored at the start of a run. The kart is spawned above the ground and
## dropped, so the first fraction of a second is a landing rather than a
## measurement — without this, every scenario reported "0 of 4 wheels down" and
## the number described the spawn instead of the driving.
const SETTLE_TICKS := 90

## Speed below which a longitudinal or lateral spike is not reported as a peak.
## At walking pace one tick of full brake is a legitimate 12 g — the clamp in the
## drive model stops exactly one tick of momentum, by design — and reporting that
## as the kart's braking figure is arithmetic describing the timestep rather than
## the kart.
const PEAK_SPEED_GATE := 5.0

## The window the sustained figures are averaged over, as a fraction of the run
## measured from its end. §6.4's lateral and braking numbers are sustained
## quantities, not spikes: a skidpad figure is what the kart holds once it has
## settled into the circle.
const STEADY_FRACTION := 0.25

## Measurements, filled as the run goes.
var _time_to_100 := -1.0
var _top_speed := 0.0
var _peak_lateral_g := 0.0
var _peak_braking_g := 0.0
var _steady_lateral_sum := 0.0
var _steady_lateral_ticks := 0
var _brake_start_speed := -1.0
var _brake_start_tick := 0
var _brake_mean_g := 0.0
var _max_roll_deg := 0.0
var _distance := 0.0
var _previous_velocity := Vector3.ZERO
var _previous_position := Vector3.ZERO
var _wheels_down_min := 4


func _initialize() -> void:
	var args := Cmdline.parse()
	_scenario = Cmdline.as_string(args, "scenario", "accel")
	_ticks = Cmdline.as_int(args, "ticks", DEFAULT_TICKS)

	if not ClassDB.class_exists("KartStateHash"):
		printerr("KartStateHash is not registered — build the extension: scons target=editor")
		quit(1)
		return

	_hash = KartStateHash.new()

	var packed: PackedScene = load(SCENE_PATH)
	if packed == null:
		printerr("could not load " + SCENE_PATH)
		quit(1)
		return

	# The HUD and both camera rigs are switched off. They read nothing the sim
	# writes, but they do allocate, and a probe that renders nothing has no reason
	# to build a canvas layer.
	_root = packed.instantiate()
	get_root().add_child(_root)

	print("scenario %s, %d ticks at %d Hz" % [
		_scenario, _ticks, Engine.physics_ticks_per_second,
	])


## The kart, found on the first tick rather than in `_initialize`.
##
## A `--script` main loop adds its scene before the tree starts running, so the
## node is parented but not yet *inside* the tree and its `_ready` has not run —
## there are no children to find. Everything the scene builds exists by the first
## physics tick, which is where the lookup belongs. `shoot.gd` has the same shape
## for the same reason.
func _attach() -> bool:
	_kart = _root.find_child("Kart", false, false) as KartDebugVehicle
	if _kart == null:
		printerr("no Kart in the scene — is assets/generated/kart.glb built?")
		return false
	_kart.input_driver = _scripted_input
	_previous_position = _kart.global_position
	_previous_velocity = _kart.linear_velocity
	return true


## The scenarios, as pure functions of the tick counter.
##
## Times are in ticks rather than seconds so that nothing here reads a clock, and
## so the same script produces the same run at a different tick rate only if the
## tick rate is deliberately changed — which is a change the hash should notice.
func _scripted_input() -> Dictionary:
	var second := float(_tick) / float(Engine.physics_ticks_per_second)
	match _scenario:
		"accel":
			# Full throttle from rest, straight, for the whole run.
			return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
		"brake":
			# Accelerate for 6 s, then stand on the brake with the throttle shut.
			if second < 6.0:
				return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
			return {"throttle": 0.0, "brake": 1.0, "steer": 0.0}
		"skidpad":
			# Accelerate for 2 s to about 55 km/h, then hold full lock on a
			# trickle of throttle. The kart settles into whatever radius it can
			# hold at whatever speed it can hold it — the measurement, rather than
			# a radius chosen in advance and a controller that hunts for it.
			#
			# The entry speed matters and was wrong at first. Full lock at 100 km/h
			# is not a skidpad, it is a spin: a 15 m circle at 2.2 g only supports
			# 18 m/s, so the kart arrives 40 km/h too fast, and with its center of
			# mass at 0.25 m over a 1.185 m rear track it rolled over instead of
			# sliding. A driver would not do that, so neither does the scenario.
			if second < 2.0:
				return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
			return {"throttle": 0.30, "brake": 0.0, "steer": 1.0}
		_:
			return {"throttle": 0.0, "brake": 0.0, "steer": 0.0}


func _physics_process(delta: float) -> bool:
	if _kart == null and not _attach():
		return true

	_sample(delta)
	_tick += 1
	if _tick >= _ticks:
		_report()
		return true
	return false


## One tick's worth of measurement and hashing.
##
## The hash absorbs the body's transform and both velocities — the complete rigid
## state, which is what a re-simulation has to reproduce. The measurements are
## derived from the same values rather than from a second source, so a
## measurement and a hash can never describe different ticks.
func _sample(delta: float) -> void:
	var velocity := _kart.linear_velocity
	var speed := velocity.length()
	var position := _kart.global_position

	if _tick % HASH_INTERVAL == 0:
		_hash.add_int(_tick)
		_hash.add_transform(_kart.global_transform)
		_hash.add_vector3(velocity)
		_hash.add_vector3(_kart.angular_velocity)

	_distance += position.distance_to(_previous_position)
	_previous_position = position
	_top_speed = maxf(_top_speed, speed)
	if _tick >= SETTLE_TICKS:
		_wheels_down_min = mini(_wheels_down_min, _kart.wheels_on_ground)
	# Roll, so that a kart which tipped over is reported as having tipped over
	# rather than as having pulled a spectacular lateral g on its way past
	# vertical. Measured from the body's up axis against the world's, which stays
	# meaningful past 90 degrees where an Euler angle does not.
	var up := _kart.global_transform.basis.y
	_max_roll_deg = maxf(_max_roll_deg, rad_to_deg(up.angle_to(Vector3.UP)))

	if _time_to_100 < 0.0 and speed >= KartCore.kmh_to_ms(100.0):
		_time_to_100 = float(_tick) / float(Engine.physics_ticks_per_second)

	# Acceleration by difference, because Godot exposes no accelerometer. The
	# first tick has no previous velocity to difference against, so it is skipped
	# rather than reported as an infinite spike.
	if _tick > SETTLE_TICKS:
		var acceleration := (velocity - _previous_velocity) / delta
		var basis := _kart.global_transform.basis
		var lateral_g := absf(acceleration.dot(basis.x)) / 9.80665
		if speed > PEAK_SPEED_GATE:
			_peak_lateral_g = maxf(_peak_lateral_g, lateral_g)
		if _tick >= int(float(_ticks) * (1.0 - STEADY_FRACTION)):
			_steady_lateral_sum += lateral_g
			_steady_lateral_ticks += 1

		# Braking only counts when the kart is going forwards and slowing down;
		# otherwise the launch would register as a large negative and the number
		# would describe the wrong end of the run.
		var forward := -basis.z
		var along := acceleration.dot(forward)
		var forward_speed := velocity.dot(forward)
		if forward_speed > PEAK_SPEED_GATE and along < 0.0:
			_peak_braking_g = maxf(_peak_braking_g, -along / 9.80665)

		# The braking figure that means anything is the average over a stop, not
		# the worst tick in it: mean deceleration from 90 km/h down to 20 km/h,
		# which is how a road test quotes it and how §6.4's 1.5-2.0 g should be
		# read. Timed by the two threshold crossings rather than by the brake
		# input, so it measures the kart and not the scenario script.
		if _kart.brake_input > 0.0 and forward_speed > 1.0:
			if _brake_start_speed < 0.0 and forward_speed <= KartCore.kmh_to_ms(90.0):
				_brake_start_speed = forward_speed
				_brake_start_tick = _tick
			elif _brake_start_speed > 0.0 and _brake_mean_g == 0.0 \
					and forward_speed <= KartCore.kmh_to_ms(20.0):
				var elapsed := float(_tick - _brake_start_tick) / float(Engine.physics_ticks_per_second)
				if elapsed > 0.0:
					_brake_mean_g = (_brake_start_speed - forward_speed) / elapsed / 9.80665
	_previous_velocity = velocity


func _report() -> void:
	var kz: Dictionary = KartCore.kz_reference()
	var lines: Array[String] = []
	lines.append("--- %s: %d ticks, %.2f s" % [
		_scenario, _tick, float(_tick) / float(Engine.physics_ticks_per_second),
	])
	lines.append("    distance        %8.1f m" % _distance)
	lines.append("    top speed       %8.1f km/h   (KZ %.0f-%.0f)" % [
		KartCore.ms_to_kmh(_top_speed), kz["top_speed_min_kmh"], kz["top_speed_max_kmh"],
	])
	if _time_to_100 >= 0.0:
		lines.append("    0-100 km/h      %8.2f s     (KZ %.1f-%.1f)" % [
			_time_to_100, kz["zero_to_100_kmh_min_s"], kz["zero_to_100_kmh_max_s"],
		])
	else:
		lines.append("    0-100 km/h        not reached")
	var steady := 0.0
	if _steady_lateral_ticks > 0:
		steady = _steady_lateral_sum / float(_steady_lateral_ticks)
	lines.append("    lateral         %8.2f g sustained, %.2f peak   (KZ %.1f-%.1f)" % [
		steady, _peak_lateral_g, kz["lateral_g_min"], kz["lateral_g_max"],
	])
	if _brake_mean_g > 0.0:
		lines.append("    braking 90-20   %8.2f g mean, %.2f peak      (KZ %.1f-%.1f)" % [
			_brake_mean_g, _peak_braking_g, kz["braking_g_min"], kz["braking_g_max"],
		])
	else:
		lines.append("    braking 90-20        not measured in this scenario")
	lines.append("    wheels down     %8d minimum after settling" % _wheels_down_min)
	# A kart past about 50 degrees of roll is on its side, and every other number
	# in the report describes a kart that is on its side. Reported rather than
	# inferred, because a tipped kart still produces plausible-looking g figures.
	lines.append("    max roll        %8.1f deg%s" % [
		_max_roll_deg, "   TIPPED OVER" if _max_roll_deg > 50.0 else "",
	])
	# The hash is the line `drive.sh` compares between two runs. Printed last and
	# on its own, so a shell can grep for it without parsing the rest.
	lines.append("state-hash %s" % _hash.hex())
	print("\n".join(lines))
	quit(0)
