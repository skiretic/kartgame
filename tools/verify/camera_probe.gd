extends SceneTree

## The camera gate: what the three driving rigs actually do, measured rather than
## read off their constants. Issue #242 area 3, and #237/#195/#203 with it.
##
##     tools/verify/camera.sh
##     godot --headless --path . --script tools/verify/camera_probe.gd -- --case=fov
##     godot --headless --path . --script tools/verify/camera_probe.gd -- --break=fov
##
## ## Why this exists
##
## Nothing has ever measured a camera in this project. Every still is taken with
## `--eye/--look`, which builds a *fourth* camera and leaves the three driving rigs
## untouched — which is how `chase_camera.gd` shipped for a milestone aimed the
## wrong way round, and how `--fov` was inert for a milestone (#237). A camera is
## also the only thing between the driver and every number the solver produces, so
## "does 140 km/h read as fast" is a question about this file's output.
##
## ## The four things it measures, and the analytic answer each is held to
##
##   1. **`attributes`** — #237. Assigning a `CameraAttributesPhysical` overwrites
##      `fov`, `near` and `far` from the physical lens; the shipped 35 mm default
##      is `2 * atan(12 / 35) = 37.8493 deg`, `near` 0.05, `far` 4000. Measured
##      here directly, and then every rig the scene builds is checked to see
##      whether its own fov survived.
##   2. **`fov`** — the speed response. The rigs claim `lerp(STATIC, AT_SPEED,
##      speed / REFERENCE)`, so the analytic answer at every sampled speed is that
##      expression, and the residual must be under `FOV_TOLERANCE_DEG`.
##   3. **`rig`** — where the chase camera actually is. The nominal arm is 3.4 m
##      back and 1.05 m up, but the anchor is exponentially smoothed with a 0.09 s
##      half-life, and the steady-state lag of a first-order smoother tracking a
##      target at constant speed `v` is `v * half_life / ln 2`. So the analytic
##      prediction for the distance behind the kart is
##      `ARM_LENGTH + v * POSITION_HALF_LIFE / ln 2`, which is a *different camera
##      at every speed*, and that is the headline number this file was written for.
##   4. **`roll`** — the lateral-G roll term, held against
##      `-clamp(lat_g / 2.5) * 3.5 deg * shake` low-passed at a 0.25 s half-life.
##   5. **`eye`** — the cockpit eye against the built helmet, read off the same
##      `kart.glb` the scene renders. No modelling: the AABBs come out of the tree.
##
## ## Ground and protocol, stated because every driving figure in this repo needs it
##
## Default scene is `proving_ground.tscn`, which is a **flat plane** — the same
## fixture every §6.4 figure was measured on. `--scene=` runs the same protocol on
## the circuit, which has elevation, and the two are reported separately rather
## than averaged. Input is open-loop and scripted through `KartBody.input_driver`,
## exactly as `drive_probe.gd` drives: full throttle from rest to the end of the
## accel phase, then a steady corner. No controller, so two runs agree.
##
## ## Sampling order, which is not free
##
## A `SceneTree` subclass's own `_process` runs **before** the node tree's, so
## sampling there reads the previous frame's camera against this frame's velocity —
## a lag that is small and not zero, and it is exactly the shape of error that
## makes a camera check pass while measuring nothing. The sampler is a `Node` with
## `process_priority = 1000` instead, which Godot calls after every rig, so the
## camera and the body are read at the same instant. The saboteurs live in the same
## node for the same reason: a `--break` that runs before the rig is overwritten by
## the rig and catches nothing.
##
## ## Negative controls
##
## `--break=<mode>` sabotages one thing and **inverts the exit code**: 0 means the
## sabotage was caught, 1 means it was not. Each verdict demands the saboteur's own
## fingerprint rather than accepting any pre-existing red, because a control that
## passes off somebody else's failure is not a control.
##
##     --break=fov          freeze the chase fov at FOV_STATIC after the rig writes
##                          it.  `fov` must go red naming a frozen response.
##     --break=attributes   re-assign a CameraAttributesPhysical every frame after
##                          the rig writes fov.  `attributes` must go red at
##                          37.8493 exactly.
##     --break=lag          snap the chase anchor onto the kart every frame.
##                          `rig` must go red because the measured lag collapses to
##                          zero where the smoother predicts meters.
##     --break=roll         zero the chase roll after the rig writes it.  `roll`
##                          must go red at a nonzero lateral g.
##     --break=eye          put the eye back on the head mass lump, which is the
##                          exact geometry #195 was filed about.  `eye` must go red
##                          at 371 mm from the head it belongs to.
##     --break=cull         restore the cockpit camera's full cull mask.  `eye` must
##                          go red because the camera is then inside a helmet it draws.

const DEFAULT_SCENE := "res://scenes/game/proving_ground.tscn"

## `2 * atan(12 / 35)` in degrees — a 35 mm lens on Godot's 24 mm sensor height.
## Measured, not recalled: the probe prints what the engine actually produced and
## compares it against this.
const CLOBBERED_FOV := 37.8493
const CLOBBERED_NEAR := 0.05
const CLOBBERED_FAR := 4000.0

## Ticks of full throttle, then ticks of steady corner. 120 Hz, so 1,500 ticks is
## 12.5 s — the proving ground's own figure is 139.8 km/h and the kart is still
## gaining a little at 10 s.
const ACCEL_TICKS := 1560
const CORNER_TICKS := 660
const SETTLE_TICKS := 90

## The airborne phase, and why it is a teleport rather than a jump.
##
## The proving ground is a flat plane and the circuit has no jump on it, so there
## is no ground feature anywhere in this project that puts a kart in the air. A
## camera that misbehaves airborne would therefore never be measured at all. This
## lifts the body `AIR_LIFT` meters and gives it a roll rate about its own forward
## axis, which is exactly the state a curb strike produces and is reproducible from
## a command. It is a **fixture**, and every figure it produces says so.
const AIR_TICKS := 300
const AIR_LIFT := 6.0
const AIR_ROLL_RATE := 2.0


## The steady corner. Quarter lock rather than full, for `drive_probe.gd`'s
## reason: past that the kart scrubs to a standstill (#137) and there is no
## sustained lateral load left to roll a camera with.
const CORNER_STEER := 0.25
const CORNER_THROTTLE := 0.55

## Speeds the tables are cut at, km/h. 140 is the §6.4 top-speed figure.
const SPEED_BINS: PackedFloat32Array = [0.0, 20.0, 60.0, 100.0, 130.0, 140.0]

