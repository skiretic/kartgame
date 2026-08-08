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
##
## ## Two of ARCHITECTURE §18's comfort settings land here
##
## `field_of_view_deg` is a trim added to both FOV endpoints and `shake` scales the
## lateral-G roll, both from `user://settings.cfg` via `KartSettings`. The
## constants below stay exactly what they were and are what a trim of 0.0 and a
## scale of 1.0 produce, so a player who never opens the settings screen sees the
## rig unchanged.
##
## **The stored file is skipped whenever the run is scripted**, per the rule
## `assist_settings.gd`'s header sets out at length: `shoot.sh` drives a scene from
## `--throttle`/`--steer`/`--brake` and every still in this project has to be
## reproducible from the command that made it, so a `settings.cfg` nobody mentioned
## must never move a published frame. A FOV trim would move every pixel of one.

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

## How close the aim point may come to the camera before `Vector3.UP` stops being
## a usable roll reference, in meters.
##
## Both of Godot's refusals are **metric, not angular**, which is why this is a
## distance and not an angle. `Node3D.look_at_from_position` errors outright —
## *"Node origin and target are in the same position"* — when the two are within
## `CMP_EPSILON`, 1e-5 m. `Basis.looking_at` then warns — *"Target and up vectors
## are colinear"* — when `up.cross(target - origin)` is componentwise under the
## same 1e-5, and for `Vector3.UP` that cross is `(d.z, 0.0, -d.x)`: the test is
## purely on the **horizontal** offset, at any range.
##
## Measured on 4.7.1 and neither is rate limited — three calls print three lines.
## A millimeter is a hundred times Godot's epsilon and a hundredth of anything a
## camera can resolve, so nothing that clears this is a frame anyone can see.
const AIM_EPSILON := 0.001

var camera: Camera3D

## ARCHITECTURE §18. Degrees added to both FOV endpoints, and a multiplier on the
## roll. These are the defaults `Settings` ships, restated here rather than read at
## construction so a rig built in a test or a still is at the shipped values with
## no file on disk.
var fov_trim_deg := 0.0
var shake := 1.0

var _target: KartBody
var _aim := Vector3.ZERO
var _previous_velocity := Vector3.ZERO
var _roll := 0.0


## Take the comfort settings off a loaded `KartSettings`.
##
## Separate from `_ready` so a caller that already has the settings open — a
## settings screen previewing a change, a scene that loads the file once for the
## kart and the camera together — applies them without a second read of the disk.
func apply_comfort(settings: Object) -> void:
	if settings == null:
		return
	fov_trim_deg = float(settings.get_field_of_view_deg())
	shake = float(settings.get_shake())
	if camera != null:
		camera.fov = FOV_STATIC + fov_trim_deg


func _ready() -> void:
	spring_length = ARM_LENGTH
	# The arm collides with the world so the camera never ends up inside a wall.
	# Margin keeps the near plane out of the surface it stops against.
	margin = 0.15
	position = Vector3(0.0, ARM_HEIGHT, 0.0)

	apply_comfort(comfort_settings())

	camera = Camera3D.new()
	camera.name = "ChaseCamera3D"
	camera.fov = FOV_STATIC + fov_trim_deg
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
	# §18's shake slider scales the amplitude and nothing else: at 0.0 the rig is
	# level and at 2.0 it rolls twice as far, but the response is the same shape and
	# the same time constant, so turning it down is not a different camera.
	var target_roll := -clampf(lateral_g / 2.5, -1.0, 1.0) * ROLL_AT_PEAK_G * shake
	_roll = lerpf(_roll, target_roll, _decay(0.25, delta))

	_aim_camera(flat_forward)
	camera.rotate_object_local(Vector3.FORWARD, _roll)
	# The trim moves both endpoints, so the kick keeps its full 16 degrees of travel
	# wherever the player put the base. Scaling the endpoints instead would make a
	# narrow FOV a flat one.
	camera.fov = lerpf(
		FOV_STATIC, FOV_AT_SPEED, clampf(speed / FOV_REFERENCE_SPEED, 0.0, 1.0)
	) + fov_trim_deg


