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
## ## Where the eye is, and why it stopped being the mass lump
##
## It was `KartBody.driver_head_position()`, which serves `chassis.h`'s *"driver
## head and helmet"* lump at (0.000, 0.560, 0.128). The argument for that was
## explicit and was right when it was made: an eye offset would have been a
## dimension invented from memory, which §5 item 10 forbids, and the lump center was
## the only figure with any derivation behind it.
##
## **That premise is gone and the number was wrong by 371 mm.** Measured off the
## `kart.glb` the scene actually renders — `tools/verify/camera_probe.gd --case=eye`:
##
##     CockpitCamera eye (was)   (0.0000, 0.5600, 0.1280)
##     driver_helmet   bbox      z 0.2840 .. 0.6240,  y 0.5880 .. 0.8880
##     driver_helmet   center    (0.0000, 0.7380, 0.4540)
##
## So the camera sat **334 mm in front of the driver's head and 178 mm below it** —
## out in the air over his thighs, between the steering wheel and his chest. It is
## not a viewpoint anybody chose; it is a mass lump, and a mass lump is allowed to
## be a centroid because nothing looks out of it. Issue #195, and the geometry half
## of #203.
##
## The replacement is **sourced and is not this file's invention**:
## `docs/KART_SPEC.md` §60.1.4's hard-point table publishes the eye at spec
## (±32, −462, 757), derived from the NASA *Anthropometric Source Book* segment
## table walked along the 25° torso axis from the Tillett T11 ML seat's own hip
## point. Spec millimeters map to this file's meters as `y = spec_z / 1000` and
## `z = -spec_y / 1000`, which is `EYE`'s two nonzero components below. The lateral
## ±32 is half an interpupillary distance and a single camera goes between the two
## eyes, so x is 0.
##
## **Two things this deliberately does not do.** It does not adopt #203's further
## 57 mm forward correction — §60.1.4's eye lands on the helmet's own fore-aft
## mid-plane by construction and a real eye sits about 100 mm behind a full-face
## shell, which measures here as 157 mm behind the shell's front face at eye height
## (z 0.3050 on the centerline). That is a **spec** decision about which of two rows
## in §60 is normative, exactly as #203 item 2 words it, and a camera file must not
## fork a published dimension to settle it. And it does not move the audio listener,
## which is `KartRig`'s and is issue #160's; ADR-0039 measured 20.7 dB of level swing
## over listener range, so that move is a mix change and belongs with whoever
## re-measures the mix.
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
##
## **A scene that assigns a `CameraAttributesPhysical` overwrites this** and `far`
## and `fov` with the physical lens's own frustum — issue #237, and the shipped
## 35 mm default is 0.05 / 4000 / 37.8493. `_process` rewrites `fov` every frame so
## that one heals itself; `near` survives only because 0.05 is also the resource's
## default, which is luck and not design. Measured by `camera_probe.gd --case=attributes`.
const NEAR_PLANE := 0.05

## The eye, in the body frame. See the header for where it comes from and for the
## 371 mm it moved.
##
## `docs/KART_SPEC.md` §60.1.4, spec (±32, −462, 757) millimeters. Held as a
## constant rather than served from the solver because the solver has no eye: the
## nearest thing it has is a 6.2 kg lump whose center is 334 mm in front of one.
const EYE := Vector3(0.0, 0.757, 0.462)

## How far the cockpit view is tilted down from the kart's own forward, radians.
##
## A driver does not look at the horizon. Fixing #195 put the eye where it belongs
## and immediately exposed the second half of the framing problem: aimed level, the
## horizon sat at **48% of frame height** and half the screen was sky, with the
## driver's own kart reduced to a sliver of wheel rim. The reference onboard --
## `docs/REFERENCES.md`, the Commons clip recorded there -- puts its horizon at
## **21%** and gives its lower ~45% to kart, wheel and hands. That lower half is
## where a cockpit view gets its speed from: the near road is what moves, and the
## edge flow figures say so (0.731 m nearest visible road at 140 km/h against
## 1.065 for the chase rig).
##
## 8 degrees is the middle of the 6-10 range measured against that reference, and
## it is Anthony's pick. It is a real trade and not a free win -- every degree down
## is a degree less sight into a corner, which matters most at exactly the moment a
## driver wants it. Judged from the seat, not from a still.
const PITCH_DOWN := deg_to_rad(8.0)