## Tolerances. The fov one is a tenth of the smallest step a driver could see; the
## geometry one is a centimeter, which is far inside anything a camera resolves.
const FOV_TOLERANCE_DEG := 0.05
const GEOMETRY_TOLERANCE_M := 0.05
const ROLL_TOLERANCE_DEG := 0.25

## The shipped window, from `project.godot`'s `[display]` block. Headless the root
## viewport is 1600x1600 and **not** 1600x900, so every horizontal figure here is
## derived from this constant rather than from the live viewport — measuring hfov
## off a headless canvas reports a camera nobody ships.
const SHIPPED_WIDTH := 1600.0
const SHIPPED_HEIGHT := 900.0

## Reference distance for the optical-flow cue, meters. Fixed so the number is
## comparable between two rigs at two heights; the frame edge is not, because it
## moves with the fov being measured.
const FLOW_REFERENCE_M := 10.0

## Kart overall length, meters. `params.py`'s `length_overall`, and CLAUDE.md's own
## warning applies: it is an **estimate** wearing no source, so it is used here only
## to turn an angle into a fraction of frame and never to justify one.
const KART_LENGTH_M := 1.830

var _args: Dictionary = {}
var _scene_path := DEFAULT_SCENE
var _break := ""
var _cases: PackedStringArray = []
var _root: Node
var _kart: KartBody
var _chase: ChaseCamera
var _cockpit: CockpitCamera
var _sampler: Node

var _tick := 0
var _launch_tick := -1
var _samples: Array[Dictionary] = []
var _corner_samples: Array[Dictionary] = []
var _bin_samples: Dictionary = {}
var _next_bin := 0
var _lines: PackedStringArray = []
var _failures: PackedStringArray = []
var _checks := 0
var _passes := 0
var _skips := 0

## What the engine did to a plain camera, filled by `_measure_clobber`.
var _clobber: Dictionary = {}
## What the comfort file would have applied, reported and then forced to defaults.
var _comfort: Dictionary = {}
## Cockpit eye and helmet geometry, filled once the mesh is in the tree.
var _eye_report: Dictionary = {}


func _initialize() -> void:
	_args = Cmdline.parse()
	var args := _args
	_scene_path = Cmdline.as_string(args, "scene", DEFAULT_SCENE)
	_break = Cmdline.as_string(args, "break", "")
	var case_text := Cmdline.as_string(args, "case", "")
	if not case_text.is_empty():
		_cases = case_text.split(",", false)

	if not ClassDB.class_exists("KartBody"):
		printerr("KartBody is not registered — build the extension: scons target=editor")
		quit(2)
		return

	_measure_clobber()

	var packed: PackedScene = load(_scene_path)
	if packed == null:
		printerr("could not load " + _scene_path)
		quit(2)
		return
	_root = packed.instantiate()
	get_root().add_child(_root)

	_sampler = Node.new()
	_sampler.name = "CameraSampler"
	# After every rig. See the header — a sampler at the default priority reads the
	# previous frame's camera and a saboteur at the default priority is undone by
	# the rig it is trying to sabotage.
	_sampler.process_priority = 1000
	_sampler.set_script(_sampler_script())
	get_root().add_child(_sampler)

	print("camera_probe: %s, break=%s, %d Hz physics"
		% [_scene_path, "none" if _break.is_empty() else _break,
			Engine.physics_ticks_per_second])


## The #237 measurement, on a bare camera with nothing else in the frame.
##
## Run before the scene so it cannot be contaminated by anything the scene does,
## and so the figure the rig checks are held against is this run's and not a
## remembered one.
func _measure_clobber() -> void:
	var camera := Camera3D.new()
	get_root().add_child(camera)
	camera.fov = 62.0
	camera.near = 0.05
	camera.far = 800.0
	var attributes := CameraAttributesPhysical.new()
	attributes.exposure_aperture = 16.0
	attributes.exposure_shutter_speed = 100.0
	attributes.exposure_sensitivity = 100.0
	attributes.auto_exposure_enabled = false
	camera.attributes = attributes
	_clobber = {
		"fov": camera.fov,
		"near": camera.near,
		"far": camera.far,
		"keep_aspect": camera.keep_aspect,
		"attr_fov": attributes.get_fov(),
		"focal_mm": attributes.frustum_focal_length,
	}
	# Does a later touch of the resource clobber a fov written after the assignment?
	# It matters: if it does, every rig that writes fov once in `_ready` is one
	# settings change away from 37.85, and a rig that writes it per frame is safe by
	# accident rather than by design. Measured separately for an **exposure**
	# property and a **frustum** property, because the two do not behave the same
	# and guessing which is which is exactly how this trap keeps landing.
	camera.fov = 62.0
	attributes.exposure_aperture = 8.0
	_clobber["fov_after_exposure_change"] = camera.fov
	camera.fov = 62.0
	attributes.frustum_focal_length = 50.0
	_clobber["fov_after_frustum_change"] = camera.fov
	_clobber["frustum_change_predicted"] = rad_to_deg(2.0 * atan(12.0 / 50.0))
	camera.queue_free()


func _sampler_script() -> GDScript:
	# Written inline rather than as a second file: it is four lines of forwarding
	# and a separate script would be a second thing to keep in step with this one.
	var script := GDScript.new()
	script.source_code = """
extends Node
var probe: Object
func _process(delta: float) -> void:
	if probe != null:
		probe.on_frame(delta)
"""
	script.reload()
	return script


func _physics_process(delta: float) -> bool:
	if _kart == null:
		if not _attach():
			return true
		return false
	_tick += 1
	if _tick == SETTLE_TICKS + ACCEL_TICKS + CORNER_TICKS:
		_launch()
	if _tick >= SETTLE_TICKS + ACCEL_TICKS + CORNER_TICKS + AIR_TICKS:
		_report()
		return true
	return false


## Put the kart in the air, rolled, so the airborne case is measured and not modeled.
func _launch() -> void:
	_launch_tick = _tick
	_kart.global_position += Vector3.UP * AIR_LIFT
	_kart.angular_velocity += (-_kart.global_transform.basis.z) * AIR_ROLL_RATE


