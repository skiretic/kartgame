extends SceneTree

## The driving acceptance harness: drive the kart with scripted input, measure
## what it does, and hash what it did.
##
## Written for M3a's GDScript stand-in and now pointed at M3b's `KartBody`, which
## is the C++ solver on a Jolt body. Nothing in the harness changed shape to
## follow it — the vehicle is found by name, driven through the same `input_driver`
## Callable, and read through the same property names — which is the useful part
## of the migration: the whole vehicle model was replaced underneath a measurement
## rig that did not have to be rewritten to notice.
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

var _kart: KartBody
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

## Entry speed for the skidpad, m/s. 13.5 m/s is 48.6 km/h.
##
## Set from a sweep rather than from an estimate, and the sweep is the reason the
## number is not the 15.3 m/s a 15 m circle at 2.2 g would support. **The table
## below was measured on the M3a stand-in vehicle, which no longer exists**, and
## is kept because it is the record of why this constant is what it is, not
## because it describes the current kart. It has deliberately not been re-swept
## against `KartBody`: re-choosing an entry speed to improve a lateral figure
## would be tuning the scenario to hit a number, which is the one thing this
## harness must not do. One run per entry speed, on the deleted vehicle:
##
##     entry m/s   sustained g   peak g   max roll
##       8.0          1.84        2.14      1.6 deg
##      10.0          1.84        2.24      1.7 deg
##      12.0          1.84        2.27      1.7 deg
##      13.5          1.84        2.30      1.7 deg
##      15.3          1.84       43.85    167.0 deg   TIPPED OVER
##
## The *sustained* figure is independent of entry speed, which is the sign that
## the kart really is settling into the same steady circle every time and that
## 1.84 g is a property of the vehicle rather than of the scenario. What entry
## speed changes is the **transient** as lock is wound on, and above 13.5 m/s
## that transient exceeds what the kart can stand up to and it rolls over.
##
## The transient is partly the scenario's own fault: `STEER_RATE` takes the input
## from zero to full lock in 0.29 s, which is faster than a driver turns into a
## corner, so the kart is asked for peak lateral before it has settled. Winding
## lock on over a second would let the entry speed go back up. That is worth
## doing when there is a vehicle worth measuring at the limit — M3b — rather than
## against a stand-in that is deleted at that milestone.
##
## Overridable with `--entry=` so the lateral limit can be found by sweeping it
## rather than by guessing at it once.
const SKIDPAD_ENTRY_MS := 13.5

## Steering input held through the skidpad, 0 to 1, where 1 is full lock.
##
## The default is 1.0, which is what the scenario has always used and is **left
## alone deliberately** — changing a scenario's input until a figure improves is
## tuning, whatever it is called. What it is now is an argument, for the same
## reason `--entry=` is one: the lateral limit is found by sweeping a lever and
## reading the result, not by picking a value once and defending it.
##
## It is worth knowing what full lock means to this vehicle, because it changed
## when the vehicle did. M3a's stand-in scaled its lock down with speed
## (`STEER_SPEED_FALLOFF`), an explicit driver aid, so "full lock" there was a
## moderate angle at 48 km/h. `steering.h` has no such aid and must not — the real
## kart's high-speed stability is emergent from caster and Ackermann — so full
## lock now genuinely is full lock, and full lock at 48 km/h is a spin rather than
## a skidpad. Sweep it before reading the sustained figure below.
const SKIDPAD_LOCK := 1.0

## Throttle held through the skidpad. Also unchanged, and also now a lever.
##
## A real skidpad is driven on a throttle that holds the speed constant, which is
## a closed loop and is the one thing this file will not have — a scenario that
## reads its own state to decide what to do measures the controller as much as the
## kart. So the throttle is a constant, and a constant throttle can only hold one
## speed against one amount of corner scrub.
##
## That is not a detail, it is the binding constraint on the number this scenario
## exists to produce. Measured against `KartBody`, on the 15 m marked circle's own
## entry speed, one run per cell:
##
##     lock   throttle   sustained g   settled radius   at
##     0.25     0.30        0.88 g        18.99 m      46.0 km/h
##     0.25     0.60        1.59 g        32.64 m      81.3 km/h
##     0.25     1.00        1.84 g        58.49 m     116.9 km/h
##     0.40     1.00        1.82 g        50.95 m     108.5 km/h
##     0.60     1.00        1.02 g         3.49 m      21.2 km/h
##     1.00     0.30        0.09 g         2.70 m       5.6 km/h   <- the default
##
## Two things fall out of that table and both are about the scenario rather than
## about the kart. At 1.84 g the kart reproduces ROADMAP M3b's solver-only 1.86 g
## to within 1%, so the boundary is not losing grip anywhere. And the default row
## is not a cornering measurement at all: at full lock the kart scrubs to walking
## pace and the "sustained" figure describes a kart that has nearly stopped.
##
## Left at 0.30 rather than raised, because raising a scenario's throttle until
## its lateral figure enters a band is tuning to hit a number however it is
## justified. What the lever is for is making the row above reproducible from a
## command instead of from this comment.
const SKIDPAD_THROTTLE := 0.30

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
## Speed over the same window, so the settled circle's radius can be reported.
## `v^2 / a` is the whole of it, and a radius is the one number that says at a
## glance whether the kart is on a skidpad or spinning on the spot.
var _steady_speed_sum := 0.0
var _brake_start_speed := -1.0
var _brake_start_tick := 0
var _brake_mean_g := 0.0
var _max_roll_deg := 0.0
var _distance := 0.0