## The driver's own head, which a camera inside his skull must not render.
##
## **There is no eye point anywhere in a real head that is outside a closed helmet
## shell**, so this is not a symptom of `EYE` being wrong — it is a consequence of
## `EYE` being right, and every sim solves it the same way. The mesh is a watertight
## loft with no eye port cut in it (KART_SPEC §60.1.5 says so and gives the reason:
## an aperture would not be the same shape at both densities), and Blender materials
## here default to `use_backface_culling = false`, so without this the cockpit view
## is the inside of a helmet liner lit from nowhere — which is exactly the *"the
## cockpit-camera cell renders as a dark blank"* #203 opens with.
##
## Done with a render layer rather than by hiding the node, because hiding is
## global and there are up to four cameras alive in a driveable scene: the chase rig
## has to keep seeing the helmet, and it is the same helmet. A layer nobody else
## uses plus one cleared bit in this camera's `cull_mask` is per-camera by
## construction and needs no state to be kept in step.
const HEAD_PARTS: PackedStringArray = ["driver_helmet", "driver_helmet_visor"]

## Render layer for `HEAD_PARTS`, 1-based as the inspector counts them. 20 is the
## last of Godot's twenty and nothing else in this project sets `layers` at all —
## every other `VisualInstance3D` is on the default layer 1.
const HEAD_LAYER := 20

## Three of ARCHITECTURE §18's comfort settings land on this rig, and it is the
## rig §18 names them for: "cockpit view makes several of these load-bearing rather
## than optional".
##
##   * `fov_trim_deg` moves both FOV endpoints. A trim rather than an absolute
##     because this rig and the chase rig are tuned 12 degrees apart on purpose and
##     one number cannot be both; `src/core/settings.h` has the argument.
##   * `head_motion` scales how far the view lags the chassis. At 0.0 the eye is
##     rigid — the camera bolted to a frame rail this file's header describes —
##     and at 2.0 the neck is twice as slack. §18 asks for "including full off".
##   * `horizon_lock` drops the chassis roll out of the aim entirely, which is the
##     option §18 names for this rig specifically.
##
## The constants above are unchanged and are what a trim of 0.0, a motion scale of
## 1.0 and horizon lock off produce. `chase_camera.gd`'s header has the rule about
## when the stored file is read at all, and `comfort_settings()` below is the same
## function for the same reason.
var fov_trim_deg := 0.0
var head_motion := 1.0
var horizon_lock := false

## The command-line proposal levers, exactly as `chase_camera.gd` carries them and
## for the same reason. Each defaults to the constant it shadows, so a run that
## names none of them is the shipped rig.
var fov_static := FOV_STATIC
var fov_at_speed := FOV_AT_SPEED
var eye := EYE
var pitch_down := PITCH_DOWN

var camera: Camera3D

var _target: KartBody
var _eye := EYE
var _aim := Quaternion.IDENTITY
var _looking_back := false


## Take the comfort settings off a loaded `KartSettings`.
func apply_comfort(settings: Object) -> void:
	if settings == null:
		return
	fov_trim_deg = float(settings.get_field_of_view_deg())
	head_motion = float(settings.get_head_motion())
	horizon_lock = bool(settings.is_horizon_lock())
	if camera != null:
		camera.fov = fov_static + fov_trim_deg


## See `chase_camera.gd`'s copy for why this guard lives in each consumer and why
## the argument list it tests is `assist_settings.gd`'s constant rather than a
## second one.
## The command-line overrides. `--cockpit-eye=x,y,z` is in body-frame meters and
## `--cockpit-fov-static` / `--cockpit-fov-speed` move the two FOV endpoints, so a
## proposal about the driver's viewpoint is an argument on a `shoot.sh` line rather
## than an edit somebody has to undo. Defaults are the constants.
func apply_arguments(args: Dictionary) -> void:
	fov_static = Cmdline.as_float(args, "cockpit-fov-static", FOV_STATIC)
	fov_at_speed = Cmdline.as_float(args, "cockpit-fov-speed", FOV_AT_SPEED)
	# In DEGREES on the command line and radians in the field, because every other
	# angle a person types at this project is in degrees and a lone radian argument
	# is a still nobody can reproduce from its own caption.
	pitch_down = deg_to_rad(
		Cmdline.as_float(args, "cockpit-pitch", rad_to_deg(PITCH_DOWN)))
	var text := Cmdline.as_string(args, "cockpit-eye", "")
	if text.is_empty():
		return
	var parts := text.split(",")
	if parts.size() != 3:
		push_warning("cockpit_camera: --cockpit-eye wants three numbers, got: " + text)
		return
	eye = Vector3(float(parts[0]), float(parts[1]), float(parts[2]))


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