func _attach() -> bool:
	_kart = _root.find_child("Kart", false, false) as KartBody
	if _kart == null:
		printerr("no Kart in the scene — is assets/generated/kart.glb built?")
		return false
	_chase = _root.find_child("ChaseRig", false, false)
	_cockpit = _kart.find_child("CockpitRig", false, false)
	if _chase == null or _cockpit == null:
		printerr("the scene built no ChaseRig/CockpitRig — nothing to measure")
		return false

	# **Force the comfort trims to their shipped defaults, and say what the file
	# held.** `ChaseCamera.comfort_settings()` reads the real `user://settings.cfg`
	# whenever the run is not scripted from `--throttle`, and every worktree shares
	# one `user://`: a trim somebody set while driving would move every fov in this
	# report and nothing would say so.
	_comfort = {
		"chase_fov_trim": _chase.fov_trim_deg,
		"chase_shake": _chase.shake,
		"cockpit_fov_trim": _cockpit.fov_trim_deg,
		"cockpit_head_motion": _cockpit.head_motion,
		"cockpit_horizon_lock": _cockpit.horizon_lock,
	}
	_chase.fov_trim_deg = 0.0
	_chase.shake = 1.0
	_cockpit.fov_trim_deg = 0.0
	_cockpit.head_motion = 1.0
	_cockpit.horizon_lock = false

	_measure_eye()

	_kart.input_driver = _scripted_input
	_sampler.probe = self
	return true


## The cockpit eye against the geometry that is actually in the tree.
##
## Read off the instanced `kart.glb`'s own nodes rather than off a table, because a
## table is what put the eye where it is: `chassis.h`'s mass lump and the built
## helmet were never compared, and #195 is that comparison written down.
func _measure_eye() -> void:
	var eye: Vector3 = _kart.driver_head_position()
	var report := {"eye": eye}
	for part: String in ["driver_helmet", "driver_helmet_visor", "driver_torso"]:
		var node := _kart.find_child(part, true, false) as VisualInstance3D
		if node == null:
			continue
		var aabb := node.get_aabb()
		# Into the body frame: the mesh hangs under the body through one or more
		# transforms, so the AABB is in the mesh's own space until it is carried.
		var to_body := _kart.global_transform.affine_inverse() * node.global_transform
		var low := Vector3(INF, INF, INF)
		var high := Vector3(-INF, -INF, -INF)
		for i in 8:
			var corner := to_body * aabb.get_endpoint(i)
			low = low.min(corner)
			high = high.max(corner)
		report[part] = {
			"low": low, "high": high, "center": (low + high) * 0.5,
			"layers": node.layers,
		}
	_eye_report = report


func _scripted_input() -> Dictionary:
	if _tick < SETTLE_TICKS + ACCEL_TICKS:
		return {"throttle": 1.0, "brake": 0.0, "steer": 0.0}
	return {"throttle": CORNER_THROTTLE, "brake": 0.0, "steer": CORNER_STEER}


## One rendered frame, called from the priority-1000 sampler so the rigs have
## already written this frame's camera state.
func on_frame(delta: float) -> void:
	if _kart == null or _chase == null:
		return
	_sabotage()

	var velocity: Vector3 = _kart.linear_velocity
	var speed := velocity.length()
	var kart_position: Vector3 = _kart.global_position
	var chase_camera: Camera3D = _chase.camera
	var cockpit_camera: Camera3D = _cockpit.camera
	var basis: Basis = _kart.global_transform.basis
	var forward := -basis.z
	var flat_forward := Vector3(forward.x, 0.0, forward.z)
	if flat_forward.length_squared() < 1e-6:
		flat_forward = Vector3.FORWARD
	flat_forward = flat_forward.normalized()

	var to_camera := chase_camera.global_position - kart_position
	var sample := {
		"tick": _tick,
		"delta": delta,
		"speed": speed,
		"chase_fov": chase_camera.fov,
		"chase_near": chase_camera.near,
		"chase_far": chase_camera.far,
		# Signed along the kart's own heading: positive is behind the kart, which is
		# where a chase camera belongs.
		"chase_back": -to_camera.dot(flat_forward),
		"chase_height": to_camera.y,
		"chase_lateral": to_camera.dot(basis.x),
		"chase_range": to_camera.length(),
		"spring_length": _chase.spring_length,
		# The arm's own origin, which is the smoothed anchor. Its lag behind the
		# kart is the quantity with the analytic answer.
		"anchor_back": -(_chase.global_position - kart_position).dot(flat_forward),
		"chase_roll_deg": rad_to_deg(_camera_roll(chase_camera)),
		# Positive is looking DOWN. The camera's view direction is -Z, so its
		# vertical component is negative when the camera is pitched down and the
		# sign has to be taken here rather than at the reader — a depression angle
		# added to half the fov with the wrong sign gives a near edge further away
		# than the far one and still prints a plausible number.
		"chase_pitch_down_deg": -rad_to_deg(
			asin(clampf(-chase_camera.global_basis.z.y, -1.0, 1.0))),
		"cockpit_fov": cockpit_camera.fov,
		"cockpit_height": cockpit_camera.global_position.y - kart_position.y,
		"cockpit_roll_deg": rad_to_deg(_camera_roll(cockpit_camera)),
		# **Measured, not assumed to be zero.** The table below used to hardcode
		# `0.0` here with a comment saying the cockpit plane is level -- true when it
		# was written and false the moment `CockpitCamera.PITCH_DOWN` landed, and
		# the "nearest visible road" column is DERIVED from this angle, so a stale
		# zero silently understates the one figure the cockpit view exists to give.
		# Same form and same sign convention as the chase row above.
		"cockpit_pitch_down_deg": -rad_to_deg(
			asin(clampf(-cockpit_camera.global_basis.z.y, -1.0, 1.0))),
		# Two lateral figures on purpose. The first is the rig's *own* arithmetic —
		# a finite difference of `linear_velocity` projected onto the kart's right
		# axis, because Godot exposes no accelerometer — and it is what the roll
		# term is driven by. The second is the solver's published figure. If they
		# ever disagree the camera is rolling to a number nobody else can see.
		"lateral_g": _lateral_g(velocity, basis, delta),
		"solver_lateral_g": _kart.get_lateral_g(),
		"solver_longitudinal_g": _kart.get_longitudinal_g(),
		"wheels_down": _kart.get_wheels_on_ground(),
		"airborne": _kart.get_wheels_on_ground() == 0,
		"right_y": basis.x.y,
	}
	_samples.append(sample)
	# The corner window closes at the launch. It did not, and the last 600 corner
	# frames then contained a tumbling airborne kart: the mean lateral figure fell
	# from 1.93 g to 0.79 g and the cockpit roll read 35 degrees, which is a kart
	# on its side being reported as a cornering camera.
	if _tick >= SETTLE_TICKS + ACCEL_TICKS and _launch_tick < 0:
		_corner_samples.append(sample)

	while _next_bin < SPEED_BINS.size() and speed * 3.6 >= SPEED_BINS[_next_bin]:
		_bin_samples[SPEED_BINS[_next_bin]] = sample
		_next_bin += 1


