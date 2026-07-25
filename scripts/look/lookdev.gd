extends Node3D

## The M1 look-development scene, built in code rather than in the editor.
##
## Everything here exists to answer one question at a time and to answer it the
## same way twice. A scene assembled by hand in the editor drifts — a light gets
## nudged, a camera gets moved, and the still that looked right last week cannot
## be reproduced. Building it from parameters means a render is fully described
## by the command line that produced it.
##
## Arguments (all optional, all after a bare `--`):
##
##   --speed=80          camera speed along the road, km/h
##   --yaw_rate=45       camera yaw rate, degrees per second — a kart corners at
##                       roughly this, and rotation is the case that separates a
##                       working motion blur from one that only handles driving
##                       in a straight line
##   --object_speed=80   speed of the box crossing the camera's view, km/h
##   --fps=60            nominal frame rate the motion is stepped at, see below
##   --blur=true         motion blur on or off
##   --shutter=180       motion blur shutter angle, degrees
##   --blur_samples=16   taps along the blur vector
##   --blur_debug=0      0 final image, 1 velocity vectors, 2 blur length
##   --sun_elevation=50  degrees above the horizon
##   --sun_azimuth=-35   degrees, 0 is straight down the road
##   --sun_lux=100000    sun illuminance; ~100,000 lx is a clear midday sun
##   --aperture=16       f-number
##   --shutter_speed=100 denominator, so 100 means 1/100 s
##   --iso=100           sensor sensitivity
##   --ground=asphalt    asphalt (the CC0 photoscan) or checker (the scale grid)
##   --fov=70            camera field of view
##   --taa=true
##
## **Motion is stepped at a fixed nominal frame rate, not at the real one.** A
## still captured on frame 32 of a real-time run has whatever blur the machine's
## actual frame time happened to produce, so the same command yields a different
## image on a busy machine. Advancing by `1.0 / fps` per rendered frame instead
## makes the per-frame motion — and therefore the length of every motion vector
## the blur reads — identical on every run. The cost is that the scene does not
## play back in real time, which does not matter for a still.

const KMH_TO_MS := 1.0 / 3.6

# ARCHITECTURE.md §5: a kart is ~1.05 m wheelbase, ~1.4 m track width, ~0.28 m
# tall at the frame. The reference geometry below is built from these so that
# "does the scale read correctly" is a question about the picture rather than
# about what the numbers were supposed to be.
const KART_TRACK_WIDTH := 1.40
const KART_WHEELBASE := 1.05
const KART_FRAME_HEIGHT := 0.28
const DRIVER_EYE_HEIGHT := 0.62

## Big enough that its edge is not mistaken for the horizon. At 400 m the edge
## sat 200 m from the camera and read as a hard line where ground met sky, which
## is a strong tell and nothing to do with the material on it.
const GROUND_SIZE := 2000.0
## Posts stop well before the ground does; they are a speed and blur reference,
## not scenery, and 400 of them is just cost.
const POST_RANGE := 300.0
const POST_SPACING := 5.0
const ROAD_HALF_WIDTH := 3.2

## The ground materials and the texel density they are derived from live in
## LookEnv, shared with the M2 kart turntable. See scripts/look/look_env.gd.

var speed_ms := 0.0
var yaw_rate_degrees := 0.0
var object_speed_ms := 0.0
var nominal_step := 1.0 / 60.0

var _camera: Camera3D
var _crossing_box: Node3D
var _crossing_travel := 16.0
var _elapsed := 0.0
var _caption: Label


func _ready() -> void:
	var args := Cmdline.parse()

	speed_ms = Cmdline.as_float(args, "speed", 0.0) * KMH_TO_MS
	yaw_rate_degrees = Cmdline.as_float(args, "yaw_rate", 0.0)
	object_speed_ms = Cmdline.as_float(args, "object_speed", 0.0) * KMH_TO_MS
	nominal_step = 1.0 / maxf(Cmdline.as_float(args, "fps", 60.0), 1.0)

	_build_environment(args)
	_build_sun(args)
	_build_ground(args)
	_build_reference_geometry()
	_build_roadside_posts()
	_build_crossing_box()
	_build_reflection_probe(args)
	_build_camera(args)
	_build_caption(args)


func _process(_delta: float) -> void:
	_elapsed += nominal_step

	# -Z is forward (ARCHITECTURE.md §11, glTF import convention).
	_camera.position -= _camera.global_transform.basis.z * (speed_ms * nominal_step)
	_camera.rotate_y(deg_to_rad(yaw_rate_degrees) * nominal_step)

	if is_instance_valid(_crossing_box) and object_speed_ms > 0.0:
		var span := _crossing_travel * 2.0
		var distance := fposmod(object_speed_ms * _elapsed, span)
		_crossing_box.position.x = -_crossing_travel + distance


