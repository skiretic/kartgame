class_name CockpitCamera
extends Node3D

## The driver's-eye rig. The other half of `chase_camera.gd`.
##
## Asked for directly: third person is hard to drive from. That is not only a
## preference — ARCHITECTURE.md §5 item 6 makes speed cues a first-class concern,
## and a cockpit view puts the horizon, the steering wheel and the road edge where
## a driver actually reads them, at the cost of the kart's own attitude being
## invisible.
##
## **Cameras read sim state and never write it** — ARCHITECTURE.md §7's rule, the
## same one `chase_camera.gd` carries. This only ever reads a transform and
## `linear_velocity`.
##
## ## Where the eye is, and why it is not a number in this file
##
## `KartBody.driver_head_position()` serves it from `chassis.h`'s lump table, which
## puts "driver head and helmet" at (0.000, 0.560, 0.128). That position is
## **calibrated rather than measured** — the lump table's own header explains that
## the driver's fore-aft position is solved for from anthropometric segment
## fractions because issue #107 leaves the seat geometry untrustworthy — so it will
## move when #107 closes, and serving it means this rig moves with it.
##
## It is the center of the helmet box and not an eye point. An eye offset would be
## a dimension invented from memory, which §5 item 10 forbids; the head center is
## a few centimeters off for a camera and exactly right for the listener that sits
## at the same place, and the listener is the load-bearing use (issue #160).
##
## ## Why the camera is not simply parented to the body
##
## A kart has no suspension. Parenting a camera rigidly to the chassis puts every
## curb strike and every kerb ripple straight into the view at full amplitude,
## which reads as a broken camera rather than as a rough surface. A driver's head
## is isolated from the chassis by a neck, and the cheapest honest model of that is
## a rotation that follows the body with a short lag: the eye position is rigid,
## the eye *direction* is damped.
##
## Short on purpose. At a long half-life the horizon swims and the rig reads as
## drunk; at zero it reads as a camera bolted to a frame rail, which is what it
## literally is.
const AIM_HALF_LIFE := 0.055

## FOV at rest and at the top of the speed range, degrees, and the speed the kick
## is scaled against.
##
## Wider than `chase_camera.gd`'s 62-78 at both ends. A chase camera already shows
## the kart moving through the world, which is a speed cue by itself; a cockpit
## view has only the road going past, so it needs more peripheral field to read as
## fast at all. The reference speed is the same §6.4 top-speed figure the chase rig
## uses, so the two rigs kick against the same number rather than two.
const FOV_STATIC := 74.0
const FOV_AT_SPEED := 92.0
const FOV_REFERENCE_SPEED := 38.9  # 140 km/h, the §6.4 top-speed figure

## Near plane, meters. Tight, because the steering wheel and the driver's own
## bodywork are within half a meter of the eye and a default near plane clips
## through them.
const NEAR_PLANE := 0.05

var camera: Camera3D

var _target: KartBody
var _eye := Vector3(0.0, 0.56, 0.13)
var _aim := Quaternion.IDENTITY
var _looking_back := false


func _ready() -> void:
	camera = Camera3D.new()
	camera.name = "CockpitCamera3D"
	camera.fov = FOV_STATIC
	camera.near = NEAR_PLANE
	add_child(camera)


## Point the rig at a kart, and read the eye position out of the solver's own
## lump table rather than holding a copy of it.
func set_target(kart: KartBody) -> void:
	_target = kart
	if kart != null:
		_eye = kart.driver_head_position()
		_aim = kart.global_transform.basis.get_rotation_quaternion()


## Look back over the shoulder, for the same key the chase rig uses.
##
## A cockpit driver looking back is a head turn, so this is applied to the aim and
## not to the eye: the position stays where the head is and only the direction
## flips. On the chase rig the equivalent moves the whole camera, which is why the
## two rigs cannot share the implementation.
func set_looking_back(looking_back: bool) -> void:
	_looking_back = looking_back


func _process(delta: float) -> void:
	if _target == null or camera == null:
		return

	# Position is rigid and aim is damped -- see the header. The eye offset is in
	# the body frame, so `to_global` carries the kart's own rotation with it and the
	# head goes where the head goes even while the view is still catching up.
	camera.global_position = _target.to_global(_eye)

	var basis := _target.global_transform.basis
	if _looking_back:
		basis = basis.rotated(basis.y.normalized(), PI)
	var target_aim := basis.get_rotation_quaternion()

	# Half-life smoothing rather than a fixed lerp factor, so the rig behaves the
	# same at 60 and at 240 Hz. `chase_camera.gd` uses the same form and the same
	# argument; a frame-rate-dependent camera is a tuning judgement that changes
	# with the machine it was judged on.
	var alpha := 1.0 - pow(0.5, delta / AIM_HALF_LIFE)
	_aim = _aim.slerp(target_aim, clampf(alpha, 0.0, 1.0))
	camera.global_transform = Transform3D(Basis(_aim), camera.global_position)

	# Speed reads as field of view, which ARCHITECTURE.md §5 item 6 calls one of the
	# primary cues and is the cheapest of them. Linear in speed rather than in
	# kinetic energy: what is being conveyed is how fast the road is arriving.
	var speed := _target.linear_velocity.length()
	var kick := clampf(speed / FOV_REFERENCE_SPEED, 0.0, 1.0)
	camera.fov = lerpf(FOV_STATIC, FOV_AT_SPEED, kick)


## Take over from whichever camera is current.
func make_current() -> void:
	if camera != null:
		camera.current = true