var _previous_velocity := Vector3.ZERO


func _lateral_g(velocity: Vector3, basis: Basis, delta: float) -> float:
	var acceleration := (velocity - _previous_velocity) / maxf(delta, 1e-4)
	_previous_velocity = velocity
	return acceleration.dot(basis.x) / 9.81


## Roll about the view axis, radians, positive when screen-right tilts up.
##
## `atan2(-x.y, y.y)` and not `asin(x.y)`: the chase rig aims with `Vector3.UP` and
## then rolls about its own forward axis, so the recovered angle has to be taken in
## the camera's own screen plane or a pitched camera reports a roll it does not have.
func _camera_roll(camera: Camera3D) -> float:
	var b := camera.global_basis
	return atan2(-b.x.y, b.y.y)


func _sabotage() -> void:
	match _break:
		"fov":
			_chase.camera.fov = _chase.FOV_STATIC
		"attributes":
			var attributes := CameraAttributesPhysical.new()
			attributes.exposure_aperture = 16.0
			attributes.auto_exposure_enabled = false
			_chase.camera.attributes = attributes
		"lag":
			# **Inverted along with the check it feeds.** This used to snap the
			# anchor onto the kart every frame -- a correct sabotage while the rig
			# smoothed position, and a **no-op** the moment Anthony had the smoothing
			# removed, because snapping became the shipped behavior. It sat there
			# reporting NOT CAUGHT, which is the one honest thing an inert control
			# can do and is still easy to read as a broken check.
			#
			# CLAUDE.md's own entry: a negative control has a default and the default
			# can be the one value that cannot fire. So this reinstates the
			# **historical** 0.09 s half-life instead -- the same principle the `eye`
			# control below states in its own words, that a control putting a real
			# past bug back is worth more than one inventing a new one, because it
			# also proves the check would have caught the original.
			_chase.position_half_life = 0.09
		"roll":
			var camera: Camera3D = _chase.camera
			camera.global_basis = Basis.looking_at(
				-camera.global_basis.z, Vector3.UP)
		"eye":
			# The exact defect #195 was filed about, put back: the eye is the head
			# mass lump's center again. A control that reinstates the historical bug
			# is worth more than one that invents a new one, because it also proves
			# the check would have caught the original.
			_cockpit._eye = _kart.driver_head_position()
		"cull":
			_cockpit.camera.cull_mask = 0xFFFFF


# --- reporting --------------------------------------------------------------


func _wants(case_name: String) -> bool:
	return _cases.is_empty() or _cases.has(case_name)


## A case that cannot be measured on this fixture, said out loud.
##
## Not the same thing as a passing check and it must never be counted as one — the
## house rule is that a check which cannot fail is not a check, and a silent skip is
## the version of that failure that also hides itself.
func _skip(case_name: String, label: String, reason: String) -> void:
	if not _wants(case_name):
		return
	_skips += 1
	_lines.append("    SKIP %-46s %s" % [label, reason])


func _check(case_name: String, label: String, ok: bool, detail: String) -> void:
	if not _wants(case_name):
		return
	_checks += 1
	if ok:
		_passes += 1
		_lines.append("    ok   %-46s %s" % [label, detail])
	else:
		_failures.append("%s: %s — %s" % [case_name, label, detail])
		_lines.append("    FAIL %-46s %s" % [label, detail])


func _report() -> void:
	if _wants("attributes"):
		_report_attributes()
	if _wants("fov"):
		_report_fov()
	if _wants("rig"):
		_report_rig()
	if _wants("roll"):
		_report_roll()
	if _wants("eye"):
		_report_eye()
	_report_cues()

	print("\n".join(_lines))
	print("")
	print("camera_probe: %d/%d checks%s" % [
		_passes, _checks, "" if _skips == 0 else ", %d skipped" % _skips])
	if not _break.is_empty():
		var caught := _caught_the_saboteur()
		print("negative control --break=%s: %s"
			% [_break, "CAUGHT" if caught else "NOT CAUGHT"])
		for failure in _failures:
			print("    red: " + failure)
		quit(0 if caught else 1)
		return
	for failure in _failures:
		print("    red: " + failure)
	quit(0 if _failures.is_empty() else 1)


## Did *this* saboteur's own fingerprint turn up in the measurement?
##
## Not "did anything go red". A control that accepts a pre-existing failure proves
## nothing, and this repo has shipped one.
func _caught_the_saboteur() -> bool:
	var wanted: String = {
		"fov": "fov",
		"attributes": "attributes",
		"lag": "rig",
		"roll": "roll",
		"eye": "eye",
		"cull": "eye",
	}.get(_break, "")
	for failure in _failures:
		if failure.begins_with(wanted + ":"):
			return true
	return false


