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

const GROUND_SIZE := 400.0
const POST_SPACING := 5.0
const ROAD_HALF_WIDTH := 3.2

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
	_build_ground()
	_build_reference_geometry()
	_build_roadside_posts()
	_build_crossing_box()
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


func _build_environment(args: Dictionary) -> void:
	var sky_material := PhysicalSkyMaterial.new()
	sky_material.sun_disk_scale = 1.0

	var sky := Sky.new()
	sky.sky_material = sky_material

	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	# ARCHITECTURE.md §4: AgX. The single biggest one-line realism win, because
	# it rolls highlights off instead of clipping them to white.
	environment.tonemap_mode = Environment.TONE_MAPPER_AGX
	environment.tonemap_exposure = 1.0
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_sky_contribution = 1.0
	# GI, AO, fog and probes are issues #6 and #7. Off here so that what a still
	# shows can be attributed to one change at a time.

	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
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
	var sun := DirectionalLight3D.new()
	sun.light_energy = 1.0
	sun.shadow_enabled = true
	# ARCHITECTURE.md §4 asks for four splits with split 0 tight, because the
	# cockpit camera sits ~0.6 m off the ground and self-shadowing there is what
	# sets the tightness. The cockpit does not exist until M4; the intent is
	# recorded here so the value is a decision rather than a default.
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_split_1 = 0.03
	sun.directional_shadow_split_2 = 0.12
	sun.directional_shadow_split_3 = 0.4
	sun.directional_shadow_max_distance = 200.0

	var elevation := Cmdline.as_float(args, "sun_elevation", 50.0)
	var azimuth := Cmdline.as_float(args, "sun_azimuth", -35.0)
	sun.rotation_degrees = Vector3(-elevation, azimuth, 0.0)
	add_child(sun)


func _build_ground() -> void:
	var plane := PlaneMesh.new()
	plane.size = Vector2(GROUND_SIZE, GROUND_SIZE)
	# Subdivision costs nothing here and gives LightmapGI (issue #6) something
	# to attach texels to; one quad the size of the world bakes to one texel.
	plane.subdivide_width = 80
	plane.subdivide_depth = 80

	var ground := MeshInstance3D.new()
	ground.mesh = plane
	ground.material_override = _placeholder_ground_material()
	ground.name = "Ground"
	add_child(ground)


## A 1 m checkerboard with fine grain on top.
##
## Placeholder for the photoscans in issue #4, but deliberately not a flat grey:
## a blur that has nothing to smear looks identical to no blur at all, and a
## surface with no high-frequency detail hides every scale error in the scene.
## One square is exactly one meter, so the reference geometry can be counted
## against the floor.
func _placeholder_ground_material() -> StandardMaterial3D:
	const TEXTURE_SIZE := 256
	const CELLS := 2

	var image := Image.create(TEXTURE_SIZE, TEXTURE_SIZE, false, Image.FORMAT_RGB8)
	var cell_pixels := TEXTURE_SIZE / CELLS
	var noise := RandomNumberGenerator.new()
	noise.seed = 0x6b617274  # "kart" — fixed, so the texture is the same every run
	for y in TEXTURE_SIZE:
		for x in TEXTURE_SIZE:
			var dark := ((x / cell_pixels) + (y / cell_pixels)) % 2 == 0
			var base := 0.16 if dark else 0.26
			var grain := noise.randf_range(-0.025, 0.025)
			var value := clampf(base + grain, 0.0, 1.0)
			image.set_pixel(x, y, Color(value, value, value))

	var material := StandardMaterial3D.new()
	material.albedo_texture = ImageTexture.create_from_image(image)
	material.roughness = 0.55
	material.metallic = 0.0
	# CELLS cells per texture repeat, one meter per cell.
	material.uv1_scale = Vector3(GROUND_SIZE / CELLS, GROUND_SIZE / CELLS, 1.0)
	return material


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
	while z > -GROUND_SIZE * 0.5:
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


func _build_camera(args: Dictionary) -> void:
	_camera = Camera3D.new()
	_camera.fov = Cmdline.as_float(args, "fov", 70.0)
	# ARCHITECTURE.md §7 calls for 0.02–0.05 m once cockpit geometry exists.
	# Depth precision at that near plane is validated in M4.
	_camera.near = 0.05
	_camera.far = 1000.0
	_camera.position = Vector3(0.0, DRIVER_EYE_HEIGHT, 30.0)
	_camera.name = "LookDevCamera"
	add_child(_camera)

	var viewport := get_viewport()
	viewport.use_taa = Cmdline.as_bool(args, "taa", true)
	viewport.msaa_3d = Viewport.MSAA_DISABLED


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
		"taa %s   fov %.0f" % [
			"on" if Cmdline.as_bool(args, "taa", true) else "off",
			Cmdline.as_float(args, "fov", 70.0),
		],
	])
	layer.add_child(_caption)


func _flat_material(albedo: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = albedo
	material.roughness = 0.6
	return material