## Point the camera at `_aim` with a roll reference that is always usable.
##
## ## What was wrong
##
## This was one bare `look_at_from_position(camera.global_position, _aim,
## Vector3.UP)`. `SpringArm3D` collapses its children onto its own origin
## whenever the cast hits at zero distance — a kart reversed into a barrier or
## sat against a tire stack, which is a first-session event — and the origin is
## the kart plus `ARM_HEIGHT`. The aim point is the kart plus
## `velocity * LOOK_AHEAD_SECONDS`, so at a standstill the two share an x and a z
## exactly, the view direction is straight down, and `Vector3.UP` is colinear
## with it. Measured with the arm forced onto its origin: **59 of 60 frames
## warned**, one line each, at `chase_camera.gd:167`. Nothing is rate limited, so
## that is one line per rendered frame for as long as the kart sits there.
##
## ## Why an up vector and not a skip
##
## Skipping the call freezes the rig while the kart is against the wall, which
## reads as a camera that has crashed. Substituting the roll reference keeps the
## camera pointed at exactly the same place in **every** frame — the aim argument
## never changes — and only replaces the one quantity Godot was picking at random
## and warning about.
##
## `flat_forward` is the right replacement because it is the continuous limit.
## For a camera `h` above the kart and `d` behind it, the screen-up direction
## `UP - (UP . dir) dir` is proportional to `(h, d)` in the (horizontal, vertical)
## plane, so as `d -> 0` it tends to the horizontal direction the camera is
## already facing. Taking that limit exactly means the switch happens at a
## sub-millimeter offset, where the two answers agree to well inside a pixel.
##
## The sign follows which way the camera is looking: `to_aim.y < 0` is looking
## down, where the limit is `+flat_forward`.
func _aim_camera(flat_forward: Vector3) -> void:
	var to_aim := _aim - camera.global_position
	# `up.cross(to_aim)` for `Vector3.UP` is `(to_aim.z, 0, -to_aim.x)`, so this is
	# the exact quantity Godot tests, with three orders of magnitude of headroom.
	if Vector2(to_aim.x, to_aim.z).length() >= AIM_EPSILON:
		camera.look_at_from_position(camera.global_position, _aim, Vector3.UP)
		return
	# Straight up or straight down. `flat_forward` is horizontal and unit, so its
	# cross with a vertical `to_aim` has a component of at least `0.707 * |to_aim.y|`
	# and clears the same epsilon.
	if absf(to_aim.y) >= AIM_EPSILON:
		camera.look_at_from_position(
			camera.global_position, _aim, flat_forward * -signf(to_aim.y))
		return
	# The aim point is inside the camera. There is no direction to take from it, so
	# the rig holds the orientation it already had rather than inventing one.


## A loaded `KartSettings`, or null when this run must not read one.
##
## **The skip rule is `assist_settings.gd`'s and the list of arguments is its
## constant**, so there is one owner of what "this run is scripted" means even
## though the four-line guard appears in each comfort consumer. `cockpit_camera.gd`
## and `motion_blur.gd` carry the same function for the same reason: they are
## constructed by three different scenes, none of which this file owns, and a
## capability wired at one end only is the failure `assist_settings.gd` exists
## because of.
##
## Static so a caller with several rigs to configure reads the file once.
static func comfort_settings() -> Object:
	if not ClassDB.class_exists("KartSettings"):
		return null
	var args := Cmdline.parse()
	for key in AssistSettings.SCRIPTED_INPUT_ARGS:
		if args.has(key):
			return null
	var settings := KartSettings.new()
	settings.load()
	return settings


## Frame-rate independent smoothing.
##
## `lerp(a, b, 0.1)` per frame is a different filter at 60 fps and at 144 fps, and
## the difference shows up as a camera that feels heavier on a slower machine.
## Expressed as a half-life instead, the result is identical at any frame rate.
func _decay(half_life: float, delta: float) -> float:
	return 1.0 - pow(0.5, delta / half_life)