# --- construction ----------------------------------------------------------


## The environment now comes from `LookEnv`, shared with every other look scene.
##
## It was inline here through M1, which is how three ambient double-counting bugs
## got three separate chances to happen. A second scene needing the same lighting
## (the M2 kart turntable) would have been a fourth. `scripts/look/look_env.gd`
## holds the values and the reasoning; this scene keeps only what is specific to
## an asphalt plane with a camera driving down it.
##
## Verified pixel-neutral: the still this produces is byte-identical to the one
## taken immediately before the extraction.
func _build_environment(args: Dictionary) -> void:
	var world_environment := WorldEnvironment.new()
	world_environment.environment = LookEnv.environment(args)
	world_environment.compositor = _build_compositor(args)
	add_child(world_environment)


func _build_compositor(args: Dictionary) -> Compositor:
	if not Cmdline.as_bool(args, "blur", true):
		return null

	var blur := MotionBlurEffect.new()
	blur.shutter_angle_degrees = Cmdline.as_float(args, "shutter", 180.0)
	blur.samples = Cmdline.as_int(args, "blur_samples", 16)
	blur.max_blur_pixels = Cmdline.as_float(args, "max_blur_pixels", 64.0)
	blur.debug_mode = Cmdline.as_int(args, "blur_debug", 0)

	var compositor := Compositor.new()
	compositor.compositor_effects = [blur]
	return compositor


func _build_sun(args: Dictionary) -> void:
	add_child(LookEnv.sun(args))


func _build_ground(args: Dictionary) -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(GROUND_SIZE, GROUND_SIZE)
	# Subdivision costs nothing here and gives LightmapGI (issue #6) something
	# to attach texels to; one quad the size of the world bakes to one texel.
	plane.subdivide_width = 80
	plane.subdivide_depth = 80

	var ground := MeshInstance3D.new()
	ground.mesh = plane
	if Cmdline.as_string(args, "ground", "asphalt") == "checker":
		ground.material_override = LookEnv.checker_material(GROUND_SIZE)
	else:
		ground.material_override = LookEnv.asphalt_material(GROUND_SIZE)
	ground.name = "Ground"
	add_child(ground)


## A 1 m cube and a kart-sized box, both sitting on the ground.
##
## The acceptance gate for issue #3 is "scale verified against the reference
## cube". A cube alone only proves the cube is right; a kart-sized box next to it
## is what makes a wrong camera height or a wrong road width visible.
func _build_reference_geometry() -> void:
	var cube := MeshInstance3D.new()
	cube.mesh = BoxMesh.new()
	(cube.mesh as BoxMesh).size = Vector3.ONE
	cube.position = Vector3(2.0, 0.5, 6.0)
	cube.material_override = _flat_material(Color(0.55, 0.16, 0.13))
	cube.name = "ReferenceCube1m"
	add_child(cube)

	var kart_box := MeshInstance3D.new()
	kart_box.mesh = BoxMesh.new()
	(kart_box.mesh as BoxMesh).size = Vector3(
		KART_TRACK_WIDTH, KART_FRAME_HEIGHT, KART_WHEELBASE
	)
	kart_box.position = Vector3(-2.0, KART_FRAME_HEIGHT * 0.5, 6.0)
	kart_box.material_override = _flat_material(Color(0.18, 0.28, 0.42))
	kart_box.name = "KartFootprint"
	add_child(kart_box)


func _build_roadside_posts() -> void:
	var post_mesh := BoxMesh.new()
	post_mesh.size = Vector3(0.12, 1.0, 0.12)
	var material := _flat_material(Color(0.72, 0.70, 0.66))

	var posts := Node3D.new()
	posts.name = "RoadsidePosts"
	add_child(posts)

	var z := 40.0
	while z > -POST_RANGE:
		for side in [-1.0, 1.0]:
			var post := MeshInstance3D.new()
			post.mesh = post_mesh
			post.material_override = material
			post.position = Vector3(side * ROAD_HALF_WIDTH, 0.5, z)
			posts.add_child(post)
		z -= POST_SPACING