func _report_attributes() -> void:
	_lines.append("")
	_lines.append("--- attributes: what a CameraAttributesPhysical does to a Camera3D (#237)")
	_lines.append("    a bare camera set to fov 62 / near 0.05 / far 800, then handed one:")
	_lines.append("        fov  %.4f      (35.0 mm lens, the resource's own fov %.4f)"
		% [_clobber["fov"], _clobber["attr_fov"]])
	_lines.append("        near %.4f  far %.1f  keep_aspect %d"
		% [_clobber["near"], _clobber["far"], _clobber["keep_aspect"]])
	_lines.append("        fov re-set to 62, then an EXPOSURE property changed: %.4f  (survives)"
		% _clobber["fov_after_exposure_change"])
	_lines.append("        fov re-set to 62, then a FRUSTUM property changed:  %.4f  (clobbered"
		% _clobber["fov_after_frustum_change"])
	_lines.append("        again — 2*atan(12/50) for the 50 mm lens is %.4f)"
		% _clobber["frustum_change_predicted"])
	_check("attributes", "a frustum change re-clobbers a fov written after the assignment",
		absf(float(_clobber["fov_after_frustum_change"])
			- float(_clobber["frustum_change_predicted"])) < 0.001,
		"%.4f vs %.4f" % [_clobber["fov_after_frustum_change"],
			_clobber["frustum_change_predicted"]])
	_check("attributes", "the clobber is 2*atan(12/35) as documented",
		absf(float(_clobber["fov"]) - CLOBBERED_FOV) < 0.001,
		"%.4f vs %.4f" % [_clobber["fov"], CLOBBERED_FOV])
	_check("attributes", "near and far are clobbered too",
		absf(float(_clobber["near"]) - CLOBBERED_NEAR) < 1e-6
			and absf(float(_clobber["far"]) - CLOBBERED_FAR) < 1e-6,
		"near %.4f far %.1f" % [_clobber["near"], _clobber["far"]])

	var last: Dictionary = _samples[-1]
	_check("attributes", "the chase rig's fov survived its attributes",
		absf(float(last["chase_fov"]) - CLOBBERED_FOV) > 0.5,
		"effective %.4f deg at %.1f km/h" % [last["chase_fov"], float(last["speed"]) * 3.6])
	_check("attributes", "the cockpit rig's fov survived its attributes",
		absf(float(last["cockpit_fov"]) - CLOBBERED_FOV) > 0.5,
		"effective %.4f deg" % last["cockpit_fov"])
	var free_camera := _root.find_child("FreeCamera", false, false) as Camera3D
	if free_camera != null:
		_lines.append("    free camera: fov %.4f near %.4f far %.1f"
			% [free_camera.fov, free_camera.near, free_camera.far])
		_check("attributes", "the free camera's fov survived its attributes",
			absf(free_camera.fov - CLOBBERED_FOV) > 0.5,
			"effective %.4f deg (it writes 70.0 in _ready)" % free_camera.fov)


func _report_fov() -> void:
	_lines.append("")
	_lines.append("--- fov: the speed response, effective rather than assigned")
	_lines.append("    km/h    chase fov   design    resid     cockpit fov  design    resid")
	var worst_chase := 0.0
	var worst_cockpit := 0.0
	var frozen := true
	var first_chase := -1.0
	for bin: float in SPEED_BINS:
		if not _bin_samples.has(bin):
			continue
		var s: Dictionary = _bin_samples[bin]
		var speed := float(s["speed"])
		var chase_design := _design_fov(_chase.FOV_STATIC, _chase.FOV_AT_SPEED,
			_chase.FOV_REFERENCE_SPEED, speed)
		var cockpit_design := _design_fov(_cockpit.FOV_STATIC, _cockpit.FOV_AT_SPEED,
			_cockpit.FOV_REFERENCE_SPEED, speed)
		var chase_residual := absf(float(s["chase_fov"]) - chase_design)
		var cockpit_residual := absf(float(s["cockpit_fov"]) - cockpit_design)
		worst_chase = maxf(worst_chase, chase_residual)
		worst_cockpit = maxf(worst_cockpit, cockpit_residual)
		if first_chase < 0.0:
			first_chase = float(s["chase_fov"])
		elif absf(float(s["chase_fov"]) - first_chase) > 0.01:
			frozen = false
		_lines.append("    %6.1f  %9.4f  %8.4f  %7.4f  %10.4f  %8.4f  %7.4f"
			% [speed * 3.6, s["chase_fov"], chase_design, chase_residual,
				s["cockpit_fov"], cockpit_design, cockpit_residual])
	# The residual check is against the *design* expression, so it can only fail if
	# something between the constant and the camera ate the value. The frozen check
	# is the one that catches a fov pinned by anything at all, which is #237's
	# actual symptom and is invisible to a residual on a single row.
	_check("fov", "the chase fov follows its own lerp at every speed",
		worst_chase < FOV_TOLERANCE_DEG, "worst residual %.4f deg" % worst_chase)
	_check("fov", "the cockpit fov follows its own lerp at every speed",
		worst_cockpit < FOV_TOLERANCE_DEG, "worst residual %.4f deg" % worst_cockpit)
	_check("fov", "the chase fov is not frozen across the sweep", not frozen,
		"%.4f deg at every speed bin" % first_chase if frozen
			else "moved %.2f deg over the sweep"
				% (_design_fov(_chase.FOV_STATIC, _chase.FOV_AT_SPEED,
					_chase.FOV_REFERENCE_SPEED, float(_samples[-1]["speed"])) - first_chase))


func _design_fov(static_fov: float, at_speed: float, reference: float, speed: float) -> float:
	return lerpf(static_fov, at_speed, clampf(speed / reference, 0.0, 1.0))


