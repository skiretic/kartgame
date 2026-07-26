class_name ChaseCamera
extends SpringArm3D

## The chase rig, first pass. ARCHITECTURE.md §7 asks for a spring arm with
## velocity look-ahead, speed-driven FOV, lateral-G roll, and a wall raycast so
## geometry never clips the near plane. Three of those four are here; the fourth
## is free, because `SpringArm3D` *is* the wall raycast.
##
## M4 owns the finished rigs. What this has to do now is let a human judge whether
## the kart drives right, which a fixed camera cannot: without look-ahead and FOV
## kick, speed is invisible, and every M3a tuning judgement would be made against
## a camera artifact rather than against the vehicle.
##
## **Cameras read sim state and never write it** — ARCHITECTURE.md §7's rule. A
## camera that pushes the body it is watching breaks replays, so this only ever
## reads `linear_velocity` and a transform.

## Arm length and height, meters. A kart is 1.83 m long, so 3.4 m back puts the
## whole kart plus a little road in frame at a 70 degree FOV.
const ARM_LENGTH := 3.4
const ARM_HEIGHT := 1.05

## How far ahead of the kart the camera looks, in seconds of travel. Multiplying
## by velocity rather than by a fixed distance is what makes it read as
## anticipation: at 20 km/h it is barely there, at 140 km/h it is 6 m up the road.
const LOOK_AHEAD_SECONDS := 0.42

## FOV at rest and at the top of the speed range, degrees. The kick is a primary
## speed cue (ARCHITECTURE.md §5 item 6) and it is also the cheapest one.
const FOV_STATIC := 62.0
const FOV_AT_SPEED := 78.0
const FOV_REFERENCE_SPEED := 38.9  # 140 km/h, the §6.4 top-speed figure

## Roll from lateral acceleration, radians at 2.5 g — the top of §6.4's
## **transient peak** band, which ADR-0034 split out from the sustained one. The
## peak figure is the right one here: a camera roll is a response to a transient,
## and scaling it against the sustained band would saturate the rig in every
## corner. Small on purpose either way: a rig that rolls with the kart reads as
## drunk rather than as fast.
const ROLL_AT_PEAK_G := deg_to_rad(3.5)

## Smoothing half-lives, seconds. Position is followed harder than aim, because a
## camera that lags in position feels attached by elastic while one that lags in
## aim feels like a head turning.
const POSITION_HALF_LIFE := 0.09
const AIM_HALF_LIFE := 0.16

var camera: Camera3D

var _target: KartBody
var _aim := Vector3.ZERO
var _previous_velocity := Vector3.ZERO
var _roll := 0.0


func _ready() -> void:
	spring_length = ARM_LENGTH
	# The arm collides with the world so the camera never ends up inside a wall.
	# Margin keeps the near plane out of the surface it stops against.
	margin = 0.15
	position = Vector3(0.0, ARM_HEIGHT, 0.0)

	camera = Camera3D.new()
	camera.name = "ChaseCamera3D"
	camera.fov = FOV_STATIC
	camera.near = 0.05
	camera.far = 800.0
	add_child(camera)


func set_target(kart: KartBody) -> void:
	_target = kart
	_aim = kart.global_position
	global_position = kart.global_position


## Driven from `_process`, not `_physics_process`.
##
## The sim runs at 120 Hz and the display does not, so sampling the body once per
## rendered frame is what keeps the camera smooth at any refresh rate. It reads
## the interpolated transform Godot already publishes for rendering; nothing here
## feeds back into physics.
func _process(delta: float) -> void:
	if _target == null:
		return

	var velocity := _target.linear_velocity
	var speed := velocity.length()

	# The rig follows the kart's yaw only. Following its roll and pitch would put
	# the horizon on a gimbal, which ARCHITECTURE.md §7 calls out as unreadable at
	# speed for the cockpit rig and which is no better here.
	var basis_forward := -_target.global_transform.basis.z
	var flat_forward := Vector3(basis_forward.x, 0.0, basis_forward.z)
	if flat_forward.length_squared() < 0.0001:
		flat_forward = Vector3.FORWARD
	flat_forward = flat_forward.normalized()

	var anchor := _target.global_position + Vector3.UP * ARM_HEIGHT
	global_position = global_position.lerp(anchor, _decay(POSITION_HALF_LIFE, delta))

	var look_at_point := _target.global_position + velocity * LOOK_AHEAD_SECONDS
	_aim = _aim.lerp(look_at_point, _decay(AIM_HALF_LIFE, delta))

	# The camera sits behind the kart's *heading*, not behind its velocity —
	# otherwise a slide would swing the rig around to the side and hide the thing
	# worth watching.
	#
	# **`SpringArm3D` places its children along its own +Z, so the arm is aimed
	# down the kart's forward axis and the camera lands behind it.** Aiming the
	# arm backward — the intuitive reading, and what this file did until it was
	# first driven — puts the camera 3.4 m in front of the nose, looking back at
	# the kart. Every still taken so far parked a camera with --eye/--look, so
	# nothing had ever exercised the rig.
	look_at_from_position(global_position, global_position + flat_forward, Vector3.UP)

	# Lateral acceleration measured from the change in velocity rather than read
	# from the body, because Godot exposes no accelerometer. Projected onto the
	# kart's right axis so that only cornering rolls the camera, not braking.
	var acceleration := (velocity - _previous_velocity) / maxf(delta, 0.0001)
	_previous_velocity = velocity
	var right := _target.global_transform.basis.x
	var lateral_g := acceleration.dot(right) / 9.81
	var target_roll := -clampf(lateral_g / 2.5, -1.0, 1.0) * ROLL_AT_PEAK_G
	_roll = lerpf(_roll, target_roll, _decay(0.25, delta))

	camera.look_at_from_position(camera.global_position, _aim, Vector3.UP)
	camera.rotate_object_local(Vector3.FORWARD, _roll)
	camera.fov = lerpf(
		FOV_STATIC, FOV_AT_SPEED, clampf(speed / FOV_REFERENCE_SPEED, 0.0, 1.0)
	)


## Frame-rate independent smoothing.
##
## `lerp(a, b, 0.1)` per frame is a different filter at 60 fps and at 144 fps, and
## the difference shows up as a camera that feels heavier on a slower machine.
## Expressed as a half-life instead, the result is identical at any frame rate.
func _decay(half_life: float, delta: float) -> float:
	return 1.0 - pow(0.5, delta / half_life)