## A box crossing the camera's view left to right.
##
## Camera motion and object motion are different cases for a motion blur pass:
## camera motion gives every pixel on screen a velocity, while object motion
## gives velocity to a silhouette against a still background — which is exactly
## where a naive gather blur fails, because it cannot smear a moving object out
## past its own edge. Having both in one scene means the failure is visible
## rather than inferred.
func _build_crossing_box() -> void:
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	(box.mesh as BoxMesh).size = Vector3(0.5, 0.5, 0.5)
	box.material_override = _flat_material(Color(0.85, 0.68, 0.13))
	box.name = "CrossingBox"
	box.position = Vector3(-_crossing_travel, 0.6, 12.0)
	add_child(box)
	_crossing_box = box


## Local specular, sized for a road scene. The probe's own reasoning — including
## why its ambient is disabled — is in `LookEnv.reflection_probe`.
func _build_reflection_probe(args: Dictionary) -> void:
	if not Cmdline.as_bool(args, "probe", true):
		return
	add_child(LookEnv.reflection_probe(
		Vector3(80.0, 20.0, 120.0), Vector3(0.0, 8.0, 0.0), 400.0))


func _build_camera(args: Dictionary) -> void:
	_camera = Camera3D.new()
	_camera.fov = Cmdline.as_float(args, "fov", 70.0)
	# ARCHITECTURE.md §7 calls for 0.02–0.05 m once cockpit geometry exists.
	# Depth precision at that near plane is validated in M4.
	_camera.near = 0.05
	_camera.far = 1000.0
	# Standing back is the wrong default for judging a surface. The close
	# position is where a kart driver's eye actually is relative to the asphalt,
	# and near-field detail is what decides whether a material reads.
	_camera.position = Vector3(
		0.0,
		Cmdline.as_float(args, "camera_height", DRIVER_EYE_HEIGHT),
		Cmdline.as_float(args, "camera_z", 30.0),
	)
	_camera.name = "LookDevCamera"
	_camera.attributes = _camera_attributes(args)
	add_child(_camera)

	var viewport := get_viewport()
	viewport.msaa_3d = Viewport.MSAA_DISABLED

	# ARCHITECTURE.md §4 lists "TAA, plus FSR2 upscaling on weaker targets". They
	# are alternatives rather than a stack: FSR2 is itself a temporal resolve, so
	# it does the job TAA would have done and running both would resolve twice.
	# --upscale picks which, and TAA is only applied when nothing else is
	# temporally resolving. See ADR-0021.
	var upscale := Cmdline.as_string(args, "upscale", "off")
	match upscale:
		"fsr2":
			viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
			# 0.67 is FSR "Quality" — 1080p rendered from 720p.
			viewport.scaling_3d_scale = Cmdline.as_float(args, "render_scale", 0.67)
			viewport.use_taa = false
		"bilinear":
			viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
			viewport.scaling_3d_scale = Cmdline.as_float(args, "render_scale", 0.67)
			viewport.use_taa = Cmdline.as_bool(args, "taa", true)
		_:
			viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
			viewport.scaling_3d_scale = 1.0
			viewport.use_taa = Cmdline.as_bool(args, "taa", true)


## Exposure in f-stop, shutter and ISO. See `LookEnv.camera_attributes`.
func _camera_attributes(args: Dictionary) -> CameraAttributesPhysical:
	return LookEnv.camera_attributes(args)


func _build_caption(args: Dictionary) -> void:
	var layer := CanvasLayer.new()
	add_child(layer)

	_caption = Label.new()
	_caption.add_theme_font_size_override("font_size", 18)
	_caption.add_theme_color_override("font_color", Color.WHITE)
	_caption.add_theme_color_override("font_outline_color", Color.BLACK)
	_caption.add_theme_constant_override("outline_size", 6)
	_caption.position = Vector2(24, 24)
	_caption.text = "\n".join([
		"camera %.0f km/h   yaw %.0f deg/s   object %.0f km/h   nominal %.0f fps" % [
			speed_ms / KMH_TO_MS, yaw_rate_degrees,
			object_speed_ms / KMH_TO_MS, 1.0 / nominal_step,
		],
		"blur %s   shutter %.0f deg   samples %d   debug %d" % [
			"on" if Cmdline.as_bool(args, "blur", true) else "off",
			Cmdline.as_float(args, "shutter", 180.0),
			Cmdline.as_int(args, "blur_samples", 16),
			Cmdline.as_int(args, "blur_debug", 0),
		],
		"taa %s   fov %.0f   ground %s" % [
			"on" if Cmdline.as_bool(args, "taa", true) else "off",
			Cmdline.as_float(args, "fov", 70.0),
			Cmdline.as_string(args, "ground", "asphalt"),
		],
		LookEnv.exposure_caption(args),
	])
	layer.add_child(_caption)


func _flat_material(albedo: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = albedo
	material.roughness = 0.6
	return material