func _report_rig() -> void:
	_lines.append("")
	_lines.append("--- rig: where the chase camera actually is, against the smoother's own answer")
	# The proposal levers, and the check that they are levers and not a change.
	# `--chase-arm=` and friends exist so a camera proposal can be a command rather
	# than an edit; the failure mode of that idea is a default that quietly differs
	# from the constant it shadows, which would move the shipped rig for everyone
	# and be invisible in a diff. Held to equality, not a tolerance.
	_lines.append("    levers: arm %.3f  height %.3f  lag %.4f  look-ahead %.3f  fov %.1f-%.1f"
		% [_chase.arm_length, _chase.arm_height, _chase.position_half_life,
			_chase.look_ahead_seconds, _chase.fov_static, _chase.fov_at_speed])
	var overridden := false
	for key: String in [
		"chase-arm", "chase-height", "chase-lag", "chase-look-ahead",
		"chase-fov-static", "chase-fov-speed",
		"cockpit-fov-static", "cockpit-fov-speed", "cockpit-eye",
	]:
		if _args.has(key):
			overridden = true
	var untouched := (
		_chase.arm_length == ChaseCamera.ARM_LENGTH
		and _chase.arm_height == ChaseCamera.ARM_HEIGHT
		and _chase.position_half_life == ChaseCamera.POSITION_HALF_LIFE
		and _chase.look_ahead_seconds == ChaseCamera.LOOK_AHEAD_SECONDS
		and _chase.fov_static == ChaseCamera.FOV_STATIC
		and _chase.fov_at_speed == ChaseCamera.FOV_AT_SPEED
		and _cockpit.fov_static == CockpitCamera.FOV_STATIC
		and _cockpit.fov_at_speed == CockpitCamera.FOV_AT_SPEED
		and _cockpit.eye == CockpitCamera.EYE
	)
	if overridden:
		# The run named a lever, so "the levers are their defaults" is not a claim
		# anybody is making. Said out loud rather than quietly passed — a sweep is
		# exactly the run in which a silent skip would be least noticed.
		_skip("rig", "the lever defaults are not asserted under an override",
			"this run passed a --chase-*/--cockpit-* argument")
	else:
		_check("rig", "with no --chase-*/--cockpit-* argument the rigs are the constants",
			untouched, "every lever is its own default" if untouched
				else "a lever's default has drifted from the constant it shadows")

	var tau: float = _chase.position_half_life / log(2.0)
	_lines.append("    the anchor is exponentially smoothed at a %.2f s half-life, so its time"
		% _chase.position_half_life)
	_lines.append("    constant is half_life / ln 2 = %.4f s. A first-order smoother chasing" % tau)
	_lines.append("    p(t) settles at p - tau*v + tau^2*a, so an ACCELERATING target sits closer")
	_lines.append("    than tau*v by tau^2*a — which is why the low-speed rows below undershoot")
	_lines.append("    and the top-speed row, where a has fallen away, does not. `a` is the")
	_lines.append("    solver's own published longitudinal g, not a second difference of mine.")
	_lines.append("    km/h    lag     tau*v   tau^2*a   pred    resid    back   height  range  arm")
	var worst_rel := 0.0
	var worst_absolute := 0.0
	var worst_predicted := 0.0
	var worst_label := ""
	for bin: float in SPEED_BINS:
		if not _bin_samples.has(bin):
			continue
		var s: Dictionary = _bin_samples[bin]
		var speed := float(s["speed"])
		var accel := float(s["solver_longitudinal_g"]) * 9.81
		var first := speed * tau
		var second := tau * tau * accel
		var predicted := first - second
		var lag := float(s["anchor_back"])
		var residual := lag - predicted
		if speed > 5.0 and absf(residual) > worst_absolute:
			worst_absolute = absf(residual)
			worst_rel = absf(residual) / maxf(predicted, 0.01)
			worst_predicted = predicted
			worst_label = "%.1f km/h: %.4f m measured, %.4f m predicted" % [
				speed * 3.6, lag, predicted]
		_lines.append("    %5.1f %8.4f %8.4f %8.4f %8.4f %8.4f %6.3f %7.3f %6.3f %5.2f"
			% [speed * 3.6, lag, first, second, predicted, residual, s["chase_back"],
				s["chase_height"], s["chase_range"], s["spring_length"]])
	# The tolerance is **8% or 0.15 m, whichever is larger**, and the absolute floor
	# is the load-bearing half. What is left after the two analytic terms is a
	# sub-frame sampling offset of order `v * delta` — the kart's position advances
	# between the rig's `_process` and this sampler's, and at 38.9 m/s a 2 ms frame
	# is 78 mm of it. That term does not shrink when the *lag* shrinks, so a purely
	# relative tolerance tightens itself as `--chase-lag` is swept and the check
	# turns red on the sweep rather than on the rig. Measured: at the shipped
	# 0.09 s half-life the worst row is 4.6%; at 0.035 it is 8.5%, and the residual
	# in meters is 0.02 either way.
	_check("rig", "the anchor lag is the smoother's own two-term answer at every speed",
		worst_absolute < maxf(0.08 * worst_predicted, 0.15),
		"worst %.4f m (%.1f%%) — %s" % [worst_absolute, worst_rel * 100.0, worst_label])

	var rest: Dictionary = _bin_samples.get(0.0, _samples[0])
	var fast: Dictionary = _bin_samples.get(130.0, _samples[-1])
	var growth := float(fast["chase_back"]) - float(rest["chase_back"])
	_lines.append("    the camera is %.3f m further back at %.1f km/h than at rest."
		% [growth, float(fast["speed"]) * 3.6])
	# **Inverted, because the defect it pinned has been fixed.** It used to assert
	# that the distance *is* speed dependent -- written to prove the runaway lag was
	# a reading rather than arithmetic, which was the right check while the rig had
	# `POSITION_HALF_LIFE = 0.09` and sat 8.45 m back at 140 km/h against a 3.40 m
	# arm. Anthony's answer was to remove position smoothing outright, so the arm is
	# now a constant and the old assertion is a check that pins a bug in place.
	#
	# Note it does NOT hardcode zero lag: `growth` is compared against the same
	# tolerance either way, so `--chase-lag=0.09` still produces a red here and the
	# check keeps working as a way to *find* the old behavior rather than being
	# retired.
	_check("rig", "the chase distance is a constant arm (no speed-dependent lag)",
		absf(growth) <= GEOMETRY_TOLERANCE_M,
		"%.3f m of growth from rest to speed" % growth)