func _ready() -> void:
	apply_arguments(Cmdline.parse())
	apply_comfort(comfort_settings())

	camera = Camera3D.new()
	camera.name = "CockpitCamera3D"
	camera.fov = fov_static + fov_trim_deg
	camera.near = NEAR_PLANE
	# Everything except the driver's own head. See `HEAD_PARTS`.
	camera.cull_mask &= ~(1 << (HEAD_LAYER - 1))
	add_child(camera)


## Point the rig at a kart.
##
## The eye is `EYE` and not something read off the kart, which is the change #195
## asked for: the solver publishes a head *lump*, and a lump center is 334 mm in
## front of an eye. Moving the head parts onto their own render layer happens here
## because this is the first moment the rig has a kart to find them under, and it is
## idempotent — a second call sets the same bits.
func set_target(kart: KartBody) -> void:
	_target = kart
	if kart == null:
		return
	_eye = eye
	_aim = kart.global_transform.basis.get_rotation_quaternion()
	for part: String in HEAD_PARTS:
		var node := kart.find_child(part, true, false) as VisualInstance3D
		if node == null:
			# Not fatal and not silent. A kart built without a driver is a legitimate
			# scene — `kartview.tscn` has shipped one — and the only cost is that a
			# camera inside a head that is not there culls nothing.
			push_warning("cockpit_camera: no %s under the kart; nothing to cull" % part)
			continue
		node.layers = 1 << (HEAD_LAYER - 1)


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
	if horizon_lock:
		# §18's horizon lock: keep the kart's heading, throw away its roll and pitch.
		# Rebuilt from the flattened forward rather than by zeroing an Euler angle,
		# because an Euler decomposition of a rolled-and-pitched basis puts some of
		# the roll into the yaw term and the view drifts sideways over a lap.
		var forward := -basis.z
		var flat := Vector3(forward.x, 0.0, forward.z)
		if flat.length_squared() > 0.0001:
			basis = Basis.looking_at(flat.normalized(), Vector3.UP)
	if _looking_back:
		basis = basis.rotated(basis.y.normalized(), PI)
	# Pitched down about the camera's OWN right axis, after the look-back rotation
	# and after the horizon lock, so it survives both and stays a constant tilt of
	# the head rather than something the roll term can cancel. See `PITCH_DOWN`.
	if pitch_down != 0.0:
		basis = basis.rotated(basis.x.normalized(), -pitch_down)
	var target_aim := basis.get_rotation_quaternion()

	# Half-life smoothing rather than a fixed lerp factor, so the rig behaves the
	# same at 60 and at 240 Hz. `chase_camera.gd` uses the same form and the same
	# argument; a frame-rate-dependent camera is a tuning judgement that changes
	# with the machine it was judged on.
	#
	# **§18's head-motion slider scales the half-life, which is the lag itself.**
	# The lag *is* the head motion here: the eye is rigid and only the direction is
	# damped, so a longer half-life is a slacker neck and a shorter one is a head
	# bolted to the frame. At 0.0 the half-life is zero and the slerp snaps, which
	# is "full off" done exactly rather than approached.
	var half_life := AIM_HALF_LIFE * head_motion
	var alpha := 1.0 if half_life <= 0.0 else 1.0 - pow(0.5, delta / half_life)
	_aim = _aim.slerp(target_aim, clampf(alpha, 0.0, 1.0))
	camera.global_transform = Transform3D(Basis(_aim), camera.global_position)

	# Speed reads as field of view, which ARCHITECTURE.md §5 item 6 calls one of the
	# primary cues and is the cheapest of them. Linear in speed rather than in
	# kinetic energy: what is being conveyed is how fast the road is arriving.
	var speed := _target.linear_velocity.length()
	var kick := clampf(speed / FOV_REFERENCE_SPEED, 0.0, 1.0)
	# The trim moves both endpoints so the 18-degree kick survives wherever the
	# player put the base. Same arithmetic as the chase rig, deliberately.
	camera.fov = lerpf(fov_static, fov_at_speed, kick) + fov_trim_deg


## Take over from whichever camera is current.
func make_current() -> void:
	if camera != null:
		camera.current = true