## Peak wheel lift per corner, meters, FL FR RL RR. Issue #32's acceptance and
## `ARCHITECTURE.md` §6's defining kart behavior: with no differential, the inside
## rear leaving the ground *is* the differential. It is measured here rather than
## being read off the telemetry panel by eye, because "the inside rear lifts" is a
## claim with a number attached and the number belongs in the same report as every
## other §6.4 figure.
var _peak_lift := [0.0, 0.0, 0.0, 0.0]
var _previous_velocity := Vector3.ZERO
var _previous_position := Vector3.ZERO

## Three counts, not one, because they are three different questions. Issue #136
## was filed on this report printing "wheels down 3 minimum" beside "peak lift
## 0.0 mm" on all four corners, with nothing on the page to say how both could be
## true at once.
##
##   * `_wheels_down_min` counts wheels **carrying load** (`grounded`), which is
##     what the solver zeroes a corner's force from.
##   * `_wheels_touching_min` counts wheels **on the road** (`tire_contact`),
##     loaded or not. The gap between the two is the rebound damper cancelling the
##     spring on a tire that never left the ground — measured at 98.2 mm/s of
##     extension on the front corner, which is real damper behavior and not a
##     defect.
##   * `_peak_lift` is meters of daylight, and is now a **lower bound** rather
##     than zero when the ray missed entirely.
##
## With all three printed there is no wheel state that has no reading, which is
## what #136 was actually about.
var _wheels_down_min := 4
var _wheels_touching_min := 4

## Set once the skidpad's entry speed is reached; see `_scripted_input`. Latched
## rather than recomputed, so a kart that scrubs speed off in the corner does not
## drop back below the threshold and start accelerating again mid-measurement.
var _skidpad_entered := false
var _skidpad_entry_tick := -1
var _entry_ms := SKIDPAD_ENTRY_MS
var _lock := SKIDPAD_LOCK
var _corner_throttle := SKIDPAD_THROTTLE