func _report_roll() -> void:
	_lines.append("")
	_lines.append("--- roll: the lateral-G term, in a steady %.2f-lock corner" % CORNER_STEER)
	# **Both halves of this case are proving-ground fixtures and they do not travel.**
	# The steady corner is an open-loop quarter of lock held for 5.5 s, which on a
	# flat plane settles into a circle and on a circuit drives the kart off the road
	# into whatever is there: measured on `valdirone.tscn` the tail lateral load was
	# -0.565 g with the camera at -0.003 deg, because the kart had already left the
	# track and was no longer turning. The ballistic drop is the same — a 6 m lift
	# over sloped terrain is not free fall, and the airborne figure came out 0.17 g
	# off its own analytic answer for exactly that reason.
	#
	# Skipped rather than loosened. A tolerance widened until a bad fixture passes is
	# a check that has stopped checking, and the number it then prints still describes
	# a kart in the barriers.
	if _scene_path != DEFAULT_SCENE:
		_skip("roll", "the corner and drop fixtures are %s's" % DEFAULT_SCENE.get_file(),
			"this run is %s" % _scene_path.get_file())
		return
	if _corner_samples.size() < 60:
		_check("roll", "the corner phase produced samples", false,
			"%d samples" % _corner_samples.size())
		return
	# The last second of the corner, so the 0.25 s low-pass has settled and the
	# figure is the steady state rather than the entry transient.
	var tail := _corner_samples.slice(maxi(0, _corner_samples.size() - 600))
	var mean_g := 0.0
	var mean_solver_g := 0.0
	var mean_roll := 0.0
	var mean_cockpit_roll := 0.0
	for s: Dictionary in tail:
		mean_g += float(s["lateral_g"])
		mean_solver_g += float(s["solver_lateral_g"])
		mean_roll += float(s["chase_roll_deg"])
		mean_cockpit_roll += float(s["cockpit_roll_deg"])
	mean_g /= float(tail.size())
	mean_solver_g /= float(tail.size())
	mean_roll /= float(tail.size())
	mean_cockpit_roll /= float(tail.size())
	var design_roll := -clampf(mean_g / 2.5, -1.0, 1.0) * rad_to_deg(_chase.ROLL_AT_PEAK_G)
	_lines.append("    mean lateral %.4f g over %d frames  (the rig's own finite difference)"
		% [mean_g, tail.size()])
	_lines.append("    solver lateral %.4f g            (KartBody.lateral_g, same window)"
		% mean_solver_g)
	_lines.append("    cockpit roll %.4f deg — it follows the chassis and has no term of its own"
		% mean_cockpit_roll)
	_lines.append("    camera roll  %.4f deg    design %.4f deg    resid %.4f deg"
		% [mean_roll, design_roll, absf(mean_roll - design_roll)])
	_lines.append("    ROLL_AT_PEAK_G is %.2f deg at 2.5 g, so a 1.35 g corner is %.3f deg."
		% [rad_to_deg(_chase.ROLL_AT_PEAK_G), 1.35 / 2.5 * rad_to_deg(_chase.ROLL_AT_PEAK_G)])
	_check("roll", "the camera roll is the design response to the measured g",
		absf(mean_roll - design_roll) < ROLL_TOLERANCE_DEG,
		"%.4f deg against %.4f deg" % [mean_roll, design_roll])
	_check("roll", "the roll term is doing something at all",
		absf(mean_roll) > 0.05, "%.4f deg at %.3f g" % [mean_roll, mean_g])

	# The airborne case, off the `_launch` fixture. It matters because the rig has
	# no accelerometer and takes lateral g from a finite difference of
	# `linear_velocity` projected onto the kart's right axis. With the wheels down
	# the normal force cancels gravity and that projection is a cornering force;
	# **in the air the only acceleration is gravity**, so the same expression
	# returns `-right.y` and the camera rolls in response to the chassis being
	# tilted rather than to any load at all.
	# The ballistic window — see `_ballistic_window`, and the reason it is a tick
	# band and not "the frames where the wheels are up".
	var flight := _ballistic_window()
	var airborne_roll := 0.0
	var airborne_g := 0.0
	var airborne_right_y := 0.0
	for s: Dictionary in flight:
		if absf(float(s["lateral_g"])) > absf(airborne_g):
			airborne_g = float(s["lateral_g"])
			airborne_right_y = float(s["right_y"])
		airborne_roll = maxf(airborne_roll, absf(float(s["chase_roll_deg"])))
	_lines.append("")
	_lines.append("    airborne (the launch fixture: +%.1f m and %.1f rad/s of roll rate)"
		% [AIR_LIFT, AIR_ROLL_RATE])
	_lines.append("    %d frames of clear flight, worst |roll| %.4f deg, worst |lateral g| %.4f"
		% [flight.size(), airborne_roll, airborne_g])
	_lines.append("    at that frame the kart's right axis had y = %.4f, and free fall makes"
		% airborne_right_y)
	_lines.append("    the rig's own expression -right.y = %.4f g. The camera is reading the"
		% -airborne_right_y)
	_lines.append("    chassis attitude as a cornering load.")
	_check("roll", "airborne, the rig's lateral g is gravity on the right axis",
		flight.size() > 20 and absf(airborne_g - (-airborne_right_y)) < 0.15,
		"measured %.4f g against -right.y = %.4f g over %d frames"
			% [airborne_g, -airborne_right_y, flight.size()])


## The ballistic window: airborne frames inside a fixed tick band after the launch.
##
## A "longest contiguous airborne run" was the first cut and it is not enough. The
## kart rolls past vertical on the way down and lands on its **side**, where the
## chassis collider stops it and no wheel ever reports contact — so the airborne
## flag never clears, the run runs to the end of the phase, and the impact sits
## inside it at -89 g. A tick band bounded by the free-fall time is the only
## definition that cannot swallow a landing: from +6.0 m the drop is
## `sqrt(2 h / g)` = 1.106 s = 133 ticks, and this window closes at 100.
func _ballistic_window() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if _launch_tick < 0:
		return out
	for s: Dictionary in _samples:
		var tick := int(s["tick"])
		if tick < _launch_tick + 10 or tick > _launch_tick + 100:
			continue
		if bool(s["airborne"]):
			out.append(s)
	return out


func _report_eye() -> void:
	_lines.append("")
	_lines.append("--- eye: the cockpit viewpoint against the helmet that is in the tree (#195/#203)")
	# The rig's own field, not the lump the rig was seeded from. Those are the same
	# value today and the whole point of #195 is that one of them should move, so a
	# check that reads the seed measures a number the camera does not use — and the
	# `--break=eye` control, which moves the field, would sail straight past it.
	var eye: Vector3 = _cockpit._eye
	var seed: Vector3 = _eye_report["eye"]
	_lines.append("    CockpitCamera eye                (%.4f, %.4f, %.4f)  body frame"
		% [eye.x, eye.y, eye.z])
	_lines.append("    KartBody.driver_head_position()  (%.4f, %.4f, %.4f)  the seed"
		% [seed.x, seed.y, seed.z])
	if not _eye_report.has("driver_helmet"):
		_check("eye", "the helmet mesh is in the tree", false,
			"no driver_helmet node — is assets/generated/kart.glb the driver build?")
		return
	var helmet: Dictionary = _eye_report["driver_helmet"]
	var low: Vector3 = helmet["low"]
	var high: Vector3 = helmet["high"]
	var center: Vector3 = helmet["center"]
	_lines.append("    driver_helmet   low (%.4f, %.4f, %.4f)  high (%.4f, %.4f, %.4f)"
		% [low.x, low.y, low.z, high.x, high.y, high.z])
	_lines.append("    driver_helmet   center (%.4f, %.4f, %.4f)  size (%.3f, %.3f, %.3f)"
		% [center.x, center.y, center.z, high.x - low.x, high.y - low.y, high.z - low.z])
	if _eye_report.has("driver_helmet_visor"):
		var visor: Dictionary = _eye_report["driver_helmet_visor"]
		var vc: Vector3 = visor["center"]
		_lines.append("    visor aperture  center (%.4f, %.4f, %.4f)" % [vc.x, vc.y, vc.z])
	_lines.append("    eye - helmet center: %+.1f mm lateral, %+.1f mm up, %+.1f mm rearward"
		% [(eye.x - center.x) * 1000.0, (eye.y - center.y) * 1000.0,
			(eye.z - center.z) * 1000.0])
	_lines.append("    eye behind the shell's front face: %+.1f mm (negative is in front of it)"
		% ((eye.z - low.z) * 1000.0))

	# The eye is a viewpoint, so where it sits relative to the head is the whole
	# question. A tolerance rather than an equality: §60.1.4's eye is a `derived`
	# figure and what is being asked is whether the camera is in the same head.
	var to_center := (eye - center).length()
	_check("eye", "the cockpit eye is inside the head it belongs to",
		to_center < 0.20, "%.1f mm from the helmet center" % (to_center * 1000.0))

	# **Not "the eye is outside the shell".** That was this check's first cut and it
	# is the wrong invariant: there is no eye point in a real head that sits outside
	# a closed full-face shell, so the check can only be satisfied by putting the
	# camera somewhere a driver's eye is not. The invariant that means something is
	# that a head part containing the eye is culled from *this* camera — which is a
	# bit test with an exact answer and no tolerance at all.
	var mask: int = _cockpit.camera.cull_mask
	for part: String in ["driver_helmet", "driver_helmet_visor"]:
		if not _eye_report.has(part):
			continue
		var bounds: Dictionary = _eye_report[part]
		var part_low: Vector3 = bounds["low"]
		var part_high: Vector3 = bounds["high"]
		var inside := (
			eye.x > part_low.x and eye.x < part_high.x
			and eye.y > part_low.y and eye.y < part_high.y
			and eye.z > part_low.z and eye.z < part_high.z
		)
		var layers: int = _eye_report[part]["layers"]
		var visible_here := (mask & layers) != 0
		_lines.append("    %-20s eye inside: %-5s  layers 0x%05x  cull_mask 0x%05x  drawn: %s"
			% [part, inside, layers, mask, visible_here])
		_check("eye", "%s is culled from the cockpit camera" % part,
			not (inside and visible_here),
			"eye is %sinside it and the camera %s it"
				% ["" if inside else "not ", "draws" if visible_here else "culls"])


## The four cues that decide whether a speed reads as a speed, reported and not
## judged. Every one is arithmetic on a measured row, and the row is printed.
func _report_cues() -> void:
	_lines.append("")
	_lines.append("--- cues: what the driver is actually given, at the shipped %d x %d"
		% [int(SHIPPED_WIDTH), int(SHIPPED_HEIGHT)])
	_lines.append("    hfov is derived: vfov is vertical because keep_aspect is KEEP_HEIGHT,")
	_lines.append("    so hfov = 2 atan(aspect * tan(vfov/2)) with aspect %.4f. Headless the root"
		% (SHIPPED_WIDTH / SHIPPED_HEIGHT))
	_lines.append("    viewport is 1600x1600, so this is derived from the shipped size and not")
	_lines.append("    read off the live canvas — a headless hfov describes a camera nobody ships.")
	_lines.append("")
	_lines.append("    near   is the ground distance at the BOTTOM EDGE of the frame:")
	_lines.append("             h / tan(pitch_down + vfov/2), the closest road the driver can see.")
	_lines.append("    edge   is the angular rate of that point as it sweeps past, v h / (x^2+h^2),")
	_lines.append("             deg/s — the flow cue, anchored to a screen position and not to a")
	_lines.append("             distance, because the screen is where the cue is read.")
	_lines.append("    ref    is the same rate at a fixed %.0f m, so two rigs at two heights are"
		% FLOW_REFERENCE_M)
	_lines.append("             comparable on one number.")
	_lines.append("    kart%   is the kart's own length as a fraction of frame height (chase only).")
	_lines.append("")
	_lines.append("    rig      km/h    vfov     hfov  height  pitch   back   near   edge d/s  ref d/s  kart%")
	var aspect := SHIPPED_WIDTH / SHIPPED_HEIGHT
	for bin: float in SPEED_BINS:
		if not _bin_samples.has(bin):
			continue
		var s: Dictionary = _bin_samples[bin]
		var speed := float(s["speed"])
		for rig: String in ["chase", "cockpit"]:
			var vfov := float(s[rig + "_fov"])
			var height := float(s[rig + "_height"])
			var is_chase := rig == "chase"
			var back: float = float(s["chase_back"]) if is_chase else 0.0
			# The cockpit rig aims down the kart's own heading, which on a flat
			# plane is level; the chase rig's pitch is measured because it lifts
			# its gaze as the look-ahead point runs away up the road.
			var pitch: float = float(s["chase_pitch_down_deg"]) if is_chase \
				else float(s["cockpit_pitch_down_deg"])
			var hfov := rad_to_deg(2.0 * atan(aspect * tan(deg_to_rad(vfov) * 0.5)))
			var depression := deg_to_rad(pitch + vfov * 0.5)
			var near_m := height / maxf(tan(depression), 1e-3)
			var edge_flow := rad_to_deg(speed * height / (near_m * near_m + height * height))
			var reference_flow := rad_to_deg(
				speed * height / (FLOW_REFERENCE_M * FLOW_REFERENCE_M + height * height))
			var kart_fraction := 0.0
			if is_chase:
				var range_m := maxf(0.01, sqrt(back * back + height * height))
				kart_fraction = rad_to_deg(2.0 * atan(KART_LENGTH_M * 0.5 / range_m)) / vfov
			_lines.append("    %-7s %5.1f  %7.3f %7.3f %6.3f %6.2f %6.2f %6.3f %9.1f %8.2f  %s"
				% [rig, speed * 3.6, vfov, hfov, height, pitch, back, near_m,
					edge_flow, reference_flow,
					("%5.3f" % kart_fraction) if is_chase else "    -"])
	_lines.append("")
	_lines.append("    comfort file at build time: chase trim %.2f deg shake %.2f,"
		% [_comfort.get("chase_fov_trim", 0.0), _comfort.get("chase_shake", 1.0)])
	_lines.append("    cockpit trim %.2f deg head_motion %.2f horizon_lock %s — forced to"
		% [_comfort.get("cockpit_fov_trim", 0.0), _comfort.get("cockpit_head_motion", 1.0),
			_comfort.get("cockpit_horizon_lock", false)])
	_lines.append("    the shipped defaults before any measurement was taken.")