func _initialize() -> void:
	var args := Cmdline.parse()
	_scenario = Cmdline.as_string(args, "scenario", "accel")
	_ticks = Cmdline.as_int(args, "ticks", DEFAULT_TICKS)
	_entry_ms = Cmdline.as_float(args, "entry", SKIDPAD_ENTRY_MS)
	_lock = clampf(Cmdline.as_float(args, "lock", SKIDPAD_LOCK), 0.0, 1.0)
	_corner_throttle = clampf(
		Cmdline.as_float(args, "corner-throttle", SKIDPAD_THROTTLE), 0.0, 1.0)

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
	_kart = _root.find_child("Kart", false, false) as KartBody
	if _kart == null:
		printerr("no Kart in the scene — is assets/generated/kart.glb built?")
		return false
	# **The §6.4 gate, and it belongs here rather than in the report.** Every
	# figure this probe prints is quoted in ROADMAP M3b as a reference against a
	# real KZ. A figure measured under a preset is not one, and the failure is
	# silent in the worst way — the numbers still look plausible, they just
	# describe a kart nobody built. ADR-0037 keeps the tuning out of `state-hash`
	# for good reasons, so the tuning has to be checked separately, and this is
	# where.
	#
	# Before `input_driver` is set, so a tuned scene aborts at tick 0 rather than
	# after 1,200 ticks of simulation whose every number is inadmissible.
	#
	# **A scene with no registry passes.** It is running the compiled-in defaults
	# by construction, which is exactly the state the gate wants, and requiring
	# the node would make `drive.sh` depend on something it does not need. Found
	# by group rather than by name because that is how the registry publishes
	# itself — `KartTuning::source_group()`.
	var registries := _root.get_tree().get_nodes_in_group("tuning_source")
	for node in registries:
		if not node.has_method("is_at_defaults") or node.is_at_defaults():
			continue
		printerr("the scene is tuned: %d changed, %d defended overridden, hash %s"
			% [node.changed_count(), node.defended_override_count(), node.tuning_hash_hex()])
		printerr("a §6.4 figure measured under a preset is not a reference figure.")
		printerr("run tools/verify/tuning.sh to see what moved.")
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
		"skidpad", "skidpad_right":
			# Accelerate to SKIDPAD_ENTRY_MS, then hold `SKIDPAD_LOCK` on
			# `SKIDPAD_THROTTLE`. The kart settles into whatever radius it can hold
			# at whatever speed it can hold it — the measurement, rather than a
			# radius chosen in advance and a controller that hunts for it.
			#
			# **Against `KartBody` it does not settle into a circle at all.** Both
			# constants' headers carry the measured sweep: at full lock the kart
			# scrubs to 5.6 km/h on a 2.7 m radius, so what the sustained figure
			# below describes is a kart that has nearly stopped. The vehicle is not
			# the problem — the same scenario at lock 0.25 and full throttle holds
			# 1.84 g against the solver's own 1.86 g. The scenario is, and it is
			# left alone here rather than adjusted, because adjusting it until the
			# figure lands in a band is tuning whatever else it is called.
			#
			# The entry speed matters and has now been wrong twice. Full lock at
			# 100 km/h is not a skidpad, it is a spin: a 15 m circle at 2.2 g only
			# supports 18 m/s, so the kart arrives 40 km/h too fast and rolls over
			# instead of sliding. A driver would not do that, so neither does the
			# scenario.
			#
			# **Gated on speed rather than on a tick count**, which is the second
			# mistake. It was "accelerate for 2.0 s", and 2.0 s is not a speed — it
			# is a speed only for one particular set of drive constants. When
			# `_force_at_contact` was corrected to measure its offset from the body
			# origin, the kart's acceleration changed, 2.0 s stopped meaning 55 km/h,
			# and the scenario tipped the kart over and reported that as its
			# cornering figure. A threshold crossing is still a pure function of a
			# deterministic simulation, so the state hash is unaffected; what it is
			# not is a *controller*, which is the thing the header comment rules out.
			var steer := -_lock if _scenario == "skidpad_right" else _lock
			if not _skidpad_entered:
				return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
			return {"throttle": _corner_throttle, "brake": 0.0, "steer": steer}
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

	# Latched here rather than inside `_scripted_input`, because this runs before
	# node propagation and therefore before the kart reads its input for the tick.
	if not _skidpad_entered and speed >= _entry_ms:
		_skidpad_entered = true
		_skidpad_entry_tick = _tick

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
		# After settling only: the kart is spawned above the ground and dropped, so
		# the first fraction of a second reports four wheels a long way off the
		# ground and that is the spawn rather than any behavior of the kart.
		var wheels: Array = _kart.telemetry().get("wheel", [])
		var touching := 0
		for corner in wheels.size():
			var wheel: Dictionary = wheels[corner]
			var lift := float(wheel.get("lift", 0.0))
			if corner < _peak_lift.size():
				_peak_lift[corner] = maxf(float(_peak_lift[corner]), lift)
			# Defaulted from `lift` rather than to false, so an extension built
			# before `tire_contact` was published still reports something true
			# instead of four wheels in the air: "no daylight under the tire" is
			# what contact means on every path except a tick the wheel left
			# part-way through.
			if bool(wheel.get("tire_contact", lift <= 0.0)):
				touching += 1
		_wheels_touching_min = mini(_wheels_touching_min, touching)
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
			_steady_speed_sum += speed
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
	# Two rows against two bands, because they are two different quantities and
	# this file printed one of them against the other's band for two milestones.
	#
	# ADR-0034. The skidpad settles into a circle and holds it, so what it measures
	# is *sustained*, and a sustained figure may only be judged against the
	# sustained band — whose ceiling is 18% below this kart's own 2.4336 g rollover
	# threshold, because nothing sustains more lateral acceleration than it tips
	# at. The peak below is a transient and is shown against the transient band,
	# which is allowed to sit above that threshold precisely because tipping takes
	# time. Both pairs come from `kz_reference.h` through the extension; neither is
	# restated here, and the ambiguous `lateral_g_*` keys they replaced were
	# removed rather than aliased so that a caller which had not been updated would
	# fail loudly instead of quietly reading the wrong band.
	lines.append("    lateral sust    %8.2f g            (KZ %.1f-%.1f sustained)" % [
		steady, kz["lateral_sustained_g_min"], kz["lateral_sustained_g_max"],
	])
	lines.append("    lateral peak    %8.2f g            (KZ %.1f-%.1f transient)" % [
		_peak_lateral_g, kz["lateral_peak_g_min"], kz["lateral_peak_g_max"],
	])
	if _brake_mean_g > 0.0:
		# Issue #131 is the same peak-versus-mean confusion in the braking row and
		# it is **not resolved here**: §6.4's 1.5-2.0 g has never been labeled
		# either way, so there is no honest band to compare a mean against yet. The
		# measurement is untouched and both numbers are printed; which of them the
		# band describes is stated as open rather than assumed, because assuming it
		# is what put the lateral row wrong for two milestones.
		lines.append("    braking 90-20   %8.2f g mean, %.2f peak      (KZ %.1f-%.1f, #131: the" % [
			_brake_mean_g, _peak_braking_g, kz["braking_g_min"], kz["braking_g_max"],
		])
		lines.append("                                                     band is unlabeled)")
	else:
		lines.append("    braking 90-20        not measured in this scenario")
	# Only for the scenarios these two lines describe. `accel` and `brake` also
	# cross the entry threshold on their way up the straight, and a "settled
	# circle" printed for a kart driving in a straight line is a number that reads
	# as a measurement and is not one.
	if _scenario.begins_with("skidpad") and _skidpad_entry_tick >= 0:
		lines.append("    entry speed     %8.1f km/h at %.2f s   lock %.2f" % [
			KartCore.ms_to_kmh(_entry_ms),
			float(_skidpad_entry_tick) / float(Engine.physics_ticks_per_second),
			_lock,
		])
		# The settled circle, stated as a radius. `v^2 / a` over the same window
		# the sustained figure is averaged on, so the two describe one state. It is
		# the number that tells a skidpad from a spin at a glance: the marked circle
		# in the scene is 15 m and it is drawn at true size.
		var steady_speed := 0.0
		if _steady_lateral_ticks > 0:
			steady_speed = _steady_speed_sum / float(_steady_lateral_ticks)
		var radius := 0.0
		if steady > 0.001:
			radius = steady_speed * steady_speed / (steady * 9.80665)
		lines.append("    settled circle  %8.2f m radius at %.1f km/h" % [
			radius, KartCore.ms_to_kmh(steady_speed),
		])
	lines.append("    wheels down     %8d minimum after settling   (carrying load)" %
			_wheels_down_min)
	lines.append("    wheels touching %8d minimum after settling   (tire on the road)" %
			_wheels_touching_min)
	# Issue #32, per corner, in millimeters. The order is `CORNER_COUNT` order —
	# FL, FR, RL, RR — which is `chassis_flex.h`'s and therefore the same order the
	# telemetry panel's columns are in.
	#
	# A **lower bound** for any corner whose ray missed entirely: the cast reaches
	# only 100 mm past the free length, so a wheel high in the air reports 100 mm
	# and not its true height. `suspension.h`'s `lift_height` says so at length.
	# It used to report zero there, which is how a kart in mid-flight showed four
	# wheels down — issue #136's worse half.
	lines.append("    peak lift mm    %8.1f FL %7.1f FR %7.1f RL %7.1f RR" % [
		float(_peak_lift[0]) * 1000.0, float(_peak_lift[1]) * 1000.0,
		float(_peak_lift[2]) * 1000.0, float(_peak_lift[3]) * 1000.0,
	])
	# A kart past about 50 degrees of roll is on its side, and every other number
	# in the report describes a kart that is on its side. Reported rather than
	# inferred, because a tipped kart still produces plausible-looking g figures.
	lines.append("    max roll        %8.1f deg%s" % [
		_max_roll_deg, "   TIPPED OVER" if _max_roll_deg > 50.0 else "",
	])
	# The hash is the line `drive.sh` compares between two runs. Printed last and
	# on its own, so a shell can grep for it without parsing the rest.
	lines.append("state-hash %s" % _hash.hex())
	# And the configuration these figures were measured under, on its own line.
	# `_attach` already refused to run a tuned scene, so this can only say
	# "default" — which is the point: a figure quoted from this output a year from
	# now carries the tuning it was taken at, rather than the reader having to
	# trust that the gate was in place on the day. Deliberately **not** folded into
	# `state-hash`; ADR-0037 has why.
	lines.append("tuning-hash default")
	print("\n".join(lines))
	quit(0)
